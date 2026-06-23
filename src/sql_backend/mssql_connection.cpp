#include "sql_backend/mssql_connection.h"

#if defined(OPENADS_WITH_MSSQL)

#include "openads/error.h"
#include "sql_backend/mssql_uri.h"
#include "sql_backend/tds_protocol.h"

#include <utility>

namespace openads::sql_backend {

struct MssqlConnection::Impl {
    TdsTlsChannel channel;
    bool          authenticated = false;
};

MssqlConnection::MssqlConnection() = default;
MssqlConnection::~MssqlConnection() = default;
MssqlConnection::MssqlConnection(MssqlConnection&&) noexcept = default;
MssqlConnection& MssqlConnection::operator=(MssqlConnection&&) noexcept = default;

bool MssqlConnection::valid() const noexcept {
    return impl_ && impl_->authenticated && impl_->channel.valid();
}

void MssqlConnection::disconnect() noexcept {
    if (impl_) {
        impl_->channel.close();
        impl_->authenticated = false;
    }
}

util::Result<MssqlConnection> MssqlConnection::open(const MssqlUri& uri) {
    // 1. Establish the TLS-in-TDS channel (TCP + PRELOGIN + tunnelled TLS).
    auto chan = TdsTlsChannel::connect(uri);
    if (!chan) return chan.error();

    MssqlConnection conn;
    conn.impl_ = std::make_unique<Impl>();
    conn.impl_->channel = std::move(chan).value();

    // 2. Build + send the LOGIN7 message over the encrypted session.
    tds::Login7Params params;
    params.hostname    = "OpenADS";
    params.username    = uri.user;
    params.password    = uri.password;
    params.app_name    = "OpenADS";
    params.server_name = uri.host;
    params.database    = uri.database;

    std::vector<std::uint8_t> login7 = tds::build_login7(params);
    // build_login7 already prepends the 8-byte TDS header; the channel's
    // send_tds adds its own header, so strip the embedded one and let the
    // channel frame (and segment) the LOGIN7 structure itself.
    std::vector<std::uint8_t> login7_payload(login7.begin() + 8, login7.end());

    if (auto r = conn.impl_->channel.send_tds(tds::TDS_PKT_LOGIN7,
                                              login7_payload); !r) {
        return r.error();
    }

    // 3. Read the login response token stream and parse it.
    auto reply = conn.impl_->channel.recv_tds();
    if (!reply) return reply.error();
    const std::vector<std::uint8_t>& payload = reply.value().second;

    tds::LoginResult res = tds::parse_login_response(payload.data(),
                                                     payload.size());
    if (!res.authenticated) {
        // Surface the SERVER's error (number + message) — never the password
        // or connection string.  Map to a login-failed family code so the ABI
        // returns a non-zero, recognisable error.
        std::int32_t code = static_cast<std::int32_t>(res.error_number);
        if (code == 0) code = openads::AE_LOGIN_FAILED;
        std::string msg = res.message.empty()
                              ? std::string("MSSQL login failed")
                              : res.message;
        return util::Error{code, 0, msg, ""};
    }

    conn.impl_->authenticated = true;
    return conn;
}

} // namespace openads::sql_backend

#endif // defined(OPENADS_WITH_MSSQL)
