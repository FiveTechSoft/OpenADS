#pragma once

#include <string>

namespace openads::network {

// Who is connecting: sent to the server in the ClientInfo frame and shown
// in the Studio console. UTF-8; a field that cannot be determined is empty.
struct ClientIdentity {
    std::string computer;  // workstation (NetBIOS / host) name
    std::string os_user;   // OS login of the process
    std::string program;   // executable file name, without the directory
};

ClientIdentity local_client_identity();

}  // namespace openads::network
