cmake_minimum_required(VERSION 3.20)

project(CNA LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Emscripten disables C++ exception throw/catch support by default (an uncaught throw aborts
# the whole runtime instead of unwinding) - this codebase relies on exceptions pervasively
# (System::Exception hierarchy, EXPECT_THROW tests, etc.), so it must be enabled globally,
# before sharp-runtime is added below, since unwinding requires every frame in the call chain
# to be compiled with -fexceptions.
if(EMSCRIPTEN)
    add_compile_options(-fexceptions)
    add_link_options(-fexceptions -sNO_DISABLE_EXCEPTION_CATCHING=1)
endif()

option(CNA_BUILD_TESTS "Build CNA tests" ON)
option(CNA_NOXNA "Enable CNA NOXNA extended graphics layer (beyond XNA 4.0)" OFF)
option(CNA_DEVICES "Enable CNA-specific device/sensor extensions beyond XNA 4.0 (battery, camera, clipboard, ...)" OFF)
option(CNA_ENABLE_NET "Build CNA networking (GamerServices + Net, requires ENet)" ON)

# DISPLAY value baked into every GPU/window-creating ctest's ENVIRONMENT property (SDL_VIDEODRIVER=x11).
# Defaults to :0 for backward compatibility; override with e.g. -DCNA_TEST_DISPLAY=:99 to run the full
# suite against a virtual display (Xvfb) instead of the real desktop.
set(CNA_TEST_DISPLAY ":0" CACHE STRING "DISPLAY value used by GPU/window-creating ctest tests")

include(cmake/ThirdPartySDL.cmake)
cna_configure_vendored_sdl()

if(CNA_ENABLE_NET)
    include(cmake/ThirdPartyENet.cmake)
    cna_configure_vendored_enet()
endif()

# Optional sanitizers (INPUT-BUILD-006 / INPUT-CI-005). Empty = off. Comma-separated clang/gcc
# sanitizer list, e.g. -DCNA_SANITIZE=address,undefined. Applied from here on, so CNA, sharp-runtime,
# and the tests are instrumented; the vendored SDL/ENet builds above stay uninstrumented (ASan/UBSan
# tolerate mixing). For diagnostics/CI only — never ship a sanitized build.
set(CNA_SANITIZE "" CACHE STRING "Comma-separated sanitizers to enable (e.g. address,undefined); empty disables")
if(CNA_SANITIZE)
    message(STATUS "CNA: sanitizers enabled: ${CNA_SANITIZE}")
    add_compile_options(-fsanitize=${CNA_SANITIZE} -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=${CNA_SANITIZE})
endif()

set(SHARP_RUNTIME_BUILD_TESTS OFF CACHE BOOL "Build sharp-runtime tests" FORCE)
# sharp-runtime is a SIBLING repository checkout, not a git submodule of this
# repo (Task DEV-BUILD-001) -- `git submodule update --init` cannot fetch it.
# It must be cloned next to this repo's own directory, so that
# "${CMAKE_SOURCE_DIR}/../sharp-runtime" resolves to a real checkout.
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/../sharp-runtime/CMakeLists.txt")
    message(FATAL_ERROR
        "CNA: Missing sibling repository 'sharp-runtime' at "
        "${CMAKE_SOURCE_DIR}/../sharp-runtime -- this is a separate git "
        "checkout expected next to this repo's own directory, not a git "
        "submodule (git submodule update --init will not fetch it). Fix: "
        "cd ${CMAKE_SOURCE_DIR}/.. && "
        "git clone https://github.com/openeggbert/sharp-runtime.git")
endif()
add_subdirectory(../sharp-runtime SHARP_RUNTIME)

#find_package(SDL3_ttf REQUIRED)

# --- Graphics Backend Selection ---
# EasyGL is the default on Linux and Emscripten (WebGL 2 = OpenGL ES 3.0).
# Other platforms default to SDL_RENDERER.
if(EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_cna_default_backend "EASYGL")
else()
    set(_cna_default_backend "SDL_RENDERER")
endif()
set(CNA_GRAPHICS_BACKEND "${_cna_default_backend}" CACHE STRING "Graphics backend to use (SDL_RENDERER, EASYGL, BGFX, or VULKAN)")
set_property(CACHE CNA_GRAPHICS_BACKEND PROPERTY STRINGS "SDL_RENDERER" "EASYGL" "BGFX" "VULKAN")

option(CNA_BACKEND_SDL_RENDERER "Enable SDL_Renderer graphics backend" OFF)
option(CNA_BACKEND_EASY_GL "Enable easy-gl graphics backend" OFF)
option(CNA_BACKEND_BGFX "Enable bgfx graphics backend" OFF)
option(CNA_BACKEND_VULKAN "Enable Vulkan graphics backend" OFF)

set(_cna_explicit_backend_selection OFF)
if(CNA_BACKEND_SDL_RENDERER OR CNA_BACKEND_EASY_GL OR CNA_BACKEND_BGFX OR CNA_BACKEND_VULKAN)
    set(_cna_explicit_backend_selection ON)
endif()

if(_cna_explicit_backend_selection)
    set(_cna_enabled_backends)
    if(CNA_BACKEND_SDL_RENDERER)
        list(APPEND _cna_enabled_backends "SDL_RENDERER")
    endif()
    if(CNA_BACKEND_EASY_GL)
        list(APPEND _cna_enabled_backends "EASYGL")
    endif()
    if(CNA_BACKEND_BGFX)
        list(APPEND _cna_enabled_backends "BGFX")
    endif()
    if(CNA_BACKEND_VULKAN)
        list(APPEND _cna_enabled_backends "VULKAN")
    endif()

    list(LENGTH _cna_enabled_backends _cna_enabled_backends_count)
    if(NOT _cna_enabled_backends_count EQUAL 1)
        message(FATAL_ERROR "CNA: Exactly one backend option must be ON when using CNA_BACKEND_* options.")
    endif()

    list(GET _cna_enabled_backends 0 CNA_GRAPHICS_BACKEND)
endif()

if(CNA_GRAPHICS_BACKEND STREQUAL "EASYGL")
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(WARNING "CNA: EASYGL backend is primarily tested on Linux. Other platforms may require additional setup.")
    endif()
    # easy-gl is a SIBLING repository checkout, not a git submodule of this
    # repo (Task DEV-BUILD-001) -- see sharp-runtime's identical check above
    # for the full rationale.
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/../easy-gl/CMakeLists.txt")
        message(FATAL_ERROR
            "CNA: Missing sibling repository 'easy-gl' at "
            "${CMAKE_SOURCE_DIR}/../easy-gl -- this is a separate git "
            "checkout expected next to this repo's own directory, not a git "
            "submodule (git submodule update --init will not fetch it). Fix: "
            "cd ${CMAKE_SOURCE_DIR}/.. && "
            "git clone https://github.com/openeggbert/easy-gl.git")
    endif()
    add_subdirectory(../easy-gl easy-gl)
endif()

if(CNA_GRAPHICS_BACKEND STREQUAL "BGFX" AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(WARNING "CNA: BGFX backend is primarily tested on Linux. Other platforms may require additional setup.")
endif()

if(CNA_GRAPHICS_BACKEND STREQUAL "SDL_RENDERER")
    message(STATUS "CNA: Using SDL_RENDERER graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/SdlRenderer")
    set(BACKEND_TARGET "cna_backend_graphics_sdl_renderer")
    add_compile_definitions(CNA_BACKEND_SDL_RENDERER)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "EASYGL")
    message(STATUS "CNA: Using EASYGL graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/EasyGL")
    set(BACKEND_TARGET "cna_backend_graphics_easygl")
    add_compile_definitions(CNA_BACKEND_EASYGL)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "BGFX")
    message(STATUS "CNA: Using BGFX graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/Bgfx")
    set(BACKEND_TARGET "cna_backend_graphics_bgfx")
    add_compile_definitions(CNA_BACKEND_BGFX)

    include(FetchContent)
    option(CNA_BGFX_BUILD_SHADERC "Build bgfx shaderc tool (needed to compile 3D shaders)" OFF)
    set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    if(NOT CNA_BGFX_BUILD_SHADERC)
        set(BGFX_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    endif()
    set(BGFX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BGFX_INSTALL OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        bgfx_cmake
        GIT_REPOSITORY https://github.com/bkaradzic/bgfx.cmake.git
        GIT_TAG master
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
        GIT_SUBMODULES_RECURSE TRUE
    )
    FetchContent_MakeAvailable(bgfx_cmake)

    set(CNA_BGFX_SHADER_INCLUDE_DIR "${bgfx_cmake_SOURCE_DIR}/bgfx/examples/common/imgui")
elseif(CNA_GRAPHICS_BACKEND STREQUAL "VULKAN")
    message(STATUS "CNA: Using VULKAN graphics backend")
    set(BACKEND_DIR "src/CNA/Internal/Backends/Vulkan")
    set(BACKEND_TARGET "cna_backend_graphics_vulkan")
    add_compile_definitions(CNA_BACKEND_VULKAN)
    find_package(Vulkan REQUIRED)
else()

    message(FATAL_ERROR "CNA: Unknown graphics backend: ${CNA_GRAPHICS_BACKEND}")
endif()

# --- FFmpeg (video decoding) — not available on Emscripten/Android, nor on the
# mingw-w64 Windows cross-build (no mingw-w64 FFmpeg dev packages in this toolchain;
# pkg-config would otherwise silently resolve to the host's native FFmpeg and poison
# the cross-compile's include path with native glibc headers). ---
if(MINGW OR EMSCRIPTEN OR ANDROID)
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

# Common backend interfaces
add_library(cna_backend_graphics_common INTERFACE)
target_include_directories(cna_backend_graphics_common INTERFACE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Backend target
file(GLOB BACKEND_SOURCES "${BACKEND_DIR}/*.cpp")
add_library(${BACKEND_TARGET} STATIC ${BACKEND_SOURCES})
target_link_libraries(${BACKEND_TARGET} PUBLIC cna_backend_graphics_common SHARP_RUNTIME)
target_include_directories(${BACKEND_TARGET} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(CNA_GRAPHICS_BACKEND STREQUAL "SDL_RENDERER")
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "EASYGL")
    target_link_libraries(${BACKEND_TARGET} PRIVATE easy-gl SDL3::SDL3)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "BGFX")
    target_link_libraries(${BACKEND_TARGET} PUBLIC bgfx bx)
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3)
    target_include_directories(${BACKEND_TARGET} PRIVATE ${CNA_BGFX_SHADER_INCLUDE_DIR})
elseif(CNA_GRAPHICS_BACKEND STREQUAL "VULKAN")
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 Vulkan::Vulkan)
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

# --- Examples ---
# The 3D house demo exercises the EasyGL 3D pipeline. Other backends throw
# "3D not supported", so the target is only enabled with the EASYGL backend.
# Emscripten uses WebGL 2 (= OpenGL ES 3.0), which the EasyGL backend targets.
option(CNA_BUILD_EXAMPLES "Build CNA example applications (house3d demo, demo_2d, ...)" ON)
if(CNA_BUILD_EXAMPLES)
    # ---- 2D sprite demo (works with all backends) ----------------------------
    add_executable(cna_demo_2d
        examples/demo_2d/src/Main.cpp
        examples/demo_2d/src/Game1.cpp
    )
    target_include_directories(cna_demo_2d PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_2d/src
    )

    if(EMSCRIPTEN)
        set_target_properties(cna_demo_2d PROPERTIES SUFFIX ".html")
        target_link_libraries(cna_demo_2d PRIVATE
            CNA ${BACKEND_TARGET} SDL3::SDL3-static SHARP_RUNTIME)
        target_link_options(cna_demo_2d PRIVATE
            -sALLOW_MEMORY_GROWTH=1
            -sSTACK_SIZE=1048576
            -sINITIAL_MEMORY=134217728
            -sFORCE_FILESYSTEM=1
            "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_2d/Content@/Content"
        )
    else()
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_2d PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_2d PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_2d PRIVATE SDL3::SDL3main)
        endif()
        add_custom_command(TARGET cna_demo_2d POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_2d/Content"
                "$<TARGET_FILE_DIR:cna_demo_2d>/Content"
            COMMENT "Copying Content next to cna_demo_2d executable")
        if(WIN32)
            set_target_properties(cna_demo_2d PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_2d)
        endif()
    endif()

    # ---- Audio demo (works with all backends) --------------------------------
    add_executable(cna_demo_sound
        examples/demo_sound/src/Main.cpp
        examples/demo_sound/src/SoundDemo.cpp
    )
    target_include_directories(cna_demo_sound PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_sound/src
    )

    if(EMSCRIPTEN)
        set_target_properties(cna_demo_sound PROPERTIES SUFFIX ".html")
        target_link_libraries(cna_demo_sound PRIVATE
            CNA ${BACKEND_TARGET} SDL3::SDL3-static SHARP_RUNTIME)
        target_link_options(cna_demo_sound PRIVATE
            -sALLOW_MEMORY_GROWTH=1
            -sSTACK_SIZE=1048576
            -sINITIAL_MEMORY=134217728
            -sFORCE_FILESYSTEM=1
            "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_sound/Content@/Content"
        )
    else()
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_sound PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_sound PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_sound PRIVATE SDL3::SDL3main)
        endif()
        add_custom_command(TARGET cna_demo_sound POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_sound/Content"
                "$<TARGET_FILE_DIR:cna_demo_sound>/Content"
            COMMENT "Copying Content next to cna_demo_sound executable")
        if(WIN32)
            set_target_properties(cna_demo_sound PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_sound)
        endif()
    endif()

    # ---- XACT demo (desktop only — XACT is Windows/Xbox-specific, no web content) -
    if(NOT EMSCRIPTEN AND NOT ANDROID)
    add_executable(cna_demo_xact
        examples/demo_xact/src/Main.cpp
        examples/demo_xact/src/XactDemo.cpp
    )
    target_include_directories(cna_demo_xact PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_xact/src
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
        target_link_libraries(cna_demo_xact PRIVATE
            -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
            SHARP_RUNTIME)
    else()
        target_link_libraries(cna_demo_xact PRIVATE CNA SHARP_RUNTIME)
    endif()
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_demo_xact PRIVATE SDL3::SDL3main)
    endif()
    add_custom_command(TARGET cna_demo_xact POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_xact/Content"
            "$<TARGET_FILE_DIR:cna_demo_xact>/Content"
        COMMENT "Copying Content next to cna_demo_xact executable")
    if(WIN32)
        set_target_properties(cna_demo_xact PROPERTIES WIN32_EXECUTABLE TRUE)
        cna_copy_sdl_runtime(cna_demo_xact)
    endif()
    endif() # NOT EMSCRIPTEN AND NOT ANDROID

    # ---- Input & Touch demo (works with all backends) ------------------------
    add_executable(cna_demo_input
        examples/demo_input/src/Main.cpp
        examples/demo_input/src/InputDemo.cpp
    )
    target_include_directories(cna_demo_input PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_input/src
    )

    if(EMSCRIPTEN)
        set_target_properties(cna_demo_input PROPERTIES SUFFIX ".html")
        target_link_libraries(cna_demo_input PRIVATE
            CNA ${BACKEND_TARGET} SDL3::SDL3-static SHARP_RUNTIME)
        target_link_options(cna_demo_input PRIVATE
            -sALLOW_MEMORY_GROWTH=1
            -sSTACK_SIZE=1048576
            -sINITIAL_MEMORY=67108864
            -sFORCE_FILESYSTEM=1
        )
    else()
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_input PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_input PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_input PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            set_target_properties(cna_demo_input PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_input)
        endif()
    endif()

    # ---- Devices (Sensors/VibrateController) demo (works with all backends) --
    # Desktop/Emscripten only: NOT ANDROID (Task DEV-BUILD-004). A plain
    # add_executable() cannot be a valid Android app target at all --
    # <SDL3/SDL_main.h> #defines main to SDL_main on Android
    # (SDL_MAIN_NEEDED, so SDLActivity.java's dlsym("SDL_main") lookup on a
    # shared library finds a real symbol), which leaves no literal `main`
    # symbol for a plain ELF executable's C runtime startup
    # (crtbegin_dynamic.o) to link against -- confirmed via a real Android
    # cross-compile attempt: FAILS at the *link* step with "undefined
    # symbol: main", not at compile time. The real, working Android build of
    # this same demo is the entirely separate Gradle/CMake project under
    # examples/demo_devices/android/ (docs/devices-build.md Section 4.1),
    # which compiles Main.cpp/DevicesDemo.cpp into a genuine shared library
    # ("main") via its own app/jni/src/CMakeLists.txt -- not this target.
    if(NOT ANDROID)
    add_executable(cna_demo_devices
        examples/demo_devices/src/Main.cpp
        examples/demo_devices/src/DevicesDemo.cpp
    )
    target_include_directories(cna_demo_devices PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_devices/src
    )

    if(EMSCRIPTEN)
        set_target_properties(cna_demo_devices PROPERTIES SUFFIX ".html")
        target_link_libraries(cna_demo_devices PRIVATE
            CNA ${BACKEND_TARGET} SDL3::SDL3-static SHARP_RUNTIME)
        target_link_options(cna_demo_devices PRIVATE
            -sALLOW_MEMORY_GROWTH=1
            -sSTACK_SIZE=1048576
            -sINITIAL_MEMORY=67108864
            -sFORCE_FILESYSTEM=1
        )
    else()
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_devices PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_devices PRIVATE CNA SHARP_RUNTIME)
        endif()
        # examples/demo_devices/src/Main.cpp includes <SDL3/SDL_main.h>
        # directly (needed on Android so SDLActivity.java's dlsym("SDL_main")
        # lookup finds a real symbol) -- unlike every other demo target,
        # which never touches an SDL3 header directly and only goes through
        # CNA's own abstraction layer. CNA links SDL3::SDL3 PRIVATE (line
        # ~222 above), so that include path does not propagate transitively
        # to any executable that only links CNA. On desktop this went
        # unnoticed because the host compiler's own default system include
        # path (e.g. /usr/local/include on this container) happens to carry
        # a coincidentally-installed system SDL3 dev package -- cross-
        # compiling for Android has no such host-path fallback, and fails
        # with "SDL3/SDL_main.h file not found". Link SDL3::SDL3 directly so
        # this target gets its own real include path on every platform,
        # independent of host-system luck.
        target_link_libraries(cna_demo_devices PRIVATE SDL3::SDL3)
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_devices PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            set_target_properties(cna_demo_devices PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_devices)
        endif()
    endif()
    endif() # NOT ANDROID
endif()

# ---- Combined input smoke sample: Keyboard+Mouse+GamePad+Touch+TextInput -----
if(CNA_BUILD_EXAMPLES AND NOT EMSCRIPTEN AND NOT ANDROID)
    add_executable(cna_input_smoke examples/input_smoke/InputSmoke.cpp)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
        target_link_libraries(cna_input_smoke PRIVATE
            -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group SHARP_RUNTIME)
    else()
        target_link_libraries(cna_input_smoke PRIVATE CNA SHARP_RUNTIME)
    endif()
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_input_smoke PRIVATE SDL3::SDL3main)
    endif()
    if(WIN32)
        set_target_properties(cna_input_smoke PROPERTIES WIN32_EXECUTABLE TRUE)
        cna_copy_sdl_runtime(cna_input_smoke)
    endif()
endif()

# ---- NOXNA settings example (no window needed — pure API test) ---------------
if(CNA_BUILD_EXAMPLES AND CNA_NOXNA)
    add_executable(cna_example_noxna_settings examples/noxna_settings_example.cpp)
    target_link_libraries(cna_example_noxna_settings PRIVATE CNA)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_example_noxna_settings PRIVATE SDL3::SDL3main)
    endif()
    if(CNA_BUILD_TESTS)
        add_test(NAME NOXNA_Settings_Compile_Run
                 COMMAND cna_example_noxna_settings)
    endif()
endif()

# ---- Task 479: CNA-side reference-value dump tool (no window/GraphicsDevice needed) -----------
# Dumps enums, state presets, PackedVector, and Viewport reference values as JSON, mirroring
# tools/fna-reference/*.cs exactly so scripts/compare-fna-reference.py can diff the two outputs.
# Not registered as a ctest: comparing against the FNA-side harness additionally requires mono/
# xbuild and a locally-built FNA.dll (tools/fna-reference/README.md), which isn't guaranteed on
# every machine that builds CNA -- this is a manually-invoked developer verification tool.
if(CNA_BUILD_EXAMPLES AND NOT EMSCRIPTEN AND NOT ANDROID)
    add_executable(cna_reference_dump tools/cna-reference/CnaReferenceDump.cpp)
    target_link_libraries(cna_reference_dump PRIVATE CNA)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_reference_dump PRIVATE SDL3::SDL3main)
    endif()
endif()

if(CNA_BUILD_EXAMPLES AND (CNA_GRAPHICS_BACKEND STREQUAL "EASYGL" OR CNA_GRAPHICS_BACKEND STREQUAL "VULKAN"))
    # ---- 3D house demo (EasyGL and Vulkan) -----------------------------------
    add_executable(cna_house3d_demo examples/house3d_demo.cpp)

    if(EMSCRIPTEN)
        # ---- Emscripten / WebAssembly ----------------------------------------
        # Produce a self-contained HTML page. The scene is fully procedural so
        # no asset preloading is required.
        set_target_properties(cna_house3d_demo PROPERTIES SUFFIX ".html")
        # wasm-ld resolves circular static-lib references without linker groups.
        target_link_libraries(cna_house3d_demo PRIVATE
            CNA ${BACKEND_TARGET} SDL3::SDL3-static SHARP_RUNTIME)
        target_link_options(cna_house3d_demo PRIVATE
            -sALLOW_MEMORY_GROWTH=1
            -sSTACK_SIZE=1048576
            -sINITIAL_MEMORY=67108864       # 64 MB — enough for a procedural scene
            -sFORCE_FILESYSTEM=1
            "-sMIN_WEBGL_VERSION=2"         # WebGL 2 = OpenGL ES 3.0
            "-sMAX_WEBGL_VERSION=2"
        )
    else()
        # ---- Native desktop --------------------------------------------------
        # The CNA library and the EasyGL backend static lib have circular symbol
        # references (backend uses Color/Rectangle from CNA, CNA uses backend
        # interfaces); wrap them in a linker group so GNU ld resolves both.
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_house3d_demo PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_house3d_demo PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_house3d_demo PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            set_target_properties(cna_house3d_demo PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_house3d_demo)
        endif()
    endif()

    # Task 11.11 (Phase 11b): real windowed proof that AvatarRenderer's real-rendering
    # extension (Phase 10) draws Phase 11a's procedurally-generated, non-synthetic avatar
    # content (tools/avatar_builder/ + tools/avatar_asset_pipeline/convert_avatar.py,
    # Task 11.10). Needs CNA_GamerServices (AvatarRenderer lives there, same gating as
    # cna_test_avatar_real_render below), already inside this EASYGL/VULKAN-only scope.
    # Emscripten isn't wired up for this demo yet (deferred, no immediate need).
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_avatar
            examples/demo_avatar/src/Main.cpp
            examples/demo_avatar/src/AvatarDemo.cpp
        )
        target_include_directories(cna_demo_avatar PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_avatar/src
        )
        target_link_libraries(cna_demo_avatar PRIVATE CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_avatar PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_avatar PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_avatar PRIVATE SDL3::SDL3main)
        endif()
        add_custom_command(TARGET cna_demo_avatar POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_avatar/Content"
                "$<TARGET_FILE_DIR:cna_demo_avatar>/Content"
            COMMENT "Copying Content next to cna_demo_avatar executable")
        if(WIN32)
            set_target_properties(cna_demo_avatar PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_avatar)
        endif()
    endif()

    # Task 15.1: cna_demo_net_client_server_arena - real two-process NetworkSession::Create/
    # Find/Join, LocalNetworkGamer::SendData/ReceiveData via PacketWriter/PacketReader, and
    # GamerJoined/SessionEnded events over real ENet, rendered as a small 2D arena. Needs
    # CNA_GamerServices (SignedInGamer) and CNA_Net (NetworkSession) - same gating as
    # cna_demo_avatar above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_net_client_server_arena
            examples/demo_net_client_server_arena/src/Main.cpp
            examples/demo_net_client_server_arena/src/ArenaGame.cpp
        )
        target_include_directories(cna_demo_net_client_server_arena PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_net_client_server_arena/src
        )
        target_link_libraries(cna_demo_net_client_server_arena PRIVATE CNA_GamerServices CNA_Net)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_net_client_server_arena PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_net_client_server_arena PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_net_client_server_arena PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            set_target_properties(cna_demo_net_client_server_arena PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_net_client_server_arena)
        endif()
    endif()

    # Task 15.2: cna_demo_packet_roundtrip - every XNA-type PacketWriter::Write/PacketReader::Read
    # overload (Vector2/3/4, Matrix, Quaternion, Color, float, double), single process, console-only,
    # no networking or windowing at all - needs only CNA_Net.
    if(CNA_ENABLE_NET)
        add_executable(cna_demo_packet_roundtrip
            examples/demo_packet_roundtrip/src/Main.cpp
        )
        target_link_libraries(cna_demo_packet_roundtrip PRIVATE CNA_Net SHARP_RUNTIME)
    endif()

    # Task 15.3: cna_demo_qos_probe - QualityOfService/NetworkGamer::RoundtripTime measured
    # between two real gamers over real ENet. Console-only, no windowing - needs
    # CNA_GamerServices (SignedInGamer) and CNA_Net (NetworkSession/QualityOfService).
    if(CNA_ENABLE_NET)
        add_executable(cna_demo_qos_probe
            examples/demo_qos_probe/src/Main.cpp
        )
        target_link_libraries(cna_demo_qos_probe PRIVATE CNA_GamerServices CNA_Net SHARP_RUNTIME)
    endif()

    # Task 15.4: cna_demo_simulated_network_conditions - NetworkSession::SimulatedLatencyProperty/
    # SimulatedPacketLossProperty over a real two-process ENet session, rendered as a small
    # Pong-style match. Needs CNA_GamerServices (SignedInGamer) and CNA_Net (NetworkSession) - same
    # gating as cna_demo_net_client_server_arena above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_simulated_network_conditions
            examples/demo_simulated_network_conditions/src/Main.cpp
            examples/demo_simulated_network_conditions/src/SimGame.cpp
        )
        target_include_directories(cna_demo_simulated_network_conditions PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_simulated_network_conditions/src
        )
        target_link_libraries(cna_demo_simulated_network_conditions PRIVATE CNA_GamerServices CNA_Net)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_simulated_network_conditions PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_simulated_network_conditions PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_simulated_network_conditions PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            set_target_properties(cna_demo_simulated_network_conditions PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_simulated_network_conditions)
        endif()
    endif()

    # Task 15.5: cna_demo_session_browser - NetworkSession::Find(...)/AvailableNetworkSessionCollection
    # rendered as a scrollable session-lobby list, Up/Down to select, Enter to Join. Needs
    # CNA_GamerServices (SignedInGamer) and CNA_Net (NetworkSession) - same gating as
    # cna_demo_net_client_server_arena above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_session_browser
            examples/demo_session_browser/src/Main.cpp
            examples/demo_session_browser/src/BrowserGame.cpp
        )
        target_include_directories(cna_demo_session_browser PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_session_browser/src
        )
        target_link_libraries(cna_demo_session_browser PRIVATE CNA_GamerServices CNA_Net)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_session_browser PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_session_browser PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_session_browser PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            set_target_properties(cna_demo_session_browser PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_session_browser)
        endif()
    endif()

    # Task 15.6: cna_demo_gamer_roster_hud - the full gamer-roster event surface (GamerJoined/
    # GamerLeft/HostChanged/SessionEnded) plus per-gamer IsHost/IsLocal/IsReady/IsTalking flags,
    # rendered as a live roster panel. Needs CNA_GamerServices (SignedInGamer) and CNA_Net
    # (NetworkSession) - same gating as cna_demo_net_client_server_arena above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_gamer_roster_hud
            examples/demo_gamer_roster_hud/src/Main.cpp
            examples/demo_gamer_roster_hud/src/RosterGame.cpp
        )
        target_include_directories(cna_demo_gamer_roster_hud PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_gamer_roster_hud/src
        )
        target_link_libraries(cna_demo_gamer_roster_hud PRIVATE CNA_GamerServices CNA_Net)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_gamer_roster_hud PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_gamer_roster_hud PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_gamer_roster_hud PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            set_target_properties(cna_demo_gamer_roster_hud PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_gamer_roster_hud)
        endif()
    endif()

    # Task 15.7: cna_demo_session_lifecycle_events - NetworkSession::StartGame()/EndGame(),
    # NetworkSessionState transitions, GameStarted/GameEnded, and a manual Raise() of the 3
    # leaderboard delegates. Console-only, single process, NetworkSessionType::Local (no real
    # networking) - needs CNA_GamerServices (SignedInGamer) and CNA_Net (NetworkSession).
    if(CNA_ENABLE_NET)
        add_executable(cna_demo_session_lifecycle_events
            examples/demo_session_lifecycle_events/src/Main.cpp
        )
        target_link_libraries(cna_demo_session_lifecycle_events PRIVATE CNA_GamerServices CNA_Net SHARP_RUNTIME)
    endif()

    # Task 15.8: cna_demo_gamerservices_signin_presence - real GamerServicesComponent
    # registration, Gamer::SignedInGamers population, SignedInGamer::SignedIn/SignedOut, and
    # GamerPresence. Needs CNA_GamerServices (SignedInGamer/GamerServicesComponent) and windowing
    # (SpriteBatch/SpriteFont HUD) - same gating as cna_demo_avatar above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_gamerservices_signin_presence
            examples/demo_gamerservices_signin_presence/src/Main.cpp
            examples/demo_gamerservices_signin_presence/src/PresenceGame.cpp
        )
        target_include_directories(cna_demo_gamerservices_signin_presence PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_gamerservices_signin_presence/src
        )
        target_link_libraries(cna_demo_gamerservices_signin_presence PRIVATE CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_gamerservices_signin_presence PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_gamerservices_signin_presence PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_gamerservices_signin_presence PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            set_target_properties(cna_demo_gamerservices_signin_presence PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_gamerservices_signin_presence)
        endif()
    endif()

    # Task 15.9: cna_demo_achievement_showcase - Achievement/AchievementCollection,
    # SignedInGamer::AwardAchievement/GetAchievements. Needs CNA_GamerServices and windowing
    # (SpriteBatch/SpriteFont HUD) - same gating as cna_demo_avatar above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_achievement_showcase
            examples/demo_achievement_showcase/src/Main.cpp
            examples/demo_achievement_showcase/src/AchievementGame.cpp
        )
        target_include_directories(cna_demo_achievement_showcase PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_achievement_showcase/src
        )
        target_link_libraries(cna_demo_achievement_showcase PRIVATE CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_achievement_showcase PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_achievement_showcase PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_achievement_showcase PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            set_target_properties(cna_demo_achievement_showcase PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_achievement_showcase)
        endif()
    endif()

    # Task 15.10: cna_demo_leaderboard_viewer - LeaderboardReader (CanPageUp/Down, Entries,
    # PageStart) plus LeaderboardWriter::GetLeaderboard's always-throws boundary. Needs
    # CNA_GamerServices and windowing (SpriteBatch/SpriteFont HUD) - same gating as
    # cna_demo_avatar above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_leaderboard_viewer
            examples/demo_leaderboard_viewer/src/Main.cpp
            examples/demo_leaderboard_viewer/src/LeaderboardGame.cpp
        )
        target_include_directories(cna_demo_leaderboard_viewer PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_leaderboard_viewer/src
        )
        target_link_libraries(cna_demo_leaderboard_viewer PRIVATE CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_leaderboard_viewer PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_leaderboard_viewer PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_leaderboard_viewer PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            set_target_properties(cna_demo_leaderboard_viewer PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_leaderboard_viewer)
        endif()
    endif()

    # Task 15.11: cna_demo_guide_overlay_console - the full Guide static API surface via a
    # numbered console menu. Console-only, single process, no graphics/window needed - needs only
    # CNA_GamerServices.
    if(CNA_ENABLE_NET)
        add_executable(cna_demo_guide_overlay_console
            examples/demo_guide_overlay_console/src/Main.cpp
        )
        target_link_libraries(cna_demo_guide_overlay_console PRIVATE CNA_GamerServices SHARP_RUNTIME)
    endif()

    # Task 15.12: cna_demo_gamerservices_dispatcher_watchdog - visual proof that Task 12.1's/Task
    # 7.1's historical GamerServicesDispatcher/NetworkSession/GetAchievements hangs are fixed.
    # Needs CNA_GamerServices and CNA_Net, plus windowing for the "waiting.../SUCCESS" HUD - same
    # gating as cna_demo_net_client_server_arena above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_gamerservices_dispatcher_watchdog
            examples/demo_gamerservices_dispatcher_watchdog/src/Main.cpp
            examples/demo_gamerservices_dispatcher_watchdog/src/WatchdogGame.cpp
        )
        target_include_directories(cna_demo_gamerservices_dispatcher_watchdog PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_gamerservices_dispatcher_watchdog/src
        )
        target_link_libraries(cna_demo_gamerservices_dispatcher_watchdog PRIVATE CNA_GamerServices CNA_Net)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_gamerservices_dispatcher_watchdog PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_gamerservices_dispatcher_watchdog PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_gamerservices_dispatcher_watchdog PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            set_target_properties(cna_demo_gamerservices_dispatcher_watchdog PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_gamerservices_dispatcher_watchdog)
        endif()
    endif()

    # Task 15.13: cna_demo_gamer_profile_privileges - GamerProfile/GamerPrivileges via
    # Gamer::GetProfile()/SignedInGamer::getPrivilegesProperty(). Needs CNA_GamerServices and
    # windowing - same gating as cna_demo_avatar above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_gamer_profile_privileges
            examples/demo_gamer_profile_privileges/src/Main.cpp
            examples/demo_gamer_profile_privileges/src/ProfileGame.cpp
        )
        target_include_directories(cna_demo_gamer_profile_privileges PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_gamer_profile_privileges/src
        )
        target_link_libraries(cna_demo_gamer_profile_privileges PRIVATE CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_gamer_profile_privileges PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_gamer_profile_privileges PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_gamer_profile_privileges PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            set_target_properties(cna_demo_gamer_profile_privileges PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_gamer_profile_privileges)
        endif()
    endif()

    # Task 15.14: cna_demo_friends_and_gamercard - FriendCollection (via CreateInternal) and the
    # no-op Guide::ShowGamerCard/ShowFriendRequest/ShowFriends/ShowComposeMessage calls. Needs
    # CNA_GamerServices and windowing - same gating as cna_demo_avatar above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_friends_and_gamercard
            examples/demo_friends_and_gamercard/src/Main.cpp
            examples/demo_friends_and_gamercard/src/FriendsGame.cpp
        )
        target_include_directories(cna_demo_friends_and_gamercard PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_friends_and_gamercard/src
        )
        target_link_libraries(cna_demo_friends_and_gamercard PRIVATE CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_friends_and_gamercard PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_friends_and_gamercard PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_friends_and_gamercard PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            set_target_properties(cna_demo_friends_and_gamercard PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_friends_and_gamercard)
        endif()
    endif()

    # Task 15.15: cna_demo_avatar_animation_gallery - a completionist version of demo_avatar's
    # Space-cycling, iterating all 31 AvatarAnimationPreset values across both genders. Reuses
    # demo_avatar's Content/ directory (copied by CMake, not duplicated) - same gating as
    # cna_demo_avatar above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_avatar_animation_gallery
            examples/demo_avatar_animation_gallery/src/Main.cpp
            examples/demo_avatar_animation_gallery/src/GalleryDemo.cpp
        )
        target_include_directories(cna_demo_avatar_animation_gallery PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_avatar_animation_gallery/src
        )
        target_link_libraries(cna_demo_avatar_animation_gallery PRIVATE CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_avatar_animation_gallery PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_avatar_animation_gallery PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_avatar_animation_gallery PRIVATE SDL3::SDL3main)
        endif()
        add_custom_command(TARGET cna_demo_avatar_animation_gallery POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_avatar/Content"
                "$<TARGET_FILE_DIR:cna_demo_avatar_animation_gallery>/Content"
            COMMENT "Copying demo_avatar's Content next to cna_demo_avatar_animation_gallery executable")
        if(WIN32)
            set_target_properties(cna_demo_avatar_animation_gallery PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_avatar_animation_gallery)
        endif()
    endif()

    # Task 15.16: cna_demo_avatar_wardrobe_hotswap - SkinnedModelEXT::AttachPartEXT/RemovePartEXT
    # used repeatedly at runtime (Tab cycles baked-in hair / hair_Cap / hair_Ponytail). Reuses
    # demo_avatar's Content/ directory (copied by CMake, not duplicated) - same gating as
    # cna_demo_avatar above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_avatar_wardrobe_hotswap
            examples/demo_avatar_wardrobe_hotswap/src/Main.cpp
            examples/demo_avatar_wardrobe_hotswap/src/HotswapDemo.cpp
        )
        target_include_directories(cna_demo_avatar_wardrobe_hotswap PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_avatar_wardrobe_hotswap/src
        )
        target_link_libraries(cna_demo_avatar_wardrobe_hotswap PRIVATE CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_avatar_wardrobe_hotswap PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_avatar_wardrobe_hotswap PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_avatar_wardrobe_hotswap PRIVATE SDL3::SDL3main)
        endif()
        add_custom_command(TARGET cna_demo_avatar_wardrobe_hotswap POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_avatar/Content"
                "$<TARGET_FILE_DIR:cna_demo_avatar_wardrobe_hotswap>/Content"
            COMMENT "Copying demo_avatar's Content next to cna_demo_avatar_wardrobe_hotswap executable")
        if(WIN32)
            set_target_properties(cna_demo_avatar_wardrobe_hotswap PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_avatar_wardrobe_hotswap)
        endif()
    endif()

    # Task 15.17: cna_demo_avatar_appearance_tint_studio - AvatarAppearanceEXT's 5 tint slots and
    # AvatarRenderer::SetAppearanceEXT as a live color customization screen. Reuses demo_avatar's
    # Content/ directory (copied by CMake, not duplicated) - same gating as cna_demo_avatar above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_avatar_appearance_tint_studio
            examples/demo_avatar_appearance_tint_studio/src/Main.cpp
            examples/demo_avatar_appearance_tint_studio/src/TintStudioDemo.cpp
        )
        target_include_directories(cna_demo_avatar_appearance_tint_studio PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_avatar_appearance_tint_studio/src
        )
        target_link_libraries(cna_demo_avatar_appearance_tint_studio PRIVATE CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_avatar_appearance_tint_studio PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_avatar_appearance_tint_studio PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_avatar_appearance_tint_studio PRIVATE SDL3::SDL3main)
        endif()
        add_custom_command(TARGET cna_demo_avatar_appearance_tint_studio POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_avatar/Content"
                "$<TARGET_FILE_DIR:cna_demo_avatar_appearance_tint_studio>/Content"
            COMMENT "Copying demo_avatar's Content next to cna_demo_avatar_appearance_tint_studio executable")
        if(WIN32)
            set_target_properties(cna_demo_avatar_appearance_tint_studio PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_avatar_appearance_tint_studio)
        endif()
    endif()

    # Task 15.18: cna_demo_avatar_dual_compare - two independent AvatarRenderer/SkinnedModelEXT
    # instances alive and drawing simultaneously. Reuses demo_avatar's Content/ directory (copied
    # by CMake, not duplicated) - same gating as cna_demo_avatar above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_avatar_dual_compare
            examples/demo_avatar_dual_compare/src/Main.cpp
            examples/demo_avatar_dual_compare/src/DualCompareDemo.cpp
        )
        target_include_directories(cna_demo_avatar_dual_compare PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_avatar_dual_compare/src
        )
        target_link_libraries(cna_demo_avatar_dual_compare PRIVATE CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_avatar_dual_compare PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_avatar_dual_compare PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_avatar_dual_compare PRIVATE SDL3::SDL3main)
        endif()
        add_custom_command(TARGET cna_demo_avatar_dual_compare POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_avatar/Content"
                "$<TARGET_FILE_DIR:cna_demo_avatar_dual_compare>/Content"
            COMMENT "Copying demo_avatar's Content next to cna_demo_avatar_dual_compare executable")
        if(WIN32)
            set_target_properties(cna_demo_avatar_dual_compare PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_avatar_dual_compare)
        endif()
    endif()

    # Task 15.19: cna_demo_avatar_multi_attach_stress - SkinnedModelEXT::AttachPartEXT called
    # repeatedly (hair variants, then a growing series of synthetic quad accessories), proving
    # accumulation doesn't break skinning/tinting as Parts.size() grows. Reuses demo_avatar's
    # Content/ directory (copied by CMake, not duplicated) - same gating as cna_demo_avatar above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_avatar_multi_attach_stress
            examples/demo_avatar_multi_attach_stress/src/Main.cpp
            examples/demo_avatar_multi_attach_stress/src/StressDemo.cpp
        )
        target_include_directories(cna_demo_avatar_multi_attach_stress PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_avatar_multi_attach_stress/src
        )
        target_link_libraries(cna_demo_avatar_multi_attach_stress PRIVATE CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_avatar_multi_attach_stress PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_avatar_multi_attach_stress PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_avatar_multi_attach_stress PRIVATE SDL3::SDL3main)
        endif()
        add_custom_command(TARGET cna_demo_avatar_multi_attach_stress POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_avatar/Content"
                "$<TARGET_FILE_DIR:cna_demo_avatar_multi_attach_stress>/Content"
            COMMENT "Copying demo_avatar's Content next to cna_demo_avatar_multi_attach_stress executable")
        if(WIN32)
            set_target_properties(cna_demo_avatar_multi_attach_stress PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_avatar_multi_attach_stress)
        endif()
    endif()

    # Task 15.20: cna_demo_avatar_bone_state_boundary - documents the real, verified
    # AvatarRenderer skeleton-API boundary (State/ParentBones/BindPose) vs the working
    # SkinnedModelEXT EXT path. Reuses demo_avatar's Content/ directory (copied by CMake, not
    # duplicated) - same gating as cna_demo_avatar above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_avatar_bone_state_boundary
            examples/demo_avatar_bone_state_boundary/src/Main.cpp
            examples/demo_avatar_bone_state_boundary/src/BoundaryDemo.cpp
        )
        target_include_directories(cna_demo_avatar_bone_state_boundary PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_avatar_bone_state_boundary/src
        )
        target_link_libraries(cna_demo_avatar_bone_state_boundary PRIVATE CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_avatar_bone_state_boundary PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_avatar_bone_state_boundary PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_avatar_bone_state_boundary PRIVATE SDL3::SDL3main)
        endif()
        add_custom_command(TARGET cna_demo_avatar_bone_state_boundary POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_avatar/Content"
                "$<TARGET_FILE_DIR:cna_demo_avatar_bone_state_boundary>/Content"
            COMMENT "Copying demo_avatar's Content next to cna_demo_avatar_bone_state_boundary executable")
        if(WIN32)
            set_target_properties(cna_demo_avatar_bone_state_boundary PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_avatar_bone_state_boundary)
        endif()
    endif()

    # Task 15.21 (bonus, cross-cutting): cna_demo_net_avatar_sync - combines Net + Avatar over a
    # real two-process ENet session, each side rendering both its own and the remote peer's real
    # avatar. Needs CNA_GamerServices/CNA_Net (real networking) plus demo_avatar's Content/
    # directory (copied by CMake, not duplicated) - same gating as cna_demo_net_client_server_arena
    # above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_net_avatar_sync
            examples/demo_net_avatar_sync/src/Main.cpp
            examples/demo_net_avatar_sync/src/SyncGame.cpp
        )
        target_include_directories(cna_demo_net_avatar_sync PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_net_avatar_sync/src
        )
        target_link_libraries(cna_demo_net_avatar_sync PRIVATE CNA_GamerServices CNA_Net)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_net_avatar_sync PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_net_avatar_sync PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_net_avatar_sync PRIVATE SDL3::SDL3main)
        endif()
        add_custom_command(TARGET cna_demo_net_avatar_sync POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_avatar/Content"
                "$<TARGET_FILE_DIR:cna_demo_net_avatar_sync>/Content"
            COMMENT "Copying demo_avatar's Content next to cna_demo_net_avatar_sync executable")
        if(WIN32)
            set_target_properties(cna_demo_net_avatar_sync PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_net_avatar_sync)
        endif()
    endif()

    # EasyGL integration tests (Task 85, 86, 87)
    if(CNA_BUILD_TESTS AND NOT EMSCRIPTEN AND NOT WIN32
       AND CNA_GRAPHICS_BACKEND STREQUAL "EASYGL")

        # --- helper macro: build a headless EasyGL test exe --------------------
        macro(cna_easygl_test target src)
            add_executable(${target} ${src})
            if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
                target_link_libraries(${target} PRIVATE
                    -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                    SHARP_RUNTIME)
            else()
                target_link_libraries(${target} PRIVATE CNA SHARP_RUNTIME)
            endif()
            if(TARGET SDL3::SDL3main)
                target_link_libraries(${target} PRIVATE SDL3::SDL3main)
            endif()
        endmacro()

        enable_testing()

        # Task 85: 3-frame smoke test
        add_test(NAME EasyGL_House3D_SmokeTest
                 COMMAND cna_house3d_demo --smoke 3)
        set_tests_properties(EasyGL_House3D_SmokeTest PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}"
            LABELS GraphicsSmoke)

        # Task 86: textured quad pixel readback
        cna_easygl_test(cna_test_easygl_textured_quad
                        examples/easygl_textured_quad_test.cpp)
        add_test(NAME EasyGL_TexturedQuad_Readback
                 COMMAND cna_test_easygl_textured_quad)
        set_tests_properties(EasyGL_TexturedQuad_Readback PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 461: canary proving the new shared PixelTestGame helper (examples/common/) works
        cna_easygl_test(cna_test_easygl_pixeltestgame_smoke
                        examples/easygl_pixeltestgame_smoke_test.cpp)
        add_test(NAME EasyGL_PixelTestGame_Smoke
                 COMMAND cna_test_easygl_pixeltestgame_smoke)
        set_tests_properties(EasyGL_PixelTestGame_Smoke PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 463: canary proving PixelTestGame::CompareGoldenImage() (examples/common/) works.
        # WORKING_DIRECTORY is set to the repo root so the test's relative
        # "examples/golden/*.png" path resolves the same way regardless of which
        # cmake-build-* directory this is run from (CMake's default test working directory is
        # CMAKE_CURRENT_BINARY_DIR, not the source tree).
        cna_easygl_test(cna_test_easygl_goldenimage_smoke
                        examples/easygl_goldenimage_smoke_test.cpp)
        add_test(NAME EasyGL_GoldenImage_Smoke
                 COMMAND cna_test_easygl_goldenimage_smoke)
        set_tests_properties(EasyGL_GoldenImage_Smoke PROPERTIES
            TIMEOUT 30
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 464: first real (non-canary) golden-image case, reusing Phase 42's already-
        # verified BasicEffect capstone scene (Task 370).
        cna_easygl_test(cna_test_easygl_basiceffect_golden
                        examples/easygl_basiceffect_golden_test.cpp)
        add_test(NAME EasyGL_BasicEffect_Golden
                 COMMAND cna_test_easygl_basiceffect_golden)
        set_tests_properties(EasyGL_BasicEffect_Golden PROPERTIES
            TIMEOUT 30
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 465: golden-image case reusing Phase 47's already-verified SpriteBatch
        # rotation-around-origin scene (Task 417).
        cna_easygl_test(cna_test_easygl_spritebatch_rotation_golden
                        examples/easygl_spritebatch_rotation_golden_test.cpp)
        add_test(NAME EasyGL_SpriteBatch_Rotation_Golden
                 COMMAND cna_test_easygl_spritebatch_rotation_golden)
        set_tests_properties(EasyGL_SpriteBatch_Rotation_Golden PROPERTIES
            TIMEOUT 30
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 466: golden-image case reusing Phase 35's already-verified texture-filter
        # magnification/Linear scene (Task 297).
        cna_easygl_test(cna_test_easygl_texture_filter_linear_golden
                        examples/easygl_texture_filter_linear_golden_test.cpp)
        add_test(NAME EasyGL_TextureFilter_Linear_Golden
                 COMMAND cna_test_easygl_texture_filter_linear_golden)
        set_tests_properties(EasyGL_TextureFilter_Linear_Golden PROPERTIES
            TIMEOUT 30
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 467: golden-image case reusing Phase 36's already-verified BlendState::Additive
        # scene (Task 306).
        cna_easygl_test(cna_test_easygl_blendstate_additive_golden
                        examples/easygl_blendstate_additive_golden_test.cpp)
        add_test(NAME EasyGL_BlendState_Additive_Golden
                 COMMAND cna_test_easygl_blendstate_additive_golden)
        set_tests_properties(EasyGL_BlendState_Additive_Golden PROPERTIES
            TIMEOUT 30
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 468: golden-image cases reusing Phase 37's DepthStencilState write-enable scene
        # (Task 313) and Phase 38's RasterizerState.CullMode scene (Task 323) -- two separate
        # files, one per phase, since no existing test combines both areas.
        cna_easygl_test(cna_test_easygl_depthstencilstate_write_enable_golden
                        examples/easygl_depthstencilstate_write_enable_golden_test.cpp)
        add_test(NAME EasyGL_DepthStencilState_WriteEnable_Golden
                 COMMAND cna_test_easygl_depthstencilstate_write_enable_golden)
        set_tests_properties(EasyGL_DepthStencilState_WriteEnable_Golden PROPERTIES
            TIMEOUT 30
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_rasterizerstate_cullmode_golden
                        examples/easygl_rasterizerstate_cullmode_golden_test.cpp)
        add_test(NAME EasyGL_RasterizerState_CullMode_Golden
                 COMMAND cna_test_easygl_rasterizerstate_cullmode_golden)
        set_tests_properties(EasyGL_RasterizerState_CullMode_Golden PROPERTIES
            TIMEOUT 30
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 469: golden-image cases reusing Phases 43-46's already-verified stock-effect
        # scenes (AlphaTestEffect/Task 373, DualTextureEffect/Task 389, EnvironmentMapEffect/
        # Task 399, SkinnedEffect/Task 409) -- 4 separate files, one per phase.
        cna_easygl_test(cna_test_easygl_alphatesteffect_golden
                        examples/easygl_alphatesteffect_golden_test.cpp)
        add_test(NAME EasyGL_AlphaTestEffect_Golden
                 COMMAND cna_test_easygl_alphatesteffect_golden)
        set_tests_properties(EasyGL_AlphaTestEffect_Golden PROPERTIES
            TIMEOUT 30
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_dualtextureeffect_golden
                        examples/easygl_dualtextureeffect_golden_test.cpp)
        add_test(NAME EasyGL_DualTextureEffect_Golden
                 COMMAND cna_test_easygl_dualtextureeffect_golden)
        set_tests_properties(EasyGL_DualTextureEffect_Golden PROPERTIES
            TIMEOUT 30
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_environmentmapeffect_golden
                        examples/easygl_environmentmapeffect_golden_test.cpp)
        add_test(NAME EasyGL_EnvironmentMapEffect_Golden
                 COMMAND cna_test_easygl_environmentmapeffect_golden)
        set_tests_properties(EasyGL_EnvironmentMapEffect_Golden PROPERTIES
            TIMEOUT 30
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_skinnedeffect_golden
                        examples/easygl_skinnedeffect_golden_test.cpp)
        add_test(NAME EasyGL_SkinnedEffect_Golden
                 COMMAND cna_test_easygl_skinnedeffect_golden)
        set_tests_properties(EasyGL_SkinnedEffect_Golden PROPERTIES
            TIMEOUT 30
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 87: RenderTarget2D → texture → readback
        cna_easygl_test(cna_test_easygl_render_target
                        examples/easygl_render_target_test.cpp)
        add_test(NAME EasyGL_RenderTarget2D_Readback
                 COMMAND cna_test_easygl_render_target)
        set_tests_properties(EasyGL_RenderTarget2D_Readback PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 335: RenderTarget2D depth buffer is real and functional (not just a stored property)
        cna_easygl_test(cna_test_easygl_rendertarget2d_depth
                        examples/rendertarget2d_depth_test.cpp)
        add_test(NAME EasyGL_RenderTarget2D_DepthBuffer
                 COMMAND cna_test_easygl_rendertarget2d_depth)
        set_tests_properties(EasyGL_RenderTarget2D_DepthBuffer PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 336: RenderTarget2D mip chain is genuinely GL-complete after auto-generation on unbind
        cna_easygl_test(cna_test_easygl_rendertarget2d_mip
                        examples/easygl_rendertarget2d_mip_test.cpp)
        add_test(NAME EasyGL_RenderTarget2D_MipComplete
                 COMMAND cna_test_easygl_rendertarget2d_mip)
        set_tests_properties(EasyGL_RenderTarget2D_MipComplete PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 337: RenderTarget2D MSAA resolve genuinely anti-aliases (not just solid-fill safe)
        cna_easygl_test(cna_test_easygl_rendertarget2d_msaa
                        examples/easygl_rendertarget2d_msaa_test.cpp)
        add_test(NAME EasyGL_RenderTarget2D_MsaaResolve
                 COMMAND cna_test_easygl_rendertarget2d_msaa)
        set_tests_properties(EasyGL_RenderTarget2D_MsaaResolve PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 338: SetRenderTarget(nullptr) resets Viewport/ScissorRectangle to the backbuffer
        cna_easygl_test(cna_test_easygl_rendertarget_viewport_scissor_reset
                        examples/rendertarget_viewport_scissor_reset_test.cpp)
        add_test(NAME EasyGL_RenderTarget_ViewportScissorReset
                 COMMAND cna_test_easygl_rendertarget_viewport_scissor_reset)
        set_tests_properties(EasyGL_RenderTarget_ViewportScissorReset PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 22: BasicEffect property tests
        cna_easygl_test(cna_test_basic_effect
                        examples/basic_effect_test.cpp)
        add_test(NAME EasyGL_BasicEffect_Properties
                 COMMAND cna_test_basic_effect)
        set_tests_properties(EasyGL_BasicEffect_Properties PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 23: AlphaTestEffect property tests
        cna_easygl_test(cna_test_alpha_test_effect
                        examples/alpha_test_effect_test.cpp)
        add_test(NAME EasyGL_AlphaTestEffect_Properties
                 COMMAND cna_test_alpha_test_effect)
        set_tests_properties(EasyGL_AlphaTestEffect_Properties PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 24: SkinnedEffect property tests
        cna_easygl_test(cna_test_skinned_effect
                        examples/skinned_effect_test.cpp)
        add_test(NAME EasyGL_SkinnedEffect_Properties
                 COMMAND cna_test_skinned_effect)
        set_tests_properties(EasyGL_SkinnedEffect_Properties PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 27: SpriteFont property and MeasureString tests
        cna_easygl_test(cna_test_sprite_font
                        examples/sprite_font_test.cpp)
        add_test(NAME EasyGL_SpriteFont_Properties
                 COMMAND cna_test_sprite_font)
        set_tests_properties(EasyGL_SpriteFont_Properties PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 35: OcclusionQuery begin/end/IsComplete integration test
        cna_easygl_test(cna_test_occlusion_query
                        examples/occlusion_query_test.cpp)
        add_test(NAME EasyGL_OcclusionQuery_Cycle
                 COMMAND cna_test_occlusion_query)
        set_tests_properties(EasyGL_OcclusionQuery_Cycle PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 445: Pixel/query test -- fully visible quad returns a positive occlusion-query pixel count.
        cna_easygl_test(cna_test_easygl_occlusion_query_visible_quad
                        examples/easygl_occlusion_query_visible_quad_test.cpp)
        add_test(NAME EasyGL_OcclusionQuery_VisibleQuad
                 COMMAND cna_test_easygl_occlusion_query_visible_quad)
        set_tests_properties(EasyGL_OcclusionQuery_VisibleQuad PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 446: Pixel/query test -- fully occluded quad returns a zero occlusion-query pixel count.
        cna_easygl_test(cna_test_easygl_occlusion_query_occluded_quad
                        examples/easygl_occlusion_query_occluded_quad_test.cpp)
        add_test(NAME EasyGL_OcclusionQuery_OccludedQuad
                 COMMAND cna_test_easygl_occlusion_query_occluded_quad)
        set_tests_properties(EasyGL_OcclusionQuery_OccludedQuad PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 122: AlphaTestEffect GPU alpha cutout + pixel readback
        cna_easygl_test(cna_test_alpha_test_integration
                        examples/alpha_test_integration_test.cpp)
        add_test(NAME EasyGL_AlphaTestEffect_AlphaCutout
                 COMMAND cna_test_alpha_test_integration)
        set_tests_properties(EasyGL_AlphaTestEffect_AlphaCutout PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 123: SkinnedEffect 2-bone GPU transform + mesh deformation
        cna_easygl_test(cna_test_skinned_integration
                        examples/skinned_effect_integration_test.cpp)
        add_test(NAME EasyGL_SkinnedEffect_BoneDeformation
                 COMMAND cna_test_skinned_integration)
        set_tests_properties(EasyGL_SkinnedEffect_BoneDeformation PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 10.14: AvatarRenderer::EnableRealRenderingEXT/DrawRealEXT real GPU-skinned
        # rendering (Phase 10 — Avatar real-rendering NOXNA/EXT extension). Needs
        # CNA_GamerServices (AvatarRenderer lives there), so it's only built when networking
        # (and thus GamerServices) is enabled.
        if(CNA_ENABLE_NET)
            cna_easygl_test(cna_test_avatar_real_render
                            examples/avatar_real_render_integration_test.cpp)
            target_link_libraries(cna_test_avatar_real_render PRIVATE CNA_GamerServices)
            add_test(NAME EasyGL_AvatarRenderer_RealRender
                     COMMAND cna_test_avatar_real_render)
            set_tests_properties(EasyGL_AvatarRenderer_RealRender PROPERTIES
                TIMEOUT 30
                ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

            # Task 11.21: SkinnedModelEXT::AttachPartEXT runtime wardrobe-attach real render.
            cna_easygl_test(cna_test_avatar_attach_part
                            examples/avatar_attach_part_integration_test.cpp)
            target_link_libraries(cna_test_avatar_attach_part PRIVATE CNA_GamerServices)
            add_test(NAME EasyGL_AvatarRenderer_AttachPart
                     COMMAND cna_test_avatar_attach_part)
            set_tests_properties(EasyGL_AvatarRenderer_AttachPart PROPERTIES
                TIMEOUT 30
                ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

            # Task 11.24: AvatarRenderer::PartTintEXT per-part tint routing regression test.
            cna_easygl_test(cna_test_avatar_tint_routing
                            examples/avatar_tint_routing_integration_test.cpp)
            target_link_libraries(cna_test_avatar_tint_routing PRIVATE CNA_GamerServices)
            add_test(NAME EasyGL_AvatarRenderer_TintRouting
                     COMMAND cna_test_avatar_tint_routing)
            set_tests_properties(EasyGL_AvatarRenderer_TintRouting PROPERTIES
                TIMEOUT 30
                ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
        endif()

        # Task 125: DXT1 texture loaded via FromStream, rendered, pixel readback
        cna_easygl_test(cna_test_dxt1_texture
                        examples/dxt1_texture_test.cpp)
        add_test(NAME EasyGL_DXT1_FromStream_Readback
                 COMMAND cna_test_dxt1_texture)
        set_tests_properties(EasyGL_DXT1_FromStream_Readback PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 132: ShaderEffect (GLSL) with SpriteBatch — red-tint over green background
        cna_easygl_test(cna_test_easygl_shader_effect
                        examples/easygl_shader_effect_test.cpp)
        add_test(NAME EasyGL_ShaderEffect_GLSL
                 COMMAND cna_test_easygl_shader_effect)
        set_tests_properties(EasyGL_ShaderEffect_GLSL PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 133: DualTextureEffect — magenta×yellow→red pixel readback
        cna_easygl_test(cna_test_easygl_dual_texture
                        examples/easygl_dual_texture_test.cpp)
        add_test(NAME EasyGL_DualTextureEffect_Blend
                 COMMAND cna_test_easygl_dual_texture)
        set_tests_properties(EasyGL_DualTextureEffect_Blend PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 134: EnvironmentMapEffect — emissive red, envAmount=0, pixel readback
        cna_easygl_test(cna_test_easygl_env_map
                        examples/easygl_env_map_test.cpp)
        add_test(NAME EasyGL_EnvironmentMapEffect_Readback
                 COMMAND cna_test_easygl_env_map)
        set_tests_properties(EasyGL_EnvironmentMapEffect_Readback PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 393: EnvironmentMapEffect EnvironmentMapAmount=0 should ignore the cube map
        cna_easygl_test(cna_test_easygl_environmentmapeffect_amount_zero
                        examples/easygl_environmentmapeffect_amount_zero_test.cpp)
        add_test(NAME EasyGL_EnvironmentMapEffect_AmountZero
                 COMMAND cna_test_easygl_environmentmapeffect_amount_zero)
        set_tests_properties(EasyGL_EnvironmentMapEffect_AmountZero PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 900: EnvironmentMapEffect fog fix verification
        cna_easygl_test(cna_test_easygl_environmentmapeffect_fog
                        examples/easygl_environmentmapeffect_fog_test.cpp)
        add_test(NAME EasyGL_EnvironmentMapEffect_Fog
                 COMMAND cna_test_easygl_environmentmapeffect_fog)
        set_tests_properties(EasyGL_EnvironmentMapEffect_Fog PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 394: EnvironmentMapEffect EnvironmentMapAmount=1 should fully replace lit color
        cna_easygl_test(cna_test_easygl_environmentmapeffect_amount_one
                        examples/easygl_environmentmapeffect_amount_one_test.cpp)
        add_test(NAME EasyGL_EnvironmentMapEffect_AmountOne
                 COMMAND cna_test_easygl_environmentmapeffect_amount_one)
        set_tests_properties(EasyGL_EnvironmentMapEffect_AmountOne PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 395: EnvironmentMapEffect EnvironmentMapSpecular should scale by cubemap alpha
        cna_easygl_test(cna_test_easygl_environmentmapeffect_specular
                        examples/easygl_environmentmapeffect_specular_test.cpp)
        add_test(NAME EasyGL_EnvironmentMapEffect_Specular
                 COMMAND cna_test_easygl_environmentmapeffect_specular)
        set_tests_properties(EasyGL_EnvironmentMapEffect_Specular PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 396: EnvironmentMapEffect Fresnel edge-weighting should suppress head-on reflections
        cna_easygl_test(cna_test_easygl_environmentmapeffect_fresnel
                        examples/easygl_environmentmapeffect_fresnel_test.cpp)
        add_test(NAME EasyGL_EnvironmentMapEffect_Fresnel
                 COMMAND cna_test_easygl_environmentmapeffect_fresnel)
        set_tests_properties(EasyGL_EnvironmentMapEffect_Fresnel PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 397: EnvironmentMapEffect reflection vector should respond to EyePosition
        cna_easygl_test(cna_test_easygl_environmentmapeffect_eyeposition
                        examples/easygl_environmentmapeffect_eyeposition_test.cpp)
        add_test(NAME EasyGL_EnvironmentMapEffect_EyePosition
                 COMMAND cna_test_easygl_environmentmapeffect_eyeposition)
        set_tests_properties(EasyGL_EnvironmentMapEffect_EyePosition PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 398: EnvironmentMapEffect normal transform should use inverse-transpose of World
        cna_easygl_test(cna_test_easygl_environmentmapeffect_worldtransform
                        examples/easygl_environmentmapeffect_worldtransform_test.cpp)
        add_test(NAME EasyGL_EnvironmentMapEffect_WorldTransform
                 COMMAND cna_test_easygl_environmentmapeffect_worldtransform)
        set_tests_properties(EasyGL_EnvironmentMapEffect_WorldTransform PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 399: cross-backend EnvironmentMapEffect capstone (Tasks 393-398 combined)
        cna_easygl_test(cna_test_easygl_environmentmapeffect_combined
                        examples/easygl_environmentmapeffect_combined_test.cpp)
        add_test(NAME EasyGL_EnvironmentMapEffect_Combined
                 COMMAND cna_test_easygl_environmentmapeffect_combined)
        set_tests_properties(EasyGL_EnvironmentMapEffect_Combined PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 900: SkinnedEffect fog fix verification
        cna_easygl_test(cna_test_easygl_skinnedeffect_fog
                        examples/easygl_skinnedeffect_fog_test.cpp)
        add_test(NAME EasyGL_SkinnedEffect_Fog
                 COMMAND cna_test_easygl_skinnedeffect_fog)
        set_tests_properties(EasyGL_SkinnedEffect_Fog PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 406: SkinnedEffect identity bone palette should produce zero deformation
        cna_easygl_test(cna_test_easygl_skinnedeffect_identity_bones
                        examples/easygl_skinnedeffect_identity_bones_test.cpp)
        add_test(NAME EasyGL_SkinnedEffect_IdentityBones
                 COMMAND cna_test_easygl_skinnedeffect_identity_bones)
        set_tests_properties(EasyGL_SkinnedEffect_IdentityBones PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 407: SkinnedEffect single translation bone should shift the mesh
        cna_easygl_test(cna_test_easygl_skinnedeffect_translation_bone
                        examples/easygl_skinnedeffect_translation_bone_test.cpp)
        add_test(NAME EasyGL_SkinnedEffect_TranslationBone
                 COMMAND cna_test_easygl_skinnedeffect_translation_bone)
        set_tests_properties(EasyGL_SkinnedEffect_TranslationBone PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 408: SkinnedEffect two-bone weighted blend should produce midpoint deformation
        cna_easygl_test(cna_test_easygl_skinnedeffect_twobone_blend
                        examples/easygl_skinnedeffect_twobone_blend_test.cpp)
        add_test(NAME EasyGL_SkinnedEffect_TwoBoneBlend
                 COMMAND cna_test_easygl_skinnedeffect_twobone_blend)
        set_tests_properties(EasyGL_SkinnedEffect_TwoBoneBlend PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 409: cross-backend SkinnedEffect capstone (identity + single-bone + two-bone blend)
        cna_easygl_test(cna_test_easygl_skinnedeffect_combined
                        examples/easygl_skinnedeffect_combined_test.cpp)
        add_test(NAME EasyGL_SkinnedEffect_Combined
                 COMMAND cna_test_easygl_skinnedeffect_combined)
        set_tests_properties(EasyGL_SkinnedEffect_Combined PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 334: RenderTargetCube sampled as TextureCube via EnvironmentMapEffect after unbinding
        cna_easygl_test(cna_test_easygl_rendertargetcube_sample
                        examples/easygl_rendertargetcube_sample_test.cpp)
        add_test(NAME EasyGL_RenderTargetCube_SampleAfterUnbind
                 COMMAND cna_test_easygl_rendertargetcube_sample)
        set_tests_properties(EasyGL_RenderTargetCube_SampleAfterUnbind PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 877: RenderTargetCube honors its exact requested DepthFormat (None vs Depth24Stencil8)
        cna_easygl_test(cna_test_easygl_rendertargetcube_depthformat
                        examples/easygl_rendertargetcube_depthformat_test.cpp)
        add_test(NAME EasyGL_RenderTargetCube_DepthFormat
                 COMMAND cna_test_easygl_rendertargetcube_depthformat)
        set_tests_properties(EasyGL_RenderTargetCube_DepthFormat PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 144: Model.Draw — 2-bone model, BasicEffect VertexColor, red quad, pixel readback
        cna_easygl_test(cna_test_easygl_model_draw
                        examples/easygl_model_draw_test.cpp)
        add_test(NAME EasyGL_ModelDraw_RedQuad
                 COMMAND cna_test_easygl_model_draw)
        set_tests_properties(EasyGL_ModelDraw_RedQuad PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 438: Pixel test -- Model with two meshes, each using its own distinct Effect.
        cna_easygl_test(cna_test_easygl_model_two_meshes_effects
                        examples/easygl_model_two_meshes_effects_test.cpp)
        add_test(NAME EasyGL_Model_TwoMeshesEffects
                 COMMAND cna_test_easygl_model_two_meshes_effects)
        set_tests_properties(EasyGL_Model_TwoMeshesEffects PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 439: Pixel test -- a child mesh's ParentBone's absolute transform actually moves it.
        cna_easygl_test(cna_test_easygl_model_hierarchy_child_mesh
                        examples/easygl_model_hierarchy_child_mesh_test.cpp)
        add_test(NAME EasyGL_Model_HierarchyChildMesh
                 COMMAND cna_test_easygl_model_hierarchy_child_mesh)
        set_tests_properties(EasyGL_Model_HierarchyChildMesh PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 145: MRT — SetRenderTargets({rt0,rt1}), draw green to attachment0, verify left=green right=blue
        cna_easygl_test(cna_test_easygl_mrt
                        examples/easygl_mrt_test.cpp)
        add_test(NAME EasyGL_MRT_TwoAttachments
                 COMMAND cna_test_easygl_mrt)
        set_tests_properties(EasyGL_MRT_TwoAttachments PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 146: MSAA 4× — multisampled renderbuffer + resolve on Present
        cna_easygl_test(cna_test_easygl_msaa
                        examples/easygl_msaa_test.cpp)
        add_test(NAME EasyGL_MSAA_4x_Readback
                 COMMAND cna_test_easygl_msaa)
        set_tests_properties(EasyGL_MSAA_4x_Readback PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 167: SpriteEffects — FlipHorizontally, FlipVertically, FlipHV pixel readback
        cna_easygl_test(cna_test_easygl_sprite_effects
                        examples/easygl_sprite_effects_test.cpp)
        add_test(NAME EasyGL_SpriteEffects_Flip
                 COMMAND cna_test_easygl_sprite_effects)
        set_tests_properties(EasyGL_SpriteEffects_Flip PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 417: SpriteBatch Draw rotation must pivot around the specified origin
        cna_easygl_test(cna_test_easygl_spritebatch_rotation
                        examples/easygl_spritebatch_rotation_test.cpp)
        add_test(NAME EasyGL_SpriteBatch_RotationAroundOrigin
                 COMMAND cna_test_easygl_spritebatch_rotation)
        set_tests_properties(EasyGL_SpriteBatch_RotationAroundOrigin PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 418: SpriteBatch Draw scalar/Vector2 scale overloads produce expected size
        cna_easygl_test(cna_test_easygl_spritebatch_scale
                        examples/easygl_spritebatch_scale_test.cpp)
        add_test(NAME EasyGL_SpriteBatch_ScaleOverloads
                 COMMAND cna_test_easygl_spritebatch_scale)
        set_tests_properties(EasyGL_SpriteBatch_ScaleOverloads PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 419: SpriteBatch Draw sourceRectangle must crop, not stretch the whole texture
        cna_easygl_test(cna_test_easygl_spritebatch_sourcerect
                        examples/easygl_spritebatch_sourcerect_test.cpp)
        add_test(NAME EasyGL_SpriteBatch_SourceRectangleCropping
                 COMMAND cna_test_easygl_spritebatch_sourcerect)
        set_tests_properties(EasyGL_SpriteBatch_SourceRectangleCropping PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 420: layerDepth must affect on-screen draw order under FrontToBack
        cna_easygl_test(cna_test_easygl_spritebatch_layerdepth
                        examples/easygl_spritebatch_layerdepth_test.cpp)
        add_test(NAME EasyGL_SpriteBatch_LayerDepthOrder
                 COMMAND cna_test_easygl_spritebatch_layerdepth)
        set_tests_properties(EasyGL_SpriteBatch_LayerDepthOrder PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 885: BasicEffect pixel test — DirectionalLight1/2 + EmissiveColor on the lit path
        cna_easygl_test(cna_test_easygl_basiceffect_multilight_emissive
                        examples/easygl_basiceffect_multilight_emissive_test.cpp)
        add_test(NAME EasyGL_BasicEffect_MultiLightEmissive
                 COMMAND cna_test_easygl_basiceffect_multilight_emissive)
        set_tests_properties(EasyGL_BasicEffect_MultiLightEmissive PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 886: BasicEffect pixel test — real specular highlights
        cna_easygl_test(cna_test_easygl_basiceffect_specular
                        examples/easygl_basiceffect_specular_test.cpp)
        add_test(NAME EasyGL_BasicEffect_Specular
                 COMMAND cna_test_easygl_basiceffect_specular)
        set_tests_properties(EasyGL_BasicEffect_Specular PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 168: transformMatrix in SpriteBatch::Begin — translation pixel readback
        cna_easygl_test(cna_test_easygl_transform_matrix
                        examples/easygl_transform_matrix_test.cpp)
        add_test(NAME EasyGL_TransformMatrix_Translation
                 COMMAND cna_test_easygl_transform_matrix)
        set_tests_properties(EasyGL_TransformMatrix_Translation PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 169: Texture2D SetData/GetData partial rectangle round-trip
        cna_easygl_test(cna_test_easygl_texture2d_partial_rect
                        examples/easygl_texture2d_partial_rect_test.cpp)
        add_test(NAME EasyGL_Texture2D_PartialRect_RoundTrip
                 COMMAND cna_test_easygl_texture2d_partial_rect)
        set_tests_properties(EasyGL_Texture2D_PartialRect_RoundTrip PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 171: Texture2D mip-level SetData/GetData round-trip
        cna_easygl_test(cna_test_easygl_texture2d_mip
                        examples/easygl_texture2d_mip_test.cpp)
        add_test(NAME EasyGL_Texture2D_Mip_RoundTrip
                 COMMAND cna_test_easygl_texture2d_mip)
        set_tests_properties(EasyGL_Texture2D_Mip_RoundTrip PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 172: TextureCube per-face SetData/GetData round-trip
        cna_easygl_test(cna_test_easygl_texturecube_faces
                        examples/easygl_texturecube_faces_test.cpp)
        add_test(NAME EasyGL_TextureCube_Faces_RoundTrip
                 COMMAND cna_test_easygl_texturecube_faces)
        set_tests_properties(EasyGL_TextureCube_Faces_RoundTrip PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 276: TextureCube mip-level SetData/GetData round-trip, all six faces
        cna_easygl_test(cna_test_easygl_texturecube_mip
                        examples/easygl_texturecube_mip_test.cpp)
        add_test(NAME EasyGL_TextureCube_Mip_RoundTrip
                 COMMAND cna_test_easygl_texturecube_mip)
        set_tests_properties(EasyGL_TextureCube_Mip_RoundTrip PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 275: TextureCube partial rect + startIndex, all six faces
        cna_easygl_test(cna_test_easygl_texturecube_partial_rect
                        examples/easygl_texturecube_partial_rect_test.cpp)
        add_test(NAME EasyGL_TextureCube_PartialRect_RoundTrip
                 COMMAND cna_test_easygl_texturecube_partial_rect)
        set_tests_properties(EasyGL_TextureCube_PartialRect_RoundTrip PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 173: Texture3D z-slice SetData/GetData round-trip
        cna_easygl_test(cna_test_easygl_texture3d_slices
                        examples/easygl_texture3d_slices_test.cpp)
        add_test(NAME EasyGL_Texture3D_Slices_RoundTrip
                 COMMAND cna_test_easygl_texture3d_slices)
        set_tests_properties(EasyGL_Texture3D_Slices_RoundTrip PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 862: Texture3D mip-level SetData/GetData round-trip
        cna_easygl_test(cna_test_easygl_texture3d_mip
                        examples/easygl_texture3d_mip_test.cpp)
        add_test(NAME EasyGL_Texture3D_Mip_RoundTrip
                 COMMAND cna_test_easygl_texture3d_mip)
        set_tests_properties(EasyGL_Texture3D_Mip_RoundTrip PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 273: Texture3D partial box (x/y/z region) SetData/GetData round-trip
        cna_easygl_test(cna_test_easygl_texture3d_partial_box
                        examples/easygl_texture3d_partial_box_test.cpp)
        add_test(NAME EasyGL_Texture3D_PartialBox_RoundTrip
                 COMMAND cna_test_easygl_texture3d_partial_box)
        set_tests_properties(EasyGL_Texture3D_PartialBox_RoundTrip PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 274: Texture3D partial box (x/y/z region) GetData readback
        cna_easygl_test(cna_test_easygl_texture3d_partial_box_readback
                        examples/easygl_texture3d_partial_box_readback_test.cpp)
        add_test(NAME EasyGL_Texture3D_PartialBox_Readback
                 COMMAND cna_test_easygl_texture3d_partial_box_readback)
        set_tests_properties(EasyGL_Texture3D_PartialBox_Readback PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 176: SurfaceFormat validation — unsupported formats throw
        cna_easygl_test(cna_test_easygl_surface_format_throws
                        examples/easygl_surface_format_throws_test.cpp)
        add_test(NAME EasyGL_SurfaceFormat_Throws
                 COMMAND cna_test_easygl_surface_format_throws)
        set_tests_properties(EasyGL_SurfaceFormat_Throws PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 177: RenderTargetUsage DiscardContents vs PreserveContents
        cna_easygl_test(cna_test_easygl_render_target_usage
                        examples/easygl_render_target_usage_test.cpp)
        add_test(NAME EasyGL_RenderTargetUsage
                 COMMAND cna_test_easygl_render_target_usage)
        set_tests_properties(EasyGL_RenderTargetUsage PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 180: backbuffer → RT → backbuffer → RT → backbuffer round-trip
        cna_easygl_test(cna_test_easygl_rt_roundtrip
                        examples/easygl_rt_roundtrip_test.cpp)
        add_test(NAME EasyGL_RT_Roundtrip
                 COMMAND cna_test_easygl_rt_roundtrip)
        set_tests_properties(EasyGL_RT_Roundtrip PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 182: PresentationParameters round-trip via GraphicsDeviceManager
        cna_easygl_test(cna_test_easygl_presentation_parameters
                        examples/easygl_presentation_parameters_test.cpp)
        add_test(NAME EasyGL_PresentationParameters
                 COMMAND cna_test_easygl_presentation_parameters)
        set_tests_properties(EasyGL_PresentationParameters PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_device_reset_events
                        examples/easygl_device_reset_events_test.cpp)
        add_test(NAME EasyGL_DeviceResetEvents
                 COMMAND cna_test_easygl_device_reset_events)
        set_tests_properties(EasyGL_DeviceResetEvents PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_device_validation
                        examples/easygl_device_validation_test.cpp)
        add_test(NAME EasyGL_DeviceValidation
                 COMMAND cna_test_easygl_device_validation)
        set_tests_properties(EasyGL_DeviceValidation PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_draw_novertexbuffer
                        examples/easygl_draw_novertexbuffer_test.cpp)
        add_test(NAME EasyGL_DrawNoVertexBuffer
                 COMMAND cna_test_easygl_draw_novertexbuffer)
        set_tests_properties(EasyGL_DrawNoVertexBuffer PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_draw_noindexbuffer
                        examples/easygl_draw_noindexbuffer_test.cpp)
        add_test(NAME EasyGL_DrawNoIndexBuffer
                 COMMAND cna_test_easygl_draw_noindexbuffer)
        set_tests_properties(EasyGL_DrawNoIndexBuffer PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_draw_range_validation
                        examples/easygl_draw_range_validation_test.cpp)
        add_test(NAME EasyGL_DrawRangeValidation
                 COMMAND cna_test_easygl_draw_range_validation)
        set_tests_properties(EasyGL_DrawRangeValidation PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_primitivetype_validation
                        examples/easygl_primitivetype_validation_test.cpp)
        add_test(NAME EasyGL_PrimitiveTypeValidation
                 COMMAND cna_test_easygl_primitivetype_validation)
        set_tests_properties(EasyGL_PrimitiveTypeValidation PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_clear_overloads
                        examples/easygl_clear_overloads_test.cpp)
        add_test(NAME EasyGL_ClearOverloads
                 COMMAND cna_test_easygl_clear_overloads)
        set_tests_properties(EasyGL_ClearOverloads PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_viewport_state
                        examples/easygl_viewport_state_test.cpp)
        add_test(NAME EasyGL_ViewportState
                 COMMAND cna_test_easygl_viewport_state)
        set_tests_properties(EasyGL_ViewportState PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_scissor
                        examples/easygl_scissor_test.cpp)
        add_test(NAME EasyGL_Scissor
                 COMMAND cna_test_easygl_scissor)
        set_tests_properties(EasyGL_Scissor PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 880: Viewport sub-region (split-screen) GPU wiring
        cna_easygl_test(cna_test_easygl_viewport_subregion
                        examples/easygl_viewport_subregion_test.cpp)
        add_test(NAME EasyGL_Viewport_Subregion
                 COMMAND cna_test_easygl_viewport_subregion)
        set_tests_properties(EasyGL_Viewport_Subregion PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_disposed_resource
                        examples/easygl_disposed_resource_test.cpp)
        add_test(NAME EasyGL_DisposedResource
                 COMMAND cna_test_easygl_disposed_resource)
        set_tests_properties(EasyGL_DisposedResource PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_double_dispose
                        examples/easygl_double_dispose_test.cpp)
        add_test(NAME EasyGL_DoubleDispose
                 COMMAND cna_test_easygl_double_dispose)
        set_tests_properties(EasyGL_DoubleDispose PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_bound_resource_dispose
                        examples/easygl_bound_resource_dispose_test.cpp)
        add_test(NAME EasyGL_BoundResourceDispose
                 COMMAND cna_test_easygl_bound_resource_dispose)
        set_tests_properties(EasyGL_BoundResourceDispose PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_handle_release
                        examples/easygl_handle_release_test.cpp)
        add_test(NAME EasyGL_HandleRelease
                 COMMAND cna_test_easygl_handle_release)
        set_tests_properties(EasyGL_HandleRelease PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_move_semantics
                        examples/easygl_move_semantics_test.cpp)
        add_test(NAME EasyGL_MoveSemantics
                 COMMAND cna_test_easygl_move_semantics)
        set_tests_properties(EasyGL_MoveSemantics PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_resource_events
                        examples/easygl_resource_events_test.cpp)
        add_test(NAME EasyGL_ResourceEvents
                 COMMAND cna_test_easygl_resource_events)
        set_tests_properties(EasyGL_ResourceEvents PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_device_dispose_order
                        examples/easygl_device_dispose_order_test.cpp)
        add_test(NAME EasyGL_DeviceDisposeOrder
                 COMMAND cna_test_easygl_device_dispose_order)
        set_tests_properties(EasyGL_DeviceDisposeOrder PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_resource_leak
                        examples/easygl_resource_leak_test.cpp)
        add_test(NAME EasyGL_ResourceLeak
                 COMMAND cna_test_easygl_resource_leak)
        set_tests_properties(EasyGL_ResourceLeak PROPERTIES
            TIMEOUT 60
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_present_interval
                        examples/easygl_present_interval_test.cpp)
        add_test(NAME EasyGL_PresentInterval
                 COMMAND cna_test_easygl_present_interval)
        set_tests_properties(EasyGL_PresentInterval PROPERTIES
            TIMEOUT 60
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_fullscreen_field
                        examples/easygl_fullscreen_field_test.cpp)
        add_test(NAME EasyGL_FullScreenField
                 COMMAND cna_test_easygl_fullscreen_field)
        set_tests_properties(EasyGL_FullScreenField PROPERTIES
            TIMEOUT 60
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_backbuffer_resize
                        examples/easygl_backbuffer_resize_test.cpp)
        add_test(NAME EasyGL_BackbufferResize
                 COMMAND cna_test_easygl_backbuffer_resize)
        set_tests_properties(EasyGL_BackbufferResize PROPERTIES
            TIMEOUT 60
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_real_window_resize
                        examples/easygl_real_window_resize_test.cpp)
        add_test(NAME EasyGL_RealWindowResize
                 COMMAND cna_test_easygl_real_window_resize)
        set_tests_properties(EasyGL_RealWindowResize PROPERTIES
            TIMEOUT 60
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 349: custom Viewport survives Present() with no resize, resets after a real one.
        cna_easygl_test(cna_test_easygl_viewport_reset_after_resize
                        examples/viewport_reset_after_resize_test.cpp)
        add_test(NAME EasyGL_ViewportResetAfterResize
                 COMMAND cna_test_easygl_viewport_reset_after_resize)
        set_tests_properties(EasyGL_ViewportResetAfterResize PROPERTIES
            TIMEOUT 60
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_depth_format
                        examples/easygl_depth_format_test.cpp)
        add_test(NAME EasyGL_DepthFormat
                 COMMAND cna_test_easygl_depth_format)
        set_tests_properties(EasyGL_DepthFormat PROPERTIES
            TIMEOUT 60
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_msaa_change
                        examples/easygl_msaa_change_test.cpp)
        add_test(NAME EasyGL_MsaaChange
                 COMMAND cna_test_easygl_msaa_change)
        set_tests_properties(EasyGL_MsaaChange PROPERTIES
            TIMEOUT 60
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_vertexbuffer_setdata
                        examples/easygl_vertexbuffer_setdata_test.cpp)
        add_test(NAME EasyGL_VbSetData
                 COMMAND cna_test_easygl_vertexbuffer_setdata)
        set_tests_properties(EasyGL_VbSetData PROPERTIES
            TIMEOUT 60
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_dynamic_buffer_stress
                        examples/easygl_dynamic_buffer_stress_test.cpp)
        add_test(NAME EasyGL_DynamicBufferStress
                 COMMAND cna_test_easygl_dynamic_buffer_stress)
        set_tests_properties(EasyGL_DynamicBufferStress PROPERTIES
            TIMEOUT 60
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_buffer_usage
                        examples/easygl_buffer_usage_test.cpp)
        add_test(NAME EasyGL_BufferUsage
                 COMMAND cna_test_easygl_buffer_usage)
        set_tests_properties(EasyGL_BufferUsage PROPERTIES
            TIMEOUT 60
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_disposed_buffer
                        examples/easygl_disposed_buffer_test.cpp)
        add_test(NAME EasyGL_DisposedBuffer
                 COMMAND cna_test_easygl_disposed_buffer)
        set_tests_properties(EasyGL_DisposedBuffer PROPERTIES
            TIMEOUT 60
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_effect_clone
                        examples/easygl_effect_clone_test.cpp)
        add_test(NAME EasyGL_EffectClone
                 COMMAND cna_test_easygl_effect_clone)
        set_tests_properties(EasyGL_EffectClone PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_effect_current_technique
                        examples/easygl_effect_current_technique_test.cpp)
        add_test(NAME EasyGL_EffectCurrentTechnique
                 COMMAND cna_test_easygl_effect_current_technique)
        set_tests_properties(EasyGL_EffectCurrentTechnique PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_basiceffect_combinations
                        examples/easygl_basiceffect_combinations_test.cpp)
        add_test(NAME EasyGL_BasicEffectCombinations
                 COMMAND cna_test_easygl_basiceffect_combinations)
        set_tests_properties(EasyGL_BasicEffectCombinations PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 364: BasicEffect pixel test — VertexColorEnabled=false, no texture, diffuse color only
        cna_easygl_test(cna_test_easygl_basiceffect_vertexcolor_disabled
                        examples/easygl_basiceffect_vertexcolor_disabled_test.cpp)
        add_test(NAME EasyGL_BasicEffect_VertexColorDisabled
                 COMMAND cna_test_easygl_basiceffect_vertexcolor_disabled)
        set_tests_properties(EasyGL_BasicEffect_VertexColorDisabled PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 365: BasicEffect pixel test — VertexColorEnabled=true, no texture, vertex color multiplication
        cna_easygl_test(cna_test_easygl_basiceffect_vertexcolor_enabled
                        examples/easygl_basiceffect_vertexcolor_enabled_test.cpp)
        add_test(NAME EasyGL_BasicEffect_VertexColorEnabled
                 COMMAND cna_test_easygl_basiceffect_vertexcolor_enabled)
        set_tests_properties(EasyGL_BasicEffect_VertexColorEnabled PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 366: BasicEffect pixel test — TextureEnabled=true, no vertex color
        cna_easygl_test(cna_test_easygl_basiceffect_texture_enabled
                        examples/easygl_basiceffect_texture_enabled_test.cpp)
        add_test(NAME EasyGL_BasicEffect_TextureEnabled
                 COMMAND cna_test_easygl_basiceffect_texture_enabled)
        set_tests_properties(EasyGL_BasicEffect_TextureEnabled PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 367: BasicEffect pixel test — TextureEnabled=true AND VertexColorEnabled=true
        cna_easygl_test(cna_test_easygl_basiceffect_texture_vertexcolor_enabled
                        examples/easygl_basiceffect_texture_vertexcolor_enabled_test.cpp)
        add_test(NAME EasyGL_BasicEffect_TextureVertexColorEnabled
                 COMMAND cna_test_easygl_basiceffect_texture_vertexcolor_enabled)
        set_tests_properties(EasyGL_BasicEffect_TextureVertexColorEnabled PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 368: BasicEffect pixel test — LightingEnabled=true, one DirectionalLight
        cna_easygl_test(cna_test_easygl_basiceffect_one_light
                        examples/easygl_basiceffect_one_light_test.cpp)
        add_test(NAME EasyGL_BasicEffect_OneLight
                 COMMAND cna_test_easygl_basiceffect_one_light)
        set_tests_properties(EasyGL_BasicEffect_OneLight PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 369: BasicEffect pixel test — DiffuseColor+EmissiveColor, LightingEnabled=false
        cna_easygl_test(cna_test_easygl_basiceffect_emissive
                        examples/easygl_basiceffect_emissive_test.cpp)
        add_test(NAME EasyGL_BasicEffect_Emissive
                 COMMAND cna_test_easygl_basiceffect_emissive)
        set_tests_properties(EasyGL_BasicEffect_Emissive PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 370: BasicEffect cross-backend image comparison suite — closes Phase 42
        cna_easygl_test(cna_test_easygl_basiceffect_combined
                        examples/easygl_basiceffect_combined_test.cpp)
        add_test(NAME EasyGL_BasicEffect_Combined
                 COMMAND cna_test_easygl_basiceffect_combined)
        set_tests_properties(EasyGL_BasicEffect_Combined PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_alphatest_modes
                        examples/easygl_alphatest_modes_test.cpp)
        add_test(NAME EasyGL_AlphaTestModes
                 COMMAND cna_test_easygl_alphatest_modes)
        set_tests_properties(EasyGL_AlphaTestModes PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 373: AlphaTestEffect all CompareFunction modes — threshold sweep (below/at/above)
        cna_easygl_test(cna_test_easygl_alphatest_comparefunction_sweep
                        examples/easygl_alphatest_comparefunction_sweep_test.cpp)
        add_test(NAME EasyGL_AlphaTest_CompareFunctionSweep
                 COMMAND cna_test_easygl_alphatest_comparefunction_sweep)
        set_tests_properties(EasyGL_AlphaTest_CompareFunctionSweep PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 377: AlphaTestEffect vertex color x diffuse color interaction
        cna_easygl_test(cna_test_easygl_alphatest_vertexcolor_diffuse
                        examples/easygl_alphatest_vertexcolor_diffuse_test.cpp)
        add_test(NAME EasyGL_AlphaTest_VertexColorDiffuse
                 COMMAND cna_test_easygl_alphatest_vertexcolor_diffuse)
        set_tests_properties(EasyGL_AlphaTest_VertexColorDiffuse PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 378: AlphaTestEffect fog behavior
        cna_easygl_test(cna_test_easygl_alphatest_fog
                        examples/easygl_alphatest_fog_test.cpp)
        add_test(NAME EasyGL_AlphaTest_Fog
                 COMMAND cna_test_easygl_alphatest_fog)
        set_tests_properties(EasyGL_AlphaTest_Fog PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 379: AlphaTestEffect null/no-texture behavior
        cna_easygl_test(cna_test_easygl_alphatest_null_texture
                        examples/easygl_alphatest_null_texture_test.cpp)
        add_test(NAME EasyGL_AlphaTest_NullTexture
                 COMMAND cna_test_easygl_alphatest_null_texture)
        set_tests_properties(EasyGL_AlphaTest_NullTexture PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 383: DualTextureEffect two-texture blend formula, including FNA's color.rgb*=2
        cna_easygl_test(cna_test_easygl_dualtextureeffect_doubling
                        examples/easygl_dualtextureeffect_doubling_test.cpp)
        add_test(NAME EasyGL_DualTextureEffect_Doubling
                 COMMAND cna_test_easygl_dualtextureeffect_doubling)
        set_tests_properties(EasyGL_DualTextureEffect_Doubling PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 385: DualTextureEffect Alpha premultiplication, verified via real AlphaBlend
        cna_easygl_test(cna_test_easygl_dualtextureeffect_alpha
                        examples/easygl_dualtextureeffect_alpha_test.cpp)
        add_test(NAME EasyGL_DualTextureEffect_Alpha
                 COMMAND cna_test_easygl_dualtextureeffect_alpha)
        set_tests_properties(EasyGL_DualTextureEffect_Alpha PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 386: DualTextureEffect first texture (Texture, slot 0) null behavior
        cna_easygl_test(cna_test_easygl_dualtextureeffect_null_texture0
                        examples/easygl_dualtextureeffect_null_texture0_test.cpp)
        add_test(NAME EasyGL_DualTextureEffect_NullTexture0
                 COMMAND cna_test_easygl_dualtextureeffect_null_texture0)
        set_tests_properties(EasyGL_DualTextureEffect_NullTexture0 PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 387: DualTextureEffect second texture (Texture2, slot 1) null behavior
        cna_easygl_test(cna_test_easygl_dualtextureeffect_null_texture2
                        examples/easygl_dualtextureeffect_null_texture2_test.cpp)
        add_test(NAME EasyGL_DualTextureEffect_NullTexture2
                 COMMAND cna_test_easygl_dualtextureeffect_null_texture2)
        set_tests_properties(EasyGL_DualTextureEffect_NullTexture2 PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 388: DualTextureEffect fog behavior
        cna_easygl_test(cna_test_easygl_dualtextureeffect_fog
                        examples/easygl_dualtextureeffect_fog_test.cpp)
        add_test(NAME EasyGL_DualTextureEffect_Fog
                 COMMAND cna_test_easygl_dualtextureeffect_fog)
        set_tests_properties(EasyGL_DualTextureEffect_Fog PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 389: DualTextureEffect cross-backend image comparison suite (closes Phase 44 pixel work)
        cna_easygl_test(cna_test_easygl_dualtextureeffect_combined
                        examples/easygl_dualtextureeffect_combined_test.cpp)
        add_test(NAME EasyGL_DualTextureEffect_Combined
                 COMMAND cna_test_easygl_dualtextureeffect_combined)
        set_tests_properties(EasyGL_DualTextureEffect_Combined PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_dualtexture
                        examples/easygl_dualtexture_test.cpp)
        add_test(NAME EasyGL_DualTexture
                 COMMAND cna_test_easygl_dualtexture)
        set_tests_properties(EasyGL_DualTexture PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_skinned_effect_bones
                        examples/easygl_skinned_effect_bones_test.cpp)
        add_test(NAME EasyGL_SkinnedBones
                 COMMAND cna_test_easygl_skinned_effect_bones)
        set_tests_properties(EasyGL_SkinnedBones PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_basiceffect_default_lighting
                        examples/easygl_basiceffect_default_lighting_test.cpp)
        add_test(NAME EasyGL_BasicEffectDefaultLighting
                 COMMAND cna_test_easygl_basiceffect_default_lighting)
        set_tests_properties(EasyGL_BasicEffectDefaultLighting PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_easygl_test(cna_test_easygl_basiceffect_fog
                        examples/easygl_basiceffect_fog_test.cpp)
        add_test(NAME EasyGL_BasicEffectFog
                 COMMAND cna_test_easygl_basiceffect_fog)
        set_tests_properties(EasyGL_BasicEffectFog PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 247: draw quad with each supported VertexElementFormat via VertexBuffer
        cna_easygl_test(cna_test_easygl_vertex_formats
                        examples/easygl_vertex_formats_test.cpp)
        add_test(NAME EasyGL_VertexFormats_AllStrides
                 COMMAND cna_test_easygl_vertex_formats)
        set_tests_properties(EasyGL_VertexFormats_AllStrides PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 255: DrawUserPrimitives<VertexPositionColor> pixel-readback
        cna_easygl_test(cna_test_easygl_draw_user_primitives_vpc
                        examples/easygl_draw_user_primitives_vpc_test.cpp)
        add_test(NAME EasyGL_DrawUserPrimitives_VPC
                 COMMAND cna_test_easygl_draw_user_primitives_vpc)
        set_tests_properties(EasyGL_DrawUserPrimitives_VPC PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 256: DrawUserPrimitives with explicit VertexDeclaration pixel-readback
        cna_easygl_test(cna_test_easygl_draw_user_primitives_custom
                        examples/easygl_draw_user_primitives_custom_test.cpp)
        add_test(NAME EasyGL_DrawUserPrimitives_CustomVD
                 COMMAND cna_test_easygl_draw_user_primitives_custom)
        set_tests_properties(EasyGL_DrawUserPrimitives_CustomVD PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 257: DrawUserIndexedPrimitives<VertexPositionColor> (16-bit indices) pixel-readback
        cna_easygl_test(cna_test_easygl_draw_user_indexed_primitives_vpc
                        examples/easygl_draw_user_indexed_primitives_vpc_test.cpp)
        add_test(NAME EasyGL_DrawUserIndexedPrimitives_VPC
                 COMMAND cna_test_easygl_draw_user_indexed_primitives_vpc)
        set_tests_properties(EasyGL_DrawUserIndexedPrimitives_VPC PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 258: DrawUserIndexedPrimitives<VertexPositionColor> (32-bit indices) pixel-readback
        cna_easygl_test(cna_test_easygl_draw_user_indexed_primitives_32
                        examples/easygl_draw_user_indexed_primitives_32_test.cpp)
        add_test(NAME EasyGL_DrawUserIndexedPrimitives_32
                 COMMAND cna_test_easygl_draw_user_indexed_primitives_32)
        set_tests_properties(EasyGL_DrawUserIndexedPrimitives_32 PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 268: non-power-of-two (3x5) texture upload + GPU-sampled pixel-readback
        cna_easygl_test(cna_test_easygl_npot_texture
                        examples/easygl_npot_texture_test.cpp)
        add_test(NAME EasyGL_NpotTexture
                 COMMAND cna_test_easygl_npot_texture)
        set_tests_properties(EasyGL_NpotTexture PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 269: SpriteBatch TextureAddressMode (Wrap vs Clamp) edge-sampling pixel-readback
        cna_easygl_test(cna_test_easygl_texture_address_mode
                        examples/easygl_texture_address_mode_test.cpp)
        add_test(NAME EasyGL_TextureAddressMode
                 COMMAND cna_test_easygl_texture_address_mode)
        set_tests_properties(EasyGL_TextureAddressMode PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 737: SpriteBatch TextureAddressMode::Mirror edge-sampling pixel-readback -- Task 269
        # only independently verified Wrap/Clamp; Mirror is fixed by the same code path but was
        # never verified on its own until now.
        cna_easygl_test(cna_test_easygl_texture_address_mode_mirror
                        examples/easygl_texture_address_mode_mirror_test.cpp)
        add_test(NAME EasyGL_TextureAddressMode_Mirror
                 COMMAND cna_test_easygl_texture_address_mode_mirror)
        set_tests_properties(EasyGL_TextureAddressMode_Mirror PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 424: Pixel test -- draw a single SpriteFont glyph at a known position on EasyGL.
        cna_easygl_test(cna_test_easygl_spritefont_single_glyph
                        examples/easygl_spritefont_single_glyph_test.cpp)
        add_test(NAME EasyGL_SpriteFont_SingleGlyph
                 COMMAND cna_test_easygl_spritefont_single_glyph)
        set_tests_properties(EasyGL_SpriteFont_SingleGlyph PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 425: Pixel test -- draw multiple SpriteFont glyphs with spacing on EasyGL.
        cna_easygl_test(cna_test_easygl_spritefont_multiglyph_spacing
                        examples/easygl_spritefont_multiglyph_spacing_test.cpp)
        add_test(NAME EasyGL_SpriteFont_MultiGlyphSpacing
                 COMMAND cna_test_easygl_spritefont_multiglyph_spacing)
        set_tests_properties(EasyGL_SpriteFont_MultiGlyphSpacing PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 426: Pixel test -- SpriteFont newline advances by line spacing on EasyGL.
        cna_easygl_test(cna_test_easygl_spritefont_newline
                        examples/easygl_spritefont_newline_test.cpp)
        add_test(NAME EasyGL_SpriteFont_Newline
                 COMMAND cna_test_easygl_spritefont_newline)
        set_tests_properties(EasyGL_SpriteFont_Newline PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 427: Pixel test -- SpriteFont default character fallback renders the expected
        # glyph on EasyGL.
        cna_easygl_test(cna_test_easygl_spritefont_default_char
                        examples/easygl_spritefont_default_char_test.cpp)
        add_test(NAME EasyGL_SpriteFont_DefaultChar
                 COMMAND cna_test_easygl_spritefont_default_char)
        set_tests_properties(EasyGL_SpriteFont_DefaultChar PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 428: Verify SpriteEffects (flip) with DrawString on EasyGL.
        cna_easygl_test(cna_test_easygl_spritefont_effects_flip
                        examples/easygl_spritefont_effects_flip_test.cpp)
        add_test(NAME EasyGL_SpriteFont_EffectsFlip
                 COMMAND cna_test_easygl_spritefont_effects_flip)
        set_tests_properties(EasyGL_SpriteFont_EffectsFlip PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 429: Verify rotation/origin/scale with DrawString on EasyGL.
        cna_easygl_test(cna_test_easygl_spritefont_effects_rotation_scale
                        examples/easygl_spritefont_effects_rotation_scale_test.cpp)
        add_test(NAME EasyGL_SpriteFont_EffectsRotationScale
                 COMMAND cna_test_easygl_spritefont_effects_rotation_scale)
        set_tests_properties(EasyGL_SpriteFont_EffectsRotationScale PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 293: DualTextureEffect (3D stock effect) honors GraphicsDevice.SamplerStates[0]
        cna_easygl_test(cna_test_easygl_sampler_state_effect
                        examples/easygl_sampler_state_effect_test.cpp)
        add_test(NAME EasyGL_SamplerState_DualTextureEffect
                 COMMAND cna_test_easygl_sampler_state_effect)
        set_tests_properties(EasyGL_SamplerState_DualTextureEffect PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 294: TextureAddressMode::Clamp on a 3D stock effect (DualTextureEffect)
        cna_easygl_test(cna_test_easygl_texture_address_mode_clamp_effect
                        examples/easygl_texture_address_mode_clamp_effect_test.cpp)
        add_test(NAME EasyGL_TextureAddressMode_Clamp_DualTextureEffect
                 COMMAND cna_test_easygl_texture_address_mode_clamp_effect)
        set_tests_properties(EasyGL_TextureAddressMode_Clamp_DualTextureEffect PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 296: TextureAddressMode::Mirror on a 3D stock effect (DualTextureEffect)
        cna_easygl_test(cna_test_easygl_texture_address_mode_mirror_effect
                        examples/easygl_texture_address_mode_mirror_effect_test.cpp)
        add_test(NAME EasyGL_TextureAddressMode_Mirror_DualTextureEffect
                 COMMAND cna_test_easygl_texture_address_mode_mirror_effect)
        set_tests_properties(EasyGL_TextureAddressMode_Mirror_DualTextureEffect PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 297: TextureFilter::Point vs Linear, magnification and minification
        cna_easygl_test(cna_test_easygl_texture_filter_point_vs_linear
                        examples/easygl_texture_filter_point_vs_linear_test.cpp)
        add_test(NAME EasyGL_TextureFilter_PointVsLinear
                 COMMAND cna_test_easygl_texture_filter_point_vs_linear)
        set_tests_properties(EasyGL_TextureFilter_PointVsLinear PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 298: mipmap filter behavior (LinearMipPoint selects the correct mip; Point does not)
        cna_easygl_test(cna_test_easygl_texture_mip_filter_effect
                        examples/easygl_texture_mip_filter_effect_test.cpp)
        add_test(NAME EasyGL_TextureMipFilter_DualTextureEffect
                 COMMAND cna_test_easygl_texture_mip_filter_effect)
        set_tests_properties(EasyGL_TextureMipFilter_DualTextureEffect PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 299: anisotropic filtering caps and fallback (extreme MaxAnisotropy must not crash)
        cna_easygl_test(cna_test_easygl_texture_anisotropic_effect
                        examples/easygl_texture_anisotropic_effect_test.cpp)
        add_test(NAME EasyGL_TextureAnisotropic_DualTextureEffect
                 COMMAND cna_test_easygl_texture_anisotropic_effect)
        set_tests_properties(EasyGL_TextureAnisotropic_DualTextureEffect PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 924 (split from Task 867): a single-level Texture2D sampled with a mipmap-requiring
        # TextureFilter (Anisotropic) must not render solid black (GL_TEXTURE_MAX_LEVEL now
        # clamped to the real level count instead of GL's own default of 1000).
        cna_easygl_test(cna_test_easygl_texture2d_anisotropic_singlelevel
                        examples/easygl_texture2d_anisotropic_singlelevel_test.cpp)
        add_test(NAME EasyGL_Texture2D_AnisotropicSingleLevel
                 COMMAND cna_test_easygl_texture2d_anisotropic_singlelevel)
        set_tests_properties(EasyGL_Texture2D_AnisotropicSingleLevel PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 918: real GL_EXT_texture_filter_anisotropic wiring, verified directly against
        # easy-gl's own Sampler API rather than trusting the capability-dump log alone.
        cna_easygl_test(cna_test_easygl_anisotropic_gl_state
                        examples/easygl_anisotropic_gl_state_test.cpp)
        target_link_libraries(cna_test_easygl_anisotropic_gl_state PRIVATE easy-gl)
        add_test(NAME EasyGL_Anisotropic_GlState
                 COMMAND cna_test_easygl_anisotropic_gl_state)
        set_tests_properties(EasyGL_Anisotropic_GlState PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 303: BlendState::Opaque discards destination + source alpha entirely
        cna_easygl_test(cna_test_easygl_blendstate_opaque
                        examples/easygl_blendstate_opaque_test.cpp)
        add_test(NAME EasyGL_BlendState_Opaque
                 COMMAND cna_test_easygl_blendstate_opaque)
        set_tests_properties(EasyGL_BlendState_Opaque PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 304: BlendState::AlphaBlend expects premultiplied source colour
        cna_easygl_test(cna_test_easygl_blendstate_alphablend
                        examples/easygl_blendstate_alphablend_test.cpp)
        add_test(NAME EasyGL_BlendState_AlphaBlend
                 COMMAND cna_test_easygl_blendstate_alphablend)
        set_tests_properties(EasyGL_BlendState_AlphaBlend PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 305: BlendState::NonPremultiplied expects a raw (non-premultiplied) source colour
        cna_easygl_test(cna_test_easygl_blendstate_nonpremultiplied
                        examples/easygl_blendstate_nonpremultiplied_test.cpp)
        add_test(NAME EasyGL_BlendState_NonPremultiplied
                 COMMAND cna_test_easygl_blendstate_nonpremultiplied)
        set_tests_properties(EasyGL_BlendState_NonPremultiplied PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 306: BlendState::Additive saturation + full-destination-add behaviour
        cna_easygl_test(cna_test_easygl_blendstate_additive
                        examples/easygl_blendstate_additive_test.cpp)
        add_test(NAME EasyGL_BlendState_Additive
                 COMMAND cna_test_easygl_blendstate_additive)
        set_tests_properties(EasyGL_BlendState_Additive PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 307: ColorBlendFunction/AlphaBlendFunction operate independently
        cna_easygl_test(cna_test_easygl_blendstate_separate_functions
                        examples/easygl_blendstate_separate_functions_test.cpp)
        add_test(NAME EasyGL_BlendState_SeparateFunctions
                 COMMAND cna_test_easygl_blendstate_separate_functions)
        set_tests_properties(EasyGL_BlendState_SeparateFunctions PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 308: ColorSourceBlend/ColorDestinationBlend operate independently from Alpha*Blend
        cna_easygl_test(cna_test_easygl_blendstate_separate_factors
                        examples/easygl_blendstate_separate_factors_test.cpp)
        add_test(NAME EasyGL_BlendState_SeparateFactors
                 COMMAND cna_test_easygl_blendstate_separate_factors)
        set_tests_properties(EasyGL_BlendState_SeparateFactors PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 309: BlendState.BlendFactor propagates via GraphicsDevice.setBlendStateProperty alone
        cna_easygl_test(cna_test_easygl_blendstate_blendfactor
                        examples/easygl_blendstate_blendfactor_test.cpp)
        add_test(NAME EasyGL_BlendState_BlendFactor
                 COMMAND cna_test_easygl_blendstate_blendfactor)
        set_tests_properties(EasyGL_BlendState_BlendFactor PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 313: DepthStencilState.DepthBufferWriteEnable actually gates depth-buffer writes
        cna_easygl_test(cna_test_easygl_depthstencilstate_write_enable
                        examples/easygl_depthstencilstate_write_enable_test.cpp)
        add_test(NAME EasyGL_DepthStencilState_WriteEnable
                 COMMAND cna_test_easygl_depthstencilstate_write_enable)
        set_tests_properties(EasyGL_DepthStencilState_WriteEnable PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 314: DepthStencilState.DepthBufferFunction (Always/Never/Less/LessEqual/Greater)
        cna_easygl_test(cna_test_easygl_depthstencilstate_compare_function
                        examples/easygl_depthstencilstate_compare_function_test.cpp)
        add_test(NAME EasyGL_DepthStencilState_CompareFunction
                 COMMAND cna_test_easygl_depthstencilstate_compare_function)
        set_tests_properties(EasyGL_DepthStencilState_CompareFunction PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 315: DepthStencilState.StencilEnable actually gates the stencil test
        cna_easygl_test(cna_test_easygl_depthstencilstate_stencil_enable
                        examples/easygl_depthstencilstate_stencil_enable_test.cpp)
        add_test(NAME EasyGL_DepthStencilState_StencilEnable
                 COMMAND cna_test_easygl_depthstencilstate_stencil_enable)
        set_tests_properties(EasyGL_DepthStencilState_StencilEnable PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 316: DepthStencilState.StencilMask / StencilWriteMask
        cna_easygl_test(cna_test_easygl_depthstencilstate_stencil_mask
                        examples/easygl_depthstencilstate_stencil_mask_test.cpp)
        add_test(NAME EasyGL_DepthStencilState_StencilMask
                 COMMAND cna_test_easygl_depthstencilstate_stencil_mask)
        set_tests_properties(EasyGL_DepthStencilState_StencilMask PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 317: DepthStencilState front-face stencil operations (StencilFail/
        # StencilDepthBufferFail/StencilPass)
        cna_easygl_test(cna_test_easygl_depthstencilstate_stencil_ops
                        examples/easygl_depthstencilstate_stencil_ops_test.cpp)
        add_test(NAME EasyGL_DepthStencilState_StencilOps
                 COMMAND cna_test_easygl_depthstencilstate_stencil_ops)
        set_tests_properties(EasyGL_DepthStencilState_StencilOps PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 318: DepthStencilState.TwoSidedStencilMode / back-face (CCW) stencil ops
        cna_easygl_test(cna_test_easygl_depthstencilstate_stencil_twosided
                        examples/easygl_depthstencilstate_stencil_twosided_test.cpp)
        add_test(NAME EasyGL_DepthStencilState_StencilTwoSided
                 COMMAND cna_test_easygl_depthstencilstate_stencil_twosided)
        set_tests_properties(EasyGL_DepthStencilState_StencilTwoSided PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 319: GraphicsDevice.ReferenceStencil independent-override behavior (Task 872 -
        # confirmed a universal, not-Vulkan-specific gap; registered as a documented known failure)
        cna_easygl_test(cna_test_easygl_graphicsdevice_reference_stencil
                        examples/easygl_graphicsdevice_reference_stencil_test.cpp)
        add_test(NAME EasyGL_GraphicsDevice_ReferenceStencil
                 COMMAND cna_test_easygl_graphicsdevice_reference_stencil)
        set_tests_properties(EasyGL_GraphicsDevice_ReferenceStencil PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 323 (+ 324/325 coverage): RasterizerState.CullMode — None/CullClockwiseFace/
        # CullCounterClockwiseFace, contrast-checked with front- and back-winding quads
        cna_easygl_test(cna_test_easygl_rasterizerstate_cullmode
                        examples/easygl_rasterizerstate_cullmode_test.cpp)
        add_test(NAME EasyGL_RasterizerState_CullMode
                 COMMAND cna_test_easygl_rasterizerstate_cullmode)
        set_tests_properties(EasyGL_RasterizerState_CullMode PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 326: FillMode::Solid baseline on EasyGL (reuses Task 327's backend-agnostic Vulkan
        # source — its Solid sub-tests 1/3 previously only ran on Vulkan).
        cna_easygl_test(cna_test_easygl_fill_mode
                        examples/vulkan_fill_mode_test.cpp)
        add_test(NAME EasyGL_FillMode_Solid
                 COMMAND cna_test_easygl_fill_mode)
        set_tests_properties(EasyGL_FillMode_Solid PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 331: RenderTarget2D constructor/property audit against FNA.
        cna_easygl_test(cna_test_easygl_rendertarget2d_properties
                        examples/easygl_rendertarget2d_properties_test.cpp)
        add_test(NAME EasyGL_RenderTarget2D_Properties
                 COMMAND cna_test_easygl_rendertarget2d_properties)
        set_tests_properties(EasyGL_RenderTarget2D_Properties PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 332: RenderTargetCube constructor/property audit against FNA.
        cna_easygl_test(cna_test_easygl_rendertargetcube_properties
                        examples/easygl_rendertargetcube_properties_test.cpp)
        add_test(NAME EasyGL_RenderTargetCube_Properties
                 COMMAND cna_test_easygl_rendertargetcube_properties)
        set_tests_properties(EasyGL_RenderTargetCube_Properties PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 498: build and run at least 5 small FNA/XNA sample ports on CNA as real
        # compatibility proof -- EasyGL is the primary/most-mature backend, so most of this
        # task's own new samples target it, with 3 genuinely 3D (Task 730 already proved 2D
        # compatibility via SDL_Renderer's own 5 samples).
        # Sample 1/5: reuse cna_demo_2d, closing the exact same kind of gap Task 730 closed for
        # SDL_Renderer (already smoke-tested on Vulkan/Bgfx via Tasks 88/89, never on EasyGL).
        add_test(NAME EasyGL_Demo2D_SmokeTest
                 COMMAND cna_demo_2d --smoke 3)
        set_tests_properties(EasyGL_Demo2D_SmokeTest PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}"
            LABELS GraphicsSmoke)

        # Sample 2/5: moving 3D object -- Update()-driven World-space translation, BasicEffect.
        cna_easygl_test(cna_test_easygl_sample_moving_quad3d
                        examples/easygl_sample_moving_quad3d_test.cpp)
        add_test(NAME EasyGL_Sample_MovingQuad3D
                 COMMAND cna_test_easygl_sample_moving_quad3d)
        set_tests_properties(EasyGL_Sample_MovingQuad3D PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Sample 3/5: reactive material swap -- Update()-driven DualTextureEffect.Texture2 swap.
        cna_easygl_test(cna_test_easygl_sample_dualtexture_swap
                        examples/easygl_sample_dualtexture_swap_test.cpp)
        add_test(NAME EasyGL_Sample_DualTextureSwap
                 COMMAND cna_test_easygl_sample_dualtexture_swap)
        set_tests_properties(EasyGL_Sample_DualTextureSwap PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Sample 4/5: keyboard-driven 3D object -- the 3D counterpart to Task 730's keyboard
        # sprite sample, using the same InputManager::SetKeyState injection seam.
        cna_easygl_test(cna_test_easygl_sample_keyboard_cube3d
                        examples/easygl_sample_keyboard_cube3d_test.cpp)
        add_test(NAME EasyGL_Sample_KeyboardCube3D
                 COMMAND cna_test_easygl_sample_keyboard_cube3d)
        set_tests_properties(EasyGL_Sample_KeyboardCube3D PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Sample 5/5: layered UI fade -- Update()-driven SpriteBatch alpha-blend compositing.
        cna_easygl_test(cna_test_easygl_sample_layered_blend
                        examples/easygl_sample_layered_blend_test.cpp)
        add_test(NAME EasyGL_Sample_LayeredBlend
                 COMMAND cna_test_easygl_sample_layered_blend)
        set_tests_properties(EasyGL_Sample_LayeredBlend PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    endif()

    # Vulkan integration tests
    if(CNA_BUILD_TESTS AND NOT EMSCRIPTEN AND NOT WIN32
       AND CNA_GRAPHICS_BACKEND STREQUAL "VULKAN")

        # --- helper macro: build a headless Vulkan test exe ----------------------
        macro(cna_vulkan_test target src)
            add_executable(${target} ${src})
            if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
                target_link_libraries(${target} PRIVATE
                    -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                    SHARP_RUNTIME)
            else()
                target_link_libraries(${target} PRIVATE CNA SHARP_RUNTIME)
            endif()
            if(TARGET SDL3::SDL3main)
                target_link_libraries(${target} PRIVATE SDL3::SDL3main)
            endif()
        endmacro()

        enable_testing()

        # Task 88: 3-frame smoke test for the Vulkan backend
        add_test(NAME Vulkan_Demo2D_SmokeTest
                 COMMAND cna_demo_2d --smoke 3)
        set_tests_properties(Vulkan_Demo2D_SmokeTest PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}"
            LABELS GraphicsSmoke)

        # Task 124: DrawInstancedPrimitives — 3 instances at different X positions
        cna_vulkan_test(cna_test_vulkan_instanced
                        examples/vulkan_instanced_test.cpp)
        add_test(NAME Vulkan_DrawInstanced_3Instances
                 COMMAND cna_test_vulkan_instanced)
        set_tests_properties(Vulkan_DrawInstanced_3Instances PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 119: ShaderEffect with SPIR-V — SpriteBatch tint via custom push constants
        cna_vulkan_test(cna_test_vulkan_shader_effect
                        examples/vulkan_shader_effect_test.cpp)
        add_test(NAME Vulkan_ShaderEffect_SpirV
                 COMMAND cna_test_vulkan_shader_effect)
        set_tests_properties(Vulkan_ShaderEffect_SpirV PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 284: Texture2D Color format mapping — linear vs. sRGB decode on Vulkan
        cna_vulkan_test(cna_test_vulkan_texture_srgb
                        examples/vulkan_texture_srgb_test.cpp)
        add_test(NAME Vulkan_Texture2D_ColorFormat_Linear
                 COMMAND cna_test_vulkan_texture_srgb)
        set_tests_properties(Vulkan_Texture2D_ColorFormat_Linear PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 135: DualTextureEffect — magenta×yellow→red pixel readback on Vulkan
        cna_vulkan_test(cna_test_vulkan_dual_texture
                        examples/vulkan_dual_texture_test.cpp)
        add_test(NAME Vulkan_DualTextureEffect_Blend
                 COMMAND cna_test_vulkan_dual_texture)
        set_tests_properties(Vulkan_DualTextureEffect_Blend PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 293: DualTextureEffect (3D stock effect) honors GraphicsDevice.SamplerStates[0] on
        # Vulkan too — reuses the backend-agnostic EasyGL test source (public XNA API only).
        cna_vulkan_test(cna_test_vulkan_sampler_state_effect
                        examples/easygl_sampler_state_effect_test.cpp)
        add_test(NAME Vulkan_SamplerState_DualTextureEffect
                 COMMAND cna_test_vulkan_sampler_state_effect)
        set_tests_properties(Vulkan_SamplerState_DualTextureEffect PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 294: TextureAddressMode::Clamp on a 3D stock effect on Vulkan too (reuses the
        # backend-agnostic EasyGL test source).
        cna_vulkan_test(cna_test_vulkan_texture_address_mode_clamp_effect
                        examples/easygl_texture_address_mode_clamp_effect_test.cpp)
        add_test(NAME Vulkan_TextureAddressMode_Clamp_DualTextureEffect
                 COMMAND cna_test_vulkan_texture_address_mode_clamp_effect)
        set_tests_properties(Vulkan_TextureAddressMode_Clamp_DualTextureEffect PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 296: TextureAddressMode::Mirror on a 3D stock effect on Vulkan too (reuses the
        # backend-agnostic EasyGL test source).
        cna_vulkan_test(cna_test_vulkan_texture_address_mode_mirror_effect
                        examples/easygl_texture_address_mode_mirror_effect_test.cpp)
        add_test(NAME Vulkan_TextureAddressMode_Mirror_DualTextureEffect
                 COMMAND cna_test_vulkan_texture_address_mode_mirror_effect)
        set_tests_properties(Vulkan_TextureAddressMode_Mirror_DualTextureEffect PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 297: TextureFilter::Point vs Linear on Vulkan too (reuses the backend-agnostic
        # EasyGL test source).
        cna_vulkan_test(cna_test_vulkan_texture_filter_point_vs_linear
                        examples/easygl_texture_filter_point_vs_linear_test.cpp)
        add_test(NAME Vulkan_TextureFilter_PointVsLinear
                 COMMAND cna_test_vulkan_texture_filter_point_vs_linear)
        set_tests_properties(Vulkan_TextureFilter_PointVsLinear PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 298: now registered on Vulkan too -- a Vulkan-specific adaptation of the EasyGL
        # source (can't reuse it verbatim: Vulkan's TextureFilter::Point mapping has always been
        # mip-aware, unlike EasyGL's still-flat mapping, so the expected result for that one check
        # differs -- see the test's own file-header comment). Task 925 fixed the confound noted
        # here previously (Texture2D::SetData(level>0) was a silent no-op on Vulkan,
        # UpdatePixelsLevel never overridden; mipLevels/levelCount were also both hardcoded to 1).
        cna_vulkan_test(cna_test_vulkan_texture_mip_filter_effect
                        examples/vulkan_texture_mip_filter_effect_test.cpp)
        add_test(NAME Vulkan_TextureMipFilter_DualTextureEffect
                 COMMAND cna_test_vulkan_texture_mip_filter_effect)
        set_tests_properties(Vulkan_TextureMipFilter_DualTextureEffect PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 299: anisotropic filtering caps and fallback on Vulkan too (reuses the
        # backend-agnostic EasyGL test source). This is the one backend where MaxAnisotropy is
        # actually respected (queried/clamped to the real device cap).
        cna_vulkan_test(cna_test_vulkan_texture_anisotropic_effect
                        examples/easygl_texture_anisotropic_effect_test.cpp)
        add_test(NAME Vulkan_TextureAnisotropic_DualTextureEffect
                 COMMAND cna_test_vulkan_texture_anisotropic_effect)
        set_tests_properties(Vulkan_TextureAnisotropic_DualTextureEffect PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 303: BlendState::Opaque on Vulkan too (reuses the backend-agnostic EasyGL source)
        cna_vulkan_test(cna_test_vulkan_blendstate_opaque
                        examples/easygl_blendstate_opaque_test.cpp)
        add_test(NAME Vulkan_BlendState_Opaque
                 COMMAND cna_test_vulkan_blendstate_opaque)
        set_tests_properties(Vulkan_BlendState_Opaque PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 304: BlendState::AlphaBlend on Vulkan too (reuses the backend-agnostic EasyGL source)
        cna_vulkan_test(cna_test_vulkan_blendstate_alphablend
                        examples/easygl_blendstate_alphablend_test.cpp)
        add_test(NAME Vulkan_BlendState_AlphaBlend
                 COMMAND cna_test_vulkan_blendstate_alphablend)
        set_tests_properties(Vulkan_BlendState_AlphaBlend PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 305: BlendState::NonPremultiplied on Vulkan too (reuses the backend-agnostic
        # EasyGL source). NOTE: a passing result here does NOT mean Task 868 is fixed — see the
        # test source's file-header comment for why.
        cna_vulkan_test(cna_test_vulkan_blendstate_nonpremultiplied
                        examples/easygl_blendstate_nonpremultiplied_test.cpp)
        add_test(NAME Vulkan_BlendState_NonPremultiplied
                 COMMAND cna_test_vulkan_blendstate_nonpremultiplied)
        set_tests_properties(Vulkan_BlendState_NonPremultiplied PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 306: BlendState::Additive on Vulkan too (reuses the backend-agnostic EasyGL source).
        # NOTE: expected to genuinely re-expose Task 868 here, unlike Task 305's coincidental pass.
        cna_vulkan_test(cna_test_vulkan_blendstate_additive
                        examples/easygl_blendstate_additive_test.cpp)
        add_test(NAME Vulkan_BlendState_Additive
                 COMMAND cna_test_vulkan_blendstate_additive)
        set_tests_properties(Vulkan_BlendState_Additive PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 307: ColorBlendFunction/AlphaBlendFunction independence on Vulkan too (reuses the
        # backend-agnostic EasyGL source). NOTE: expect Check A (Subtract) to fail per Task 868 -
        # Vulkan always hardcodes VK_BLEND_OP_ADD.
        cna_vulkan_test(cna_test_vulkan_blendstate_separate_functions
                        examples/easygl_blendstate_separate_functions_test.cpp)
        add_test(NAME Vulkan_BlendState_SeparateFunctions
                 COMMAND cna_test_vulkan_blendstate_separate_functions)
        set_tests_properties(Vulkan_BlendState_SeparateFunctions PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 308: ColorSourceBlend/ColorDestinationBlend independence on Vulkan too (reuses the
        # backend-agnostic EasyGL source). NOTE: expect Check A to coincidentally pass and Check B
        # to fail per Task 868 - Vulkan always hardcodes SourceAlpha/InverseSourceAlpha for colour.
        cna_vulkan_test(cna_test_vulkan_blendstate_separate_factors
                        examples/easygl_blendstate_separate_factors_test.cpp)
        add_test(NAME Vulkan_BlendState_SeparateFactors
                 COMMAND cna_test_vulkan_blendstate_separate_factors)
        set_tests_properties(Vulkan_BlendState_SeparateFactors PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 309: BlendState.BlendFactor propagation (reuses the backend-agnostic EasyGL source).
        # NOTE: expected to FAIL on Vulkan per Task 868 - ApplyBlendState hardcodes a single blend
        # equation that never selects VK_BLEND_FACTOR_CONSTANT_COLOR, so Blend::BlendFactor is moot
        # here regardless of the Task 309 propagation fix. Kept registered as a further documented
        # confirmation of Task 868, not a new bug.
        cna_vulkan_test(cna_test_vulkan_blendstate_blendfactor
                        examples/easygl_blendstate_blendfactor_test.cpp)
        add_test(NAME Vulkan_BlendState_BlendFactor
                 COMMAND cna_test_vulkan_blendstate_blendfactor)
        set_tests_properties(Vulkan_BlendState_BlendFactor PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 313: DepthStencilState.DepthBufferWriteEnable on Vulkan (reuses the backend-agnostic
        # EasyGL source).
        cna_vulkan_test(cna_test_vulkan_depthstencilstate_write_enable
                        examples/easygl_depthstencilstate_write_enable_test.cpp)
        add_test(NAME Vulkan_DepthStencilState_WriteEnable
                 COMMAND cna_test_vulkan_depthstencilstate_write_enable)
        set_tests_properties(Vulkan_DepthStencilState_WriteEnable PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 314: DepthStencilState.DepthBufferFunction on Vulkan (reuses the backend-agnostic
        # EasyGL source). NOTE: expected to FAIL some/all checks per Task 870 - Vulkan hardcodes
        # depthCompareOp per pipeline, ignoring DepthBufferFunction entirely.
        cna_vulkan_test(cna_test_vulkan_depthstencilstate_compare_function
                        examples/easygl_depthstencilstate_compare_function_test.cpp)
        add_test(NAME Vulkan_DepthStencilState_CompareFunction
                 COMMAND cna_test_vulkan_depthstencilstate_compare_function)
        set_tests_properties(Vulkan_DepthStencilState_CompareFunction PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 315: DepthStencilState.StencilEnable on Vulkan (reuses the backend-agnostic EasyGL
        # source). NOTE: expected to FAIL Check A per Task 870 - stencil testing is completely
        # non-functional on Vulkan (stencilTestEnable never set), so no gating ever happens.
        cna_vulkan_test(cna_test_vulkan_depthstencilstate_stencil_enable
                        examples/easygl_depthstencilstate_stencil_enable_test.cpp)
        add_test(NAME Vulkan_DepthStencilState_StencilEnable
                 COMMAND cna_test_vulkan_depthstencilstate_stencil_enable)
        set_tests_properties(Vulkan_DepthStencilState_StencilEnable PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 316: DepthStencilState.StencilMask/StencilWriteMask on Vulkan (reuses the
        # backend-agnostic EasyGL source). NOTE: expected to partially fail per Task 870 - stencil
        # testing never gates on Vulkan, so 2 of 4 checks pass only by coincidence.
        cna_vulkan_test(cna_test_vulkan_depthstencilstate_stencil_mask
                        examples/easygl_depthstencilstate_stencil_mask_test.cpp)
        add_test(NAME Vulkan_DepthStencilState_StencilMask
                 COMMAND cna_test_vulkan_depthstencilstate_stencil_mask)
        set_tests_properties(Vulkan_DepthStencilState_StencilMask PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 317: DepthStencilState front-face stencil operations on Vulkan (reuses the
        # backend-agnostic EasyGL source). NOTE: expected to FAIL all 3 checks per Task 870 -
        # stencil testing never gates on Vulkan, so no operation ever fires.
        cna_vulkan_test(cna_test_vulkan_depthstencilstate_stencil_ops
                        examples/easygl_depthstencilstate_stencil_ops_test.cpp)
        add_test(NAME Vulkan_DepthStencilState_StencilOps
                 COMMAND cna_test_vulkan_depthstencilstate_stencil_ops)
        set_tests_properties(Vulkan_DepthStencilState_StencilOps PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 318: DepthStencilState.TwoSidedStencilMode on Vulkan (reuses the backend-agnostic
        # EasyGL source). NOTE: expected to FAIL the contrast check per Task 870 - stencil testing
        # never gates on Vulkan.
        cna_vulkan_test(cna_test_vulkan_depthstencilstate_stencil_twosided
                        examples/easygl_depthstencilstate_stencil_twosided_test.cpp)
        add_test(NAME Vulkan_DepthStencilState_StencilTwoSided
                 COMMAND cna_test_vulkan_depthstencilstate_stencil_twosided)
        set_tests_properties(Vulkan_DepthStencilState_StencilTwoSided PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 319: GraphicsDevice.ReferenceStencil independent-override behavior on Vulkan (Task
        # 872, universal gap, reuses the backend-agnostic EasyGL source).
        cna_vulkan_test(cna_test_vulkan_graphicsdevice_reference_stencil
                        examples/easygl_graphicsdevice_reference_stencil_test.cpp)
        add_test(NAME Vulkan_GraphicsDevice_ReferenceStencil
                 COMMAND cna_test_vulkan_graphicsdevice_reference_stencil)
        set_tests_properties(Vulkan_GraphicsDevice_ReferenceStencil PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 323 (+ 324/325 coverage): RasterizerState.CullMode on Vulkan (reuses the
        # backend-agnostic EasyGL source).
        cna_vulkan_test(cna_test_vulkan_rasterizerstate_cullmode
                        examples/easygl_rasterizerstate_cullmode_test.cpp)
        add_test(NAME Vulkan_RasterizerState_CullMode
                 COMMAND cna_test_vulkan_rasterizerstate_cullmode)
        set_tests_properties(Vulkan_RasterizerState_CullMode PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 136: EnvironmentMapEffect — emissive=(1,0,0), envAmount=0 → red pixel on Vulkan
        cna_vulkan_test(cna_test_vulkan_env_map
                        examples/vulkan_env_map_test.cpp)
        add_test(NAME Vulkan_EnvironmentMapEffect_Readback
                 COMMAND cna_test_vulkan_env_map)
        set_tests_properties(Vulkan_EnvironmentMapEffect_Readback PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 393: EnvironmentMapEffect EnvironmentMapAmount=0 should ignore the cube map
        cna_vulkan_test(cna_test_vulkan_environmentmapeffect_amount_zero
                        examples/vulkan_environmentmapeffect_amount_zero_test.cpp)
        add_test(NAME Vulkan_EnvironmentMapEffect_AmountZero
                 COMMAND cna_test_vulkan_environmentmapeffect_amount_zero)
        set_tests_properties(Vulkan_EnvironmentMapEffect_AmountZero PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 394: EnvironmentMapEffect EnvironmentMapAmount=1 should fully replace lit color
        cna_vulkan_test(cna_test_vulkan_environmentmapeffect_amount_one
                        examples/vulkan_environmentmapeffect_amount_one_test.cpp)
        add_test(NAME Vulkan_EnvironmentMapEffect_AmountOne
                 COMMAND cna_test_vulkan_environmentmapeffect_amount_one)
        set_tests_properties(Vulkan_EnvironmentMapEffect_AmountOne PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 395: EnvironmentMapEffect EnvironmentMapSpecular should scale by cubemap alpha
        cna_vulkan_test(cna_test_vulkan_environmentmapeffect_specular
                        examples/vulkan_environmentmapeffect_specular_test.cpp)
        add_test(NAME Vulkan_EnvironmentMapEffect_Specular
                 COMMAND cna_test_vulkan_environmentmapeffect_specular)
        set_tests_properties(Vulkan_EnvironmentMapEffect_Specular PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 396: EnvironmentMapEffect Fresnel edge-weighting should suppress head-on reflections
        cna_vulkan_test(cna_test_vulkan_environmentmapeffect_fresnel
                        examples/vulkan_environmentmapeffect_fresnel_test.cpp)
        add_test(NAME Vulkan_EnvironmentMapEffect_Fresnel
                 COMMAND cna_test_vulkan_environmentmapeffect_fresnel)
        set_tests_properties(Vulkan_EnvironmentMapEffect_Fresnel PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 397: EnvironmentMapEffect reflection vector should respond to EyePosition
        cna_vulkan_test(cna_test_vulkan_environmentmapeffect_eyeposition
                        examples/vulkan_environmentmapeffect_eyeposition_test.cpp)
        add_test(NAME Vulkan_EnvironmentMapEffect_EyePosition
                 COMMAND cna_test_vulkan_environmentmapeffect_eyeposition)
        set_tests_properties(Vulkan_EnvironmentMapEffect_EyePosition PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 398: EnvironmentMapEffect normal transform should use inverse-transpose of World
        cna_vulkan_test(cna_test_vulkan_environmentmapeffect_worldtransform
                        examples/vulkan_environmentmapeffect_worldtransform_test.cpp)
        add_test(NAME Vulkan_EnvironmentMapEffect_WorldTransform
                 COMMAND cna_test_vulkan_environmentmapeffect_worldtransform)
        set_tests_properties(Vulkan_EnvironmentMapEffect_WorldTransform PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 399: cross-backend EnvironmentMapEffect capstone (Tasks 393-398 combined)
        cna_vulkan_test(cna_test_vulkan_environmentmapeffect_combined
                        examples/vulkan_environmentmapeffect_combined_test.cpp)
        add_test(NAME Vulkan_EnvironmentMapEffect_Combined
                 COMMAND cna_test_vulkan_environmentmapeffect_combined)
        set_tests_properties(Vulkan_EnvironmentMapEffect_Combined PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 406: SkinnedEffect identity bone palette should produce zero deformation
        cna_vulkan_test(cna_test_vulkan_skinnedeffect_identity_bones
                        examples/vulkan_skinnedeffect_identity_bones_test.cpp)
        add_test(NAME Vulkan_SkinnedEffect_IdentityBones
                 COMMAND cna_test_vulkan_skinnedeffect_identity_bones)
        set_tests_properties(Vulkan_SkinnedEffect_IdentityBones PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 407: SkinnedEffect single translation bone should shift the mesh
        cna_vulkan_test(cna_test_vulkan_skinnedeffect_translation_bone
                        examples/vulkan_skinnedeffect_translation_bone_test.cpp)
        add_test(NAME Vulkan_SkinnedEffect_TranslationBone
                 COMMAND cna_test_vulkan_skinnedeffect_translation_bone)
        set_tests_properties(Vulkan_SkinnedEffect_TranslationBone PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 408: SkinnedEffect two-bone weighted blend should produce midpoint deformation
        cna_vulkan_test(cna_test_vulkan_skinnedeffect_twobone_blend
                        examples/vulkan_skinnedeffect_twobone_blend_test.cpp)
        add_test(NAME Vulkan_SkinnedEffect_TwoBoneBlend
                 COMMAND cna_test_vulkan_skinnedeffect_twobone_blend)
        set_tests_properties(Vulkan_SkinnedEffect_TwoBoneBlend PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 409: cross-backend SkinnedEffect capstone (identity + single-bone + two-bone blend)
        cna_vulkan_test(cna_test_vulkan_skinnedeffect_combined
                        examples/vulkan_skinnedeffect_combined_test.cpp)
        add_test(NAME Vulkan_SkinnedEffect_Combined
                 COMMAND cna_test_vulkan_skinnedeffect_combined)
        set_tests_properties(Vulkan_SkinnedEffect_Combined PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 334: RenderTargetCube sampled as TextureCube via EnvironmentMapEffect after unbinding
        cna_vulkan_test(cna_test_vulkan_rendertargetcube_sample
                        examples/vulkan_rendertargetcube_sample_test.cpp)
        add_test(NAME Vulkan_RenderTargetCube_SampleAfterUnbind
                 COMMAND cna_test_vulkan_rendertargetcube_sample)
        set_tests_properties(Vulkan_RenderTargetCube_SampleAfterUnbind PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 907: RenderTargetCube mip chains are genuinely generated per-face
        # (vkCmdBlitImage cascade), split out of Task 878's original combined scope.
        cna_vulkan_test(cna_test_vulkan_rendertargetcube_mip
                        examples/vulkan_rendertargetcube_mip_test.cpp)
        add_test(NAME Vulkan_RenderTargetCube_MipChain
                 COMMAND cna_test_vulkan_rendertargetcube_mip)
        set_tests_properties(Vulkan_RenderTargetCube_MipChain PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 903: RenderTargetCube MSAA is genuinely generated per-face (shared MSAA colour
        # image + promoted MSAA depth + 3-attachment render pass), split out of Task 878/879's
        # original RenderTarget2D-only scope.
        cna_vulkan_test(cna_test_vulkan_rendertargetcube_msaa
                        examples/vulkan_rendertargetcube_msaa_test.cpp)
        add_test(NAME Vulkan_RenderTargetCube_MsaaResolve
                 COMMAND cna_test_vulkan_rendertargetcube_msaa)
        set_tests_properties(Vulkan_RenderTargetCube_MsaaResolve PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 148: RenderTarget2D full cycle — red quad into RT, blit to backbuffer
        cna_vulkan_test(cna_test_vulkan_rt2d
                        examples/vulkan_rt2d_test.cpp)
        add_test(NAME Vulkan_RenderTarget2D_FullCycle
                 COMMAND cna_test_vulkan_rt2d)
        set_tests_properties(Vulkan_RenderTarget2D_FullCycle PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 875: Clear()-only render target (no draw call) actually records a render pass
        cna_vulkan_test(cna_test_vulkan_rt_roundtrip
                        examples/vulkan_rt_roundtrip_test.cpp)
        add_test(NAME Vulkan_RenderTarget2D_ClearOnlyRoundtrip
                 COMMAND cna_test_vulkan_rt_roundtrip)
        set_tests_properties(Vulkan_RenderTarget2D_ClearOnlyRoundtrip PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 335: RenderTarget2D depth buffer is real and functional (not just a stored property)
        cna_vulkan_test(cna_test_vulkan_rendertarget2d_depth
                        examples/rendertarget2d_depth_test.cpp)
        add_test(NAME Vulkan_RenderTarget2D_DepthBuffer
                 COMMAND cna_test_vulkan_rendertarget2d_depth)
        set_tests_properties(Vulkan_RenderTarget2D_DepthBuffer PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 865: Texture3D::GetData is now real on Vulkan -- shared with EasyGL's registration
        # of the same backend-agnostic file (Task 173).
        cna_vulkan_test(cna_test_vulkan_texture3d_slices
                        examples/easygl_texture3d_slices_test.cpp)
        add_test(NAME Vulkan_Texture3D_Slices_RoundTrip
                 COMMAND cna_test_vulkan_texture3d_slices)
        set_tests_properties(Vulkan_Texture3D_Slices_RoundTrip PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 865: TextureCube::GetData is now real on Vulkan -- shared with EasyGL's
        # registration of the same backend-agnostic file (Task 275).
        cna_vulkan_test(cna_test_vulkan_texturecube_partial_rect
                        examples/easygl_texturecube_partial_rect_test.cpp)
        add_test(NAME Vulkan_TextureCube_PartialRect_RoundTrip
                 COMMAND cna_test_vulkan_texturecube_partial_rect)
        set_tests_properties(Vulkan_TextureCube_PartialRect_RoundTrip PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 864: Texture3D mip-level allocation is now real on Vulkan -- shared with EasyGL's
        # registration of the same backend-agnostic file (Task 862).
        cna_vulkan_test(cna_test_vulkan_texture3d_mip
                        examples/easygl_texture3d_mip_test.cpp)
        add_test(NAME Vulkan_Texture3D_Mip_RoundTrip
                 COMMAND cna_test_vulkan_texture3d_mip)
        set_tests_properties(Vulkan_Texture3D_Mip_RoundTrip PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 864: TextureCube mip-level allocation is now real on Vulkan -- shared with EasyGL's
        # registration of the same backend-agnostic file (Task 276).
        cna_vulkan_test(cna_test_vulkan_texturecube_mip
                        examples/easygl_texturecube_mip_test.cpp)
        add_test(NAME Vulkan_TextureCube_Mip_RoundTrip
                 COMMAND cna_test_vulkan_texturecube_mip)
        set_tests_properties(Vulkan_TextureCube_Mip_RoundTrip PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 178: RenderTargetUsage DiscardContents vs PreserveContents
        cna_vulkan_test(cna_test_vulkan_render_target_usage
                        examples/vulkan_render_target_usage_test.cpp)
        add_test(NAME Vulkan_RenderTargetUsage
                 COMMAND cna_test_vulkan_render_target_usage)
        set_tests_properties(Vulkan_RenderTargetUsage PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 142: RenderTargetCube — 6-face per-face render passes
        cna_vulkan_test(cna_test_vulkan_rtcube
                        examples/vulkan_rtcube_test.cpp)
        add_test(NAME Vulkan_RenderTargetCube_PerFace
                 COMMAND cna_test_vulkan_rtcube)
        set_tests_properties(Vulkan_RenderTargetCube_PerFace PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 147: MSAA 4× — multisampled swapchain + subpass resolve
        cna_vulkan_test(cna_test_vulkan_msaa
                        examples/vulkan_msaa_test.cpp)
        add_test(NAME Vulkan_MSAA_4x_Readback
                 COMMAND cna_test_vulkan_msaa)
        set_tests_properties(Vulkan_MSAA_4x_Readback PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_vertex_format
                        examples/vulkan_vertex_format_test.cpp)
        add_test(NAME Vulkan_VertexFormat_AllStrides
                 COMMAND cna_test_vulkan_vertex_format)
        set_tests_properties(Vulkan_VertexFormat_AllStrides PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_fill_mode
                        examples/vulkan_fill_mode_test.cpp)
        add_test(NAME Vulkan_FillMode_WireFrame
                 COMMAND cna_test_vulkan_fill_mode)
        set_tests_properties(Vulkan_FillMode_WireFrame PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_depth_bias
                        examples/vulkan_depth_bias_test.cpp)
        add_test(NAME Vulkan_DepthBias
                 COMMAND cna_test_vulkan_depth_bias)
        set_tests_properties(Vulkan_DepthBias PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 329: ScissorTestEnable + ScissorRectangle pixel-readback test
        cna_vulkan_test(cna_test_vulkan_scissor
                        examples/vulkan_scissor_test.cpp)
        add_test(NAME Vulkan_ScissorTest
                 COMMAND cna_test_vulkan_scissor)
        set_tests_properties(Vulkan_ScissorTest PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 664: SpriteBatch multiple Begin()/End() cycles per frame must all render
        cna_vulkan_test(cna_test_vulkan_spritebatch_multi_begin_end
                        examples/vulkan_spritebatch_multi_begin_end_test.cpp)
        add_test(NAME Vulkan_SpriteBatch_MultiBeginEnd
                 COMMAND cna_test_vulkan_spritebatch_multi_begin_end)
        set_tests_properties(Vulkan_SpriteBatch_MultiBeginEnd PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 665: SpriteBatch Begin()'s SamplerState (TextureAddressMode) must take effect
        cna_vulkan_test(cna_test_vulkan_texture_address_mode
                        examples/vulkan_texture_address_mode_test.cpp)
        add_test(NAME Vulkan_TextureAddressMode
                 COMMAND cna_test_vulkan_texture_address_mode)
        set_tests_properties(Vulkan_TextureAddressMode PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 338: SetRenderTarget(nullptr) resets Viewport/ScissorRectangle to the backbuffer
        cna_vulkan_test(cna_test_vulkan_rendertarget_viewport_scissor_reset
                        examples/rendertarget_viewport_scissor_reset_test.cpp)
        add_test(NAME Vulkan_RenderTarget_ViewportScissorReset
                 COMMAND cna_test_vulkan_rendertarget_viewport_scissor_reset)
        set_tests_properties(Vulkan_RenderTarget_ViewportScissorReset PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 349: custom Viewport survives Present() with no resize, resets after a real one.
        cna_vulkan_test(cna_test_vulkan_viewport_reset_after_resize
                        examples/viewport_reset_after_resize_test.cpp)
        add_test(NAME Vulkan_ViewportResetAfterResize
                 COMMAND cna_test_vulkan_viewport_reset_after_resize)
        set_tests_properties(Vulkan_ViewportResetAfterResize PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 331: RenderTarget2D constructor/property audit against FNA.
        cna_vulkan_test(cna_test_vulkan_rendertarget2d_properties
                        examples/easygl_rendertarget2d_properties_test.cpp)
        add_test(NAME Vulkan_RenderTarget2D_Properties
                 COMMAND cna_test_vulkan_rendertarget2d_properties)
        set_tests_properties(Vulkan_RenderTarget2D_Properties PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 332: RenderTargetCube constructor/property audit against FNA.
        cna_vulkan_test(cna_test_vulkan_rendertargetcube_properties
                        examples/easygl_rendertargetcube_properties_test.cpp)
        add_test(NAME Vulkan_RenderTargetCube_Properties
                 COMMAND cna_test_vulkan_rendertargetcube_properties)
        set_tests_properties(Vulkan_RenderTargetCube_Properties PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 364: BasicEffect pixel test — VertexColorEnabled=false, no texture, diffuse color only
        cna_vulkan_test(cna_test_vulkan_basiceffect_vertexcolor_disabled
                        examples/vulkan_basiceffect_vertexcolor_disabled_test.cpp)
        add_test(NAME Vulkan_BasicEffect_VertexColorDisabled
                 COMMAND cna_test_vulkan_basiceffect_vertexcolor_disabled)
        set_tests_properties(Vulkan_BasicEffect_VertexColorDisabled PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 365: BasicEffect pixel test — VertexColorEnabled=true, no texture, vertex color multiplication
        cna_vulkan_test(cna_test_vulkan_basiceffect_vertexcolor_enabled
                        examples/vulkan_basiceffect_vertexcolor_enabled_test.cpp)
        add_test(NAME Vulkan_BasicEffect_VertexColorEnabled
                 COMMAND cna_test_vulkan_basiceffect_vertexcolor_enabled)
        set_tests_properties(Vulkan_BasicEffect_VertexColorEnabled PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 366: BasicEffect pixel test — TextureEnabled=true, no vertex color
        cna_vulkan_test(cna_test_vulkan_basiceffect_texture_enabled
                        examples/vulkan_basiceffect_texture_enabled_test.cpp)
        add_test(NAME Vulkan_BasicEffect_TextureEnabled
                 COMMAND cna_test_vulkan_basiceffect_texture_enabled)
        set_tests_properties(Vulkan_BasicEffect_TextureEnabled PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 904: GetOrCreatePipelineFogTex3D's missing msaa-aware render-pass check.
        cna_vulkan_test(cna_test_vulkan_basiceffect_textured_msaa
                        examples/vulkan_basiceffect_textured_msaa_test.cpp)
        add_test(NAME Vulkan_BasicEffect_TexturedMsaa
                 COMMAND cna_test_vulkan_basiceffect_textured_msaa)
        set_tests_properties(Vulkan_BasicEffect_TexturedMsaa PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 367: BasicEffect pixel test — TextureEnabled=true AND VertexColorEnabled=true
        cna_vulkan_test(cna_test_vulkan_basiceffect_texture_vertexcolor_enabled
                        examples/vulkan_basiceffect_texture_vertexcolor_enabled_test.cpp)
        add_test(NAME Vulkan_BasicEffect_TextureVertexColorEnabled
                 COMMAND cna_test_vulkan_basiceffect_texture_vertexcolor_enabled)
        set_tests_properties(Vulkan_BasicEffect_TextureVertexColorEnabled PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 368: BasicEffect pixel test — LightingEnabled=true, one DirectionalLight
        cna_vulkan_test(cna_test_vulkan_basiceffect_one_light
                        examples/vulkan_basiceffect_one_light_test.cpp)
        add_test(NAME Vulkan_BasicEffect_OneLight
                 COMMAND cna_test_vulkan_basiceffect_one_light)
        set_tests_properties(Vulkan_BasicEffect_OneLight PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 897: BasicEffect pixel test — DirectionalLight1/2 + EmissiveColor on the lit path
        cna_vulkan_test(cna_test_vulkan_basiceffect_multilight_emissive
                        examples/vulkan_basiceffect_multilight_emissive_test.cpp)
        add_test(NAME Vulkan_BasicEffect_MultiLightEmissive
                 COMMAND cna_test_vulkan_basiceffect_multilight_emissive)
        set_tests_properties(Vulkan_BasicEffect_MultiLightEmissive PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 886: BasicEffect pixel test — real specular highlights
        cna_vulkan_test(cna_test_vulkan_basiceffect_specular
                        examples/vulkan_basiceffect_specular_test.cpp)
        add_test(NAME Vulkan_BasicEffect_Specular
                 COMMAND cna_test_vulkan_basiceffect_specular)
        set_tests_properties(Vulkan_BasicEffect_Specular PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 887: AlphaTestEffect.VertexColorEnabled fix verification
        cna_vulkan_test(cna_test_vulkan_alphatest_vertexcolor
                        examples/vulkan_alphatest_vertexcolor_test.cpp)
        add_test(NAME Vulkan_AlphaTest_VertexColor
                 COMMAND cna_test_vulkan_alphatest_vertexcolor)
        set_tests_properties(Vulkan_AlphaTest_VertexColor PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 888: BasicEffect fog on the lit_textured3d (stride-32) pipeline
        cna_vulkan_test(cna_test_vulkan_basiceffect_fog
                        examples/vulkan_basiceffect_fog_test.cpp)
        add_test(NAME Vulkan_BasicEffect_Fog
                 COMMAND cna_test_vulkan_basiceffect_fog)
        set_tests_properties(Vulkan_BasicEffect_Fog PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 888: AlphaTestEffect fog on the alpha_test3d pipeline
        cna_vulkan_test(cna_test_vulkan_alphatest_fog
                        examples/vulkan_alphatest_fog_test.cpp)
        add_test(NAME Vulkan_AlphaTest_Fog
                 COMMAND cna_test_vulkan_alphatest_fog)
        set_tests_properties(Vulkan_AlphaTest_Fog PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 369: BasicEffect pixel test — DiffuseColor+EmissiveColor, LightingEnabled=false
        cna_vulkan_test(cna_test_vulkan_basiceffect_emissive
                        examples/vulkan_basiceffect_emissive_test.cpp)
        add_test(NAME Vulkan_BasicEffect_Emissive
                 COMMAND cna_test_vulkan_basiceffect_emissive)
        set_tests_properties(Vulkan_BasicEffect_Emissive PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 370: BasicEffect cross-backend image comparison suite — closes Phase 42
        cna_vulkan_test(cna_test_vulkan_basiceffect_combined
                        examples/vulkan_basiceffect_combined_test.cpp)
        add_test(NAME Vulkan_BasicEffect_Combined
                 COMMAND cna_test_vulkan_basiceffect_combined)
        set_tests_properties(Vulkan_BasicEffect_Combined PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 374: AlphaTestEffect all CompareFunction modes — threshold sweep
        cna_vulkan_test(cna_test_vulkan_alphatest_comparefunction_sweep
                        examples/vulkan_alphatest_comparefunction_sweep_test.cpp)
        add_test(NAME Vulkan_AlphaTest_CompareFunctionSweep
                 COMMAND cna_test_vulkan_alphatest_comparefunction_sweep)
        set_tests_properties(Vulkan_AlphaTest_CompareFunctionSweep PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 379: AlphaTestEffect null/no-texture behavior
        cna_vulkan_test(cna_test_vulkan_alphatest_null_texture
                        examples/vulkan_alphatest_null_texture_test.cpp)
        add_test(NAME Vulkan_AlphaTest_NullTexture
                 COMMAND cna_test_vulkan_alphatest_null_texture)
        set_tests_properties(Vulkan_AlphaTest_NullTexture PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 383: DualTextureEffect two-texture blend formula, including FNA's color.rgb*=2
        cna_vulkan_test(cna_test_vulkan_dualtextureeffect_doubling
                        examples/vulkan_dualtextureeffect_doubling_test.cpp)
        add_test(NAME Vulkan_DualTextureEffect_Doubling
                 COMMAND cna_test_vulkan_dualtextureeffect_doubling)
        set_tests_properties(Vulkan_DualTextureEffect_Doubling PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 385: DualTextureEffect Alpha forwarding, alpha-channel-only (see file header for
        # why the full RGB-premultiplication check is deliberately not done on Vulkan)
        cna_vulkan_test(cna_test_vulkan_dualtextureeffect_alpha
                        examples/vulkan_dualtextureeffect_alpha_test.cpp)
        add_test(NAME Vulkan_DualTextureEffect_Alpha
                 COMMAND cna_test_vulkan_dualtextureeffect_alpha)
        set_tests_properties(Vulkan_DualTextureEffect_Alpha PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 386: DualTextureEffect first texture (Texture, slot 0) null behavior
        cna_vulkan_test(cna_test_vulkan_dualtextureeffect_null_texture0
                        examples/vulkan_dualtextureeffect_null_texture0_test.cpp)
        add_test(NAME Vulkan_DualTextureEffect_NullTexture0
                 COMMAND cna_test_vulkan_dualtextureeffect_null_texture0)
        set_tests_properties(Vulkan_DualTextureEffect_NullTexture0 PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 387: DualTextureEffect second texture (Texture2, slot 1) null behavior
        cna_vulkan_test(cna_test_vulkan_dualtextureeffect_null_texture2
                        examples/vulkan_dualtextureeffect_null_texture2_test.cpp)
        add_test(NAME Vulkan_DualTextureEffect_NullTexture2
                 COMMAND cna_test_vulkan_dualtextureeffect_null_texture2)
        set_tests_properties(Vulkan_DualTextureEffect_NullTexture2 PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 389: DualTextureEffect cross-backend image comparison suite (closes Phase 44 pixel work)
        cna_vulkan_test(cna_test_vulkan_dualtextureeffect_combined
                        examples/vulkan_dualtextureeffect_combined_test.cpp)
        add_test(NAME Vulkan_DualTextureEffect_Combined
                 COMMAND cna_test_vulkan_dualtextureeffect_combined)
        set_tests_properties(Vulkan_DualTextureEffect_Combined PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 880: GraphicsDevice.Viewport GPU wiring — sub-region viewport pixel-readback test
        cna_vulkan_test(cna_test_vulkan_viewport_subregion
                        examples/vulkan_viewport_subregion_test.cpp)
        add_test(NAME Vulkan_Viewport_Subregion
                 COMMAND cna_test_vulkan_viewport_subregion)
        set_tests_properties(Vulkan_Viewport_Subregion PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 899: fog on colored3d/textured3d/colored_textured3d/dual_texture3d/skinned3d
        cna_vulkan_test(cna_test_vulkan_basiceffect_colored3d_fog
                        examples/vulkan_basiceffect_colored3d_fog_test.cpp)
        add_test(NAME Vulkan_BasicEffect_Colored3D_Fog
                 COMMAND cna_test_vulkan_basiceffect_colored3d_fog)
        set_tests_properties(Vulkan_BasicEffect_Colored3D_Fog PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_basiceffect_textured3d_fog
                        examples/vulkan_basiceffect_textured3d_fog_test.cpp)
        add_test(NAME Vulkan_BasicEffect_Textured3D_Fog
                 COMMAND cna_test_vulkan_basiceffect_textured3d_fog)
        set_tests_properties(Vulkan_BasicEffect_Textured3D_Fog PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_basiceffect_coloredtextured3d_fog
                        examples/vulkan_basiceffect_coloredtextured3d_fog_test.cpp)
        add_test(NAME Vulkan_BasicEffect_ColoredTextured3D_Fog
                 COMMAND cna_test_vulkan_basiceffect_coloredtextured3d_fog)
        set_tests_properties(Vulkan_BasicEffect_ColoredTextured3D_Fog PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_dualtextureeffect_fog
                        examples/vulkan_dualtextureeffect_fog_test.cpp)
        add_test(NAME Vulkan_DualTextureEffect_Fog
                 COMMAND cna_test_vulkan_dualtextureeffect_fog)
        set_tests_properties(Vulkan_DualTextureEffect_Fog PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        cna_vulkan_test(cna_test_vulkan_skinnedeffect_fog
                        examples/vulkan_skinnedeffect_fog_test.cpp)
        add_test(NAME Vulkan_SkinnedEffect_Fog
                 COMMAND cna_test_vulkan_skinnedeffect_fog)
        set_tests_properties(Vulkan_SkinnedEffect_Fog PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 899's own noted cheap leftover: env_map3d fog packed into EnvMapParams' spare bytes
        cna_vulkan_test(cna_test_vulkan_environmentmapeffect_fog
                        examples/vulkan_environmentmapeffect_fog_test.cpp)
        add_test(NAME Vulkan_EnvironmentMapEffect_Fog
                 COMMAND cna_test_vulkan_environmentmapeffect_fog)
        set_tests_properties(Vulkan_EnvironmentMapEffect_Fog PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 878/879: RenderTarget2D MSAA resolve genuinely anti-aliases on Vulkan (piggybacks
        # on the backend's own backbuffer sampleCount_ -- requires PreferMultiSampling=true).
        cna_vulkan_test(cna_test_vulkan_rendertarget2d_msaa
                        examples/vulkan_rendertarget2d_msaa_test.cpp)
        add_test(NAME Vulkan_RenderTarget2D_MsaaResolve
                 COMMAND cna_test_vulkan_rendertarget2d_msaa)
        set_tests_properties(Vulkan_RenderTarget2D_MsaaResolve PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 878: RenderTarget2D mip chains are genuinely generated (vkCmdBlitImage cascade),
        # not just present-but-undefined storage.
        cna_vulkan_test(cna_test_vulkan_rendertarget2d_mip
                        examples/vulkan_rendertarget2d_mip_test.cpp)
        add_test(NAME Vulkan_RenderTarget2D_MipChain
                 COMMAND cna_test_vulkan_rendertarget2d_mip)
        set_tests_properties(Vulkan_RenderTarget2D_MipChain PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 911: RenderTarget2D per-instance DepthStencilFormat fidelity -- DepthFormat::None
        # genuinely has no depth buffer, Depth24Stencil8/Depth16 genuinely do (each with its own
        # render pass), all three coexisting correctly in a single frame.
        cna_vulkan_test(cna_test_vulkan_rendertarget_depthformat_fidelity
                        examples/vulkan_rendertarget_depthformat_fidelity_test.cpp)
        add_test(NAME Vulkan_RenderTarget2D_DepthFormatFidelity
                 COMMAND cna_test_vulkan_rendertarget_depthformat_fidelity)
        set_tests_properties(Vulkan_RenderTarget2D_DepthFormatFidelity PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

        # Task 13.6: the same 3 avatar-rendering GPU integration tests already registered for
        # EasyGL above, reusing the exact same backend-agnostic source files (public
        # GraphicsDevice/SkinnedModelEXT/AvatarRenderer API only) - Vulkan's own dedicated
        # GetOrCreatePipelineSkinned3D pipeline (Task 11.10) had never been run against real (or
        # even synthetic) avatar content before now.
        if(CNA_ENABLE_NET)
            cna_vulkan_test(cna_test_vulkan_avatar_real_render
                            examples/avatar_real_render_integration_test.cpp)
            target_link_libraries(cna_test_vulkan_avatar_real_render PRIVATE CNA_GamerServices)
            add_test(NAME Vulkan_AvatarRenderer_RealRender
                     COMMAND cna_test_vulkan_avatar_real_render)
            set_tests_properties(Vulkan_AvatarRenderer_RealRender PROPERTIES
                TIMEOUT 30
                ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

            cna_vulkan_test(cna_test_vulkan_avatar_attach_part
                            examples/avatar_attach_part_integration_test.cpp)
            target_link_libraries(cna_test_vulkan_avatar_attach_part PRIVATE CNA_GamerServices)
            add_test(NAME Vulkan_AvatarRenderer_AttachPart
                     COMMAND cna_test_vulkan_avatar_attach_part)
            set_tests_properties(Vulkan_AvatarRenderer_AttachPart PROPERTIES
                TIMEOUT 30
                ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

            cna_vulkan_test(cna_test_vulkan_avatar_tint_routing
                            examples/avatar_tint_routing_integration_test.cpp)
            target_link_libraries(cna_test_vulkan_avatar_tint_routing PRIVATE CNA_GamerServices)
            add_test(NAME Vulkan_AvatarRenderer_TintRouting
                     COMMAND cna_test_vulkan_avatar_tint_routing)
            set_tests_properties(Vulkan_AvatarRenderer_TintRouting PROPERTIES
                TIMEOUT 30
                ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
        endif()

    endif()

endif()

# SDL_Renderer integration tests (Task 666 audit phase)
if(CNA_BUILD_TESTS AND NOT EMSCRIPTEN AND NOT WIN32
   AND CNA_GRAPHICS_BACKEND STREQUAL "SDL_RENDERER")

    enable_testing()

    # --- helper macro: build a headless SDL_Renderer test exe --------------------
    macro(cna_sdl_test target src)
        add_executable(${target} ${src})
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(${target} PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(${target} PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(${target} PRIVATE SDL3::SDL3main)
        endif()
    endmacro()

    # Task 915: GetBackBufferData readback -- foundational prerequisite for the whole
    # SDL_Renderer pixel-test audit phase (Tasks 666-861).
    cna_sdl_test(cna_test_sdl_readback
                 examples/sdlrenderer_readback_test.cpp)
    add_test(NAME SDL_Renderer_Readback
             COMMAND cna_test_sdl_readback)
    set_tests_properties(SDL_Renderer_Readback PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 666: audit all SpriteBatch::Draw overloads through SDL_Renderer
    cna_sdl_test(cna_test_sdl_spritebatch_overloads
                 examples/sdlrenderer_spritebatch_overloads_test.cpp)
    add_test(NAME SDL_Renderer_SpriteBatch_Overloads
             COMMAND cna_test_sdl_spritebatch_overloads)
    set_tests_properties(SDL_Renderer_SpriteBatch_Overloads PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 671: rotation around origin -- found and fixed a real SDL_RenderTextureRotated
    # pivot-offset bug while writing this test.
    cna_sdl_test(cna_test_sdl_spritebatch_rotation
                 examples/sdlrenderer_spritebatch_rotation_test.cpp)
    add_test(NAME SDL_Renderer_SpriteBatch_Rotation
             COMMAND cna_test_sdl_spritebatch_rotation)
    set_tests_properties(SDL_Renderer_SpriteBatch_Rotation PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 669: SpriteSortMode::FrontToBack layerDepth ordering on SDL_Renderer -- direct port of
    # Task 420's EasyGL test (PresentationMode::NativeBackBuffer required for exact-pixel readback,
    # see Task 915).
    cna_sdl_test(cna_test_sdl_spritebatch_layerdepth
                 examples/sdlrenderer_spritebatch_layerdepth_test.cpp)
    add_test(NAME SDL_Renderer_SpriteBatch_LayerDepth
             COMMAND cna_test_sdl_spritebatch_layerdepth)
    set_tests_properties(SDL_Renderer_SpriteBatch_LayerDepth PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 667: SpriteSortMode::Deferred submission-order pixel test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_spritebatch_deferred_order
                 examples/sdlrenderer_spritebatch_deferred_order_test.cpp)
    add_test(NAME SDL_Renderer_SpriteBatch_DeferredOrder
             COMMAND cna_test_sdl_spritebatch_deferred_order)
    set_tests_properties(SDL_Renderer_SpriteBatch_DeferredOrder PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 668: SpriteSortMode::Texture grouping pixel test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_spritebatch_texture_sort
                 examples/sdlrenderer_spritebatch_texture_sort_test.cpp)
    add_test(NAME SDL_Renderer_SpriteBatch_TextureSort
             COMMAND cna_test_sdl_spritebatch_texture_sort)
    set_tests_properties(SDL_Renderer_SpriteBatch_TextureSort PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 670: SpriteSortMode::Immediate per-draw flush pixel test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_spritebatch_immediate_flush
                 examples/sdlrenderer_spritebatch_immediate_flush_test.cpp)
    add_test(NAME SDL_Renderer_SpriteBatch_ImmediateFlush
             COMMAND cna_test_sdl_spritebatch_immediate_flush)
    set_tests_properties(SDL_Renderer_SpriteBatch_ImmediateFlush PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 672: scalar/Vector2 scale overload pixel test on SDL_Renderer -- direct port of
    # Task 418's EasyGL test.
    cna_sdl_test(cna_test_sdl_spritebatch_scale
                 examples/sdlrenderer_spritebatch_scale_test.cpp)
    add_test(NAME SDL_Renderer_SpriteBatch_Scale
             COMMAND cna_test_sdl_spritebatch_scale)
    set_tests_properties(SDL_Renderer_SpriteBatch_Scale PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 673: source rectangle cropping pixel test on SDL_Renderer -- direct port of
    # Task 419's EasyGL test.
    cna_sdl_test(cna_test_sdl_spritebatch_sourcerect
                 examples/sdlrenderer_spritebatch_sourcerect_test.cpp)
    add_test(NAME SDL_Renderer_SpriteBatch_SourceRect
             COMMAND cna_test_sdl_spritebatch_sourcerect)
    set_tests_properties(SDL_Renderer_SpriteBatch_SourceRect PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 674: SpriteEffects::FlipHorizontally/FlipVertically pixel test on SDL_Renderer --
    # direct port of Task 167's EasyGL test.
    cna_sdl_test(cna_test_sdl_sprite_effects
                 examples/sdlrenderer_sprite_effects_test.cpp)
    add_test(NAME SDL_Renderer_SpriteEffects
             COMMAND cna_test_sdl_sprite_effects)
    set_tests_properties(SDL_Renderer_SpriteEffects PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 675: transformMatrix in SpriteBatch::Begin pixel test on SDL_Renderer -- direct port
    # of Task 168's EasyGL test.
    cna_sdl_test(cna_test_sdl_transform_matrix
                 examples/sdlrenderer_transform_matrix_test.cpp)
    add_test(NAME SDL_Renderer_TransformMatrix
             COMMAND cna_test_sdl_transform_matrix)
    set_tests_properties(SDL_Renderer_TransformMatrix PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 676: SpriteBatch::Begin(effect) custom-Effect behavior decision -- must throw
    # (no programmable shader stage exists on SDL_Renderer to apply a custom Effect with).
    cna_sdl_test(cna_test_sdl_custom_effect_throws
                 examples/sdlrenderer_custom_effect_throws_test.cpp)
    add_test(NAME SDL_Renderer_CustomEffectThrows
             COMMAND cna_test_sdl_custom_effect_throws)
    set_tests_properties(SDL_Renderer_CustomEffectThrows PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 677: SpriteBatch Begin/End sequencing guard end-to-end integration test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_spritebatch_begin_end_guard
                 examples/sdlrenderer_spritebatch_begin_end_guard_test.cpp)
    add_test(NAME SDL_Renderer_SpriteBatch_BeginEndGuard
             COMMAND cna_test_sdl_spritebatch_begin_end_guard)
    set_tests_properties(SDL_Renderer_SpriteBatch_BeginEndGuard PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 678: Texture2D::SetData/GetData full-array round-trip audit on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_texture2d_setdata_getdata
                 examples/sdlrenderer_texture2d_setdata_getdata_test.cpp)
    add_test(NAME SDL_Renderer_Texture2D_SetDataGetData
             COMMAND cna_test_sdl_texture2d_setdata_getdata)
    set_tests_properties(SDL_Renderer_Texture2D_SetDataGetData PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 679: Texture2D::SetData partial-rectangle region pixel test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_texture2d_partial_rect
                 examples/sdlrenderer_texture2d_partial_rect_test.cpp)
    add_test(NAME SDL_Renderer_Texture2D_PartialRect
             COMMAND cna_test_sdl_texture2d_partial_rect)
    set_tests_properties(SDL_Renderer_Texture2D_PartialRect PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 680: Texture2D::SetData startIndex/elementCount slice pixel test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_texture2d_startindex
                 examples/sdlrenderer_texture2d_startindex_test.cpp)
    add_test(NAME SDL_Renderer_Texture2D_StartIndex
             COMMAND cna_test_sdl_texture2d_startindex)
    set_tests_properties(SDL_Renderer_Texture2D_StartIndex PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 681: Texture2D mip-level SetData/GetData decision test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_texture2d_miplevel_throws
                 examples/sdlrenderer_texture2d_miplevel_throws_test.cpp)
    add_test(NAME SDL_Renderer_Texture2D_MipLevelThrows
             COMMAND cna_test_sdl_texture2d_miplevel_throws)
    set_tests_properties(SDL_Renderer_Texture2D_MipLevelThrows PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 682: Texture2D::FromStream round-trip real-render pixel test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_texture2d_fromstream
                 examples/sdlrenderer_texture2d_fromstream_test.cpp)
    add_test(NAME SDL_Renderer_Texture2D_FromStream
             COMMAND cna_test_sdl_texture2d_fromstream)
    set_tests_properties(SDL_Renderer_Texture2D_FromStream PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 683: SaveAsPng/SaveAsJpeg round-trip real-render pixel test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_texture2d_saveas_roundtrip
                 examples/sdlrenderer_texture2d_saveas_roundtrip_test.cpp)
    add_test(NAME SDL_Renderer_Texture2D_SaveAsRoundTrip
             COMMAND cna_test_sdl_texture2d_saveas_roundtrip)
    set_tests_properties(SDL_Renderer_Texture2D_SaveAsRoundTrip PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 684: NPOT (3x5, 7x11) texture upload+sample pixel test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_npot_texture
                 examples/sdlrenderer_npot_texture_test.cpp)
    add_test(NAME SDL_Renderer_NpotTexture
             COMMAND cna_test_sdl_npot_texture)
    set_tests_properties(SDL_Renderer_NpotTexture PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 685: TextureAddressMode::Clamp via SpriteBatch pixel test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_texture_address_mode_clamp
                 examples/sdlrenderer_texture_address_mode_clamp_test.cpp)
    add_test(NAME SDL_Renderer_TextureAddressModeClamp
             COMMAND cna_test_sdl_texture_address_mode_clamp)
    set_tests_properties(SDL_Renderer_TextureAddressModeClamp PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 688: TextureFilter::Point vs Linear via SDL_ScaleMode pixel test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_texture_filter_point_vs_linear
                 examples/sdlrenderer_texture_filter_point_vs_linear_test.cpp)
    add_test(NAME SDL_Renderer_TextureFilterPointVsLinear
             COMMAND cna_test_sdl_texture_filter_point_vs_linear)
    set_tests_properties(SDL_Renderer_TextureFilterPointVsLinear PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 689: Texture2D::Dispose double-free/lifetime verification on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_texture2d_dispose
                 examples/sdlrenderer_texture2d_dispose_test.cpp)
    add_test(NAME SDL_Renderer_Texture2D_Dispose
             COMMAND cna_test_sdl_texture2d_dispose)
    set_tests_properties(SDL_Renderer_Texture2D_Dispose PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 690: SpriteFont single-glyph-at-known-position pixel test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_spritefont_single_glyph
                 examples/sdlrenderer_spritefont_single_glyph_test.cpp)
    add_test(NAME SDL_Renderer_SpriteFont_SingleGlyph
             COMMAND cna_test_sdl_spritefont_single_glyph)
    set_tests_properties(SDL_Renderer_SpriteFont_SingleGlyph PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 691: SpriteFont multiple-glyphs-with-spacing pixel test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_spritefont_multiglyph_spacing
                 examples/sdlrenderer_spritefont_multiglyph_spacing_test.cpp)
    add_test(NAME SDL_Renderer_SpriteFont_MultiGlyphSpacing
             COMMAND cna_test_sdl_spritefont_multiglyph_spacing)
    set_tests_properties(SDL_Renderer_SpriteFont_MultiGlyphSpacing PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 692: SpriteFont newline-advances-by-lineSpacing pixel test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_spritefont_newline
                 examples/sdlrenderer_spritefont_newline_test.cpp)
    add_test(NAME SDL_Renderer_SpriteFont_Newline
             COMMAND cna_test_sdl_spritefont_newline)
    set_tests_properties(SDL_Renderer_SpriteFont_Newline PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 693: SpriteFont default-character-fallback pixel test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_spritefont_default_char
                 examples/sdlrenderer_spritefont_default_char_test.cpp)
    add_test(NAME SDL_Renderer_SpriteFont_DefaultChar
             COMMAND cna_test_sdl_spritefont_default_char)
    set_tests_properties(SDL_Renderer_SpriteFont_DefaultChar PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 694: SpriteFont SpriteEffects flip + rotation/origin/scale pixel test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_spritefont_effects
                 examples/sdlrenderer_spritefont_effects_test.cpp)
    add_test(NAME SDL_Renderer_SpriteFont_Effects
             COMMAND cna_test_sdl_spritefont_effects)
    set_tests_properties(SDL_Renderer_SpriteFont_Effects PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 695: BlendState -> SDL_BlendMode mapping audit pixel test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_blendstate_audit
                 examples/sdlrenderer_blendstate_audit_test.cpp)
    add_test(NAME SDL_Renderer_BlendState_Audit
             COMMAND cna_test_sdl_blendstate_audit)
    set_tests_properties(SDL_Renderer_BlendState_Audit PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 696: BlendState::Opaque pixel test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_blendstate_opaque
                 examples/sdlrenderer_blendstate_opaque_test.cpp)
    add_test(NAME SDL_Renderer_BlendState_Opaque
             COMMAND cna_test_sdl_blendstate_opaque)
    set_tests_properties(SDL_Renderer_BlendState_Opaque PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 697: BlendState::AlphaBlend premultiplied alpha pixel test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_blendstate_alphablend
                 examples/sdlrenderer_blendstate_alphablend_test.cpp)
    add_test(NAME SDL_Renderer_BlendState_AlphaBlend
             COMMAND cna_test_sdl_blendstate_alphablend)
    set_tests_properties(SDL_Renderer_BlendState_AlphaBlend PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 698: BlendState::NonPremultiplied pixel test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_blendstate_nonpremultiplied
                 examples/sdlrenderer_blendstate_nonpremultiplied_test.cpp)
    add_test(NAME SDL_Renderer_BlendState_NonPremultiplied
             COMMAND cna_test_sdl_blendstate_nonpremultiplied)
    set_tests_properties(SDL_Renderer_BlendState_NonPremultiplied PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 699: BlendState::Additive saturation behavior pixel test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_blendstate_additive
                 examples/sdlrenderer_blendstate_additive_test.cpp)
    add_test(NAME SDL_Renderer_BlendState_Additive
             COMMAND cna_test_sdl_blendstate_additive)
    set_tests_properties(SDL_Renderer_BlendState_Additive PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 700: custom (non-preset) BlendState combination decision/verification on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_blendstate_custom
                 examples/sdlrenderer_blendstate_custom_test.cpp)
    add_test(NAME SDL_Renderer_BlendState_Custom
             COMMAND cna_test_sdl_blendstate_custom)
    set_tests_properties(SDL_Renderer_BlendState_Custom PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 701: SamplerState -> SDL_ScaleMode + address-mode mapping audit pixel test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_samplerstate_filter_audit
                 examples/sdlrenderer_samplerstate_filter_audit_test.cpp)
    add_test(NAME SDL_Renderer_SamplerState_FilterAudit
             COMMAND cna_test_sdl_samplerstate_filter_audit)
    set_tests_properties(SDL_Renderer_SamplerState_FilterAudit PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 702: default SamplerState (LinearClamp) verification pixel test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_samplerstate_default
                 examples/sdlrenderer_samplerstate_default_test.cpp)
    add_test(NAME SDL_Renderer_SamplerState_Default
             COMMAND cna_test_sdl_samplerstate_default)
    set_tests_properties(SDL_Renderer_SamplerState_Default PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 703: per-draw SamplerState changes take effect on the next SpriteBatch::Begin pixel test on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_samplerstate_perdraw_switch
                 examples/sdlrenderer_samplerstate_perdraw_switch_test.cpp)
    add_test(NAME SDL_Renderer_SamplerState_PerDrawSwitch
             COMMAND cna_test_sdl_samplerstate_perdraw_switch)
    set_tests_properties(SDL_Renderer_SamplerState_PerDrawSwitch PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 704: RenderTarget2D construction audit (property wiring + bind/draw/unbind isolation) on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_rendertarget2d_construction
                 examples/sdlrenderer_rendertarget2d_construction_test.cpp)
    add_test(NAME SDL_Renderer_RenderTarget2D_Construction
             COMMAND cna_test_sdl_rendertarget2d_construction)
    set_tests_properties(SDL_Renderer_RenderTarget2D_Construction PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 705: RenderTarget2D sampled as Texture2D via SpriteBatch after unbinding on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_rendertarget2d_sample
                 examples/sdlrenderer_rendertarget2d_sample_test.cpp)
    add_test(NAME SDL_Renderer_RenderTarget2D_SampleAfterUnbind
             COMMAND cna_test_sdl_rendertarget2d_sample)
    set_tests_properties(SDL_Renderer_RenderTarget2D_SampleAfterUnbind PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 706: RenderTargetUsage::DiscardContents vs PreserveContents on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_rendertarget_usage
                 examples/sdlrenderer_rendertarget_usage_test.cpp)
    add_test(NAME SDL_Renderer_RenderTargetUsage
             COMMAND cna_test_sdl_rendertarget_usage)
    set_tests_properties(SDL_Renderer_RenderTargetUsage PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 707: GetBackBufferData reads correct pixels after SetRenderTarget(nullptr) restores the backbuffer on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_getbackbufferdata_after_rt_unbind
                 examples/sdlrenderer_getbackbufferdata_after_rt_unbind_test.cpp)
    add_test(NAME SDL_Renderer_GetBackBufferData_AfterRtUnbind
             COMMAND cna_test_sdl_getbackbufferdata_after_rt_unbind)
    set_tests_properties(SDL_Renderer_GetBackBufferData_AfterRtUnbind PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 708: render-target depth-buffer behavior decision (silently ignore DepthFormat) on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_rendertarget_depth_decision
                 examples/sdlrenderer_rendertarget_depth_decision_test.cpp)
    add_test(NAME SDL_Renderer_RenderTarget_DepthDecision
             COMMAND cna_test_sdl_rendertarget_depth_decision)
    set_tests_properties(SDL_Renderer_RenderTarget_DepthDecision PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 709: MRT (SetRenderTargets with 2+ bindings) throws clearly on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_rendertargets_mrt_throws
                 examples/sdlrenderer_rendertargets_mrt_throws_test.cpp)
    add_test(NAME SDL_Renderer_RenderTargets_MrtThrows
             COMMAND cna_test_sdl_rendertargets_mrt_throws)
    set_tests_properties(SDL_Renderer_RenderTargets_MrtThrows PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 710: Viewport get/set round-trip and Project/Unproject math (2D orthographic case) on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_viewport_project_unproject
                 examples/sdlrenderer_viewport_project_unproject_test.cpp)
    add_test(NAME SDL_Renderer_Viewport_ProjectUnproject
             COMMAND cna_test_sdl_viewport_project_unproject)
    set_tests_properties(SDL_Renderer_Viewport_ProjectUnproject PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 711: backbuffer resize through PresentationParameters on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_presentationparameters_resize
                 examples/sdlrenderer_presentationparameters_resize_test.cpp)
    add_test(NAME SDL_Renderer_PresentationParameters_Resize
             COMMAND cna_test_sdl_presentationparameters_resize)
    set_tests_properties(SDL_Renderer_PresentationParameters_Resize PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 712: fullscreen toggle on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_fullscreen_toggle
                 examples/sdlrenderer_fullscreen_toggle_test.cpp)
    add_test(NAME SDL_Renderer_FullscreenToggle
             COMMAND cna_test_sdl_fullscreen_toggle)
    set_tests_properties(SDL_Renderer_FullscreenToggle PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 713: PresentInterval (vsync) mapping to SDL_SetRenderVSync on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_presentinterval
                 examples/sdlrenderer_presentinterval_test.cpp)
    add_test(NAME SDL_Renderer_PresentInterval
             COMMAND cna_test_sdl_presentinterval)
    set_tests_properties(SDL_Renderer_PresentInterval PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 714: MultiSampleCount decision (accept-and-ignore-with-log) on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_multisamplecount_decision
                 examples/sdlrenderer_multisamplecount_decision_test.cpp)
    add_test(NAME SDL_Renderer_MultiSampleCount_Decision
             COMMAND cna_test_sdl_multisamplecount_decision)
    set_tests_properties(SDL_Renderer_MultiSampleCount_Decision PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 715: DeviceResetting/DeviceReset events fire correctly on SDL_Renderer backbuffer resize.
    cna_sdl_test(cna_test_sdl_devicereset_events
                 examples/sdlrenderer_devicereset_events_test.cpp)
    add_test(NAME SDL_Renderer_DeviceReset_Events
             COMMAND cna_test_sdl_devicereset_events)
    set_tests_properties(SDL_Renderer_DeviceReset_Events PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 716: GraphicsDevice::Clear (all ClearOptions combinations) on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_clearoptions_audit
                 examples/sdlrenderer_clearoptions_audit_test.cpp)
    add_test(NAME SDL_Renderer_ClearOptions_Audit
             COMMAND cna_test_sdl_clearoptions_audit)
    set_tests_properties(SDL_Renderer_ClearOptions_Audit PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 717: disposed-resource guards throw ObjectDisposedException consistently on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_disposed_guards
                 examples/sdlrenderer_disposed_guards_test.cpp)
    add_test(NAME SDL_Renderer_DisposedGuards
             COMMAND cna_test_sdl_disposed_guards)
    set_tests_properties(SDL_Renderer_DisposedGuards PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 718: double-Dispose is safe for every SDL_Renderer-backed resource type.
    cna_sdl_test(cna_test_sdl_double_dispose
                 examples/sdlrenderer_double_dispose_test.cpp)
    add_test(NAME SDL_Renderer_DoubleDispose
             COMMAND cna_test_sdl_double_dispose)
    set_tests_properties(SDL_Renderer_DoubleDispose PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 719: leak-check -- create/dispose 80 SDL_Renderer textures/render-targets,
    # verify GetTrackedResourceCount returns to baseline. Mirrors Task 219.
    cna_sdl_test(cna_test_sdl_resource_leak
                 examples/sdlrenderer_resource_leak_test.cpp)
    add_test(NAME SDL_Renderer_ResourceLeak
             COMMAND cna_test_sdl_resource_leak)
    set_tests_properties(SDL_Renderer_ResourceLeak PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 720: DrawPrimitives/DrawIndexedPrimitives/DrawInstancedPrimitives throw the
    # correct exception type+message on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_drawprimitives_throws
                 examples/sdlrenderer_drawprimitives_throws_test.cpp)
    add_test(NAME SDL_Renderer_DrawPrimitivesThrows
             COMMAND cna_test_sdl_drawprimitives_throws)
    set_tests_properties(SDL_Renderer_DrawPrimitivesThrows PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 721: all 5 DrawUserPrimitives typed + VertexDeclaration overloads throw the
    # correct exception type+message on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_drawuserprimitives_throws
                 examples/sdlrenderer_drawuserprimitives_throws_test.cpp)
    add_test(NAME SDL_Renderer_DrawUserPrimitivesThrows
             COMMAND cna_test_sdl_drawuserprimitives_throws)
    set_tests_properties(SDL_Renderer_DrawUserPrimitivesThrows PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 722: all 10 DrawUserIndexedPrimitives typed + VertexDeclaration overloads throw the
    # correct exception type+message on SDL_Renderer.
    cna_sdl_test(cna_test_sdl_drawuserindexedprimitives_throws
                 examples/sdlrenderer_drawuserindexedprimitives_throws_test.cpp)
    add_test(NAME SDL_Renderer_DrawUserIndexedPrimitivesThrows
             COMMAND cna_test_sdl_drawuserindexedprimitives_throws)
    set_tests_properties(SDL_Renderer_DrawUserIndexedPrimitivesThrows PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 723: re-verify CreateVertexBuffer/CreateIndexBuffer/Dynamic variants throw correctly
    # on SDL_Renderer, covering every construction overload plus zero/negative-count edge cases.
    cna_sdl_test(cna_test_sdl_buffer_construction_throws
                 examples/sdlrenderer_buffer_construction_throws_test.cpp)
    add_test(NAME SDL_Renderer_BufferConstructionThrows
             COMMAND cna_test_sdl_buffer_construction_throws)
    set_tests_properties(SDL_Renderer_BufferConstructionThrows PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 724: VertexDeclaration construction does NOT throw on SDL_Renderer -- pure data,
    # no backend touch; only an actual draw call using it should throw.
    cna_sdl_test(cna_test_sdl_vertexdeclaration_construction
                 examples/sdlrenderer_vertexdeclaration_construction_test.cpp)
    add_test(NAME SDL_Renderer_VertexDeclarationConstruction
             COMMAND cna_test_sdl_vertexdeclaration_construction)
    set_tests_properties(SDL_Renderer_VertexDeclarationConstruction PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 726: all 5 stock 3D effects' Apply()+property setters do NOT throw on SDL_Renderer,
    # but drawing with them does.
    cna_sdl_test(cna_test_sdl_stock3deffects_apply
                 examples/sdlrenderer_stock3deffects_apply_test.cpp)
    add_test(NAME SDL_Renderer_Stock3DEffectsApply
             COMMAND cna_test_sdl_stock3deffects_apply)
    set_tests_properties(SDL_Renderer_Stock3DEffectsApply PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 727: OcclusionQuery::Begin/End throw correctly on SDL_Renderer -- construction now
    # throws (fixed a real silent-no-op gap; the only inconsistent 3D-only entry point).
    cna_sdl_test(cna_test_sdl_occlusionquery_throws
                 examples/sdlrenderer_occlusionquery_throws_test.cpp)
    add_test(NAME SDL_Renderer_OcclusionQueryThrows
             COMMAND cna_test_sdl_occlusionquery_throws)
    set_tests_properties(SDL_Renderer_OcclusionQueryThrows PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 728: Model::Draw throws correctly on SDL_Renderer (requires 3D primitives).
    cna_sdl_test(cna_test_sdl_model_draw_throws
                 examples/sdlrenderer_model_draw_throws_test.cpp)
    add_test(NAME SDL_Renderer_ModelDrawThrows
             COMMAND cna_test_sdl_model_draw_throws)
    set_tests_properties(SDL_Renderer_ModelDrawThrows PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 729: RasterizerState/DepthStencilState construction and assignment do NOT throw on
    # SDL_Renderer -- pure data, mirrors Task 724's VertexDeclaration pattern.
    cna_sdl_test(cna_test_sdl_rasterizer_depthstencil_construction
                 examples/sdlrenderer_rasterizer_depthstencil_construction_test.cpp)
    add_test(NAME SDL_Renderer_RasterizerDepthStencilConstruction
             COMMAND cna_test_sdl_rasterizer_depthstencil_construction)
    set_tests_properties(SDL_Renderer_RasterizerDepthStencilConstruction PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 730: port and run 5 minimal 2D-only FNA/XNA samples specifically targeting
    # SDL_Renderer -- real compatibility proof for the 2D-only backend, not EasyGL.
    # Sample 1/5: the existing cross-backend cna_demo_2d, already smoke-tested on Vulkan/Bgfx
    # (Tasks 88/89) but never on SDL_Renderer -- a genuine gap, closed here.
    add_test(NAME SDL_Renderer_Demo2D_SmokeTest
             COMMAND cna_demo_2d --smoke 3)
    set_tests_properties(SDL_Renderer_Demo2D_SmokeTest PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}"
        LABELS GraphicsSmoke)

    # Sample 2/5: bouncing sprite -- real multi-frame Update()+Draw() physics/bounce loop.
    cna_sdl_test(cna_test_sdl_sample_bouncing_sprite
                 examples/sdlrenderer_sample_bouncing_sprite_test.cpp)
    add_test(NAME SDL_Renderer_Sample_BouncingSprite
             COMMAND cna_test_sdl_sample_bouncing_sprite)
    set_tests_properties(SDL_Renderer_Sample_BouncingSprite PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Sample 3/5: keyboard-driven sprite -- Keyboard::GetState()-driven Update() logic.
    cna_sdl_test(cna_test_sdl_sample_keyboard_sprite
                 examples/sdlrenderer_sample_keyboard_sprite_test.cpp)
    add_test(NAME SDL_Renderer_Sample_KeyboardSprite
             COMMAND cna_test_sdl_sample_keyboard_sprite)
    set_tests_properties(SDL_Renderer_Sample_KeyboardSprite PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Sample 4/5: SpriteFont text display -- genuine multi-glyph string layout ("HI").
    cna_sdl_test(cna_test_sdl_sample_spritefont_text
                 examples/sdlrenderer_sample_spritefont_text_test.cpp)
    add_test(NAME SDL_Renderer_Sample_SpriteFontText
             COMMAND cna_test_sdl_sample_spritefont_text)
    set_tests_properties(SDL_Renderer_Sample_SpriteFontText PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Sample 5/5: animated spritesheet -- Update()-driven sourceRectangle frame selection.
    cna_sdl_test(cna_test_sdl_sample_animated_spritesheet
                 examples/sdlrenderer_sample_animated_spritesheet_test.cpp)
    add_test(NAME SDL_Renderer_Sample_AnimatedSpritesheet
             COMMAND cna_test_sdl_sample_animated_spritesheet)
    set_tests_properties(SDL_Renderer_Sample_AnimatedSpritesheet PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

endif()

if(CNA_BUILD_TESTS AND NOT EMSCRIPTEN AND NOT WIN32
   AND CNA_GRAPHICS_BACKEND STREQUAL "BGFX")

    enable_testing()

    macro(cna_bgfx_test target src)
        add_executable(${target} ${src})
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(${target} PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(${target} PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(${target} PRIVATE SDL3::SDL3main)
        endif()
    endmacro()

    # Task 13.6: investigated adding avatar-rendering smoke tests here too (mirroring the Vulkan
    # registration below). Confirmed a real, concrete blocker independent of readback reliability:
    # BgfxGraphicsBackend::SetDepthTestEnabled/SetBlendEnabled/SetDepthWriteEnabled are all
    # unconditionally unimplemented stubs (ThrowNo3DState()) - since the avatar test harnesses
    # call SetDepthTestEnabled/setBlendStateProperty to set up their scene before drawing (not
    # AvatarRenderer's own code, which never touches these), all 3 avatar tests throw immediately
    # on Bgfx today, before ever reaching DrawRealEXT. This is a pre-existing, deep gap in the
    # Bgfx backend's 3D device-state wiring, not a tooling/build limitation - genuinely fixing it
    # (implementing depth-test/blend state via bgfx::setState flags) is a full backend feature,
    # well beyond this test-coverage task's scope. Deferred; not attempted here.

    # Task 89: 3-frame smoke test for the Bgfx backend
    add_test(NAME Bgfx_Demo2D_SmokeTest
             COMMAND cna_demo_2d --smoke 3)
    set_tests_properties(Bgfx_Demo2D_SmokeTest PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}"
        LABELS GraphicsSmoke)

    # Task 179: RenderTargetUsage DiscardContents/PreserveContents smoke test
    cna_bgfx_test(cna_test_bgfx_render_target_usage
                  examples/bgfx_render_target_usage_test.cpp)
    add_test(NAME Bgfx_RenderTargetUsage
             COMMAND cna_test_bgfx_render_target_usage)
    set_tests_properties(Bgfx_RenderTargetUsage PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 249: Bgfx vertex layout mapping tests — all 12 VertexElementFormat values
    cna_bgfx_test(cna_test_bgfx_vertex_format
                  examples/bgfx_vertex_format_test.cpp)
    add_test(NAME Bgfx_VertexFormatMapping
             COMMAND cna_test_bgfx_vertex_format)
    set_tests_properties(Bgfx_VertexFormatMapping PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 448: real Bgfx occlusion-query pixel/query test — visible quad (PixelCount > 0) and
    # occluded quad (PixelCount <= 0), proving BgfxOcclusionQueryBackend's Begin()/End() are now
    # actually wired to bgfx's own submit(id, program, occlusionQuery, ...) overload.
    cna_bgfx_test(cna_test_bgfx_occlusion_query
                  examples/bgfx_occlusion_query_test.cpp)
    add_test(NAME Bgfx_OcclusionQuery
             COMMAND cna_test_bgfx_occlusion_query)
    set_tests_properties(Bgfx_OcclusionQuery PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 278: EnvironmentMapEffect smoke test (no GPU readback available on Bgfx)
    cna_bgfx_test(cna_test_bgfx_env_map
                  examples/bgfx_env_map_test.cpp)
    add_test(NAME Bgfx_EnvironmentMapEffect_Smoke
             COMMAND cna_test_bgfx_env_map)
    set_tests_properties(Bgfx_EnvironmentMapEffect_Smoke PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 393: EnvironmentMapEffect EnvironmentMapAmount=0 should ignore the cube map
    cna_bgfx_test(cna_test_bgfx_environmentmapeffect_amount_zero
                  examples/bgfx_environmentmapeffect_amount_zero_test.cpp)
    add_test(NAME Bgfx_EnvironmentMapEffect_AmountZero
             COMMAND cna_test_bgfx_environmentmapeffect_amount_zero)
    set_tests_properties(Bgfx_EnvironmentMapEffect_AmountZero PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 394: EnvironmentMapEffect EnvironmentMapAmount=1 should fully replace lit color
    cna_bgfx_test(cna_test_bgfx_environmentmapeffect_amount_one
                  examples/bgfx_environmentmapeffect_amount_one_test.cpp)
    add_test(NAME Bgfx_EnvironmentMapEffect_AmountOne
             COMMAND cna_test_bgfx_environmentmapeffect_amount_one)
    set_tests_properties(Bgfx_EnvironmentMapEffect_AmountOne PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 395: EnvironmentMapEffect EnvironmentMapSpecular should scale by cubemap alpha
    cna_bgfx_test(cna_test_bgfx_environmentmapeffect_specular
                  examples/bgfx_environmentmapeffect_specular_test.cpp)
    add_test(NAME Bgfx_EnvironmentMapEffect_Specular
             COMMAND cna_test_bgfx_environmentmapeffect_specular)
    set_tests_properties(Bgfx_EnvironmentMapEffect_Specular PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 396: EnvironmentMapEffect Fresnel edge-weighting should suppress head-on reflections
    cna_bgfx_test(cna_test_bgfx_environmentmapeffect_fresnel
                  examples/bgfx_environmentmapeffect_fresnel_test.cpp)
    add_test(NAME Bgfx_EnvironmentMapEffect_Fresnel
             COMMAND cna_test_bgfx_environmentmapeffect_fresnel)
    set_tests_properties(Bgfx_EnvironmentMapEffect_Fresnel PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 397: EnvironmentMapEffect reflection vector should respond to EyePosition
    cna_bgfx_test(cna_test_bgfx_environmentmapeffect_eyeposition
                  examples/bgfx_environmentmapeffect_eyeposition_test.cpp)
    add_test(NAME Bgfx_EnvironmentMapEffect_EyePosition
             COMMAND cna_test_bgfx_environmentmapeffect_eyeposition)
    set_tests_properties(Bgfx_EnvironmentMapEffect_EyePosition PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 398: EnvironmentMapEffect normal transform should use inverse-transpose of World
    cna_bgfx_test(cna_test_bgfx_environmentmapeffect_worldtransform
                  examples/bgfx_environmentmapeffect_worldtransform_test.cpp)
    add_test(NAME Bgfx_EnvironmentMapEffect_WorldTransform
             COMMAND cna_test_bgfx_environmentmapeffect_worldtransform)
    set_tests_properties(Bgfx_EnvironmentMapEffect_WorldTransform PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 399: cross-backend EnvironmentMapEffect capstone (Tasks 393-398 combined)
    cna_bgfx_test(cna_test_bgfx_environmentmapeffect_combined
                  examples/bgfx_environmentmapeffect_combined_test.cpp)
    add_test(NAME Bgfx_EnvironmentMapEffect_Combined
             COMMAND cna_test_bgfx_environmentmapeffect_combined)
    set_tests_properties(Bgfx_EnvironmentMapEffect_Combined PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 406: SkinnedEffect identity bone palette should produce zero deformation
    cna_bgfx_test(cna_test_bgfx_skinnedeffect_identity_bones
                  examples/bgfx_skinnedeffect_identity_bones_test.cpp)
    add_test(NAME Bgfx_SkinnedEffect_IdentityBones
             COMMAND cna_test_bgfx_skinnedeffect_identity_bones)
    set_tests_properties(Bgfx_SkinnedEffect_IdentityBones PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 407: SkinnedEffect single translation bone should shift the mesh
    cna_bgfx_test(cna_test_bgfx_skinnedeffect_translation_bone
                  examples/bgfx_skinnedeffect_translation_bone_test.cpp)
    add_test(NAME Bgfx_SkinnedEffect_TranslationBone
             COMMAND cna_test_bgfx_skinnedeffect_translation_bone)
    set_tests_properties(Bgfx_SkinnedEffect_TranslationBone PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 408: SkinnedEffect two-bone weighted blend should produce midpoint deformation
    cna_bgfx_test(cna_test_bgfx_skinnedeffect_twobone_blend
                  examples/bgfx_skinnedeffect_twobone_blend_test.cpp)
    add_test(NAME Bgfx_SkinnedEffect_TwoBoneBlend
             COMMAND cna_test_bgfx_skinnedeffect_twobone_blend)
    set_tests_properties(Bgfx_SkinnedEffect_TwoBoneBlend PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 409: cross-backend SkinnedEffect capstone (identity + single-bone + two-bone blend)
    cna_bgfx_test(cna_test_bgfx_skinnedeffect_combined
                  examples/bgfx_skinnedeffect_combined_test.cpp)
    add_test(NAME Bgfx_SkinnedEffect_Combined
             COMMAND cna_test_bgfx_skinnedeffect_combined)
    set_tests_properties(Bgfx_SkinnedEffect_Combined PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 333: RenderTarget2D sampled as Texture2D via SpriteBatch after unbinding (Bgfx)
    cna_bgfx_test(cna_test_bgfx_render_target_sample
                  examples/bgfx_render_target_sample_test.cpp)
    add_test(NAME Bgfx_RenderTarget2D_SampleAfterUnbind
             COMMAND cna_test_bgfx_render_target_sample)
    set_tests_properties(Bgfx_RenderTarget2D_SampleAfterUnbind PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 335/912: RenderTarget2D depth buffer is real and functional (Bgfx) -- shared with
    # EasyGL/Vulkan's registrations of the same file; needed the retry-until-non-black convention
    # added to the shared test first (see that file's header comment).
    cna_bgfx_test(cna_test_bgfx_rendertarget2d_depth
                  examples/rendertarget2d_depth_test.cpp)
    add_test(NAME Bgfx_RenderTarget2D_DepthBuffer
             COMMAND cna_test_bgfx_rendertarget2d_depth)
    set_tests_properties(Bgfx_RenderTarget2D_DepthBuffer PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 334: RenderTargetCube sampled as TextureCube via EnvironmentMapEffect after unbinding (Bgfx)
    cna_bgfx_test(cna_test_bgfx_render_target_cube_sample
                  examples/bgfx_render_target_cube_sample_test.cpp)
    add_test(NAME Bgfx_RenderTargetCube_SampleAfterUnbind
             COMMAND cna_test_bgfx_render_target_cube_sample)
    set_tests_properties(Bgfx_RenderTargetCube_SampleAfterUnbind PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 364: BasicEffect pixel test — VertexColorEnabled=false, no texture, diffuse color only
    cna_bgfx_test(cna_test_bgfx_basiceffect_vertexcolor_disabled
                  examples/bgfx_basiceffect_vertexcolor_disabled_test.cpp)
    add_test(NAME Bgfx_BasicEffect_VertexColorDisabled
             COMMAND cna_test_bgfx_basiceffect_vertexcolor_disabled)
    set_tests_properties(Bgfx_BasicEffect_VertexColorDisabled PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 365: BasicEffect pixel test — VertexColorEnabled=true, no texture, vertex color multiplication
    cna_bgfx_test(cna_test_bgfx_basiceffect_vertexcolor_enabled
                  examples/bgfx_basiceffect_vertexcolor_enabled_test.cpp)
    add_test(NAME Bgfx_BasicEffect_VertexColorEnabled
             COMMAND cna_test_bgfx_basiceffect_vertexcolor_enabled)
    set_tests_properties(Bgfx_BasicEffect_VertexColorEnabled PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 366: BasicEffect pixel test — TextureEnabled=true, no vertex color
    cna_bgfx_test(cna_test_bgfx_basiceffect_texture_enabled
                  examples/bgfx_basiceffect_texture_enabled_test.cpp)
    add_test(NAME Bgfx_BasicEffect_TextureEnabled
             COMMAND cna_test_bgfx_basiceffect_texture_enabled)
    set_tests_properties(Bgfx_BasicEffect_TextureEnabled PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 367: BasicEffect pixel test — TextureEnabled=true AND VertexColorEnabled=true
    cna_bgfx_test(cna_test_bgfx_basiceffect_texture_vertexcolor_enabled
                  examples/bgfx_basiceffect_texture_vertexcolor_enabled_test.cpp)
    add_test(NAME Bgfx_BasicEffect_TextureVertexColorEnabled
             COMMAND cna_test_bgfx_basiceffect_texture_vertexcolor_enabled)
    set_tests_properties(Bgfx_BasicEffect_TextureVertexColorEnabled PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 368: BasicEffect pixel test — LightingEnabled=true, one DirectionalLight
    cna_bgfx_test(cna_test_bgfx_basiceffect_one_light
                  examples/bgfx_basiceffect_one_light_test.cpp)
    add_test(NAME Bgfx_BasicEffect_OneLight
             COMMAND cna_test_bgfx_basiceffect_one_light)
    set_tests_properties(Bgfx_BasicEffect_OneLight PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 885: BasicEffect pixel test — DirectionalLight1/2 + EmissiveColor on the lit path
    cna_bgfx_test(cna_test_bgfx_basiceffect_multilight_emissive
                  examples/bgfx_basiceffect_multilight_emissive_test.cpp)
    add_test(NAME Bgfx_BasicEffect_MultiLightEmissive
             COMMAND cna_test_bgfx_basiceffect_multilight_emissive)
    set_tests_properties(Bgfx_BasicEffect_MultiLightEmissive PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 886: BasicEffect pixel test — real specular highlights
    cna_bgfx_test(cna_test_bgfx_basiceffect_specular
                  examples/bgfx_basiceffect_specular_test.cpp)
    add_test(NAME Bgfx_BasicEffect_Specular
             COMMAND cna_test_bgfx_basiceffect_specular)
    set_tests_properties(Bgfx_BasicEffect_Specular PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 887: AlphaTestEffect.VertexColorEnabled fix verification
    cna_bgfx_test(cna_test_bgfx_alphatest_vertexcolor
                  examples/bgfx_alphatest_vertexcolor_test.cpp)
    add_test(NAME Bgfx_AlphaTest_VertexColor
             COMMAND cna_test_bgfx_alphatest_vertexcolor)
    set_tests_properties(Bgfx_AlphaTest_VertexColor PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 888: BasicEffect fog on the colored3d (stride-16) pipeline
    cna_bgfx_test(cna_test_bgfx_basiceffect_fog
                  examples/bgfx_basiceffect_fog_test.cpp)
    add_test(NAME Bgfx_BasicEffect_Fog
             COMMAND cna_test_bgfx_basiceffect_fog)
    set_tests_properties(Bgfx_BasicEffect_Fog PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 888: BasicEffect fog on the lit_textured3d (stride-32) pipeline
    cna_bgfx_test(cna_test_bgfx_basiceffect_lit_fog
                  examples/bgfx_basiceffect_lit_fog_test.cpp)
    add_test(NAME Bgfx_BasicEffect_LitFog
             COMMAND cna_test_bgfx_basiceffect_lit_fog)
    set_tests_properties(Bgfx_BasicEffect_LitFog PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 888: AlphaTestEffect fog on the alpha_test3d pipeline
    cna_bgfx_test(cna_test_bgfx_alphatest_fog
                  examples/bgfx_alphatest_fog_test.cpp)
    add_test(NAME Bgfx_AlphaTest_Fog
             COMMAND cna_test_bgfx_alphatest_fog)
    set_tests_properties(Bgfx_AlphaTest_Fog PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 888: DualTextureEffect fog on the dual_texture3d pipeline
    cna_bgfx_test(cna_test_bgfx_dualtextureeffect_fog
                  examples/bgfx_dualtextureeffect_fog_test.cpp)
    add_test(NAME Bgfx_DualTextureEffect_Fog
             COMMAND cna_test_bgfx_dualtextureeffect_fog)
    set_tests_properties(Bgfx_DualTextureEffect_Fog PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 892: BasicEffect lit-textured normal transform under a real camera (Bgfx)
    cna_bgfx_test(cna_test_bgfx_basiceffect_normaltransform
                  examples/bgfx_basiceffect_normaltransform_test.cpp)
    add_test(NAME Bgfx_BasicEffect_NormalTransform
             COMMAND cna_test_bgfx_basiceffect_normaltransform)
    set_tests_properties(Bgfx_BasicEffect_NormalTransform PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 369: BasicEffect pixel test — DiffuseColor+EmissiveColor, LightingEnabled=false
    cna_bgfx_test(cna_test_bgfx_basiceffect_emissive
                  examples/bgfx_basiceffect_emissive_test.cpp)
    add_test(NAME Bgfx_BasicEffect_Emissive
             COMMAND cna_test_bgfx_basiceffect_emissive)
    set_tests_properties(Bgfx_BasicEffect_Emissive PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 370: BasicEffect cross-backend image comparison suite — closes Phase 42
    cna_bgfx_test(cna_test_bgfx_basiceffect_combined
                  examples/bgfx_basiceffect_combined_test.cpp)
    add_test(NAME Bgfx_BasicEffect_Combined
             COMMAND cna_test_bgfx_basiceffect_combined)
    set_tests_properties(Bgfx_BasicEffect_Combined PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 375: AlphaTestEffect all CompareFunction modes — threshold sweep
    cna_bgfx_test(cna_test_bgfx_alphatest_comparefunction_sweep
                  examples/bgfx_alphatest_comparefunction_sweep_test.cpp)
    add_test(NAME Bgfx_AlphaTest_CompareFunctionSweep
             COMMAND cna_test_bgfx_alphatest_comparefunction_sweep)
    set_tests_properties(Bgfx_AlphaTest_CompareFunctionSweep PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 379: AlphaTestEffect null/no-texture behavior
    cna_bgfx_test(cna_test_bgfx_alphatest_null_texture
                  examples/bgfx_alphatest_null_texture_test.cpp)
    add_test(NAME Bgfx_AlphaTest_NullTexture
             COMMAND cna_test_bgfx_alphatest_null_texture)
    set_tests_properties(Bgfx_AlphaTest_NullTexture PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 383: DualTextureEffect two-texture blend formula, including FNA's color.rgb*=2.
    # Also the first-ever Bgfx pixel-integration test for DualTextureEffect.
    cna_bgfx_test(cna_test_bgfx_dualtextureeffect_doubling
                  examples/bgfx_dualtextureeffect_doubling_test.cpp)
    add_test(NAME Bgfx_DualTextureEffect_Doubling
             COMMAND cna_test_bgfx_dualtextureeffect_doubling)
    set_tests_properties(Bgfx_DualTextureEffect_Doubling PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 384: DualTextureEffect magenta × yellow = red (mirrors Task 133/135 EasyGL/Vulkan)
    cna_bgfx_test(cna_test_bgfx_dual_texture
                  examples/bgfx_dual_texture_test.cpp)
    add_test(NAME Bgfx_DualTextureEffect_Blend
             COMMAND cna_test_bgfx_dual_texture)
    set_tests_properties(Bgfx_DualTextureEffect_Blend PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 385: DualTextureEffect Alpha premultiplication, verified via real AlphaBlend
    cna_bgfx_test(cna_test_bgfx_dualtextureeffect_alpha
                  examples/bgfx_dualtextureeffect_alpha_test.cpp)
    add_test(NAME Bgfx_DualTextureEffect_Alpha
             COMMAND cna_test_bgfx_dualtextureeffect_alpha)
    set_tests_properties(Bgfx_DualTextureEffect_Alpha PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 386: DualTextureEffect first texture (Texture, slot 0) null behavior
    cna_bgfx_test(cna_test_bgfx_dualtextureeffect_null_texture0
                  examples/bgfx_dualtextureeffect_null_texture0_test.cpp)
    add_test(NAME Bgfx_DualTextureEffect_NullTexture0
             COMMAND cna_test_bgfx_dualtextureeffect_null_texture0)
    set_tests_properties(Bgfx_DualTextureEffect_NullTexture0 PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 387: DualTextureEffect second texture (Texture2, slot 1) null behavior
    cna_bgfx_test(cna_test_bgfx_dualtextureeffect_null_texture2
                  examples/bgfx_dualtextureeffect_null_texture2_test.cpp)
    add_test(NAME Bgfx_DualTextureEffect_NullTexture2
             COMMAND cna_test_bgfx_dualtextureeffect_null_texture2)
    set_tests_properties(Bgfx_DualTextureEffect_NullTexture2 PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 389: DualTextureEffect cross-backend image comparison suite (closes Phase 44 pixel work)
    cna_bgfx_test(cna_test_bgfx_dualtextureeffect_combined
                  examples/bgfx_dualtextureeffect_combined_test.cpp)
    add_test(NAME Bgfx_DualTextureEffect_Combined
             COMMAND cna_test_bgfx_dualtextureeffect_combined)
    set_tests_properties(Bgfx_DualTextureEffect_Combined PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 880: GraphicsDevice.Viewport GPU wiring — sub-region viewport pixel-readback test
    cna_bgfx_test(cna_test_bgfx_viewport_subregion
                  examples/bgfx_viewport_subregion_test.cpp)
    add_test(NAME Bgfx_Viewport_Subregion
             COMMAND cna_test_bgfx_viewport_subregion)
    set_tests_properties(Bgfx_Viewport_Subregion PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 750: SpriteBatch Begin()'s SamplerState (TextureAddressMode) must take effect
    cna_bgfx_test(cna_test_bgfx_texture_address_mode
                  examples/bgfx_texture_address_mode_test.cpp)
    add_test(NAME Bgfx_TextureAddressMode
             COMMAND cna_test_bgfx_texture_address_mode)
    set_tests_properties(Bgfx_TextureAddressMode PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 899: EnvironmentMapEffect/SkinnedEffect fog on Bgfx (opportunistic bonus scope)
    cna_bgfx_test(cna_test_bgfx_environmentmapeffect_fog
                  examples/bgfx_environmentmapeffect_fog_test.cpp)
    add_test(NAME Bgfx_EnvironmentMapEffect_Fog
             COMMAND cna_test_bgfx_environmentmapeffect_fog)
    set_tests_properties(Bgfx_EnvironmentMapEffect_Fog PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_bgfx_test(cna_test_bgfx_skinnedeffect_fog
                  examples/bgfx_skinnedeffect_fog_test.cpp)
    add_test(NAME Bgfx_SkinnedEffect_Fog
             COMMAND cna_test_bgfx_skinnedeffect_fog)
    set_tests_properties(Bgfx_SkinnedEffect_Fog PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 878/879: RenderTarget2D MSAA resolve genuinely anti-aliases on Bgfx
    # (BGFX_TEXTURE_RT_MSAA_Xn -- also closes Task 873's SpriteBatch RT-sampling cast bug and a
    # second, separate EnsureViewState() RT-viewport-clobbering bug, both hard prerequisites for
    # this test's RT-then-backbuffer-sample methodology to be meaningful at all -- see the test
    # file's own header comment for full detail). CNA_BGFX_RENDERER=VULKAN routes bgfx through its
    # Vulkan backend instead of its default OpenGL one: this dev environment's bgfx OpenGL path
    # negotiates only a legacy GL 2.1 context (confirmed via glxinfo that the underlying driver
    # actually supports OpenGL 4.6) under which MSAA-flagged framebuffer textures do not really
    # resolve with sub-pixel blending, despite bgfx::getCaps() reporting format support -- a
    # bgfx/legacy-GL-context environment limitation, not a defect in this task's (100% backend-
    # agnostic) C++ wiring, confirmed by this exact test passing cleanly under bgfx's Vulkan
    # renderer with zero code changes.
    cna_bgfx_test(cna_test_bgfx_rendertarget2d_msaa
                  examples/bgfx_rendertarget2d_msaa_test.cpp)
    add_test(NAME Bgfx_RenderTarget2D_MsaaResolve
             COMMAND cna_test_bgfx_rendertarget2d_msaa)
    set_tests_properties(Bgfx_RenderTarget2D_MsaaResolve PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_BGFX_RENDERER=VULKAN")

    # Task 906: RenderTarget2D mip chains are genuinely generated (bgfx's own built-in
    # hasMips+BGFX_RESOLVE_AUTO_GEN_MIPS mechanism), not just present-but-undefined storage.
    cna_bgfx_test(cna_test_bgfx_rendertarget2d_mip
                  examples/bgfx_rendertarget2d_mip_test.cpp)
    add_test(NAME Bgfx_RenderTarget2D_MipChain
             COMMAND cna_test_bgfx_rendertarget2d_mip)
    set_tests_properties(Bgfx_RenderTarget2D_MipChain PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 907: RenderTargetCube mip chains (closes Task 874 as a hard prerequisite --
    # this is Bgfx's first-ever RenderTargetCube-via-EnvironmentMapEffect test).
    cna_bgfx_test(cna_test_bgfx_rendertargetcube_mip
                  examples/bgfx_rendertargetcube_mip_test.cpp)
    add_test(NAME Bgfx_RenderTargetCube_MipChain
             COMMAND cna_test_bgfx_rendertargetcube_mip)
    set_tests_properties(Bgfx_RenderTargetCube_MipChain PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 903: RenderTargetCube MSAA (property fidelity + no-corruption sanity check -- see this
    # test's own header comment for why a genuine sub-pixel AA differential isn't attempted).
    cna_bgfx_test(cna_test_bgfx_rendertargetcube_msaa
                  examples/bgfx_rendertargetcube_msaa_test.cpp)
    add_test(NAME Bgfx_RenderTargetCube_MsaaResolve
             COMMAND cna_test_bgfx_rendertargetcube_msaa)
    set_tests_properties(Bgfx_RenderTargetCube_MsaaResolve PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 877: RenderTargetCube honors its exact requested DepthFormat -- Bgfx's first-ever
    # depth-tested RenderTargetCube face (previously no depth attachment support at all).
    cna_bgfx_test(cna_test_bgfx_rendertargetcube_depthformat
                  examples/bgfx_rendertargetcube_depthformat_test.cpp)
    add_test(NAME Bgfx_RenderTargetCube_DepthFormat
             COMMAND cna_test_bgfx_rendertargetcube_depthformat)
    set_tests_properties(Bgfx_RenderTargetCube_DepthFormat PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 910: binding + filling more than one RenderTarget2D within a single un-advanced bgfx
    # frame no longer clobbers all but the last one bound -- each render target now gets its own
    # distinct bgfx view id instead of sharing one hardcoded id.
    cna_bgfx_test(cna_test_bgfx_concurrent_rendertargets
                  examples/bgfx_concurrent_rendertargets_test.cpp)
    add_test(NAME Bgfx_ConcurrentRenderTargets
             COMMAND cna_test_bgfx_concurrent_rendertargets)
    set_tests_properties(Bgfx_ConcurrentRenderTargets PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 914: Texture3D/TextureCube::GetData is now real on Bgfx (blit into a temporary
    # BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK texture + bgfx::readTexture()) -- shared with
    # EasyGL/Vulkan's registration of the same backend-agnostic files (Tasks 173/275/865).
    cna_bgfx_test(cna_test_bgfx_texture3d_slices
                  examples/easygl_texture3d_slices_test.cpp)
    add_test(NAME Bgfx_Texture3D_Slices_RoundTrip
             COMMAND cna_test_bgfx_texture3d_slices)
    set_tests_properties(Bgfx_Texture3D_Slices_RoundTrip PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_bgfx_test(cna_test_bgfx_texturecube_partial_rect
                  examples/easygl_texturecube_partial_rect_test.cpp)
    add_test(NAME Bgfx_TextureCube_PartialRect_RoundTrip
             COMMAND cna_test_bgfx_texturecube_partial_rect)
    set_tests_properties(Bgfx_TextureCube_PartialRect_RoundTrip PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 914: Texture3D/TextureCube mip-level allocation is now real on Bgfx (mipMap genuinely
    # threaded through createTexture3D/createTextureCube's _hasMips, previously hardcoded false) --
    # shared with EasyGL/Vulkan's registration of the same backend-agnostic files (Tasks 862/864).
    cna_bgfx_test(cna_test_bgfx_texture3d_mip
                  examples/easygl_texture3d_mip_test.cpp)
    add_test(NAME Bgfx_Texture3D_Mip_RoundTrip
             COMMAND cna_test_bgfx_texture3d_mip)
    set_tests_properties(Bgfx_Texture3D_Mip_RoundTrip PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_bgfx_test(cna_test_bgfx_texturecube_mip
                  examples/easygl_texturecube_mip_test.cpp)
    add_test(NAME Bgfx_TextureCube_Mip_RoundTrip
             COMMAND cna_test_bgfx_texturecube_mip)
    set_tests_properties(Bgfx_TextureCube_Mip_RoundTrip PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 923: BlendState ColorBlendFunction/AlphaBlendFunction independence on Bgfx -- a Bgfx-
    # specific adaptation of the EasyGL source (Task 307; can't reuse it verbatim like Vulkan's
    # Task 868 registration does, since it calls GraphicsDevice.SetDepthTestEnabled(false), an
    # unconditional throw on Bgfx -- see the test's own file-header comment). Discriminates Bgfx's
    # colorBlendFunc-always-Add gap (Check A expects Subtract; failed before this task's fix).
    cna_bgfx_test(cna_test_bgfx_blendstate_separate_functions
                  examples/bgfx_blendstate_separate_functions_test.cpp)
    add_test(NAME Bgfx_BlendState_SeparateFunctions
             COMMAND cna_test_bgfx_blendstate_separate_functions)
    set_tests_properties(Bgfx_BlendState_SeparateFunctions PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 923: BlendState AlphaSourceBlend/AlphaDestinationBlend independence from the colour
    # factors -- fixed in ApplyBlendState (real BGFX_STATE_BLEND_FUNC_SEPARATE, confirmed correct
    # against bgfx's own renderer_gl.cpp bit-decode logic), but NOT independently pixel-verified:
    # a two-draw indirect-alpha-observation test was built and empirically found to report the
    # SAME (wrong) result whether or not the fix is applied -- this sandbox's bgfx GL2.1/Mesa-
    # llvmpipe software renderer does not appear to honor separately-requested alpha blend factors
    # when they differ from the colour channel's own (confirmed via 3 independent probes; not
    # investigated further, out of this task's scope -- see plan_graphics.md Task 923 for detail).
    # Matches this project's established precedent for a sandbox limitation, not a CNA defect
    # (e.g. Task 448's occlusion query, Task 879's Bgfx_RenderTarget2D_MsaaResolve).

    # Task 926 (split from Task 867, the last of the 3 backends -- closes the split in full):
    # real Texture2D mip-level upload on Bgfx -- a Bgfx-specific adaptation of the EasyGL test
    # source (Task 298; can't reuse it verbatim, same reasoning as Task 925's Vulkan copy: Bgfx's
    # TextureFilter::Point has always mapped to BGFX_SAMPLER_MIP_POINT, genuinely mip-aware,
    # unlike EasyGL's still-flat mapping -- see the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_texture_mip_filter_effect
                  examples/bgfx_texture_mip_filter_effect_test.cpp)
    add_test(NAME Bgfx_TextureMipFilter_DualTextureEffect
             COMMAND cna_test_bgfx_texture_mip_filter_effect)
    set_tests_properties(Bgfx_TextureMipFilter_DualTextureEffect PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 759 (Phase 72 Bgfx gap closure): DepthStencilState.DepthBufferWriteEnable pixel test
    # on Bgfx -- a Bgfx-specific adaptation of the EasyGL source (Task 313; already reused
    # verbatim on Vulkan, but Bgfx's own GetBackBufferData only reliably reflects the first read
    # per rendered frame, Task 406, so the EasyGL source's 2-reads-per-frame structure needed
    # restructuring into 2 separately-read passes -- see the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_depthstencilstate_write_enable
                  examples/bgfx_depthstencilstate_write_enable_test.cpp)
    add_test(NAME Bgfx_DepthStencilState_WriteEnable
             COMMAND cna_test_bgfx_depthstencilstate_write_enable)
    set_tests_properties(Bgfx_DepthStencilState_WriteEnable PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 760 (Phase 72 Bgfx gap closure): all 8 depth CompareFunction values on Bgfx -- a
    # Bgfx-specific adaptation of the EasyGL source (Task 314; that file only covers 5 of 8 values
    # and reads back 5 columns in one frame, which Bgfx's own GetBackBufferData quirk, Task 406,
    # doesn't support reliably -- see the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_depthstencilstate_compare_function
                  examples/bgfx_depthstencilstate_compare_function_test.cpp)
    add_test(NAME Bgfx_DepthStencilState_CompareFunction
             COMMAND cna_test_bgfx_depthstencilstate_compare_function)
    set_tests_properties(Bgfx_DepthStencilState_CompareFunction PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 761 (Phase 72 Bgfx gap closure): StencilEnable + StencilMask (read mask) +
    # StencilWriteMask on Bgfx -- a Bgfx-specific adaptation of the EasyGL sources (Tasks 315/316;
    # those files sample multiple regions in one frame, incompatible with Bgfx's GetBackBufferData
    # quirk, Task 406 -- see the test's own file-header comment). StencilWriteMask is verified
    # informationally only: bgfx's own state API has no per-draw stencil write-mask flag at all
    # (confirmed against bgfx/defines.h), a permanent backend limitation, not a bug in this codebase.
    cna_bgfx_test(cna_test_bgfx_depthstencilstate_stencil_mask
                  examples/bgfx_depthstencilstate_stencil_mask_test.cpp)
    add_test(NAME Bgfx_DepthStencilState_StencilMask
             COMMAND cna_test_bgfx_depthstencilstate_stencil_mask)
    set_tests_properties(Bgfx_DepthStencilState_StencilMask PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 762 (Phase 72 Bgfx gap closure): front-facing StencilFail/StencilDepthBufferFail/
    # StencilPass operation slots on Bgfx -- a Bgfx-specific adaptation of the EasyGL source (Task
    # 317; that file reads back 4 columns in one frame, incompatible with Bgfx's GetBackBufferData
    # quirk, Task 406 -- see the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_depthstencilstate_stencil_ops
                  examples/bgfx_depthstencilstate_stencil_ops_test.cpp)
    add_test(NAME Bgfx_DepthStencilState_StencilOps
             COMMAND cna_test_bgfx_depthstencilstate_stencil_ops)
    set_tests_properties(Bgfx_DepthStencilState_StencilOps PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 763 (Phase 72 Bgfx gap closure): TwoSidedStencilMode (separate CCW stencil func/ops for
    # back-facing triangles) on Bgfx -- a Bgfx-specific adaptation of the EasyGL source (Task 318;
    # that file reads back 2 columns in one frame, incompatible with Bgfx's GetBackBufferData
    # quirk, Task 406 -- see the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_depthstencilstate_stencil_twosided
                  examples/bgfx_depthstencilstate_stencil_twosided_test.cpp)
    add_test(NAME Bgfx_DepthStencilState_StencilTwoSided
             COMMAND cna_test_bgfx_depthstencilstate_stencil_twosided)
    set_tests_properties(Bgfx_DepthStencilState_StencilTwoSided PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 764 (Phase 72 Bgfx gap closure): GraphicsDevice.ReferenceStencil standalone override
    # reaching Bgfx draw calls -- a Bgfx-specific adaptation of the EasyGL source (Task 319; that
    # file reads back 2 regions in one frame, incompatible with Bgfx's GetBackBufferData quirk,
    # Task 406 -- see the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_graphicsdevice_reference_stencil
                  examples/bgfx_graphicsdevice_reference_stencil_test.cpp)
    add_test(NAME Bgfx_GraphicsDevice_ReferenceStencil
             COMMAND cna_test_bgfx_graphicsdevice_reference_stencil)
    set_tests_properties(Bgfx_GraphicsDevice_ReferenceStencil PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 765 (Phase 72 Bgfx gap closure): CullMode (None/CullClockwiseFace/
    # CullCounterClockwiseFace) on Bgfx -- a Bgfx-specific adaptation of the EasyGL source (Task
    # 323/324/325; that file reads back 2 columns in one frame, incompatible with Bgfx's
    # GetBackBufferData quirk, Task 406 -- see the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_rasterizerstate_cullmode
                  examples/bgfx_rasterizerstate_cullmode_test.cpp)
    add_test(NAME Bgfx_RasterizerState_CullMode
             COMMAND cna_test_bgfx_rasterizerstate_cullmode)
    set_tests_properties(Bgfx_RasterizerState_CullMode PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 766 (Phase 72 Bgfx gap closure): FillMode::WireFrame on Bgfx -- adapted from
    # vulkan_fill_mode_test.cpp (Task 327; no EasyGL/shared source exists for this property, no
    # dedicated pixel test existed anywhere in this project before this task -- see the test's own
    # file-header comment). Exercises the new ExpandWireframeIndices triangle-to-line-list
    # emulation (bgfx has no native polygon-fill-mode toggle).
    cna_bgfx_test(cna_test_bgfx_rasterizerstate_fillmode_wireframe
                  examples/bgfx_rasterizerstate_fillmode_wireframe_test.cpp)
    add_test(NAME Bgfx_RasterizerState_FillMode_WireFrame
             COMMAND cna_test_bgfx_rasterizerstate_fillmode_wireframe)
    set_tests_properties(Bgfx_RasterizerState_FillMode_WireFrame PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 768 (Phase 72 Bgfx gap closure): ScissorTestEnable/ScissorRectangle interaction on Bgfx
    # -- a Bgfx-specific adaptation of the EasyGL source (Task 209; that file reads 2 columns per
    # frame across 3 draws, incompatible with Bgfx's GetBackBufferData quirk, Task 406 -- see the
    # test's own file-header comment). Found and fixed a genuine bug: none of Bgfx's 4 3D
    # draw-dispatch functions ever called bgfx::setScissor (only the 2D SpriteBatch path did).
    cna_bgfx_test(cna_test_bgfx_scissor
                  examples/bgfx_scissor_test.cpp)
    add_test(NAME Bgfx_Scissor
             COMMAND cna_test_bgfx_scissor)
    set_tests_properties(Bgfx_Scissor PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Tasks 752-755 (Phase 72 Bgfx gap closure): BlendState::Opaque/AlphaBlend/NonPremultiplied/
    # Additive on Bgfx -- each is a Bgfx-specific copy of the backend-agnostic EasyGL source (also
    # reused verbatim on Vulkan above), differing only in replacing the legacy
    # GraphicsDevice::SetDepthTestEnabled(false) call (throws on Bgfx -- a pre-existing, deliberate
    # stub) with the equivalent DepthStencilState-based call. Each still does exactly 1 Draw + 1
    # GetBackBufferData read per frame, so Bgfx's own "first read per rendered frame" quirk (Task
    # 406) does not apply -- no other restructuring needed.
    cna_bgfx_test(cna_test_bgfx_blendstate_opaque
                  examples/bgfx_blendstate_opaque_test.cpp)
    add_test(NAME Bgfx_BlendState_Opaque
             COMMAND cna_test_bgfx_blendstate_opaque)
    set_tests_properties(Bgfx_BlendState_Opaque PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_bgfx_test(cna_test_bgfx_blendstate_alphablend
                  examples/bgfx_blendstate_alphablend_test.cpp)
    add_test(NAME Bgfx_BlendState_AlphaBlend
             COMMAND cna_test_bgfx_blendstate_alphablend)
    set_tests_properties(Bgfx_BlendState_AlphaBlend PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_bgfx_test(cna_test_bgfx_blendstate_nonpremultiplied
                  examples/bgfx_blendstate_nonpremultiplied_test.cpp)
    add_test(NAME Bgfx_BlendState_NonPremultiplied
             COMMAND cna_test_bgfx_blendstate_nonpremultiplied)
    set_tests_properties(Bgfx_BlendState_NonPremultiplied PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_bgfx_test(cna_test_bgfx_blendstate_additive
                  examples/bgfx_blendstate_additive_test.cpp)
    add_test(NAME Bgfx_BlendState_Additive
             COMMAND cna_test_bgfx_blendstate_additive)
    set_tests_properties(Bgfx_BlendState_Additive PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 803 (Phase 72 Bgfx gap closure): SpriteSortMode ordering affects on-screen draw order
    # on Bgfx -- a Bgfx-specific adaptation of the EasyGL source (Task 420; that file reads back 3
    # regions from one rendered frame, incompatible with Bgfx's GetBackBufferData quirk, Task 406
    # -- see the test's own file-header comment for full scope notes).
    cna_bgfx_test(cna_test_bgfx_spritebatch_layerdepth
                  examples/bgfx_spritebatch_layerdepth_test.cpp)
    add_test(NAME Bgfx_SpriteBatch_LayerDepthOrder
             COMMAND cna_test_bgfx_spritebatch_layerdepth)
    set_tests_properties(Bgfx_SpriteBatch_LayerDepthOrder PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 804 (Phase 72 Bgfx gap closure): SpriteBatch::Draw rotation-around-origin on Bgfx -- a
    # Bgfx-specific adaptation of the EasyGL source (Task 417; that file reads back 3 regions from
    # one rendered frame, incompatible with Bgfx's GetBackBufferData quirk, Task 406 -- see the
    # test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_spritebatch_rotation
                  examples/bgfx_spritebatch_rotation_test.cpp)
    add_test(NAME Bgfx_SpriteBatch_Rotation
             COMMAND cna_test_bgfx_spritebatch_rotation)
    set_tests_properties(Bgfx_SpriteBatch_Rotation PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 805 (Phase 72 Bgfx gap closure): SpriteBatch::Draw scalar/Vector2 scale overloads on
    # Bgfx -- a Bgfx-specific adaptation of the EasyGL source (Task 418; that file reads back 5
    # regions from one rendered frame, incompatible with Bgfx's GetBackBufferData quirk, Task 406
    # -- see the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_spritebatch_scale
                  examples/bgfx_spritebatch_scale_test.cpp)
    add_test(NAME Bgfx_SpriteBatch_Scale
             COMMAND cna_test_bgfx_spritebatch_scale)
    set_tests_properties(Bgfx_SpriteBatch_Scale PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 806 (Phase 72 Bgfx gap closure): SpriteBatch::Draw sourceRectangle cropping on Bgfx --
    # a Bgfx-specific adaptation of the EasyGL source (Task 419; that file reads back 3 regions
    # from one rendered frame, incompatible with Bgfx's GetBackBufferData quirk, Task 406 -- see
    # the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_spritebatch_sourcerect
                  examples/bgfx_spritebatch_sourcerect_test.cpp)
    add_test(NAME Bgfx_SpriteBatch_SourceRectangleCropping
             COMMAND cna_test_bgfx_spritebatch_sourcerect)
    set_tests_properties(Bgfx_SpriteBatch_SourceRectangleCropping PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 807 (Phase 72 Bgfx gap closure): SpriteEffects::FlipHorizontally/FlipVertically on
    # Bgfx (mirrors Task 167) -- a Bgfx-specific adaptation of the EasyGL source (that file reads
    # back 8 points from 4 draws in one rendered frame, incompatible with Bgfx's
    # GetBackBufferData quirk, Task 406 -- see the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_sprite_effects
                  examples/bgfx_sprite_effects_test.cpp)
    add_test(NAME Bgfx_SpriteEffects_Flip
             COMMAND cna_test_bgfx_sprite_effects)
    set_tests_properties(Bgfx_SpriteEffects_Flip PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 808 (Phase 72 Bgfx gap closure): SpriteBatch::Begin's transformMatrix parameter on
    # Bgfx (mirrors Task 168) -- a Bgfx-specific adaptation of the EasyGL source (that file reads
    # back 2 regions from one rendered frame, incompatible with Bgfx's GetBackBufferData quirk,
    # Task 406 -- see the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_transform_matrix
                  examples/bgfx_transform_matrix_test.cpp)
    add_test(NAME Bgfx_SpriteBatch_TransformMatrix
             COMMAND cna_test_bgfx_transform_matrix)
    set_tests_properties(Bgfx_SpriteBatch_TransformMatrix PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 809 (Phase 72 Bgfx gap closure): SpriteFont single-glyph placement on Bgfx -- a
    # Bgfx-specific adaptation of the EasyGL source (Task 424; that file reads back 5 regions from
    # one rendered frame, incompatible with Bgfx's GetBackBufferData quirk, Task 406 -- see the
    # test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_spritefont_single_glyph
                  examples/bgfx_spritefont_single_glyph_test.cpp)
    add_test(NAME Bgfx_SpriteFont_SingleGlyph
             COMMAND cna_test_bgfx_spritefont_single_glyph)
    set_tests_properties(Bgfx_SpriteFont_SingleGlyph PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 810 (Phase 72 Bgfx gap closure): SpriteFont multi-glyph spacing + newline advance on
    # Bgfx -- a Bgfx-specific adaptation combining the EasyGL sources (Tasks 425/426; those files
    # read back several regions from one rendered frame, incompatible with Bgfx's
    # GetBackBufferData quirk, Task 406 -- see the test's own file-header comment).
    cna_bgfx_test(cna_test_bgfx_spritefont_multiglyph_newline
                  examples/bgfx_spritefont_multiglyph_newline_test.cpp)
    add_test(NAME Bgfx_SpriteFont_MultiGlyphSpacingNewline
             COMMAND cna_test_bgfx_spritefont_multiglyph_newline)
    set_tests_properties(Bgfx_SpriteFont_MultiGlyphSpacingNewline PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

endif()

# --- Task 6.1: real two-process ENet loopback test harness ---
# A tiny standalone (non-GTest) executable playing "host" or "client", spawned twice as
# independent OS processes by tests/CNA/Internal/Net/TwoProcessLoopbackTest.cpp. See NEXT.md /
# the approved Phase 6 plan for why this exists separately from every Phase 5 single-process test.
if(CNA_ENABLE_NET AND CNA_BUILD_TESTS)
    add_executable(cna_net_two_process_harness
        tools/net/net_two_process_harness.cpp
    )
    target_link_libraries(cna_net_two_process_harness
        PRIVATE
        CNA_GamerServices
        CNA_Net
        SHARP_RUNTIME
    )
endif()

# --- Task 12.1: GamerServicesDispatcher hang regression harness ---
# A tiny standalone (non-GTest) executable that calls GamerServicesDispatcher::Initialize() then
# NetworkSession::Create(), reproducing DEFERRED.md item #19 (../cna-samples) if it regresses.
# Must be a separate process, same reasoning as cna_net_two_process_harness above but for a
# different process-global-state hazard — see the harness file's own top-of-file comment.
if(CNA_ENABLE_NET AND CNA_BUILD_TESTS)
    add_executable(cna_net_gamerservices_dispatcher_harness
        tools/net/gamerservices_dispatcher_harness.cpp
    )
    target_link_libraries(cna_net_gamerservices_dispatcher_harness
        PRIVATE
        CNA_GamerServices
        CNA_Net
        SHARP_RUNTIME
    )
endif()

# --- Task P9-HARDWARE-005: standalone no-audio-hardware harness ---
# A tiny standalone (non-GTest) executable that forces SDL_AUDIODRIVER to a nonexistent driver
# name before any SDL audio call in this fresh process, then proves NoAudioHardwareException is
# genuinely thrown. Spawned by tests/CNA/Internal/Audio/AudioMixerTests.cpp -- see that file /
# tools/audio/audio_no_hardware_harness.cpp for why this needs its own process (AudioMixer.cpp's
# g_mixer is a process-wide, once-ever-initialized cache).
if(CNA_BUILD_TESTS)
    add_executable(cna_audio_no_hardware_harness
        tools/audio/audio_no_hardware_harness.cpp
    )
    target_link_libraries(cna_audio_no_hardware_harness
        PRIVATE
        CNA
        SHARP_RUNTIME
    )
endif()

# --- Task VERIFY-003/DEV-API-002: strict XNA API surface compile check ---
# Compiles tools/devices/StrictXnaApiSurfaceCheck.cpp with CNA_STRICT_XNA_API
# defined (turns the NOXNA macro into [[deprecated]], see CNAHelper.hpp) and
# -Werror=deprecated-declarations, so this target fails to build if that file
# ever calls a NOXNA-tagged Microsoft::Devices/Sensors member. A standalone
# executable, not a gtest binary — same tools/ precedent as
# cna_net_two_process_harness above, for the same reason (its own main()).
if(CNA_BUILD_TESTS AND (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
    add_executable(cna_strict_xna_api_check
        tools/devices/StrictXnaApiSurfaceCheck.cpp
    )
    target_link_libraries(cna_strict_xna_api_check PRIVATE CNA)
    target_compile_definitions(cna_strict_xna_api_check PRIVATE CNA_STRICT_XNA_API)
    target_compile_options(cna_strict_xna_api_check PRIVATE -Werror=deprecated-declarations)
    add_test(NAME StrictXnaApiSurfaceCheck_Compile_Run COMMAND cna_strict_xna_api_check)
endif()

if(CNA_BUILD_TESTS)
    add_subdirectory(vendor/googletest)

    enable_testing()

    file(GLOB_RECURSE CNA_TEST_SOURCES CONFIGURE_DEPENDS
            "tests/*.cpp"
    )

    # TwoProcessLoopbackTest.cpp (Task 6.1) uses POSIX-only process APIs (posix_spawn, poll,
    # sys/wait.h) to orchestrate two independent OS processes. Compiles under mingw-w64 and
    # Emscripten (both provide POSIX-ish libc stubs) but can't actually work under either: no real
    # multi-process spawning exists in a single Node.js/Wasm module. Also excluded on Android: the
    # harness path baked in via CNA_NET_HARNESS_PATH is an absolute path on the build machine, not
    # the on-device filesystem, and CnaTests is run as a bare pushed executable, not a packaged app
    # with its own bundled assets. Not part of the Task 6.2/6.4 verification filters
    # (*Network*:*Gamer*:*ENet*:*Packet*) either, since its suite name is TwoProcessLoopbackTest.
    if(WIN32 OR EMSCRIPTEN OR ANDROID)
        list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX ".*/CNA/Internal/Net/TwoProcessLoopbackTest\\.cpp$")
        # GamerServicesDispatcherHangRegressionTest.cpp (Task 12.1) uses the same POSIX-only
        # posix_spawn/sys-wait process APIs, for the same reasons — see TwoProcessLoopbackTest's
        # own exclusion comment just above.
        list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX ".*/CNA/Internal/Net/GamerServicesDispatcherHangRegressionTest\\.cpp$")
    endif()

    # AudioMixerTests.cpp (Task P9-HARDWARE-005) uses the same POSIX-only process APIs
    # (posix_spawn, poll, sys/wait.h) to spawn tools/audio/audio_no_hardware_harness.cpp as an
    # independent OS process, for the same reason TwoProcessLoopbackTest.cpp needs one above.
    # Excluded on the same platforms and for the same reasons (no real process spawning in a
    # single Node.js/Wasm module; CNA_AUDIO_NO_HARDWARE_HARNESS_PATH is a build-machine absolute
    # path, meaningless on-device on Android).
    if(WIN32 OR EMSCRIPTEN OR ANDROID)
        list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX ".*/CNA/Internal/Audio/AudioMixerTests\\.cpp$")
    endif()

    add_executable(CnaTests
            ${CNA_TEST_SOURCES}
    )

    # mingw-w64's <cmath> only exposes M_PI when _USE_MATH_DEFINES is set (unlike glibc, which
    # defines it unconditionally) — a handful of test files use M_PI directly as a reference value.
    if(MINGW)
        target_compile_definitions(CnaTests PRIVATE _USE_MATH_DEFINES)
        # Known mingw-w64/GCC PE-COFF toolchain limitation: std::type_info::operator== is emitted
        # as vague linkage (COMDAT) in libstdc++.a, but a test binary this large (many RTTI-using
        # TUs) trips a case where the linker fails to fold it against the identical libstdc++
        # definition. Both copies are byte-identical, so allowing the duplicate is safe.
        target_link_options(CnaTests PRIVATE -Wl,--allow-multiple-definition)
    endif()

    if(EMSCRIPTEN)
        # Emscripten's EXIT_RUNTIME defaults to 0 (deliberately keeps the JS runtime alive after
        # main() returns, so pending async JS/WebSocket handles can keep running) - without this,
        # `node CnaTests.js` never exits after printing its results, even on success.
        target_link_options(CnaTests PRIVATE -sEXIT_RUNTIME=1)

        # Emscripten's default build is fully synchronous/single-threaded: a real WebSocket
        # handshake structurally cannot complete while C++ code holds the call stack (confirmed
        # empirically - even a real 1s sleep loop never lets it finish, since nothing returns
        # control to Node's event loop). The SystemLink loopback tests need emscripten_sleep() to
        # actually yield back to Node between polls, which requires Asyncify. Scoped to CnaTests
        # only (a per-executable link-time transformation - does not affect native/Windows builds
        # or any other Emscripten executable target such as the demos or the two-process harness).
        target_link_options(CnaTests PRIVATE -sASYNCIFY=1)
    endif()

    target_link_libraries(CnaTests
            PRIVATE
            CNA
            SHARP_RUNTIME
            gtest_main
            SDL3::SDL3
    )

    if(CNA_ENABLE_NET)
        target_link_libraries(CnaTests PRIVATE CNA_GamerServices CNA_Net)
    endif()

    if(TARGET cna_net_two_process_harness)
        # Building CnaTests must also build the harness it spawns at test time (they're separate
        # executables, so CMake won't infer this dependency on its own), and the orchestrator test
        # needs the harness's real built path baked in at compile time.
        add_dependencies(CnaTests cna_net_two_process_harness)
        target_compile_definitions(CnaTests PRIVATE
            CNA_NET_HARNESS_PATH="$<TARGET_FILE:cna_net_two_process_harness>"
        )
    endif()

    if(TARGET cna_net_gamerservices_dispatcher_harness)
        # Same reasoning as cna_net_two_process_harness just above.
        add_dependencies(CnaTests cna_net_gamerservices_dispatcher_harness)
        target_compile_definitions(CnaTests PRIVATE
            CNA_NET_GAMERSERVICES_HANG_HARNESS_PATH="$<TARGET_FILE:cna_net_gamerservices_dispatcher_harness>"
        )
    endif()

    if(TARGET cna_audio_no_hardware_harness)
        # Same reasoning as cna_net_two_process_harness above, for AudioMixerTests.cpp.
        add_dependencies(CnaTests cna_audio_no_hardware_harness)
        target_compile_definitions(CnaTests PRIVATE
            CNA_AUDIO_NO_HARDWARE_HARNESS_PATH="$<TARGET_FILE:cna_audio_no_hardware_harness>"
        )
    endif()

    if(TARGET easy-gl)
        target_link_libraries(CnaTests PRIVATE easy-gl)
    endif()

    include(GoogleTest)
    gtest_discover_tests(CnaTests DISCOVERY_MODE PRE_TEST)

    # INPUT-BUILD-003: canonical Input-test selector — SINGLE SOURCE OF TRUTH.
    # The input subset used to be selected by a long --gtest_filter string copy-pasted across the docs
    # and CI, which silently drifted whenever a new suite name fell outside its tokens (e.g. the
    # ButtonState/KeyState/Buttons suites from INPUT-TEST-001). Keep the token list HERE only; docs and
    # CI select the subset via `ctest -L input` (see docs/input-build-and-test.md). When you add an input
    # test suite whose name matches none of these tokens, extend this one string.
    set(CNA_INPUT_TEST_FILTER
        "*Keyboard*:*Mouse*:*GamePad*:*Touch*:*Gesture*:*TextInput*:*SdlInputBridge*:*InputResetAllForTests*:*FakeGamepad*:*SdlGamepadSubsystemInit*:*ButtonState*:*KeyState*:*Buttons*:*PublicApiInput*:*CnaInput*:*Joystick*:*Haptic*")
    # INPUT-BUILD-009: `--gtest_shuffle --gtest_repeat=5` is the standardized order-independence gate.
    # The input state is a process-wide singleton (InputManager / GestureDetector / MouseCursor stock
    # cursors persist for the whole binary), so a static-state leak would reintroduce order dependence;
    # running the filtered subset 5x under a fresh shuffle each iteration is the required check that
    # catches it. `ctest -L input` runs exactly this on every backend/CI job.
    add_test(NAME CnaInputTests
             COMMAND CnaTests --gtest_filter=${CNA_INPUT_TEST_FILTER} --gtest_shuffle --gtest_repeat=5)
    # Headless-safe audio everywhere; the video driver is left to the runner (Xvfb+x11 in CI, real
    # display or `xvfb-run` locally) because the MouseCursor tests need real cursors (the SDL dummy
    # driver has null cursors).
    set_tests_properties(CnaInputTests PROPERTIES
        LABELS input
        ENVIRONMENT "SDL_AUDIODRIVER=dummy")

    if(MINGW)
        # Statically link MinGW runtime into the test binary too, so it can
        # run outside CLion without libgcc_s_seh-1.dll / libstdc++-6.dll.
        target_link_options(CnaTests PRIVATE -static-libgcc -static-libstdc++)
        # libwinpthread-1.dll cannot be statically linked; copy it next to the exe.
        cna_copy_mingw_runtime(CnaTests)
    endif()

    if(WIN32)
        cna_copy_sdl_runtime(CnaTests)
        foreach(_gtest_target IN ITEMS gtest gtest_main gmock gmock_main)
            if(TARGET ${_gtest_target})
                add_custom_command(TARGET CnaTests POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        $<TARGET_FILE:${_gtest_target}>
                        $<TARGET_FILE_DIR:CnaTests>
                    VERBATIM)
            endif()
        endforeach()
    endif()
endif()

# Task 470: headless-safe CTest skip convention. A test that self-detects "no GPU/display
# available" (e.g. SDL_InitSubSystem(SDL_INIT_VIDEO) failing, see
# examples/common/PixelTestGame.hpp's RunPixelTest()) and exits with this sentinel code is
# reported by ctest as SKIPPED rather than FAILED. Applied here in one shot to every test
# already registered anywhere in this file, rather than adding a SKIP_RETURN_CODE property to
# each individual set_tests_properties() call across ~330 existing test registrations -- purely
# additive: a test that never exits with this code is completely unaffected. 77 is the
# conventional Automake/BSD "test skipped" exit code, also CMake's own documented example value
# for SKIP_RETURN_CODE.
get_property(_cna_all_tests DIRECTORY PROPERTY TESTS)
if(_cna_all_tests)
    set_tests_properties(${_cna_all_tests} PROPERTIES SKIP_RETURN_CODE 77)
endif()
