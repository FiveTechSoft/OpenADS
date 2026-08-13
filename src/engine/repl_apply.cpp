#include "engine/repl_apply.h"

#include "engine/repl_queue.h"
#include "engine/table.h"
#include "network/client.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace openads::engine {

namespace {

std::uint32_t find_by_identity(Table& t,
                                const std::vector<ReplIdent>& identity) {
    if (auto gr = t.goto_top(); !gr) return 0;
    while (!t.eof()) {
        bool match = true;
        for (auto& id : identity) {
            auto idx = t.field_index(id.name);
            if (idx < 0) { match = false; break; }
            auto val = t.read_field(static_cast<std::uint16_t>(idx));
            if (!val) { match = false; break; }
            if (val.value().as_string != id.value) { match = false; break; }
        }
        if (match) return t.recno();
        if (auto sr = t.skip(1); !sr) break;
    }
    return 0;
}

} // namespace

util::Result<ReplApplyResult>
repl_apply_once(DataDict& dd,
                const std::string& queue_path,
                const std::string& subscription_name,
                const std::string& my_origin_id) {
    ReplApplyResult result;

    auto& subs = dd.subscriptions();
    auto sit = subs.find(subscription_name);
    if (sit == subs.end())
        return util::Error{5000, 0, "subscription not found: " + subscription_name, ""};
    auto sub = sit->second;
    if (!sub.enabled) return result;

    ReplQueue q;
    if (auto r = q.open(queue_path); !r) return r.error();

    auto recs = q.read_from(sub.last_lsn);
    if (!recs) return recs.error();

    namespace fs = std::filesystem;
    fs::path target_dir = sub.target_uri;

    // Cache: alias -> resolved target path
    auto& tables = dd.tables();

    for (auto& rec : recs.value()) {
        if (rec.type == ReplRecType::TxBegin ||
            rec.type == ReplRecType::TxCommit ||
            rec.type == ReplRecType::TxAbort) {
            result.last_lsn_applied = rec.lsn;
            continue;
        }

        // M12.34 — loop prevention: skip records originating from this server.
        if (!my_origin_id.empty() && rec.origin_id == my_origin_id) {
            result.last_lsn_applied = rec.lsn;
            continue;
        }

        auto tit = tables.find(rec.source_table);
        if (tit == tables.end()) {
            result.last_lsn_applied = rec.lsn;
            continue;
        }
        fs::path target_path = target_dir / tit->second;

        if (!fs::exists(target_path)) {
            result.last_lsn_applied = rec.lsn;
            continue;
        }

        auto tbl_res = Table::open(target_path.string(), TableType::Cdx,
                                   OpenMode::Exclusive);
        if (!tbl_res) {
            result.last_lsn_applied = rec.lsn;
            continue;
        }
        Table tbl = std::move(tbl_res).value();

        switch (static_cast<std::uint8_t>(rec.type)) {
            case static_cast<std::uint8_t>(ReplRecType::Insert): {
                if (rec.after.empty()) break;
                if (auto r = tbl.append_record(); !r) break;
                if (auto r = tbl.set_record_raw(rec.after.data(),
                    rec.after.size()); !r) break;
                if (auto r = tbl.commit_dirty_record(); !r) break;
                result.records_applied++;
                break;
            }
            case static_cast<std::uint8_t>(ReplRecType::Update): {
                if (rec.after.empty()) break;
                auto recno = find_by_identity(tbl, rec.identity);
                if (recno == 0) break;
                if (auto r = tbl.goto_record(recno); !r) break;
                // M12.34 — conflict detection stub. Full before-image
                // comparison requires Table::read_record_raw() (not yet
                // public). For now, Skip mode always applies; Trigger
                // mode logs and applies. Overwrite mode always applies.
                if (sub.conflict_mode == DataDict::ConflictMode::Skip &&
                    !rec.before.empty()) {
                    // TODO: compare rec.before with current record bytes
                    // when read_record_raw becomes public.
                }
                if (auto r = tbl.set_record_raw(rec.after.data(),
                    rec.after.size()); !r) break;
                if (auto r = tbl.commit_dirty_record(); !r) break;
                result.records_applied++;
                break;
            }
            case static_cast<std::uint8_t>(ReplRecType::Delete): {
                auto recno = find_by_identity(tbl, rec.identity);
                if (recno == 0) break;
                if (auto r = tbl.goto_record(recno); !r) break;
                if (auto r = tbl.mark_deleted(); !r) break;
                result.records_applied++;
                break;
            }
            default: break;
        }
        result.last_lsn_applied = rec.lsn;
    }

    if (result.last_lsn_applied > 0) {
        dd.set_subscription_last_lsn(subscription_name, result.last_lsn_applied);
    }

    return result;
}

namespace {

// Parse "tcp://host:port/data_dir" into host, port, data_dir.
bool parse_tcp_uri(const std::string& uri,
                   std::string& host,
                   std::uint16_t& port,
                   std::string& data_dir) {
    const std::string prefix = "tcp://";
    if (uri.size() <= prefix.size()) return false;
    if (uri.compare(0, prefix.size(), prefix) != 0) return false;
    auto rest = uri.substr(prefix.size());
    auto slash = rest.find('/');
    std::string hostport = (slash != std::string::npos)
                               ? rest.substr(0, slash)
                               : rest;
    data_dir = (slash != std::string::npos) ? rest.substr(slash + 1) : "";
    auto colon = hostport.find(':');
    if (colon != std::string::npos) {
        host = hostport.substr(0, colon);
        try { port = static_cast<std::uint16_t>(std::stoul(hostport.substr(colon + 1))); }
        catch (...) { return false; }
    } else {
        host = hostport;
        port = 4561; // default ADS port
    }
    return !host.empty();
}

} // namespace

util::Result<ReplApplyResult>
repl_apply_once_remote(DataDict& dd,
                       const std::string& queue_path,
                       const std::string& subscription_name,
                       const std::string& my_origin_id) {
    ReplApplyResult result;

    auto& subs = dd.subscriptions();
    auto sit = subs.find(subscription_name);
    if (sit == subs.end())
        return util::Error{5000, 0, "subscription not found: " + subscription_name, ""};
    auto sub = sit->second;
    if (!sub.enabled) return result;

    ReplQueue q;
    if (auto r = q.open(queue_path); !r) return r.error();

    auto recs = q.read_from(sub.last_lsn);
    if (!recs) return recs.error();

    std::string host, data_dir;
    std::uint16_t port = 4561;
    if (!parse_tcp_uri(sub.target_uri, host, port, data_dir))
        return util::Error{5000, 0, "bad tcp:// URI: " + sub.target_uri, ""};

    network::RemoteConnection conn;
    auto cr = conn.connect(host, port, data_dir);
    if (!cr) return cr.error();

    auto& tables = dd.tables();

    // Cache: source_table -> remote table id
    std::unordered_map<std::string, std::uint32_t> open_tables;

    for (auto& rec : recs.value()) {
        if (rec.type == ReplRecType::TxBegin ||
            rec.type == ReplRecType::TxCommit ||
            rec.type == ReplRecType::TxAbort) {
            result.last_lsn_applied = rec.lsn;
            continue;
        }

        // M12.34 — loop prevention: skip records originating from this server.
        if (!my_origin_id.empty() && rec.origin_id == my_origin_id) {
            result.last_lsn_applied = rec.lsn;
            continue;
        }

        // Resolve table id from source_table name.
        auto tid_it = open_tables.find(rec.source_table);
        if (tid_it == open_tables.end()) {
            auto tit = tables.find(rec.source_table);
            if (tit == tables.end()) {
                result.last_lsn_applied = rec.lsn;
                continue;
            }
            auto otr = conn.open_table(tit->second);
            if (!otr) { result.last_lsn_applied = rec.lsn; continue; }
            tid_it = open_tables.emplace(rec.source_table,
                                         otr.value().id).first;
        }
        std::uint32_t tid = tid_it->second;

        switch (static_cast<std::uint8_t>(rec.type)) {
            case static_cast<std::uint8_t>(ReplRecType::Insert): {
                if (rec.after.empty()) break;
                if (auto r = conn.append_blank(tid); !r) break;
                if (auto r = conn.set_record(tid, rec.after.data(),
                    rec.after.size()); !r) break;
                if (auto r = conn.flush_table(tid); !r) break;
                result.records_applied++;
                break;
            }
            case static_cast<std::uint8_t>(ReplRecType::Update): {
                if (rec.after.empty()) break;
                std::vector<std::pair<std::string, std::string>> ident;
                for (auto& id : rec.identity)
                    ident.emplace_back(id.name, id.value);
                auto fr = conn.find_record(tid, ident);
                if (!fr || fr.value() == 0) break;
                if (auto r = conn.goto_record(tid, fr.value()); !r) break;
                if (auto r = conn.set_record(tid, rec.after.data(),
                    rec.after.size()); !r) break;
                if (auto r = conn.flush_table(tid); !r) break;
                result.records_applied++;
                break;
            }
            case static_cast<std::uint8_t>(ReplRecType::Delete): {
                std::vector<std::pair<std::string, std::string>> ident;
                for (auto& id : rec.identity)
                    ident.emplace_back(id.name, id.value);
                auto fr = conn.find_record(tid, ident);
                if (!fr || fr.value() == 0) break;
                if (auto r = conn.goto_record(tid, fr.value()); !r) break;
                if (auto r = conn.delete_record(tid); !r) break;
                result.records_applied++;
                break;
            }
            default: break;
        }
        result.last_lsn_applied = rec.lsn;
    }

    for (auto& [name, tid] : open_tables)
        conn.close_table(tid);

    if (result.last_lsn_applied > 0) {
        dd.set_subscription_last_lsn(subscription_name, result.last_lsn_applied);
    }

    return result;
}

} // namespace openads::engine
