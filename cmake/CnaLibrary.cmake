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
    # -- the same pattern this project already uses for SDL3::SDL3/Vulkan::Vulkan below. This
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

# =====================================================================================
# Module targets (MODULARIZATION_PLAN.md §2). The former monolithic CNA STATIC library is
# split into subsystem STATIC libraries over the unmoved current sources; `CNA` itself
# becomes the compatible INTERFACE umbrella carrying the same public surface (include dir,
# public compile definitions, and the full framework + selected backend link closure), so
# every existing consumer keeps `target_link_libraries(x CNA)` working unchanged.
# =====================================================================================

# Common public build surface shared by every CNA module and, transitively, by every
# consumer of any module: the public include tree and the public compile definitions the
# monolithic CNA target used to carry PUBLIC.
add_library(cna_build_flags INTERFACE)
add_library(CNA::BuildFlags ALIAS cna_build_flags)
target_include_directories(cna_build_flags INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_definitions(cna_build_flags INTERFACE
        SOUND_ENABLED
        XNA5
        ${CNA_BACKEND_DEFINE}
        $<$<BOOL:${CNA_NOXNA}>:CNA_NOXNA>
        $<$<BOOL:${CNA_DEVICES}>:CNA_DEVICES>
        $<$<BOOL:${CNA_DRACO_AVAILABLE}>:CNA_DRACO_AVAILABLE>
        $<$<BOOL:${CNA_FFMPEG_AVAILABLE}>:CNA_FFMPEG_AVAILABLE>
)

# Defines one CNA module: a STATIC library named cna_<name> with alias CNA::<Alias>,
# the shared public build surface, and the private include roots every module TU uses
# today (internal headers under src/ resolve exactly as they did in the monolith).
function(cna_add_module target alias)
    add_library(${target} STATIC ${ARGN})
    add_library(CNA::${alias} ALIAS ${target})
    target_link_libraries(${target} PUBLIC cna_build_flags)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
endfunction()

# --- Framework-root source split (the one directory whose files belong to three different
# modules; every other module owns whole directories). Kept as explicit lists on purpose:
# the partition validator below fails the configure if a newly added root TU is missing
# from exactly one of these lists.
set(CNA_MATH_ROOT_SOURCES
    src/Microsoft/Xna/Framework/BoundingBox.cpp
    src/Microsoft/Xna/Framework/BoundingFrustum.cpp
    src/Microsoft/Xna/Framework/BoundingSphere.cpp
    src/Microsoft/Xna/Framework/Color.cpp
    src/Microsoft/Xna/Framework/Curve.cpp
    src/Microsoft/Xna/Framework/CurveKey.cpp
    src/Microsoft/Xna/Framework/CurveKeyCollection.cpp
    src/Microsoft/Xna/Framework/MathHelper.cpp
    src/Microsoft/Xna/Framework/Matrix.cpp
    src/Microsoft/Xna/Framework/Plane.cpp
    src/Microsoft/Xna/Framework/Point.cpp
    src/Microsoft/Xna/Framework/Quaternion.cpp
    src/Microsoft/Xna/Framework/Ray.cpp
    src/Microsoft/Xna/Framework/Rectangle.cpp
    src/Microsoft/Xna/Framework/Vector2.cpp
    src/Microsoft/Xna/Framework/Vector3.cpp
    src/Microsoft/Xna/Framework/Vector4.cpp
)
# FrameworkDispatcher lives with audio, not runtime: its implementation state is the audio
# stream list, and audio/media/runtime all call INTO it (ownership follows dependency
# direction; assigning it to runtime would create a runtime<->audio link cycle for nothing).
set(CNA_AUDIO_ROOT_SOURCES
    src/Microsoft/Xna/Framework/FrameworkDispatcher.cpp
)
set(CNA_RUNTIME_ROOT_SOURCES
    src/Microsoft/Xna/Framework/DrawableGameComponent.cpp
    src/Microsoft/Xna/Framework/ExitingEventArgs.cpp
    src/Microsoft/Xna/Framework/Game.cpp
    src/Microsoft/Xna/Framework/GameComponent.cpp
    src/Microsoft/Xna/Framework/GameComponentCollection.cpp
    src/Microsoft/Xna/Framework/GameComponentCollectionEventArgs.cpp
    src/Microsoft/Xna/Framework/GameServiceContainer.cpp
    src/Microsoft/Xna/Framework/GameTime.cpp
    src/Microsoft/Xna/Framework/GameWindow.cpp
    src/Microsoft/Xna/Framework/GraphicsDeviceInformation.cpp
    src/Microsoft/Xna/Framework/GraphicsDeviceManager.cpp
    src/Microsoft/Xna/Framework/LaunchParameters.cpp
    src/Microsoft/Xna/Framework/PreparingDeviceSettingsEventArgs.cpp
    src/Microsoft/Xna/Framework/TitleContainer.cpp
    src/Microsoft/Xna/Framework/TitleLocation.cpp
)

# --- Per-module directory-owned sources ---
file(GLOB CNA_CORE_SOURCES CONFIGURE_DEPENDS "src/CNA/*.cpp")
file(GLOB_RECURSE CNA_GRAPHICS_CORE_SOURCES CONFIGURE_DEPENDS
    "src/Microsoft/Xna/Framework/Graphics/*.cpp"
    "src/CNA/Internal/Graphics/*.cpp"
    "src/CNA/Internal/Backends/Common/*.cpp"
)
file(GLOB_RECURSE CNA_INPUT_SOURCES CONFIGURE_DEPENDS
    "src/Microsoft/Xna/Framework/Input/*.cpp"
    "src/CNA/Internal/Input/*.cpp"
    "src/CNA/Input/*.cpp"
)
file(GLOB_RECURSE CNA_AUDIO_SOURCES CONFIGURE_DEPENDS
    "src/Microsoft/Xna/Framework/Audio/*.cpp"
    "src/CNA/Internal/Audio/*.cpp"
)
list(APPEND CNA_AUDIO_SOURCES ${CNA_AUDIO_ROOT_SOURCES})
file(GLOB_RECURSE CNA_MEDIA_SOURCES CONFIGURE_DEPENDS
    "src/Microsoft/Xna/Framework/Media/*.cpp"
    "src/CNA/Internal/Media/*.cpp"
)
file(GLOB_RECURSE CNA_CONTENT_SOURCES CONFIGURE_DEPENDS
    "src/Microsoft/Xna/Framework/Content/*.cpp"
    "src/CNA/Internal/Xnb/*.cpp"
    "src/CNA/Internal/GltfImport/*.cpp"
)
file(GLOB CNA_STORAGE_SOURCES CONFIGURE_DEPENDS
    "src/Microsoft/Xna/Framework/Storage/*.cpp"
)
file(GLOB_RECURSE CNA_DEVICES_SOURCES CONFIGURE_DEPENDS
    "src/Microsoft/Devices/*.cpp"
    "src/CNA/Devices/*.cpp"
)
file(GLOB CNA_NOXNA_SOURCES CONFIGURE_DEPENDS
    "src/CNA/Graphics/*.cpp"
)

# Exclude FFmpeg-dependent sources on platforms where FFmpeg is unavailable
if(NOT CNA_FFMPEG_AVAILABLE)
    list(FILTER CNA_MEDIA_SOURCES EXCLUDE REGEX ".*/CNA/Internal/Media/VideoDecoder\\.cpp$")
    list(FILTER CNA_MEDIA_SOURCES EXCLUDE REGEX ".*/Media/Video/VideoPlayer\\.cpp$")
    list(FILTER CNA_MEDIA_SOURCES EXCLUDE REGEX ".*/Media/Video/Video\\.cpp$")
    # REMED-BUILD-013 (discovered while verifying it): VideoContentTypeReader.cpp constructs a
    # Video (Media::Video::Video(...)) -- a genuine, previously-latent gap in this same exclusion
    # list, never hit before because every FFmpeg-unavailable target (MinGW D3D9/D3D11/D3D12,
    # Emscripten, Android) that links a full CNA never got far enough in a real build to reach it.
    list(FILTER CNA_CONTENT_SOURCES EXCLUDE REGEX ".*/CNA/Internal/Xnb/VideoContentTypeReader\\.cpp$")
endif()

# --- Source-partition validator (no-loss ownership gate, MODULARIZATION_PLAN.md §5):
# every production TU under src/ must be owned by exactly one module target, or belong to
# the separately owned backend/GamerServices/Net source trees, or be an explicitly
# FFmpeg-gated exclusion. A new file that matches none of the module globs, or a file
# matched by two of them, fails the configure here rather than silently changing coverage.
file(GLOB_RECURSE _cna_all_production_sources CONFIGURE_DEPENDS "src/*.cpp")
list(FILTER _cna_all_production_sources EXCLUDE REGEX "src/CNA/Internal/Backends/.*")
list(FILTER _cna_all_production_sources EXCLUDE REGEX "src/Microsoft/Xna/Framework/GamerServices/.*")
list(FILTER _cna_all_production_sources EXCLUDE REGEX "src/CNA/Internal/GamerServices/.*")
list(FILTER _cna_all_production_sources EXCLUDE REGEX "src/Microsoft/Xna/Framework/Net/.*")
list(FILTER _cna_all_production_sources EXCLUDE REGEX "src/CNA/Internal/Net/.*")
if(NOT CNA_FFMPEG_AVAILABLE)
    list(FILTER _cna_all_production_sources EXCLUDE REGEX ".*/CNA/Internal/Media/VideoDecoder\\.cpp$")
    list(FILTER _cna_all_production_sources EXCLUDE REGEX ".*/Media/Video/VideoPlayer\\.cpp$")
    list(FILTER _cna_all_production_sources EXCLUDE REGEX ".*/Media/Video/Video\\.cpp$")
    list(FILTER _cna_all_production_sources EXCLUDE REGEX ".*/CNA/Internal/Xnb/VideoContentTypeReader\\.cpp$")
endif()

set(_cna_owned_sources)
foreach(_cna_list IN ITEMS
        CNA_MATH_ROOT_SOURCES CNA_RUNTIME_ROOT_SOURCES CNA_CORE_SOURCES
        CNA_GRAPHICS_CORE_SOURCES CNA_INPUT_SOURCES CNA_AUDIO_SOURCES
        CNA_MEDIA_SOURCES CNA_CONTENT_SOURCES CNA_STORAGE_SOURCES
        CNA_DEVICES_SOURCES CNA_NOXNA_SOURCES)
    foreach(_cna_src IN LISTS ${_cna_list})
        # Normalize the explicit relative lists to absolute paths, as the globs produce.
        cmake_path(ABSOLUTE_PATH _cna_src BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE)
        list(APPEND _cna_owned_sources "${_cna_src}")
    endforeach()
endforeach()
list(LENGTH _cna_owned_sources _cna_owned_count_with_duplicates)
list(REMOVE_DUPLICATES _cna_owned_sources)
list(LENGTH _cna_owned_sources _cna_owned_count)
if(NOT _cna_owned_count_with_duplicates EQUAL _cna_owned_count)
    set(_cna_dup_probe ${_cna_owned_sources})
    message(FATAL_ERROR "CNA: module source partition contains doubly-owned translation units")
endif()
set(_cna_unowned ${_cna_all_production_sources})
list(REMOVE_ITEM _cna_unowned ${_cna_owned_sources})
set(_cna_unknown ${_cna_owned_sources})
list(REMOVE_ITEM _cna_unknown ${_cna_all_production_sources})
if(_cna_unowned)
    message(FATAL_ERROR "CNA: production translation units owned by no module target: ${_cna_unowned}")
endif()
if(_cna_unknown)
    message(FATAL_ERROR "CNA: module source lists reference unknown/unexpected files: ${_cna_unknown}")
endif()
unset(_cna_all_production_sources)
unset(_cna_owned_sources)
unset(_cna_unknown)

# --- Module target definitions and their real link edges (derived, not aspirational;
# see MODULARIZATION_PLAN.md §1.4 for the mechanical include-graph evidence) ---

cna_add_module(cna_math Math ${CNA_MATH_ROOT_SOURCES})
target_link_libraries(cna_math PUBLIC SHARP_RUNTIME)

cna_add_module(cna_core Core ${CNA_CORE_SOURCES})
target_link_libraries(cna_core PUBLIC SHARP_RUNTIME)
# Logger.cpp logs through SDL; the dependency is an implementation detail, not API surface.
target_link_libraries(cna_core PRIVATE SDL3::SDL3)

cna_add_module(cna_graphics_core GraphicsCore ${CNA_GRAPHICS_CORE_SOURCES})
target_link_libraries(cna_graphics_core PUBLIC cna_math cna_core SHARP_RUNTIME)
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
# below this closes the graphics-core <-> backend cycle for the backends that have one, and a
# plain ordered dependency for the rest.
target_link_libraries(cna_graphics_core PRIVATE ${BACKEND_TARGET})

cna_add_module(cna_input Input ${CNA_INPUT_SOURCES})
target_link_libraries(cna_input PUBLIC cna_graphics_core cna_math cna_core SHARP_RUNTIME)
target_link_libraries(cna_input PRIVATE SDL3::SDL3)

cna_add_module(cna_audio Audio ${CNA_AUDIO_SOURCES})
target_link_libraries(cna_audio PUBLIC cna_core cna_math SHARP_RUNTIME)
target_link_libraries(cna_audio PRIVATE SDL3::SDL3 SDL3_mixer::SDL3_mixer)
# Genuine XNA-semantic static-archive cycle (kept, not redesigned): FrameworkDispatcher.Update()
# pumps both the audio streams and MediaPlayer (MediaPlayer::Update/OnActiveSongChanged/
# OnMediaStateChanged), while MediaPlayer itself plays through the audio mixer. Declared on both
# targets (see cna_media below) so CMake repeats the archives.
target_link_libraries(cna_audio PRIVATE cna_media)

cna_add_module(cna_media Media ${CNA_MEDIA_SOURCES})
target_link_libraries(cna_media PUBLIC cna_audio cna_graphics_core SHARP_RUNTIME)
target_link_libraries(cna_media PRIVATE SDL3::SDL3 SDL3_mixer::SDL3_mixer)

cna_add_module(cna_content Content ${CNA_CONTENT_SOURCES})
target_link_libraries(cna_content PUBLIC cna_graphics_core cna_audio cna_media cna_math cna_core SHARP_RUNTIME)
target_link_libraries(cna_content PRIVATE SDL3::SDL3 SDL3_mixer::SDL3_mixer)
# plan_cnj.md CNB-70 (Phase 13D): CNA::Internal::GltfImport::GltfImportCore (used by both
# ContentManager.cpp's GltfModelTypeReader and tools/gltf_to_cnj) needs cgltf.h.
# plan_cnj.md CNB-88 (Phase 14E): RemapOcclusionImageForDualTextureEXT needs
# stb_image.h/stb_image_write.h.
target_include_directories(cna_content PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cgltf
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
)
if(CNA_DRACO_AVAILABLE)
    target_link_libraries(cna_content PRIVATE draco::draco)
endif()

cna_add_module(cna_storage Storage ${CNA_STORAGE_SOURCES})
target_link_libraries(cna_storage PUBLIC SHARP_RUNTIME)
target_link_libraries(cna_storage PRIVATE SDL3::SDL3)

cna_add_module(cna_runtime Runtime ${CNA_RUNTIME_ROOT_SOURCES})
target_link_libraries(cna_runtime PUBLIC
        cna_graphics_core cna_input cna_content cna_audio cna_media cna_core cna_math SHARP_RUNTIME)
target_link_libraries(cna_runtime PRIVATE SDL3::SDL3 SDL3_mixer::SDL3_mixer)

cna_add_module(cna_devices Devices ${CNA_DEVICES_SOURCES})
target_link_libraries(cna_devices PUBLIC cna_runtime cna_graphics_core cna_core cna_math SHARP_RUNTIME)
target_link_libraries(cna_devices PRIVATE SDL3::SDL3)

cna_add_module(cna_noxna NoXna ${CNA_NOXNA_SOURCES})
target_link_libraries(cna_noxna PUBLIC cna_graphics_core SHARP_RUNTIME)

if(ANDROID)
    # Detail::AndroidSensorBridge (Microsoft::Devices::Sensors) calls the NDK's
    # ASensorManager_*/ASensorEventQueue_*/ALooper_* API directly (no JNI) --
    # those symbols live in libandroid.so, not libc/libc++. PUBLIC so any
    # executable linking the devices module (e.g. cna_demo_devices) picks up the
    # transitive dependency automatically (plan_devices.md Task DEVICES-0121).
    #
    # Task ANDR2-006 (2026-07-17): the same file's debug-only
    # __android_log_print() diagnostic (disable/destroy failure reporting)
    # needs liblog.so -- a separate NDK system library from libandroid.so,
    # not pulled in transitively by it.
    target_link_libraries(cna_devices PUBLIC android log)
endif()

if(CNA_FFMPEG_AVAILABLE)
    # PkgConfig::<prefix> IMPORTED targets (see the pkg_check_modules() calls above) carry their
    # own INTERFACE include dirs, link dirs, and libraries together, so this is the only
    # target_link_libraries() call needed -- no separate target_include_directories()/
    # target_link_directories() call, unlike raw _LIBRARIES/_INCLUDE_DIRS variables.
    target_link_libraries(cna_media PRIVATE
        PkgConfig::LIBAVCODEC
        PkgConfig::LIBAVFORMAT
        PkgConfig::LIBAVUTIL
        PkgConfig::LIBSWRESAMPLE
    )
endif()

# --- Compatible umbrella. `CNA` keeps its historical meaning for every consumer: the whole
# framework plus the selected backend, with the public include dir and public defines.
add_library(CNA INTERFACE)
target_link_libraries(CNA INTERFACE
        cna_runtime
        cna_devices
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

# D3D11's/D3D12's SpriteBatch backend (plan_dx.md DX-70/DX-71) calls back into
# Microsoft::Xna::Framework::Graphics::Effect::Apply() for SpriteBatch::Begin(effect)'s custom-
# Effect path -- a genuine, honest circular dependency between the backend static library and the
# CNA graphics core (the backend needs a CNA-defined symbol, while CNA needs the backend to
# implement IGraphicsBackend). Under MinGW's single-pass archive resolution, any executable
# linking CNA (not just the D3D11/D3D12 CTest binaries, which never link the full CNA target) hit
# a real "undefined reference to Effect::Apply()" at link time -- e.g. cna_reference_dump/
# cna_demo_2d. CMake's documented static-library-cycle support (see LINK_INTERFACE_MULTIPLICITY)
# resolves this by repeating the archives on the final link line once this cycle is declared.
# plan_dx9.md D9-112 (Phase D9-11, authorized 2026-07-15): D3D9 joins this condition too.
#
# plan_sdlgpu.md: the SDL_GPU backend calls CNA::Logger::Warn (now in cna_core) for its own
# real, non-fatal capability-gap warnings -- the exact same single-pass archive-scanning problem
# under GNU ld, not just MinGW.
#
# plan_sokol.md SOKOL-20: SokolVertexBufferBackend stores the caller's VertexDeclaration by value,
# and that class's vtable lives in cna_graphics_core's VertexDeclaration.cpp.
#
# plan_diligent.md DILIGENT-1: DiligentGraphicsBackend's constructor calls CNA::Logger::Info to
# report which native API the runtime device selection actually chose.
#
# The intentionally 2D-only SDL_RENDERER, ASCII, FREEDIRECT (formerly DX3) and CANVAS backends use
# the same logger for Unsupported3DGraphicsCallBehavior::WarnAndStub, so they have the same static
# archive cycle. SOFTWARE inherits IGraphicsBackend's default instanced-draw method, whose
# permanently-2D guard shares that diagnostic helper. HEADLESS's concrete backend object
# instantiates the same default method and therefore carries the same reverse reference to
# CNA::Logger::Warn.
#
# GDI's private Software CPU rasterizer is compiled directly into the GDI archive and also calls
# CNA-owned math and colour implementations.
#
# LLGL stores VertexDeclaration values in its backend archive and uses CNA::Logger from the shared
# unsupported-call guard.
#
# The Metal Objective-C++ archive has the same reverse edge: its out-of-line implementation calls
# CNA-owned Effect methods and named math/color values.
#
# Modularization note: the reverse edges land on the specific modules that define the symbols
# (Effect/VertexDeclaration/DxtUtil -> cna_graphics_core, Logger -> cna_core, named math/colour
# values -> cna_math) instead of the former monolith; the declared cycle semantics are unchanged.
if(CNA_GRAPHICS_BACKEND STREQUAL "D3D11"
   OR CNA_GRAPHICS_BACKEND STREQUAL "D3D12"
   OR CNA_GRAPHICS_BACKEND STREQUAL "D3D9"
   OR CNA_GRAPHICS_BACKEND STREQUAL "SDL_GPU"
   OR CNA_GRAPHICS_BACKEND STREQUAL "SOKOL"
   OR CNA_GRAPHICS_BACKEND STREQUAL "DILIGENT"
   OR CNA_GRAPHICS_BACKEND STREQUAL "SDL_RENDERER"
   OR CNA_GRAPHICS_BACKEND STREQUAL "ASCII"
   OR CNA_GRAPHICS_BACKEND STREQUAL "FREEDIRECT"
   OR CNA_GRAPHICS_BACKEND STREQUAL "CANVAS"
   OR CNA_GRAPHICS_BACKEND STREQUAL "SOFTWARE"
   OR CNA_GRAPHICS_BACKEND STREQUAL "HEADLESS"
   OR CNA_GRAPHICS_BACKEND STREQUAL "OPENGLES"
   OR CNA_GRAPHICS_BACKEND STREQUAL "OPENGL33"
   OR CNA_GRAPHICS_BACKEND STREQUAL "WEBGL1"
   OR CNA_GRAPHICS_BACKEND STREQUAL "WEBGL2"
   OR CNA_GRAPHICS_BACKEND STREQUAL "GDI"
   OR CNA_GRAPHICS_BACKEND STREQUAL "METAL"
   OR CNA_GRAPHICS_BACKEND STREQUAL "LLGL")
    target_link_libraries(${BACKEND_TARGET} PRIVATE cna_graphics_core cna_core cna_math)
endif()

# D9-112: D3D9GraphicsBackend::CreateEffectBackend() (below) needs to construct a real
# D3D9EffectBackend, so ${BACKEND_TARGET} now links the isolated, d3dcompiler-carrying
# cna_backend_graphics_d3d9_effect target (D9-111, design decision 16) it didn't need before.
if(TARGET cna_backend_graphics_d3d9_effect)
    target_link_libraries(${BACKEND_TARGET} PRIVATE cna_backend_graphics_d3d9_effect)
endif()

# --- GamerServices + Net ---
if(CNA_ENABLE_NET)
    file(GLOB_RECURSE CNA_GAMERSERVICES_SOURCES CONFIGURE_DEPENDS
        "src/Microsoft/Xna/Framework/GamerServices/*.cpp"
        "src/CNA/Internal/GamerServices/*.cpp"
    )
    add_library(CNA_GamerServices STATIC ${CNA_GAMERSERVICES_SOURCES})
    add_library(CNA::GamerServices ALIAS CNA_GamerServices)
    target_include_directories(CNA_GamerServices
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    # Guide.cpp calls SDL3 directly (message box); this was previously only compiling by
    # accident on hosts with a stray system-wide SDL3 install on the default include path.
    # The public dependency is the runtime layer (Guide/SignedInGamer use Game, graphics,
    # input, audio through it) plus Storage (device selector flows).
    target_link_libraries(CNA_GamerServices PUBLIC cna_runtime cna_storage PRIVATE SDL3::SDL3)

    file(GLOB_RECURSE CNA_NET_SOURCES CONFIGURE_DEPENDS
        "src/Microsoft/Xna/Framework/Net/*.cpp"
        "src/CNA/Internal/Net/*.cpp"
    )
    add_library(CNA_Net STATIC ${CNA_NET_SOURCES})
    add_library(CNA::Net ALIAS CNA_Net)
    target_include_directories(CNA_Net
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(CNA_Net
        PUBLIC
        CNA_GamerServices
        enet
    )
endif()
