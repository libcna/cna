# =====================================================================================
# CNA physical module composition (MODULARIZATION_PLAN.md §11). Each subsystem owns
# modules/<name>/{CMakeLists.txt,include/,src/,tests/}; this file detects the shared
# third-party media dependencies, defines the shared build-flag surface, composes the
# modules, and keeps the source-partition/no-loss ownership gate.
# =====================================================================================

# --- FFmpeg (video decoding) — not available on Emscripten/Android, nor on any Windows target
# (mingw-w64 cross-build OR a native MSVC build, e.g. this project's own D3D11/D3D12 Windows CI --
# neither has an FFmpeg pkg-config path; on the MinGW cross-build, pkg-config would otherwise
# silently resolve to the host's native FFmpeg and poison the cross-compile's include path with
# native glibc headers). WIN32 covers both MinGW and native MSVC uniformly; MINGW is kept for
# clarity/documentation even though it's now redundant with WIN32. ---
if(MINGW OR WIN32 OR EMSCRIPTEN OR ANDROID)
    set(CNA_FFMPEG_AVAILABLE OFF)
else()
    set(CNA_FFMPEG_AVAILABLE ON)
endif()

if(CNA_FFMPEG_AVAILABLE)
    find_package(PkgConfig REQUIRED)
    # IMPORTED_TARGET (not raw _LIBRARIES/_INCLUDE_DIRS variables) so include dirs, -L search
    # dirs, and libraries all propagate correctly as INTERFACE properties of a real CMake target
    # -- the same pattern this project already uses for SDL3::SDL3/Vulkan::Vulkan. This
    # matters in practice: Homebrew's ffmpeg on macOS lives under a non-default prefix
    # (/opt/homebrew), unlike Linux distro packages under /usr/lib/<triplet>.
    pkg_check_modules(LIBAVCODEC  REQUIRED IMPORTED_TARGET libavcodec)
    pkg_check_modules(LIBAVFORMAT REQUIRED IMPORTED_TARGET libavformat)
    pkg_check_modules(LIBAVUTIL   REQUIRED IMPORTED_TARGET libavutil)
    pkg_check_modules(LIBSWRESAMPLE REQUIRED IMPORTED_TARGET libswresample)
endif()

# --- Draco (KHR_draco_mesh_compression mesh decoding, plan_cnj.md CNB-91, Phase 14F) — optional,
# genuinely a system dependency (unlike cgltf.h/stb_image.h, which are vendored single-header
# libs): Draco is a real multi-file C++ library, not something worth vendoring wholesale just to
# decode compressed meshes. Detected via CMake's own exported package config (Debian's
# libdraco-dev ships draco-config.cmake); when absent, GltfImportCore::ExtractMesh keeps its own
# existing "throws a clear unsupported-format error" behavior for a Draco-compressed primitive,
# exactly like FFmpeg's own CNA_FFMPEG_AVAILABLE=OFF fallback above.
find_package(draco CONFIG QUIET)
if(draco_FOUND)
    set(CNA_DRACO_AVAILABLE ON)
    message(STATUS "CNA: Draco found (${draco_VERSION}) -- KHR_draco_mesh_compression decoding enabled")
else()
    set(CNA_DRACO_AVAILABLE OFF)
    message(STATUS "CNA: Draco not found -- KHR_draco_mesh_compression primitives will throw at import time")
endif()

# The root-scope test registration (cmake/UnitTests.cmake) keys its FFmpeg-gated test
# exclusions on this value; export it up from this directory scope, or the root would see
# an unset variable and silently drop the five video test TUs on FFmpeg-capable hosts.
set(CNA_FFMPEG_AVAILABLE ${CNA_FFMPEG_AVAILABLE} PARENT_SCOPE)
set(CNA_DRACO_AVAILABLE ${CNA_DRACO_AVAILABLE} PARENT_SCOPE)

# Common public build surface shared by every CNA module and, transitively, by every
# consumer of any module: the public compile definitions the monolithic CNA target used
# to carry PUBLIC. Public include paths deliberately do NOT live here any more -- each
# module target exposes its own include/ root, and consumers reach the aggregate surface
# through target composition (the CNA umbrella, or the specific modules they link).
add_library(cna_build_flags INTERFACE)
add_library(CNA::BuildFlags ALIAS cna_build_flags)
target_compile_definitions(cna_build_flags INTERFACE
        SOUND_ENABLED
        XNA5
        ${CNA_BACKEND_DEFINE}
        $<$<BOOL:${CNA_NOXNA}>:CNA_NOXNA>
        $<$<BOOL:${CNA_DEVICES}>:CNA_DEVICES>
        $<$<BOOL:${CNA_DRACO_AVAILABLE}>:CNA_DRACO_AVAILABLE>
        $<$<BOOL:${CNA_FFMPEG_AVAILABLE}>:CNA_FFMPEG_AVAILABLE>
)

# Defines one CNA module: a STATIC library named cna_<name> with alias CNA::<Alias>, the
# shared public build surface, and the calling module's own public include/ root. No module
# needs another module's src/ as an include root: every cross-module internal header lives
# under some module's include/CNA/Internal/ (public-internal contract headers), and the only
# headers under src/ are backend-local shader headers, addressed includer-relative from
# inside their own renderer directory.
function(cna_add_module target alias)
    add_library(${target} STATIC ${ARGN})
    add_library(CNA::${alias} ALIAS ${target})
    target_link_libraries(${target} PUBLIC cna_build_flags)
    target_include_directories(${target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
endfunction()

# --- Module composition. Order is readability-only: target_link_libraries resolves
# not-yet-defined target names at generate time, so cycles and forward references are fine.
add_subdirectory(core)
add_subdirectory(math)
add_subdirectory(graphics)
add_subdirectory(input)
add_subdirectory(audio)
add_subdirectory(media)
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

# --- CNA::NoXna compatibility umbrella. The former cna_noxna STATIC library's implementation
# is now the graphics-ext module; the historical target name survives as an INTERFACE
# composition over the actual extension modules so existing consumers keep linking it
# unchanged (and now also receive the device extensions that used to hide inside
# cna_devices).
add_library(cna_noxna INTERFACE)
add_library(CNA::NoXna ALIAS cna_noxna)
target_link_libraries(cna_noxna INTERFACE cna_graphics_ext cna_devices_ext)

# --- Compatible umbrella. `CNA` keeps its historical meaning for every consumer: the whole
# framework plus the selected backend, with the aggregate public include surface (via the
# module targets) and the public defines.
add_library(CNA INTERFACE)
target_link_libraries(CNA INTERFACE
        cna_runtime
        cna_devices
        cna_devices_ext
        cna_graphics_ext
        cna_noxna
        cna_storage
        cna_content
        cna_media
        cna_audio
        cna_input
        cna_graphics_core
        cna_core
        cna_math
        cna_build_flags
        ${BACKEND_TARGET}
)

# --- Physical source-partition validator (no-loss ownership gate, MODULARIZATION_PLAN.md
# §5/§11): file physical module location == declared CMake ownership. Every production TU
# must live under a declared module's src/ tree (module CMakeLists glob exactly their own
# src/, so location IS ownership); a TU outside every module src/ tree, or a module
# directory outside the declared set, fails the configure. The one documented
# target-membership exception is GDI compiling eight software-module 2D TUs
# (modules/renderers/gdi/CMakeLists.txt) -- physical ownership stays with software.
set(_cna_framework_modules
    core math runtime graphics input audio media content storage
    devices devices-ext graphics-ext gamer-services net)
set(_cna_renderer_modules
    ascii bgfx canvas d3d10 d3d11 d3d12 d3d9 diligent direct2d dx1 dx2 dx3 dx5 dx6
    dx7 dx8 easygl freedirect gdi glide headless html-dom llgl magnum metal opengl1
    opengl2 opengl4 opengles1 sdl-gpu sdl-renderer skia software sokol stub vulkan
    webgpu wicked common/d3d)
file(GLOB_RECURSE _cna_all_module_tus RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    CONFIGURE_DEPENDS "*.cpp" "*.mm")
foreach(_cna_tu IN LISTS _cna_all_module_tus)
    set(_cna_tu_ok FALSE)
    foreach(_cna_m IN LISTS _cna_framework_modules)
        if(_cna_tu MATCHES "^${_cna_m}/(src|tests)/")
            set(_cna_tu_ok TRUE)
        endif()
    endforeach()
    foreach(_cna_m IN LISTS _cna_renderer_modules)
        if(_cna_tu MATCHES "^renderers/${_cna_m}/(src|tests)/")
            set(_cna_tu_ok TRUE)
        endif()
    endforeach()
    if(NOT _cna_tu_ok)
        message(FATAL_ERROR
            "CNA: translation unit outside every declared physical module src/tests tree: "
            "modules/${_cna_tu} -- add it to a module (modules/<name>/src) or extend the "
            "declared module set here deliberately.")
    endif()
endforeach()
unset(_cna_all_module_tus)

# The legacy global trees must not silently come back.
foreach(_cna_legacy src include)
    if(EXISTS "${CMAKE_SOURCE_DIR}/${_cna_legacy}")
        message(FATAL_ERROR
            "CNA: legacy global '${_cna_legacy}/' tree reappeared at the repository root -- "
            "production files belong to modules/<name>/ (physical module layout).")
    endif()
endforeach()
