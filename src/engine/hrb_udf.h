// Server-side Harbour UDFs from a loaded `.hrb` module (LetoDB
// `letoudf.hrb` pattern — canonical upstream is Kresin's LetoDB,
// see docs/server-udf-hrb-design.md).
//
// Shape A: scalar expression functions for index keys and FOR
// conditions. The engine evaluator (index_expr.cpp) falls back here
// when a function name is not a native builtin.
//
// This header is dependency-free on purpose: `openads_core` (which
// also ships inside ace64.dll — a Harbour client process already
// has its OWN hbvm, so core must never link Harbour symbols) only
// holds the provider interface. The Harbour implementation lives
// in hrb_harbour.* and is linked into openads_serverd (and unit
// tests) only, registering itself via set_backend().

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace openads::engine::hrb_udf {

// One scalar argument / result exchanged with an HRB function.
// Strings are raw bytes — never codepage-translated (index keys
// are byte-exact). Dates travel as Date (Harbour Julian day);
// backends format them as YYYYMMDD when the engine needs text.
struct Scalar {
    enum class Kind { Nil, String, Number, Logical, Date };
    Kind        kind = Kind::Nil;
    std::string s;
    double      n    = 0.0;
    bool        l    = false;
    long        jul  = 0;
};

// Provider implemented by the Harbour backend (or by tests).
// All methods must be thread-safe; the engine serialises calls,
// but load/unload/list may arrive from other threads.
struct IBackend {
    virtual ~IBackend() = default;
    virtual bool load(const std::string& path, std::string& err) = 0;
    virtual void unload() = 0;
    virtual bool loaded() const = 0;
    virtual bool has(const std::string& upper_name) = 0;
    virtual bool call(const std::string& upper_name, const Scalar* args,
                      std::size_t argc, Scalar& out,
                      std::string& err) = 0;
    virtual std::vector<std::string> functions() = 0;
    virtual std::string module_path() const = 0;
};

// Register the backend (serverd startup / tests). Replaces any
// previous registration. Pass nullptr to detach.
void set_backend(std::shared_ptr<IBackend> backend);

// True once a backend is registered AND a module is loaded.
bool available();

// Load `path` through the backend (runs UDF_Init when exported).
// False + `err` when no backend is registered or the load fails —
// callers should fail startup, a half-loaded UDF set would build
// wrong indexes silently.
bool load(const std::string& path, std::string& err);

// Unload (runs UDF_Exit). Restart-to-refresh in production.
void unload();

// Uppercase-name lookup, e.g. has("REVERSE").
bool has(const std::string& upper_name);

// Call a loaded function. False + `err` when unavailable, unknown,
// or the UDF raised.
bool call(const std::string& upper_name, const Scalar* args,
          std::size_t argc, Scalar& out, std::string& err);

// Exported function names (uppercase), for startup logging.
std::vector<std::string> functions();

// Loaded module path, empty when none.
std::string module_path();

}  // namespace openads::engine::hrb_udf
