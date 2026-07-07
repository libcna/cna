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
option(CNA_ENABLE_NET "Build CNA networking (GamerServices + Net, requires ENet)" ON)

include(cmake/ThirdPartySDL.cmake)
cna_configure_vendored_sdl()

if(CNA_ENABLE_NET)
    include(cmake/ThirdPartyENet.cmake)
    cna_configure_vendored_enet()
endif()

set(SHARP_RUNTIME_BUILD_TESTS OFF CACHE BOOL "Build sharp-runtime tests" FORCE)
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
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_devices PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            set_target_properties(cna_demo_devices PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_devices)
        endif()
    endif()
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
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 86: textured quad pixel readback
        cna_easygl_test(cna_test_easygl_textured_quad
                        examples/easygl_textured_quad_test.cpp)
        add_test(NAME EasyGL_TexturedQuad_Readback
                 COMMAND cna_test_easygl_textured_quad)
        set_tests_properties(EasyGL_TexturedQuad_Readback PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 87: RenderTarget2D → texture → readback
        cna_easygl_test(cna_test_easygl_render_target
                        examples/easygl_render_target_test.cpp)
        add_test(NAME EasyGL_RenderTarget2D_Readback
                 COMMAND cna_test_easygl_render_target)
        set_tests_properties(EasyGL_RenderTarget2D_Readback PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 335: RenderTarget2D depth buffer is real and functional (not just a stored property)
        cna_easygl_test(cna_test_easygl_rendertarget2d_depth
                        examples/rendertarget2d_depth_test.cpp)
        add_test(NAME EasyGL_RenderTarget2D_DepthBuffer
                 COMMAND cna_test_easygl_rendertarget2d_depth)
        set_tests_properties(EasyGL_RenderTarget2D_DepthBuffer PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 336: RenderTarget2D mip chain is genuinely GL-complete after auto-generation on unbind
        cna_easygl_test(cna_test_easygl_rendertarget2d_mip
                        examples/easygl_rendertarget2d_mip_test.cpp)
        add_test(NAME EasyGL_RenderTarget2D_MipComplete
                 COMMAND cna_test_easygl_rendertarget2d_mip)
        set_tests_properties(EasyGL_RenderTarget2D_MipComplete PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 337: RenderTarget2D MSAA resolve genuinely anti-aliases (not just solid-fill safe)
        cna_easygl_test(cna_test_easygl_rendertarget2d_msaa
                        examples/easygl_rendertarget2d_msaa_test.cpp)
        add_test(NAME EasyGL_RenderTarget2D_MsaaResolve
                 COMMAND cna_test_easygl_rendertarget2d_msaa)
        set_tests_properties(EasyGL_RenderTarget2D_MsaaResolve PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 338: SetRenderTarget(nullptr) resets Viewport/ScissorRectangle to the backbuffer
        cna_easygl_test(cna_test_easygl_rendertarget_viewport_scissor_reset
                        examples/rendertarget_viewport_scissor_reset_test.cpp)
        add_test(NAME EasyGL_RenderTarget_ViewportScissorReset
                 COMMAND cna_test_easygl_rendertarget_viewport_scissor_reset)
        set_tests_properties(EasyGL_RenderTarget_ViewportScissorReset PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 22: BasicEffect property tests
        cna_easygl_test(cna_test_basic_effect
                        examples/basic_effect_test.cpp)
        add_test(NAME EasyGL_BasicEffect_Properties
                 COMMAND cna_test_basic_effect)
        set_tests_properties(EasyGL_BasicEffect_Properties PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 23: AlphaTestEffect property tests
        cna_easygl_test(cna_test_alpha_test_effect
                        examples/alpha_test_effect_test.cpp)
        add_test(NAME EasyGL_AlphaTestEffect_Properties
                 COMMAND cna_test_alpha_test_effect)
        set_tests_properties(EasyGL_AlphaTestEffect_Properties PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 24: SkinnedEffect property tests
        cna_easygl_test(cna_test_skinned_effect
                        examples/skinned_effect_test.cpp)
        add_test(NAME EasyGL_SkinnedEffect_Properties
                 COMMAND cna_test_skinned_effect)
        set_tests_properties(EasyGL_SkinnedEffect_Properties PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 27: SpriteFont property and MeasureString tests
        cna_easygl_test(cna_test_sprite_font
                        examples/sprite_font_test.cpp)
        add_test(NAME EasyGL_SpriteFont_Properties
                 COMMAND cna_test_sprite_font)
        set_tests_properties(EasyGL_SpriteFont_Properties PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 35: OcclusionQuery begin/end/IsComplete integration test
        cna_easygl_test(cna_test_occlusion_query
                        examples/occlusion_query_test.cpp)
        add_test(NAME EasyGL_OcclusionQuery_Cycle
                 COMMAND cna_test_occlusion_query)
        set_tests_properties(EasyGL_OcclusionQuery_Cycle PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 122: AlphaTestEffect GPU alpha cutout + pixel readback
        cna_easygl_test(cna_test_alpha_test_integration
                        examples/alpha_test_integration_test.cpp)
        add_test(NAME EasyGL_AlphaTestEffect_AlphaCutout
                 COMMAND cna_test_alpha_test_integration)
        set_tests_properties(EasyGL_AlphaTestEffect_AlphaCutout PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 123: SkinnedEffect 2-bone GPU transform + mesh deformation
        cna_easygl_test(cna_test_skinned_integration
                        examples/skinned_effect_integration_test.cpp)
        add_test(NAME EasyGL_SkinnedEffect_BoneDeformation
                 COMMAND cna_test_skinned_integration)
        set_tests_properties(EasyGL_SkinnedEffect_BoneDeformation PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

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
                ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")
        endif()

        # Task 125: DXT1 texture loaded via FromStream, rendered, pixel readback
        cna_easygl_test(cna_test_dxt1_texture
                        examples/dxt1_texture_test.cpp)
        add_test(NAME EasyGL_DXT1_FromStream_Readback
                 COMMAND cna_test_dxt1_texture)
        set_tests_properties(EasyGL_DXT1_FromStream_Readback PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 132: ShaderEffect (GLSL) with SpriteBatch — red-tint over green background
        cna_easygl_test(cna_test_easygl_shader_effect
                        examples/easygl_shader_effect_test.cpp)
        add_test(NAME EasyGL_ShaderEffect_GLSL
                 COMMAND cna_test_easygl_shader_effect)
        set_tests_properties(EasyGL_ShaderEffect_GLSL PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 133: DualTextureEffect — magenta×yellow→red pixel readback
        cna_easygl_test(cna_test_easygl_dual_texture
                        examples/easygl_dual_texture_test.cpp)
        add_test(NAME EasyGL_DualTextureEffect_Blend
                 COMMAND cna_test_easygl_dual_texture)
        set_tests_properties(EasyGL_DualTextureEffect_Blend PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 134: EnvironmentMapEffect — emissive red, envAmount=0, pixel readback
        cna_easygl_test(cna_test_easygl_env_map
                        examples/easygl_env_map_test.cpp)
        add_test(NAME EasyGL_EnvironmentMapEffect_Readback
                 COMMAND cna_test_easygl_env_map)
        set_tests_properties(EasyGL_EnvironmentMapEffect_Readback PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 393: EnvironmentMapEffect EnvironmentMapAmount=0 should ignore the cube map
        cna_easygl_test(cna_test_easygl_environmentmapeffect_amount_zero
                        examples/easygl_environmentmapeffect_amount_zero_test.cpp)
        add_test(NAME EasyGL_EnvironmentMapEffect_AmountZero
                 COMMAND cna_test_easygl_environmentmapeffect_amount_zero)
        set_tests_properties(EasyGL_EnvironmentMapEffect_AmountZero PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 900: EnvironmentMapEffect fog fix verification
        cna_easygl_test(cna_test_easygl_environmentmapeffect_fog
                        examples/easygl_environmentmapeffect_fog_test.cpp)
        add_test(NAME EasyGL_EnvironmentMapEffect_Fog
                 COMMAND cna_test_easygl_environmentmapeffect_fog)
        set_tests_properties(EasyGL_EnvironmentMapEffect_Fog PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 394: EnvironmentMapEffect EnvironmentMapAmount=1 should fully replace lit color
        cna_easygl_test(cna_test_easygl_environmentmapeffect_amount_one
                        examples/easygl_environmentmapeffect_amount_one_test.cpp)
        add_test(NAME EasyGL_EnvironmentMapEffect_AmountOne
                 COMMAND cna_test_easygl_environmentmapeffect_amount_one)
        set_tests_properties(EasyGL_EnvironmentMapEffect_AmountOne PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 395: EnvironmentMapEffect EnvironmentMapSpecular should scale by cubemap alpha
        cna_easygl_test(cna_test_easygl_environmentmapeffect_specular
                        examples/easygl_environmentmapeffect_specular_test.cpp)
        add_test(NAME EasyGL_EnvironmentMapEffect_Specular
                 COMMAND cna_test_easygl_environmentmapeffect_specular)
        set_tests_properties(EasyGL_EnvironmentMapEffect_Specular PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 396: EnvironmentMapEffect Fresnel edge-weighting should suppress head-on reflections
        cna_easygl_test(cna_test_easygl_environmentmapeffect_fresnel
                        examples/easygl_environmentmapeffect_fresnel_test.cpp)
        add_test(NAME EasyGL_EnvironmentMapEffect_Fresnel
                 COMMAND cna_test_easygl_environmentmapeffect_fresnel)
        set_tests_properties(EasyGL_EnvironmentMapEffect_Fresnel PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 397: EnvironmentMapEffect reflection vector should respond to EyePosition
        cna_easygl_test(cna_test_easygl_environmentmapeffect_eyeposition
                        examples/easygl_environmentmapeffect_eyeposition_test.cpp)
        add_test(NAME EasyGL_EnvironmentMapEffect_EyePosition
                 COMMAND cna_test_easygl_environmentmapeffect_eyeposition)
        set_tests_properties(EasyGL_EnvironmentMapEffect_EyePosition PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 398: EnvironmentMapEffect normal transform should use inverse-transpose of World
        cna_easygl_test(cna_test_easygl_environmentmapeffect_worldtransform
                        examples/easygl_environmentmapeffect_worldtransform_test.cpp)
        add_test(NAME EasyGL_EnvironmentMapEffect_WorldTransform
                 COMMAND cna_test_easygl_environmentmapeffect_worldtransform)
        set_tests_properties(EasyGL_EnvironmentMapEffect_WorldTransform PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 399: cross-backend EnvironmentMapEffect capstone (Tasks 393-398 combined)
        cna_easygl_test(cna_test_easygl_environmentmapeffect_combined
                        examples/easygl_environmentmapeffect_combined_test.cpp)
        add_test(NAME EasyGL_EnvironmentMapEffect_Combined
                 COMMAND cna_test_easygl_environmentmapeffect_combined)
        set_tests_properties(EasyGL_EnvironmentMapEffect_Combined PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 900: SkinnedEffect fog fix verification
        cna_easygl_test(cna_test_easygl_skinnedeffect_fog
                        examples/easygl_skinnedeffect_fog_test.cpp)
        add_test(NAME EasyGL_SkinnedEffect_Fog
                 COMMAND cna_test_easygl_skinnedeffect_fog)
        set_tests_properties(EasyGL_SkinnedEffect_Fog PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 406: SkinnedEffect identity bone palette should produce zero deformation
        cna_easygl_test(cna_test_easygl_skinnedeffect_identity_bones
                        examples/easygl_skinnedeffect_identity_bones_test.cpp)
        add_test(NAME EasyGL_SkinnedEffect_IdentityBones
                 COMMAND cna_test_easygl_skinnedeffect_identity_bones)
        set_tests_properties(EasyGL_SkinnedEffect_IdentityBones PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 407: SkinnedEffect single translation bone should shift the mesh
        cna_easygl_test(cna_test_easygl_skinnedeffect_translation_bone
                        examples/easygl_skinnedeffect_translation_bone_test.cpp)
        add_test(NAME EasyGL_SkinnedEffect_TranslationBone
                 COMMAND cna_test_easygl_skinnedeffect_translation_bone)
        set_tests_properties(EasyGL_SkinnedEffect_TranslationBone PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 408: SkinnedEffect two-bone weighted blend should produce midpoint deformation
        cna_easygl_test(cna_test_easygl_skinnedeffect_twobone_blend
                        examples/easygl_skinnedeffect_twobone_blend_test.cpp)
        add_test(NAME EasyGL_SkinnedEffect_TwoBoneBlend
                 COMMAND cna_test_easygl_skinnedeffect_twobone_blend)
        set_tests_properties(EasyGL_SkinnedEffect_TwoBoneBlend PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 409: cross-backend SkinnedEffect capstone (identity + single-bone + two-bone blend)
        cna_easygl_test(cna_test_easygl_skinnedeffect_combined
                        examples/easygl_skinnedeffect_combined_test.cpp)
        add_test(NAME EasyGL_SkinnedEffect_Combined
                 COMMAND cna_test_easygl_skinnedeffect_combined)
        set_tests_properties(EasyGL_SkinnedEffect_Combined PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 334: RenderTargetCube sampled as TextureCube via EnvironmentMapEffect after unbinding
        cna_easygl_test(cna_test_easygl_rendertargetcube_sample
                        examples/easygl_rendertargetcube_sample_test.cpp)
        add_test(NAME EasyGL_RenderTargetCube_SampleAfterUnbind
                 COMMAND cna_test_easygl_rendertargetcube_sample)
        set_tests_properties(EasyGL_RenderTargetCube_SampleAfterUnbind PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 144: Model.Draw — 2-bone model, BasicEffect VertexColor, red quad, pixel readback
        cna_easygl_test(cna_test_easygl_model_draw
                        examples/easygl_model_draw_test.cpp)
        add_test(NAME EasyGL_ModelDraw_RedQuad
                 COMMAND cna_test_easygl_model_draw)
        set_tests_properties(EasyGL_ModelDraw_RedQuad PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 145: MRT — SetRenderTargets({rt0,rt1}), draw green to attachment0, verify left=green right=blue
        cna_easygl_test(cna_test_easygl_mrt
                        examples/easygl_mrt_test.cpp)
        add_test(NAME EasyGL_MRT_TwoAttachments
                 COMMAND cna_test_easygl_mrt)
        set_tests_properties(EasyGL_MRT_TwoAttachments PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 146: MSAA 4× — multisampled renderbuffer + resolve on Present
        cna_easygl_test(cna_test_easygl_msaa
                        examples/easygl_msaa_test.cpp)
        add_test(NAME EasyGL_MSAA_4x_Readback
                 COMMAND cna_test_easygl_msaa)
        set_tests_properties(EasyGL_MSAA_4x_Readback PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 167: SpriteEffects — FlipHorizontally, FlipVertically, FlipHV pixel readback
        cna_easygl_test(cna_test_easygl_sprite_effects
                        examples/easygl_sprite_effects_test.cpp)
        add_test(NAME EasyGL_SpriteEffects_Flip
                 COMMAND cna_test_easygl_sprite_effects)
        set_tests_properties(EasyGL_SpriteEffects_Flip PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 417: SpriteBatch Draw rotation must pivot around the specified origin
        cna_easygl_test(cna_test_easygl_spritebatch_rotation
                        examples/easygl_spritebatch_rotation_test.cpp)
        add_test(NAME EasyGL_SpriteBatch_RotationAroundOrigin
                 COMMAND cna_test_easygl_spritebatch_rotation)
        set_tests_properties(EasyGL_SpriteBatch_RotationAroundOrigin PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 418: SpriteBatch Draw scalar/Vector2 scale overloads produce expected size
        cna_easygl_test(cna_test_easygl_spritebatch_scale
                        examples/easygl_spritebatch_scale_test.cpp)
        add_test(NAME EasyGL_SpriteBatch_ScaleOverloads
                 COMMAND cna_test_easygl_spritebatch_scale)
        set_tests_properties(EasyGL_SpriteBatch_ScaleOverloads PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 419: SpriteBatch Draw sourceRectangle must crop, not stretch the whole texture
        cna_easygl_test(cna_test_easygl_spritebatch_sourcerect
                        examples/easygl_spritebatch_sourcerect_test.cpp)
        add_test(NAME EasyGL_SpriteBatch_SourceRectangleCropping
                 COMMAND cna_test_easygl_spritebatch_sourcerect)
        set_tests_properties(EasyGL_SpriteBatch_SourceRectangleCropping PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 420: layerDepth must affect on-screen draw order under FrontToBack
        cna_easygl_test(cna_test_easygl_spritebatch_layerdepth
                        examples/easygl_spritebatch_layerdepth_test.cpp)
        add_test(NAME EasyGL_SpriteBatch_LayerDepthOrder
                 COMMAND cna_test_easygl_spritebatch_layerdepth)
        set_tests_properties(EasyGL_SpriteBatch_LayerDepthOrder PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 885: BasicEffect pixel test — DirectionalLight1/2 + EmissiveColor on the lit path
        cna_easygl_test(cna_test_easygl_basiceffect_multilight_emissive
                        examples/easygl_basiceffect_multilight_emissive_test.cpp)
        add_test(NAME EasyGL_BasicEffect_MultiLightEmissive
                 COMMAND cna_test_easygl_basiceffect_multilight_emissive)
        set_tests_properties(EasyGL_BasicEffect_MultiLightEmissive PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 886: BasicEffect pixel test — real specular highlights
        cna_easygl_test(cna_test_easygl_basiceffect_specular
                        examples/easygl_basiceffect_specular_test.cpp)
        add_test(NAME EasyGL_BasicEffect_Specular
                 COMMAND cna_test_easygl_basiceffect_specular)
        set_tests_properties(EasyGL_BasicEffect_Specular PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 168: transformMatrix in SpriteBatch::Begin — translation pixel readback
        cna_easygl_test(cna_test_easygl_transform_matrix
                        examples/easygl_transform_matrix_test.cpp)
        add_test(NAME EasyGL_TransformMatrix_Translation
                 COMMAND cna_test_easygl_transform_matrix)
        set_tests_properties(EasyGL_TransformMatrix_Translation PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 169: Texture2D SetData/GetData partial rectangle round-trip
        cna_easygl_test(cna_test_easygl_texture2d_partial_rect
                        examples/easygl_texture2d_partial_rect_test.cpp)
        add_test(NAME EasyGL_Texture2D_PartialRect_RoundTrip
                 COMMAND cna_test_easygl_texture2d_partial_rect)
        set_tests_properties(EasyGL_Texture2D_PartialRect_RoundTrip PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 171: Texture2D mip-level SetData/GetData round-trip
        cna_easygl_test(cna_test_easygl_texture2d_mip
                        examples/easygl_texture2d_mip_test.cpp)
        add_test(NAME EasyGL_Texture2D_Mip_RoundTrip
                 COMMAND cna_test_easygl_texture2d_mip)
        set_tests_properties(EasyGL_Texture2D_Mip_RoundTrip PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 172: TextureCube per-face SetData/GetData round-trip
        cna_easygl_test(cna_test_easygl_texturecube_faces
                        examples/easygl_texturecube_faces_test.cpp)
        add_test(NAME EasyGL_TextureCube_Faces_RoundTrip
                 COMMAND cna_test_easygl_texturecube_faces)
        set_tests_properties(EasyGL_TextureCube_Faces_RoundTrip PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 276: TextureCube mip-level SetData/GetData round-trip, all six faces
        cna_easygl_test(cna_test_easygl_texturecube_mip
                        examples/easygl_texturecube_mip_test.cpp)
        add_test(NAME EasyGL_TextureCube_Mip_RoundTrip
                 COMMAND cna_test_easygl_texturecube_mip)
        set_tests_properties(EasyGL_TextureCube_Mip_RoundTrip PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 275: TextureCube partial rect + startIndex, all six faces
        cna_easygl_test(cna_test_easygl_texturecube_partial_rect
                        examples/easygl_texturecube_partial_rect_test.cpp)
        add_test(NAME EasyGL_TextureCube_PartialRect_RoundTrip
                 COMMAND cna_test_easygl_texturecube_partial_rect)
        set_tests_properties(EasyGL_TextureCube_PartialRect_RoundTrip PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 173: Texture3D z-slice SetData/GetData round-trip
        cna_easygl_test(cna_test_easygl_texture3d_slices
                        examples/easygl_texture3d_slices_test.cpp)
        add_test(NAME EasyGL_Texture3D_Slices_RoundTrip
                 COMMAND cna_test_easygl_texture3d_slices)
        set_tests_properties(EasyGL_Texture3D_Slices_RoundTrip PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 273: Texture3D partial box (x/y/z region) SetData/GetData round-trip
        cna_easygl_test(cna_test_easygl_texture3d_partial_box
                        examples/easygl_texture3d_partial_box_test.cpp)
        add_test(NAME EasyGL_Texture3D_PartialBox_RoundTrip
                 COMMAND cna_test_easygl_texture3d_partial_box)
        set_tests_properties(EasyGL_Texture3D_PartialBox_RoundTrip PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 274: Texture3D partial box (x/y/z region) GetData readback
        cna_easygl_test(cna_test_easygl_texture3d_partial_box_readback
                        examples/easygl_texture3d_partial_box_readback_test.cpp)
        add_test(NAME EasyGL_Texture3D_PartialBox_Readback
                 COMMAND cna_test_easygl_texture3d_partial_box_readback)
        set_tests_properties(EasyGL_Texture3D_PartialBox_Readback PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 176: SurfaceFormat validation — unsupported formats throw
        cna_easygl_test(cna_test_easygl_surface_format_throws
                        examples/easygl_surface_format_throws_test.cpp)
        add_test(NAME EasyGL_SurfaceFormat_Throws
                 COMMAND cna_test_easygl_surface_format_throws)
        set_tests_properties(EasyGL_SurfaceFormat_Throws PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 177: RenderTargetUsage DiscardContents vs PreserveContents
        cna_easygl_test(cna_test_easygl_render_target_usage
                        examples/easygl_render_target_usage_test.cpp)
        add_test(NAME EasyGL_RenderTargetUsage
                 COMMAND cna_test_easygl_render_target_usage)
        set_tests_properties(EasyGL_RenderTargetUsage PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 180: backbuffer → RT → backbuffer → RT → backbuffer round-trip
        cna_easygl_test(cna_test_easygl_rt_roundtrip
                        examples/easygl_rt_roundtrip_test.cpp)
        add_test(NAME EasyGL_RT_Roundtrip
                 COMMAND cna_test_easygl_rt_roundtrip)
        set_tests_properties(EasyGL_RT_Roundtrip PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 182: PresentationParameters round-trip via GraphicsDeviceManager
        cna_easygl_test(cna_test_easygl_presentation_parameters
                        examples/easygl_presentation_parameters_test.cpp)
        add_test(NAME EasyGL_PresentationParameters
                 COMMAND cna_test_easygl_presentation_parameters)
        set_tests_properties(EasyGL_PresentationParameters PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        cna_easygl_test(cna_test_easygl_device_reset_events
                        examples/easygl_device_reset_events_test.cpp)
        add_test(NAME EasyGL_DeviceResetEvents
                 COMMAND cna_test_easygl_device_reset_events)
        set_tests_properties(EasyGL_DeviceResetEvents PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        cna_easygl_test(cna_test_easygl_device_validation
                        examples/easygl_device_validation_test.cpp)
        add_test(NAME EasyGL_DeviceValidation
                 COMMAND cna_test_easygl_device_validation)
        set_tests_properties(EasyGL_DeviceValidation PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        cna_easygl_test(cna_test_easygl_draw_novertexbuffer
                        examples/easygl_draw_novertexbuffer_test.cpp)
        add_test(NAME EasyGL_DrawNoVertexBuffer
                 COMMAND cna_test_easygl_draw_novertexbuffer)
        set_tests_properties(EasyGL_DrawNoVertexBuffer PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        cna_easygl_test(cna_test_easygl_draw_noindexbuffer
                        examples/easygl_draw_noindexbuffer_test.cpp)
        add_test(NAME EasyGL_DrawNoIndexBuffer
                 COMMAND cna_test_easygl_draw_noindexbuffer)
        set_tests_properties(EasyGL_DrawNoIndexBuffer PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        cna_easygl_test(cna_test_easygl_draw_range_validation
                        examples/easygl_draw_range_validation_test.cpp)
        add_test(NAME EasyGL_DrawRangeValidation
                 COMMAND cna_test_easygl_draw_range_validation)
        set_tests_properties(EasyGL_DrawRangeValidation PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        cna_easygl_test(cna_test_easygl_primitivetype_validation
                        examples/easygl_primitivetype_validation_test.cpp)
        add_test(NAME EasyGL_PrimitiveTypeValidation
                 COMMAND cna_test_easygl_primitivetype_validation)
        set_tests_properties(EasyGL_PrimitiveTypeValidation PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        cna_easygl_test(cna_test_easygl_clear_overloads
                        examples/easygl_clear_overloads_test.cpp)
        add_test(NAME EasyGL_ClearOverloads
                 COMMAND cna_test_easygl_clear_overloads)
        set_tests_properties(EasyGL_ClearOverloads PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        cna_easygl_test(cna_test_easygl_viewport_state
                        examples/easygl_viewport_state_test.cpp)
        add_test(NAME EasyGL_ViewportState
                 COMMAND cna_test_easygl_viewport_state)
        set_tests_properties(EasyGL_ViewportState PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        cna_easygl_test(cna_test_easygl_scissor
                        examples/easygl_scissor_test.cpp)
        add_test(NAME EasyGL_Scissor
                 COMMAND cna_test_easygl_scissor)
        set_tests_properties(EasyGL_Scissor PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 880: Viewport sub-region (split-screen) GPU wiring
        cna_easygl_test(cna_test_easygl_viewport_subregion
                        examples/easygl_viewport_subregion_test.cpp)
        add_test(NAME EasyGL_Viewport_Subregion
                 COMMAND cna_test_easygl_viewport_subregion)
        set_tests_properties(EasyGL_Viewport_Subregion PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        cna_easygl_test(cna_test_easygl_disposed_resource
                        examples/easygl_disposed_resource_test.cpp)
        add_test(NAME EasyGL_DisposedResource
                 COMMAND cna_test_easygl_disposed_resource)
        set_tests_properties(EasyGL_DisposedResource PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        cna_easygl_test(cna_test_easygl_double_dispose
                        examples/easygl_double_dispose_test.cpp)
        add_test(NAME EasyGL_DoubleDispose
                 COMMAND cna_test_easygl_double_dispose)
        set_tests_properties(EasyGL_DoubleDispose PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        cna_easygl_test(cna_test_easygl_bound_resource_dispose
                        examples/easygl_bound_resource_dispose_test.cpp)
        add_test(NAME EasyGL_BoundResourceDispose
                 COMMAND cna_test_easygl_bound_resource_dispose)
        set_tests_properties(EasyGL_BoundResourceDispose PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        cna_easygl_test(cna_test_easygl_handle_release
                        examples/easygl_handle_release_test.cpp)
        add_test(NAME EasyGL_HandleRelease
                 COMMAND cna_test_easygl_handle_release)
        set_tests_properties(EasyGL_HandleRelease PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        cna_easygl_test(cna_test_easygl_move_semantics
                        examples/easygl_move_semantics_test.cpp)
        add_test(NAME EasyGL_MoveSemantics
                 COMMAND cna_test_easygl_move_semantics)
        set_tests_properties(EasyGL_MoveSemantics PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        cna_easygl_test(cna_test_easygl_resource_events
                        examples/easygl_resource_events_test.cpp)
        add_test(NAME EasyGL_ResourceEvents
                 COMMAND cna_test_easygl_resource_events)
        set_tests_properties(EasyGL_ResourceEvents PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        cna_easygl_test(cna_test_easygl_device_dispose_order
                        examples/easygl_device_dispose_order_test.cpp)
        add_test(NAME EasyGL_DeviceDisposeOrder
                 COMMAND cna_test_easygl_device_dispose_order)
        set_tests_properties(EasyGL_DeviceDisposeOrder PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        cna_easygl_test(cna_test_easygl_resource_leak
                        examples/easygl_resource_leak_test.cpp)
        add_test(NAME EasyGL_ResourceLeak
                 COMMAND cna_test_easygl_resource_leak)
        set_tests_properties(EasyGL_ResourceLeak PROPERTIES
            TIMEOUT 60
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        cna_easygl_test(cna_test_easygl_present_interval
                        examples/easygl_present_interval_test.cpp)
        add_test(NAME EasyGL_PresentInterval
                 COMMAND cna_test_easygl_present_interval)
        set_tests_properties(EasyGL_PresentInterval PROPERTIES
            TIMEOUT 60
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        cna_easygl_test(cna_test_easygl_fullscreen_field
                        examples/easygl_fullscreen_field_test.cpp)
        add_test(NAME EasyGL_FullScreenField
                 COMMAND cna_test_easygl_fullscreen_field)
        set_tests_properties(EasyGL_FullScreenField PROPERTIES
            TIMEOUT 60
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        cna_easygl_test(cna_test_easygl_backbuffer_resize
                        examples/easygl_backbuffer_resize_test.cpp)
        add_test(NAME EasyGL_BackbufferResize
                 COMMAND cna_test_easygl_backbuffer_resize)
        set_tests_properties(EasyGL_BackbufferResize PROPERTIES
            TIMEOUT 60
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        cna_easygl_test(cna_test_easygl_real_window_resize
                        examples/easygl_real_window_resize_test.cpp)
        add_test(NAME EasyGL_RealWindowResize
                 COMMAND cna_test_easygl_real_window_resize)
        set_tests_properties(EasyGL_RealWindowResize PROPERTIES
            TIMEOUT 60
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 349: custom Viewport survives Present() with no resize, resets after a real one.
        cna_easygl_test(cna_test_easygl_viewport_reset_after_resize
                        examples/viewport_reset_after_resize_test.cpp)
        add_test(NAME EasyGL_ViewportResetAfterResize
                 COMMAND cna_test_easygl_viewport_reset_after_resize)
        set_tests_properties(EasyGL_ViewportResetAfterResize PROPERTIES
            TIMEOUT 60
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        cna_easygl_test(cna_test_easygl_depth_format
                        examples/easygl_depth_format_test.cpp)
        add_test(NAME EasyGL_DepthFormat
                 COMMAND cna_test_easygl_depth_format)
        set_tests_properties(EasyGL_DepthFormat PROPERTIES
            TIMEOUT 60
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        cna_easygl_test(cna_test_easygl_msaa_change
                        examples/easygl_msaa_change_test.cpp)
        add_test(NAME EasyGL_MsaaChange
                 COMMAND cna_test_easygl_msaa_change)
        set_tests_properties(EasyGL_MsaaChange PROPERTIES
            TIMEOUT 60
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        cna_easygl_test(cna_test_easygl_vertexbuffer_setdata
                        examples/easygl_vertexbuffer_setdata_test.cpp)
        add_test(NAME EasyGL_VbSetData
                 COMMAND cna_test_easygl_vertexbuffer_setdata)
        set_tests_properties(EasyGL_VbSetData PROPERTIES
            TIMEOUT 60
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        cna_easygl_test(cna_test_easygl_dynamic_buffer_stress
                        examples/easygl_dynamic_buffer_stress_test.cpp)
        add_test(NAME EasyGL_DynamicBufferStress
                 COMMAND cna_test_easygl_dynamic_buffer_stress)
        set_tests_properties(EasyGL_DynamicBufferStress PROPERTIES
            TIMEOUT 60
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        cna_easygl_test(cna_test_easygl_buffer_usage
                        examples/easygl_buffer_usage_test.cpp)
        add_test(NAME EasyGL_BufferUsage
                 COMMAND cna_test_easygl_buffer_usage)
        set_tests_properties(EasyGL_BufferUsage PROPERTIES
            TIMEOUT 60
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        cna_easygl_test(cna_test_easygl_disposed_buffer
                        examples/easygl_disposed_buffer_test.cpp)
        add_test(NAME EasyGL_DisposedBuffer
                 COMMAND cna_test_easygl_disposed_buffer)
        set_tests_properties(EasyGL_DisposedBuffer PROPERTIES
            TIMEOUT 60
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        cna_easygl_test(cna_test_easygl_effect_clone
                        examples/easygl_effect_clone_test.cpp)
        add_test(NAME EasyGL_EffectClone
                 COMMAND cna_test_easygl_effect_clone)
        set_tests_properties(EasyGL_EffectClone PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        cna_easygl_test(cna_test_easygl_effect_current_technique
                        examples/easygl_effect_current_technique_test.cpp)
        add_test(NAME EasyGL_EffectCurrentTechnique
                 COMMAND cna_test_easygl_effect_current_technique)
        set_tests_properties(EasyGL_EffectCurrentTechnique PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        cna_easygl_test(cna_test_easygl_basiceffect_combinations
                        examples/easygl_basiceffect_combinations_test.cpp)
        add_test(NAME EasyGL_BasicEffectCombinations
                 COMMAND cna_test_easygl_basiceffect_combinations)
        set_tests_properties(EasyGL_BasicEffectCombinations PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 364: BasicEffect pixel test — VertexColorEnabled=false, no texture, diffuse color only
        cna_easygl_test(cna_test_easygl_basiceffect_vertexcolor_disabled
                        examples/easygl_basiceffect_vertexcolor_disabled_test.cpp)
        add_test(NAME EasyGL_BasicEffect_VertexColorDisabled
                 COMMAND cna_test_easygl_basiceffect_vertexcolor_disabled)
        set_tests_properties(EasyGL_BasicEffect_VertexColorDisabled PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 365: BasicEffect pixel test — VertexColorEnabled=true, no texture, vertex color multiplication
        cna_easygl_test(cna_test_easygl_basiceffect_vertexcolor_enabled
                        examples/easygl_basiceffect_vertexcolor_enabled_test.cpp)
        add_test(NAME EasyGL_BasicEffect_VertexColorEnabled
                 COMMAND cna_test_easygl_basiceffect_vertexcolor_enabled)
        set_tests_properties(EasyGL_BasicEffect_VertexColorEnabled PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 366: BasicEffect pixel test — TextureEnabled=true, no vertex color
        cna_easygl_test(cna_test_easygl_basiceffect_texture_enabled
                        examples/easygl_basiceffect_texture_enabled_test.cpp)
        add_test(NAME EasyGL_BasicEffect_TextureEnabled
                 COMMAND cna_test_easygl_basiceffect_texture_enabled)
        set_tests_properties(EasyGL_BasicEffect_TextureEnabled PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 367: BasicEffect pixel test — TextureEnabled=true AND VertexColorEnabled=true
        cna_easygl_test(cna_test_easygl_basiceffect_texture_vertexcolor_enabled
                        examples/easygl_basiceffect_texture_vertexcolor_enabled_test.cpp)
        add_test(NAME EasyGL_BasicEffect_TextureVertexColorEnabled
                 COMMAND cna_test_easygl_basiceffect_texture_vertexcolor_enabled)
        set_tests_properties(EasyGL_BasicEffect_TextureVertexColorEnabled PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 368: BasicEffect pixel test — LightingEnabled=true, one DirectionalLight
        cna_easygl_test(cna_test_easygl_basiceffect_one_light
                        examples/easygl_basiceffect_one_light_test.cpp)
        add_test(NAME EasyGL_BasicEffect_OneLight
                 COMMAND cna_test_easygl_basiceffect_one_light)
        set_tests_properties(EasyGL_BasicEffect_OneLight PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 369: BasicEffect pixel test — DiffuseColor+EmissiveColor, LightingEnabled=false
        cna_easygl_test(cna_test_easygl_basiceffect_emissive
                        examples/easygl_basiceffect_emissive_test.cpp)
        add_test(NAME EasyGL_BasicEffect_Emissive
                 COMMAND cna_test_easygl_basiceffect_emissive)
        set_tests_properties(EasyGL_BasicEffect_Emissive PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 370: BasicEffect cross-backend image comparison suite — closes Phase 42
        cna_easygl_test(cna_test_easygl_basiceffect_combined
                        examples/easygl_basiceffect_combined_test.cpp)
        add_test(NAME EasyGL_BasicEffect_Combined
                 COMMAND cna_test_easygl_basiceffect_combined)
        set_tests_properties(EasyGL_BasicEffect_Combined PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        cna_easygl_test(cna_test_easygl_alphatest_modes
                        examples/easygl_alphatest_modes_test.cpp)
        add_test(NAME EasyGL_AlphaTestModes
                 COMMAND cna_test_easygl_alphatest_modes)
        set_tests_properties(EasyGL_AlphaTestModes PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 373: AlphaTestEffect all CompareFunction modes — threshold sweep (below/at/above)
        cna_easygl_test(cna_test_easygl_alphatest_comparefunction_sweep
                        examples/easygl_alphatest_comparefunction_sweep_test.cpp)
        add_test(NAME EasyGL_AlphaTest_CompareFunctionSweep
                 COMMAND cna_test_easygl_alphatest_comparefunction_sweep)
        set_tests_properties(EasyGL_AlphaTest_CompareFunctionSweep PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 377: AlphaTestEffect vertex color x diffuse color interaction
        cna_easygl_test(cna_test_easygl_alphatest_vertexcolor_diffuse
                        examples/easygl_alphatest_vertexcolor_diffuse_test.cpp)
        add_test(NAME EasyGL_AlphaTest_VertexColorDiffuse
                 COMMAND cna_test_easygl_alphatest_vertexcolor_diffuse)
        set_tests_properties(EasyGL_AlphaTest_VertexColorDiffuse PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 378: AlphaTestEffect fog behavior
        cna_easygl_test(cna_test_easygl_alphatest_fog
                        examples/easygl_alphatest_fog_test.cpp)
        add_test(NAME EasyGL_AlphaTest_Fog
                 COMMAND cna_test_easygl_alphatest_fog)
        set_tests_properties(EasyGL_AlphaTest_Fog PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 379: AlphaTestEffect null/no-texture behavior
        cna_easygl_test(cna_test_easygl_alphatest_null_texture
                        examples/easygl_alphatest_null_texture_test.cpp)
        add_test(NAME EasyGL_AlphaTest_NullTexture
                 COMMAND cna_test_easygl_alphatest_null_texture)
        set_tests_properties(EasyGL_AlphaTest_NullTexture PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 383: DualTextureEffect two-texture blend formula, including FNA's color.rgb*=2
        cna_easygl_test(cna_test_easygl_dualtextureeffect_doubling
                        examples/easygl_dualtextureeffect_doubling_test.cpp)
        add_test(NAME EasyGL_DualTextureEffect_Doubling
                 COMMAND cna_test_easygl_dualtextureeffect_doubling)
        set_tests_properties(EasyGL_DualTextureEffect_Doubling PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 385: DualTextureEffect Alpha premultiplication, verified via real AlphaBlend
        cna_easygl_test(cna_test_easygl_dualtextureeffect_alpha
                        examples/easygl_dualtextureeffect_alpha_test.cpp)
        add_test(NAME EasyGL_DualTextureEffect_Alpha
                 COMMAND cna_test_easygl_dualtextureeffect_alpha)
        set_tests_properties(EasyGL_DualTextureEffect_Alpha PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 386: DualTextureEffect first texture (Texture, slot 0) null behavior
        cna_easygl_test(cna_test_easygl_dualtextureeffect_null_texture0
                        examples/easygl_dualtextureeffect_null_texture0_test.cpp)
        add_test(NAME EasyGL_DualTextureEffect_NullTexture0
                 COMMAND cna_test_easygl_dualtextureeffect_null_texture0)
        set_tests_properties(EasyGL_DualTextureEffect_NullTexture0 PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 387: DualTextureEffect second texture (Texture2, slot 1) null behavior
        cna_easygl_test(cna_test_easygl_dualtextureeffect_null_texture2
                        examples/easygl_dualtextureeffect_null_texture2_test.cpp)
        add_test(NAME EasyGL_DualTextureEffect_NullTexture2
                 COMMAND cna_test_easygl_dualtextureeffect_null_texture2)
        set_tests_properties(EasyGL_DualTextureEffect_NullTexture2 PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 388: DualTextureEffect fog behavior
        cna_easygl_test(cna_test_easygl_dualtextureeffect_fog
                        examples/easygl_dualtextureeffect_fog_test.cpp)
        add_test(NAME EasyGL_DualTextureEffect_Fog
                 COMMAND cna_test_easygl_dualtextureeffect_fog)
        set_tests_properties(EasyGL_DualTextureEffect_Fog PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 389: DualTextureEffect cross-backend image comparison suite (closes Phase 44 pixel work)
        cna_easygl_test(cna_test_easygl_dualtextureeffect_combined
                        examples/easygl_dualtextureeffect_combined_test.cpp)
        add_test(NAME EasyGL_DualTextureEffect_Combined
                 COMMAND cna_test_easygl_dualtextureeffect_combined)
        set_tests_properties(EasyGL_DualTextureEffect_Combined PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        cna_easygl_test(cna_test_easygl_dualtexture
                        examples/easygl_dualtexture_test.cpp)
        add_test(NAME EasyGL_DualTexture
                 COMMAND cna_test_easygl_dualtexture)
        set_tests_properties(EasyGL_DualTexture PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        cna_easygl_test(cna_test_easygl_skinned_effect_bones
                        examples/easygl_skinned_effect_bones_test.cpp)
        add_test(NAME EasyGL_SkinnedBones
                 COMMAND cna_test_easygl_skinned_effect_bones)
        set_tests_properties(EasyGL_SkinnedBones PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        cna_easygl_test(cna_test_easygl_basiceffect_default_lighting
                        examples/easygl_basiceffect_default_lighting_test.cpp)
        add_test(NAME EasyGL_BasicEffectDefaultLighting
                 COMMAND cna_test_easygl_basiceffect_default_lighting)
        set_tests_properties(EasyGL_BasicEffectDefaultLighting PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        cna_easygl_test(cna_test_easygl_basiceffect_fog
                        examples/easygl_basiceffect_fog_test.cpp)
        add_test(NAME EasyGL_BasicEffectFog
                 COMMAND cna_test_easygl_basiceffect_fog)
        set_tests_properties(EasyGL_BasicEffectFog PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 247: draw quad with each supported VertexElementFormat via VertexBuffer
        cna_easygl_test(cna_test_easygl_vertex_formats
                        examples/easygl_vertex_formats_test.cpp)
        add_test(NAME EasyGL_VertexFormats_AllStrides
                 COMMAND cna_test_easygl_vertex_formats)
        set_tests_properties(EasyGL_VertexFormats_AllStrides PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 255: DrawUserPrimitives<VertexPositionColor> pixel-readback
        cna_easygl_test(cna_test_easygl_draw_user_primitives_vpc
                        examples/easygl_draw_user_primitives_vpc_test.cpp)
        add_test(NAME EasyGL_DrawUserPrimitives_VPC
                 COMMAND cna_test_easygl_draw_user_primitives_vpc)
        set_tests_properties(EasyGL_DrawUserPrimitives_VPC PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 256: DrawUserPrimitives with explicit VertexDeclaration pixel-readback
        cna_easygl_test(cna_test_easygl_draw_user_primitives_custom
                        examples/easygl_draw_user_primitives_custom_test.cpp)
        add_test(NAME EasyGL_DrawUserPrimitives_CustomVD
                 COMMAND cna_test_easygl_draw_user_primitives_custom)
        set_tests_properties(EasyGL_DrawUserPrimitives_CustomVD PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 257: DrawUserIndexedPrimitives<VertexPositionColor> (16-bit indices) pixel-readback
        cna_easygl_test(cna_test_easygl_draw_user_indexed_primitives_vpc
                        examples/easygl_draw_user_indexed_primitives_vpc_test.cpp)
        add_test(NAME EasyGL_DrawUserIndexedPrimitives_VPC
                 COMMAND cna_test_easygl_draw_user_indexed_primitives_vpc)
        set_tests_properties(EasyGL_DrawUserIndexedPrimitives_VPC PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 258: DrawUserIndexedPrimitives<VertexPositionColor> (32-bit indices) pixel-readback
        cna_easygl_test(cna_test_easygl_draw_user_indexed_primitives_32
                        examples/easygl_draw_user_indexed_primitives_32_test.cpp)
        add_test(NAME EasyGL_DrawUserIndexedPrimitives_32
                 COMMAND cna_test_easygl_draw_user_indexed_primitives_32)
        set_tests_properties(EasyGL_DrawUserIndexedPrimitives_32 PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 268: non-power-of-two (3x5) texture upload + GPU-sampled pixel-readback
        cna_easygl_test(cna_test_easygl_npot_texture
                        examples/easygl_npot_texture_test.cpp)
        add_test(NAME EasyGL_NpotTexture
                 COMMAND cna_test_easygl_npot_texture)
        set_tests_properties(EasyGL_NpotTexture PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 269: SpriteBatch TextureAddressMode (Wrap vs Clamp) edge-sampling pixel-readback
        cna_easygl_test(cna_test_easygl_texture_address_mode
                        examples/easygl_texture_address_mode_test.cpp)
        add_test(NAME EasyGL_TextureAddressMode
                 COMMAND cna_test_easygl_texture_address_mode)
        set_tests_properties(EasyGL_TextureAddressMode PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 293: DualTextureEffect (3D stock effect) honors GraphicsDevice.SamplerStates[0]
        cna_easygl_test(cna_test_easygl_sampler_state_effect
                        examples/easygl_sampler_state_effect_test.cpp)
        add_test(NAME EasyGL_SamplerState_DualTextureEffect
                 COMMAND cna_test_easygl_sampler_state_effect)
        set_tests_properties(EasyGL_SamplerState_DualTextureEffect PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 294: TextureAddressMode::Clamp on a 3D stock effect (DualTextureEffect)
        cna_easygl_test(cna_test_easygl_texture_address_mode_clamp_effect
                        examples/easygl_texture_address_mode_clamp_effect_test.cpp)
        add_test(NAME EasyGL_TextureAddressMode_Clamp_DualTextureEffect
                 COMMAND cna_test_easygl_texture_address_mode_clamp_effect)
        set_tests_properties(EasyGL_TextureAddressMode_Clamp_DualTextureEffect PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 296: TextureAddressMode::Mirror on a 3D stock effect (DualTextureEffect)
        cna_easygl_test(cna_test_easygl_texture_address_mode_mirror_effect
                        examples/easygl_texture_address_mode_mirror_effect_test.cpp)
        add_test(NAME EasyGL_TextureAddressMode_Mirror_DualTextureEffect
                 COMMAND cna_test_easygl_texture_address_mode_mirror_effect)
        set_tests_properties(EasyGL_TextureAddressMode_Mirror_DualTextureEffect PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 297: TextureFilter::Point vs Linear, magnification and minification
        cna_easygl_test(cna_test_easygl_texture_filter_point_vs_linear
                        examples/easygl_texture_filter_point_vs_linear_test.cpp)
        add_test(NAME EasyGL_TextureFilter_PointVsLinear
                 COMMAND cna_test_easygl_texture_filter_point_vs_linear)
        set_tests_properties(EasyGL_TextureFilter_PointVsLinear PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 298: mipmap filter behavior (LinearMipPoint selects the correct mip; Point does not)
        cna_easygl_test(cna_test_easygl_texture_mip_filter_effect
                        examples/easygl_texture_mip_filter_effect_test.cpp)
        add_test(NAME EasyGL_TextureMipFilter_DualTextureEffect
                 COMMAND cna_test_easygl_texture_mip_filter_effect)
        set_tests_properties(EasyGL_TextureMipFilter_DualTextureEffect PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 299: anisotropic filtering caps and fallback (extreme MaxAnisotropy must not crash)
        cna_easygl_test(cna_test_easygl_texture_anisotropic_effect
                        examples/easygl_texture_anisotropic_effect_test.cpp)
        add_test(NAME EasyGL_TextureAnisotropic_DualTextureEffect
                 COMMAND cna_test_easygl_texture_anisotropic_effect)
        set_tests_properties(EasyGL_TextureAnisotropic_DualTextureEffect PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 303: BlendState::Opaque discards destination + source alpha entirely
        cna_easygl_test(cna_test_easygl_blendstate_opaque
                        examples/easygl_blendstate_opaque_test.cpp)
        add_test(NAME EasyGL_BlendState_Opaque
                 COMMAND cna_test_easygl_blendstate_opaque)
        set_tests_properties(EasyGL_BlendState_Opaque PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 304: BlendState::AlphaBlend expects premultiplied source colour
        cna_easygl_test(cna_test_easygl_blendstate_alphablend
                        examples/easygl_blendstate_alphablend_test.cpp)
        add_test(NAME EasyGL_BlendState_AlphaBlend
                 COMMAND cna_test_easygl_blendstate_alphablend)
        set_tests_properties(EasyGL_BlendState_AlphaBlend PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 305: BlendState::NonPremultiplied expects a raw (non-premultiplied) source colour
        cna_easygl_test(cna_test_easygl_blendstate_nonpremultiplied
                        examples/easygl_blendstate_nonpremultiplied_test.cpp)
        add_test(NAME EasyGL_BlendState_NonPremultiplied
                 COMMAND cna_test_easygl_blendstate_nonpremultiplied)
        set_tests_properties(EasyGL_BlendState_NonPremultiplied PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 306: BlendState::Additive saturation + full-destination-add behaviour
        cna_easygl_test(cna_test_easygl_blendstate_additive
                        examples/easygl_blendstate_additive_test.cpp)
        add_test(NAME EasyGL_BlendState_Additive
                 COMMAND cna_test_easygl_blendstate_additive)
        set_tests_properties(EasyGL_BlendState_Additive PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 307: ColorBlendFunction/AlphaBlendFunction operate independently
        cna_easygl_test(cna_test_easygl_blendstate_separate_functions
                        examples/easygl_blendstate_separate_functions_test.cpp)
        add_test(NAME EasyGL_BlendState_SeparateFunctions
                 COMMAND cna_test_easygl_blendstate_separate_functions)
        set_tests_properties(EasyGL_BlendState_SeparateFunctions PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 308: ColorSourceBlend/ColorDestinationBlend operate independently from Alpha*Blend
        cna_easygl_test(cna_test_easygl_blendstate_separate_factors
                        examples/easygl_blendstate_separate_factors_test.cpp)
        add_test(NAME EasyGL_BlendState_SeparateFactors
                 COMMAND cna_test_easygl_blendstate_separate_factors)
        set_tests_properties(EasyGL_BlendState_SeparateFactors PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 309: BlendState.BlendFactor propagates via GraphicsDevice.setBlendStateProperty alone
        cna_easygl_test(cna_test_easygl_blendstate_blendfactor
                        examples/easygl_blendstate_blendfactor_test.cpp)
        add_test(NAME EasyGL_BlendState_BlendFactor
                 COMMAND cna_test_easygl_blendstate_blendfactor)
        set_tests_properties(EasyGL_BlendState_BlendFactor PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 313: DepthStencilState.DepthBufferWriteEnable actually gates depth-buffer writes
        cna_easygl_test(cna_test_easygl_depthstencilstate_write_enable
                        examples/easygl_depthstencilstate_write_enable_test.cpp)
        add_test(NAME EasyGL_DepthStencilState_WriteEnable
                 COMMAND cna_test_easygl_depthstencilstate_write_enable)
        set_tests_properties(EasyGL_DepthStencilState_WriteEnable PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 314: DepthStencilState.DepthBufferFunction (Always/Never/Less/LessEqual/Greater)
        cna_easygl_test(cna_test_easygl_depthstencilstate_compare_function
                        examples/easygl_depthstencilstate_compare_function_test.cpp)
        add_test(NAME EasyGL_DepthStencilState_CompareFunction
                 COMMAND cna_test_easygl_depthstencilstate_compare_function)
        set_tests_properties(EasyGL_DepthStencilState_CompareFunction PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 315: DepthStencilState.StencilEnable actually gates the stencil test
        cna_easygl_test(cna_test_easygl_depthstencilstate_stencil_enable
                        examples/easygl_depthstencilstate_stencil_enable_test.cpp)
        add_test(NAME EasyGL_DepthStencilState_StencilEnable
                 COMMAND cna_test_easygl_depthstencilstate_stencil_enable)
        set_tests_properties(EasyGL_DepthStencilState_StencilEnable PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 316: DepthStencilState.StencilMask / StencilWriteMask
        cna_easygl_test(cna_test_easygl_depthstencilstate_stencil_mask
                        examples/easygl_depthstencilstate_stencil_mask_test.cpp)
        add_test(NAME EasyGL_DepthStencilState_StencilMask
                 COMMAND cna_test_easygl_depthstencilstate_stencil_mask)
        set_tests_properties(EasyGL_DepthStencilState_StencilMask PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 317: DepthStencilState front-face stencil operations (StencilFail/
        # StencilDepthBufferFail/StencilPass)
        cna_easygl_test(cna_test_easygl_depthstencilstate_stencil_ops
                        examples/easygl_depthstencilstate_stencil_ops_test.cpp)
        add_test(NAME EasyGL_DepthStencilState_StencilOps
                 COMMAND cna_test_easygl_depthstencilstate_stencil_ops)
        set_tests_properties(EasyGL_DepthStencilState_StencilOps PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 318: DepthStencilState.TwoSidedStencilMode / back-face (CCW) stencil ops
        cna_easygl_test(cna_test_easygl_depthstencilstate_stencil_twosided
                        examples/easygl_depthstencilstate_stencil_twosided_test.cpp)
        add_test(NAME EasyGL_DepthStencilState_StencilTwoSided
                 COMMAND cna_test_easygl_depthstencilstate_stencil_twosided)
        set_tests_properties(EasyGL_DepthStencilState_StencilTwoSided PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 319: GraphicsDevice.ReferenceStencil independent-override behavior (Task 872 -
        # confirmed a universal, not-Vulkan-specific gap; registered as a documented known failure)
        cna_easygl_test(cna_test_easygl_graphicsdevice_reference_stencil
                        examples/easygl_graphicsdevice_reference_stencil_test.cpp)
        add_test(NAME EasyGL_GraphicsDevice_ReferenceStencil
                 COMMAND cna_test_easygl_graphicsdevice_reference_stencil)
        set_tests_properties(EasyGL_GraphicsDevice_ReferenceStencil PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 323 (+ 324/325 coverage): RasterizerState.CullMode — None/CullClockwiseFace/
        # CullCounterClockwiseFace, contrast-checked with front- and back-winding quads
        cna_easygl_test(cna_test_easygl_rasterizerstate_cullmode
                        examples/easygl_rasterizerstate_cullmode_test.cpp)
        add_test(NAME EasyGL_RasterizerState_CullMode
                 COMMAND cna_test_easygl_rasterizerstate_cullmode)
        set_tests_properties(EasyGL_RasterizerState_CullMode PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 326: FillMode::Solid baseline on EasyGL (reuses Task 327's backend-agnostic Vulkan
        # source — its Solid sub-tests 1/3 previously only ran on Vulkan).
        cna_easygl_test(cna_test_easygl_fill_mode
                        examples/vulkan_fill_mode_test.cpp)
        add_test(NAME EasyGL_FillMode_Solid
                 COMMAND cna_test_easygl_fill_mode)
        set_tests_properties(EasyGL_FillMode_Solid PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 331: RenderTarget2D constructor/property audit against FNA.
        cna_easygl_test(cna_test_easygl_rendertarget2d_properties
                        examples/easygl_rendertarget2d_properties_test.cpp)
        add_test(NAME EasyGL_RenderTarget2D_Properties
                 COMMAND cna_test_easygl_rendertarget2d_properties)
        set_tests_properties(EasyGL_RenderTarget2D_Properties PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 332: RenderTargetCube constructor/property audit against FNA.
        cna_easygl_test(cna_test_easygl_rendertargetcube_properties
                        examples/easygl_rendertargetcube_properties_test.cpp)
        add_test(NAME EasyGL_RenderTargetCube_Properties
                 COMMAND cna_test_easygl_rendertargetcube_properties)
        set_tests_properties(EasyGL_RenderTargetCube_Properties PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

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
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 124: DrawInstancedPrimitives — 3 instances at different X positions
        cna_vulkan_test(cna_test_vulkan_instanced
                        examples/vulkan_instanced_test.cpp)
        add_test(NAME Vulkan_DrawInstanced_3Instances
                 COMMAND cna_test_vulkan_instanced)
        set_tests_properties(Vulkan_DrawInstanced_3Instances PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 119: ShaderEffect with SPIR-V — SpriteBatch tint via custom push constants
        cna_vulkan_test(cna_test_vulkan_shader_effect
                        examples/vulkan_shader_effect_test.cpp)
        add_test(NAME Vulkan_ShaderEffect_SpirV
                 COMMAND cna_test_vulkan_shader_effect)
        set_tests_properties(Vulkan_ShaderEffect_SpirV PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 284: Texture2D Color format mapping — linear vs. sRGB decode on Vulkan
        cna_vulkan_test(cna_test_vulkan_texture_srgb
                        examples/vulkan_texture_srgb_test.cpp)
        add_test(NAME Vulkan_Texture2D_ColorFormat_Linear
                 COMMAND cna_test_vulkan_texture_srgb)
        set_tests_properties(Vulkan_Texture2D_ColorFormat_Linear PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 135: DualTextureEffect — magenta×yellow→red pixel readback on Vulkan
        cna_vulkan_test(cna_test_vulkan_dual_texture
                        examples/vulkan_dual_texture_test.cpp)
        add_test(NAME Vulkan_DualTextureEffect_Blend
                 COMMAND cna_test_vulkan_dual_texture)
        set_tests_properties(Vulkan_DualTextureEffect_Blend PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 293: DualTextureEffect (3D stock effect) honors GraphicsDevice.SamplerStates[0] on
        # Vulkan too — reuses the backend-agnostic EasyGL test source (public XNA API only).
        cna_vulkan_test(cna_test_vulkan_sampler_state_effect
                        examples/easygl_sampler_state_effect_test.cpp)
        add_test(NAME Vulkan_SamplerState_DualTextureEffect
                 COMMAND cna_test_vulkan_sampler_state_effect)
        set_tests_properties(Vulkan_SamplerState_DualTextureEffect PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 294: TextureAddressMode::Clamp on a 3D stock effect on Vulkan too (reuses the
        # backend-agnostic EasyGL test source).
        cna_vulkan_test(cna_test_vulkan_texture_address_mode_clamp_effect
                        examples/easygl_texture_address_mode_clamp_effect_test.cpp)
        add_test(NAME Vulkan_TextureAddressMode_Clamp_DualTextureEffect
                 COMMAND cna_test_vulkan_texture_address_mode_clamp_effect)
        set_tests_properties(Vulkan_TextureAddressMode_Clamp_DualTextureEffect PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 296: TextureAddressMode::Mirror on a 3D stock effect on Vulkan too (reuses the
        # backend-agnostic EasyGL test source).
        cna_vulkan_test(cna_test_vulkan_texture_address_mode_mirror_effect
                        examples/easygl_texture_address_mode_mirror_effect_test.cpp)
        add_test(NAME Vulkan_TextureAddressMode_Mirror_DualTextureEffect
                 COMMAND cna_test_vulkan_texture_address_mode_mirror_effect)
        set_tests_properties(Vulkan_TextureAddressMode_Mirror_DualTextureEffect PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 297: TextureFilter::Point vs Linear on Vulkan too (reuses the backend-agnostic
        # EasyGL test source).
        cna_vulkan_test(cna_test_vulkan_texture_filter_point_vs_linear
                        examples/easygl_texture_filter_point_vs_linear_test.cpp)
        add_test(NAME Vulkan_TextureFilter_PointVsLinear
                 COMMAND cna_test_vulkan_texture_filter_point_vs_linear)
        set_tests_properties(Vulkan_TextureFilter_PointVsLinear PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 298: NOT registered on Vulkan. Texture2D::SetData(level>0) is a silent no-op on
        # Vulkan (UpdatePixelsLevel never overridden — Task 867 finding), which confounds this
        # test's mip-filter-selection intent (it would fail for the wrong reason). See Task 867.

        # Task 299: anisotropic filtering caps and fallback on Vulkan too (reuses the
        # backend-agnostic EasyGL test source). This is the one backend where MaxAnisotropy is
        # actually respected (queried/clamped to the real device cap).
        cna_vulkan_test(cna_test_vulkan_texture_anisotropic_effect
                        examples/easygl_texture_anisotropic_effect_test.cpp)
        add_test(NAME Vulkan_TextureAnisotropic_DualTextureEffect
                 COMMAND cna_test_vulkan_texture_anisotropic_effect)
        set_tests_properties(Vulkan_TextureAnisotropic_DualTextureEffect PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 303: BlendState::Opaque on Vulkan too (reuses the backend-agnostic EasyGL source)
        cna_vulkan_test(cna_test_vulkan_blendstate_opaque
                        examples/easygl_blendstate_opaque_test.cpp)
        add_test(NAME Vulkan_BlendState_Opaque
                 COMMAND cna_test_vulkan_blendstate_opaque)
        set_tests_properties(Vulkan_BlendState_Opaque PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 304: BlendState::AlphaBlend on Vulkan too (reuses the backend-agnostic EasyGL source)
        cna_vulkan_test(cna_test_vulkan_blendstate_alphablend
                        examples/easygl_blendstate_alphablend_test.cpp)
        add_test(NAME Vulkan_BlendState_AlphaBlend
                 COMMAND cna_test_vulkan_blendstate_alphablend)
        set_tests_properties(Vulkan_BlendState_AlphaBlend PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 305: BlendState::NonPremultiplied on Vulkan too (reuses the backend-agnostic
        # EasyGL source). NOTE: a passing result here does NOT mean Task 868 is fixed — see the
        # test source's file-header comment for why.
        cna_vulkan_test(cna_test_vulkan_blendstate_nonpremultiplied
                        examples/easygl_blendstate_nonpremultiplied_test.cpp)
        add_test(NAME Vulkan_BlendState_NonPremultiplied
                 COMMAND cna_test_vulkan_blendstate_nonpremultiplied)
        set_tests_properties(Vulkan_BlendState_NonPremultiplied PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 306: BlendState::Additive on Vulkan too (reuses the backend-agnostic EasyGL source).
        # NOTE: expected to genuinely re-expose Task 868 here, unlike Task 305's coincidental pass.
        cna_vulkan_test(cna_test_vulkan_blendstate_additive
                        examples/easygl_blendstate_additive_test.cpp)
        add_test(NAME Vulkan_BlendState_Additive
                 COMMAND cna_test_vulkan_blendstate_additive)
        set_tests_properties(Vulkan_BlendState_Additive PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 307: ColorBlendFunction/AlphaBlendFunction independence on Vulkan too (reuses the
        # backend-agnostic EasyGL source). NOTE: expect Check A (Subtract) to fail per Task 868 -
        # Vulkan always hardcodes VK_BLEND_OP_ADD.
        cna_vulkan_test(cna_test_vulkan_blendstate_separate_functions
                        examples/easygl_blendstate_separate_functions_test.cpp)
        add_test(NAME Vulkan_BlendState_SeparateFunctions
                 COMMAND cna_test_vulkan_blendstate_separate_functions)
        set_tests_properties(Vulkan_BlendState_SeparateFunctions PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 308: ColorSourceBlend/ColorDestinationBlend independence on Vulkan too (reuses the
        # backend-agnostic EasyGL source). NOTE: expect Check A to coincidentally pass and Check B
        # to fail per Task 868 - Vulkan always hardcodes SourceAlpha/InverseSourceAlpha for colour.
        cna_vulkan_test(cna_test_vulkan_blendstate_separate_factors
                        examples/easygl_blendstate_separate_factors_test.cpp)
        add_test(NAME Vulkan_BlendState_SeparateFactors
                 COMMAND cna_test_vulkan_blendstate_separate_factors)
        set_tests_properties(Vulkan_BlendState_SeparateFactors PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

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
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 313: DepthStencilState.DepthBufferWriteEnable on Vulkan (reuses the backend-agnostic
        # EasyGL source).
        cna_vulkan_test(cna_test_vulkan_depthstencilstate_write_enable
                        examples/easygl_depthstencilstate_write_enable_test.cpp)
        add_test(NAME Vulkan_DepthStencilState_WriteEnable
                 COMMAND cna_test_vulkan_depthstencilstate_write_enable)
        set_tests_properties(Vulkan_DepthStencilState_WriteEnable PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 314: DepthStencilState.DepthBufferFunction on Vulkan (reuses the backend-agnostic
        # EasyGL source). NOTE: expected to FAIL some/all checks per Task 870 - Vulkan hardcodes
        # depthCompareOp per pipeline, ignoring DepthBufferFunction entirely.
        cna_vulkan_test(cna_test_vulkan_depthstencilstate_compare_function
                        examples/easygl_depthstencilstate_compare_function_test.cpp)
        add_test(NAME Vulkan_DepthStencilState_CompareFunction
                 COMMAND cna_test_vulkan_depthstencilstate_compare_function)
        set_tests_properties(Vulkan_DepthStencilState_CompareFunction PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 315: DepthStencilState.StencilEnable on Vulkan (reuses the backend-agnostic EasyGL
        # source). NOTE: expected to FAIL Check A per Task 870 - stencil testing is completely
        # non-functional on Vulkan (stencilTestEnable never set), so no gating ever happens.
        cna_vulkan_test(cna_test_vulkan_depthstencilstate_stencil_enable
                        examples/easygl_depthstencilstate_stencil_enable_test.cpp)
        add_test(NAME Vulkan_DepthStencilState_StencilEnable
                 COMMAND cna_test_vulkan_depthstencilstate_stencil_enable)
        set_tests_properties(Vulkan_DepthStencilState_StencilEnable PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 316: DepthStencilState.StencilMask/StencilWriteMask on Vulkan (reuses the
        # backend-agnostic EasyGL source). NOTE: expected to partially fail per Task 870 - stencil
        # testing never gates on Vulkan, so 2 of 4 checks pass only by coincidence.
        cna_vulkan_test(cna_test_vulkan_depthstencilstate_stencil_mask
                        examples/easygl_depthstencilstate_stencil_mask_test.cpp)
        add_test(NAME Vulkan_DepthStencilState_StencilMask
                 COMMAND cna_test_vulkan_depthstencilstate_stencil_mask)
        set_tests_properties(Vulkan_DepthStencilState_StencilMask PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 317: DepthStencilState front-face stencil operations on Vulkan (reuses the
        # backend-agnostic EasyGL source). NOTE: expected to FAIL all 3 checks per Task 870 -
        # stencil testing never gates on Vulkan, so no operation ever fires.
        cna_vulkan_test(cna_test_vulkan_depthstencilstate_stencil_ops
                        examples/easygl_depthstencilstate_stencil_ops_test.cpp)
        add_test(NAME Vulkan_DepthStencilState_StencilOps
                 COMMAND cna_test_vulkan_depthstencilstate_stencil_ops)
        set_tests_properties(Vulkan_DepthStencilState_StencilOps PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 318: DepthStencilState.TwoSidedStencilMode on Vulkan (reuses the backend-agnostic
        # EasyGL source). NOTE: expected to FAIL the contrast check per Task 870 - stencil testing
        # never gates on Vulkan.
        cna_vulkan_test(cna_test_vulkan_depthstencilstate_stencil_twosided
                        examples/easygl_depthstencilstate_stencil_twosided_test.cpp)
        add_test(NAME Vulkan_DepthStencilState_StencilTwoSided
                 COMMAND cna_test_vulkan_depthstencilstate_stencil_twosided)
        set_tests_properties(Vulkan_DepthStencilState_StencilTwoSided PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 319: GraphicsDevice.ReferenceStencil independent-override behavior on Vulkan (Task
        # 872, universal gap, reuses the backend-agnostic EasyGL source).
        cna_vulkan_test(cna_test_vulkan_graphicsdevice_reference_stencil
                        examples/easygl_graphicsdevice_reference_stencil_test.cpp)
        add_test(NAME Vulkan_GraphicsDevice_ReferenceStencil
                 COMMAND cna_test_vulkan_graphicsdevice_reference_stencil)
        set_tests_properties(Vulkan_GraphicsDevice_ReferenceStencil PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 323 (+ 324/325 coverage): RasterizerState.CullMode on Vulkan (reuses the
        # backend-agnostic EasyGL source).
        cna_vulkan_test(cna_test_vulkan_rasterizerstate_cullmode
                        examples/easygl_rasterizerstate_cullmode_test.cpp)
        add_test(NAME Vulkan_RasterizerState_CullMode
                 COMMAND cna_test_vulkan_rasterizerstate_cullmode)
        set_tests_properties(Vulkan_RasterizerState_CullMode PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 136: EnvironmentMapEffect — emissive=(1,0,0), envAmount=0 → red pixel on Vulkan
        cna_vulkan_test(cna_test_vulkan_env_map
                        examples/vulkan_env_map_test.cpp)
        add_test(NAME Vulkan_EnvironmentMapEffect_Readback
                 COMMAND cna_test_vulkan_env_map)
        set_tests_properties(Vulkan_EnvironmentMapEffect_Readback PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 393: EnvironmentMapEffect EnvironmentMapAmount=0 should ignore the cube map
        cna_vulkan_test(cna_test_vulkan_environmentmapeffect_amount_zero
                        examples/vulkan_environmentmapeffect_amount_zero_test.cpp)
        add_test(NAME Vulkan_EnvironmentMapEffect_AmountZero
                 COMMAND cna_test_vulkan_environmentmapeffect_amount_zero)
        set_tests_properties(Vulkan_EnvironmentMapEffect_AmountZero PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 394: EnvironmentMapEffect EnvironmentMapAmount=1 should fully replace lit color
        cna_vulkan_test(cna_test_vulkan_environmentmapeffect_amount_one
                        examples/vulkan_environmentmapeffect_amount_one_test.cpp)
        add_test(NAME Vulkan_EnvironmentMapEffect_AmountOne
                 COMMAND cna_test_vulkan_environmentmapeffect_amount_one)
        set_tests_properties(Vulkan_EnvironmentMapEffect_AmountOne PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 395: EnvironmentMapEffect EnvironmentMapSpecular should scale by cubemap alpha
        cna_vulkan_test(cna_test_vulkan_environmentmapeffect_specular
                        examples/vulkan_environmentmapeffect_specular_test.cpp)
        add_test(NAME Vulkan_EnvironmentMapEffect_Specular
                 COMMAND cna_test_vulkan_environmentmapeffect_specular)
        set_tests_properties(Vulkan_EnvironmentMapEffect_Specular PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 396: EnvironmentMapEffect Fresnel edge-weighting should suppress head-on reflections
        cna_vulkan_test(cna_test_vulkan_environmentmapeffect_fresnel
                        examples/vulkan_environmentmapeffect_fresnel_test.cpp)
        add_test(NAME Vulkan_EnvironmentMapEffect_Fresnel
                 COMMAND cna_test_vulkan_environmentmapeffect_fresnel)
        set_tests_properties(Vulkan_EnvironmentMapEffect_Fresnel PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 397: EnvironmentMapEffect reflection vector should respond to EyePosition
        cna_vulkan_test(cna_test_vulkan_environmentmapeffect_eyeposition
                        examples/vulkan_environmentmapeffect_eyeposition_test.cpp)
        add_test(NAME Vulkan_EnvironmentMapEffect_EyePosition
                 COMMAND cna_test_vulkan_environmentmapeffect_eyeposition)
        set_tests_properties(Vulkan_EnvironmentMapEffect_EyePosition PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 398: EnvironmentMapEffect normal transform should use inverse-transpose of World
        cna_vulkan_test(cna_test_vulkan_environmentmapeffect_worldtransform
                        examples/vulkan_environmentmapeffect_worldtransform_test.cpp)
        add_test(NAME Vulkan_EnvironmentMapEffect_WorldTransform
                 COMMAND cna_test_vulkan_environmentmapeffect_worldtransform)
        set_tests_properties(Vulkan_EnvironmentMapEffect_WorldTransform PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 399: cross-backend EnvironmentMapEffect capstone (Tasks 393-398 combined)
        cna_vulkan_test(cna_test_vulkan_environmentmapeffect_combined
                        examples/vulkan_environmentmapeffect_combined_test.cpp)
        add_test(NAME Vulkan_EnvironmentMapEffect_Combined
                 COMMAND cna_test_vulkan_environmentmapeffect_combined)
        set_tests_properties(Vulkan_EnvironmentMapEffect_Combined PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 406: SkinnedEffect identity bone palette should produce zero deformation
        cna_vulkan_test(cna_test_vulkan_skinnedeffect_identity_bones
                        examples/vulkan_skinnedeffect_identity_bones_test.cpp)
        add_test(NAME Vulkan_SkinnedEffect_IdentityBones
                 COMMAND cna_test_vulkan_skinnedeffect_identity_bones)
        set_tests_properties(Vulkan_SkinnedEffect_IdentityBones PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 407: SkinnedEffect single translation bone should shift the mesh
        cna_vulkan_test(cna_test_vulkan_skinnedeffect_translation_bone
                        examples/vulkan_skinnedeffect_translation_bone_test.cpp)
        add_test(NAME Vulkan_SkinnedEffect_TranslationBone
                 COMMAND cna_test_vulkan_skinnedeffect_translation_bone)
        set_tests_properties(Vulkan_SkinnedEffect_TranslationBone PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 408: SkinnedEffect two-bone weighted blend should produce midpoint deformation
        cna_vulkan_test(cna_test_vulkan_skinnedeffect_twobone_blend
                        examples/vulkan_skinnedeffect_twobone_blend_test.cpp)
        add_test(NAME Vulkan_SkinnedEffect_TwoBoneBlend
                 COMMAND cna_test_vulkan_skinnedeffect_twobone_blend)
        set_tests_properties(Vulkan_SkinnedEffect_TwoBoneBlend PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 409: cross-backend SkinnedEffect capstone (identity + single-bone + two-bone blend)
        cna_vulkan_test(cna_test_vulkan_skinnedeffect_combined
                        examples/vulkan_skinnedeffect_combined_test.cpp)
        add_test(NAME Vulkan_SkinnedEffect_Combined
                 COMMAND cna_test_vulkan_skinnedeffect_combined)
        set_tests_properties(Vulkan_SkinnedEffect_Combined PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 334: RenderTargetCube sampled as TextureCube via EnvironmentMapEffect after unbinding
        cna_vulkan_test(cna_test_vulkan_rendertargetcube_sample
                        examples/vulkan_rendertargetcube_sample_test.cpp)
        add_test(NAME Vulkan_RenderTargetCube_SampleAfterUnbind
                 COMMAND cna_test_vulkan_rendertargetcube_sample)
        set_tests_properties(Vulkan_RenderTargetCube_SampleAfterUnbind PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 148: RenderTarget2D full cycle — red quad into RT, blit to backbuffer
        cna_vulkan_test(cna_test_vulkan_rt2d
                        examples/vulkan_rt2d_test.cpp)
        add_test(NAME Vulkan_RenderTarget2D_FullCycle
                 COMMAND cna_test_vulkan_rt2d)
        set_tests_properties(Vulkan_RenderTarget2D_FullCycle PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 335: RenderTarget2D depth buffer is real and functional (not just a stored property)
        cna_vulkan_test(cna_test_vulkan_rendertarget2d_depth
                        examples/rendertarget2d_depth_test.cpp)
        add_test(NAME Vulkan_RenderTarget2D_DepthBuffer
                 COMMAND cna_test_vulkan_rendertarget2d_depth)
        set_tests_properties(Vulkan_RenderTarget2D_DepthBuffer PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 178: RenderTargetUsage DiscardContents vs PreserveContents
        cna_vulkan_test(cna_test_vulkan_render_target_usage
                        examples/vulkan_render_target_usage_test.cpp)
        add_test(NAME Vulkan_RenderTargetUsage
                 COMMAND cna_test_vulkan_render_target_usage)
        set_tests_properties(Vulkan_RenderTargetUsage PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 142: RenderTargetCube — 6-face per-face render passes
        cna_vulkan_test(cna_test_vulkan_rtcube
                        examples/vulkan_rtcube_test.cpp)
        add_test(NAME Vulkan_RenderTargetCube_PerFace
                 COMMAND cna_test_vulkan_rtcube)
        set_tests_properties(Vulkan_RenderTargetCube_PerFace PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 147: MSAA 4× — multisampled swapchain + subpass resolve
        cna_vulkan_test(cna_test_vulkan_msaa
                        examples/vulkan_msaa_test.cpp)
        add_test(NAME Vulkan_MSAA_4x_Readback
                 COMMAND cna_test_vulkan_msaa)
        set_tests_properties(Vulkan_MSAA_4x_Readback PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        cna_vulkan_test(cna_test_vulkan_vertex_format
                        examples/vulkan_vertex_format_test.cpp)
        add_test(NAME Vulkan_VertexFormat_AllStrides
                 COMMAND cna_test_vulkan_vertex_format)
        set_tests_properties(Vulkan_VertexFormat_AllStrides PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        cna_vulkan_test(cna_test_vulkan_fill_mode
                        examples/vulkan_fill_mode_test.cpp)
        add_test(NAME Vulkan_FillMode_WireFrame
                 COMMAND cna_test_vulkan_fill_mode)
        set_tests_properties(Vulkan_FillMode_WireFrame PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        cna_vulkan_test(cna_test_vulkan_depth_bias
                        examples/vulkan_depth_bias_test.cpp)
        add_test(NAME Vulkan_DepthBias
                 COMMAND cna_test_vulkan_depth_bias)
        set_tests_properties(Vulkan_DepthBias PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 329: ScissorTestEnable + ScissorRectangle pixel-readback test
        cna_vulkan_test(cna_test_vulkan_scissor
                        examples/vulkan_scissor_test.cpp)
        add_test(NAME Vulkan_ScissorTest
                 COMMAND cna_test_vulkan_scissor)
        set_tests_properties(Vulkan_ScissorTest PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 664: SpriteBatch multiple Begin()/End() cycles per frame must all render
        cna_vulkan_test(cna_test_vulkan_spritebatch_multi_begin_end
                        examples/vulkan_spritebatch_multi_begin_end_test.cpp)
        add_test(NAME Vulkan_SpriteBatch_MultiBeginEnd
                 COMMAND cna_test_vulkan_spritebatch_multi_begin_end)
        set_tests_properties(Vulkan_SpriteBatch_MultiBeginEnd PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 665: SpriteBatch Begin()'s SamplerState (TextureAddressMode) must take effect
        cna_vulkan_test(cna_test_vulkan_texture_address_mode
                        examples/vulkan_texture_address_mode_test.cpp)
        add_test(NAME Vulkan_TextureAddressMode
                 COMMAND cna_test_vulkan_texture_address_mode)
        set_tests_properties(Vulkan_TextureAddressMode PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 338: SetRenderTarget(nullptr) resets Viewport/ScissorRectangle to the backbuffer
        cna_vulkan_test(cna_test_vulkan_rendertarget_viewport_scissor_reset
                        examples/rendertarget_viewport_scissor_reset_test.cpp)
        add_test(NAME Vulkan_RenderTarget_ViewportScissorReset
                 COMMAND cna_test_vulkan_rendertarget_viewport_scissor_reset)
        set_tests_properties(Vulkan_RenderTarget_ViewportScissorReset PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 349: custom Viewport survives Present() with no resize, resets after a real one.
        cna_vulkan_test(cna_test_vulkan_viewport_reset_after_resize
                        examples/viewport_reset_after_resize_test.cpp)
        add_test(NAME Vulkan_ViewportResetAfterResize
                 COMMAND cna_test_vulkan_viewport_reset_after_resize)
        set_tests_properties(Vulkan_ViewportResetAfterResize PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 331: RenderTarget2D constructor/property audit against FNA.
        cna_vulkan_test(cna_test_vulkan_rendertarget2d_properties
                        examples/easygl_rendertarget2d_properties_test.cpp)
        add_test(NAME Vulkan_RenderTarget2D_Properties
                 COMMAND cna_test_vulkan_rendertarget2d_properties)
        set_tests_properties(Vulkan_RenderTarget2D_Properties PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 332: RenderTargetCube constructor/property audit against FNA.
        cna_vulkan_test(cna_test_vulkan_rendertargetcube_properties
                        examples/easygl_rendertargetcube_properties_test.cpp)
        add_test(NAME Vulkan_RenderTargetCube_Properties
                 COMMAND cna_test_vulkan_rendertargetcube_properties)
        set_tests_properties(Vulkan_RenderTargetCube_Properties PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 364: BasicEffect pixel test — VertexColorEnabled=false, no texture, diffuse color only
        cna_vulkan_test(cna_test_vulkan_basiceffect_vertexcolor_disabled
                        examples/vulkan_basiceffect_vertexcolor_disabled_test.cpp)
        add_test(NAME Vulkan_BasicEffect_VertexColorDisabled
                 COMMAND cna_test_vulkan_basiceffect_vertexcolor_disabled)
        set_tests_properties(Vulkan_BasicEffect_VertexColorDisabled PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 365: BasicEffect pixel test — VertexColorEnabled=true, no texture, vertex color multiplication
        cna_vulkan_test(cna_test_vulkan_basiceffect_vertexcolor_enabled
                        examples/vulkan_basiceffect_vertexcolor_enabled_test.cpp)
        add_test(NAME Vulkan_BasicEffect_VertexColorEnabled
                 COMMAND cna_test_vulkan_basiceffect_vertexcolor_enabled)
        set_tests_properties(Vulkan_BasicEffect_VertexColorEnabled PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 366: BasicEffect pixel test — TextureEnabled=true, no vertex color
        cna_vulkan_test(cna_test_vulkan_basiceffect_texture_enabled
                        examples/vulkan_basiceffect_texture_enabled_test.cpp)
        add_test(NAME Vulkan_BasicEffect_TextureEnabled
                 COMMAND cna_test_vulkan_basiceffect_texture_enabled)
        set_tests_properties(Vulkan_BasicEffect_TextureEnabled PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 367: BasicEffect pixel test — TextureEnabled=true AND VertexColorEnabled=true
        cna_vulkan_test(cna_test_vulkan_basiceffect_texture_vertexcolor_enabled
                        examples/vulkan_basiceffect_texture_vertexcolor_enabled_test.cpp)
        add_test(NAME Vulkan_BasicEffect_TextureVertexColorEnabled
                 COMMAND cna_test_vulkan_basiceffect_texture_vertexcolor_enabled)
        set_tests_properties(Vulkan_BasicEffect_TextureVertexColorEnabled PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 368: BasicEffect pixel test — LightingEnabled=true, one DirectionalLight
        cna_vulkan_test(cna_test_vulkan_basiceffect_one_light
                        examples/vulkan_basiceffect_one_light_test.cpp)
        add_test(NAME Vulkan_BasicEffect_OneLight
                 COMMAND cna_test_vulkan_basiceffect_one_light)
        set_tests_properties(Vulkan_BasicEffect_OneLight PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 897: BasicEffect pixel test — DirectionalLight1/2 + EmissiveColor on the lit path
        cna_vulkan_test(cna_test_vulkan_basiceffect_multilight_emissive
                        examples/vulkan_basiceffect_multilight_emissive_test.cpp)
        add_test(NAME Vulkan_BasicEffect_MultiLightEmissive
                 COMMAND cna_test_vulkan_basiceffect_multilight_emissive)
        set_tests_properties(Vulkan_BasicEffect_MultiLightEmissive PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 886: BasicEffect pixel test — real specular highlights
        cna_vulkan_test(cna_test_vulkan_basiceffect_specular
                        examples/vulkan_basiceffect_specular_test.cpp)
        add_test(NAME Vulkan_BasicEffect_Specular
                 COMMAND cna_test_vulkan_basiceffect_specular)
        set_tests_properties(Vulkan_BasicEffect_Specular PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 887: AlphaTestEffect.VertexColorEnabled fix verification
        cna_vulkan_test(cna_test_vulkan_alphatest_vertexcolor
                        examples/vulkan_alphatest_vertexcolor_test.cpp)
        add_test(NAME Vulkan_AlphaTest_VertexColor
                 COMMAND cna_test_vulkan_alphatest_vertexcolor)
        set_tests_properties(Vulkan_AlphaTest_VertexColor PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 888: BasicEffect fog on the lit_textured3d (stride-32) pipeline
        cna_vulkan_test(cna_test_vulkan_basiceffect_fog
                        examples/vulkan_basiceffect_fog_test.cpp)
        add_test(NAME Vulkan_BasicEffect_Fog
                 COMMAND cna_test_vulkan_basiceffect_fog)
        set_tests_properties(Vulkan_BasicEffect_Fog PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 888: AlphaTestEffect fog on the alpha_test3d pipeline
        cna_vulkan_test(cna_test_vulkan_alphatest_fog
                        examples/vulkan_alphatest_fog_test.cpp)
        add_test(NAME Vulkan_AlphaTest_Fog
                 COMMAND cna_test_vulkan_alphatest_fog)
        set_tests_properties(Vulkan_AlphaTest_Fog PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 369: BasicEffect pixel test — DiffuseColor+EmissiveColor, LightingEnabled=false
        cna_vulkan_test(cna_test_vulkan_basiceffect_emissive
                        examples/vulkan_basiceffect_emissive_test.cpp)
        add_test(NAME Vulkan_BasicEffect_Emissive
                 COMMAND cna_test_vulkan_basiceffect_emissive)
        set_tests_properties(Vulkan_BasicEffect_Emissive PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 370: BasicEffect cross-backend image comparison suite — closes Phase 42
        cna_vulkan_test(cna_test_vulkan_basiceffect_combined
                        examples/vulkan_basiceffect_combined_test.cpp)
        add_test(NAME Vulkan_BasicEffect_Combined
                 COMMAND cna_test_vulkan_basiceffect_combined)
        set_tests_properties(Vulkan_BasicEffect_Combined PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 374: AlphaTestEffect all CompareFunction modes — threshold sweep
        cna_vulkan_test(cna_test_vulkan_alphatest_comparefunction_sweep
                        examples/vulkan_alphatest_comparefunction_sweep_test.cpp)
        add_test(NAME Vulkan_AlphaTest_CompareFunctionSweep
                 COMMAND cna_test_vulkan_alphatest_comparefunction_sweep)
        set_tests_properties(Vulkan_AlphaTest_CompareFunctionSweep PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 379: AlphaTestEffect null/no-texture behavior
        cna_vulkan_test(cna_test_vulkan_alphatest_null_texture
                        examples/vulkan_alphatest_null_texture_test.cpp)
        add_test(NAME Vulkan_AlphaTest_NullTexture
                 COMMAND cna_test_vulkan_alphatest_null_texture)
        set_tests_properties(Vulkan_AlphaTest_NullTexture PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 383: DualTextureEffect two-texture blend formula, including FNA's color.rgb*=2
        cna_vulkan_test(cna_test_vulkan_dualtextureeffect_doubling
                        examples/vulkan_dualtextureeffect_doubling_test.cpp)
        add_test(NAME Vulkan_DualTextureEffect_Doubling
                 COMMAND cna_test_vulkan_dualtextureeffect_doubling)
        set_tests_properties(Vulkan_DualTextureEffect_Doubling PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 385: DualTextureEffect Alpha forwarding, alpha-channel-only (see file header for
        # why the full RGB-premultiplication check is deliberately not done on Vulkan)
        cna_vulkan_test(cna_test_vulkan_dualtextureeffect_alpha
                        examples/vulkan_dualtextureeffect_alpha_test.cpp)
        add_test(NAME Vulkan_DualTextureEffect_Alpha
                 COMMAND cna_test_vulkan_dualtextureeffect_alpha)
        set_tests_properties(Vulkan_DualTextureEffect_Alpha PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 386: DualTextureEffect first texture (Texture, slot 0) null behavior
        cna_vulkan_test(cna_test_vulkan_dualtextureeffect_null_texture0
                        examples/vulkan_dualtextureeffect_null_texture0_test.cpp)
        add_test(NAME Vulkan_DualTextureEffect_NullTexture0
                 COMMAND cna_test_vulkan_dualtextureeffect_null_texture0)
        set_tests_properties(Vulkan_DualTextureEffect_NullTexture0 PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 387: DualTextureEffect second texture (Texture2, slot 1) null behavior
        cna_vulkan_test(cna_test_vulkan_dualtextureeffect_null_texture2
                        examples/vulkan_dualtextureeffect_null_texture2_test.cpp)
        add_test(NAME Vulkan_DualTextureEffect_NullTexture2
                 COMMAND cna_test_vulkan_dualtextureeffect_null_texture2)
        set_tests_properties(Vulkan_DualTextureEffect_NullTexture2 PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 389: DualTextureEffect cross-backend image comparison suite (closes Phase 44 pixel work)
        cna_vulkan_test(cna_test_vulkan_dualtextureeffect_combined
                        examples/vulkan_dualtextureeffect_combined_test.cpp)
        add_test(NAME Vulkan_DualTextureEffect_Combined
                 COMMAND cna_test_vulkan_dualtextureeffect_combined)
        set_tests_properties(Vulkan_DualTextureEffect_Combined PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

        # Task 880: GraphicsDevice.Viewport GPU wiring — sub-region viewport pixel-readback test
        cna_vulkan_test(cna_test_vulkan_viewport_subregion
                        examples/vulkan_viewport_subregion_test.cpp)
        add_test(NAME Vulkan_Viewport_Subregion
                 COMMAND cna_test_vulkan_viewport_subregion)
        set_tests_properties(Vulkan_Viewport_Subregion PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

    endif()

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

    # Task 89: 3-frame smoke test for the Bgfx backend
    add_test(NAME Bgfx_Demo2D_SmokeTest
             COMMAND cna_demo_2d --smoke 3)
    set_tests_properties(Bgfx_Demo2D_SmokeTest PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

    # Task 179: RenderTargetUsage DiscardContents/PreserveContents smoke test
    cna_bgfx_test(cna_test_bgfx_render_target_usage
                  examples/bgfx_render_target_usage_test.cpp)
    add_test(NAME Bgfx_RenderTargetUsage
             COMMAND cna_test_bgfx_render_target_usage)
    set_tests_properties(Bgfx_RenderTargetUsage PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

    # Task 249: Bgfx vertex layout mapping tests — all 12 VertexElementFormat values
    cna_bgfx_test(cna_test_bgfx_vertex_format
                  examples/bgfx_vertex_format_test.cpp)
    add_test(NAME Bgfx_VertexFormatMapping
             COMMAND cna_test_bgfx_vertex_format)
    set_tests_properties(Bgfx_VertexFormatMapping PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

    # Task 278: EnvironmentMapEffect smoke test (no GPU readback available on Bgfx)
    cna_bgfx_test(cna_test_bgfx_env_map
                  examples/bgfx_env_map_test.cpp)
    add_test(NAME Bgfx_EnvironmentMapEffect_Smoke
             COMMAND cna_test_bgfx_env_map)
    set_tests_properties(Bgfx_EnvironmentMapEffect_Smoke PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

    # Task 393: EnvironmentMapEffect EnvironmentMapAmount=0 should ignore the cube map
    cna_bgfx_test(cna_test_bgfx_environmentmapeffect_amount_zero
                  examples/bgfx_environmentmapeffect_amount_zero_test.cpp)
    add_test(NAME Bgfx_EnvironmentMapEffect_AmountZero
             COMMAND cna_test_bgfx_environmentmapeffect_amount_zero)
    set_tests_properties(Bgfx_EnvironmentMapEffect_AmountZero PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

    # Task 394: EnvironmentMapEffect EnvironmentMapAmount=1 should fully replace lit color
    cna_bgfx_test(cna_test_bgfx_environmentmapeffect_amount_one
                  examples/bgfx_environmentmapeffect_amount_one_test.cpp)
    add_test(NAME Bgfx_EnvironmentMapEffect_AmountOne
             COMMAND cna_test_bgfx_environmentmapeffect_amount_one)
    set_tests_properties(Bgfx_EnvironmentMapEffect_AmountOne PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

    # Task 395: EnvironmentMapEffect EnvironmentMapSpecular should scale by cubemap alpha
    cna_bgfx_test(cna_test_bgfx_environmentmapeffect_specular
                  examples/bgfx_environmentmapeffect_specular_test.cpp)
    add_test(NAME Bgfx_EnvironmentMapEffect_Specular
             COMMAND cna_test_bgfx_environmentmapeffect_specular)
    set_tests_properties(Bgfx_EnvironmentMapEffect_Specular PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

    # Task 396: EnvironmentMapEffect Fresnel edge-weighting should suppress head-on reflections
    cna_bgfx_test(cna_test_bgfx_environmentmapeffect_fresnel
                  examples/bgfx_environmentmapeffect_fresnel_test.cpp)
    add_test(NAME Bgfx_EnvironmentMapEffect_Fresnel
             COMMAND cna_test_bgfx_environmentmapeffect_fresnel)
    set_tests_properties(Bgfx_EnvironmentMapEffect_Fresnel PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

    # Task 397: EnvironmentMapEffect reflection vector should respond to EyePosition
    cna_bgfx_test(cna_test_bgfx_environmentmapeffect_eyeposition
                  examples/bgfx_environmentmapeffect_eyeposition_test.cpp)
    add_test(NAME Bgfx_EnvironmentMapEffect_EyePosition
             COMMAND cna_test_bgfx_environmentmapeffect_eyeposition)
    set_tests_properties(Bgfx_EnvironmentMapEffect_EyePosition PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

    # Task 398: EnvironmentMapEffect normal transform should use inverse-transpose of World
    cna_bgfx_test(cna_test_bgfx_environmentmapeffect_worldtransform
                  examples/bgfx_environmentmapeffect_worldtransform_test.cpp)
    add_test(NAME Bgfx_EnvironmentMapEffect_WorldTransform
             COMMAND cna_test_bgfx_environmentmapeffect_worldtransform)
    set_tests_properties(Bgfx_EnvironmentMapEffect_WorldTransform PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

    # Task 399: cross-backend EnvironmentMapEffect capstone (Tasks 393-398 combined)
    cna_bgfx_test(cna_test_bgfx_environmentmapeffect_combined
                  examples/bgfx_environmentmapeffect_combined_test.cpp)
    add_test(NAME Bgfx_EnvironmentMapEffect_Combined
             COMMAND cna_test_bgfx_environmentmapeffect_combined)
    set_tests_properties(Bgfx_EnvironmentMapEffect_Combined PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

    # Task 406: SkinnedEffect identity bone palette should produce zero deformation
    cna_bgfx_test(cna_test_bgfx_skinnedeffect_identity_bones
                  examples/bgfx_skinnedeffect_identity_bones_test.cpp)
    add_test(NAME Bgfx_SkinnedEffect_IdentityBones
             COMMAND cna_test_bgfx_skinnedeffect_identity_bones)
    set_tests_properties(Bgfx_SkinnedEffect_IdentityBones PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

    # Task 407: SkinnedEffect single translation bone should shift the mesh
    cna_bgfx_test(cna_test_bgfx_skinnedeffect_translation_bone
                  examples/bgfx_skinnedeffect_translation_bone_test.cpp)
    add_test(NAME Bgfx_SkinnedEffect_TranslationBone
             COMMAND cna_test_bgfx_skinnedeffect_translation_bone)
    set_tests_properties(Bgfx_SkinnedEffect_TranslationBone PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

    # Task 408: SkinnedEffect two-bone weighted blend should produce midpoint deformation
    cna_bgfx_test(cna_test_bgfx_skinnedeffect_twobone_blend
                  examples/bgfx_skinnedeffect_twobone_blend_test.cpp)
    add_test(NAME Bgfx_SkinnedEffect_TwoBoneBlend
             COMMAND cna_test_bgfx_skinnedeffect_twobone_blend)
    set_tests_properties(Bgfx_SkinnedEffect_TwoBoneBlend PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

    # Task 409: cross-backend SkinnedEffect capstone (identity + single-bone + two-bone blend)
    cna_bgfx_test(cna_test_bgfx_skinnedeffect_combined
                  examples/bgfx_skinnedeffect_combined_test.cpp)
    add_test(NAME Bgfx_SkinnedEffect_Combined
             COMMAND cna_test_bgfx_skinnedeffect_combined)
    set_tests_properties(Bgfx_SkinnedEffect_Combined PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

    # Task 333: RenderTarget2D sampled as Texture2D via SpriteBatch after unbinding (Bgfx)
    cna_bgfx_test(cna_test_bgfx_render_target_sample
                  examples/bgfx_render_target_sample_test.cpp)
    add_test(NAME Bgfx_RenderTarget2D_SampleAfterUnbind
             COMMAND cna_test_bgfx_render_target_sample)
    set_tests_properties(Bgfx_RenderTarget2D_SampleAfterUnbind PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

    # Task 334: RenderTargetCube sampled as TextureCube via EnvironmentMapEffect after unbinding (Bgfx)
    cna_bgfx_test(cna_test_bgfx_render_target_cube_sample
                  examples/bgfx_render_target_cube_sample_test.cpp)
    add_test(NAME Bgfx_RenderTargetCube_SampleAfterUnbind
             COMMAND cna_test_bgfx_render_target_cube_sample)
    set_tests_properties(Bgfx_RenderTargetCube_SampleAfterUnbind PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

    # Task 364: BasicEffect pixel test — VertexColorEnabled=false, no texture, diffuse color only
    cna_bgfx_test(cna_test_bgfx_basiceffect_vertexcolor_disabled
                  examples/bgfx_basiceffect_vertexcolor_disabled_test.cpp)
    add_test(NAME Bgfx_BasicEffect_VertexColorDisabled
             COMMAND cna_test_bgfx_basiceffect_vertexcolor_disabled)
    set_tests_properties(Bgfx_BasicEffect_VertexColorDisabled PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

    # Task 365: BasicEffect pixel test — VertexColorEnabled=true, no texture, vertex color multiplication
    cna_bgfx_test(cna_test_bgfx_basiceffect_vertexcolor_enabled
                  examples/bgfx_basiceffect_vertexcolor_enabled_test.cpp)
    add_test(NAME Bgfx_BasicEffect_VertexColorEnabled
             COMMAND cna_test_bgfx_basiceffect_vertexcolor_enabled)
    set_tests_properties(Bgfx_BasicEffect_VertexColorEnabled PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

    # Task 366: BasicEffect pixel test — TextureEnabled=true, no vertex color
    cna_bgfx_test(cna_test_bgfx_basiceffect_texture_enabled
                  examples/bgfx_basiceffect_texture_enabled_test.cpp)
    add_test(NAME Bgfx_BasicEffect_TextureEnabled
             COMMAND cna_test_bgfx_basiceffect_texture_enabled)
    set_tests_properties(Bgfx_BasicEffect_TextureEnabled PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

    # Task 367: BasicEffect pixel test — TextureEnabled=true AND VertexColorEnabled=true
    cna_bgfx_test(cna_test_bgfx_basiceffect_texture_vertexcolor_enabled
                  examples/bgfx_basiceffect_texture_vertexcolor_enabled_test.cpp)
    add_test(NAME Bgfx_BasicEffect_TextureVertexColorEnabled
             COMMAND cna_test_bgfx_basiceffect_texture_vertexcolor_enabled)
    set_tests_properties(Bgfx_BasicEffect_TextureVertexColorEnabled PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

    # Task 368: BasicEffect pixel test — LightingEnabled=true, one DirectionalLight
    cna_bgfx_test(cna_test_bgfx_basiceffect_one_light
                  examples/bgfx_basiceffect_one_light_test.cpp)
    add_test(NAME Bgfx_BasicEffect_OneLight
             COMMAND cna_test_bgfx_basiceffect_one_light)
    set_tests_properties(Bgfx_BasicEffect_OneLight PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

    # Task 885: BasicEffect pixel test — DirectionalLight1/2 + EmissiveColor on the lit path
    cna_bgfx_test(cna_test_bgfx_basiceffect_multilight_emissive
                  examples/bgfx_basiceffect_multilight_emissive_test.cpp)
    add_test(NAME Bgfx_BasicEffect_MultiLightEmissive
             COMMAND cna_test_bgfx_basiceffect_multilight_emissive)
    set_tests_properties(Bgfx_BasicEffect_MultiLightEmissive PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

    # Task 886: BasicEffect pixel test — real specular highlights
    cna_bgfx_test(cna_test_bgfx_basiceffect_specular
                  examples/bgfx_basiceffect_specular_test.cpp)
    add_test(NAME Bgfx_BasicEffect_Specular
             COMMAND cna_test_bgfx_basiceffect_specular)
    set_tests_properties(Bgfx_BasicEffect_Specular PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

    # Task 887: AlphaTestEffect.VertexColorEnabled fix verification
    cna_bgfx_test(cna_test_bgfx_alphatest_vertexcolor
                  examples/bgfx_alphatest_vertexcolor_test.cpp)
    add_test(NAME Bgfx_AlphaTest_VertexColor
             COMMAND cna_test_bgfx_alphatest_vertexcolor)
    set_tests_properties(Bgfx_AlphaTest_VertexColor PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

    # Task 888: BasicEffect fog on the colored3d (stride-16) pipeline
    cna_bgfx_test(cna_test_bgfx_basiceffect_fog
                  examples/bgfx_basiceffect_fog_test.cpp)
    add_test(NAME Bgfx_BasicEffect_Fog
             COMMAND cna_test_bgfx_basiceffect_fog)
    set_tests_properties(Bgfx_BasicEffect_Fog PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

    # Task 888: BasicEffect fog on the lit_textured3d (stride-32) pipeline
    cna_bgfx_test(cna_test_bgfx_basiceffect_lit_fog
                  examples/bgfx_basiceffect_lit_fog_test.cpp)
    add_test(NAME Bgfx_BasicEffect_LitFog
             COMMAND cna_test_bgfx_basiceffect_lit_fog)
    set_tests_properties(Bgfx_BasicEffect_LitFog PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

    # Task 888: AlphaTestEffect fog on the alpha_test3d pipeline
    cna_bgfx_test(cna_test_bgfx_alphatest_fog
                  examples/bgfx_alphatest_fog_test.cpp)
    add_test(NAME Bgfx_AlphaTest_Fog
             COMMAND cna_test_bgfx_alphatest_fog)
    set_tests_properties(Bgfx_AlphaTest_Fog PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

    # Task 888: DualTextureEffect fog on the dual_texture3d pipeline
    cna_bgfx_test(cna_test_bgfx_dualtextureeffect_fog
                  examples/bgfx_dualtextureeffect_fog_test.cpp)
    add_test(NAME Bgfx_DualTextureEffect_Fog
             COMMAND cna_test_bgfx_dualtextureeffect_fog)
    set_tests_properties(Bgfx_DualTextureEffect_Fog PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

    # Task 892: BasicEffect lit-textured normal transform under a real camera (Bgfx)
    cna_bgfx_test(cna_test_bgfx_basiceffect_normaltransform
                  examples/bgfx_basiceffect_normaltransform_test.cpp)
    add_test(NAME Bgfx_BasicEffect_NormalTransform
             COMMAND cna_test_bgfx_basiceffect_normaltransform)
    set_tests_properties(Bgfx_BasicEffect_NormalTransform PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

    # Task 369: BasicEffect pixel test — DiffuseColor+EmissiveColor, LightingEnabled=false
    cna_bgfx_test(cna_test_bgfx_basiceffect_emissive
                  examples/bgfx_basiceffect_emissive_test.cpp)
    add_test(NAME Bgfx_BasicEffect_Emissive
             COMMAND cna_test_bgfx_basiceffect_emissive)
    set_tests_properties(Bgfx_BasicEffect_Emissive PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

    # Task 370: BasicEffect cross-backend image comparison suite — closes Phase 42
    cna_bgfx_test(cna_test_bgfx_basiceffect_combined
                  examples/bgfx_basiceffect_combined_test.cpp)
    add_test(NAME Bgfx_BasicEffect_Combined
             COMMAND cna_test_bgfx_basiceffect_combined)
    set_tests_properties(Bgfx_BasicEffect_Combined PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

    # Task 375: AlphaTestEffect all CompareFunction modes — threshold sweep
    cna_bgfx_test(cna_test_bgfx_alphatest_comparefunction_sweep
                  examples/bgfx_alphatest_comparefunction_sweep_test.cpp)
    add_test(NAME Bgfx_AlphaTest_CompareFunctionSweep
             COMMAND cna_test_bgfx_alphatest_comparefunction_sweep)
    set_tests_properties(Bgfx_AlphaTest_CompareFunctionSweep PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

    # Task 379: AlphaTestEffect null/no-texture behavior
    cna_bgfx_test(cna_test_bgfx_alphatest_null_texture
                  examples/bgfx_alphatest_null_texture_test.cpp)
    add_test(NAME Bgfx_AlphaTest_NullTexture
             COMMAND cna_test_bgfx_alphatest_null_texture)
    set_tests_properties(Bgfx_AlphaTest_NullTexture PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

    # Task 383: DualTextureEffect two-texture blend formula, including FNA's color.rgb*=2.
    # Also the first-ever Bgfx pixel-integration test for DualTextureEffect.
    cna_bgfx_test(cna_test_bgfx_dualtextureeffect_doubling
                  examples/bgfx_dualtextureeffect_doubling_test.cpp)
    add_test(NAME Bgfx_DualTextureEffect_Doubling
             COMMAND cna_test_bgfx_dualtextureeffect_doubling)
    set_tests_properties(Bgfx_DualTextureEffect_Doubling PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

    # Task 384: DualTextureEffect magenta × yellow = red (mirrors Task 133/135 EasyGL/Vulkan)
    cna_bgfx_test(cna_test_bgfx_dual_texture
                  examples/bgfx_dual_texture_test.cpp)
    add_test(NAME Bgfx_DualTextureEffect_Blend
             COMMAND cna_test_bgfx_dual_texture)
    set_tests_properties(Bgfx_DualTextureEffect_Blend PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

    # Task 385: DualTextureEffect Alpha premultiplication, verified via real AlphaBlend
    cna_bgfx_test(cna_test_bgfx_dualtextureeffect_alpha
                  examples/bgfx_dualtextureeffect_alpha_test.cpp)
    add_test(NAME Bgfx_DualTextureEffect_Alpha
             COMMAND cna_test_bgfx_dualtextureeffect_alpha)
    set_tests_properties(Bgfx_DualTextureEffect_Alpha PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

    # Task 386: DualTextureEffect first texture (Texture, slot 0) null behavior
    cna_bgfx_test(cna_test_bgfx_dualtextureeffect_null_texture0
                  examples/bgfx_dualtextureeffect_null_texture0_test.cpp)
    add_test(NAME Bgfx_DualTextureEffect_NullTexture0
             COMMAND cna_test_bgfx_dualtextureeffect_null_texture0)
    set_tests_properties(Bgfx_DualTextureEffect_NullTexture0 PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

    # Task 387: DualTextureEffect second texture (Texture2, slot 1) null behavior
    cna_bgfx_test(cna_test_bgfx_dualtextureeffect_null_texture2
                  examples/bgfx_dualtextureeffect_null_texture2_test.cpp)
    add_test(NAME Bgfx_DualTextureEffect_NullTexture2
             COMMAND cna_test_bgfx_dualtextureeffect_null_texture2)
    set_tests_properties(Bgfx_DualTextureEffect_NullTexture2 PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

    # Task 389: DualTextureEffect cross-backend image comparison suite (closes Phase 44 pixel work)
    cna_bgfx_test(cna_test_bgfx_dualtextureeffect_combined
                  examples/bgfx_dualtextureeffect_combined_test.cpp)
    add_test(NAME Bgfx_DualTextureEffect_Combined
             COMMAND cna_test_bgfx_dualtextureeffect_combined)
    set_tests_properties(Bgfx_DualTextureEffect_Combined PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

    # Task 880: GraphicsDevice.Viewport GPU wiring — sub-region viewport pixel-readback test
    cna_bgfx_test(cna_test_bgfx_viewport_subregion
                  examples/bgfx_viewport_subregion_test.cpp)
    add_test(NAME Bgfx_Viewport_Subregion
             COMMAND cna_test_bgfx_viewport_subregion)
    set_tests_properties(Bgfx_Viewport_Subregion PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

    # Task 899: EnvironmentMapEffect/SkinnedEffect fog on Bgfx (opportunistic bonus scope)
    cna_bgfx_test(cna_test_bgfx_environmentmapeffect_fog
                  examples/bgfx_environmentmapeffect_fog_test.cpp)
    add_test(NAME Bgfx_EnvironmentMapEffect_Fog
             COMMAND cna_test_bgfx_environmentmapeffect_fog)
    set_tests_properties(Bgfx_EnvironmentMapEffect_Fog PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

    cna_bgfx_test(cna_test_bgfx_skinnedeffect_fog
                  examples/bgfx_skinnedeffect_fog_test.cpp)
    add_test(NAME Bgfx_SkinnedEffect_Fog
             COMMAND cna_test_bgfx_skinnedeffect_fog)
    set_tests_properties(Bgfx_SkinnedEffect_Fog PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=:0")

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

    if(TARGET easy-gl)
        target_link_libraries(CnaTests PRIVATE easy-gl)
    endif()

    include(GoogleTest)
    gtest_discover_tests(CnaTests DISCOVERY_MODE PRE_TEST)

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