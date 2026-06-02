cmake_minimum_required(VERSION 3.20)

project(CNA LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CNA_BUILD_TESTS "Build CNA tests" ON)

include(cmake/ThirdPartySDL.cmake)
cna_configure_vendored_sdl()

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
    set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BGFX_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
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
else()
    message(FATAL_ERROR "CNA: Unknown graphics backend: ${CNA_GRAPHICS_BACKEND}")
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
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3)
endif()

file(GLOB_RECURSE CNA_SOURCES CONFIGURE_DEPENDS
        "src/*.cpp"
)
# Exclude backend sources from main CNA library to avoid double compilation and conflicts
list(FILTER CNA_SOURCES EXCLUDE REGEX "src/CNA/Internal/Backends/.*")

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
endif()

if(CNA_BUILD_EXAMPLES AND CNA_GRAPHICS_BACKEND STREQUAL "EASYGL")
    # ---- 3D house demo (EasyGL only) -----------------------------------------
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
endif()

if(CNA_BUILD_TESTS)
    add_subdirectory(vendor/googletest)

    enable_testing()

    file(GLOB_RECURSE CNA_TEST_SOURCES CONFIGURE_DEPENDS
            "tests/*.cpp"
    )

    add_executable(CnaTests
            ${CNA_TEST_SOURCES}
    )

    target_link_libraries(CnaTests
            PRIVATE
            CNA
            SHARP_RUNTIME
            gtest_main
            SDL3::SDL3
    )

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