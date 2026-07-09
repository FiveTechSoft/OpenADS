#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"

#include <cstdlib>
#include <filesystem>
#include <string>

int main(int argc, char** argv) {
    // Keep the SAP-style error log (mgmt::ErrorLog) inside the temp tree
    // for the whole suite — without this, tests that start servers or
    // trigger SQL errors would write ads_err.dbf to the real default
    // location (C:\ / /var/log/advantage) on the developer's machine.
    std::error_code ec;
    auto dir = std::filesystem::temp_directory_path(ec) / "openads_test_errlog";
#ifdef _WIN32
    _putenv_s("OPENADS_ERROR_LOG_PATH", dir.string().c_str());
#else
    setenv("OPENADS_ERROR_LOG_PATH", dir.string().c_str(), 1);
#endif
    return doctest::Context(argc, argv).run();
}
