cmake_minimum_required(VERSION 3.20)

project(CNA LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CNA_BUILD_TESTS "Build CNA tests" ON)
option(CNA_NOXNA "Enable CNA NOXNA extended graphics layer (beyond XNA 4.0)" OFF)

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

# --- FFmpeg (video decoding) — not available on Emscripten/Android ---
if(NOT EMSCRIPTEN AND NOT ANDROID)
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

# Exclude FFmpeg-dependent sources on platforms where FFmpeg is unavailable
if(EMSCRIPTEN OR ANDROID)
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

if(NOT EMSCRIPTEN AND NOT ANDROID)
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

        cna_easygl_test(cna_test_easygl_alphatest_modes
                        examples/easygl_alphatest_modes_test.cpp)
        add_test(NAME EasyGL_AlphaTestModes
                 COMMAND cna_test_easygl_alphatest_modes)
        set_tests_properties(EasyGL_AlphaTestModes PROPERTIES
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

        # Task 136: EnvironmentMapEffect — emissive=(1,0,0), envAmount=0 → red pixel on Vulkan
        cna_vulkan_test(cna_test_vulkan_env_map
                        examples/vulkan_env_map_test.cpp)
        add_test(NAME Vulkan_EnvironmentMapEffect_Readback
                 COMMAND cna_test_vulkan_env_map)
        set_tests_properties(Vulkan_EnvironmentMapEffect_Readback PROPERTIES
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