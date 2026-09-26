file(GLOB CNA_STORAGE_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
cna_add_module(cna_storage Storage ${CNA_STORAGE_SOURCES})
# StorageDevice/StorageContainer's public API names Microsoft::Xna::Framework::PlayerIndex
# and the headers use the CNAEXT marker + the header-only PathContainment internals -- all
# owned by the core module. The monolithic global include root used to satisfy this
# silently; the physical layout states the real include-contract edge instead. Headers-only
# (no core symbol referenced), so storage's accepted link closure is unchanged.
target_link_libraries(cna_storage PUBLIC cna_core_headers)
# StorageDevice uses a dedicated browser IDBFS mount. Keep these flags on the
# module's consumer link interface so every Emscripten application that uses CNA
# storage initializes and populates the mount before its first XNA API call.
if(EMSCRIPTEN)
    target_link_options(cna_storage INTERFACE
        -lidbfs.js
        "SHELL:--pre-js ${CMAKE_CURRENT_SOURCE_DIR}/src/WebStoragePre.js"
    )
endif()
cna_link_sharp_runtime(cna_storage PUBLIC Core.Base IO Runtime Threading)
cna_link_sharp_runtime(cna_storage PRIVATE Storage)
# Storage owns its portable save-root policy.  It intentionally does not depend on the platform
# module: persistence is a standard-filesystem concern, not a window/input capability.
