#pragma once

#include "mgmt/mg_snapshot.h"
#include "util/result.h"

#include <cstdint>
#include <string>
#include <vector>

namespace openads::network {

// Selects which telemetry the server should collect for a MgRequest.
enum class MgRequestKind : std::uint8_t {
    Snapshot       = 0x01,  // full MgSnapshot (covers every Get*)
    KillUser       = 0x02,  // arg: u16 conn_no
    ResetCommStats = 0x03,
    DumpTables     = 0x04,
    HealthJson     = 0x05,  // additive aggregate-only health reply
};

// Request payload: [u8 kind][optional args].
std::string encode_mg_request(MgRequestKind kind, std::uint16_t arg);

struct MgRequest {
    MgRequestKind kind = MgRequestKind::Snapshot;
    std::uint16_t arg  = 0;
};
util::Result<MgRequest> decode_mg_request(const std::string& payload);

// Reply payload: a fully serialized MgSnapshot, little-endian.
std::string encode_mg_snapshot(const mgmt::MgSnapshot& snap);
util::Result<mgmt::MgSnapshot> decode_mg_snapshot(
    const std::string& payload);

}  // namespace openads::network
