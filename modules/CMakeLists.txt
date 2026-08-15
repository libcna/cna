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
        XNA5
        ${CNA_RENDERER_DEFINE}
        $<$<BOOL:${CNA_CNAEXT}>:CNA_CNAEXT>
        $<$<BOOL:${CNA_DEVICES}>:CNA_DEVICES>
        $<$<BOOL:${CNA_DRACO_AVAILABLE}>:CNA_DRACO_AVAILABLE>
        $<$<BOOL:${CNA_FFMPEG_AVAILABLE}>:CNA_FFMPEG_AVAILABLE>
)

# The current high-level mixer is SDL3_mixer based.  Do not advertise sound support in a
# configuration which deliberately selected a non-SDL3 audio transport: that would compile
# the XNA facade against an implementation which cannot exist in the resulting binary.
if(CNA_AUDIO_PLATFORM STREQUAL "SDL3")
    target_compile_definitions(cna_build_flags INTERFACE SOUND_ENABLED)
endif()

# Defines one CNA module: a STATIC library named cna_<name> with alias CNA::<Alias>, the
# shared public build surface, and the calling module's own public include/ root. No module
# needs another module's src/ as an include root: every cross-module internal header lives
# under some module's include/CNA/Internal/ (public-internal contract headers), and the only
# headers under src/ are renderer-local shader headers, addressed includer-relative from
# inside their own renderer directory.
function(cna_add_module target alias)
    add_library(${target} STATIC ${ARGN})
    add_library(CNA::${alias} ALIAS ${target})
    target_link_libraries(${target} PUBLIC cna_build_flags)
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
# The platform contract (plan_platform.md): composed before the subsystems that will consume
# it, so the read order matches the dependency direction even though CMake does not require it.
add_subdirectory(platform)
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
        cna_build_flags
        ${RENDERER_TARGET}
)

# --- Physical source-partition validator (no-loss ownership gate, MODULARIZATION_PLAN.md
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
    core math platform runtime graphics input audio media content storage
    devices devices-ext graphics-ext gamer-services net)
set(_cna_renderer_modules
    bgfx blend2d canvas diligent direct2d directx1 directx2 directx3 directx5 directx6
    directx7 directx8 directx9 directx10 directx11 directx12
    easygl fna3d freedirect gdi glide headless html-dom llgl magnum metal opengl1
    opengl2 opengl4 opengles1 openvg portablegl sdl-gpu sdl-renderer skia software sokol
    stub svg-dom tinygl vulkan
    webgpu wicked common/d3d common/mojoshader)
file(GLOB_RECURSE _cna_all_module_tus RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    CONFIGURE_DEPENDS "*.cpp" "*.mm")
foreach(_cna_tu IN LISTS _cna_all_module_tus)
    set(_cna_tu_ok FALSE)
    foreach(_cna_m IN LISTS _cna_framework_modules)
        if(_cna_tu MATCHES "^${_cna_m}/(src|tests|examples)/")
            set(_cna_tu_ok TRUE)
        endif()
    endforeach()
    foreach(_cna_m IN LISTS _cna_renderer_modules)
        if(_cna_tu MATCHES "^renderers/${_cna_m}/(src|tests|examples)/")
            set(_cna_tu_ok TRUE)
        endif()
    endforeach()
    if(NOT _cna_tu_ok)
        message(FATAL_ERROR
            "CNA: translation unit outside every declared physical module src/tests/examples "
            "tree: modules/${_cna_tu} -- add it to a module (modules/<name>/src) or extend "
            "the declared module set here deliberately.")
    endif()
endforeach()
unset(_cna_all_module_tus)

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
