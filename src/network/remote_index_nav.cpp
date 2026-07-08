#include "network/remote_index_nav.h"

#include "openads/error.h"

namespace openads::network {

void remote_index_nav_preamble(RemoteIndex* ri) {
    if (ri == nullptr || ri->parent == nullptr) return;
    ri->parent->found_cached  = true;
    ri->parent->current_found = false;
    ri->parent->prefetch_queue.clear();
    ri->parent->prefetch_consumed = 0;
}

util::Result<void> remote_activate_index(RemoteIndex* ri) {
    if (ri == nullptr || ri->parent == nullptr || ri->conn == nullptr) {
        return util::Error{
            openads::AE_INTERNAL_ERROR, 0,
            "remote index: missing parent or connection", ""};
    }
    // Always sync the server's active order. The client may already
    // consider this index focused (active_index_id) after production
    // CDX auto-open or OrdSetFocus handle resolution without a prior
    // SetOrder round-trip; skipping the wire op left ordered_tables_
    // empty on the server so later GotoTop/Skip walked the engine
    // table and ignored scope set on the ABI handle.
    auto r = ri->conn->set_order(ri->parent->id, ri->id);
    if (!r) return r.error();
    ri->parent->active_index_id = ri->id;
    return {};
}

util::Result<void> remote_index_goto_top(RemoteIndex* ri) {
    remote_index_nav_preamble(ri);
    auto act = remote_activate_index(ri);
    if (!act) return act.error();
    return ri->conn->goto_top(ri->parent);
}

util::Result<void> remote_index_goto_bottom(RemoteIndex* ri) {
    remote_index_nav_preamble(ri);
    auto act = remote_activate_index(ri);
    if (!act) return act.error();
    return ri->conn->goto_bottom(ri->parent);
}

util::Result<void> remote_index_skip(RemoteIndex* ri, std::int32_t rows) {
    if (ri == nullptr || ri->parent == nullptr || ri->conn == nullptr) {
        return util::Error{
            openads::AE_INTERNAL_ERROR, 0,
            "remote index skip: missing parent or connection", ""};
    }
    auto act = remote_activate_index(ri);
    if (!act) return act.error();
    // Skip(0) settles prefetch lag — must not clear prefetch_consumed first.
    if (rows == 0) {
        return ri->conn->skip(ri->parent, 0);
    }
    remote_index_nav_preamble(ri);
    return ri->conn->skip(ri->parent, rows);
}

util::Result<std::uint32_t> remote_index_key_count(RemoteIndex* ri) {
    if (ri == nullptr || ri->parent == nullptr || ri->conn == nullptr) {
        return util::Error{
            openads::AE_INTERNAL_ERROR, 0,
            "remote index key count: missing parent or connection", ""};
    }
    auto act = remote_activate_index(ri);
    if (!act) return act.error();
    auto r = ri->conn->key_count(ri->parent->id);
    if (!r) return r.error();
    return r.value();
}

} // namespace openads::network