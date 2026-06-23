#include "sql_backend/tds_tls_channel.h"

#if defined(OPENADS_WITH_MSSQL)

#include "network/socket.h"
#include "openads/error.h"
#include "sql_backend/mssql_uri.h"
#include "sql_backend/tds_protocol.h"

#include <cstring>
#include <utility>

// Maximum total reassembled TDS payload: 64 MiB.  A hostile or misbehaving
// server cannot grow this buffer beyond the cap; we fail fast instead.
static constexpr std::size_t kMaxReassembly = 64u * 1024u * 1024u;

#if defined(OPENADS_WITH_TLS)
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#endif

namespace openads::sql_backend {

namespace {

#if defined(OPENADS_WITH_TLS)
std::string mbed_msg(int err) {
    char buf[160] = {0};
    mbedtls_strerror(err, buf, sizeof(buf));
    return std::string(buf);
}
#endif

// Send |n| bytes through the raw socket, looping until all are written.
util::Result<void> sock_send_all(network::Socket& s,
                                 const std::uint8_t* buf, std::size_t n) {
    std::size_t sent = 0;
    while (sent < n) {
        auto r = network::sock_send(s, buf + sent, n - sent);
        if (!r) return r.error();
        std::size_t w = r.value();
        if (w == 0) {
            return util::Error{openads::AE_REMOTE_ERROR, 0,
                "socket closed during send", ""};
        }
        sent += w;
    }
    return {};
}

// Receive exactly |n| bytes from the raw socket, looping until filled.
util::Result<void> sock_recv_exact(network::Socket& s,
                                   std::uint8_t* buf, std::size_t n) {
    std::size_t got = 0;
    while (got < n) {
        auto r = network::sock_recv(s, buf + got, n - got);
        if (!r) return r.error();
        std::size_t rd = r.value();
        if (rd == 0) {
            return util::Error{openads::AE_REMOTE_ERROR, 0,
                "socket closed during receive", ""};
        }
        got += rd;
    }
    return {};
}

}  // namespace

// ---------------------------------------------------------------------------
// Impl — owns the socket and (under TLS) the pinned mbedtls contexts.
// ---------------------------------------------------------------------------
struct TdsTlsChannel::Impl {
    network::Socket sock{};

#if defined(OPENADS_WITH_TLS)
    mbedtls_ssl_context      ssl{};
    mbedtls_ssl_config       conf{};
    mbedtls_ctr_drbg_context drbg{};
    mbedtls_entropy_context  entropy{};
    mbedtls_x509_crt         ca{};
    bool                     tls_inited     = false;
    bool                     handshake_done = false;
    // Bytes of the current 0x12 handshake packet not yet consumed by mbedtls.
    std::vector<std::uint8_t> recv_pending;
    std::size_t               recv_pending_pos = 0;
    // During the handshake, mbedtls may emit several TLS records (e.g.
    // ClientKeyExchange + ChangeCipherSpec + Finished) via separate bio_send
    // calls that belong to ONE client flight.  SQL Server expects that flight
    // coalesced into a single 0x12 TDS packet, so we buffer here and flush the
    // whole flight as one packet right before we read the server's reply.
    std::vector<std::uint8_t> send_buf;
    // Negotiated max payload per TDS packet for the encrypted session.
    std::size_t               packet_size = 4096;
#endif

    Impl() = default;
    Impl(const Impl&)            = delete;
    Impl& operator=(const Impl&) = delete;

    ~Impl() { close(); }

    void close() noexcept {
#if defined(OPENADS_WITH_TLS)
        if (tls_inited) {
            mbedtls_ssl_free(&ssl);
            mbedtls_ssl_config_free(&conf);
            mbedtls_ctr_drbg_free(&drbg);
            mbedtls_entropy_free(&entropy);
            mbedtls_x509_crt_free(&ca);
            tls_inited = false;
        }
#endif
        if (sock.valid()) {
            network::sock_close(sock);
        }
    }

#if defined(OPENADS_WITH_TLS)
    // --- Custom BIO: tunnels TLS handshake records inside 0x12 packets. ---
    // Flush the buffered handshake flight as a single 0x12 TDS packet.
    util::Result<void> flush_handshake_send() {
        if (send_buf.empty()) return {};
        std::vector<std::uint8_t> pkt;
        const std::size_t total = 8 + send_buf.size();
        if (total > 0xFFFF) {
            // A handshake flight that big is unexpected for the dev path;
            // fail loud rather than truncate the length field.
            return util::Error{openads::AE_REMOTE_ERROR, 0,
                "handshake flight exceeds one TDS packet", ""};
        }
        tds::write_header(pkt, tds::TDS_PKT_PRELOGIN, tds::TDS_STATUS_EOM,
                          static_cast<std::uint16_t>(total));
        pkt.insert(pkt.end(), send_buf.begin(), send_buf.end());
        send_buf.clear();
        return sock_send_all(sock, pkt.data(), pkt.size());
    }

    static int bio_send(void* ctx, const unsigned char* buf, std::size_t len) {
        auto* self = static_cast<Impl*>(ctx);
        // After the handshake the TLS records ARE the wire bytes — the TDS
        // framing now lives in the plaintext (built by send_tds), so the BIO
        // just ships the ciphertext straight to the socket.
        if (self->handshake_done) {
            auto r = network::sock_send(self->sock, buf, len);
            if (!r) return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
            std::size_t w = r.value();
            if (w == 0) return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
            return static_cast<int>(w);
        }
        // During the handshake: buffer the flight; it is flushed as one 0x12
        // TDS packet in bio_recv (or after the handshake completes).
        self->send_buf.insert(self->send_buf.end(), buf, buf + len);
        return static_cast<int>(len);
    }

    static int bio_recv(void* ctx, unsigned char* buf, std::size_t len) {
        auto* self = static_cast<Impl*>(ctx);
        // After the handshake the wire carries raw TLS records; hand the
        // ciphertext straight to mbedtls (the TDS framing is in the plaintext).
        if (self->handshake_done) {
            auto r = network::sock_recv(self->sock, buf, len);
            if (!r) return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
            std::size_t rd = r.value();
            if (rd == 0) return MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY;
            return static_cast<int>(rd);
        }
        if (self->recv_pending_pos >= self->recv_pending.size()) {
            // mbedtls wants to read => the current outgoing flight is complete.
            // Flush it as a single TDS packet before blocking on the reply.
            if (auto fr = self->flush_handshake_send(); !fr) {
                return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
            }
            auto r = self->read_one_tds_packet_raw();
            if (!r) {
                return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
            }
            self->recv_pending     = std::move(r).value();
            self->recv_pending_pos = 0;
            if (self->recv_pending.empty()) {
                return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
            }
        }
        std::size_t avail = self->recv_pending.size() - self->recv_pending_pos;
        std::size_t take  = (len < avail) ? len : avail;
        std::memcpy(buf, self->recv_pending.data() + self->recv_pending_pos,
                    take);
        self->recv_pending_pos += take;
        return static_cast<int>(take);
    }

    // Read exactly one raw TDS packet off the socket; return payload (header
    // stripped).  Used by the handshake recv BIO and the PRELOGIN exchange.
    util::Result<std::vector<std::uint8_t>> read_one_tds_packet_raw() {
        std::uint8_t hdr[8];
        if (auto r = sock_recv_exact(sock, hdr, 8); !r) return r.error();
        tds::TdsPacketHeader h;
        if (!tds::read_header(hdr, 8, h) || h.length < 8) {
            return util::Error{openads::AE_REMOTE_ERROR, 0,
                "malformed TDS packet header", ""};
        }
        std::size_t payload_len = static_cast<std::size_t>(h.length) - 8;
        std::vector<std::uint8_t> payload(payload_len);
        if (payload_len > 0) {
            if (auto r = sock_recv_exact(sock, payload.data(), payload_len);
                !r) {
                return r.error();
            }
        }
        return payload;
    }

    // Read exactly one TDS packet through the encrypted session.
    util::Result<void> recv_one_packet(std::uint8_t& type, std::uint8_t& status,
                                       std::vector<std::uint8_t>& payload) {
        std::uint8_t hdr[8];
        std::size_t got = 0;
        while (got < 8) {
            int rc = mbedtls_ssl_read(&ssl, hdr + got, 8 - got);
            if (rc == MBEDTLS_ERR_SSL_WANT_READ ||
                rc == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
            if (rc <= 0) {
                return util::Error{openads::AE_REMOTE_ERROR, rc,
                    "tls read (header): " + mbed_msg(rc), ""};
            }
            got += static_cast<std::size_t>(rc);
        }
        tds::TdsPacketHeader h;
        if (!tds::read_header(hdr, 8, h) || h.length < 8) {
            return util::Error{openads::AE_REMOTE_ERROR, 0,
                "malformed TDS packet header (tls)", ""};
        }
        type   = h.type;
        status = h.status;
        std::size_t payload_len = static_cast<std::size_t>(h.length) - 8;
        payload.assign(payload_len, 0);
        got = 0;
        while (got < payload_len) {
            int rc = mbedtls_ssl_read(&ssl, payload.data() + got,
                                      payload_len - got);
            if (rc == MBEDTLS_ERR_SSL_WANT_READ ||
                rc == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
            if (rc <= 0) {
                return util::Error{openads::AE_REMOTE_ERROR, rc,
                    "tls read (payload): " + mbed_msg(rc), ""};
            }
            got += static_cast<std::size_t>(rc);
        }
        return {};
    }
#endif  // OPENADS_WITH_TLS
};

// ---------------------------------------------------------------------------
// TdsTlsChannel — thin movable handle over Impl.
// ---------------------------------------------------------------------------

TdsTlsChannel::TdsTlsChannel() = default;
TdsTlsChannel::~TdsTlsChannel() = default;
TdsTlsChannel::TdsTlsChannel(TdsTlsChannel&&) noexcept = default;
TdsTlsChannel& TdsTlsChannel::operator=(TdsTlsChannel&&) noexcept = default;

bool TdsTlsChannel::valid() const noexcept {
    return impl_ && impl_->sock.valid();
}

void TdsTlsChannel::close() noexcept {
    if (impl_) impl_->close();
}

util::Result<TdsTlsChannel> TdsTlsChannel::connect(const MssqlUri& uri) {
#if !defined(OPENADS_WITH_TLS)
    (void)uri;
    return util::Error{openads::AE_FUNCTION_NOT_AVAILABLE, 0,
        "mssql native backend requires building with OPENADS_WITH_TLS=ON", ""};
#else
    if (auto r = network::network_init(); !r) return r.error();

    TdsTlsChannel ch;
    ch.impl_ = std::make_unique<Impl>();
    Impl& im = *ch.impl_;

    // --- 1. TCP connect ---
    auto sk = network::connect_tcp(uri.host, uri.port);
    if (!sk) return sk.error();
    im.sock = std::move(sk).value();

    // --- 2. Send PRELOGIN advertising ENCRYPT_ON ---
    {
        std::vector<std::uint8_t> pre = tds::build_prelogin();
        if (auto r = sock_send_all(im.sock, pre.data(), pre.size()); !r) {
            return r.error();
        }
    }

    // --- 3. Read PRELOGIN response, parse encryption negotiation ---
    {
        auto r = im.read_one_tds_packet_raw();
        if (!r) return r.error();
        std::vector<std::uint8_t> resp = std::move(r).value();
        tds::PreloginEncryption enc = tds::PreloginEncryption::NotSup;
        if (!tds::parse_prelogin_response(resp.data(), resp.size(), enc)) {
            return util::Error{openads::AE_REMOTE_ERROR, 0,
                "PRELOGIN response missing ENCRYPTION option", ""};
        }
        if (enc == tds::PreloginEncryption::NotSup) {
            return util::Error{openads::AE_REMOTE_ERROR, 0,
                "server does not support encryption (ENCRYPT_NOT_SUP); the "
                "native TDS 7.4 tunnelled-TLS path requires it", ""};
        }
        // On / Req / Off all proceed: SQL Server 2025 mandates encryption and
        // the tunnelled handshake runs now regardless of On vs Off semantics.
    }

    // --- 4. mbedtls setup + tunnelled TLS handshake ---
    mbedtls_ssl_init(&im.ssl);
    mbedtls_ssl_config_init(&im.conf);
    mbedtls_ctr_drbg_init(&im.drbg);
    mbedtls_entropy_init(&im.entropy);
    mbedtls_x509_crt_init(&im.ca);
    im.tls_inited = true;

    int rc = mbedtls_ctr_drbg_seed(&im.drbg, mbedtls_entropy_func,
                                   &im.entropy, nullptr, 0);
    if (rc != 0) return util::Error{openads::AE_REMOTE_ERROR, rc,
        "ctr_drbg_seed: " + mbed_msg(rc), ""};

    rc = mbedtls_ssl_config_defaults(&im.conf, MBEDTLS_SSL_IS_CLIENT,
                                     MBEDTLS_SSL_TRANSPORT_STREAM,
                                     MBEDTLS_SSL_PRESET_DEFAULT);
    if (rc != 0) return util::Error{openads::AE_REMOTE_ERROR, rc,
        "ssl_config_defaults: " + mbed_msg(rc), ""};

    // Dev / self-signed: trust the server cert when the URI asks for it.
    if (uri.trust_server_cert) {
        mbedtls_ssl_conf_authmode(&im.conf, MBEDTLS_SSL_VERIFY_NONE);
    } else {
        mbedtls_ssl_conf_authmode(&im.conf, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_ca_chain(&im.conf, &im.ca, nullptr);
    }
    mbedtls_ssl_conf_rng(&im.conf, mbedtls_ctr_drbg_random, &im.drbg);

    rc = mbedtls_ssl_setup(&im.ssl, &im.conf);
    if (rc != 0) return util::Error{openads::AE_REMOTE_ERROR, rc,
        "ssl_setup: " + mbed_msg(rc), ""};

    rc = mbedtls_ssl_set_hostname(&im.ssl, uri.host.c_str());
    if (rc != 0) return util::Error{openads::AE_REMOTE_ERROR, rc,
        "ssl_set_hostname: " + mbed_msg(rc), ""};

    // The crux: custom BIO that tunnels handshake records inside 0x12 packets.
    // ctx is the (pinned) Impl* — never relocated for the channel's lifetime.
    mbedtls_ssl_set_bio(&im.ssl, &im,
                        &Impl::bio_send, &Impl::bio_recv, nullptr);

    while ((rc = mbedtls_ssl_handshake(&im.ssl)) != 0) {
        if (rc != MBEDTLS_ERR_SSL_WANT_READ &&
            rc != MBEDTLS_ERR_SSL_WANT_WRITE) {
            return util::Error{openads::AE_REMOTE_ERROR, rc,
                "tunnelled TLS handshake failed: " + mbed_msg(rc), ""};
        }
    }
    // The final client flight (CCS + Finished) may still be buffered if the
    // handshake completed without a trailing read; flush it now.
    if (auto fr = im.flush_handshake_send(); !fr) return fr.error();
    im.handshake_done = true;

    // After the handshake, route all TDS I/O through the encrypted session.
    int maxpl = mbedtls_ssl_get_max_out_record_payload(&im.ssl);
    if (maxpl > 8) {
        im.packet_size = static_cast<std::size_t>(maxpl);
    }

    return ch;
#endif
}

util::Result<void>
TdsTlsChannel::send_tds(std::uint8_t type,
                        const std::vector<std::uint8_t>& payload) {
#if !defined(OPENADS_WITH_TLS)
    (void)type; (void)payload;
    return util::Error{openads::AE_FUNCTION_NOT_AVAILABLE, 0,
        "mssql native backend requires OPENADS_WITH_TLS", ""};
#else
    if (!impl_ || !impl_->handshake_done) {
        return util::Error{openads::AE_REMOTE_ERROR, 0,
            "send_tds before TLS handshake complete", ""};
    }
    Impl& im = *impl_;
    // Max payload bytes per TDS packet (header is 8 bytes).
    std::size_t max_payload = (im.packet_size > 8) ? (im.packet_size - 8) : 4088;
    // Clamp so total packet length fits a 16-bit field.
    if (max_payload > 0xFFFFu - 8) max_payload = 0xFFFFu - 8;

    std::size_t off = 0;
    const std::size_t total = payload.size();
    // Always send at least one packet (even for an empty payload).
    do {
        std::size_t chunk = total - off;
        if (chunk > max_payload) chunk = max_payload;
        bool last = (off + chunk >= total);
        std::uint8_t status = last ? tds::TDS_STATUS_EOM : 0x00;

        std::vector<std::uint8_t> pkt;
        pkt.reserve(8 + chunk);
        tds::write_header(pkt, type, status,
                          static_cast<std::uint16_t>(8 + chunk));
        pkt.insert(pkt.end(),
                   payload.begin() + static_cast<std::ptrdiff_t>(off),
                   payload.begin() + static_cast<std::ptrdiff_t>(off + chunk));

        std::size_t sent = 0;
        while (sent < pkt.size()) {
            int rc = mbedtls_ssl_write(&im.ssl, pkt.data() + sent,
                                       pkt.size() - sent);
            if (rc == MBEDTLS_ERR_SSL_WANT_READ ||
                rc == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
            if (rc <= 0) {
                return util::Error{openads::AE_REMOTE_ERROR, rc,
                    "tls write: " + mbed_msg(rc), ""};
            }
            sent += static_cast<std::size_t>(rc);
        }
        off += chunk;
    } while (off < total);
    return {};
#endif
}

util::Result<std::pair<std::uint8_t, std::vector<std::uint8_t>>>
TdsTlsChannel::recv_tds() {
#if !defined(OPENADS_WITH_TLS)
    return util::Error{openads::AE_FUNCTION_NOT_AVAILABLE, 0,
        "mssql native backend requires OPENADS_WITH_TLS", ""};
#else
    if (!impl_ || !impl_->handshake_done) {
        return util::Error{openads::AE_REMOTE_ERROR, 0,
            "recv_tds before TLS handshake complete", ""};
    }
    Impl& im = *impl_;
    std::vector<std::uint8_t> full;
    std::uint8_t first_type = 0;
    bool have_first = false;
    for (;;) {
        std::uint8_t type = 0, status = 0;
        std::vector<std::uint8_t> chunk;
        if (auto r = im.recv_one_packet(type, status, chunk); !r) {
            return r.error();
        }
        if (!have_first) { first_type = type; have_first = true; }
        full.insert(full.end(), chunk.begin(), chunk.end());
        if (full.size() > kMaxReassembly) {
            return util::Error{openads::AE_REMOTE_ERROR, 0,
                "TDS reply exceeds reassembly cap", ""};
        }
        if (status & tds::TDS_STATUS_EOM) break;
    }
    return std::pair<std::uint8_t, std::vector<std::uint8_t>>{
        first_type, std::move(full)};
#endif
}

} // namespace openads::sql_backend

#endif // defined(OPENADS_WITH_MSSQL)
