// hrb_udf delegation — backend-agnostic, no Harbour dependency.
// See hrb_udf.h.

#include "engine/hrb_udf.h"

#include <mutex>

namespace openads::engine::hrb_udf {
namespace {

std::mutex& backend_mu() {
    static std::mutex m;
    return m;
}

std::shared_ptr<IBackend>& backend_slot() {
    static std::shared_ptr<IBackend> b;
    return b;
}

}  // namespace

void set_backend(std::shared_ptr<IBackend> backend) {
    std::lock_guard<std::mutex> lk(backend_mu());
    backend_slot() = std::move(backend);
}

bool available() {
    std::lock_guard<std::mutex> lk(backend_mu());
    return backend_slot() != nullptr && backend_slot()->loaded();
}

bool load(const std::string& path, std::string& err) {
    std::lock_guard<std::mutex> lk(backend_mu());
    if (backend_slot() == nullptr) {
        err = "no Harbour UDF backend registered (build lacks "
              "OPENADS_WITH_HARBOUR_UDF)";
        return false;
    }
    return backend_slot()->load(path, err);
}

void unload() {
    std::lock_guard<std::mutex> lk(backend_mu());
    if (backend_slot() != nullptr) backend_slot()->unload();
}

bool has(const std::string& upper_name) {
    std::lock_guard<std::mutex> lk(backend_mu());
    return backend_slot() != nullptr &&
           backend_slot()->has(upper_name);
}

bool call(const std::string& upper_name, const Scalar* args,
          std::size_t argc, Scalar& out, std::string& err) {
    std::lock_guard<std::mutex> lk(backend_mu());
    out = Scalar{};
    if (backend_slot() == nullptr) {
        err = "no Harbour UDF backend registered";
        return false;
    }
    return backend_slot()->call(upper_name, args, argc, out, err);
}

std::vector<std::string> functions() {
    std::lock_guard<std::mutex> lk(backend_mu());
    if (backend_slot() == nullptr) return {};
    return backend_slot()->functions();
}

std::string module_path() {
    std::lock_guard<std::mutex> lk(backend_mu());
    if (backend_slot() == nullptr) return {};
    return backend_slot()->module_path();
}

}  // namespace openads::engine::hrb_udf
