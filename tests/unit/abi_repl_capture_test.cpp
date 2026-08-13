#include "doctest.h"
#include "engine/data_dict.h"
#include "engine/repl_capture.h"
#include "engine/repl_queue.h"
#include "engine/repl_catalog.h"
#include "session/connection.h"

#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using openads::engine::DataDict;
using openads::engine::ReplQueue;
using openads::engine::ReplCatalog;
using openads::engine::ReplRecType;

static void safe_remove(const fs::path& p) { std::error_code ec; fs::remove(p, ec); }

TEST_CASE("repl capture: enqueue failures counter starts at zero") {
    CHECK(openads::engine::repl_enqueue_failures() == 0);
}
