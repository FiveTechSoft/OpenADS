// Harbour VM backend for server-side UDFs — see hrb_harbour.h.
// Only compiled with OPENADS_WITH_HARBOUR_UDF.
//
// All Harbour calls go through hb_vmTryEval(): name resolution,
// per-thread attach, the error-handler envelope and stack cleanup
// are Harbour-maintained, so a UDF runtime error unwinds to the
// bridge instead of killing the server. Worker threads attach
// explicitly via hb_vmThreadInit (TryEval's inner re-enter only
// works on attached threads).
//
// One deliberate addition: runner.c's HB_HRB* functions have NO
// init table anywhere in Harbour itself (verified: initsymb.c's RT
// table lacks them), so in a static link they never register.
// Publish them with our own table (same shape as the RDD drivers');
// the HB_FUNCNAME references also pull runner.o deterministically.

#include "engine/hrb_harbour.h"

#include <cstdio>
#include <cstring>
#include <mutex>

// Harbour headers warn under strict sets; third-party code, silence
// locally. Clang first (__clang__ also defines __GNUC__): the
// -Wunknown-warning-option shield must come first so version-specific
// groups (unknown to older Clangs, e.g. 18) degrade to silence
// instead of erroring. (MinGW-GCC builds carry no strict flags;
// MSVC gets push,0 below.)
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-warning-option"
#pragma clang diagnostic ignored "-Wconversion"
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wpedantic"
#pragma clang diagnostic ignored "-Wshadow"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wzero-as-null-pointer-constant"
#pragma clang diagnostic ignored "-Wc2y-extensions"
#pragma clang diagnostic ignored "-Wlanguage-extension-token"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include "hbapi.h"
#include "hbapierr.h"
#include "hbapiitm.h"
#include "hbstack.h"
#include "hbthread.h"
#include "hbvm.h"
#include "hbinit.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

// HB_HRB_BIND_DEFAULT from hbhrb.ch: never overwrite existing
// functions at load; resolution happens per call.
#define OPENADS_HRB_BIND_DEFAULT 0x0

// hb_vmTryEval takes varargs; index UDFs stay far below this.
#define OPENADS_HRB_MAX_ARGS 8

namespace openads::engine::hrb_udf {

HB_FUNC_EXTERN( HB_HRBLOAD );
HB_FUNC_EXTERN( HB_HRBDO );
HB_FUNC_EXTERN( HB_HRBUNLOAD );
HB_FUNC_EXTERN( HB_HRBGETFUNSYM );
HB_FUNC_EXTERN( HB_HRBGETFUNLIST );

HB_INIT_SYMBOLS_BEGIN( openads_hrb__InitSymbols )
{ "HB_HRBLOAD",       { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_HRBLOAD ) },      NULL },
{ "HB_HRBDO",         { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_HRBDO ) },        NULL },
{ "HB_HRBUNLOAD",     { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_HRBUNLOAD ) },    NULL },
{ "HB_HRBGETFUNSYM",  { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_HRBGETFUNSYM ) }, NULL },
{ "HB_HRBGETFUNLIST", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_HRBGETFUNLIST ) }, NULL }
HB_INIT_SYMBOLS_END( openads_hrb__InitSymbols )

namespace {

// Julian day number (Harbour date) -> YYYYMMDD. Fliegel–Van Flandern.
std::string julian_to_ymd(long jd) {
    long l = jd + 68569;
    long n = (4 * l) / 146097;
    l = l - (146097 * n + 3) / 4;
    long i = (4000 * (l + 1)) / 1461001;
    l = l - (1461 * i) / 4 + 31;
    long j = (80 * l) / 2447;
    long d = l - (2447 * j) / 80;
    l = j / 11;
    long m = j + 2 - 12 * l;
    long y = 100 * (n - 49) + i + l;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04ld%02ld%02ld", y, m, d);
    return std::string(buf);
}

Scalar scalar_from_item(PHB_ITEM p) {
    Scalar out;
    if (p == nullptr) return out;
    const HB_TYPE t = hb_itemType(p);
    if ((t & HB_IT_STRING) != 0) {
        out.kind = Scalar::Kind::String;
        const char* ptr = hb_itemGetCPtr(p);
        const HB_SIZE len = hb_itemGetCLen(p);
        out.s.assign(ptr != nullptr ? ptr : "",
                     static_cast<std::size_t>(len));
    } else if ((t & HB_IT_NUMERIC) != 0) {
        out.kind = Scalar::Kind::Number;
        out.n = hb_itemGetND(p);
    } else if ((t & HB_IT_LOGICAL) != 0) {
        // xBase key convention for logicals.
        out.kind = Scalar::Kind::String;
        out.s = (hb_itemGetL(p) != HB_FALSE) ? "T" : "F";
    } else if ((t & HB_IT_DATE) != 0) {
        out.kind = Scalar::Kind::String;
        out.s = julian_to_ymd(hb_itemGetDL(p));
    }
    return out;
}

PHB_ITEM new_scalar_item(const Scalar& a) {
    PHB_ITEM p = hb_itemNew(nullptr);
    switch (a.kind) {
        case Scalar::Kind::Number:
            hb_itemPutND(p, a.n);
            break;
        case Scalar::Kind::Logical:
            hb_itemPutL(p, (a.l != false) ? HB_TRUE : HB_FALSE);
            break;
        case Scalar::Kind::Date:
            hb_itemPutDL(p, a.jul);
            break;
        case Scalar::Kind::String:
            hb_itemPutCL(p, a.s.data(),
                         static_cast<HB_SIZE>(a.s.size()));
            break;
        case Scalar::Kind::Nil:
            break;
    }
    return p;
}

void release_items(PHB_ITEM* items, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i)
        if (items[i] != nullptr) hb_itemRelease(items[i]);
}

// hb_vmInit must run exactly once per process (a second init
// corrupts the VM — observed as an AV inside the module's
// _INITSTATICS on the next load). Backends come and go; this stays.
void ensure_vm_started() {
    static std::once_flag once;
    std::call_once(once, [] { hb_vmInit(HB_FALSE); });
}

// Attach-once per calling thread (server worker threads are foreign
// to the VM; TryEval only re-enters attached ones).
void ensure_thread_attached() {
    thread_local bool attached = false;
    if (!attached) {
        hb_vmThreadInit(nullptr);
        attached = true;
    }
}

// Raw protected call: arg items are caller-owned (released here).
// On success *result_out receives an OWNED clone of the return
// value (TryEval transfer semantics — caller releases it).
bool protected_call_raw(const char* name, PHB_ITEM* arg_items,
                        std::size_t argc, PHB_ITEM& result_out,
                        std::string& err) {
    result_out = nullptr;
    if (argc > OPENADS_HRB_MAX_ARGS) {
        err = "too many arguments";
        return false;
    }
    ensure_thread_attached();
    PHB_ITEM name_item = hb_itemNew(nullptr);
    hb_itemPutC(name_item, name);
    PHB_ITEM result = nullptr;
    bool ok = false;
    switch (argc) {
        case 0:
            ok = hb_vmTryEval(&result, name_item, 0) != HB_FALSE;
            break;
        case 1:
            ok = hb_vmTryEval(&result, name_item, 1,
                              arg_items[0]) != HB_FALSE;
            break;
        case 2:
            ok = hb_vmTryEval(&result, name_item, 2, arg_items[0],
                              arg_items[1]) != HB_FALSE;
            break;
        case 3:
            ok = hb_vmTryEval(&result, name_item, 3, arg_items[0],
                              arg_items[1], arg_items[2]) != HB_FALSE;
            break;
        case 4:
            ok = hb_vmTryEval(&result, name_item, 4, arg_items[0],
                              arg_items[1], arg_items[2],
                              arg_items[3]) != HB_FALSE;
            break;
        case 5:
            ok = hb_vmTryEval(&result, name_item, 5, arg_items[0],
                              arg_items[1], arg_items[2], arg_items[3],
                              arg_items[4]) != HB_FALSE;
            break;
        case 6:
            ok = hb_vmTryEval(&result, name_item, 6, arg_items[0],
                              arg_items[1], arg_items[2], arg_items[3],
                              arg_items[4], arg_items[5]) != HB_FALSE;
            break;
        case 7:
            ok = hb_vmTryEval(&result, name_item, 7, arg_items[0],
                              arg_items[1], arg_items[2], arg_items[3],
                              arg_items[4], arg_items[5],
                              arg_items[6]) != HB_FALSE;
            break;
        default:
            ok = hb_vmTryEval(&result, name_item, 8, arg_items[0],
                              arg_items[1], arg_items[2], arg_items[3],
                              arg_items[4], arg_items[5], arg_items[6],
                              arg_items[7]) != HB_FALSE;
            break;
    }
    hb_itemRelease(name_item);
    if (!ok) {
        // TryEval leaves the error object behind on the failure path —
        // surface subsystem/operation/description (plus the first
        // string arg, which for link errors names the missing symbol).
        std::string detail;
        if (result != nullptr &&
            (hb_itemType(result) & HB_IT_OBJECT) != 0) {
            const char* sub = hb_errGetSubSystem(result);
            const char* op = hb_errGetOperation(result);
            const char* desc = hb_errGetDescription(result);
            detail += " [";
            detail += (sub != nullptr) ? sub : "?";
            detail += "/";
            detail += (op != nullptr) ? op : "?";
            detail += "] ";
            detail += (desc != nullptr) ? desc : "";
            PHB_ITEM ea = hb_errGetArgs(result);
            if (ea != nullptr && (hb_itemType(ea) & HB_IT_ARRAY) != 0) {
                const HB_SIZE n = hb_arrayLen(ea);
                for (HB_SIZE i = 1; i <= n; ++i) {
                    PHB_ITEM e = hb_arrayGetItemPtr(ea, i);
                    if (e != nullptr &&
                        (hb_itemType(e) & HB_IT_STRING) != 0) {
                        const char* s = hb_itemGetCPtr(e);
                        if (s != nullptr && s[0] != '\0') {
                            detail += " '";
                            detail += s;
                            detail += "'";
                        }
                        break;
                    }
                }
            }
        }
        if (result != nullptr) hb_itemRelease(result);
        err = std::string("UDF '") + name + "' failed" + detail;
        return false;
    }
    result_out = result;
    return true;
}

// Protected call of a Harbour function by (uppercased) name with
// caller-owned scalar args.
bool protected_call(const char* name, const Scalar* args,
                    std::size_t argc, Scalar& out, std::string& err) {
    out = Scalar{};
    if (args == nullptr && argc != 0) {
        err = "internal: null args with nonzero argc";
        return false;
    }
    PHB_ITEM items[OPENADS_HRB_MAX_ARGS];
    for (std::size_t i = 0; i < argc; ++i)
        items[i] = new_scalar_item(args[i]);
    PHB_ITEM result = nullptr;
    const bool ok = protected_call_raw(name, items, argc, result, err);
    release_items(items, argc);
    if (!ok) return false;
    if (result != nullptr) {
        out = scalar_from_item(result);
        hb_itemRelease(result);
    }
    return true;
}

}  // namespace

HarbourBackend::~HarbourBackend() {
    if (module_ != nullptr) {
        // Best-effort UDF_Exit; the process is going away anyway.
        // No lock: destruction happens single-threaded at shutdown.
        unload();
    }
}

bool HarbourBackend::loaded() const { return module_ != nullptr; }

std::string HarbourBackend::module_path() const { return module_path_; }

bool HarbourBackend::load(const std::string& path, std::string& err) {
    if (module_ != nullptr) {
        err = "an HRB module is already loaded";
        return false;
    }
    if (!vm_started_) {
        ensure_vm_started();  // no Main proc — we are the host
        vm_started_ = true;
    }
    // HB_HRBLOAD( mode, file ) -> keeper handle (owned GC-pointer
    // item; kept for unload()/functions()).
    PHB_ITEM raw_args[2];
    raw_args[0] = hb_itemNew(nullptr);
    hb_itemPutNI(raw_args[0], OPENADS_HRB_BIND_DEFAULT);
    raw_args[1] = hb_itemNew(nullptr);
    hb_itemPutCL(raw_args[1], path.data(),
                 static_cast<HB_SIZE>(path.size()));
    PHB_ITEM kept = nullptr;
    std::string raw_err;
    const bool ok =
        protected_call_raw("HB_HRBLOAD", raw_args, 2, kept, raw_err);
    release_items(raw_args, 2);
    if (!ok || kept == nullptr ||
        (hb_itemType(kept) & HB_IT_POINTER) == 0) {
        if (kept != nullptr) hb_itemRelease(kept);
        err = "hb_hrbLoad failed for '" + path + "': " + raw_err;
        return false;
    }
    module_ = kept;
    module_path_ = path;
    // UDF_Init when exported (LetoDB parity). Failure is fatal — a
    // half-initialised module must never serve keys.
    if (has("UDF_INIT")) {
        Scalar out;
        std::string call_err;
        if (!call("UDF_INIT", nullptr, 0, out, call_err)) {
            unload();
            err = "UDF_Init failed for '" + path + "': " + call_err;
            return false;
        }
    }
    return true;
}

void HarbourBackend::unload() {
    if (module_ == nullptr) return;
    auto* kept = static_cast<PHB_ITEM>(module_);
    if (has("UDF_EXIT")) {
        Scalar out;
        std::string ignored;
        (void)call("UDF_EXIT", nullptr, 0, out, ignored);
    }
    PHB_ITEM raw_args[1] = {kept};
    PHB_ITEM result = nullptr;
    std::string ignored;
    (void)protected_call_raw("HB_HRBUNLOAD", raw_args, 1, result,
                             ignored);
    if (result != nullptr) hb_itemRelease(result);
    hb_itemRelease(kept);
    module_ = nullptr;
    module_path_.clear();
}

bool HarbourBackend::has(const std::string& upper_name) {
    if (module_ == nullptr) return false;
    ensure_thread_attached();
    return hb_dynsymFindName(upper_name.c_str()) != nullptr;
}

bool HarbourBackend::call(const std::string& upper_name,
                          const Scalar* args, std::size_t argc,
                          Scalar& out, std::string& err) {
    out = Scalar{};
    if (module_ == nullptr) {
        err = "no HRB module loaded";
        return false;
    }
    return protected_call(upper_name.c_str(), args, argc, out, err);
}

std::vector<std::string> HarbourBackend::functions() {
    std::vector<std::string> names;
    if (module_ == nullptr) return names;
    auto* kept = static_cast<PHB_ITEM>(module_);
    PHB_ITEM raw_args[1] = {kept};
    PHB_ITEM result = nullptr;
    std::string err;
    if (!protected_call_raw("HB_HRBGETFUNLIST", raw_args, 1, result,
                            err) ||
        result == nullptr) {
        if (result != nullptr) hb_itemRelease(result);
        return names;
    }
    if ((hb_itemType(result) & HB_IT_ARRAY) != 0) {
        const HB_SIZE n = hb_arrayLen(result);
        for (HB_SIZE i = 1; i <= n; ++i) {
            PHB_ITEM e = hb_arrayGetItemPtr(result, i);
            if (e != nullptr && (hb_itemType(e) & HB_IT_STRING) != 0) {
                const char* s = hb_arrayGetCPtr(result, i);
                if (s != nullptr) names.emplace_back(s);
            }
        }
    }
    hb_itemRelease(result);
    return names;
}

}  // namespace openads::engine::hrb_udf
