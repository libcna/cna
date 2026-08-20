#!/usr/bin/env python3
"""One-shot generator for the Phase-3 per-module CMakeLists.txt tree."""
import os

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def W(path, text):
    full = os.path.join(REPO, path)
    os.makedirs(os.path.dirname(full), exist_ok=True)
    with open(full, "w") as f:
        f.write(text.lstrip("\n"))
    print(path)


# =====================================================================================
# modules/CMakeLists.txt — composition root
# =====================================================================================
W("modules/CMakeLists.txt", r'''
# =====================================================================================
# CNA physical module composition (plans/MODULARIZATION_PLAN.md §11). Each subsystem owns
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

# --- Draco (KHR_draco_mesh_compression mesh decoding, plans/plan_cnj.md CNB-91, Phase 14F) — optional,
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

# --- Physical source-partition validator (no-loss ownership gate, plans/MODULARIZATION_PLAN.md
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
''')

# =====================================================================================
# Framework modules
# =====================================================================================
W("modules/core/CMakeLists.txt", r'''
file(GLOB CNA_CORE_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
cna_add_module(cna_core Core ${CNA_CORE_SOURCES})
cna_link_sharp_runtime(cna_core PUBLIC Core.Base)
# Logger.cpp logs through SDL; the dependency is an implementation detail, not API surface.
target_link_libraries(cna_core PRIVATE SDL3::SDL3)
''')

W("modules/math/CMakeLists.txt", r'''
file(GLOB CNA_MATH_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
cna_add_module(cna_math Math ${CNA_MATH_SOURCES})
cna_link_sharp_runtime(cna_math PUBLIC Core.Base)
''')

W("modules/runtime/CMakeLists.txt", r'''
file(GLOB CNA_RUNTIME_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
cna_add_module(cna_runtime Runtime ${CNA_RUNTIME_SOURCES})
target_link_libraries(cna_runtime PUBLIC
        cna_graphics_core cna_input cna_content cna_audio cna_media cna_core cna_math)
cna_link_sharp_runtime(cna_runtime PUBLIC Core.Base IO)
target_link_libraries(cna_runtime PRIVATE SDL3::SDL3 SDL3_mixer::SDL3_mixer)
''')

W("modules/graphics/CMakeLists.txt", r'''
file(GLOB_RECURSE CNA_GRAPHICS_CORE_SOURCES CONFIGURE_DEPENDS
    "src/Xna/*.cpp"
    "src/Internal/*.cpp"
)
cna_add_module(cna_graphics_core GraphicsCore ${CNA_GRAPHICS_CORE_SOURCES})
target_link_libraries(cna_graphics_core PUBLIC cna_math cna_core)
cna_link_sharp_runtime(cna_graphics_core PUBLIC Core.Base IO Collections.Core Text)
# GraphicsDevice.cpp/GraphicsAdapter.cpp/Texture2D.cpp query SDL video state directly;
# ImageLoader.cpp decodes through SDL3_image.
target_link_libraries(cna_graphics_core PRIVATE SDL3::SDL3 SDL3_image::SDL3_image)
# Genuine XNA-semantic static-archive cycle (kept, not redesigned): GraphicsDevice updates
# TouchPanel display metrics and Mouse/TextInputEXT window binding, while Input::MouseCursor
# builds on Graphics::Texture2D. Declared on both targets so CMake repeats the archives.
target_link_libraries(cna_graphics_core PRIVATE cna_input)
# GraphicsDevice.cpp constructs the selected backend through
# CNA::Internal::Backends::CreateGraphicsBackend(), whose definition lives in the backend
# archive. The umbrella's INTERFACE membership is not a link-order constraint, so the forward
# edge must be declared on the graphics core itself; together with the reverse edges declared
# by the renderer modules this closes the graphics-core <-> backend cycle for the backends
# that have one, and a plain ordered dependency for the rest.
target_link_libraries(cna_graphics_core PRIVATE ${BACKEND_TARGET})
''')

W("modules/input/CMakeLists.txt", r'''
file(GLOB_RECURSE CNA_INPUT_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
cna_add_module(cna_input Input ${CNA_INPUT_SOURCES})
target_link_libraries(cna_input PUBLIC cna_graphics_core cna_math cna_core)
cna_link_sharp_runtime(cna_input PUBLIC Core.Base)
target_link_libraries(cna_input PRIVATE SDL3::SDL3)
''')

W("modules/audio/CMakeLists.txt", r'''
# FrameworkDispatcher.cpp lives in this module's src/Xna/: its implementation state is the
# audio stream list, and audio/media/runtime all call INTO it (ownership follows dependency
# direction; assigning it to runtime would create a runtime<->audio link cycle for nothing).
file(GLOB_RECURSE CNA_AUDIO_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
cna_add_module(cna_audio Audio ${CNA_AUDIO_SOURCES})
target_link_libraries(cna_audio PUBLIC cna_core cna_math)
cna_link_sharp_runtime(cna_audio PUBLIC Core.Base IO Runtime)
target_link_libraries(cna_audio PRIVATE SDL3::SDL3 SDL3_mixer::SDL3_mixer)
# Genuine XNA-semantic static-archive cycle (kept, not redesigned): FrameworkDispatcher.Update()
# pumps both the audio streams and MediaPlayer (MediaPlayer::Update/OnActiveSongChanged/
# OnMediaStateChanged), while MediaPlayer itself plays through the audio mixer. Declared on both
# targets (see the media module) so CMake repeats the archives.
target_link_libraries(cna_audio PRIVATE cna_media)
# FrameworkDispatcher::Update() also pumps TouchPanel (TouchDeviceExists check + gesture
# update), exactly as FNA's FrameworkDispatcher.Update() does. This audio -> input symbol
# edge previously resolved only through the transitive link closure (media -> graphics-core
# -> $<LINK_ONLY:cna_input>); declare it directly so the build graph states the real
# dependency instead of relying on another module's private edge. Plain DAG edge, no cycle:
# input has no path back to audio.
target_link_libraries(cna_audio PRIVATE cna_input)
''')

W("modules/media/CMakeLists.txt", r'''
file(GLOB_RECURSE CNA_MEDIA_SOURCES CONFIGURE_DEPENDS "src/*.cpp")

# Exclude FFmpeg-dependent sources on platforms where FFmpeg is unavailable.
if(NOT CNA_FFMPEG_AVAILABLE)
    list(FILTER CNA_MEDIA_SOURCES EXCLUDE REGEX ".*/src/Internal/VideoDecoder\\.cpp$")
    list(FILTER CNA_MEDIA_SOURCES EXCLUDE REGEX ".*/src/Xna/Video/VideoPlayer\\.cpp$")
    list(FILTER CNA_MEDIA_SOURCES EXCLUDE REGEX ".*/src/Xna/Video/Video\\.cpp$")
endif()

cna_add_module(cna_media Media ${CNA_MEDIA_SOURCES})
target_link_libraries(cna_media PUBLIC cna_audio cna_graphics_core)
cna_link_sharp_runtime(cna_media PUBLIC Core.Base IO)
target_link_libraries(cna_media PRIVATE SDL3::SDL3 SDL3_mixer::SDL3_mixer)

if(CNA_FFMPEG_AVAILABLE)
    # PkgConfig::<prefix> IMPORTED targets (see modules/CMakeLists.txt) carry their own
    # INTERFACE include dirs, link dirs, and libraries together, so this is the only
    # target_link_libraries() call needed -- no separate target_include_directories()/
    # target_link_directories() call, unlike raw _LIBRARIES/_INCLUDE_DIRS variables.
    target_link_libraries(cna_media PRIVATE
        PkgConfig::LIBAVCODEC
        PkgConfig::LIBAVFORMAT
        PkgConfig::LIBAVUTIL
        PkgConfig::LIBSWRESAMPLE
    )
endif()
''')

W("modules/content/CMakeLists.txt", r'''
file(GLOB_RECURSE CNA_CONTENT_SOURCES CONFIGURE_DEPENDS "src/*.cpp")

# REMED-BUILD-013 (discovered while verifying it): VideoContentTypeReader.cpp constructs a
# Video (Media::Video::Video(...)) -- a genuine, previously-latent gap in the FFmpeg
# exclusion list, never hit before because every FFmpeg-unavailable target (MinGW
# D3D9/D3D11/D3D12, Emscripten, Android) that links a full CNA never got far enough in a
# real build to reach it.
if(NOT CNA_FFMPEG_AVAILABLE)
    list(FILTER CNA_CONTENT_SOURCES EXCLUDE REGEX ".*/src/Xnb/VideoContentTypeReader\\.cpp$")
endif()

cna_add_module(cna_content Content ${CNA_CONTENT_SOURCES})
target_link_libraries(cna_content PUBLIC cna_graphics_core cna_audio cna_media cna_math cna_core)
cna_link_sharp_runtime(cna_content PUBLIC Core.Base IO)
target_link_libraries(cna_content PRIVATE SDL3::SDL3 SDL3_mixer::SDL3_mixer)
# plans/plan_cnj.md CNB-70 (Phase 13D): CNA::Internal::GltfImport::GltfImportCore (used by both
# ContentManager.cpp's GltfModelTypeReader and tools/gltf_to_cnj) needs cgltf.h.
# plans/plan_cnj.md CNB-88 (Phase 14E): RemapOcclusionImageForDualTextureEXT needs
# stb_image.h/stb_image_write.h.
target_include_directories(cna_content PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party/cgltf
        ${CMAKE_SOURCE_DIR}/third_party/stb
)
if(CNA_DRACO_AVAILABLE)
    target_link_libraries(cna_content PRIVATE draco::draco)
endif()
''')

W("modules/storage/CMakeLists.txt", r'''
file(GLOB CNA_STORAGE_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
cna_add_module(cna_storage Storage ${CNA_STORAGE_SOURCES})
# StorageDevice/StorageContainer's public API names Microsoft::Xna::Framework::PlayerIndex,
# whose header the core module owns. The monolithic global include root used to satisfy this
# silently; the physical module layout states the real include-contract edge instead.
target_link_libraries(cna_storage PUBLIC cna_core)
cna_link_sharp_runtime(cna_storage PUBLIC Core.Base IO Runtime Threading)
target_link_libraries(cna_storage PRIVATE SDL3::SDL3)
''')

W("modules/devices/CMakeLists.txt", r'''
# XNA-compatible device base: Microsoft::Devices sensors + VibrateController. The
# CNA-specific device extensions live in modules/devices-ext (dependency direction:
# devices-ext -> devices base surface, never the reverse).
file(GLOB_RECURSE CNA_DEVICES_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
cna_add_module(cna_devices Devices ${CNA_DEVICES_SOURCES})
target_link_libraries(cna_devices PUBLIC cna_runtime cna_graphics_core cna_core cna_math)
cna_link_sharp_runtime(cna_devices PUBLIC Core.Base)
target_link_libraries(cna_devices PRIVATE SDL3::SDL3)

if(ANDROID)
    # Detail::AndroidSensorBridge (Microsoft::Devices::Sensors) calls the NDK's
    # ASensorManager_*/ASensorEventQueue_*/ALooper_* API directly (no JNI) --
    # those symbols live in libandroid.so, not libc/libc++. PUBLIC so any
    # executable linking the devices module (e.g. cna_demo_devices) picks up the
    # transitive dependency automatically (plans/plan_devices.md Task DEVICES-0121).
    #
    # Task ANDR2-006 (2026-07-17): the same file's debug-only
    # __android_log_print() diagnostic (disable/destroy failure reporting)
    # needs liblog.so -- a separate NDK system library from libandroid.so,
    # not pulled in transitively by it.
    target_link_libraries(cna_devices PUBLIC android log)
endif()
''')

W("modules/devices-ext/CMakeLists.txt", r'''
# CNA-specific device extensions beyond the XNA 4.0 contract (Camera, Clipboard,
# FileDialog, MessageBox, SystemTray, DisplayInfo, PowerInfo, SystemInfo, Locale,
# UrlLauncher) -- the former NoXna half of the devices sources. Depends on the runtime
# layer (GameWindow/display metrics), graphics (Texture2D camera frames), core and math;
# deliberately NOT on the XNA device base and never the reverse direction.
file(GLOB_RECURSE CNA_DEVICES_EXT_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
cna_add_module(cna_devices_ext DevicesExt ${CNA_DEVICES_EXT_SOURCES})
target_link_libraries(cna_devices_ext PUBLIC cna_runtime cna_graphics_core cna_core cna_math)
cna_link_sharp_runtime(cna_devices_ext PUBLIC Core.Base)
target_link_libraries(cna_devices_ext PRIVATE SDL3::SDL3)
''')

W("modules/graphics-ext/CMakeLists.txt", r'''
# CNA graphics extensions beyond the XNA 4.0 contract (PbrMaterial, CRTEffect,
# DepthEffect, RenderPipelineSettings and their CNA/Graphics headers) -- the complete
# implementation of the former cna_noxna STATIC library. CNA::NoXna survives as an
# INTERFACE umbrella over the extension modules (modules/CMakeLists.txt).
file(GLOB_RECURSE CNA_GRAPHICS_EXT_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
cna_add_module(cna_graphics_ext GraphicsExt ${CNA_GRAPHICS_EXT_SOURCES})
target_link_libraries(cna_graphics_ext PUBLIC cna_graphics_core)
cna_link_sharp_runtime(cna_graphics_ext PUBLIC Core.Base)
''')

W("modules/gamer-services/CMakeLists.txt", r'''
file(GLOB_RECURSE CNA_GAMERSERVICES_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
add_library(CNA_GamerServices STATIC ${CNA_GAMERSERVICES_SOURCES})
add_library(CNA::GamerServices ALIAS CNA_GamerServices)
target_link_libraries(CNA_GamerServices PUBLIC cna_build_flags)
target_include_directories(CNA_GamerServices PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# Guide.cpp calls SDL3 directly (message box); this was previously only compiling by
# accident on hosts with a stray system-wide SDL3 install on the default include path.
# The public dependency is the runtime layer (Guide/SignedInGamer use Game, graphics,
# input, audio through it) plus Storage (device selector flows).
target_link_libraries(CNA_GamerServices PUBLIC cna_runtime cna_storage PRIVATE SDL3::SDL3)
cna_link_sharp_runtime(CNA_GamerServices PUBLIC Core.Base IO Collections.Core Globalization Runtime Threading)
''')

W("modules/net/CMakeLists.txt", r'''
file(GLOB_RECURSE CNA_NET_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
add_library(CNA_Net STATIC ${CNA_NET_SOURCES})
add_library(CNA::Net ALIAS CNA_Net)
target_link_libraries(CNA_Net PUBLIC cna_build_flags)
target_include_directories(CNA_Net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(CNA_Net
    PUBLIC
    CNA_GamerServices
    enet
)
cna_link_sharp_runtime(CNA_Net PUBLIC Core.Base IO Collections.Core Runtime Threading)
''')

# =====================================================================================
# Renderers
# =====================================================================================
W("modules/renderers/CMakeLists.txt", r'''
# =====================================================================================
# Renderer module composition. Every implementation family owns
# modules/renderers/<family>/{CMakeLists.txt,src/,include/,tests/}; exactly one family
# builds the selected ${BACKEND_TARGET} per configuration (BackendSelection.cmake resolves
# the public identity to BACKEND_DIR = modules/renderers/<family>). The 41 public renderer
# identities live in BackendSelection.cmake + GraphicsBackendType.hpp and are pinned by
# scripts/check_renderer_identities.py; families and helper targets here are
# implementation structure, not identities.
# =====================================================================================

# Common backend interface: the shared public compile definitions. Public include roots
# ride the owning module targets now -- backends reach the graphics/math/core headers
# through their declared reverse edges below, not through a global include directory.
add_library(cna_backend_graphics_common INTERFACE)
target_link_libraries(cna_backend_graphics_common INTERFACE cna_build_flags)

# Applies the common renderer-target surface to an existing ${BACKEND_TARGET}: the common
# interface, the sharp-runtime closure, the module's own public include root, and the
# reverse edges into the framework modules.
function(cna_renderer_backend_common_setup)
    target_link_libraries(${BACKEND_TARGET} PUBLIC cna_backend_graphics_common)
    cna_link_sharp_runtime(${BACKEND_TARGET} PUBLIC)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include")
        target_include_directories(${BACKEND_TARGET} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
    endif()
    # D3D11's/D3D12's SpriteBatch backend (plans/plan_dx.md DX-70/DX-71) calls back into
    # Microsoft::Xna::Framework::Graphics::Effect::Apply() for SpriteBatch::Begin(effect)'s
    # custom-Effect path -- a genuine, honest circular dependency between the backend static
    # library and the CNA graphics core (the backend needs a CNA-defined symbol, while CNA needs
    # the backend to implement IGraphicsBackend). Under single-pass archive resolution any
    # executable linking CNA hit a real "undefined reference to Effect::Apply()" at link time.
    # CMake's documented static-library-cycle support resolves this by repeating the archives on
    # the final link line once this cycle is declared.
    #
    # The header-inlined IGraphicsBackend defaults (HandleUnsupported3DCall -> CNA::Logger::Warn,
    # the default instanced-draw guard, ...) give EVERY backend the same structural reverse edge,
    # and the module split surfaces it deterministically (first hit for real: VULKAN's
    # cna_reference_dump link). Declared unconditionally: the reverse edges land on the specific
    # modules that define the symbols (Effect/VertexDeclaration/DxtUtil -> cna_graphics_core,
    # Logger -> cna_core, named math/colour values -> cna_math). These PRIVATE edges also carry
    # the framework include roots the backend TUs compile against.
    target_link_libraries(${BACKEND_TARGET} PRIVATE cna_graphics_core cna_core cna_math)
endfunction()

# Defines the selected ${BACKEND_TARGET} from the calling family's src/ directory
# (non-recursive, exactly as the former central glob: shader headers live in src/shaders/
# and are included includer-relative). Extra arguments append additional sources
# (Objective-C++ units, shared cross-family units).
function(cna_add_renderer_backend)
    file(GLOB _cna_backend_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
    list(APPEND _cna_backend_sources ${ARGN})
    add_library(${BACKEND_TARGET} STATIC ${_cna_backend_sources})
    cna_renderer_backend_common_setup()
endfunction()

get_filename_component(CNA_SELECTED_RENDERER "${BACKEND_DIR}" NAME)

# metal and glide keep unconditional header-interface targets: their policy suites in
# modules/renderers/{metal,glide}/tests are deliberately host-portable and compile into the
# CnaTests corpus on every backend (see the suites' own header comments), so their
# CNA/Internal/Backends/<X> policy headers must stay reachable in every configuration.
add_subdirectory(metal)
add_subdirectory(glide)

# plans/plan_dx.md design decision 4: D3DCommon shared core, consumed by the d3d11 and d3d12
# families only -- D3D9 and D3D10 are confirmed independent (design decisions 12 and the
# plans/plan_d3d10.md link audit).
if(CNA_GRAPHICS_BACKEND STREQUAL "D3D11" OR CNA_GRAPHICS_BACKEND STREQUAL "D3D12")
    add_subdirectory(common/d3d)
endif()

# plans/plan_ascii.md design decision 2: ASCII is a thin decorator around SDL_RENDERER's own
# SdlGraphicsBackend (composition, not reimplementation) -- the sdl-renderer module builds
# its sources as a shared core library for the ASCII configuration.
if(CNA_GRAPHICS_BACKEND STREQUAL "ASCII")
    add_subdirectory(sdl-renderer)
endif()

# GDI presents the Software backend's CPU framebuffer through classic Win32 GDI: the
# software module publishes its shared 2D translation-unit list (and its header interface)
# for the GDI configuration. Ordering matters: software must be entered before gdi so the
# published list is visible there.
if(CNA_GRAPHICS_BACKEND STREQUAL "GDI")
    add_subdirectory(software)
endif()

if(NOT CNA_SELECTED_RENDERER STREQUAL "metal" AND NOT CNA_SELECTED_RENDERER STREQUAL "glide")
    add_subdirectory(${CNA_SELECTED_RENDERER})
endif()
''')

W("modules/renderers/common/d3d/CMakeLists.txt", r'''
# plans/plan_dx.md design decision 4: D3DCommon shared core, consumed by cna_backend_graphics_d3d11
# and cna_backend_graphics_d3d12. Only built when actually needed -- the renderers
# composition adds this directory for the D3D11/D3D12 configurations only.
# plans/plan_dx9.md design decision 12: D3D9 deliberately does NOT join this condition. D3DFORMAT is a
# different enum space from DXGI_FORMAT and D3D9 has no state objects at all -- D3D9 gets its own
# D3D9FormatMapping/D3D9StateMapping/D3D9VertexDeclarations instead of expanding this shared core.
# D3D10 is likewise independent (its own format/state mapping, no D3DCommon include or link
# edge -- verified mechanically during the physical-module move).
file(GLOB D3DCOMMON_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
add_library(cna_backend_graphics_d3dcommon STATIC ${D3DCOMMON_SOURCES})
target_link_libraries(cna_backend_graphics_d3dcommon PUBLIC cna_backend_graphics_common d3d11 dxgi)
cna_link_sharp_runtime(cna_backend_graphics_d3dcommon PUBLIC)
target_include_directories(cna_backend_graphics_d3dcommon PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
# Same reverse edges as every backend target (cna_renderer_backend_common_setup): the shared
# core compiles against the graphics/math/core public headers and calls CNA-owned symbols.
target_link_libraries(cna_backend_graphics_d3dcommon PRIVATE cna_graphics_core cna_core cna_math)
''')

SIMPLE = {
    "headless": "# plans/plan_headless.md: no GPU, no window, no native graphics SDK -- the backend ends at\n# the common interface and the declared reverse edges.\ncna_add_renderer_backend()\n",
    "stub": "# plans/plan_stub.md: deliberately minimal no-op backend -- renders nothing, touches no SDL\n# window/video subsystem/GPU library.\ncna_add_renderer_backend()\n",
    "canvas": "# plans/plan_canvas.md design decision 2: reuses the existing SDL-created window/canvas element,\n# so still links SDL3 even though actual rendering goes through EM_JS/Canvas2D, not SDL_Renderer.\ncna_add_renderer_backend()\ntarget_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3)\n",
    "html-dom": "# plans/plan_html_dom.md design decision 2: reuses the SDL-created window/canvas element for sizing,\n# input and events, so SDL3 is still linked even though every draw goes through EM_JS/DOM/CSS\n# and no SDL_Renderer is ever created.\ncna_add_renderer_backend()\ntarget_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3)\n",
    "vulkan": "cna_add_renderer_backend()\ntarget_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 Vulkan::Vulkan)\n",
    "direct2d": "# Direct2D 1.1 presents through a BGRA-capable D3D11/DXGI flip-model swap chain. d2d1 is\n# the renderer; d3d11/dxgi provide only the device and presentation surface it targets.\ncna_add_renderer_backend()\ntarget_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 d2d1 d3d11 dxgi)\n",
    "d3d10": "# plans/plan_d3d10.md design decision: unlike DX8, mingw-w64 ships a REAL d3d10 import library\n# (libd3d10.a) -- no DXVK .dll.a linking hack needed. DXVK itself ships no d3d10.dll at all\n# (only d3d10core.dll); the real d3d10.dll/d3d10_1.dll come from Wine's own builtin, which\n# forward to d3d10core (overridden to DXVK's real implementation at the Wine-prefix level, not\n# a link-time concern).\ncna_add_renderer_backend()\ntarget_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 d3d10 dxgi d3dcompiler)\n",
    "d3d11": "# plans/plan_dx.md design decision 3 (DX-1): d3d11/dxgi is the confirmed minimal link set for the\n# stock offline-compiled shader pipeline. d3dcompiler is linked too, specifically for DX-58's\n# runtime D3DCompile() custom-ShaderEffect path (D3D11EffectBackend) -- confirmed safe to link\n# in isolation by DX-1/DX-14-compile's own spikes; the offline stock pipeline never calls it.\ncna_add_renderer_backend()\ntarget_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 d3d11 dxgi d3dcompiler cna_backend_graphics_d3dcommon)\n",
    "d3d12": "# plans/plan_dx.md DX-100/DX-101: d3d12+dxgi alone was confirmed sufficient for device/queue/\n# command-list creation by DX-100's own spike -- start from that minimum, same discipline as\n# DX-12/design decision 3. dxguid is deliberately NOT linked (still unneeded). d3dcompiler was\n# added by DX-121 (D3D12EffectBackend's runtime D3DCompile() path) -- confirmed safe to link in\n# isolation by D3D11's own DX-14-compile/DX-58 precedent.\ncna_add_renderer_backend()\ntarget_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 d3d12 dxgi d3dcompiler cna_backend_graphics_d3dcommon)\n",
    "freedirect": "# plans/plan_freedirect.md design decision 10: free-direct's own public target is the literal lowercase\n# `free-direct` (PUBLIC-links free-api::free-api, PUBLIC-exposes its own include/ dir so\n# #include <ddraw.h> resolves) -- no ALIAS namespace exists for it, unlike easy-gl's own target.\ncna_add_renderer_backend()\ntarget_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 free-direct)\n",
    "dx1": "# plans/plan_dx1.md design decision 10: ddraw + dxguid (GUID storage for IID_IDirectDraw etc.) is the\n# confirmed minimal link set (DX1-0 spike) -- no free-direct, no DXVK, no d3dcompiler, no\n# d3d11/dxgi. SDL3 is linked only for window/HWND access (design decision 3), same as every\n# other backend.\ncna_add_renderer_backend()\ntarget_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 ddraw dxguid)\n",
    "dx2": "# plans/plan_dx2.md design decision 10: same confirmed minimal link set as DX1 -- ddraw + dxguid +\n# SDL3::SDL3. No separate Direct3D import library is needed: IDirect3D2/IDirect3DDevice2 are\n# obtained purely via QueryInterface/CreateDevice on DirectDraw objects/surfaces (confirmed\n# during the DX2-0 spike, see plans/plan_dx2.md section 1).\ncna_add_renderer_backend()\ntarget_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 ddraw dxguid)\n",
    "dx3": "# plans/plan_dx3.md design decision 6: same confirmed minimal link set as DX1/DX2 -- ddraw + dxguid +\n# SDL3::SDL3. No separate Direct3D import library is needed here either (DX30-0 spike confirmed\n# IDirect3D2/IDirect3DDevice2 are still obtained purely via QueryInterface/CreateDevice, now off\n# an IDirectDraw2 object instead of v1 -- see plans/plan_dx3.md section 1).\ncna_add_renderer_backend()\ntarget_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 ddraw dxguid)\n",
    "dx5": "# plans/plan_dx5.md design decision 9: same confirmed minimal link set as DX1/DX2/DX3 -- ddraw +\n# dxguid + SDL3::SDL3. No separate Direct3D import library is needed here either (DX5-0 spike\n# confirmed IDirect3D3/IDirect3DDevice3 are still obtained purely via QueryInterface/\n# CreateDevice, now off an IDirectDraw4 object instead of v2 -- see plans/plan_dx5.md section 1).\ncna_add_renderer_backend()\ntarget_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 ddraw dxguid)\n",
    "dx6": "# plans/plan_dx6.md design decision 10: same confirmed minimal link set as DX1/DX2/DX3/DX5 -- ddraw +\n# dxguid + SDL3::SDL3. DX6 introduces no new interface at all, so no new link dependency either.\ncna_add_renderer_backend()\ntarget_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 ddraw dxguid)\n",
    "dx7": "# plans/plan_dx7.md design decision 14: same confirmed minimal link set as DX1/DX2/DX3/DX5/DX6 --\n# ddraw + dxguid + SDL3::SDL3. DirectDrawCreateEx/IDirectDraw7/IDirect3D7 all resolve from the\n# same import libraries, spike-confirmed (DX7-0) with no new link dependency.\ncna_add_renderer_backend()\ntarget_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 ddraw dxguid)\n",
    "magnum": "# plans/plan_magnum.md MAGNUM-2: Magnum::GL is the whole rendering surface this backend uses, and\n# Magnum::<platform>Context supplies the OpenGL function loader Platform::GLContext needs for a\n# context CNA created itself through SDL3 (which stays the window/context owner, exactly as in\n# every other windowed backend here). CNA_MAGNUM_CONTEXT_COMPONENT is resolved per platform in\n# cmake/ThirdPartyMagnum.cmake.\ncna_add_renderer_backend()\ntarget_link_libraries(${BACKEND_TARGET} PRIVATE\n    SDL3::SDL3 Magnum::GL Magnum::${CNA_MAGNUM_CONTEXT_COMPONENT} Magnum::Magnum)\n",
    "opengl1": "cna_add_renderer_backend()\nfind_package(OpenGL REQUIRED)\ntarget_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 OpenGL::GL)\n",
    "opengl2": "cna_add_renderer_backend()\nfind_package(OpenGL REQUIRED)\ntarget_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 OpenGL::GL)\n",
    "opengl4": "# plans/plan_opengl4.md GL4-1: real desktop OpenGL 4.x core profile via the platform's own GL\n# library (libGL/opengl32/OpenGL.framework, resolved by find_package(OpenGL) in\n# BackendSelection.cmake) plus SDL3 for window/context management -- no other new\n# third-party dependency (GL4Loader.hpp/.cpp is this backend's own hand-rolled loader for\n# the GL 1.2+ entry points a core profile needs).\ncna_add_renderer_backend()\ntarget_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 OpenGL::GL)\n",
    "easygl": "# The EasyGL family implements the four public GL-profile identities\n# (OPENGLES/OPENGL33/WEBGL1/WEBGL2) on top of the sibling easy-gl -> meta-gl chain\n# (plans/plan_glbackends.md); the profile define CNA_GL_PROFILE_* is applied by\n# BackendSelection.cmake at identity-selection time.\ncna_add_renderer_backend()\ntarget_link_libraries(${BACKEND_TARGET} PRIVATE easy-gl SDL3::SDL3)\n",
    "diligent": "# plans/plan_diligent.md DILIGENT-2: SDL3 provides the window (and the native handle Diligent's swap\n# chain is created from); every graphics call goes through the Diligent engine targets that\n# cna_configure_diligent() actually built for this platform.\ncna_add_renderer_backend()\ntarget_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3)\ncna_link_diligent(${BACKEND_TARGET})\n",
    "sokol": "# plans/plan_sokol.md SOKOL-2/SOKOL-3: cna_sokol_headers (cmake/ThirdPartySokol.cmake) carries the\n# fetched sokol include directory, the CNA_SOKOL_API define sokol_gfx.h dispatches on, and --\n# for the GL APIs -- OpenGL::GL, since sokol_gfx.h has no GL loader outside Windows. SDL3\n# supplies the window and, via SDL_GL_CreateContext, the GL context sokol_gfx renders into\n# (design decision 1: CNA keeps owning the window and the game loop, so sokol_app is not used).\ncna_add_renderer_backend()\ntarget_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 cna_sokol_headers)\n",
}
for fam, body in SIMPLE.items():
    W(f"modules/renderers/{fam}/CMakeLists.txt", body)

W("modules/renderers/sdl-renderer/CMakeLists.txt", r'''
if(CNA_GRAPHICS_BACKEND STREQUAL "SDL_RENDERER")
    cna_add_renderer_backend()
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "ASCII")
    # plans/plan_ascii.md design decision 2: ASCII is a thin decorator around SDL_RENDERER's own
    # SdlGraphicsBackend (composition, not reimplementation) -- it needs SdlGraphicsBackend.cpp's
    # real implementation compiled in even though CNA_GRAPHICS_BACKEND=SDL_RENDERER was not itself
    # selected. Mirrors the D3DCommon shared-core pattern: a small static lib, built only when
    # actually needed. CNA_BACKEND_SDL_RENDERER is deliberately NOT defined for this build, so
    # SdlGraphicsBackend.cpp's own #ifdef CNA_BACKEND_SDL_RENDERER-guarded CreateGraphicsBackend()
    # is compiled out here, leaving Ascii's own CreateGraphicsBackend() (guarded by
    # CNA_BACKEND_ASCII) as the only factory definition -- no duplicate-symbol clash.
    file(GLOB CNA_ASCII_SDLRENDERER_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
    add_library(cna_backend_graphics_sdl_renderer_core STATIC ${CNA_ASCII_SDLRENDERER_SOURCES})
    target_link_libraries(cna_backend_graphics_sdl_renderer_core PUBLIC cna_backend_graphics_common)
    cna_link_sharp_runtime(cna_backend_graphics_sdl_renderer_core PUBLIC)
    target_include_directories(cna_backend_graphics_sdl_renderer_core PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_link_libraries(cna_backend_graphics_sdl_renderer_core PRIVATE SDL3::SDL3)
    # Same reverse edges as every backend target: the shared core calls CNA-owned symbols and
    # compiles against the graphics/math/core public headers.
    target_link_libraries(cna_backend_graphics_sdl_renderer_core PRIVATE cna_graphics_core cna_core cna_math)
endif()
''')

W("modules/renderers/ascii/CMakeLists.txt", r'''
cna_add_renderer_backend()
target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 cna_backend_graphics_sdl_renderer_core)
''')

W("modules/renderers/software/CMakeLists.txt", r'''
if(CNA_GRAPHICS_BACKEND STREQUAL "SOFTWARE")
    # This wrapper includes SoftwareGraphicsBackend.cpp with CNA_SOFTWARE_2D_ONLY for the GDI
    # archive. The complete SOFTWARE archive compiles the implementation directly, once.
    file(GLOB _cna_software_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
    list(REMOVE_ITEM _cna_software_sources
        "${CMAKE_CURRENT_SOURCE_DIR}/src/SoftwareGraphicsBackend2D.cpp")
    add_library(${BACKEND_TARGET} STATIC ${_cna_software_sources})
    cna_renderer_backend_common_setup()
elseif(CNA_GRAPHICS_BACKEND STREQUAL "GDI")
    # GDI consumes the CPU 2D implementation through a dedicated translation unit. It deliberately
    # compiles neither the Software 3D/cube resource implementations nor the 3D draw entry points;
    # the full SOFTWARE backend compiles the remaining general implementation plus the shared 2D
    # units. Keep this dependency explicit: future Software translation units must not enter the
    # 2D-only GDI build without review. Physical ownership of these files stays with the software
    # module; compiling them into the GDI archive is the one documented target-membership
    # exception (see modules/CMakeLists.txt's validator note).
    set(CNA_GDI_SOFTWARE_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/SoftwareFramebufferAllocation.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/SoftwareTextureAllocation.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/SoftwareFramebuffer.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/SoftwareTexture2D.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/SoftwareRenderTarget2D.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/SoftwareGraphicsBackend2DState.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/SoftwareSpriteBatch.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/SoftwareGraphicsBackend2D.cpp"
    )
    set(CNA_GDI_SOFTWARE_SOURCES ${CNA_GDI_SOFTWARE_SOURCES} PARENT_SCOPE)
    # The GDI translation units include the software module's public-internal headers
    # (CNA/Internal/Backends/Software/...) -- publish the include root as an interface target.
    add_library(cna_renderer_software_headers INTERFACE)
    target_include_directories(cna_renderer_software_headers INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/include")
endif()
''')

W("modules/renderers/gdi/CMakeLists.txt", r'''
# GDI presents the Software backend's CPU framebuffer through classic Win32 GDI; the shared
# CPU-2D translation units come from the software module (CNA_GDI_SOFTWARE_SOURCES, published
# by modules/renderers/software/CMakeLists.txt in the GDI configuration).
cna_add_renderer_backend(${CNA_GDI_SOFTWARE_SOURCES})
target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 gdi32 cna_renderer_software_headers)

# GDI-078: report the actual GDI archive boundary at configure time, so plans/plan_gdi.md/NEXT_gdi.md
# source/object counts can be checked against generated evidence instead of a copied number.
list(LENGTH CNA_GDI_SOFTWARE_SOURCES CNA_GDI_SHARED_SOURCE_COUNT)
math(EXPR CNA_GDI_ARCHIVE_TOTAL "${CNA_GDI_SHARED_SOURCE_COUNT} + 3")
message(STATUS
    "CNA: GDI backend archive = ${CNA_GDI_SHARED_SOURCE_COUNT} shared CPU-2D sources + 3 "
    "GDI-owned units = ${CNA_GDI_ARCHIVE_TOTAL} translation units")
''')

W("modules/renderers/d3d9/CMakeLists.txt", r'''
# plans/plan_dx9.md Phase D9-11 (D9-110/D9-111), design decision 16: D3D9EffectBackend.cpp calls
# D3DCompile() (the custom-ShaderEffect runtime compile path), which needs d3dcompiler -- but the
# stock D3D9 pipeline must stay d3dcompiler-free (unlike D3D11/D3D12, which link it into their
# whole backend target unconditionally, DX-58/DX-121). Excluded from the main glob and built as its
# own isolated static library instead, with d3dcompiler linked ONLY there (D9-111's own "on this
# target only"). D3D9ConstantTable.cpp (D9-110's CTAB parser) moves here too -- it has no consumer
# outside D3D9EffectBackend.cpp, and leaving it in the main glob while D3D9EffectBackend.cpp moved
# out created a genuine link-order circular dependency between the two targets (this effect target
# calling ParseConstantTableEXT() while ALSO needing to be linked INTO the main backend target for
# D3D9GraphicsBackend::CreateEffectBackend() to construct one) -- found empirically via a real
# "undefined reference to ParseConstantTableEXT" link failure across every D3D9 test binary, not
# assumed in advance. ${BACKEND_TARGET} links this target (D9-112) so
# D3D9GraphicsBackend::CreateEffectBackend() can construct one.
set(D3D9_EFFECT_BACKEND_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/D3D9EffectBackend.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/D3D9ConstantTable.cpp"
)

file(GLOB _cna_d3d9_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/D3D9EffectBackend.cpp")
    list(REMOVE_ITEM _cna_d3d9_sources ${D3D9_EFFECT_BACKEND_SOURCES})
    add_library(cna_backend_graphics_d3d9_effect STATIC ${D3D9_EFFECT_BACKEND_SOURCES})
    target_link_libraries(cna_backend_graphics_d3d9_effect PUBLIC cna_backend_graphics_common)
    cna_link_sharp_runtime(cna_backend_graphics_d3d9_effect PUBLIC)
    target_include_directories(cna_backend_graphics_d3d9_effect PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_link_libraries(cna_backend_graphics_d3d9_effect PRIVATE d3d9 d3dcompiler)
    # Same reverse edges as every backend target: the effect backend compiles against the
    # graphics/math/core public headers and calls CNA-owned symbols.
    target_link_libraries(cna_backend_graphics_d3d9_effect PRIVATE cna_graphics_core cna_core cna_math)
endif()

add_library(${BACKEND_TARGET} STATIC ${_cna_d3d9_sources})
cna_renderer_backend_common_setup()
# plans/plan_dx9.md design decision 16 / D9-2: d3d9 alone was confirmed sufficient (no dxguid, no
# dxgi -- D3D9 predates DXGI) by D9-2's own spike. No cna_backend_graphics_d3dcommon (design
# decision 12 -- D3D9 does not share D3DCommon). d3dcompiler is deliberately NOT linked here --
# design decision 9/16 keep the stock pipeline dependency-free; it lives on the isolated effect
# target only.
target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 d3d9)
# D9-112: D3D9GraphicsBackend::CreateEffectBackend() needs to construct a real D3D9EffectBackend,
# so ${BACKEND_TARGET} links the isolated, d3dcompiler-carrying effect target.
if(TARGET cna_backend_graphics_d3d9_effect)
    target_link_libraries(${BACKEND_TARGET} PRIVATE cna_backend_graphics_d3d9_effect)
endif()
''')

W("modules/renderers/dx8/CMakeLists.txt", r'''
# plans/plan_dx8.md design decision 2/14: mingw-w64's x86_64 target ships NO real d3d8 import
# library at all (only the unrelated libd3d8thk.a "thunk" library; only the i686/32-bit target
# has a real one) -- DXVK's own d3d8.dll.a (D8VK, merged into DXVK 2.0+) exports the real
# Direct3DCreate8 symbol and is linked against directly instead, spike-confirmed (DX8-0a).
cna_add_renderer_backend()
set(CNA_DX8_DXVK_LIB "/usr/lib/dxvk/wine64/d3d8.dll.a" CACHE FILEPATH
    "Path to DXVK's own d3d8 import library (exports Direct3DCreate8) -- no MinGW x86_64 import library exists for real d3d8.")
if(NOT EXISTS "${CNA_DX8_DXVK_LIB}")
    message(FATAL_ERROR
        "CNA: DX8 backend requires DXVK's own d3d8 import library, not found at "
        "${CNA_DX8_DXVK_LIB} -- install the dxvk package (providing /usr/lib/dxvk/wine64/"
        "d3d8.dll.a) or set -DCNA_DX8_DXVK_LIB=<path> to point at it explicitly.")
endif()
target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 "${CNA_DX8_DXVK_LIB}")
''')

W("modules/renderers/bgfx/CMakeLists.txt", r'''
cna_add_renderer_backend()
target_link_libraries(${BACKEND_TARGET} PUBLIC bgfx bx)
target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3)
target_include_directories(${BACKEND_TARGET} PRIVATE ${CNA_BGFX_SHADER_INCLUDE_DIR})
''')

W("modules/renderers/wicked/CMakeLists.txt", r'''
# plans/plan_wicked.md WICKED-2: Wicked Engine vendors its own Vulkan headers and loads the loader
# through volk at runtime, so no find_package(Vulkan) is needed here (unlike the VULKAN backend).
# WickedEngine is PUBLIC because WickedGraphicsBackend.hpp includes wiGraphicsDevice.h -- every
# target compiling against this backend's header needs the same include directory and the same
# SDL3/WI_UNORDERED_MAP_TYPE compile definitions.
cna_add_renderer_backend()
target_link_libraries(${BACKEND_TARGET} PUBLIC WickedEngine)
target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3)
if(CNA_WICKED_DXCOMPILER)
    add_custom_command(TARGET ${BACKEND_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CNA_WICKED_DXCOMPILER}" "$<TARGET_FILE_DIR:${BACKEND_TARGET}>"
        VERBATIM)
endif()
''')

W("modules/renderers/webgpu/CMakeLists.txt", r'''
cna_add_renderer_backend()
target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 WebGPU::WebGPU)
if(CNA_WEBGPU_RUNTIME_LIBRARY)
    add_custom_command(TARGET ${BACKEND_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CNA_WEBGPU_RUNTIME_LIBRARY}" "$<TARGET_FILE_DIR:${BACKEND_TARGET}>"
        VERBATIM)
endif()
''')

W("modules/renderers/skia/CMakeLists.txt", r'''
# SKIA-160: CNA_SKIA_LINK_TARGET is CNA::Skia (raster, default) or CNA::SkiaGanesh (GANESH
# mode), set by BackendSelection.cmake's CNA_SKIA_MODE branch -- never both; the two are
# mutually exclusive GN builds of the same checkout and cannot be linked into one binary.
cna_add_renderer_backend()
target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 ${CNA_SKIA_LINK_TARGET})
# SKIA-140/141: SkiaTextureBackend.cpp calls CNA::Internal::Graphics::DxtUtil/Bc7Util, which
# live in the CNA graphics core module, not this backend target -- the reverse edge every
# backend now declares (cna_renderer_backend_common_setup) covers exactly this dependency
# (found originally via a real "undefined reference to Bc7Util::DecompressBc7" failure on
# cna_reference_dump).
# The pinned upstream raster archives are built with `skia_enable_ganesh=false` and RTTI
# disabled. GCC/Clang's broad `undefined` group otherwise instruments virtual calls made by
# this adapter with `vptr` checks and then requires typeinfo symbols (for example SkCanvas)
# which those archives deliberately do not export. Keep every other UBSan check enabled and
# exclude only the incompatible RTTI-dependent check at the third-party boundary.
if(CNA_SANITIZE MATCHES "(^|,)undefined(,|$)"
   AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${BACKEND_TARGET} PRIVATE -fno-sanitize=vptr)
endif()
''')

W("modules/renderers/sdl-gpu/CMakeLists.txt", r'''
# plans/plan_sdlgpu.md SDLGPU-1: SDL_gpu.h is part of SDL3 itself (SDL_gpu.c is already compiled
# into the same SDL3 library every other backend links against) -- no separate find_package
# or FetchContent is needed, unlike VULKAN/WEBGPU/BGFX.
cna_add_renderer_backend()
target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3)

# plans/plan_sdlgpu.md SDLGPU-42/43: SdlGpuEffectBackend needs a REAL runtime GLSL->SPIR-V compile
# for arbitrary user ShaderEffect source (SDL_gpu only accepts precompiled bytecode) -- unlike
# D3D11/D3D12's d3dcompiler, this environment has no libshaderc-dev package (no unversioned
# .so symlink, no headers), only the runtime libshaderc1 package's versioned .so.1. Prefer a
# normal find_library() first (works if a dev package IS present, e.g. other machines/CI), and
# fall back to globbing standard library directories for the exact versioned file otherwise --
# mirrors compile_shaders.py's own ctypes loader fallback list.
find_library(CNA_SHADERC_LIBRARY NAMES shaderc_shared shaderc)
if(NOT CNA_SHADERC_LIBRARY)
    file(GLOB CNA_SHADERC_CANDIDATES
        /usr/lib/*/libshaderc.so*
        /usr/lib/libshaderc.so*
        /usr/local/lib/libshaderc.so*
    )
    if(CNA_SHADERC_CANDIDATES)
        list(GET CNA_SHADERC_CANDIDATES 0 CNA_SHADERC_LIBRARY)
    endif()
endif()
if(NOT CNA_SHADERC_LIBRARY)
    message(FATAL_ERROR "CNA: libshaderc not found (required for SDL_GPU's runtime ShaderEffect GLSL compile, SDLGPU-42/43) -- install libshaderc1 or libshaderc-dev")
endif()
message(STATUS "CNA: using libshaderc at ${CNA_SHADERC_LIBRARY}")
target_link_libraries(${BACKEND_TARGET} PRIVATE "${CNA_SHADERC_LIBRARY}")
''')

W("modules/renderers/opengles1/CMakeLists.txt", r'''
# plans/plan_opengles1.md design decision 1: requires a REAL system OpenGL ES 1.1 (fixed-function
# "Common", CM) library and Khronos headers -- e.g. Debian/Ubuntu's libgles1 + libgles-dev
# (GLESv1_CM.so + GLES/gl.h + GLES/glext.h), or the equivalent on an embedded/Android SDK.
# Same "hard system dependency, not vendored, FATAL_ERROR with install instructions if
# missing" shape as VULKAN's find_package(Vulkan REQUIRED), not a soft find_library
# fallback -- this backend cannot function at all without a genuine ES1 CM implementation.
cna_add_renderer_backend()
find_library(CNA_GLESV1_CM_LIBRARY NAMES GLESv1_CM)
find_path(CNA_GLES_INCLUDE_DIR NAMES GLES/gl.h)
if(NOT CNA_GLESV1_CM_LIBRARY OR NOT CNA_GLES_INCLUDE_DIR)
    message(FATAL_ERROR
        "CNA: OpenGL ES 1.1 (GLESv1_CM) library/headers not found -- install libgles1 "
        "libgles-dev (Debian/Ubuntu), providing libGLESv1_CM.so plus GLES/gl.h and "
        "GLES/glext.h, or the equivalent on other distros/embedded SDKs.")
endif()
message(STATUS "CNA: using GLESv1_CM at ${CNA_GLESV1_CM_LIBRARY} (headers: ${CNA_GLES_INCLUDE_DIR})")
target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 "${CNA_GLESV1_CM_LIBRARY}")
target_include_directories(${BACKEND_TARGET} PRIVATE "${CNA_GLES_INCLUDE_DIR}")
''')

W("modules/renderers/llgl/CMakeLists.txt", r'''
cna_add_renderer_backend()
# cmake/ThirdPartyLLGL.cmake configured LLGL as a STATIC library, so the renderer modules
# (LLGL_OpenGL/LLGL_Vulkan/LLGL_Null) are separate archives that LLGL's static module registry
# resolves by name at runtime -- they are not transitive dependencies of the core LLGL target
# and must all reach the final link. CNA_LLGL_LIBRARIES is that complete list, published by
# cna_configure_llgl(). PUBLIC (not PRIVATE) because every test/example executable links the
# backend target directly and needs the same archives on its own link line.
#
# The core archive and its modules genuinely reference each other -- LLGL's static module
# registry calls into LLGL_OpenGL/LLGL_Vulkan/LLGL_Null, and those call back into the core --
# and CMake orders the modules ahead of the core, so a single left-to-right pass leaves the
# registry's calls unresolved (found empirically: "undefined reference to
# LLGL::ModuleOpenGL::AllocRenderSystem" from StaticModuleInterface.cpp). A link group makes
# the linker rescan the set until it converges -- the same mechanism the per-backend test
# macros already use for the CNA/backend cycle.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
    string(JOIN "," CNA_LLGL_LINK_GROUP_MEMBERS ${CNA_LLGL_LIBRARIES})
    target_link_libraries(${BACKEND_TARGET} PUBLIC
        "$<LINK_GROUP:RESCAN,${CNA_LLGL_LINK_GROUP_MEMBERS}>")
else()
    target_link_libraries(${BACKEND_TARGET} PUBLIC ${CNA_LLGL_LIBRARIES})
endif()
target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3)

# plans/plan_llgl.md LLGL-27: LlglEffectBackend needs a REAL runtime GLSL->SPIR-V compile for
# arbitrary user ShaderEffect source when the Vulkan module is selected at runtime (LLGL's own
# OpenGL module accepts the same GLSL text directly, no compiler needed there) -- the exact
# same problem SDL_GPU's own effect backend already solved with libshaderc, and the
# same find_library()-then-glob-fallback this environment's own libshaderc1-only (no -dev
# package) install needs. Duplicated rather than shared with the sdl-gpu module: only the one
# selected renderer module is ever configured, so the other module's variable is never set here.
find_library(CNA_SHADERC_LIBRARY NAMES shaderc_shared shaderc)
if(NOT CNA_SHADERC_LIBRARY)
    file(GLOB CNA_SHADERC_CANDIDATES
        /usr/lib/*/libshaderc.so*
        /usr/lib/libshaderc.so*
        /usr/local/lib/libshaderc.so*
    )
    if(CNA_SHADERC_CANDIDATES)
        list(GET CNA_SHADERC_CANDIDATES 0 CNA_SHADERC_LIBRARY)
    endif()
endif()
if(NOT CNA_SHADERC_LIBRARY)
    message(FATAL_ERROR "CNA: libshaderc not found (required for LLGL's runtime ShaderEffect GLSL compile, LLGL-27) -- install libshaderc1 or libshaderc-dev")
endif()
message(STATUS "CNA: using libshaderc at ${CNA_SHADERC_LIBRARY}")
target_link_libraries(${BACKEND_TARGET} PRIVATE "${CNA_SHADERC_LIBRARY}")
''')

W("modules/renderers/metal/CMakeLists.txt", r'''
# The Metal policy suites (modules/renderers/metal/tests) are deliberately host-portable and
# compile into the CnaTests corpus on every backend, so the policy headers stay reachable in
# every configuration through this unconditional interface target.
add_library(cna_renderer_metal_headers INTERFACE)
target_include_directories(cna_renderer_metal_headers INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

if(CNA_GRAPHICS_BACKEND STREQUAL "METAL")
    # Metal owns the renderer directly; SDL provides only the native macOS window and
    # CAMetalLayer. The Objective-C++ units join the backend archive alongside the C++ ones.
    file(GLOB CNA_METAL_OBJCXX_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.mm")
    cna_add_renderer_backend(${CNA_METAL_OBJCXX_SOURCES})
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 "-framework Metal" "-framework QuartzCore" "-framework Foundation")
endif()
''')

W("modules/renderers/glide/CMakeLists.txt", r'''
# The Glide ABI/policy suites (modules/renderers/glide/tests) are deliberately host-portable
# and compile into the CnaTests corpus on every backend, so the ABI/policy headers stay
# reachable in every configuration through this unconditional interface target.
add_library(cna_renderer_glide_headers INTERFACE)
target_include_directories(cna_renderer_glide_headers INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

if(CNA_GRAPHICS_BACKEND STREQUAL "GLIDE")
    # Glide itself is dynamically loaded at runtime (glide3x.dll); SDL is used only to obtain the
    # application's existing Win32 HWND. Do not link or vendor a particular Glide emulator here.
    cna_add_renderer_backend()
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3)
endif()
''')

print("done")
