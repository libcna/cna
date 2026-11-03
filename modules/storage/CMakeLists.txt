file(GLOB CNA_STORAGE_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
cna_add_module(cna_storage Storage ${CNA_STORAGE_SOURCES})
# StorageDevice/StorageContainer's public API names Microsoft::Xna::Framework::PlayerIndex
# and the headers use the CNAEXT marker + the header-only PathContainment internals -- all
# owned by the core module. The monolithic global include root used to satisfy this
# silently; the physical layout states the real include-contract edge instead. Headers-only
# (no core symbol referenced), so storage's accepted link closure is unchanged.
target_link_libraries(cna_storage PUBLIC cna_core_headers)
# StorageDevice uses a dedicated browser IDBFS mount. Keep these flags on the
# module's consumer link interface so Emscripten applications using the legacy
# filesystem initialize and populate the mount before their first XNA API call.
# WasmFS cannot link IDBFS; its storage API reports unavailable instead of
# silently pretending that an in-memory save is persistent.
if(EMSCRIPTEN AND (NOT CNA_ENABLE_EMSCRIPTEN_THREADS OR NOT CNA_EMSCRIPTEN_USE_WASMFS))
    # The installed SDK carries its own copy: an exported source path breaks a moved SDK.
    set(_cna_storage_pre_js_dir "${CMAKE_INSTALL_DATADIR}/cna/emscripten")
    # The pre-js calls FS, IDBFS and add/removeRunDependency before main(). Emscripten includes
    # them only for a program whose own code reaches the filesystem; a small C program linking
    # the static C API does not (MTH-PLAT-004 D2: "addRunDependency is not defined" at startup).
    target_link_options(cna_storage INTERFACE
        -lidbfs.js
        -sFORCE_FILESYSTEM=1
        "SHELL:--pre-js $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/WebStoragePre.js>$<INSTALL_INTERFACE:$<INSTALL_PREFIX>/${_cna_storage_pre_js_dir}/WebStoragePre.js>"
    )
    if(CNA_INSTALL_CXX)
        install(FILES src/WebStoragePre.js DESTINATION "${_cna_storage_pre_js_dir}" COMPONENT CNACpp)
    endif()
endif()
cna_link_sharp_runtime(cna_storage PUBLIC Core.Base IO Runtime Threading)
cna_link_sharp_runtime(cna_storage PRIVATE Storage)
# Storage owns its portable save-root policy.  It intentionally does not depend on the platform
# module: persistence is a standard-filesystem concern, not a window/input capability.
