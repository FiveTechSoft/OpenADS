#pragma once

// TDS transport channel with the TLS handshake tunnelled inside TDS PRELOGIN
// (0x12) packets, as MS SQL Server's classic TDS 7.4 encrypted path requires.
//
// Lifecycle:
//   1. TCP connect to host:port.
//   2. Send a PRELOGIN (0x12) packet advertising ENCRYPT_ON.
//   3. Read the PRELOGIN response; if the server negotiated encryption, run
//      the TLS handshake with the raw handshake records wrapped in 0x12 TDS
//      packets (custom mbedtls BIO callbacks).
//   4. After the handshake completes, every TDS message (LOGIN7, SQLBATCH, …)
//      travels through mbedtls_ssl_write / mbedtls_ssl_read, i.e. encrypted.
//
// This file only exists when OPENADS_WITH_MSSQL is defined; the encrypted path
// additionally needs OPENADS_WITH_TLS (mbedtls). The CMakeLists guards that the
// two flags are set together.
//
// The mbedtls contexts live in a heap-allocated Impl (pimpl) so the channel is
// cheaply movable WITHOUT relocating the SSL context (mbedtls contexts hold
// internal self-pointers and must not be byte-copied after init).

#if defined(OPENADS_WITH_MSSQL)

#include "util/result.h"

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace openads::sql_backend {

struct MssqlUri;  // forward declaration (sql_backend/mssql_uri.h)

class TdsTlsChannel {
public:
    TdsTlsChannel();
    ~TdsTlsChannel();

    TdsTlsChannel(TdsTlsChannel&&) noexcept;
    TdsTlsChannel& operator=(TdsTlsChannel&&) noexcept;

    TdsTlsChannel(const TdsTlsChannel&)            = delete;
    TdsTlsChannel& operator=(const TdsTlsChannel&) = delete;

    // TCP connect + PRELOGIN + tunnelled TLS handshake.  On success the
    // returned channel is ready for send_tds / recv_tds (encrypted).
    static util::Result<TdsTlsChannel> connect(const MssqlUri& uri);

    // Send one logical TDS message of |type| carrying |payload| (the bytes
    // AFTER the 8-byte header).  Segmented into negotiated-size packets with
    // EOM set only on the last.  Travels over the established TLS session.
    util::Result<void> send_tds(std::uint8_t type,
                                const std::vector<std::uint8_t>& payload);

    // Receive one logical TDS message: reassembles multi-packet replies by the
    // EOM status bit.  Returns {packet type of first packet, full payload}.
    util::Result<std::pair<std::uint8_t, std::vector<std::uint8_t>>> recv_tds();

    void close() noexcept;
    bool valid() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace openads::sql_backend

#endif // defined(OPENADS_WITH_MSSQL)
