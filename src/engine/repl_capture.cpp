#include "engine/repl_capture.h"

#include "engine/data_dict.h"
#include "engine/repl_catalog.h"
#include "engine/repl_queue.h"
#include "session/connection.h"

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

namespace openads::engine {

namespace {

std::atomic<std::uint64_t> g_repl_enqueue_failures{0};

std::string stem_from_dd_path(const std::string& dd_path) {
    // "C:/data/mydb.add" -> "C:/data/mydb"
    auto pos = dd_path.rfind('.');
    if (pos != std::string::npos) return dd_path.substr(0, pos);
    return dd_path;
}

// Read the string value of a field from the record buffer.
std::string read_field_as_string(Table& t, const std::string& name) {
    auto idx = t.field_index(name);
    if (idx < 0) return {};
    auto val = t.read_field(static_cast<std::uint16_t>(idx));
    if (!val) return {};
    return val.value().as_string;
}

// Find the table alias for a given table path in the connection.
std::string alias_for_table(session::Connection* c, const std::string& path) {
    if (!c) return {};
    if (auto* dd = c->dd()) {
        // Build absolute paths from DD relative paths + data_dir
        namespace fs = std::filesystem;
        fs::path data_dir = c->data_dir();
        for (auto& kv : dd->tables()) {
            fs::path abs_path = data_dir / kv.second;
            std::error_code ec;
            if (fs::equivalent(abs_path, path, ec)) return kv.first;
            // Also try direct string match for fully-qualified paths
            if (kv.second == path) return kv.first;
        }
    }
    return {};
}

} // namespace

void repl_capture_row(session::Connection* c, Table& t, ReplRecType type,
                      const std::vector<std::uint8_t>* before,
                      const std::vector<std::uint8_t>* after) {
    if (!c || !c->has_dd()) return;
    auto* dd = c->dd();
    if (!dd) return;

    // Resolve table alias
    std::string alias = alias_for_table(c, t.path());
    if (alias.empty()) return;

    auto& cat = c->repl_catalog();
    if (!cat.table_is_published(alias)) return;

    auto articles = cat.articles_for_table(alias);
    if (articles.empty()) return;

    // Open queue on first use
    if (!c->repl_queue_open()) {
        std::string stem = stem_from_dd_path(c->dd_path());
        std::string qpath = stem + ".replq";
        auto open_r = c->repl_queue().open(qpath);
        if (!open_r) {
            g_repl_enqueue_failures.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        c->set_repl_queue_open(true);
    }

    auto& q = c->repl_queue();
    std::uint64_t tx_id = c->in_tx() ? c->tx_id() : 0;

    for (auto& art : articles) {
        ReplRecord rec;
        rec.type = type;
        rec.tx_id = tx_id;
        rec.source_table = alias;

        // Build identity
        for (auto& col : art.identity_cols) {
            std::string val = read_field_as_string(t, col);
            rec.identity.push_back({col, val});
        }

        if (before) rec.before = *before;
        if (after)  rec.after  = *after;

        // M12.34 — stamp origin for loop prevention.
        rec.origin_id = c->origin_id();

        auto r = q.append(rec);
        if (!r) {
            g_repl_enqueue_failures.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

void repl_capture_tx(session::Connection* c, ReplRecType type, std::uint64_t tx_id) {
    if (!c || !c->has_dd()) return;
    if (!c->repl_queue_open()) return;

    ReplRecord rec;
    rec.type = type;
    rec.tx_id = tx_id;
    auto r = c->repl_queue().append(rec);
    if (!r) {
        g_repl_enqueue_failures.fetch_add(1, std::memory_order_relaxed);
    }
}

std::uint64_t repl_enqueue_failures() {
    return g_repl_enqueue_failures.load(std::memory_order_relaxed);
}

} // namespace openads::engine
