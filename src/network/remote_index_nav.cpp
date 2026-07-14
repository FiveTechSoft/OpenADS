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
    // RCB 07/14/2026: sync the server's active order — but ONLY when it isn't
    // already what we want. This used to fire unconditionally on every nav op,
    // which doubled the round-trips of an ordered browse: rddads skips on
    // hOrdCurrent, so an xBrowse PgDn was SetOrder+Skip, SetOrder+Skip,
    // SetOrder+Skip ... one entirely redundant frame per row.
    //
    // Do not "simplify" this back to checking active_index_id. It was
    // unconditional for a real reason: active_index_id is only the client's
    // BELIEF about the order and it can be set without any round-trip at all
    // (production-bag auto-open in AdsOpenTable100, AdsGetIndexHandle resolving
    // a tag). Trusting it left ordered_tables_ empty on the server, so
    // GotoTop/Skip walked the engine table in natural order and ignored any
    // scope — the "remote browse shows no index" bug.
    //
    // server_order_id closes that hole properly: only a SetOrder *ack* ever
    // writes it, so a match here means the server really does have this order
    // installed. Everything that can invalidate the server's order (change by
    // name, by handle, natural-order reset) puts it back to kOrderUnknown.
    if (ri->parent->server_order_id == ri->id) {
        ri->parent->active_index_id = ri->id;
        return {};
    }
    auto r = ri->conn->set_order(ri->parent->id, ri->id);
    if (!r) return r.error();
    ri->parent->server_order_id = ri->id;
    ri->parent->active_index_id = ri->id;
    // The controlling order just changed on the server — anything queued was
    // read in the previous order.
    ri->parent->row_valid = false;
    ri->parent->invalidate_prefetch();
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

bool remote_drain_prefetch(RemoteTable* rt) {
    if (rt == nullptr || rt->prefetch_queue.empty()) return false;
    auto pr = std::move(rt->prefetch_queue.front());
    rt->prefetch_queue.pop_front();
    rt->current_recno   = pr.recno;
    rt->current_deleted = pr.deleted;
    rt->current_row     = std::move(pr.fields);
    rt->row_valid       = true;
    // The server cursor did not move — remember we are one logical row further
    // ahead so the next wire op resyncs by (step + prefetch_consumed).
    ++rt->prefetch_consumed;
    return true;
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
    RemoteTable* rt = ri->parent;
    // xBase: any Skip clears Found().
    rt->found_cached  = true;
    rt->current_found = false;
    // RCB 07/14/2026: the ordered browse is the browse that matters (rddads
    // skips on hOrdCurrent), and it now gets a real index-order lookahead
    // block, so serve the forward step from it — zero round-trips for the rest
    // of the block. This path used to call remote_index_nav_preamble()
    // unconditionally, which threw the queue away on EVERY skip. That made
    // sense while the server refused to send a block for ordered tables (the
    // queue was always empty anyway); it is exactly wrong now.
    if (rows == 1 && remote_drain_prefetch(rt)) {
        return {};
    }
    // RCB 07/14/2026: non-sequential step. The queued rows were read forward
    // from where we were, so they cannot serve this move — drop them.
    //
    // But do NOT reset prefetch_consumed here, however tempting the symmetry
    // is. The server cursor still lags the client's logical position by exactly
    // that many rows, and RemoteConnection::skip has to fold the lag into the
    // wire step (step + prefetch_consumed) to land in the right place. Zeroing
    // it would send a short step and silently land the cursor on the wrong
    // record. The ack is what clears it (parse_row_trailer_into), because the
    // ack is the point at which the lag is genuinely gone.
    rt->prefetch_queue.clear();
    return ri->conn->skip(rt, rows);
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