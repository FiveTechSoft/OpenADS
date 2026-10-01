#include "abi/runtime.h"

namespace openads::abi::detail {

ProcessState& state() {
    static ProcessState process_state;
    return process_state;
}

} // namespace openads::abi::detail
