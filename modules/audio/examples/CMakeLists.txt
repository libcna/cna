# Audio-subsystem examples (module-local registration; the target definitions moved here
# verbatim from cmake/Examples.cmake when examples moved into their owning physical modules).

# Example/demo executables historically live at the build root; keep them there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

if(CNA_BUILD_EXAMPLES)
    # ---- Audio demo (works with all renderers) --------------------------------
    add_executable(cna_demo_sound
        demo_sound/src/Main.cpp
        demo_sound/src/SoundDemo.cpp
    )
    target_include_directories(cna_demo_sound PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/demo_sound/src
    )

    if(EMSCRIPTEN)
        set_target_properties(cna_demo_sound PROPERTIES SUFFIX ".html")
        target_link_libraries(cna_demo_sound PRIVATE
            CNA ${RENDERER_TARGET} SDL3::SDL3-static SHARP_RUNTIME)
        target_link_options(cna_demo_sound PRIVATE
            -sALLOW_MEMORY_GROWTH=1
            -sSTACK_SIZE=1048576
            -sINITIAL_MEMORY=134217728
            -sFORCE_FILESYSTEM=1
            "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/demo_sound/Content@/Content"
        )
    else()
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32 AND NOT APPLE)
            target_link_libraries(cna_demo_sound PRIVATE
                CNA
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_sound PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_sound PRIVATE SDL3::SDL3main)
        endif()
        add_custom_command(TARGET cna_demo_sound POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                -DCNA_COPY_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/demo_sound/Content
                -DCNA_COPY_DESTINATION=$<TARGET_FILE_DIR:cna_demo_sound>/Content
                -DCNA_COPY_LOCK=${CMAKE_BINARY_DIR}/CMakeFiles/cna-example-content.lock
                -P ${CMAKE_SOURCE_DIR}/cmake/CopyDirectoryLocked.cmake
            COMMENT "Copying Content next to cna_demo_sound executable"
            VERBATIM)
        if(WIN32)
            set_target_properties(cna_demo_sound PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_sound)
        endif()
    endif()

    # ---- XACT demo (desktop only — XACT is Windows/Xbox-specific, no web content) -
    if(NOT EMSCRIPTEN AND NOT ANDROID)
    add_executable(cna_demo_xact
        demo_xact/src/Main.cpp
        demo_xact/src/XactDemo.cpp
    )
    target_include_directories(cna_demo_xact PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/demo_xact/src
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32 AND NOT APPLE)
        target_link_libraries(cna_demo_xact PRIVATE
            CNA
            SHARP_RUNTIME)
    else()
        target_link_libraries(cna_demo_xact PRIVATE CNA SHARP_RUNTIME)
    endif()
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_demo_xact PRIVATE SDL3::SDL3main)
    endif()
    # REMED-BUILD-002: no POST_BUILD Content copy here, unlike the other demo targets above --
    # demo_xact/ has no Content/ directory at all. XactDemo::LoadContent() generates its
    # own XGS/XWB/XSB files at runtime via XactFileGen::GenerateXactFiles() into "Content/Audio"
    # relative to the process's working directory, so there is nothing to copy from source.
    if(WIN32)
        set_target_properties(cna_demo_xact PROPERTIES WIN32_EXECUTABLE TRUE)
        cna_copy_sdl_runtime(cna_demo_xact)
    endif()
    endif() # NOT EMSCRIPTEN AND NOT ANDROID
endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
