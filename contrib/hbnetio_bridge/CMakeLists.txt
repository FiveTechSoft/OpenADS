# ============================================================================
# hbnetio Bridge for OpenADS
# ============================================================================
#
# This module provides a C++ bridge between harbour's hbnetio virtual file
# system and RPC capabilities and OpenADS's network transport layer.
#
# Components:
#   1. VfsAdapter         — maps hb_vf*() operations over ITransport
#   2. DistributedLockMgr — distributed file locking (like __isLocked)
#   3. RpcBridge          — hbnetio RPC <-> OpenADS Session dispatch
#
# To enable in your OpenADS build:
#   cmake -DOPENADS_WITH_HBNETIO_BRIDGE=ON ..
#
# Dependencies:
#   - OpenADS core (always)
#   - ITransport implementation (PlainTransport or TlsTransport)
#
# ============================================================================

option(OPENADS_WITH_HBNETIO_BRIDGE
    "Build the hbnetio VFS/RPC bridge for OpenADS" OFF)

if(OPENADS_WITH_HBNETIO_BRIDGE)
    target_sources(openads_core PRIVATE
        contrib/hbnetio_bridge/vfs_adapter.cpp
        contrib/hbnetio_bridge/dist_lock_mgr.cpp
        contrib/hbnetio_bridge/rpc_bridge.cpp
    )
    target_compile_definitions(openads_core PUBLIC
        OPENADS_WITH_HBNETIO_BRIDGE=1)
endif()
