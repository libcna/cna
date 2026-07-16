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
    pkg_check_modules(LIBAVCODEC  REQUIRED libavcodec)
    pkg_check_modules(LIBAVFORMAT REQUIRED libavformat)
    pkg_check_modules(LIBAVUTIL   REQUIRED libavutil)
    pkg_check_modules(LIBSWRESAMPLE REQUIRED libswresample)
endif()

file(GLOB_RECURSE CNA_SOURCES CONFIGURE_DEPENDS
        "src/*.cpp"
)
# Exclude backend sources from main CNA library to avoid double compilation and conflicts
list(FILTER CNA_SOURCES EXCLUDE REGEX "src/CNA/Internal/Backends/.*")

# Exclude Net / GamerServices sources — they live in CNA_GamerServices and CNA_Net targets
list(FILTER CNA_SOURCES EXCLUDE REGEX "src/Microsoft/Xna/Framework/GamerServices/.*")
list(FILTER CNA_SOURCES EXCLUDE REGEX "src/Microsoft/Xna/Framework/Net/.*")
list(FILTER CNA_SOURCES EXCLUDE REGEX "src/CNA/Internal/Net/.*")

# Exclude FFmpeg-dependent sources on platforms where FFmpeg is unavailable
if(NOT CNA_FFMPEG_AVAILABLE)
    list(FILTER CNA_SOURCES EXCLUDE REGEX ".*/CNA/Internal/Media/VideoDecoder\\.cpp$")
    list(FILTER CNA_SOURCES EXCLUDE REGEX ".*/Media/Video/VideoPlayer\\.cpp$")
    list(FILTER CNA_SOURCES EXCLUDE REGEX ".*/Media/Video/Video\\.cpp$")
endif()

add_library(CNA STATIC
        ${CNA_SOURCES}
)

target_include_directories(CNA
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(CNA
        PUBLIC
        SOUND_ENABLED
        XNA5
        $<$<BOOL:${CNA_NOXNA}>:CNA_NOXNA>
        $<$<BOOL:${CNA_DEVICES}>:CNA_DEVICES>
)

target_link_libraries(CNA
        PUBLIC
        SHARP_RUNTIME
        ${BACKEND_TARGET}
        PRIVATE
        SDL3::SDL3
        SDL3_image::SDL3_image
        SDL3_mixer::SDL3_mixer
#        SDL3_ttf::SDL3_ttf
)

# D3D11's/D3D12's SpriteBatch backend (plan_dx.md DX-70/DX-71) calls back into
# Microsoft::Xna::Framework::Graphics::Effect::Apply() for SpriteBatch::Begin(effect)'s custom-
# Effect path -- a genuine, honest circular dependency between the backend static library and CNA
# itself (the backend needs a CNA-defined symbol, while CNA needs the backend to implement
# IGraphicsBackend). Under MinGW's single-pass archive resolution, any executable linking CNA
# (not just the D3D11/D3D12 CTest binaries, which never link the full CNA target) hit a real
# "undefined reference to Effect::Apply()" at link time -- e.g. cna_reference_dump/cna_demo_2d --
# because libCNA.a is fully scanned before libcna_backend_graphics_d3d11.a creates the need.
# CMake's documented static-library-cycle support (see LINK_INTERFACE_MULTIPLICITY) resolves this
# by repeating the archives on the final link line once this cycle is declared.
# plan_dx9.md D9-112 (Phase D9-11, authorized 2026-07-15): D3D9 now joins this condition too --
# D3D9SpriteBatchBackend::FlushBatch() calls customEffect_->Apply() (a CNA-defined symbol) for
# SpriteBatch::Begin(effect)'s custom-Effect path, the same genuine circular dependency D3D11/D3D12
# already have. (D9-10's own original note said "add D3D9 here when D9-112 actually needs it" --
# this is that task.)
#
# plan_sdlgpu.md: the SDL_GPU backend (SdlGpuGraphicsBackend.cpp) calls CNA::Logger::Warn (a
# CNA-defined symbol) for its own real, non-fatal capability-gap warnings -- the exact same
# single-pass archive-scanning problem under GNU ld, not just MinGW: any executable linking the
# plain CNA target without --start-group/--end-group (cna_reference_dump, cna_demo_xact) hit a
# real "undefined reference to CNA::Logger::Warn" at link time, because libCNA.a (which contains
# Logger.cpp.o) is fully scanned before libcna_backend_graphics_sdl_gpu.a creates the need. Only
# the SDL_GPU CTest targets (cna_sdlgpu_test's own --start-group/--end-group wrapping) avoided
# this; every other SDL_GPU-backend executable did not. Fixed the same way as D3D11/D3D12.
if(CNA_GRAPHICS_BACKEND STREQUAL "D3D11" OR CNA_GRAPHICS_BACKEND STREQUAL "D3D12" OR CNA_GRAPHICS_BACKEND STREQUAL "D3D9" OR CNA_GRAPHICS_BACKEND STREQUAL "SDL_GPU")
    target_link_libraries(${BACKEND_TARGET} PRIVATE CNA)
endif()

# D9-112: D3D9GraphicsBackend::CreateEffectBackend() (below) needs to construct a real
# D3D9EffectBackend, so ${BACKEND_TARGET} now links the isolated, d3dcompiler-carrying
# cna_backend_graphics_d3d9_effect target (D9-111, design decision 16) it didn't need before.
if(TARGET cna_backend_graphics_d3d9_effect)
    target_link_libraries(${BACKEND_TARGET} PRIVATE cna_backend_graphics_d3d9_effect)
endif()

if(ANDROID)
    # Detail::AndroidSensorBridge (Microsoft::Devices::Sensors) calls the NDK's
    # ASensorManager_*/ASensorEventQueue_*/ALooper_* API directly (no JNI) --
    # those symbols live in libandroid.so, not libc/libc++. PUBLIC so any
    # executable linking CNA (e.g. cna_demo_devices) picks up the transitive
    # dependency automatically (plan_devices.md Task DEVICES-0121).
    target_link_libraries(CNA PUBLIC android)
endif()

if(CNA_FFMPEG_AVAILABLE)
    target_link_libraries(CNA PRIVATE
        ${LIBAVCODEC_LIBRARIES}
        ${LIBAVFORMAT_LIBRARIES}
        ${LIBAVUTIL_LIBRARIES}
        ${LIBSWRESAMPLE_LIBRARIES}
    )
    target_include_directories(CNA PRIVATE
        ${LIBAVCODEC_INCLUDE_DIRS}
        ${LIBAVFORMAT_INCLUDE_DIRS}
        ${LIBAVUTIL_INCLUDE_DIRS}
        ${LIBSWRESAMPLE_INCLUDE_DIRS}
    )
endif()

# --- GamerServices + Net ---
if(CNA_ENABLE_NET)
    file(GLOB_RECURSE CNA_GAMERSERVICES_SOURCES CONFIGURE_DEPENDS
        "src/Microsoft/Xna/Framework/GamerServices/*.cpp"
    )
    add_library(CNA_GamerServices STATIC ${CNA_GAMERSERVICES_SOURCES})
    target_include_directories(CNA_GamerServices
        PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    # Guide.cpp calls SDL3 directly (message box); this was previously only compiling by
    # accident on hosts with a stray system-wide SDL3 install on the default include path.
    target_link_libraries(CNA_GamerServices PUBLIC CNA PRIVATE SDL3::SDL3)

    file(GLOB_RECURSE CNA_NET_SOURCES CONFIGURE_DEPENDS
        "src/Microsoft/Xna/Framework/Net/*.cpp"
        "src/CNA/Internal/Net/*.cpp"
    )
    add_library(CNA_Net STATIC ${CNA_NET_SOURCES})
    target_include_directories(CNA_Net
        PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(CNA_Net
        PUBLIC
        CNA_GamerServices
        enet
    )
endif()
