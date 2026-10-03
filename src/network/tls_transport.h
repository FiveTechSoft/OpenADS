#pragma once

#include "network/socket.h"
#include "network/transport.h"
#include "util/result.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#if defined(OPENADS_WITH_TLS)

namespace openads::network {

// M12.12 — real TLS transport, vendored mbedtls 3.6 LTS (Apache 2.0).
//
// Available only when CMake is configured with `-DOPENADS_WITH_TLS=ON`.
// The default build keeps TLS disabled so the engine still ships
// without a network dependency at configure time. When enabled,
// AdsConnect60 with a `tls://host:port/<dir>` URI opens a
// TlsTransport instead of returning AE_FUNCTION_NOT_AVAILABLE.

struct TlsConfig {
    // PEM-encoded CA bundle the client uses to verify the server.
    // Empty fails verification unless insecure_skip_verify is explicit.
    std::string ca_pem;
    // PEM-encoded server cert + key (server-side only).
    std::string cert_pem;
    std::string key_pem;
    // Hostname for SNI + verification (client-side).
    std::string sni_hostname;
    // Skip peer verification (insecure — dev/test only).
    bool insecure_skip_verify = false;
};

// Connect a TLS client to host:port, returning a transport ready
// for read_frame / write_frame.
util::Result<std::unique_ptr<ITransport>>
    connect_tls(const std::string& host, std::uint16_t port,
                const TlsConfig& cfg);

// Adopt an accepted, non-blocking socket without owning its lifetime.
// Handshake advances cooperatively from recv/send. The Session owns closure.
util::Result<std::unique_ptr<ITransport>>
    accept_tls(Socket socket, const TlsConfig& cfg);
util::Result<void> validate_tls_server_config(const TlsConfig& cfg);

} // namespace openads::network

#endif // OPENADS_WITH_TLS
