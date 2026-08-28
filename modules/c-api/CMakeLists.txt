# SPDX-License-Identifier: MS-PL

include(GNUInstallDirs)

function(cna_c_api_enable_strict_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endfunction()

# plans/plan_cabi.md CABI-14: SHARED under Emscripten means a PIC *side module*, which is neither
# what a wasm consumer wants nor linkable here -- the vendored SDL3 is built non-PIC, so wasm-ld
# refuses its relocations outright. A wasm consumer wants an ESM factory plus a .wasm, which is a
# static library linked into a module target (cna_c_api_wasm, below).
if(EMSCRIPTEN)
    set(CNA_C_API_LIBRARY_KIND STATIC)
else()
    set(CNA_C_API_LIBRARY_KIND SHARED)
endif()

add_library(cna_c_api ${CNA_C_API_LIBRARY_KIND}
    src/CnaCApi.cpp
    src/CnaCApiAudio.cpp
    src/CnaCApiXact.cpp
    src/CnaCApiColor.cpp
    src/CnaCApiCnb.cpp
    src/CnaCApiContent.cpp
    src/CnaCApiContentReaders.cpp
    src/CnaCApiCoreExt.cpp
    src/CnaCApiCurve.cpp
    src/CnaCApiDetail.cpp
    src/CnaCApiDisplay.cpp
    src/CnaCApiEffects.cpp
    src/CnaCApiGraphics.cpp
    src/CnaCApiGraphicsDevice.cpp
    src/CnaCApiEngineLayer.cpp
    src/CnaCApiGraphicsExt.cpp
    src/CnaCApiGraphicsResource.cpp
    src/CnaCApiGraphicsState.cpp
    src/CnaCApiGamerServices.cpp
    src/CnaCApiAchievements.cpp
    src/CnaCApiGamers.cpp
    src/CnaCApiGamerProperties.cpp
    src/CnaCApiAvatars.cpp
    src/CnaCApiLeaderboards.cpp
    src/CnaCApiGuide.cpp
    src/CnaCApiGeometry.cpp
    src/CnaCApiRenderTarget.cpp
    src/CnaCApiSpriteFont.cpp
    src/CnaCApiStorage.cpp
    src/CnaCApiInput.cpp
    src/CnaCApiInputHaptics.cpp
    src/CnaCApiInputJoystick.cpp
    src/CnaCApiInputDevices.cpp
    src/CnaCApiMath.cpp
    src/CnaCApiMedia.cpp
    src/CnaCApiMediaLibrary.cpp
    src/CnaCApiMediaPlayer.cpp
    src/CnaCApiVideo.cpp
    src/CnaCApiSensors.cpp
    src/CnaCApiDevices.cpp
    src/CnaCApiGameComponents.cpp
    src/CnaCApiGameProperties.cpp
    src/CnaCApiGraphicsDeviceManager.cpp
    src/CnaCApiMatrix.cpp
    src/CnaCApiModels.cpp
    src/CnaCApiNet.cpp
    src/CnaCApiNetGamers.cpp
    src/CnaCApiNetSessions.cpp
    src/CnaCApiPackedVectors.cpp
    src/CnaCApiPlatformOverride.cpp
    src/CnaCApiQuaternion.cpp
    src/CnaCApiRuntime.cpp
    src/CnaCApiTexture.cpp
    src/CnaCApiTextureVolume.cpp
    src/CnaCApiVectors.cpp
    src/CnaCApiVertexResources.cpp
    src/CnaCApiVertexBuffer.cpp
    src/CnaCApiIndexBuffer.cpp
    src/CnaCApiVertexValues.cpp
)
add_library(CNA::CApi ALIAS cna_c_api)

set_target_properties(cna_c_api PROPERTIES
    EXPORT_NAME CApi
    OUTPUT_NAME cna_c_api
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    # CBIND-041: the installed library keeps looking beside itself. It carries DT_NEEDED entries for
    # SDL3 and FFmpeg, and CMake strips the build tree's RPATH on install, so without this a
    # deployment that ships those libraries next to libcna_c_api.so would still not load. It costs
    # nothing when they are installed system-wide. What it does NOT do is put them there --
    # docs/c-api/CONSUMING.md records that as the package's one open packaging question.
    INSTALL_RPATH "$ORIGIN"
)

# plans/plan_cabi.md CABI-12: Emscripten satisfies UNIX AND NOT APPLE, and then wasm-ld rejects
# both of these -- they are ELF symbol-visibility mechanisms with no wasm counterpart
# ("unknown argument: --exclude-libs"). A wasm build controls its surface with
# -sEXPORTED_FUNCTIONS instead, so the export list is declared there rather than lost.
if(UNIX AND NOT APPLE AND NOT EMSCRIPTEN)
    target_link_options(cna_c_api PRIVATE
        "LINKER:--exclude-libs,ALL"
        "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/cmake/CnaCApiExports.map"
    )
endif()

# ---- Wasm module (plans/plan_cabi.md CABI-14) --------------------------------------------
# The consumable artifact for a browser: an ES module factory plus a .wasm, exporting the same
# routes the ELF build publishes through its version script. The export list is generated from the
# headers rather than maintained beside them -- a name missing from -sEXPORTED_FUNCTIONS is simply
# absent from the module, with no build-time diagnostic, so the list must not be hand-written.
if(EMSCRIPTEN)
    set(CNA_C_API_WASM_EXPORTS "${CMAKE_CURRENT_BINARY_DIR}/cna_c_api_wasm_exports.json")
    # CABI-29: the generator reads the headers, so the headers are inputs. Depending only on the
    # two scripts made the list stale the moment a route was declared without a script edit: an
    # incremental build happily relinked against the previous list and the new routes were simply
    # absent from the module, which is exactly the silent failure the comment above warns about.
    # CONFIGURE_DEPENDS so a brand-new header is picked up too, not just an edit to an existing one.
    file(GLOB CNA_C_API_PUBLIC_HEADERS CONFIGURE_DEPENDS
         "${CMAKE_CURRENT_SOURCE_DIR}/include/CNA/C/*.h")
    add_custom_command(
        OUTPUT "${CNA_C_API_WASM_EXPORTS}"
        COMMAND ${CMAKE_COMMAND} -E env python3
                "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/c-api/generate_wasm_exports.py"
                --headers "${CMAKE_CURRENT_SOURCE_DIR}/include/CNA/C"
                --output "${CNA_C_API_WASM_EXPORTS}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/c-api/generate_wasm_exports.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/c-api/check_declared_exports.py"
                ${CNA_C_API_PUBLIC_HEADERS}
        COMMENT "Generating the wasm export list from the public C headers"
        VERBATIM
    )
    add_custom_target(cna_c_api_wasm_exports DEPENDS "${CNA_C_API_WASM_EXPORTS}")

    # --no-entry: this is a library, not a program. MODULARIZE/EXPORT_ES6 give cna-ts a factory it
    # can import rather than a script that installs a global.
    add_executable(cna_c_api_wasm wasm/module_entry.c)
    add_dependencies(cna_c_api_wasm cna_c_api_wasm_exports)
    target_link_libraries(cna_c_api_wasm PRIVATE cna_c_api)
    set_target_properties(cna_c_api_wasm PROPERTIES
        OUTPUT_NAME "cna_c_api"
        SUFFIX ".mjs"
    )
    target_link_options(cna_c_api_wasm PRIVATE
        "--no-entry"
        "-sMODULARIZE=1"
        "-sEXPORT_ES6=1"
        "-sEXPORT_NAME=createCnaCApi"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sSTACK_SIZE=1048576"
        "-sFORCE_FILESYSTEM=1"
        # CABI-41: addFunction/removeFunction and a growable table, without which a JavaScript
        # consumer cannot make a function pointer at all -- and this ABI is callback-driven
        # (game callbacks, ContentLost, storage disposing, resource events). The artifact was
        # exporting 2,872 routes and no way to hand any of them a callback. FS goes with it:
        # FORCE_FILESYSTEM was already set, but the object was not reachable from JS, so
        # "filesystem/title content" in the fixcnats.md Phase 7 contract had no implementation
        # on the consumer's side either.
        "-sALLOW_TABLE_GROWTH=1"
        "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','UTF8ToString','stringToUTF8','lengthBytesUTF8','HEAPU8','HEAP8','HEAP16','HEAPU16','HEAP32','HEAPU32','HEAPF32','HEAPF64','getValue','setValue','addFunction','removeFunction','FS']"
        "SHELL:-sEXPORTED_FUNCTIONS=@${CNA_C_API_WASM_EXPORTS}"
    )

    # CABI-37: the browser probe is a hand-written page, not one Emscripten emits, so nothing put
    # it beside the module it imports. It was being copied by hand, which is exactly the kind of
    # step that works once and is gone at the next clean build -- external review caught it.
    #
    # An OUTPUT rule rather than POST_BUILD, and the difference matters: POST_BUILD only runs when
    # the target relinks, so editing the page alone left a stale copy staged. This depends on the
    # page, so the page is what triggers it.
    # A plain path, not $<TARGET_FILE_DIR:>: a generator expression naming a target is not usable
    # in a custom command's OUTPUT here. The module lands in this module's own binary directory,
    # which is the same place -- CMAKE_RUNTIME_OUTPUT_DIRECTORY is set only in examples/
    # subdirectories and is directory-scoped, so nothing redirects it here. The test below still
    # passes $<TARGET_FILE_DIR:cna_c_api_wasm> to the runner, so if those two ever diverge the
    # runner fails with "missing built page" rather than testing nothing.
    set(CNA_C_API_BROWSER_PROBE "${CMAKE_CURRENT_BINARY_DIR}/browser_probe.html")
    add_custom_command(
        OUTPUT "${CNA_C_API_BROWSER_PROBE}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/wasm/browser_probe.html"
                "${CNA_C_API_BROWSER_PROBE}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/wasm/browser_probe.html"
        COMMENT "Staging the C ABI browser probe beside the wasm module"
        VERBATIM)
    add_custom_target(cna_c_api_browser_probe ALL DEPENDS "${CNA_C_API_BROWSER_PROBE}")
    add_dependencies(cna_c_api_browser_probe cna_c_api_wasm)

    # CABI-39: NODE_JS_EXECUTABLE was read here and set nowhere, so this guard was always false and
    # neither wasm test was ever registered -- they were only ever run by hand.
    #
    # The cause is worth writing down, because the symptom is misleading. Emscripten.cmake does set
    # NODE_JS_EXECUTABLE, but only inside `if (NOT DEFINED CMAKE_CROSSCOMPILING_EMULATOR)` -- and
    # `emcmake` passes CMAKE_CROSSCOMPILING_EMULATOR on the command line, so that branch never runs
    # and the variable is never defined. The emulator it passes *is* node, which makes it the right
    # thing to use. It may carry arguments, so take the first element.
    if(NOT NODE_JS_EXECUTABLE)
        if(CMAKE_CROSSCOMPILING_EMULATOR)
            list(GET CMAKE_CROSSCOMPILING_EMULATOR 0 NODE_JS_EXECUTABLE)
        elseif(DEFINED ENV{EMSDK_NODE})
            set(NODE_JS_EXECUTABLE "$ENV{EMSDK_NODE}")
        else()
            find_program(NODE_JS_EXECUTABLE NAMES node nodejs)
        endif()
    endif()
    if(NOT NODE_JS_EXECUTABLE AND CNA_BUILD_TESTS)
        message(STATUS "cna_c_api: no node found, so the wasm module and browser tests are skipped")
    endif()

    # The structural checks -- an ESM factory, one wrapper per generated name -- only prove the
    # link was configured. This instantiates the module and calls into it, which is the first
    # thing a consumer actually depends on.
    if(CNA_BUILD_TESTS AND NODE_JS_EXECUTABLE)
        add_test(NAME CApi_WasmModuleSmoke
            COMMAND "${NODE_JS_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/wasm/module_smoke.mjs"
                    "$<TARGET_FILE_DIR:cna_c_api_wasm>/cna_c_api.mjs"
                    "${CNA_C_API_WASM_EXPORTS}")

        # And the same artifact in a real browser, which is what fixcnats.md Phase 7 actually asks
        # for: node compiles the module itself, a browser fetches it over HTTP and runs it through
        # its own WebAssembly pipeline. Registered only where Playwright resolves, since a browser
        # is an environment fact rather than a build one -- and reported as a skipped test rather
        # than silently absent, so a machine that cannot run it says so.
        # Playwright is usually a global install that a bare `require` will not find, so NODE_PATH
        # is pointed at the module root belonging to *this* node -- `npm root -g` answers for
        # whichever npm is on PATH, which need not be the same installation.
        get_filename_component(_cna_node_bin_dir "${NODE_JS_EXECUTABLE}" DIRECTORY)
        get_filename_component(_cna_node_prefix "${_cna_node_bin_dir}" DIRECTORY)
        set(_cna_node_global_modules "${_cna_node_prefix}/lib/node_modules")
        if(NOT IS_DIRECTORY "${_cna_node_global_modules}")
            execute_process(
                COMMAND "${NODE_JS_EXECUTABLE}" -e "console.log(require('node:child_process').execSync('npm root -g').toString().trim())"
                OUTPUT_VARIABLE _cna_node_global_modules
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET)
        endif()

        # The gate asks the runner itself, so the two cannot disagree about what is resolvable. A
        # separate search here was narrower than the runner's own and would report a skip on a
        # machine where the test would have passed.
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E env "NODE_PATH=${_cna_node_global_modules}"
                    "${NODE_JS_EXECUTABLE}"
                    "${PROJECT_SOURCE_DIR}/scripts/run_pixijs_browser_tests.mjs"
                    --check-playwright
            RESULT_VARIABLE _cna_playwright_missing
            OUTPUT_QUIET ERROR_QUIET)
        find_program(CNA_BROWSER_FOR_WASM_PROBE
            NAMES google-chrome google-chrome-stable chromium chromium-browser)
        if(_cna_playwright_missing EQUAL 0)
            set(_cna_probe_env "NODE_PATH=${_cna_node_global_modules}")
            if(CNA_BROWSER_FOR_WASM_PROBE)
                list(APPEND _cna_probe_env "PLAYWRIGHT_CHROMIUM=${CNA_BROWSER_FOR_WASM_PROBE}")
            endif()
            add_test(NAME CApi_WasmBrowserProbe
                COMMAND ${CMAKE_COMMAND} -E env ${_cna_probe_env}
                        "${NODE_JS_EXECUTABLE}"
                        "${PROJECT_SOURCE_DIR}/scripts/run_pixijs_browser_tests.mjs"
                        "$<TARGET_FILE_DIR:cna_c_api_wasm>"
                        "browser_probe.html")
            set_tests_properties(CApi_WasmBrowserProbe PROPERTIES TIMEOUT 300)
        else()
            # SKIP_REGULAR_EXPRESSION, not PASS_REGULAR_EXPRESSION: the latter reports Passed, which
            # would fold a test that did nothing into the suite's pass count -- the very thing this
            # branch exists to avoid. Matches the convention in modules/renderers/webgpu and
            # modules/renderers/diligent.
            add_test(NAME CApi_WasmBrowserProbe
                COMMAND ${CMAKE_COMMAND} -E echo
                        "[SKIP] Playwright is not resolvable from this node, so the browser probe cannot run here.")
            set_tests_properties(CApi_WasmBrowserProbe PROPERTIES
                SKIP_REGULAR_EXPRESSION "\\[SKIP\\]")
        endif()
    endif()
endif()

target_compile_definitions(cna_c_api PRIVATE CNA_C_API_BUILD)
# CBIND-052A: the adapter library gets the same strict warnings every one of its tests already
# had. It did not before, which is how three renderers reached CNA::GraphicsRendererType with no
# C identity while -Wswitch had been reporting exactly that on an unread diagnostic stream. Two
# deliberate exclusions, so the flags mean something rather than being turned off again later:
# -Wpedantic fires only inside Sharp Runtime's Decimal.hpp, on the __int128 extension that is a
# sibling project's decision to make; and -Wmissing-field-initializers contradicts this ABI's own
# versioned-structure idiom, where `{sizeof(T), version}` leaves the remaining members
# value-initialized on purpose.
if(MSVC)
    target_compile_options(cna_c_api PRIVATE /W4 /WX)
else()
    target_compile_options(cna_c_api PRIVATE
        -Wall -Wextra -Werror -Wno-missing-field-initializers)
endif()
target_include_directories(cna_c_api
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
# Keep the initial ABI substrate narrowly linked to the canonical core module. Each later C API
# family adds the exact CNA module it adapts; linking the whole aggregate before a graphics/runtime
# adapter exists needlessly builds every renderer-facing archive.
# cna_platform joined the closure with the platform-separation merge: the exception barrier
# catches CNA::Platform::PlatformException so a platform refusal reaches a caller as
# CNA_RESULT_PLATFORM instead of falling through to CNA_RESULT_INTERNAL.
target_link_libraries(cna_c_api PRIVATE cna_core cna_platform cna_runtime cna_graphics_ext cna_storage cna_content cna_media cna_devices cna_devices_ext CNA_Net CNA_GamerServices)

# Every rule below names the same component, so `cmake --install . --component CNACApi` stages the
# C ABI and nothing else. Without it the smallest possible install is the whole project -- 113 MB of
# SDL and GoogleTest headers -- which is both wasteful and a misleading picture of what a C consumer
# actually needs, since the shared library links every canonical module privately.
# CBIND-046: the static configuration, which exists only because it can keep the same promise.
#
# Archiving the C API together with every CNA module and Sharp Runtime would publish tens of
# thousands of C++ symbols into a consumer's program. generate_static_archive.py instead partially
# links the whole closure into one relocatable object, localizes every symbol that is not part of
# the ABI, and FAILS if any non-cna_* symbol of any binding other than STB_GNU_UNIQUE survives --
# so the archive exports the same names the shared library exports, or it is not produced at all.
#
# It reads the link line CMake already computed for cna_c_api, which is why it cannot drift from the
# set of objects and archives that actually produce the working .so.
# Python3 is looked up here rather than relied upon: cmake/Tests/ModuleProbes.cmake finds it too,
# but that file is included after add_subdirectory(modules), so at this point nothing has yet.
find_package(Python3 COMPONENTS Interpreter QUIET)
# The archive is a single relocatable object holding the entire closure, so in a Debug tree it is
# a few hundred megabytes and is rewritten whenever the library relinks. That is the right artifact
# to ship and the wrong one to regenerate idly, so it can be turned off per build tree -- and when
# it is off, the consumer gate says so by name instead of quietly testing half the package.
option(CNA_C_API_BUILD_STATIC "Build and install the static C API archive" ON)
if(CNA_C_API_BUILD_STATIC AND UNIX AND NOT APPLE AND Python3_Interpreter_FOUND)
    set(CNA_C_API_STATIC_ARCHIVE "${CMAKE_CURRENT_BINARY_DIR}/libcna_c_api_static.a")
    set(CNA_C_API_STATIC_TARGETS "${CMAKE_CURRENT_BINARY_DIR}/CNACStaticTargets.cmake")
    add_custom_command(
        OUTPUT "${CNA_C_API_STATIC_ARCHIVE}" "${CNA_C_API_STATIC_TARGETS}"
        COMMAND Python3::Interpreter
            "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/c-api/generate_static_archive.py"
            --build-dir "${CMAKE_BINARY_DIR}"
            # The module's own binary directory, because CMAKE_BINARY_DIR is the *consumer's*
            # root when CNA is consumed with add_subdirectory() -- the C API then lives at
            # <build>/<subdir>/modules/c-api and the link line is not where the build root says.
            --module-binary-dir "${CMAKE_CURRENT_BINARY_DIR}"
            --output "${CNA_C_API_STATIC_ARCHIVE}"
            --targets-file "${CNA_C_API_STATIC_TARGETS}"
            --work-dir "${CMAKE_CURRENT_BINARY_DIR}/static-archive"
        DEPENDS cna_c_api "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/c-api/generate_static_archive.py"
        COMMENT "Combining the C API and its closure into a single static archive"
        VERBATIM)
    add_custom_target(cna_c_api_static ALL
        DEPENDS "${CNA_C_API_STATIC_ARCHIVE}" "${CNA_C_API_STATIC_TARGETS}")
    install(FILES "${CNA_C_API_STATIC_ARCHIVE}"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}
        COMPONENT CNACApi)
    install(FILES "${CNA_C_API_STATIC_TARGETS}"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CNA
        COMPONENT CNACApi)
endif()

if(NOT EMSCRIPTEN)
    install(TARGETS cna_c_api
        EXPORT CNACTargets
        COMPONENT CNACApi
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()
install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    COMPONENT CNACApi
)

# CBIND-045: the package carries the native libraries CNA itself builds.
#
# libcna_c_api.so records DT_NEEDED entries for SDL3, SDL3_image and SDL3_mixer, which this project
# builds from third_party/ into CNA_SDL_PREBUILT_ROOT. Shipping them in the same directory, with the
# library's INSTALL_RPATH of $ORIGIN, is what makes the installed package load with no environment
# variable and link with no -rpath-link: the loader looks beside the library and finds them.
#
# What is deliberately NOT shipped is every library CNA did not build. When the optional video
# backend is enabled, libavcodec, libavformat, libavutil and libswresample come from the distribution;
# copying those binaries into this package would take on their redistribution terms, freeze their
# soname against future security updates, and drag in their transitive dependencies. They remain a
# system dependency only for that configuration, as documented in docs/c-api/CONSUMING.md. An
# explicit CNA_ENABLE_VIDEO=OFF package has no FFmpeg edge at all.
if(NOT CNA_USE_SYSTEM_SDL AND EXISTS "${CNA_SDL_PREBUILT_ROOT}/install/lib")
    install(DIRECTORY "${CNA_SDL_PREBUILT_ROOT}/install/lib/"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}
        COMPONENT CNACApi
        # FILES_MATCHING must precede every PATTERN, including the exclusions -- CMake rejects the
        # other order outright.
        FILES_MATCHING
            # The soname symlinks come too: DT_NEEDED names libSDL3.so.0, not the versioned file.
            PATTERN "libSDL3*.so*"
            PATTERN "SDL3*.dll"
            # A consumer links CNA::CApi and never SDL directly, so SDL's own CMake package and
            # pkg-config files have no business in this install.
            PATTERN "cmake" EXCLUDE
            PATTERN "pkgconfig" EXCLUDE
    )
endif()
if(WIN32 AND NOT CNA_USE_SYSTEM_SDL AND EXISTS "${CNA_SDL_PREBUILT_ROOT}/install/bin")
    install(DIRECTORY "${CNA_SDL_PREBUILT_ROOT}/install/bin/"
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT CNACApi
        FILES_MATCHING PATTERN "SDL3*.dll"
    )
endif()
# CABI-14: a wasm build ships an ESM factory plus a .wasm, not an installable CMake
# package, and cna_c_api is STATIC there -- which makes install(EXPORT) demand every
# link dependency join the export set. Skip the package on that target rather than
# widening the export set for consumers that will never call find_package.
if(NOT EMSCRIPTEN)
    install(EXPORT CNACTargets
        FILE CNACTargets.cmake
        NAMESPACE CNA::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CNA
        COMPONENT CNACApi
    )
endif()

# CBIND-041: the targets file alone is not a package. Without a CNAConfig.cmake beside it,
# find_package(CNA CONFIG) finds nothing and every documented consumption instruction would have
# been aspirational.
#
# The package version is read out of abi.h rather than written twice, so `find_package(CNA 0.1
# CONFIG)` cannot come to mean something different from what cna_get_abi_version() returns.
#
# CBIND-062: the compatibility is SameMajorVersion, which is what docs/c-api/ABI_VERSIONING.md
# actually promises -- "a consumer must reject a different major and may require a minimum minor".
# It read SameMinorVersion until the minor first moved, and nothing noticed because it never had:
# SameMinorVersion demands an *exact* minor, so it cannot express "a minimum minor" at all, and the
# 0.1 -> 0.2 bump made `find_package(CNA 0.1 CONFIG)` fail against a library that is additively
# compatible with it in every respect. CApi_InstalledConsumer caught exactly that, which is what it
# is for. Requesting 0.1 now means "0.1 or newer within major 0", so the example keeps requesting
# the version it was written against and thereby tests the compatibility claim instead of pinning
# to whatever today's minor happens to be.
include(CMakePackageConfigHelpers)
file(STRINGS include/CNA/C/abi.h _cna_abi_version_lines
    REGEX "^#define CNA_ABI_VERSION_(MAJOR|MINOR|PATCH) UINT32_C\\([0-9]+\\)$")
foreach(_cna_abi_component MAJOR MINOR PATCH)
    foreach(_cna_abi_line IN LISTS _cna_abi_version_lines)
        if(_cna_abi_line MATCHES "^#define CNA_ABI_VERSION_${_cna_abi_component} UINT32_C\\(([0-9]+)\\)$")
            set(_cna_abi_${_cna_abi_component} "${CMAKE_MATCH_1}")
        endif()
    endforeach()
endforeach()
if(NOT DEFINED _cna_abi_MAJOR OR NOT DEFINED _cna_abi_MINOR OR NOT DEFINED _cna_abi_PATCH)
    message(FATAL_ERROR
        "The ABI version could not be read from modules/c-api/include/CNA/C/abi.h.")
endif()
set(CNA_C_ABI_VERSION "${_cna_abi_MAJOR}.${_cna_abi_MINOR}.${_cna_abi_PATCH}")

set(CNA_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
set(CNA_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CNAConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/CNAConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CNA
    PATH_VARS CNA_INSTALL_LIBDIR CNA_INSTALL_INCLUDEDIR
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/CNAConfigVersion.cmake
    VERSION ${CNA_C_ABI_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/CNAConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/CNAConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CNA
    COMPONENT CNACApi
)

if(CNA_BUILD_TESTS)
    find_package(Threads REQUIRED)

    if(UNIX AND NOT APPLE)
        add_test(NAME CApi_Exports
            COMMAND ${CMAKE_COMMAND}
                -DCNA_C_API_LIBRARY=$<TARGET_FILE:cna_c_api>
                -DCNA_C_API_NM=${CMAKE_NM}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckElfExports.cmake
        )
    endif()

    add_custom_target(cna_c_api_header_audit
        COMMAND ${CMAKE_COMMAND}
            -DCNA_C_API_INCLUDE_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckPublicHeaders.cmake
        VERBATIM
    )
    add_test(NAME CApi_HeaderAudit
        COMMAND ${CMAKE_COMMAND}
            -DCNA_C_API_INCLUDE_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckPublicHeaders.cmake
    )

    # CBIND-037E5: the `CNA::Runtime` facade is declared and never defined, which is why its rows are
    # recorded not-applicable. This is what stops that record from going stale in silence.
    add_test(NAME CApi_UnimplementedRuntimeFacade
        COMMAND ${CMAKE_COMMAND}
            -DCNA_RUNTIME_LIBRARY=$<TARGET_FILE:cna_runtime>
            -DCNA_RUNTIME_NM=${CMAKE_NM}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckUnimplementedRuntimeFacade.cmake
    )

    add_custom_target(cna_c_api_key_identity_audit
        COMMAND ${CMAKE_COMMAND}
            -DCNA_C_API_INPUT_HEADER=${CMAKE_CURRENT_SOURCE_DIR}/include/CNA/C/input.h
            -DCNA_NATIVE_KEYS_HEADER=${PROJECT_SOURCE_DIR}/modules/input/include/Microsoft/Xna/Framework/Input/Keys.hpp
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckKeyIdentities.cmake
        VERBATIM
    )
    add_test(NAME CApi_KeyIdentities
        COMMAND ${CMAKE_COMMAND}
            -DCNA_C_API_INPUT_HEADER=${CMAKE_CURRENT_SOURCE_DIR}/include/CNA/C/input.h
            -DCNA_NATIVE_KEYS_HEADER=${PROJECT_SOURCE_DIR}/modules/input/include/Microsoft/Xna/Framework/Input/Keys.hpp
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckKeyIdentities.cmake
    )

    add_custom_target(cna_c_api_gamepad_button_identity_audit
        COMMAND ${CMAKE_COMMAND}
            -DCNA_C_API_INPUT_HEADER=${CMAKE_CURRENT_SOURCE_DIR}/include/CNA/C/input.h
            -DCNA_NATIVE_BUTTONS_HEADER=${PROJECT_SOURCE_DIR}/modules/input/include/Microsoft/Xna/Framework/Input/Buttons.hpp
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckGamePadButtonIdentities.cmake
        VERBATIM
    )
    add_test(NAME CApi_GamePadButtonIdentities
        COMMAND ${CMAKE_COMMAND}
            -DCNA_C_API_INPUT_HEADER=${CMAKE_CURRENT_SOURCE_DIR}/include/CNA/C/input.h
            -DCNA_NATIVE_BUTTONS_HEADER=${PROJECT_SOURCE_DIR}/modules/input/include/Microsoft/Xna/Framework/Input/Buttons.hpp
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckGamePadButtonIdentities.cmake
    )

    add_executable(cna_c_api_abi_smoke
        tests/pure_c/AbiSmoke.c
    )
    set_target_properties(cna_c_api_abi_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_abi_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_abi_smoke)
    add_test(NAME CApi_AbiSmoke COMMAND cna_c_api_abi_smoke)

    add_executable(cna_c_api_error_smoke
        tests/pure_c/ErrorSmoke.c
    )
    set_target_properties(cna_c_api_error_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_error_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_error_smoke)
    add_test(NAME CApi_ErrorSmoke COMMAND cna_c_api_error_smoke)

    # plans/plan_binding.md CBIND-067: every CNA_Bool input refuses a byte outside {0, 1}.
    #
    # Generated from the public headers by tools/c-api/generate_bool_contract_test.py, because
    # 94 hand-written assertions would leave the guarantee exactly as fragile as the one they
    # replace -- the next flag parameter to arrive would simply not have one. The generator errors
    # on a by-value type it has no stand-in for rather than dropping that route, so the covered
    # set cannot shrink in silence.
    add_executable(cna_c_api_bool_contract_smoke
        tests/pure_c/BoolContractSmoke.c
    )
    set_target_properties(cna_c_api_bool_contract_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_bool_contract_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_bool_contract_smoke)
    add_test(NAME CApi_BoolContractSmoke COMMAND cna_c_api_bool_contract_smoke)

    add_executable(cna_c_api_core_ext_smoke
        tests/pure_c/CoreExtSmoke.c
    )
    set_target_properties(cna_c_api_core_ext_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_core_ext_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_core_ext_smoke)
    add_test(NAME CApi_CoreExtSmoke COMMAND cna_c_api_core_ext_smoke)

    add_executable(cna_c_api_math_values_smoke
        tests/pure_c/MathValuesSmoke.c
    )
    set_target_properties(cna_c_api_math_values_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_math_values_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_math_values_smoke)
    add_test(NAME CApi_MathValuesSmoke COMMAND cna_c_api_math_values_smoke)

    add_executable(cna_c_api_graphics_device_smoke
        tests/pure_c/GraphicsDeviceSmoke.c
    )
    set_target_properties(cna_c_api_graphics_device_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_graphics_device_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_graphics_device_smoke)
    add_test(NAME CApi_GraphicsDeviceSmoke COMMAND cna_c_api_graphics_device_smoke)

    add_executable(cna_c_api_draw3d_smoke
        tests/pure_c/Draw3DSmoke.c
    )
    set_target_properties(cna_c_api_draw3d_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_draw3d_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_draw3d_smoke)
    add_test(NAME CApi_Draw3DSmoke COMMAND cna_c_api_draw3d_smoke)
    # This draws and reads pixels back, so it needs a driver that can give it a GL context. Under
    # the dummy driver -- which has no OpenGL at all -- it had been failing at cna_game_create,
    # returning 1, "no window": a failure it can never get past on any machine, and one that masks
    # whatever the draw path actually does. Naming the driver and the display here rather than
    # inheriting whichever one ctest was started with keeps the run hermetic on a GL build too.
    # Same convention as CApi_OwnedGraphicsDeviceSmoke below.
    set_tests_properties(CApi_Draw3DSmoke PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}"
    )

    add_executable(cna_c_api_graphics_ext_smoke
        tests/pure_c/GraphicsExtSmoke.c
    )
    set_target_properties(cna_c_api_graphics_ext_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_graphics_ext_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_graphics_ext_smoke)
    add_test(NAME CApi_GraphicsExtSmoke COMMAND cna_c_api_graphics_ext_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_GraphicsExtSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_owned_graphics_device_smoke
        tests/pure_c/OwnedGraphicsDeviceSmoke.c
    )
    set_target_properties(cna_c_api_owned_graphics_device_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_owned_graphics_device_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_owned_graphics_device_smoke)
    add_test(NAME CApi_OwnedGraphicsDeviceSmoke
        COMMAND cna_c_api_owned_graphics_device_smoke)
    set_tests_properties(CApi_OwnedGraphicsDeviceSmoke PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    add_executable(cna_c_api_render_target_lifetime_smoke
        tests/pure_c/RenderTargetLifetimeSmoke.c
    )
    set_target_properties(cna_c_api_render_target_lifetime_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_render_target_lifetime_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_render_target_lifetime_smoke)
    add_test(NAME CApi_RenderTargetLifetimeSmoke
        COMMAND cna_c_api_render_target_lifetime_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_RenderTargetLifetimeSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_storage_smoke
        tests/pure_c/StorageSmoke.c
    )
    set_target_properties(cna_c_api_storage_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_storage_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_storage_smoke)
    add_test(NAME CApi_StorageSmoke COMMAND cna_c_api_storage_smoke)
    # The storage root is derived from the platform preference path, so the suite is pinned to a
    # build-tree location instead of writing into the developer's real user data directory.
    set_tests_properties(CApi_StorageSmoke PROPERTIES
        ENVIRONMENT "XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/storage-smoke-home"
    )

    add_executable(cna_c_api_content_reader_smoke
        tests/pure_c/ContentReaderSmoke.c
    )
    set_target_properties(cna_c_api_content_reader_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_content_reader_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_content_reader_smoke)
    add_test(NAME CApi_ContentReaderSmoke COMMAND cna_c_api_content_reader_smoke)
    set_tests_properties(CApi_ContentReaderSmoke PROPERTIES
        ENVIRONMENT "XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/content-reader-smoke-home"
    )

    add_executable(cna_c_api_net_smoke
        tests/pure_c/NetSmoke.c
    )
    set_target_properties(cna_c_api_net_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_net_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_net_smoke)
    add_test(NAME CApi_NetSmoke COMMAND cna_c_api_net_smoke)

    add_executable(cna_c_api_vector_smoke
        tests/pure_c/VectorSmoke.c
    )
    set_target_properties(cna_c_api_vector_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_vector_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_vector_smoke)
    add_test(NAME CApi_VectorSmoke COMMAND cna_c_api_vector_smoke)

    add_executable(cna_c_api_quaternion_smoke
        tests/pure_c/QuaternionSmoke.c
    )
    set_target_properties(cna_c_api_quaternion_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_quaternion_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_quaternion_smoke)
    add_test(NAME CApi_QuaternionSmoke COMMAND cna_c_api_quaternion_smoke)

    add_executable(cna_c_api_matrix_smoke
        tests/pure_c/MatrixSmoke.c
    )
    set_target_properties(cna_c_api_matrix_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_matrix_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_matrix_smoke)
    add_test(NAME CApi_MatrixSmoke COMMAND cna_c_api_matrix_smoke)

    add_executable(cna_c_api_geometry_smoke
        tests/pure_c/GeometrySmoke.c
    )
    set_target_properties(cna_c_api_geometry_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_geometry_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_geometry_smoke)
    add_test(NAME CApi_GeometrySmoke COMMAND cna_c_api_geometry_smoke)

    add_executable(cna_c_api_curve_smoke
        tests/pure_c/CurveSmoke.c
    )
    set_target_properties(cna_c_api_curve_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_curve_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_curve_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_curve_smoke)
    add_test(NAME CApi_CurveSmoke COMMAND cna_c_api_curve_smoke)

    add_executable(cna_c_api_color_smoke
        tests/pure_c/ColorSmoke.c
    )
    set_target_properties(cna_c_api_color_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_color_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_color_smoke)
    add_test(NAME CApi_ColorSmoke COMMAND cna_c_api_color_smoke)

    # CBIND-106: the .cnb container. It owns no handle and needs no game, so unlike every other
    # graphics-adjacent suite here it registers with no environment at all -- and the loop at the
    # end of this file gives it the configured test display anyway, because a test that names
    # nothing must not inherit the developer's session.
    add_executable(cna_c_api_cnb_smoke
        tests/pure_c/CnbSmoke.c
    )
    set_target_properties(cna_c_api_cnb_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_cnb_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_cnb_smoke)
    add_test(NAME CApi_CnbSmoke COMMAND cna_c_api_cnb_smoke)

    # CBIND-107: the document, its cursor and the two writers. Separate from CnbSmoke because it
    # builds a container and parses it back, which is a different kind of evidence from the
    # container primitives' one-call-per-fact checks -- and because one file covering both would be
    # two thousand lines nobody re-reads.
    add_executable(cna_c_api_cnb_document_smoke
        tests/pure_c/CnbDocumentSmoke.c
    )
    set_target_properties(cna_c_api_cnb_document_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_cnb_document_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_cnb_document_smoke)
    add_test(NAME CApi_CnbDocumentSmoke COMMAND cna_c_api_cnb_document_smoke)

    add_executable(cna_c_api_packed_vector_smoke
        tests/pure_c/PackedVectorSmoke.c
    )
    set_target_properties(cna_c_api_packed_vector_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_packed_vector_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_packed_vector_smoke)
    add_test(NAME CApi_PackedVectorSmoke COMMAND cna_c_api_packed_vector_smoke)

    add_executable(cna_c_api_vertex_value_smoke
        tests/pure_c/VertexValueSmoke.c
    )
    set_target_properties(cna_c_api_vertex_value_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_vertex_value_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_vertex_value_smoke)
    add_test(NAME CApi_VertexValueSmoke COMMAND cna_c_api_vertex_value_smoke)

    add_executable(cna_c_api_vertex_resource_smoke
        tests/pure_c/VertexResourceSmoke.c
    )
    set_target_properties(cna_c_api_vertex_resource_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_vertex_resource_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_vertex_resource_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_vertex_resource_smoke)
    add_test(NAME CApi_VertexResourceSmoke COMMAND cna_c_api_vertex_resource_smoke)

    add_executable(cna_c_api_vertex_buffer_smoke
        tests/pure_c/VertexBufferSmoke.c
    )
    set_target_properties(cna_c_api_vertex_buffer_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_vertex_buffer_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_vertex_buffer_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_vertex_buffer_smoke)
    add_test(NAME CApi_VertexBufferSmoke COMMAND cna_c_api_vertex_buffer_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_VertexBufferSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_index_buffer_smoke
        tests/pure_c/IndexBufferSmoke.c
    )
    set_target_properties(cna_c_api_index_buffer_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_index_buffer_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_index_buffer_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_index_buffer_smoke)
    add_test(NAME CApi_IndexBufferSmoke COMMAND cna_c_api_index_buffer_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_IndexBufferSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_effect_annotation_smoke
        tests/pure_c/EffectAnnotationSmoke.c
    )
    set_target_properties(cna_c_api_effect_annotation_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_effect_annotation_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_effect_annotation_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_effect_annotation_smoke)
    add_test(NAME CApi_EffectAnnotationSmoke COMMAND cna_c_api_effect_annotation_smoke)

    add_executable(cna_c_api_effect_parameter_smoke
        tests/pure_c/EffectParameterSmoke.c
    )
    set_target_properties(cna_c_api_effect_parameter_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_effect_parameter_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_effect_parameter_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_effect_parameter_smoke)
    add_test(NAME CApi_EffectParameterSmoke COMMAND cna_c_api_effect_parameter_smoke)

    add_executable(cna_c_api_effect_technique_smoke
        tests/pure_c/EffectTechniqueSmoke.c
    )
    set_target_properties(cna_c_api_effect_technique_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_effect_technique_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_effect_technique_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_effect_technique_smoke)
    add_test(NAME CApi_EffectTechniqueSmoke COMMAND cna_c_api_effect_technique_smoke)

    add_executable(cna_c_api_effect_smoke
        tests/pure_c/EffectSmoke.c
    )
    set_target_properties(cna_c_api_effect_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_effect_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_effect_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_effect_smoke)
    add_test(NAME CApi_EffectSmoke COMMAND cna_c_api_effect_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_EffectSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_basic_effect_smoke
        tests/pure_c/BasicEffectSmoke.c
    )
    set_target_properties(cna_c_api_basic_effect_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_basic_effect_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_basic_effect_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_basic_effect_smoke)
    add_test(NAME CApi_BasicEffectSmoke COMMAND cna_c_api_basic_effect_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_BasicEffectSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_stock_effect_smoke
        tests/pure_c/StockEffectSmoke.c
    )
    set_target_properties(cna_c_api_stock_effect_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_stock_effect_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_stock_effect_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_stock_effect_smoke)
    add_test(NAME CApi_StockEffectSmoke COMMAND cna_c_api_stock_effect_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_StockEffectSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_skinned_effect_smoke
        tests/pure_c/SkinnedEffectSmoke.c
    )
    set_target_properties(cna_c_api_skinned_effect_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_skinned_effect_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_skinned_effect_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_skinned_effect_smoke)
    add_test(NAME CApi_SkinnedEffectSmoke COMMAND cna_c_api_skinned_effect_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_SkinnedEffectSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_pbr_effect_smoke
        tests/pure_c/PbrEffectSmoke.c
    )
    set_target_properties(cna_c_api_pbr_effect_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_pbr_effect_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_pbr_effect_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_pbr_effect_smoke)
    add_test(NAME CApi_PbrEffectSmoke COMMAND cna_c_api_pbr_effect_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_PbrEffectSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_model_bone_smoke
        tests/pure_c/ModelBoneSmoke.c
    )
    set_target_properties(cna_c_api_model_bone_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_model_bone_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_model_bone_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_model_bone_smoke)
    add_test(NAME CApi_ModelBoneSmoke COMMAND cna_c_api_model_bone_smoke)

    add_executable(cna_c_api_model_mesh_part_smoke
        tests/pure_c/ModelMeshPartSmoke.c
    )
    set_target_properties(cna_c_api_model_mesh_part_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_model_mesh_part_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_model_mesh_part_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_model_mesh_part_smoke)
    add_test(NAME CApi_ModelMeshPartSmoke COMMAND cna_c_api_model_mesh_part_smoke)

    add_executable(cna_c_api_model_mesh_smoke
        tests/pure_c/ModelMeshSmoke.c
    )
    set_target_properties(cna_c_api_model_mesh_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_model_mesh_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_model_mesh_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_model_mesh_smoke)
    add_test(NAME CApi_ModelMeshSmoke COMMAND cna_c_api_model_mesh_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_ModelMeshSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_model_smoke
        tests/pure_c/ModelSmoke.c
    )
    set_target_properties(cna_c_api_model_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_model_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_model_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_model_smoke)
    add_test(NAME CApi_ModelSmoke COMMAND cna_c_api_model_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_ModelSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_morph_target_smoke
        tests/pure_c/MorphTargetSmoke.c
    )
    set_target_properties(cna_c_api_morph_target_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_morph_target_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_morph_target_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_morph_target_smoke)
    add_test(NAME CApi_MorphTargetSmoke COMMAND cna_c_api_morph_target_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_MorphTargetSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_skinned_model_smoke
        tests/pure_c/SkinnedModelSmoke.c
    )
    set_target_properties(cna_c_api_skinned_model_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_skinned_model_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_skinned_model_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_skinned_model_smoke)
    add_test(NAME CApi_SkinnedModelSmoke COMMAND cna_c_api_skinned_model_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_SkinnedModelSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_animation_player_smoke
        tests/pure_c/AnimationPlayerSmoke.c
    )
    set_target_properties(cna_c_api_animation_player_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_animation_player_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_animation_player_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_animation_player_smoke)
    add_test(NAME CApi_AnimationPlayerSmoke COMMAND cna_c_api_animation_player_smoke)

    add_executable(cna_c_api_graphics_resource_smoke
        tests/pure_c/GraphicsResourceSmoke.c
    )
    set_target_properties(cna_c_api_graphics_resource_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_graphics_resource_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_graphics_resource_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_graphics_resource_smoke)
    add_test(NAME CApi_GraphicsResourceSmoke COMMAND cna_c_api_graphics_resource_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_GraphicsResourceSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_texture_smoke
        tests/pure_c/TextureSmoke.c
    )
    set_target_properties(cna_c_api_texture_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_texture_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_texture_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_texture_smoke)
    add_test(NAME CApi_TextureSmoke COMMAND cna_c_api_texture_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_TextureSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_texture_volume_smoke
        tests/pure_c/TextureVolumeSmoke.c
    )
    set_target_properties(cna_c_api_texture_volume_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_texture_volume_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_texture_volume_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_texture_volume_smoke)
    add_test(NAME CApi_TextureVolumeSmoke COMMAND cna_c_api_texture_volume_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_TextureVolumeSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_lifecycle_smoke
        tests/pure_c/LifecycleSmoke.c
    )
    set_target_properties(cna_c_api_lifecycle_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_lifecycle_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_lifecycle_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_lifecycle_smoke)
    add_test(NAME CApi_LifecycleSmoke COMMAND cna_c_api_lifecycle_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_LifecycleSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_content_smoke
        tests/pure_c/ContentSmoke.c
    )
    set_target_properties(cna_c_api_content_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_content_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_content_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_content_smoke)
    add_test(NAME CApi_ContentSmoke COMMAND cna_c_api_content_smoke)
    # The manifest suite builds its own content root through the storage API, so the storage root
    # is pinned to a build-tree location; dummy audio lets the WAV load route reach its real
    # success path instead of only its unsupported one.
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_ContentSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/content-smoke-home"
        )
    else()
        set_tests_properties(CApi_ContentSmoke PROPERTIES
            ENVIRONMENT "SDL_AUDIODRIVER=dummy;XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/content-smoke-home"
        )
    endif()

    add_executable(cna_c_api_graphics_surface_smoke
        tests/pure_c/GraphicsSurfaceSmoke.c
    )
    set_target_properties(cna_c_api_graphics_surface_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_graphics_surface_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_graphics_surface_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_graphics_surface_smoke)
    add_test(NAME CApi_GraphicsSurfaceSmoke COMMAND cna_c_api_graphics_surface_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_GraphicsSurfaceSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_input_snapshots_smoke
        tests/pure_c/InputSnapshotsSmoke.c
    )
    set_target_properties(cna_c_api_input_snapshots_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_input_snapshots_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_input_snapshots_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_input_snapshots_smoke)
    add_test(NAME CApi_InputSnapshotsSmoke COMMAND cna_c_api_input_snapshots_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_InputSnapshotsSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_haptics_smoke
        tests/pure_c/HapticsSmoke.c
    )
    set_target_properties(cna_c_api_haptics_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_haptics_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_haptics_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_haptics_smoke)
    add_test(NAME CApi_HapticsSmoke COMMAND cna_c_api_haptics_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_HapticsSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_joystick_smoke
        tests/pure_c/JoystickSmoke.c
    )
    set_target_properties(cna_c_api_joystick_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_joystick_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_joystick_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_joystick_smoke)
    add_test(NAME CApi_JoystickSmoke COMMAND cna_c_api_joystick_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_JoystickSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_input_devices_smoke
        tests/pure_c/InputDevicesSmoke.c
    )
    set_target_properties(cna_c_api_input_devices_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_input_devices_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_input_devices_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_input_devices_smoke)
    add_test(NAME CApi_InputDevicesSmoke COMMAND cna_c_api_input_devices_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_InputDevicesSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_engine_layer_smoke
        tests/pure_c/EngineLayerSmoke.c
    )
    set_target_properties(cna_c_api_engine_layer_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_engine_layer_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_engine_layer_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_engine_layer_smoke)
    add_test(NAME CApi_EngineLayerSmoke COMMAND cna_c_api_engine_layer_smoke)
    # plans/plan_binding.md CBIND-084A. The suite creates a game, so it takes the same isolated
    # environment CBIND-094 gave the other game-creating smokes rather than reading the machine.
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_EngineLayerSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/engine-layer-smoke-home"
        )
    elseif(CNA_GRAPHICS_RENDERER MATCHES "^OPEN(GL|GLES)")
        set_tests_properties(CApi_EngineLayerSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=offscreen;SDL_AUDIODRIVER=dummy;XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/engine-layer-smoke-home"
        )
    else()
        set_tests_properties(CApi_EngineLayerSmoke PROPERTIES
            ENVIRONMENT "SDL_AUDIODRIVER=dummy;XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/engine-layer-smoke-home"
        )
    endif()

    add_executable(cna_c_api_media_smoke
        tests/pure_c/MediaSmoke.c
    )
    set_target_properties(cna_c_api_media_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_media_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_media_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_media_smoke)
    add_test(NAME CApi_MediaSmoke COMMAND cna_c_api_media_smoke)
    # CBIND-094. This test builds its song fixture through a real StorageContainer, so it writes
    # to the user storage root -- which resolves under $XDG_DATA_HOME, or $HOME when that is
    # unset. Left to the ambient environment it fails outright wherever $HOME is not writable,
    # which is a statement about the machine and not about the media routes. Pin the storage root
    # into the build tree the way CApi_StorageSmoke and CApi_ContentSmoke already do, and take
    # dummy audio unconditionally: loading a song reaches the audio device, and a test must not
    # depend on a reachable sound server to report on the media ABI.
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_MediaSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/media-smoke-home"
        )
    else()
        set_tests_properties(CApi_MediaSmoke PROPERTIES
            ENVIRONMENT "SDL_AUDIODRIVER=dummy;XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/media-smoke-home"
        )
    endif()

    # The media library scans the platform's user folders, which SDL resolves through
    # $XDG_CONFIG_HOME/user-dirs.dirs. Pointing that at a generated fixture gives the suite a
    # deterministic library instead of whatever music the developer's machine happens to hold --
    # and keeps the test from ever reading or writing a real user directory.
    set(CNA_C_API_MEDIA_FIXTURE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/media_fixture")
    file(MAKE_DIRECTORY "${CNA_C_API_MEDIA_FIXTURE_ROOT}/config")
    file(MAKE_DIRECTORY "${CNA_C_API_MEDIA_FIXTURE_ROOT}/Music")
    file(MAKE_DIRECTORY "${CNA_C_API_MEDIA_FIXTURE_ROOT}/Pictures")
    file(WRITE "${CNA_C_API_MEDIA_FIXTURE_ROOT}/config/user-dirs.dirs"
        "XDG_MUSIC_DIR=\"${CNA_C_API_MEDIA_FIXTURE_ROOT}/Music\"\n"
        "XDG_PICTURES_DIR=\"${CNA_C_API_MEDIA_FIXTURE_ROOT}/Pictures\"\n"
    )

    add_executable(cna_c_api_media_library_smoke
        tests/pure_c/MediaLibrarySmoke.c
    )
    set_target_properties(cna_c_api_media_library_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_compile_definitions(cna_c_api_media_library_smoke PRIVATE
        CNA_C_API_MEDIA_FIXTURE_MUSIC="${CNA_C_API_MEDIA_FIXTURE_ROOT}/Music"
        CNA_C_API_MEDIA_FIXTURE_PICTURES="${CNA_C_API_MEDIA_FIXTURE_ROOT}/Pictures"
    )
    target_link_libraries(cna_c_api_media_library_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_media_library_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_media_library_smoke)
    add_executable(cna_c_api_media_player_smoke
        tests/pure_c/MediaPlayerSmoke.c
    )
    set_target_properties(cna_c_api_media_player_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_compile_definitions(cna_c_api_media_player_smoke PRIVATE
        CNA_C_API_MEDIA_FIXTURE_MUSIC="${CNA_C_API_MEDIA_FIXTURE_ROOT}/Music"
    )
    target_link_libraries(cna_c_api_media_player_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_media_player_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_media_player_smoke)
    add_executable(cna_c_api_video_smoke
        tests/pure_c/VideoSmoke.c
    )
    set_target_properties(cna_c_api_video_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_video_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_video_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_video_smoke)
    add_executable(cna_c_api_sensor_values_smoke
        tests/pure_c/SensorValuesSmoke.c
    )
    set_target_properties(cna_c_api_sensor_values_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_sensor_values_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_sensor_values_smoke)
    add_test(NAME CApi_SensorValuesSmoke COMMAND cna_c_api_sensor_values_smoke)

    add_executable(cna_c_api_sensor_device_smoke
        tests/pure_c/SensorDeviceSmoke.c
    )
    set_target_properties(cna_c_api_sensor_device_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_sensor_device_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_sensor_device_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_sensor_device_smoke)
    add_test(NAME CApi_SensorDeviceSmoke COMMAND cna_c_api_sensor_device_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_SensorDeviceSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_audio_streaming_smoke
        tests/pure_c/AudioStreamingSmoke.c
    )
    set_target_properties(cna_c_api_audio_streaming_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_audio_streaming_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_audio_streaming_smoke)
    add_test(NAME CApi_AudioStreamingSmoke COMMAND cna_c_api_audio_streaming_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_AudioStreamingSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_audio_sound_effect_smoke
        tests/pure_c/AudioSoundEffectSmoke.c
    )
    set_target_properties(cna_c_api_audio_sound_effect_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_audio_sound_effect_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_audio_sound_effect_smoke)
    add_test(NAME CApi_AudioSoundEffectSmoke COMMAND cna_c_api_audio_sound_effect_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_AudioSoundEffectSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_avatar_identities_smoke
        tests/pure_c/AvatarIdentitiesSmoke.c
    )
    set_target_properties(cna_c_api_avatar_identities_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_avatar_identities_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_avatar_identities_smoke)
    add_test(NAME CApi_AvatarIdentitiesSmoke COMMAND cna_c_api_avatar_identities_smoke)

    add_executable(cna_c_api_guide_smoke
        tests/pure_c/GuideSmoke.c
    )
    set_target_properties(cna_c_api_guide_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_guide_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_guide_smoke)
    add_test(NAME CApi_GuideSmoke COMMAND cna_c_api_guide_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_GuideSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_gamer_properties_smoke
        tests/pure_c/GamerPropertiesSmoke.c
    )
    set_target_properties(cna_c_api_gamer_properties_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_gamer_properties_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_gamer_properties_smoke)
    add_test(NAME CApi_GamerPropertiesSmoke COMMAND cna_c_api_gamer_properties_smoke)

    add_executable(cna_c_api_leaderboards_smoke
        tests/pure_c/LeaderboardsSmoke.c
    )
    set_target_properties(cna_c_api_leaderboards_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_leaderboards_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_leaderboards_smoke)
    add_test(NAME CApi_LeaderboardsSmoke COMMAND cna_c_api_leaderboards_smoke)

    add_executable(cna_c_api_avatars_smoke
        tests/pure_c/AvatarsSmoke.c
    )
    set_target_properties(cna_c_api_avatars_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_avatars_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_avatars_smoke)
    add_test(NAME CApi_AvatarsSmoke COMMAND cna_c_api_avatars_smoke)

    add_executable(cna_c_api_achievements_smoke
        tests/pure_c/AchievementsSmoke.c
    )
    set_target_properties(cna_c_api_achievements_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_achievements_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_achievements_smoke)
    add_test(NAME CApi_AchievementsSmoke COMMAND cna_c_api_achievements_smoke)

    add_executable(cna_c_api_gamers_smoke
        tests/pure_c/GamersSmoke.c
    )
    set_target_properties(cna_c_api_gamers_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_gamers_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_gamers_smoke)
    add_test(NAME CApi_GamersSmoke COMMAND cna_c_api_gamers_smoke)
    # CBIND-094. The gamer routes bring SDL up, and a default audio driver blocks where no sound
    # server answers -- the test hung rather than failing, which is the worse of the two. It had
    # no ENVIRONMENT at all; every neighbouring registration already takes dummy drivers.
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_GamersSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy"
        )
    else()
        set_tests_properties(CApi_GamersSmoke PROPERTIES
            ENVIRONMENT "SDL_AUDIODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_gamer_identities_smoke
        tests/pure_c/GamerIdentitiesSmoke.c
    )
    set_target_properties(cna_c_api_gamer_identities_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_gamer_identities_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_gamer_identities_smoke)
    add_test(NAME CApi_GamerIdentitiesSmoke COMMAND cna_c_api_gamer_identities_smoke)

    add_executable(cna_c_api_xact_smoke
        tests/pure_c/XactSmoke.c
    )
    set_target_properties(cna_c_api_xact_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_xact_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_xact_smoke)
    add_test(NAME CApi_XactSmoke COMMAND cna_c_api_xact_smoke)

    # CBIND-040A: the lifetime and boundary contract rather than any one route's behavior.
    # Threads::Threads because it proves handle thread-affinity from a second thread.
    add_executable(cna_c_api_stress_smoke
        tests/pure_c/StressSmoke.c
    )
    set_target_properties(cna_c_api_stress_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_stress_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_stress_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_stress_smoke)
    add_test(NAME CApi_StressSmoke COMMAND cna_c_api_stress_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_XactSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_audio_3d_smoke
        tests/pure_c/Audio3DSmoke.c
    )
    set_target_properties(cna_c_api_audio_3d_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_audio_3d_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_audio_3d_smoke)
    add_test(NAME CApi_Audio3DSmoke COMMAND cna_c_api_audio_3d_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_Audio3DSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_graphics_device_manager_smoke
        tests/pure_c/GraphicsDeviceManagerSmoke.c
    )
    set_target_properties(cna_c_api_graphics_device_manager_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_graphics_device_manager_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_graphics_device_manager_smoke)
    add_test(NAME CApi_GraphicsDeviceManagerSmoke
        COMMAND cna_c_api_graphics_device_manager_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_GraphicsDeviceManagerSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_runtime_game_smoke
        tests/pure_c/RuntimeGameSmoke.c
    )
    set_target_properties(cna_c_api_runtime_game_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_runtime_game_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_runtime_game_smoke)
    add_test(NAME CApi_RuntimeGameSmoke COMMAND cna_c_api_runtime_game_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_RuntimeGameSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_runtime_components_smoke
        tests/pure_c/RuntimeComponentsSmoke.c
    )
    set_target_properties(cna_c_api_runtime_components_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_runtime_components_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_runtime_components_smoke)
    add_test(NAME CApi_RuntimeComponentsSmoke COMMAND cna_c_api_runtime_components_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_RuntimeComponentsSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_devices_smoke
        tests/pure_c/DevicesSmoke.c
    )
    set_target_properties(cna_c_api_devices_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_devices_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_devices_smoke)
    add_test(NAME CApi_DevicesSmoke COMMAND cna_c_api_devices_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_DevicesSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_sensor_events_smoke
        tests/pure_c/SensorEventsSmoke.c
    )
    set_target_properties(cna_c_api_sensor_events_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_sensor_events_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_sensor_events_smoke)
    add_test(NAME CApi_SensorEventsSmoke COMMAND cna_c_api_sensor_events_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_SensorEventsSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    # CABI-35: the frame-identity coverage needs a real decodable video, and this is the first C
    # test to want an asset at all. The source tree's own fixture directory is compiled in rather
    # than relying on the working directory, which ctest does not guarantee.
    target_compile_definitions(cna_c_api_video_smoke PRIVATE
        CNA_C_API_TEST_ASSET_DIR="${PROJECT_SOURCE_DIR}/tests/assets")
    add_test(NAME CApi_VideoSmoke COMMAND cna_c_api_video_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_VideoSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy"
        )
    else()
        set_tests_properties(CApi_VideoSmoke PROPERTIES
            ENVIRONMENT "SDL_AUDIODRIVER=dummy"
        )
    endif()

    add_test(NAME CApi_MediaPlayerSmoke COMMAND cna_c_api_media_player_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_MediaPlayerSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;XDG_CONFIG_HOME=${CNA_C_API_MEDIA_FIXTURE_ROOT}/config"
        )
    else()
        set_tests_properties(CApi_MediaPlayerSmoke PROPERTIES
            ENVIRONMENT "SDL_AUDIODRIVER=dummy;XDG_CONFIG_HOME=${CNA_C_API_MEDIA_FIXTURE_ROOT}/config"
        )
    endif()

    add_test(NAME CApi_MediaLibrarySmoke COMMAND cna_c_api_media_library_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_MediaLibrarySmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;XDG_CONFIG_HOME=${CNA_C_API_MEDIA_FIXTURE_ROOT}/config"
        )
    else()
        set_tests_properties(CApi_MediaLibrarySmoke PROPERTIES
            ENVIRONMENT "XDG_CONFIG_HOME=${CNA_C_API_MEDIA_FIXTURE_ROOT}/config"
        )
    endif()

    add_executable(cna_c_api_audio_smoke
        tests/pure_c/AudioSmoke.c
    )
    set_target_properties(cna_c_api_audio_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_audio_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_audio_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_audio_smoke)
    add_test(NAME CApi_AudioSmoke COMMAND cna_c_api_audio_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_AudioSmoke PROPERTIES
            ENVIRONMENT "SDL_AUDIODRIVER=dummy;SDL_VIDEODRIVER=dummy"
        )
    else()
        set_tests_properties(CApi_AudioSmoke PROPERTIES
            ENVIRONMENT "SDL_AUDIODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_audio_unavailable_smoke
        tests/pure_c/AudioUnavailableSmoke.c
    )
    set_target_properties(cna_c_api_audio_unavailable_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_audio_unavailable_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_audio_unavailable_smoke)
    add_test(NAME CApi_AudioUnavailableSmoke COMMAND cna_c_api_audio_unavailable_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_AudioUnavailableSmoke PROPERTIES
            ENVIRONMENT "SDL_AUDIODRIVER=cna_driver_that_does_not_exist;SDL_VIDEODRIVER=dummy"
        )
    else()
        set_tests_properties(CApi_AudioUnavailableSmoke PROPERTIES
            ENVIRONMENT "SDL_AUDIODRIVER=cna_driver_that_does_not_exist"
        )
    endif()

    add_library(cna_c_api_header_c OBJECT tests/pure_c/AbiHeaderC.c)
    set_target_properties(cna_c_api_header_c PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_include_directories(cna_c_api_header_c PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_dependencies(cna_c_api_header_c cna_c_api_header_audit)
    add_dependencies(cna_c_api_header_c cna_c_api_key_identity_audit)
    add_dependencies(cna_c_api_header_c cna_c_api_gamepad_button_identity_audit)
    cna_c_api_enable_strict_warnings(cna_c_api_header_c)

    add_library(cna_c_api_header_cpp OBJECT tests/cpp/AbiHeaderCpp.cpp)
    target_include_directories(cna_c_api_header_cpp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_dependencies(cna_c_api_header_cpp cna_c_api_header_audit)
    add_dependencies(cna_c_api_header_cpp cna_c_api_key_identity_audit)
    add_dependencies(cna_c_api_header_cpp cna_c_api_gamepad_button_identity_audit)
    cna_c_api_enable_strict_warnings(cna_c_api_header_cpp)

    add_executable(cna_c_api_handle_registry_test
        tests/cpp/HandleRegistryTest.cpp
        src/CnaCApiDetail.cpp
    )
    target_include_directories(cna_c_api_handle_registry_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/modules/core/include
        ${PROJECT_SOURCE_DIR}/modules/graphics/include
        ${PROJECT_SOURCE_DIR}/modules/storage/include
        ${PROJECT_SOURCE_DIR}/modules/content/include
        ${PROJECT_SOURCE_DIR}/modules/net/include
        ${PROJECT_SOURCE_DIR}/modules/gamer-services/include
        ${PROJECT_SOURCE_DIR}/modules/devices/include
        ${PROJECT_SOURCE_DIR}/modules/audio/include
        ${PROJECT_SOURCE_DIR}/modules/math/include
        ${PROJECT_SOURCE_DIR}/modules/platform/include
    )
    target_link_libraries(cna_c_api_handle_registry_test PRIVATE
        # plans/plan_binding.md CBIND-084A. CNA_CNAEXT reaches this target through
        # cna_build_flags, so CnaCApiDetail.hpp's EngineException firewall arm is compiled here --
        # which means the engine layer's include path has to reach it too. Linking cna_graphics_ext
        # is what supplies it, and it also keeps this target catching the same exceptions the
        # library does: a boundary test that saw a narrower firewall than production would be
        # testing a different boundary.
        cna_graphics_ext
        cna_platform
        Threads::Threads
        sharp_runtime_core_base
    )
    cna_link_sharp_runtime(cna_c_api_handle_registry_test PRIVATE Core.Base IO Runtime)
    cna_c_api_enable_strict_warnings(cna_c_api_handle_registry_test)
    add_test(NAME CApi_HandleRegistry COMMAND cna_c_api_handle_registry_test)

    add_executable(cna_c_api_boundary_detail_test
        tests/cpp/BoundaryDetailTest.cpp
        src/CnaCApi.cpp
        src/CnaCApiDetail.cpp
    )
    target_include_directories(cna_c_api_boundary_detail_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/modules/core/include
        ${PROJECT_SOURCE_DIR}/modules/graphics/include
        ${PROJECT_SOURCE_DIR}/modules/storage/include
        ${PROJECT_SOURCE_DIR}/modules/content/include
        ${PROJECT_SOURCE_DIR}/modules/net/include
        ${PROJECT_SOURCE_DIR}/modules/gamer-services/include
        ${PROJECT_SOURCE_DIR}/modules/devices/include
        ${PROJECT_SOURCE_DIR}/modules/audio/include
        ${PROJECT_SOURCE_DIR}/modules/math/include
        ${PROJECT_SOURCE_DIR}/modules/platform/include
    )
    # The storage, network-join, gamer-services and CNA core exceptions are the canonical types this test
    # throws whose constructors are not header-only, so proving their conversion needs those modules on the
    # link line.
    target_link_libraries(cna_c_api_boundary_detail_test PRIVATE
        # plans/plan_binding.md CBIND-084A. CNA_CNAEXT reaches this target through
        # cna_build_flags, so CnaCApiDetail.hpp's EngineException firewall arm is compiled here --
        # which means the engine layer's include path has to reach it too. Linking cna_graphics_ext
        # is what supplies it, and it also keeps this target catching the same exceptions the
        # library does: a boundary test that saw a narrower firewall than production would be
        # testing a different boundary.
        cna_graphics_ext
        sharp_runtime_core_base
        cna_core
        cna_platform
        cna_storage
        CNA_Net
        CNA_GamerServices
        cna_devices
    )
    cna_link_sharp_runtime(cna_c_api_boundary_detail_test PRIVATE Core.Base IO Runtime)
    cna_c_api_enable_strict_warnings(cna_c_api_boundary_detail_test)
    add_test(NAME CApi_BoundaryDetail COMMAND cna_c_api_boundary_detail_test)

    # CBIND-040B: the byte-facing surfaces against an independent oracle. Detail-level rather than
    # through the C ABI, because the subject is one decision function reached by thousands of
    # routes, and reaching it 18 million times through a public route would measure the route.
    add_executable(cna_c_api_utf8_oracle_test
        tests/cpp/Utf8OracleTest.cpp
        src/CnaCApi.cpp
        src/CnaCApiDetail.cpp
    )
    target_include_directories(cna_c_api_utf8_oracle_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/support
        ${PROJECT_SOURCE_DIR}/modules/core/include
        ${PROJECT_SOURCE_DIR}/modules/graphics/include
        ${PROJECT_SOURCE_DIR}/modules/storage/include
        ${PROJECT_SOURCE_DIR}/modules/content/include
        ${PROJECT_SOURCE_DIR}/modules/net/include
        ${PROJECT_SOURCE_DIR}/modules/gamer-services/include
        ${PROJECT_SOURCE_DIR}/modules/devices/include
        ${PROJECT_SOURCE_DIR}/modules/audio/include
        ${PROJECT_SOURCE_DIR}/modules/math/include
        ${PROJECT_SOURCE_DIR}/modules/platform/include
    )
    target_link_libraries(cna_c_api_utf8_oracle_test PRIVATE
        # plans/plan_binding.md CBIND-084A. CNA_CNAEXT reaches this target through
        # cna_build_flags, so CnaCApiDetail.hpp's EngineException firewall arm is compiled here --
        # which means the engine layer's include path has to reach it too. Linking cna_graphics_ext
        # is what supplies it, and it also keeps this target catching the same exceptions the
        # library does: a boundary test that saw a narrower firewall than production would be
        # testing a different boundary.
        cna_graphics_ext
        sharp_runtime_core_base
        cna_core
        cna_platform
        cna_storage
        CNA_Net
        CNA_GamerServices
        cna_devices
    )
    cna_link_sharp_runtime(cna_c_api_utf8_oracle_test PRIVATE Core.Base IO Runtime)
    cna_c_api_enable_strict_warnings(cna_c_api_utf8_oracle_test)
    add_test(NAME CApi_Utf8Oracle COMMAND cna_c_api_utf8_oracle_test)

    # CBIND-090: the .cube parser against an independent oracle. Unlike the UTF-8 oracle this one
    # goes through the public routes, because the subject is the parser as a caller reaches it --
    # a few thousand entries rather than eighteen million decisions, so the route costs nothing.
    add_executable(cna_c_api_cube_lut_oracle_test tests/cpp/CubeLutOracleTest.cpp)
    target_include_directories(cna_c_api_cube_lut_oracle_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/support
    )
    target_link_libraries(cna_c_api_cube_lut_oracle_test PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_cube_lut_oracle_test)
    add_test(NAME CApi_CubeLutOracle COMMAND cna_c_api_cube_lut_oracle_test)

    # CBIND-041: the only test that leaves the source tree. It installs the CNACApi component into a
    # staging prefix, configures modules/c-api/examples/c as a standalone project that knows nothing
    # but CMAKE_PREFIX_PATH, builds it and runs it. Everything else in this suite proves the ABI
    # works from inside the build; this proves the *package* works from outside it.
    #
    # ELF-only: the run step inspects an installed shared object and its RPATH. Since CBIND-045 it
    # passes no SDL path of any kind -- the package ships those libraries, so a consumer needing an
    # environment variable would be a defect rather than a fact of life, and this is where it shows.
    if(UNIX AND NOT APPLE)
        add_test(NAME CApi_InstalledConsumer
            COMMAND ${CMAKE_COMMAND}
                -DCNA_BUILD_DIR=${CMAKE_BINARY_DIR}
                -DCNA_EXAMPLE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/examples/c
                -DCNA_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/installed-consumer
                -DCNA_C_COMPILER=${CMAKE_C_COMPILER}
                -DCNA_C_COMPILER_LAUNCHER=${CMAKE_C_COMPILER_LAUNCHER}
                -DCNA_SANITIZE=${CNA_SANITIZE}
                -DCNA_EXPECT_STATIC=$<BOOL:${CNA_C_API_BUILD_STATIC}>
                -DCNA_CONSUMER_VIDEODRIVER=x11
                -DCNA_CONSUMER_DISPLAY=${CNA_TEST_DISPLAY}
                -DCNA_GRAPHICS_RENDERER=${CNA_GRAPHICS_RENDERER}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunInstalledConsumer.cmake)
        # It installs, configures, compiles and runs; it is by far the slowest test here, and the
        # steady-state cost is small because cmake --install rewrites nothing that has not changed.
        set_tests_properties(CApi_InstalledConsumer PROPERTIES TIMEOUT 900 RUN_SERIAL TRUE)
    endif()

    # The libFuzzer target is not a ctest test: it needs Clang and, by design, never terminates.
    # It is compiled anyway -- as an object library nobody links -- so that it cannot rot silently
    # between the sessions when somebody actually goes hunting with it. docs/c-api/FUZZING.md has
    # the Clang command line that turns this source into a running fuzzer.
    add_library(cna_c_api_fuzz_targets OBJECT
        tests/fuzz/StringViewFuzz.cpp
        # CBIND-090: the engine layer's only byte-facing surface, the .cube LUT parser.
        tests/fuzz/CubeLutFuzz.cpp
    )
    target_include_directories(cna_c_api_fuzz_targets PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/support
        ${PROJECT_SOURCE_DIR}/modules/core/include
        ${PROJECT_SOURCE_DIR}/modules/graphics/include
        ${PROJECT_SOURCE_DIR}/modules/storage/include
        ${PROJECT_SOURCE_DIR}/modules/content/include
        ${PROJECT_SOURCE_DIR}/modules/net/include
        ${PROJECT_SOURCE_DIR}/modules/gamer-services/include
        ${PROJECT_SOURCE_DIR}/modules/devices/include
        ${PROJECT_SOURCE_DIR}/modules/audio/include
        ${PROJECT_SOURCE_DIR}/modules/math/include
        ${PROJECT_SOURCE_DIR}/modules/platform/include
    )
    cna_link_sharp_runtime(cna_c_api_fuzz_targets PRIVATE Core.Base IO Runtime)
    cna_c_api_enable_strict_warnings(cna_c_api_fuzz_targets)
    # The exhaustive sweep is the slowest test in the suite by a wide margin, and slower still under
    # a sanitizer; the default 1500 s ctest timeout is generous, but say so rather than rely on it.
    set_tests_properties(CApi_Utf8Oracle PROPERTIES TIMEOUT 900)

    # plans/plan_binding.md CBIND-101. Every registration above that names a video driver at all
    # names it for a reason, and those are left exactly as they are. What this loop settles is the
    # rest: a test that names none inherits ctest's own environment, which on a developer machine is
    # the live desktop session -- a shared, externally mutated resource the suite does not own, and
    # which another application can resize, focus, lock or blank underneath it. Every registration
    # that scopes `SDL_VIDEODRIVER=dummy` to `SDL_RENDERER` therefore ran unpinned in every other
    # renderer's tree, which is all three this campaign uses.
    #
    # This is CBIND-094's rule applied to the display: a test whose result depends on the machine it
    # runs on is not evidence. It is deliberately **not** a claim to have diagnosed CBIND-101 -- that
    # row closes as not reproducible -- but it does remove the only structural candidate for it, and
    # it would be worth doing with no such row at all.
    get_property(_cna_c_api_registered_tests DIRECTORY PROPERTY TESTS)
    foreach(_cna_c_api_test IN LISTS _cna_c_api_registered_tests)
        get_test_property(${_cna_c_api_test} ENVIRONMENT _cna_c_api_test_environment)
        if(NOT _cna_c_api_test_environment)
            set(_cna_c_api_test_environment "")
        endif()
        string(FIND "${_cna_c_api_test_environment}" "SDL_VIDEODRIVER=" _cna_c_api_driver_named)
        if(_cna_c_api_driver_named EQUAL -1)
            list(APPEND _cna_c_api_test_environment
                "SDL_VIDEODRIVER=x11" "DISPLAY=${CNA_TEST_DISPLAY}")
            set_tests_properties(${_cna_c_api_test} PROPERTIES
                ENVIRONMENT "${_cna_c_api_test_environment}")
        endif()
    endforeach()
    unset(_cna_c_api_registered_tests)
    unset(_cna_c_api_test)
    unset(_cna_c_api_test_environment)
    unset(_cna_c_api_driver_named)
endif()
