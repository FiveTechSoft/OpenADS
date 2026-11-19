#pragma once
#include "util/result.h"

namespace openads::network {
// Keep the twin-before-engine order, but never acknowledge an ignored twin
// error. Finish includes pending-append disposition and engine flush.
template<class TwinFlush, class Finish>
util::Result<void> checked_record_flush(TwinFlush&& twin, Finish&& finish) {
    const auto code = twin();
    if (code != 0)
        return util::Error{static_cast<int>(code), 0,
                           "FlushTable: twin flush failed", ""};
    return finish();
}
}
