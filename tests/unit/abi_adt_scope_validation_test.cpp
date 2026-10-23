// M5 scope validation — native ADT/ADI create, read, write, seek:
//   AUTOINC, CHAR, INTEGER, DATE, MEMO (+ .adm / .adi)
#include "doctest.h"
#include "drivers/adi/adi_index.h"
#include "network/server.h"
#include "openads/ace.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <signal.h>
#  include <spawn.h>
#  include <sys/wait.h>
#  include <unistd.h>
#  if defined(__APPLE__)
#    include <crt_externs.h>
#    define environ (*_NSGetEnviron())
#  else
extern char** environ;
#  endif
#endif

namespace fs = std::filesystem;

namespace {

std::string trim_spaces(std::string s) {
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

fs::path scope_tmp_dir() {
    return fs::temp_directory_path() / "openads_adt_scope_validation";
}

void wipe_scope_tmp() {
    std::error_code ec;
    fs::remove_all(scope_tmp_dir(), ec);
    fs::create_directories(scope_tmp_dir(), ec);
}

void connect_local(ADSHANDLE* hConn, const fs::path& dir) {
    UNSIGNED8 srv[260]{};
    std::memcpy(srv, dir.string().c_str(), dir.string().size());
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, hConn)
            == AE_SUCCESS);
}

std::string get_field_str(ADSHANDLE hTable, const char* field) {
    UNSIGNED8 fld[64]{};
    std::strncpy(reinterpret_cast<char*>(fld), field, sizeof(fld) - 1);
    UNSIGNED8 buf[512]{};
    UNSIGNED32 len = sizeof(buf);
    REQUIRE(AdsGetString(hTable, fld, buf, &len, 0) == AE_SUCCESS);
    return trim_spaces(std::string(reinterpret_cast<char*>(buf), len));
}

void set_field_str(ADSHANDLE hTable, const char* field, const std::string& val) {
    UNSIGNED8 fld[64]{};
    std::strncpy(reinterpret_cast<char*>(fld), field, sizeof(fld) - 1);
    REQUIRE(AdsSetString(hTable, fld,
                         reinterpret_cast<UNSIGNED8*>(const_cast<char*>(val.data())),
                         static_cast<UNSIGNED32>(val.size()))
            == AE_SUCCESS);
}

const char* serverd_exe_path() {
    if (const char* env = std::getenv("OPENADS_TEST_SERVERD")) {
        if (env[0] != '\0') return env;
    }
#if defined(OPENADS_TEST_SERVERD)
    return OPENADS_TEST_SERVERD;
#else
    return nullptr;
#endif
}

// Launch a real openads_serverd subprocess; parse the bound port from
// its "listening on host:port" banner on stdout.
class ServerdProcess {
public:
    ServerdProcess() = default;
    ServerdProcess(const ServerdProcess&) = delete;
    ServerdProcess& operator=(const ServerdProcess&) = delete;
    ~ServerdProcess() { stop(); }

    bool start(const fs::path& exe, const fs::path& data_dir) {
        stop();
        if (!fs::exists(exe)) return false;
#if defined(_WIN32)
        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        HANDLE wr = INVALID_HANDLE_VALUE;
        if (!CreatePipe(&rd_pipe_, &wr, &sa, 0)) return false;
        SetHandleInformation(rd_pipe_, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOA si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.hStdOutput = wr;
        si.hStdError  = wr;
        si.wShowWindow = SW_HIDE;

        std::string cmd = "\"" + exe.string() + "\""
                          " --host 127.0.0.1 --port 0 --data \""
                          + data_dir.string() + "\"";
        std::vector<char> cmdline(cmd.begin(), cmd.end());
        cmdline.push_back('\0');

        PROCESS_INFORMATION pi{};
        if (!CreateProcessA(nullptr, cmdline.data(), nullptr, nullptr, TRUE,
                            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            CloseHandle(wr);
            return false;
        }
        CloseHandle(wr);
        proc_ = pi.hProcess;
        thread_ = pi.hThread;
#else
        if (pipe(pipefd_) != 0) return false;
        posix_spawn_file_actions_t fa{};
        posix_spawn_file_actions_init(&fa);
        posix_spawn_file_actions_adddup2(&fa, pipefd_[1], STDOUT_FILENO);
        posix_spawn_file_actions_adddup2(&fa, pipefd_[1], STDERR_FILENO);
        posix_spawn_file_actions_addclose(&fa, pipefd_[0]);

        std::string exe_s = exe.string();
        std::string host = "127.0.0.1";
        std::string port = "0";
        std::string data = data_dir.string();
        char* argv[] = {
            exe_s.data(),
            const_cast<char*>("--host"), host.data(),
            const_cast<char*>("--port"), port.data(),
            const_cast<char*>("--data"), data.data(),
            nullptr};
        if (posix_spawn(&pid_, exe.c_str(), &fa, nullptr, argv, environ) != 0) {
            posix_spawn_file_actions_destroy(&fa);
            return false;
        }
        posix_spawn_file_actions_destroy(&fa);
        close(pipefd_[1]);
        pipefd_[1] = -1;
#endif
        return wait_for_port();
    }

    void stop() {
#if defined(_WIN32)
        if (proc_ != nullptr) {
            TerminateProcess(proc_, 0);
            WaitForSingleObject(proc_, 3000);
            CloseHandle(proc_);
            CloseHandle(thread_);
            proc_ = nullptr;
            thread_ = nullptr;
        }
        if (rd_pipe_ != INVALID_HANDLE_VALUE) {
            CloseHandle(rd_pipe_);
            rd_pipe_ = INVALID_HANDLE_VALUE;
        }
#else
        if (pid_ > 0) {
            kill(pid_, SIGTERM);
            int st = 0;
            waitpid(pid_, &st, 0);
            pid_ = 0;
        }
        if (pipefd_[0] >= 0) { close(pipefd_[0]); pipefd_[0] = -1; }
        if (pipefd_[1] >= 0) { close(pipefd_[1]); pipefd_[1] = -1; }
#endif
        port_ = 0;
    }

    std::uint16_t port() const { return port_; }

private:
    bool wait_for_port() {
        std::string acc;
        acc.reserve(512);
        auto deadline = std::chrono::steady_clock::now()
                      + std::chrono::seconds(8);
        char buf[256];
        while (std::chrono::steady_clock::now() < deadline) {
#if defined(_WIN32)
            DWORD avail = 0;
            if (!PeekNamedPipe(rd_pipe_, nullptr, 0, nullptr, &avail, nullptr))
                break;
            if (avail > 0) {
                DWORD n = 0;
                if (!ReadFile(rd_pipe_, buf, sizeof(buf) - 1, &n, nullptr))
                    break;
                buf[n] = '\0';
                acc.append(buf, n);
            } else {
                Sleep(30);
            }
#else
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(pipefd_[0], &fds);
            timeval tv{0, 30000};
            if (select(pipefd_[0] + 1, &fds, nullptr, nullptr, &tv) > 0) {
                ssize_t n = read(pipefd_[0], buf, sizeof(buf) - 1);
                if (n <= 0) break;
                buf[static_cast<std::size_t>(n)] = '\0';
                acc.append(buf, static_cast<std::size_t>(n));
            }
#endif
            const std::string marker = "listening on ";
            auto pos = acc.find(marker);
            if (pos != std::string::npos) {
                auto colon = acc.find(':', pos + marker.size());
                auto end   = acc.find_first_of(" (", colon + 1);
                if (colon != std::string::npos && end != std::string::npos) {
                    try {
                        port_ = static_cast<std::uint16_t>(
                            std::stoul(acc.substr(colon + 1, end - colon - 1)));
                        return port_ != 0;
                    } catch (...) {
                        return false;
                    }
                }
            }
        }
        stop();
        return false;
    }

    std::uint16_t port_ = 0;
#if defined(_WIN32)
    HANDLE proc_  = nullptr;
    HANDLE thread_ = nullptr;
    HANDLE rd_pipe_ = INVALID_HANDLE_VALUE;
#else
    pid_t pid_ = 0;
    int pipefd_[2] = {-1, -1};
#endif
};

void seed_clientes_adt_fixture(const fs::path& dir) {
    ADSHANDLE hLocal = 0;
    connect_local(&hLocal, dir);

    UNSIGNED8 tbl[] = "clientes.adt";
    UNSIGNED8 flddef[] =
        "Rid,AutoInc,4;"
        "Nome,Character,20;"
        "Qtd,Integer,4;"
        "Nasc,Date,8;"
        "Obs,Memo,9";
    ADSHANDLE hSeed = 0;
    REQUIRE(AdsCreateTable(hLocal, tbl, nullptr, ADS_ADT, ADS_ANSI, 0, 0, 0,
                           flddef, &hSeed) == AE_SUCCESS);

    REQUIRE(AdsAppendRecord(hSeed) == AE_SUCCESS);
    set_field_str(hSeed, "Nome", "Ana Silva");
    UNSIGNED8 qtd_f[] = "Qtd";
    REQUIRE(AdsSetDouble(hSeed, qtd_f, 12.0) == AE_SUCCESS);
    set_field_str(hSeed, "Nasc", "19850315");
    set_field_str(hSeed, "Obs", "primeira observacao memo");
    REQUIRE(AdsWriteRecord(hSeed) == AE_SUCCESS);

    REQUIRE(AdsAppendRecord(hSeed) == AE_SUCCESS);
    set_field_str(hSeed, "Nome", "Bruno Costa");
    REQUIRE(AdsSetDouble(hSeed, qtd_f, 7.0) == AE_SUCCESS);
    set_field_str(hSeed, "Nasc", "19901120");
    set_field_str(hSeed, "Obs", "segunda linha do memo");
    REQUIRE(AdsWriteRecord(hSeed) == AE_SUCCESS);

    UNSIGNED8 idxfile[] = "clientes.adi";
    ADSHANDLE hIdxRid = 0;
    REQUIRE(AdsCreateIndex61(hSeed, idxfile, (UNSIGNED8*)"RID",
                             (UNSIGNED8*)"Rid", nullptr, nullptr, 0, 0, &hIdxRid)
            == AE_SUCCESS);
    ADSHANDLE hIdxNome = 0;
    REQUIRE(AdsCreateIndex61(hSeed, idxfile, (UNSIGNED8*)"NOME",
                             (UNSIGNED8*)"Nome", nullptr, nullptr, 0, 0, &hIdxNome)
            == AE_SUCCESS);

    REQUIRE(AdsCloseTable(hSeed) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hLocal) == AE_SUCCESS);
}

void exercise_remote_adt_client(const char* uri, const fs::path& dir) {
    UNSIGNED8 tbl[] = "clientes.adt";
    UNSIGNED8 qtd_f[] = "Qtd";
    UNSIGNED8 idxfile[] = "clientes.adi";

    UNSIGNED8 srvbuf[512]{};
    std::memcpy(srvbuf, uri, std::strlen(uri) + 1);

    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srvbuf, ADS_REMOTE_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);

    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI, ADS_SHARED,
                         ADS_COMPATIBLE_LOCKING, ADS_DEFAULT, &hTable)
            == AE_SUCCESS);

    UNSIGNED16 nf = 0;
    REQUIRE(AdsGetNumFields(hTable, &nf) == AE_SUCCESS);
    CHECK(nf == 5);

    UNSIGNED16 t_obs = 0;
    REQUIRE(AdsGetFieldType(hTable, (UNSIGNED8*)"Obs", &t_obs) == AE_SUCCESS);
    CHECK(t_obs == ADS_MEMO);

    UNSIGNED32 nrecs = 0;
    REQUIRE(AdsGetRecordCount(hTable, ADS_RESPECTFILTERS, &nrecs) == AE_SUCCESS);
    CHECK(nrecs == 2u);

    REQUIRE(AdsGotoRecord(hTable, 1) == AE_SUCCESS);
    CHECK(get_field_str(hTable, "Nome") == "Ana Silva");
    CHECK(get_field_str(hTable, "Obs") == "primeira observacao memo");
    double q = 0;
    REQUIRE(AdsGetDouble(hTable, qtd_f, &q) == AE_SUCCESS);
    CHECK(q == 12.0);

    REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
    set_field_str(hTable, "Nome", "Carla Dias");
    REQUIRE(AdsSetDouble(hTable, qtd_f, 99.0) == AE_SUCCESS);
    set_field_str(hTable, "Nasc", "20000101");
    set_field_str(hTable, "Obs", "memo remoto via tcp");
    REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);

    REQUIRE(AdsGetRecordCount(hTable, ADS_RESPECTFILTERS, &nrecs) == AE_SUCCESS);
    CHECK(nrecs == 3u);

    REQUIRE(AdsGotoRecord(hTable, 3) == AE_SUCCESS);
    CHECK(get_field_str(hTable, "Nome") == "Carla Dias");
    CHECK(get_field_str(hTable, "Obs") == "memo remoto via tcp");
    REQUIRE(AdsGetDouble(hTable, qtd_f, &q) == AE_SUCCESS);
    CHECK(q == 99.0);

    ADSHANDLE idx_handles[4]{};
    UNSIGNED16 idx_count = 4;
    REQUIRE(AdsOpenIndex(hTable, idxfile, idx_handles, &idx_count) == AE_SUCCESS);
    REQUIRE(idx_count >= 2);

    ADSHANDLE hNomeIdx = idx_handles[0];
    for (UNSIGNED16 i = 0; i < idx_count; ++i) {
        std::string sk = "Bruno Costa";
        sk.append(20 - sk.size(), ' ');
        UNSIGNED8 key[20]{};
        std::memcpy(key, sk.data(), sk.size());
        UNSIGNED16 found = 0;
        if (AdsSeek(idx_handles[i], key, 20, 0, 0, &found) == AE_SUCCESS
            && found) {
            hNomeIdx = idx_handles[i];
            break;
        }
    }
    std::string sk = "Bruno Costa";
    sk.append(20 - sk.size(), ' ');
    UNSIGNED8 key[20]{};
    std::memcpy(key, sk.data(), sk.size());
    UNSIGNED16 found = 0;
    REQUIRE(AdsSeek(hNomeIdx, key, 20, 0, 0, &found) == AE_SUCCESS);
    CHECK(found != 0);
    CHECK(get_field_str(hTable, "Nome") == "Bruno Costa");
    CHECK(get_field_str(hTable, "Obs") == "segunda linha do memo");

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);

    ADSHANDLE hLocal = 0;
    connect_local(&hLocal, dir);
    hTable = 0;
    REQUIRE(AdsOpenTable(hLocal, tbl, nullptr, ADS_ADT, ADS_ANSI, ADS_READONLY,
                         ADS_COMPATIBLE_LOCKING, ADS_DEFAULT, &hTable)
            == AE_SUCCESS);
    REQUIRE(AdsGetRecordCount(hTable, ADS_RESPECTFILTERS, &nrecs) == AE_SUCCESS);
    CHECK(nrecs == 3u);
    REQUIRE(AdsGotoRecord(hTable, 3) == AE_SUCCESS);
    CHECK(get_field_str(hTable, "Nome") == "Carla Dias");
    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hLocal) == AE_SUCCESS);
}

constexpr char kTiposExtFlddef[] =
    "Rid,AutoInc,4;"
    "Nome,Character,20;"
    "Ativo,Logical;"
    "Qtd,Integer,4;"
    "Valor,Numeric,8,4;"
    "Nasc,Date,8;"
    "Hora,Time,4;"
    "Criado,Timestamp;"
    "Saldo,Money;"
    "Obs,Memo,9;"
    "Foto,Image,9;"
    "Raw,Binary,9";

void append_tipos_ext_row(ADSHANDLE hTable, int row_no) {
    char name[24];
    std::snprintf(name, sizeof(name), "Tipo%02d", row_no);
    set_field_str(hTable, "Nome", name);

    UNSIGNED8 ativo_f[] = "Ativo";
    REQUIRE(AdsSetLogical(hTable, ativo_f, row_no % 2 != 0 ? 1 : 0) == AE_SUCCESS);

    UNSIGNED8 qtd_f[] = "Qtd";
    REQUIRE(AdsSetDouble(hTable, qtd_f, static_cast<double>(row_no * 10))
            == AE_SUCCESS);

    UNSIGNED8 valor_f[] = "Valor";
    REQUIRE(AdsSetDouble(hTable, valor_f, 3.1415 + row_no) == AE_SUCCESS);

    set_field_str(hTable, "Nasc", row_no == 1 ? "19991231" : "20000615");

    // 14:30:00.000 = 52'200'000 ms since midnight
    UNSIGNED8 hora_f[] = "Hora";
    REQUIRE(AdsSetDouble(hTable, hora_f, 52200000.0) == AE_SUCCESS);

    set_field_str(hTable, "Criado",
                  row_no == 1 ? "20250621143000" : "20251225120000");

    UNSIGNED8 saldo_f[] = "Saldo";
    REQUIRE(AdsSetDouble(hTable, saldo_f, 999.99 + row_no) == AE_SUCCESS);

    std::string memo = "memo tipos #" + std::to_string(row_no);
    set_field_str(hTable, "Obs", memo);

    std::string foto = "img#" + std::to_string(row_no);
    set_field_str(hTable, "Foto", foto);

    std::string raw = "bin#" + std::to_string(row_no);
    set_field_str(hTable, "Raw", raw);
}

void verify_tipos_ext_row(ADSHANDLE hTable, int row_no) {
    char want_name[24];
    std::snprintf(want_name, sizeof(want_name), "Tipo%02d", row_no);
    CHECK(get_field_str(hTable, "Nome") == want_name);

    UNSIGNED8 ativo_f[] = "Ativo";
    UNSIGNED16 ativo = 99;
    REQUIRE(AdsGetLogical(hTable, ativo_f, &ativo) == AE_SUCCESS);
    CHECK(ativo == (row_no % 2 != 0 ? 1 : 0));

    UNSIGNED8 qtd_f[] = "Qtd";
    double q = 0;
    REQUIRE(AdsGetDouble(hTable, qtd_f, &q) == AE_SUCCESS);
    CHECK(q == static_cast<double>(row_no * 10));

    UNSIGNED8 valor_f[] = "Valor";
    REQUIRE(AdsGetDouble(hTable, valor_f, &q) == AE_SUCCESS);
    CHECK(q > 3.1415 + row_no - 0.001);
    CHECK(q < 3.1415 + row_no + 0.001);

    CHECK(get_field_str(hTable, "Nasc")
          == (row_no == 1 ? "19991231" : "20000615"));
    CHECK(get_field_str(hTable, "Criado")
          == (row_no == 1 ? "20250621143000" : "20251225120000"));

    UNSIGNED8 saldo_f[] = "Saldo";
    REQUIRE(AdsGetDouble(hTable, saldo_f, &q) == AE_SUCCESS);
    CHECK(q > 999.99 + row_no - 0.01);
    CHECK(q < 999.99 + row_no + 0.01);

    std::string want_memo = "memo tipos #" + std::to_string(row_no);
    CHECK(get_field_str(hTable, "Obs") == want_memo);
    CHECK(get_field_str(hTable, "Foto") == ("img#" + std::to_string(row_no)));
    CHECK(get_field_str(hTable, "Raw") == ("bin#" + std::to_string(row_no)));
}

void seed_tipos_ext_fixture(const fs::path& dir, int seed_rows = 1) {
    ADSHANDLE hLocal = 0;
    connect_local(&hLocal, dir);

    UNSIGNED8 tbl[] = "tipos_ext.adt";
    UNSIGNED8 flddef[sizeof(kTiposExtFlddef)]{};
    std::memcpy(flddef, kTiposExtFlddef, sizeof(kTiposExtFlddef));

    ADSHANDLE hSeed = 0;
    REQUIRE(AdsCreateTable(hLocal, tbl, nullptr, ADS_ADT, ADS_ANSI, 0, 0, 0,
                           flddef, &hSeed) == AE_SUCCESS);

    for (int r = 1; r <= seed_rows; ++r) {
        REQUIRE(AdsAppendRecord(hSeed) == AE_SUCCESS);
        append_tipos_ext_row(hSeed, r);
        REQUIRE(AdsWriteRecord(hSeed) == AE_SUCCESS);
    }

    REQUIRE(AdsCloseTable(hSeed) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hLocal) == AE_SUCCESS);
}

void exercise_remote_tipos_ext_client(const char* uri, const fs::path& dir) {
    UNSIGNED8 tbl[] = "tipos_ext.adt";
    UNSIGNED8 srvbuf[512]{};
    std::memcpy(srvbuf, uri, std::strlen(uri) + 1);

    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srvbuf, ADS_REMOTE_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);

    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI, ADS_SHARED,
                         ADS_COMPATIBLE_LOCKING, ADS_DEFAULT, &hTable)
            == AE_SUCCESS);

    UNSIGNED16 nf = 0;
    REQUIRE(AdsGetNumFields(hTable, &nf) == AE_SUCCESS);
    CHECK(nf == 12);

    UNSIGNED16 t_ativo = 0, t_criado = 0, t_saldo = 0;
    REQUIRE(AdsGetFieldType(hTable, (UNSIGNED8*)"Ativo", &t_ativo) == AE_SUCCESS);
    REQUIRE(AdsGetFieldType(hTable, (UNSIGNED8*)"Criado", &t_criado) == AE_SUCCESS);
    REQUIRE(AdsGetFieldType(hTable, (UNSIGNED8*)"Saldo", &t_saldo) == AE_SUCCESS);
    CHECK(t_ativo == ADS_LOGICAL);
    CHECK(t_criado == ADS_TIMESTAMP);
    CHECK(t_saldo == ADS_MONEY);

    UNSIGNED32 nrecs = 0;
    REQUIRE(AdsGetRecordCount(hTable, ADS_RESPECTFILTERS, &nrecs) == AE_SUCCESS);
    CHECK(nrecs == 1u);

    REQUIRE(AdsGotoRecord(hTable, 1) == AE_SUCCESS);
    verify_tipos_ext_row(hTable, 1);

    // Second row — all extended write ops over the wire.
    REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
    append_tipos_ext_row(hTable, 2);
    REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);

    REQUIRE(AdsGetRecordCount(hTable, ADS_RESPECTFILTERS, &nrecs) == AE_SUCCESS);
    CHECK(nrecs == 2u);
    REQUIRE(AdsGotoRecord(hTable, 2) == AE_SUCCESS);
    verify_tipos_ext_row(hTable, 2);

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);

    ADSHANDLE hLocal = 0;
    connect_local(&hLocal, dir);
    hTable = 0;
    REQUIRE(AdsOpenTable(hLocal, tbl, nullptr, ADS_ADT, ADS_ANSI, ADS_READONLY,
                         ADS_COMPATIBLE_LOCKING, ADS_DEFAULT, &hTable)
            == AE_SUCCESS);
    REQUIRE(AdsGetRecordCount(hTable, ADS_RESPECTFILTERS, &nrecs) == AE_SUCCESS);
    CHECK(nrecs == 2u);
    REQUIRE(AdsGotoRecord(hTable, 2) == AE_SUCCESS);
    verify_tipos_ext_row(hTable, 2);
    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hLocal) == AE_SUCCESS);
}

} // namespace

TEST_CASE("M5 ADT scope: create from zero — autoinc char int date memo") {
    wipe_scope_tmp();
    auto dir = scope_tmp_dir();

    ADSHANDLE hConn = 0;
    connect_local(&hConn, dir);

    UNSIGNED8 tbl[] = "clientes.adt";
    UNSIGNED8 flddef[] =
        "Rid,AutoInc,4;"
        "Nome,Character,20;"
        "Qtd,Integer,4;"
        "Nasc,Date,8;"
        "Obs,Memo,9";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsCreateTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI, 0, 0, 0,
                           flddef, &hTable) == AE_SUCCESS);

    UNSIGNED16 nf = 0;
    REQUIRE(AdsGetNumFields(hTable, &nf) == AE_SUCCESS);
    CHECK(nf == 5);

    UNSIGNED16 t_rid = 0, t_nome = 0, t_qtd = 0, t_nasc = 0, t_obs = 0;
    REQUIRE(AdsGetFieldType(hTable, (UNSIGNED8*)"Rid", &t_rid) == AE_SUCCESS);
    REQUIRE(AdsGetFieldType(hTable, (UNSIGNED8*)"Nome", &t_nome) == AE_SUCCESS);
    REQUIRE(AdsGetFieldType(hTable, (UNSIGNED8*)"Qtd", &t_qtd) == AE_SUCCESS);
    REQUIRE(AdsGetFieldType(hTable, (UNSIGNED8*)"Nasc", &t_nasc) == AE_SUCCESS);
    REQUIRE(AdsGetFieldType(hTable, (UNSIGNED8*)"Obs", &t_obs) == AE_SUCCESS);
    CHECK(t_rid == ADS_AUTOINC);
    CHECK(t_nome == ADS_STRING);
    CHECK(t_qtd == ADS_INTEGER);
    CHECK(t_nasc == ADS_DATE);
    CHECK(t_obs == ADS_MEMO);

    CHECK(fs::exists(dir / "clientes.adm"));

    // Record 1
    REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
    set_field_str(hTable, "Nome", "Ana Silva");
    UNSIGNED8 qtd_f[] = "Qtd";
    REQUIRE(AdsSetDouble(hTable, qtd_f, 12.0) == AE_SUCCESS);
    set_field_str(hTable, "Nasc", "19850315");
    set_field_str(hTable, "Obs", "primeira observacao memo");
    REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);

    // Record 2
    REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
    set_field_str(hTable, "Nome", "Bruno Costa");
    REQUIRE(AdsSetDouble(hTable, qtd_f, 7.0) == AE_SUCCESS);
    set_field_str(hTable, "Nasc", "19901120");
    set_field_str(hTable, "Obs", "segunda linha do memo");
    REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);

    UNSIGNED8 idxfile[] = "clientes.adi";
    ADSHANDLE hIdxRid = 0;
    REQUIRE(AdsCreateIndex61(hTable, idxfile, (UNSIGNED8*)"RID",
                             (UNSIGNED8*)"Rid", nullptr, nullptr, 0, 0, &hIdxRid)
            == AE_SUCCESS);
    ADSHANDLE hIdxNome = 0;
    REQUIRE(AdsCreateIndex61(hTable, idxfile, (UNSIGNED8*)"NOME",
                             (UNSIGNED8*)"Nome", nullptr, nullptr, 0, 0, &hIdxNome)
            == AE_SUCCESS);

    auto tags = openads::drivers::adi::AdiIndex::list_tags(
        (dir / "clientes.adi").string());
    REQUIRE(tags);
    CHECK(tags.value().size() == 2u);

    // Seek by name index before close
    std::string sk = "Bruno Costa";
    sk.append(20 - sk.size(), ' ');
    UNSIGNED8 seek_key[20]{};
    std::memcpy(seek_key, sk.data(), sk.size());
    UNSIGNED16 found = 0;
    REQUIRE(AdsSeek(hIdxNome, seek_key, 20, 0, 0, &found) == AE_SUCCESS);
    CHECK(found != 0);
    CHECK(get_field_str(hTable, "Nome") == "Bruno Costa");
    CHECK(get_field_str(hTable, "Obs") == "segunda linha do memo");

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);

    // Reopen read-only — round-trip
    hTable = 0;
    REQUIRE(AdsOpenTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI, ADS_READONLY,
                         ADS_COMPATIBLE_LOCKING, ADS_DEFAULT, &hTable)
            == AE_SUCCESS);

    UNSIGNED32 nrecs = 0;
    REQUIRE(AdsGetRecordCount(hTable, ADS_RESPECTFILTERS, &nrecs) == AE_SUCCESS);
    CHECK(nrecs == 2u);

    REQUIRE(AdsGotoRecord(hTable, 1) == AE_SUCCESS);
    CHECK(get_field_str(hTable, "Nome") == "Ana Silva");
    CHECK(get_field_str(hTable, "Obs") == "primeira observacao memo");
    double q = 0;
    REQUIRE(AdsGetDouble(hTable, qtd_f, &q) == AE_SUCCESS);
    CHECK(q == 12.0);
    CHECK(get_field_str(hTable, "Nasc") == "19850315");

    double rid1 = 0;
    REQUIRE(AdsGetDouble(hTable, (UNSIGNED8*)"Rid", &rid1) == AE_SUCCESS);
    CHECK(rid1 >= 1.0);

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
}

TEST_CASE("M5 ADT scope: stress append + index order + memo round-trip") {
    wipe_scope_tmp();
    auto dir = scope_tmp_dir();

    ADSHANDLE hConn = 0;
    connect_local(&hConn, dir);

    UNSIGNED8 tbl[] = "stress.adt";
    UNSIGNED8 flddef[] =
        "Rid,AutoInc,4;Tag,Character,12;Val,Integer,4;Obs,Memo,9";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsCreateTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI, 0, 0, 0,
                           flddef, &hTable) == AE_SUCCESS);

    UNSIGNED8 idxfile[] = "stress.adi";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTable, idxfile, (UNSIGNED8*)"TAG",
                             (UNSIGNED8*)"Tag", nullptr, nullptr, 0, 0, &hIdx)
            == AE_SUCCESS);

    constexpr int kN = 250;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kN; ++i) {
        REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
        char name[16];
        std::snprintf(name, sizeof(name), "R%04d", i);
        set_field_str(hTable, "Tag", name);
        UNSIGNED8 vf[] = "Val";
        REQUIRE(AdsSetDouble(hTable, vf, static_cast<double>(i)) == AE_SUCCESS);
        std::string memo = "memo payload #" + std::to_string(i) + " x";
        memo.append(static_cast<std::size_t>(i % 40), 'M');
        set_field_str(hTable, "Obs", memo);
        REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);
    }
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - t0)
                  .count();
    MESSAGE("stress: ", kN, " appends in ", ms, " ms");

    REQUIRE(AdsSetIndexOrderByHandle(hTable, hIdx) == AE_SUCCESS);
    REQUIRE(AdsGotoTop(hTable) == AE_SUCCESS);

    int walked = 0;
    std::string prev;
    while (true) {
        UNSIGNED16 eof = 0;
        REQUIRE(AdsAtEOF(hTable, &eof) == AE_SUCCESS);
        if (eof) break;
        std::string tag = get_field_str(hTable, "Tag");
        if (!prev.empty()) CHECK(tag > prev);
        prev = tag;
        int idx = std::atoi(tag.c_str() + 1);
        std::string want = "memo payload #" + std::to_string(idx) + " x";
        want.append(static_cast<std::size_t>(idx % 40), 'M');
        CHECK(get_field_str(hTable, "Obs") == want);
        ++walked;
        REQUIRE(AdsSkip(hTable, 1) == AE_SUCCESS);
    }
    CHECK(walked == kN);

    std::string sk = "R0123";
    sk.append(12 - sk.size(), ' ');
    UNSIGNED8 key[12]{};
    std::memcpy(key, sk.data(), sk.size());
    UNSIGNED16 found = 0;
    REQUIRE(AdsSeek(hIdx, key, 12, 0, 0, &found) == AE_SUCCESS);
    CHECK(found != 0);
    CHECK(get_field_str(hTable, "Tag") == trim_spaces("R0123"));

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
}

TEST_CASE("M5 ADT scope: all types local round-trip") {
    wipe_scope_tmp();
    auto dir = scope_tmp_dir();

    ADSHANDLE hConn = 0;
    connect_local(&hConn, dir);

    UNSIGNED8 tbl[] = "tipos_ext.adt";
    UNSIGNED8 flddef[sizeof(kTiposExtFlddef)]{};
    std::memcpy(flddef, kTiposExtFlddef, sizeof(kTiposExtFlddef));

    ADSHANDLE hTable = 0;
    REQUIRE(AdsCreateTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI, 0, 0, 0,
                           flddef, &hTable) == AE_SUCCESS);

    CHECK(fs::exists(dir / "tipos_ext.adm"));

    UNSIGNED16 t_valor = 0, t_foto = 0, t_raw = 0;
    REQUIRE(AdsGetFieldType(hTable, (UNSIGNED8*)"Valor", &t_valor) == AE_SUCCESS);
    REQUIRE(AdsGetFieldType(hTable, (UNSIGNED8*)"Foto", &t_foto) == AE_SUCCESS);
    REQUIRE(AdsGetFieldType(hTable, (UNSIGNED8*)"Raw", &t_raw) == AE_SUCCESS);
    // Numeric with decimals is ADT type 2 (ASCII digits, declared scale), the
    // way ADS stores it; it used to be a binary DOUBLE and lost the scale.
    CHECK(t_valor == ADS_NUMERIC);
    CHECK(t_foto == ADS_IMAGE);
    CHECK(t_raw == ADS_BINARY);

    REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
    append_tipos_ext_row(hTable, 1);
    REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);
    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);

    hTable = 0;
    REQUIRE(AdsOpenTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI, ADS_READONLY,
                         ADS_COMPATIBLE_LOCKING, ADS_DEFAULT, &hTable)
            == AE_SUCCESS);
    REQUIRE(AdsGotoRecord(hTable, 1) == AE_SUCCESS);
    verify_tipos_ext_row(hTable, 1);
    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
}

TEST_CASE("M5 ADT scope: all types remote wire") {
    wipe_scope_tmp();
    auto dir = scope_tmp_dir();
    seed_tipos_ext_fixture(dir, 1);

    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    char uri[512];
    std::snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(srv.port()), dir.string().c_str());
    exercise_remote_tipos_ext_client(uri, dir);
    srv.stop();
}

TEST_CASE("M5 ADT scope: all types openads_serverd (ace client)") {
    const char* exe = serverd_exe_path();
    if (exe == nullptr || !fs::exists(exe)) {
        MESSAGE("openads_serverd not found — skip");
        return;
    }

    wipe_scope_tmp();
    auto dir = scope_tmp_dir();
    seed_tipos_ext_fixture(dir, 1);

    ServerdProcess serverd;
    REQUIRE(serverd.start(fs::path(exe), dir));
    MESSAGE("openads_serverd port=", serverd.port());

    char uri[512];
    std::snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(serverd.port()), dir.string().c_str());
    exercise_remote_tipos_ext_client(uri, dir);
    serverd.stop();
}

TEST_CASE("M5 ADT scope: remote server (tcp wire)") {
    wipe_scope_tmp();
    auto dir = scope_tmp_dir();
    seed_clientes_adt_fixture(dir);

    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    char uri[512];
    std::snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(srv.port()), dir.string().c_str());
    exercise_remote_adt_client(uri, dir);
    srv.stop();
}

TEST_CASE("M5 ADT scope: openads_serverd subprocess (ace client)") {
    const char* exe = serverd_exe_path();
    if (exe == nullptr || !fs::exists(exe)) {
        MESSAGE("openads_serverd not found — skip (build openads_serverd "
                "or set OPENADS_TEST_SERVERD)");
        return;
    }

    wipe_scope_tmp();
    auto dir = scope_tmp_dir();
    seed_clientes_adt_fixture(dir);

    ServerdProcess serverd;
    REQUIRE(serverd.start(fs::path(exe), dir));
    MESSAGE("openads_serverd pid port=", serverd.port());

    char uri[512];
    std::snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(serverd.port()), dir.string().c_str());
    exercise_remote_adt_client(uri, dir);
    serverd.stop();
}