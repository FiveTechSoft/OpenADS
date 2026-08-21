// util/client_config: OPENADS_* settings can also live in openads.ini.
// Env var wins over the ini key; the ini is re-read when its mtime
// changes so long-lived hosts pick up edits without a restart.
#include "doctest.h"
#include "util/client_config.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

struct EnvGuard {
    const char* name_;
    EnvGuard(const char* n, const char* v) : name_(n) {
#ifdef _WIN32
        _putenv_s(n, v);
#else
        setenv(n, v, 1);
#endif
    }
    ~EnvGuard() {
#ifdef _WIN32
        _putenv_s(name_, "");
#else
        unsetenv(name_);
#endif
    }
};

void write_ini(const fs::path& p, const char* text) {
    std::ofstream out(p, std::ios::trunc);
    out << text;
}

} // namespace

TEST_CASE("client_setting: env wins, ini fallback, truthy, mtime reload") {
    const auto dir = fs::temp_directory_path() / "openads_client_config";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    const auto ini = dir / "openads.ini";

    write_ini(ini,
        "# comment\n"
        "[server]\n"
        "remote-only-access = 1\n"   // dash style, like serverd keys
        "Log_File = C:/logs/ace.log\n"
        "tls_insecure = off\n");

    EnvGuard ini_env("OPENADS_INI", ini.string().c_str());

    // make sure the flag env var is really unset (an empty value would
    // still win over the ini key)
#ifdef _WIN32
    _putenv_s("OPENADS_REMOTE_ONLY_ACCESS", "");
#else
    unsetenv("OPENADS_REMOTE_ONLY_ACCESS");
#endif

    // ini fallback (key lookup is case-insensitive)
    CHECK(openads::util::client_setting("OPENADS_REMOTE_ONLY_ACCESS",
                                        "remote_only_access") == "1");
    CHECK(openads::util::client_setting_truthy("OPENADS_REMOTE_ONLY_ACCESS",
                                               "remote_only_access"));
    CHECK(openads::util::client_setting("OPENADS_LOG_FILE",
                                        "log_file") == "C:/logs/ace.log");
    CHECK(!openads::util::client_setting_truthy("OPENADS_TLS_INSECURE",
                                                "tls_insecure"));
    CHECK(openads::util::client_setting("OPENADS_MISSING", "missing").empty());

    // env wins over ini
    {
        EnvGuard off("OPENADS_REMOTE_ONLY_ACCESS", "0");
        CHECK(!openads::util::client_setting_truthy(
            "OPENADS_REMOTE_ONLY_ACCESS", "remote_only_access"));
    }

    // mtime change reloads the file
    write_ini(ini, "remote_only_access = 0\n");
    fs::last_write_time(ini, fs::file_time_type::clock::now() +
                                 std::chrono::seconds(5), ec);
    CHECK(!openads::util::client_setting_truthy(
        "OPENADS_REMOTE_ONLY_ACCESS", "remote_only_access"));

    fs::remove_all(dir, ec);
}
