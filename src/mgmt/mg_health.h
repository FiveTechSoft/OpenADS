#pragma once
#include "mgmt/mg_snapshot.h"
#include <string>
namespace openads::mgmt {
// Aggregate-only JSON. No user names, paths, SQL text or credentials.
std::string health_json(const MgSnapshot& snap, const std::string& version);
}
