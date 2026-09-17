// Harbour VM backend for server-side UDFs (hrb_udf::IBackend).
// Compiled only with OPENADS_WITH_HARBOUR_UDF and linked into
// openads_serverd (and unit tests) — never into openads_core's
// downstream DLLs. See docs/server-udf-hrb-design.md.

#pragma once

#include "engine/hrb_udf.h"

#include <memory>

namespace openads::engine::hrb_udf {

// Embeds hbvm (MT build): hb_vmInit once, .hrb via the HB_HRBLOAD
// Harbour function, dispatch by dynamic symbol + hb_vmDo under
// hb_vmRequestReenterExt (attaches foreign worker threads on first
// use, installs the error-recovery frame). All methods assume the
// delegation layer serialises them (hrb_udf.cpp holds its mutex
// across every call).
class HarbourBackend : public IBackend {
  public:
    HarbourBackend() = default;
    ~HarbourBackend() override;

    HarbourBackend(const HarbourBackend&) = delete;
    HarbourBackend& operator=(const HarbourBackend&) = delete;

    bool load(const std::string& path, std::string& err) override;
    void unload() override;
    bool loaded() const override;
    bool has(const std::string& upper_name) override;
    bool call(const std::string& upper_name, const Scalar* args,
              std::size_t argc, Scalar& out,
              std::string& err) override;
    std::vector<std::string> functions() override;
    std::string module_path() const override;

  private:
    bool        vm_started_ = false;
    void*       module_     = nullptr;  // keeper PHB_ITEM (opaque here)
    std::string module_path_;
};

inline std::shared_ptr<IBackend> make_harbour_backend() {
    return std::make_shared<HarbourBackend>();
}

}  // namespace openads::engine::hrb_udf
