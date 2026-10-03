#include "network/tls_transport.h"

#if defined(OPENADS_WITH_TLS)

#include "openads/error.h"

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/pk.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#include <cstring>
#include <algorithm>
#include <utility>

namespace openads::network {

namespace {

inline std::string mbed_msg(int err) {
    char buf[160] = {0};
    mbedtls_strerror(err, buf, sizeof(buf));
    return std::string(buf);
}

class MbedTlsTransport : public ITransport {
public:
    MbedTlsTransport() {
        mbedtls_net_init(&net_);
        mbedtls_ssl_init(&ssl_);
        mbedtls_ssl_config_init(&conf_);
        mbedtls_ctr_drbg_init(&drbg_);
        mbedtls_entropy_init(&entropy_);
        mbedtls_x509_crt_init(&ca_);
        mbedtls_x509_crt_init(&srv_cert_);
        mbedtls_pk_init(&srv_key_);
    }
    ~MbedTlsTransport() override { close(); }
    MbedTlsTransport(const MbedTlsTransport&) = delete;
    MbedTlsTransport& operator=(const MbedTlsTransport&) = delete;

    util::Result<std::size_t>
        send(const std::uint8_t* buf, std::size_t n) override {
        if (server_) {
            if (auto h = advance_handshake(); !h) return h.error();
        }
        // One wire frame can exceed a TLS record. Let the caller's send loop
        // advance through bounded records instead of rejecting large replies.
        const auto capacity = mbedtls_ssl_get_max_out_record_payload(&ssl_);
        if (capacity < 0) return tls_error(capacity);
        int rc = mbedtls_ssl_write(&ssl_, buf,
            std::min(n, static_cast<std::size_t>(capacity)));
        if (server_ && retry(rc)) return blocked(rc);
        if (rc < 0) {
            return util::Error{openads::AE_REMOTE_ERROR, rc,
                "mbedtls_ssl_write: " + mbed_msg(rc), ""};
        }
        if (server_) want_write_ = false;
        return static_cast<std::size_t>(rc);
    }

    util::Result<std::size_t>
        recv(std::uint8_t* buf, std::size_t n) override {
        if (server_) {
            if (auto h = advance_handshake(); !h) return h.error();
        }
        int rc = mbedtls_ssl_read(&ssl_, buf, n);
        if (server_ && retry(rc)) return blocked(rc);
        if (rc == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return std::size_t{0};
        if (rc < 0) {
            return util::Error{openads::AE_REMOTE_ERROR, rc,
                "mbedtls_ssl_read: " + mbed_msg(rc), ""};
        }
        if (server_) want_write_ = false;
        return static_cast<std::size_t>(rc);
    }

    void close() noexcept override {
        if (closed_) return;
        closed_ = true;
        // Teardown cannot block a reactor on a stalled peer.
        if (!server_) (void)mbedtls_ssl_close_notify(&ssl_);
        mbedtls_x509_crt_free(&ca_);
        mbedtls_x509_crt_free(&srv_cert_);
        mbedtls_pk_free(&srv_key_);
        mbedtls_ssl_free(&ssl_);
        mbedtls_ssl_config_free(&conf_);
        mbedtls_ctr_drbg_free(&drbg_);
        mbedtls_entropy_free(&entropy_);
        mbedtls_net_free(&net_);
    }

    bool valid() const noexcept override { return !closed_; }

    util::Result<void>
        client_handshake(const std::string& host, std::uint16_t port,
                         const TlsConfig& cfg) {
        const std::string portstr = std::to_string(port);
        int rc = mbedtls_ctr_drbg_seed(&drbg_, mbedtls_entropy_func,
                                       &entropy_, nullptr, 0);
        if (rc != 0) return util::Error{openads::AE_REMOTE_ERROR, rc,
            "ctr_drbg_seed: " + mbed_msg(rc), ""};

        rc = mbedtls_net_connect(&net_, host.c_str(), portstr.c_str(),
                                 MBEDTLS_NET_PROTO_TCP);
        if (rc != 0) return util::Error{openads::AE_REMOTE_ERROR, rc,
            "net_connect: " + mbed_msg(rc), host + ":" + portstr};

        rc = mbedtls_ssl_config_defaults(&conf_, MBEDTLS_SSL_IS_CLIENT,
                                          MBEDTLS_SSL_TRANSPORT_STREAM,
                                          MBEDTLS_SSL_PRESET_DEFAULT);
        if (rc != 0) return util::Error{openads::AE_REMOTE_ERROR, rc,
            "ssl_config_defaults: " + mbed_msg(rc), ""};

        if (cfg.insecure_skip_verify) {
            mbedtls_ssl_conf_authmode(&conf_, MBEDTLS_SSL_VERIFY_NONE);
        } else if (cfg.ca_pem.empty()) {
            mbedtls_ssl_conf_authmode(&conf_, MBEDTLS_SSL_VERIFY_REQUIRED);
        } else {
            rc = mbedtls_x509_crt_parse(&ca_,
                reinterpret_cast<const unsigned char*>(cfg.ca_pem.data()),
                cfg.ca_pem.size() + 1);
            if (rc < 0) return util::Error{openads::AE_REMOTE_ERROR, rc,
                "x509_crt_parse(CA): " + mbed_msg(rc), ""};
            mbedtls_ssl_conf_authmode(&conf_, MBEDTLS_SSL_VERIFY_REQUIRED);
            mbedtls_ssl_conf_ca_chain(&conf_, &ca_, nullptr);
        }
        mbedtls_ssl_conf_rng(&conf_, mbedtls_ctr_drbg_random, &drbg_);

        rc = mbedtls_ssl_setup(&ssl_, &conf_);
        if (rc != 0) return util::Error{openads::AE_REMOTE_ERROR, rc,
            "ssl_setup: " + mbed_msg(rc), ""};

        const std::string& sni = cfg.sni_hostname.empty() ? host
                                                          : cfg.sni_hostname;
        rc = mbedtls_ssl_set_hostname(&ssl_, sni.c_str());
        if (rc != 0) return util::Error{openads::AE_REMOTE_ERROR, rc,
            "ssl_set_hostname: " + mbed_msg(rc), ""};

        mbedtls_ssl_set_bio(&ssl_, &net_, mbedtls_net_send,
                            mbedtls_net_recv, nullptr);

        while ((rc = mbedtls_ssl_handshake(&ssl_)) != 0) {
            if (rc != MBEDTLS_ERR_SSL_WANT_READ &&
                rc != MBEDTLS_ERR_SSL_WANT_WRITE) {
                return util::Error{openads::AE_REMOTE_ERROR, rc,
                    "ssl_handshake (client): " + mbed_msg(rc), host};
            }
        }
        return {};
    }

    bool wants_write() const noexcept override { return want_write_; }
    bool buffered_read() const noexcept override {
        return server_ && handshake_done_ && mbedtls_ssl_check_pending(&ssl_) != 0;
    }
    util::Result<void> server_setup(Socket socket, const TlsConfig& cfg) {
        server_ = true;
        socket_ = socket;
        if (cfg.cert_pem.empty() || cfg.key_pem.empty() ||
            cfg.cert_pem.size() > 1024 * 1024 || cfg.key_pem.size() > 1024 * 1024)
            return util::Error{openads::AE_REMOTE_ERROR, 0, "TLS PEM size invalid", ""};
        int rc = mbedtls_ctr_drbg_seed(&drbg_, mbedtls_entropy_func, &entropy_, nullptr, 0);
        if (rc != 0) return tls_error(rc);
        rc = mbedtls_ssl_config_defaults(&conf_, MBEDTLS_SSL_IS_SERVER,
            MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
        if (rc != 0) return tls_error(rc);
        rc = mbedtls_x509_crt_parse(&srv_cert_,
            reinterpret_cast<const unsigned char*>(cfg.cert_pem.c_str()), cfg.cert_pem.size() + 1);
        if (rc != 0) return tls_error(rc);
        rc = mbedtls_pk_parse_key(&srv_key_,
            reinterpret_cast<const unsigned char*>(cfg.key_pem.c_str()), cfg.key_pem.size() + 1,
            nullptr, 0, mbedtls_ctr_drbg_random, &drbg_);
        if (rc != 0) return tls_error(rc);
        rc = mbedtls_pk_check_pair(&srv_cert_.pk, &srv_key_, mbedtls_ctr_drbg_random, &drbg_);
        if (rc != 0) return tls_error(rc);
        mbedtls_ssl_conf_rng(&conf_, mbedtls_ctr_drbg_random, &drbg_);
#if defined(MBEDTLS_SSL_RENEGOTIATION)
        mbedtls_ssl_conf_renegotiation(&conf_, MBEDTLS_SSL_RENEGOTIATION_DISABLED);
#endif
        // This listener authenticates application users through Connect.
        // Client certificates are not required; no insecure server fallback.
        mbedtls_ssl_conf_authmode(&conf_, MBEDTLS_SSL_VERIFY_NONE);
        rc = mbedtls_ssl_conf_own_cert(&conf_, &srv_cert_, &srv_key_);
        if (rc != 0) return tls_error(rc);
        rc = mbedtls_ssl_setup(&ssl_, &conf_);
        if (rc != 0) return tls_error(rc);
        mbedtls_ssl_set_bio(&ssl_, this, bio_send, bio_recv, nullptr);
        return {};
    }
private:
    static util::Error tls_error(int rc) {
        return {openads::AE_REMOTE_ERROR, rc, "TLS server: " + mbed_msg(rc), ""};
    }
    static bool retry(int rc) {
        return rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE;
    }
    util::Error blocked(int rc) {
        want_write_ = rc == MBEDTLS_ERR_SSL_WANT_WRITE;
        return {openads::AE_REMOTE_ERROR, rc, "TLS I/O pending", "transport would block"};
    }
    util::Result<void> advance_handshake() {
        if (handshake_done_) return {};
        int rc = mbedtls_ssl_handshake(&ssl_);
        if (retry(rc)) return blocked(rc);
        if (rc != 0) return tls_error(rc);
        handshake_done_ = true;
        want_write_ = false;
        return {};
    }
    static int bio_send(void* ctx, const unsigned char* buf, std::size_t n) {
        auto* self = static_cast<MbedTlsTransport*>(ctx);
        auto result = sock_send(self->socket_, buf, n);
        if (!result) return socket_recv_would_block(result.error())
            ? MBEDTLS_ERR_SSL_WANT_WRITE : MBEDTLS_ERR_NET_SEND_FAILED;
        return static_cast<int>(result.value());
    }
    static int bio_recv(void* ctx, unsigned char* buf, std::size_t n) {
        auto* self = static_cast<MbedTlsTransport*>(ctx);
        auto result = sock_recv(self->socket_, buf, n);
        if (!result) return socket_recv_would_block(result.error())
            ? MBEDTLS_ERR_SSL_WANT_READ : MBEDTLS_ERR_NET_RECV_FAILED;
        return static_cast<int>(result.value());
    }
    Socket socket_{};
    bool server_ = false;
    bool handshake_done_ = false;
    bool want_write_ = false;
    mbedtls_net_context        net_{};
    mbedtls_ssl_context        ssl_{};
    mbedtls_ssl_config         conf_{};
    mbedtls_ctr_drbg_context   drbg_{};
    mbedtls_entropy_context    entropy_{};
    mbedtls_x509_crt           ca_{};
    mbedtls_x509_crt           srv_cert_{};
    mbedtls_pk_context         srv_key_{};
    bool                       closed_ = false;
};

} // namespace

util::Result<std::unique_ptr<ITransport>>
connect_tls(const std::string& host, std::uint16_t port,
            const TlsConfig& cfg) {
    auto t = std::make_unique<MbedTlsTransport>();
    if (auto r = t->client_handshake(host, port, cfg); !r) {
        return r.error();
    }
    return std::unique_ptr<ITransport>(std::move(t));
}

util::Result<std::unique_ptr<ITransport>> accept_tls(Socket socket, const TlsConfig& cfg) {
    auto transport = std::make_unique<MbedTlsTransport>();
    if (auto result = transport->server_setup(socket, cfg); !result) return result.error();
    return std::unique_ptr<ITransport>(std::move(transport));
}
util::Result<void> validate_tls_server_config(const TlsConfig& cfg) {
    MbedTlsTransport transport;
    return transport.server_setup(Socket{}, cfg);
}

} // namespace openads::network

#endif // OPENADS_WITH_TLS
