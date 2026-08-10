# Input-subsystem examples (module-local registration; the target definitions moved here
# verbatim from cmake/Examples.cmake when examples moved into their owning physical modules).

# Example/demo executables historically live at the build root; keep them there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

if(CNA_BUILD_EXAMPLES)
    # ---- Input & Touch demo (works with all renderers) ------------------------
    add_executable(cna_demo_input
        demo_input/src/Main.cpp
        demo_input/src/InputDemo.cpp
    )
    target_include_directories(cna_demo_input PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/demo_input/src
    )

    if(EMSCRIPTEN)
        set_target_properties(cna_demo_input PROPERTIES SUFFIX ".html")
        target_link_libraries(cna_demo_input PRIVATE
            CNA ${RENDERER_TARGET} SDL3::SDL3-static SHARP_RUNTIME)
        target_link_options(cna_demo_input PRIVATE
            -sALLOW_MEMORY_GROWTH=1
            -sSTACK_SIZE=1048576
            -sINITIAL_MEMORY=67108864
            -sFORCE_FILESYSTEM=1
        )
    else()
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32 AND NOT APPLE)
            target_link_libraries(cna_demo_input PRIVATE
                CNA
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

# ---- Combined input smoke sample: Keyboard+Mouse+GamePad+Touch+TextInput -----
if(CNA_BUILD_EXAMPLES AND NOT EMSCRIPTEN AND NOT ANDROID)
    add_executable(cna_input_smoke input_smoke/InputSmoke.cpp)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32 AND NOT APPLE)
        target_link_libraries(cna_input_smoke PRIVATE
            CNA SHARP_RUNTIME)
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
