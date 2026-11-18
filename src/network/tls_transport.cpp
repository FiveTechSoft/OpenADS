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
#include <mbedtls/x509.h>
#include <mbedtls/bignum.h>
#include <mbedtls/hkdf.h>
#include <mbedtls/md.h>
#include <mbedtls/platform_util.h>

#include <cstring>
#include <algorithm>
#include <utility>
#include <vector>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#endif

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
        const auto capacity = server_ ? mbedtls_ssl_get_max_out_record_payload(&ssl_) : 0;
        if (capacity < 0) return tls_error(capacity);
        int rc = mbedtls_ssl_write(&ssl_, buf,
            server_ ? std::min(n, static_cast<std::size_t>(capacity)) : n);
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

        // Same small-frame latency policy as connect_tcp. mbedTLS creates
        // this socket directly, bypassing our ordinary socket setup.
        int no_delay = 1;
#ifdef _WIN32
        int socket_rc = setsockopt(net_.fd, IPPROTO_TCP, TCP_NODELAY,
            reinterpret_cast<const char*>(&no_delay), sizeof(no_delay));
#else
        int socket_rc = setsockopt(net_.fd, IPPROTO_TCP, TCP_NODELAY,
            &no_delay, sizeof(no_delay));
#endif
        if (socket_rc != 0)
            return util::Error{openads::AE_REMOTE_ERROR, 0,
                "TLS client TCP_NODELAY setup failed", ""};

        if(cfg.auth_tls13_only) {
#ifdef _WIN32
            DWORD timeout=30000;
            setsockopt(net_.fd,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout));
            setsockopt(net_.fd,SOL_SOCKET,SO_SNDTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout));
#else
            timeval timeout{30,0};
            setsockopt(net_.fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
            setsockopt(net_.fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
#endif
        }
        rc = mbedtls_ssl_config_defaults(&conf_, MBEDTLS_SSL_IS_CLIENT,
                                          MBEDTLS_SSL_TRANSPORT_STREAM,
                                          MBEDTLS_SSL_PRESET_DEFAULT);
        if (rc != 0) return util::Error{openads::AE_REMOTE_ERROR, rc,
            "ssl_config_defaults: " + mbed_msg(rc), ""};

        if (cfg.auth_tls13_only) configure_auth_tls();
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
        handshake_done_ = true;
        return {};
    }

    bool encrypted_ready() const noexcept override { return handshake_done_ && !closed_; }
    // RFC8446 section7.5 exporter, pinned mbedTLS3.6.2 session layout.
    // Never export master/traffic secrets. Caller gets only domain-specific32B.

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
        if (cfg.auth_tls13_only) configure_auth_tls();
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
    void configure_auth_tls() {
        mbedtls_ssl_conf_min_tls_version(&conf_, MBEDTLS_SSL_VERSION_TLS1_3);
        mbedtls_ssl_conf_max_tls_version(&conf_, MBEDTLS_SSL_VERSION_TLS1_3);
        mbedtls_ssl_conf_tls13_key_exchange_modes(&conf_, MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_EPHEMERAL);
#if defined(MBEDTLS_SSL_SESSION_TICKETS)
        mbedtls_ssl_conf_session_tickets(&conf_, MBEDTLS_SSL_SESSION_TICKETS_DISABLED);
#endif
#if defined(MBEDTLS_SSL_SRV_C) && defined(MBEDTLS_SSL_PROTO_TLS1_3)
        mbedtls_ssl_conf_new_session_tickets(&conf_, 0);
#endif
    }
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

// Fresh ephemeral identity at startup. Authentication is deliberately absent.
bool auth_random_bytes(std::uint8_t* out, std::size_t n) {
    mbedtls_entropy_context e; mbedtls_ctr_drbg_context d;
    mbedtls_entropy_init(&e);mbedtls_ctr_drbg_init(&d);
    int rc=mbedtls_ctr_drbg_seed(&d,mbedtls_entropy_func,&e,nullptr,0);
    if(rc==0)rc=mbedtls_ctr_drbg_random(&d,out,n);
    mbedtls_ctr_drbg_free(&d);mbedtls_entropy_free(&e);return rc==0;
}
util::Result<TlsConfig> make_auth_tls_config() {
    mbedtls_entropy_context e;mbedtls_ctr_drbg_context d;mbedtls_pk_context k;
    mbedtls_x509write_cert c;mbedtls_mpi serial;
    mbedtls_entropy_init(&e);mbedtls_ctr_drbg_init(&d);mbedtls_pk_init(&k);
    mbedtls_x509write_crt_init(&c);mbedtls_mpi_init(&serial);
    TlsConfig cfg;cfg.auth_tls13_only=true;
    int rc=mbedtls_ctr_drbg_seed(&d,mbedtls_entropy_func,&e,nullptr,0);
    if(rc==0)rc=mbedtls_pk_setup(&k,mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
    if(rc==0)rc=mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1,mbedtls_pk_ec(k),mbedtls_ctr_drbg_random,&d);
    if(rc==0)rc=mbedtls_mpi_fill_random(&serial,16,mbedtls_ctr_drbg_random,&d);
    if(rc==0)rc=mbedtls_x509write_crt_set_serial(&c,&serial);
    mbedtls_x509write_crt_set_version(&c,MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&c,MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&c,&k);mbedtls_x509write_crt_set_issuer_key(&c,&k);
    if(rc==0)rc=mbedtls_x509write_crt_set_subject_name(&c,"CN=OpenADS unverified auth spike");
    if(rc==0)rc=mbedtls_x509write_crt_set_issuer_name(&c,"CN=OpenADS unverified auth spike");
    if(rc==0)rc=mbedtls_x509write_crt_set_validity(&c,"20200101000000","20991231235959");
    unsigned char pem[8192]{};
    if(rc==0)rc=mbedtls_x509write_crt_pem(&c,pem,sizeof(pem),mbedtls_ctr_drbg_random,&d);
    if(rc==0)cfg.cert_pem=reinterpret_cast<char*>(pem);
    if(rc==0)rc=mbedtls_pk_write_key_pem(&k,pem,sizeof(pem));
    if(rc==0)cfg.key_pem=reinterpret_cast<char*>(pem);
    volatile unsigned char* w=pem;for(std::size_t i=0;i<sizeof(pem);++i)w[i]=0;
    mbedtls_x509write_crt_free(&c);mbedtls_mpi_free(&serial);mbedtls_pk_free(&k);
    mbedtls_ctr_drbg_free(&d);mbedtls_entropy_free(&e);
    if(rc!=0)return util::Error{openads::AE_REMOTE_ERROR,rc,"auth TLS identity generation failed",""};
    return cfg;
}

} // namespace openads::network

#endif // OPENADS_WITH_TLS
