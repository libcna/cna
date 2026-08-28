# =====================================================================================
# CNA physical module composition (plans/MODULARIZATION_PLAN.md §11). Each subsystem owns
# modules/<name>/{CMakeLists.txt,include/,src/,tests/}; this file detects the shared
# third-party media dependencies, defines the shared build-flag surface, composes the
# modules, and keeps the source-partition/no-loss ownership gate.
# =====================================================================================

# --- Optional FFmpeg video backend ------------------------------------------------------------
# CNA_ENABLE_VIDEO is a three-state feature switch:
#   OFF  -- do not probe or link FFmpeg; public Video/VideoPlayer types use the unavailable backend.
#   AUTO -- use FFmpeg only when all required pkg-config modules are present.
#   ON   -- require a supported platform and all FFmpeg development modules.
#
# The platforms below deliberately remain outside the current FFmpeg integration. In particular,
# a cross-build must never resolve the build host's pkg-config files and poison its target include
# or library paths. AUTO falls back to the no-video backend there; ON reports the unsupported
# configuration immediately instead of silently changing the user's request.
set(CNA_FFMPEG_AVAILABLE OFF)
set(_cna_ffmpeg_platform_supported ON)
if(MINGW OR WIN32 OR EMSCRIPTEN OR ANDROID OR CNA_APPLE_IOS)
    set(_cna_ffmpeg_platform_supported OFF)
endif()

if(CNA_ENABLE_VIDEO STREQUAL "ON" AND NOT _cna_ffmpeg_platform_supported)
    message(FATAL_ERROR
        "CNA_ENABLE_VIDEO=ON is not supported by this target toolchain. Use AUTO/OFF, or add a "
        "target-native FFmpeg integration for this platform.")
elseif(NOT CNA_ENABLE_VIDEO STREQUAL "OFF" AND _cna_ffmpeg_platform_supported)
    if(CNA_ENABLE_VIDEO STREQUAL "ON")
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(LIBAVCODEC REQUIRED IMPORTED_TARGET libavcodec)
        pkg_check_modules(LIBAVFORMAT REQUIRED IMPORTED_TARGET libavformat)
        pkg_check_modules(LIBAVUTIL REQUIRED IMPORTED_TARGET libavutil)
        pkg_check_modules(LIBSWRESAMPLE REQUIRED IMPORTED_TARGET libswresample)
        set(CNA_FFMPEG_AVAILABLE ON)
    else()
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(LIBAVCODEC QUIET IMPORTED_TARGET libavcodec)
            pkg_check_modules(LIBAVFORMAT QUIET IMPORTED_TARGET libavformat)
            pkg_check_modules(LIBAVUTIL QUIET IMPORTED_TARGET libavutil)
            pkg_check_modules(LIBSWRESAMPLE QUIET IMPORTED_TARGET libswresample)
            if(LIBAVCODEC_FOUND AND LIBAVFORMAT_FOUND AND LIBAVUTIL_FOUND AND
               LIBSWRESAMPLE_FOUND)
                set(CNA_FFMPEG_AVAILABLE ON)
            endif()
        endif()
    endif()
endif()
unset(_cna_ffmpeg_platform_supported)

if(CNA_FFMPEG_AVAILABLE)
    message(STATUS
        "CNA: video enabled (${CNA_ENABLE_VIDEO}; FFmpeg backend cna_video_ffmpeg)")
else()
    message(STATUS
        "CNA: video disabled (${CNA_ENABLE_VIDEO}); Video playback reports NotSupportedException")
endif()

# --- Draco (KHR_draco_mesh_compression mesh decoding, plans/plan_gltf.md GLTF-353/GLTF-363) ---------
# The default is the repository's immutable gitlink at google/draco 1.5.7, not a host package:
# conformance must not silently depend on which distro version happened to be installed. A system
# package remains an explicit escape hatch for packagers, and the decoder-free configuration stays
# available as an intentional test mode for the extension-required refusal path.
#
# Off by default under Emscripten, and only there. The pinned Draco 1.5.7 predates the toolchain
# by enough that a web build of CNA cannot get through it: emsdk no longer exports the EMSCRIPTEN
# variable its CMake demands, src/draco/io/ply_reader.cc calls std::all_of on a current libc++
# without including <algorithm>, and its own --whole-archive self-link then makes wasm-ld report
# the whole library as duplicate symbols. Each of those is worked around or reported below, but
# the last one is upstream's own link recipe and CNA is not going to rewrite it inside a submodule
# pinned for conformance reasons. The result was that NO Emscripten build of CNA succeeded at all,
# by default, over a component a web game may well not use -- so the decoder-free configuration,
# which CNA already supports and documents (compressed primitives throw at import), becomes the
# web default. -DCNA_ENABLE_DRACO=ON still opts in and is expected to fail at link until the
# pinned Draco moves.
if(EMSCRIPTEN)
    option(CNA_ENABLE_DRACO "Enable KHR_draco_mesh_compression decoding" OFF)
else()
    option(CNA_ENABLE_DRACO "Enable KHR_draco_mesh_compression decoding" ON)
endif()
option(CNA_USE_SYSTEM_DRACO "Use a system Draco package instead of CNA's pinned submodule" OFF)

# plans/plan_cnb.md CNBF-105: optional Zstandard for compressed .cnb chunks.
#
# AUTO (the default) enables it when the system library is present and stays silent when it is
# not; ON demands it; OFF omits it. Absent, the reader refuses a compressed chunk with a message
# naming the missing codec, exactly as it did before any codec existed, and the writer cannot
# produce one -- so a build without zstd reads and writes every file it could read and write
# before. Compression is opt-in per chunk and OFF by default even when the library IS present:
# docs/cnb-compression-measurements.md shows it costs load time on fast storage.
set(CNA_CNB_ZSTD "AUTO" CACHE STRING "Zstandard support for .cnb chunk compression (AUTO/ON/OFF)")
set_property(CACHE CNA_CNB_ZSTD PROPERTY STRINGS AUTO ON OFF)

if(CNA_CNB_ZSTD STREQUAL "OFF")
    set(CNA_CNB_ZSTD_AVAILABLE OFF)
    message(STATUS "CNA: .cnb Zstandard support disabled -- a compressed chunk will be refused")
else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(CNA_ZSTD QUIET libzstd)
    endif()
    if(NOT CNA_ZSTD_FOUND)
        find_path(CNA_ZSTD_INCLUDE_DIR zstd.h)
        find_library(CNA_ZSTD_LIBRARY NAMES zstd)
        if(CNA_ZSTD_INCLUDE_DIR AND CNA_ZSTD_LIBRARY)
            set(CNA_ZSTD_FOUND TRUE)
            set(CNA_ZSTD_INCLUDE_DIRS "${CNA_ZSTD_INCLUDE_DIR}")
            set(CNA_ZSTD_LIBRARIES "${CNA_ZSTD_LIBRARY}")
        endif()
    endif()
    if(CNA_ZSTD_FOUND)
        set(CNA_CNB_ZSTD_AVAILABLE ON)
        message(STATUS "CNA: .cnb Zstandard support enabled (opt-in per chunk, off by default)")
    elseif(CNA_CNB_ZSTD STREQUAL "ON")
        message(FATAL_ERROR "CNA: CNA_CNB_ZSTD=ON but libzstd was not found.")
    else()
        set(CNA_CNB_ZSTD_AVAILABLE OFF)
        message(STATUS "CNA: libzstd not found -- .cnb chunk compression unavailable")
    endif()
endif()

if(NOT CNA_ENABLE_DRACO)
    set(CNA_DRACO_AVAILABLE OFF)
    if(EMSCRIPTEN)
        message(STATUS "CNA: Draco off for this web build (pinned Draco 1.5.7 does not link under "
                       "Emscripten) -- KHR_draco_mesh_compression primitives will throw at import time")
    else()
        message(STATUS "CNA: Draco explicitly disabled -- KHR_draco_mesh_compression primitives will throw at import time")
    endif()
elseif(CNA_USE_SYSTEM_DRACO)
    find_package(draco CONFIG REQUIRED)
    set(CNA_DRACO_AVAILABLE ON)
    message(STATUS "CNA: Using system Draco (${draco_VERSION}) -- KHR_draco_mesh_compression decoding enabled")
else()
    set(_cna_draco_dir "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/draco")
    if(NOT EXISTS "${_cna_draco_dir}/CMakeLists.txt")
        message(FATAL_ERROR
            "CNA: pinned Draco source not found at ${_cna_draco_dir}. "
            "Run: git submodule update --init third_party/draco, or configure "
            "-DCNA_ENABLE_DRACO=OFF for the intentional decoder-free build.")
    endif()

    # Draco 1.5.7 hard-requires an EMSCRIPTEN environment variable under an Emscripten toolchain
    # (draco_check_emscripten_environment, a FATAL_ERROR), and emsdk stopped exporting that name
    # long ago -- emsdk_env.sh sets EMSDK instead. Left alone, every web build of CNA dies at
    # configure time inside a submodule the game never asked about, with a message naming Draco's
    # README rather than anything a CNA consumer can act on. Derive it from what the toolchain
    # does provide instead of making each consumer export it by hand. Configure-time only, which
    # is where Draco reads it; nothing about the host build is touched.
    if(EMSCRIPTEN AND NOT EXISTS "$ENV{EMSCRIPTEN}")
        if(DEFINED EMSCRIPTEN_ROOT_PATH AND EXISTS "${EMSCRIPTEN_ROOT_PATH}")
            set(ENV{EMSCRIPTEN} "${EMSCRIPTEN_ROOT_PATH}")
        else()
            # emcc itself lives in the Emscripten root, so the selected compiler locates it
            # without assuming any particular emsdk layout.
            get_filename_component(_cna_emscripten_root "${CMAKE_C_COMPILER}" DIRECTORY)
            if(EXISTS "${_cna_emscripten_root}")
                set(ENV{EMSCRIPTEN} "${_cna_emscripten_root}")
            endif()
            unset(_cna_emscripten_root)
        endif()
        if(EXISTS "$ENV{EMSCRIPTEN}")
            message(STATUS "CNA: derived EMSCRIPTEN=$ENV{EMSCRIPTEN} for the vendored Draco build")
        else()
            message(FATAL_ERROR
                "CNA: the vendored Draco requires an EMSCRIPTEN environment variable pointing at "
                "the Emscripten root, and CNA could not derive one. Export it (EMSCRIPTEN=<emsdk>/"
                "upstream/emscripten), or configure -DCNA_ENABLE_DRACO=OFF for the intentional "
                "decoder-free build.")
        endif()
    endif()

    # Only the normative KHR_draco_mesh_compression bitstream feature set is enabled. Draco 1.5.7
    # still builds one static library containing its encoder too; CNA links only the public
    # draco::draco target and exposes none of that API as its own public surface.
    set(DRACO_GLTF_BITSTREAM ON CACHE BOOL "Build only Draco's glTF bitstream features" FORCE)
    set(DRACO_TESTS OFF CACHE BOOL "Disable vendored Draco tests" FORCE)
    set(DRACO_INSTALL OFF CACHE BOOL "Disable vendored Draco install rules" FORCE)
    set(DRACO_JS_GLUE OFF CACHE BOOL "Disable vendored Draco JavaScript glue" FORCE)
    set(DRACO_WASM OFF CACHE BOOL "Disable vendored Draco WASM targets" FORCE)
    set(DRACO_UNITY_PLUGIN OFF CACHE BOOL "Disable vendored Draco Unity plugin" FORCE)
    set(DRACO_MAYA_PLUGIN OFF CACHE BOOL "Disable vendored Draco Maya plugin" FORCE)
    set(DRACO_TRANSCODER_SUPPORTED OFF CACHE BOOL "Disable vendored Draco transcoder" FORCE)
    # Draco 1.5.7 predates libc++'s transitive-include cleanup: src/draco/io/ply_reader.cc calls
    # std::all_of while including only <array> and <regex>, which libstdc++ still pulls in for it
    # and a current libc++ does not. GCC hosts therefore build it and every clang build fails --
    # an Emscripten build is where CNA first meets one. The submodule must remain byte-identical,
    # so attach the compatibility include to its owning object target after it is created rather
    # than changing CMAKE_CXX_FLAGS for the whole configure scope.
    add_subdirectory("${_cna_draco_dir}" "${CMAKE_CURRENT_BINARY_DIR}/draco" EXCLUDE_FROM_ALL)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(draco_io PRIVATE -include algorithm)
    endif()
    set(CNA_DRACO_AVAILABLE ON)
    message(STATUS "CNA: Using pinned vendored Draco 1.5.7 (8786740086a9f4d83f44aa83badfbea4dce7a1b5)")
endif()

if(CNA_DRACO_AVAILABLE)
    # Draco 1.5.7's in-tree draco::draco alias exposes only its install include directory even
    # though its headers include generated draco_features.h. Keep that upstream target unmodified
    # and provide CNA's private consumption wrapper with the two build-tree include roots.
    add_library(cna_draco INTERFACE)
    target_link_libraries(cna_draco INTERFACE draco::draco)
    if(NOT CNA_USE_SYSTEM_DRACO)
        target_include_directories(cna_draco INTERFACE
            "${_cna_draco_dir}/src"
            "${CMAKE_BINARY_DIR}")
    endif()
endif()

# The root-scope test registration (cmake/UnitTests.cmake) keys its real-decoder test exclusions on
# this value; export it up from this directory scope, or the root would see an unset variable and
# silently drop the FFmpeg fixture suites on capable hosts.
set(CNA_FFMPEG_AVAILABLE ${CNA_FFMPEG_AVAILABLE} PARENT_SCOPE)
set(CNA_DRACO_AVAILABLE ${CNA_DRACO_AVAILABLE} PARENT_SCOPE)

# Public build facts shared by every CNA module and, transitively, by every consumer of a
# module. Keep this surface to ABI/header requirements only: warnings, instrumentation, linker
# selection, and other project-local policy live in private support targets from
# cmake/BuildPerformance.cmake. Public include paths deliberately do NOT live here any more --
# each module target exposes its own include/ root, and consumers reach the aggregate surface
# through target composition (the CNA umbrella, or the specific modules they link).
add_library(cna_build_config INTERFACE)
add_library(CNA::BuildConfig ALIAS cna_build_config)
target_compile_features(cna_build_config INTERFACE cxx_std_23)
target_compile_definitions(cna_build_config INTERFACE
        XNA5
        ${CNA_RENDERER_DEFINE}
        $<$<BOOL:${CNA_CNAEXT}>:CNA_CNAEXT>
        $<$<BOOL:${CNA_DEVICES}>:CNA_DEVICES>
        $<$<BOOL:${CNA_DRACO_AVAILABLE}>:CNA_DRACO_AVAILABLE>
        $<$<BOOL:${CNA_FFMPEG_AVAILABLE}>:CNA_VIDEO_AVAILABLE>
        $<$<BOOL:${CNA_FFMPEG_AVAILABLE}>:CNA_FFMPEG_AVAILABLE>
)

if(EMSCRIPTEN)
    # These are part of CNA's public WebAssembly ABI: consumers and every linked library must use
    # the same exception model. Emscripten's JS exception lowering is compatible with Asyncify
    # (native Wasm EH is not), and Asyncify lets Game::Run() suspend and restore its caller while
    # the browser owns frame scheduling. Together they preserve XNA's blocking lifetime contract
    # across the browser versions supported by Emscripten without legacy Wasm `try` instructions.
    target_link_libraries(cna_build_config INTERFACE cna_emscripten_abi)
    target_link_options(cna_build_config INTERFACE -sSTACK_SIZE=1048576)
endif()

# The current high-level mixer is SDL3_mixer based.  Do not advertise sound support in a
# configuration which deliberately selected a non-SDL3 audio transport: that would compile
# the XNA facade against an implementation which cannot exist in the resulting binary.
if(CNA_AUDIO_PLATFORM STREQUAL "SDL3")
    target_compile_definitions(cna_build_config INTERFACE SOUND_ENABLED)
endif()

# Non-ABI compiler policy for CNA-owned targets. This target is linked privately after every CNA
# target has been declared, so it cannot become an installed consumer requirement. `/utf-8` makes
# MSVC decode the repository's UTF-8 C++ sources deterministically; GCC and Clang already do so.
add_library(cna_project_options INTERFACE)
add_library(CNA::ProjectOptions ALIAS cna_project_options)
target_compile_options(cna_project_options INTERFACE
    "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

# Defines one CNA module: a STATIC library named cna_<name> with alias CNA::<Alias>, the
# shared public build surface, and the calling module's own public include/ root. No module
# needs another module's src/ as an include root: every cross-module internal header lives
# under some module's include/CNA/Internal/ (public-internal contract headers), and the only
# headers under src/ are renderer-local shader headers, addressed includer-relative from
# inside their own renderer directory.
function(cna_add_module target alias)
    add_library(${target} STATIC ${ARGN})
    add_library(CNA::${alias} ALIAS ${target})
    target_link_libraries(${target} PUBLIC cna_build_config)
    target_include_directories(${target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
endfunction()

# The graphics module's examples/ directory owns the shared renderer-agnostic example
# fixtures and the shared example-support headers ("common/PixelTestGame.hpp", ...).
# Renderer-family example registrations compile those fixtures per renderer through this
# path, and consumers of the support headers add it as an include directory.
set(CNA_GRAPHICS_EXAMPLES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/graphics/examples")

# --- Module composition. Order is readability-only: target_link_libraries resolves
# not-yet-defined target names at generate time, so cycles and forward references are fine.
add_subdirectory(core)
add_subdirectory(math)
# The platform contract (plans/plan_platform.md): composed before the subsystems that will consume
# it, so the read order matches the dependency direction even though CMake does not require it.
add_subdirectory(platform)
add_subdirectory(graphics)
add_subdirectory(input)
add_subdirectory(audio)
add_subdirectory(media)
if(CNA_FFMPEG_AVAILABLE)
    add_subdirectory(video-ffmpeg)
    # cna_media owns the stable XNA API and decoder-facing contract. The optional backend owns
    # every FFmpeg symbol and satisfies that contract only in an enabled build.
    target_link_libraries(cna_media PRIVATE cna_video_ffmpeg)
endif()
add_subdirectory(content)
add_subdirectory(storage)
add_subdirectory(runtime)
add_subdirectory(devices)
add_subdirectory(devices-ext)
add_subdirectory(graphics-ext)
if(CNA_ENABLE_NET)
    add_subdirectory(gamer-services)
    add_subdirectory(net)
endif()
add_subdirectory(renderers)

# --- CNA::CnaExt extension umbrella (cna_cnaext; named cna_noxna/CNA::NoXna before the
# 2026-08 CNAEXT naming normalization). The former cna_noxna STATIC library's implementation
# is now the graphics-ext module; the historical target name survives as an INTERFACE
# composition over the actual extension modules so existing consumers keep linking it
# unchanged (and now also receive the device extensions that used to hide inside
# cna_devices).
add_library(cna_cnaext INTERFACE)
add_library(CNA::CnaExt ALIAS cna_cnaext)
target_link_libraries(cna_cnaext INTERFACE cna_graphics_ext cna_devices_ext)

# --- Compatible umbrella. `CNA` keeps its historical meaning for every consumer: the whole
# framework plus the selected renderer, with the aggregate public include surface (via the
# module targets) and the public defines.
add_library(CNA INTERFACE)
target_link_libraries(CNA INTERFACE
        cna_runtime
        cna_devices
        cna_devices_ext
        cna_graphics_ext
        cna_cnaext
        cna_storage
        cna_content
        cna_media
        cna_audio
        cna_input
        cna_graphics_core
        cna_core
        cna_math
        cna_platform
        cna_build_config
        ${RENDERER_TARGET}
)

# The C API is a separate shared-library adapter over the canonical C++ aggregate. It remains
# opt-in until its experimental ABI is proven by the C-only test matrix.
if(CNA_BUILD_C_API)
    add_subdirectory(c-api)
endif()

# --- Physical source-partition validator (no-loss ownership gate, plans/MODULARIZATION_PLAN.md
# §5/§11): file physical module location == declared CMake ownership. Every production TU
# must live under a declared module's src/ tree (module CMakeLists glob exactly their own
# src/, so location IS ownership); a TU outside every module src/ tree, or a module
# directory outside the declared set, fails the configure. The one documented
# target-membership exception is GDI compiling eight software-module 2D TUs
# (modules/renderers/gdi/CMakeLists.txt) -- physical ownership stays with software.
# examples/ joined src/ and tests/ as a declared owned area when the tracked examples
# moved into their owning modules (module-examples campaign): a module may own
# modules/<name>/examples/, and example TUs outside every declared module still fail.
set(_cna_framework_modules
    c-api core math platform runtime graphics input audio media video-ffmpeg content storage
    devices devices-ext graphics-ext gamer-services net)
set(_cna_renderer_modules
    bgfx blend2d canvas diligent direct2d directx1 directx2 directx3 directx5 directx6
    directx7 directx8 directx9 directx10 directx11 directx12
    easygl fna3d freedirect gdi glide headless html-dom igl llgl magnum metal nanovg opengl1
    opengl2 opengl4 opengles1 openvg pixijs portablegl sdl-gpu sdl-renderer skia software sokol
    stub svg-dom tinygl vulkan
    webgpu wicked common/d3d common/mojoshader)
string(JOIN "|" _cna_framework_module_pattern ${_cna_framework_modules})
string(JOIN "|" _cna_renderer_module_pattern ${_cna_renderer_modules})
file(GLOB_RECURSE _cna_all_module_tus RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    CONFIGURE_DEPENDS "*.cpp" "*.mm")
foreach(_cna_tu IN LISTS _cna_all_module_tus)
    if(NOT _cna_tu MATCHES "^(${_cna_framework_module_pattern})/(src|tests|examples)/"
            AND NOT _cna_tu MATCHES
                "^renderers/(${_cna_renderer_module_pattern})/(src|tests|examples)/")
        message(FATAL_ERROR
            "CNA: translation unit outside every declared physical module src/tests/examples "
            "tree: modules/${_cna_tu} -- add it to a module (modules/<name>/src) or extend "
            "the declared module set here deliberately.")
    endif()
endforeach()
unset(_cna_all_module_tus)
unset(_cna_framework_module_pattern)
unset(_cna_renderer_module_pattern)

# The legacy global trees must not silently come back. Anchored on CNA_SOURCE_DIR (this
# repository's own root, from project(CNA)) and NOT on CMAKE_SOURCE_DIR: when CNA is consumed
# via add_subdirectory (mobile-eggbert does exactly that), CMAKE_SOURCE_DIR is the *consumer's*
# root, and a consumer is perfectly entitled to its own src/ and include/ trees -- keying the
# gate on it made every such consumer's configure fail with a bogus "legacy tree reappeared".
foreach(_cna_legacy src include)
    if(EXISTS "${CNA_SOURCE_DIR}/${_cna_legacy}")
        message(FATAL_ERROR
            "CNA: legacy global '${_cna_legacy}/' tree reappeared at the repository root -- "
            "production files belong to modules/<name>/ (physical module layout).")
    endif()
endforeach()
