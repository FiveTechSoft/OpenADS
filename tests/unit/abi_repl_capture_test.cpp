#include "doctest.h"
#include "engine/data_dict.h"
#include "engine/repl_capture.h"
#include "engine/repl_queue.h"
#include "engine/repl_catalog.h"
#include "session/connection.h"

#include <string>

using openads::engine::DataDict;
using openads::engine::ReplQueue;
using openads::engine::ReplCatalog;
using openads::engine::ReplRecType;

TEST_CASE("repl capture: enqueue failures counter starts at zero") {
    CHECK(openads::engine::repl_enqueue_failures() == 0);
}
