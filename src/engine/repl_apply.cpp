#include "engine/repl_apply.h"

#include "engine/repl_queue.h"
#include "engine/table.h"

#include <filesystem>
#include <string>
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
                const std::string& subscription_name) {
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

        switch (rec.type) {
            case ReplRecType::Insert: {
                if (rec.after.empty()) break;
                if (auto r = tbl.append_record(); !r) break;
                if (auto r = tbl.set_record_raw(rec.after.data(),
                    rec.after.size()); !r) break;
                if (auto r = tbl.commit_dirty_record(); !r) break;
                result.records_applied++;
                break;
            }
            case ReplRecType::Update: {
                if (rec.after.empty()) break;
                auto recno = find_by_identity(tbl, rec.identity);
                if (recno == 0) break;
                if (auto r = tbl.goto_record(recno); !r) break;
                if (auto r = tbl.set_record_raw(rec.after.data(),
                    rec.after.size()); !r) break;
                if (auto r = tbl.commit_dirty_record(); !r) break;
                result.records_applied++;
                break;
            }
            case ReplRecType::Delete: {
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

} // namespace openads::engine
