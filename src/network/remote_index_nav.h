#pragma once

#include "network/client.h"
#include "util/result.h"

#include <cstdint>

namespace openads::network {

// Invalidate parent row cache / prefetch before index-driven navigation.
void remote_index_nav_preamble(RemoteIndex* ri);

// Serve one forward step from the lookahead block, with no round-trip.
// Returns false when the queue is empty (caller must go to the wire).
// Shared by the table-handle and index-handle Skip paths so there is exactly
// one place that knows how a queued row becomes the current row.
bool remote_drain_prefetch(RemoteTable* rt);

// Ensure the parent RemoteTable's active order matches this index tag.
util::Result<void> remote_activate_index(RemoteIndex* ri);

// rddads passes hOrdCurrent (a RemoteIndex ADSHANDLE) to AdsGotoTop /
// AdsGotoBottom / AdsSkip. Route those calls through the parent table
// cursor once the tag above is active.
util::Result<void> remote_index_goto_top(RemoteIndex* ri);
util::Result<void> remote_index_goto_bottom(RemoteIndex* ri);
util::Result<void> remote_index_skip(RemoteIndex* ri, std::int32_t rows);

// Return the filtered key count from the active index order via the wire.
util::Result<std::uint32_t> remote_index_key_count(RemoteIndex* ri);

} // namespace openads::network