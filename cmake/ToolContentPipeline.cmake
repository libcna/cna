# plans/plan_content_pipeline.md CP-006/CP-022: one command-line coordinator implementation is
# linked by both the stock front end and user-built custom content compilers. Keep cache,
# configuration and atomic-publication logic here rather than copying it into custom launchers.
add_library(cna_content_compiler STATIC
    tools/content/content.cpp
)
add_library(CNA::ContentCompiler ALIAS cna_content_compiler)
target_include_directories(cna_content_compiler PRIVATE
    ${CNA_SOURCE_DIR}/tools/common
)
# cna_content_pipeline carries the build-time-only components (the FreeType-backed .spritefont
# route). Linking it here and nowhere else is what keeps FreeType out of a runtime game's closure
# while still giving the CLI, custom compilers and the tests the complete source-route set.
target_link_libraries(cna_content_compiler PUBLIC cna_content cna_content_pipeline)
cna_link_sharp_runtime(cna_content_compiler PRIVATE)

add_executable(cna_content_tool
    tools/content/content_main.cpp
)
set_target_properties(cna_content_tool PROPERTIES OUTPUT_NAME "cna-content")
target_link_libraries(cna_content_tool PRIVATE cna_content_compiler)
cna_link_sharp_runtime(cna_content_tool PRIVATE)
if(MINGW)
    # MinGW's default console CRT calls main/WinMain. Select its Unicode console CRT so the
    # native-path entry point in content_main.cpp is resolved as wmain instead.
    target_link_options(cna_content_tool PRIVATE -municode)
endif()

if(CNA_BUILD_EXAMPLES OR CNA_BUILD_TESTS)
    # CP-022: a real user-owned compiler executable, deliberately source/toolchain linked rather
    # than loaded through an unsupported dynamic C++ plugin ABI.
    add_executable(cna_custom_content_compiler_example
        modules/content/examples/custom-content-compiler.cpp
    )
    target_link_libraries(cna_custom_content_compiler_example PRIVATE cna_content_compiler)
    cna_link_sharp_runtime(cna_custom_content_compiler_example PRIVATE)
    if(MINGW)
        target_link_options(cna_custom_content_compiler_example PRIVATE -municode)
    endif()

    # plans/plan_xnapipeline_parity.md XNAPP-260: the same idea one layer up -- a user-owned
    # compiler whose route is written against the XNA façade rather than the canonical API, so the
    # documented way to extend an XNA content pipeline is exercised by something outside CNA.
    add_executable(cna_xna_custom_pipeline_example
        modules/content-pipeline/examples/xna-custom-pipeline.cpp
    )
    target_link_libraries(cna_xna_custom_pipeline_example PRIVATE cna_content_compiler)
    cna_link_sharp_runtime(cna_xna_custom_pipeline_example PRIVATE)
    if(MINGW)
        target_link_options(cna_xna_custom_pipeline_example PRIVATE -municode)
    endif()
endif()

# Adds a build target that delegates content compilation to the same cna-content executable users
# invoke manually. The target intentionally runs whenever requested; cna-content's content-hashed
# manifest makes an identical run a cheap, correct no-op without teaching CMake a second dependency
# model. SOURCE_DIR and an optional CONFIG_FILE are relative to the caller's source directory;
# OUTPUT_DIR is relative to its binary directory. Cross builds require an explicit host
# CONTENT_EXECUTABLE because a target-platform tool cannot be executed by the host build.
#
# plans/plan_xnapipeline_parity.md XNAPP-320: the two inputs an XNA game actually has were reachable
# from the command line and not from here -- an existing `.contentproj`, and the `.xnb` container.
# CONTENT_PROJECT names the first and FORMAT/XNB_* select the second, so a ported project needs no
# hand-written add_custom_command to say what `cna-content` already knows how to do. The values are
# forwarded rather than re-validated: the CLI is the one authority on what a platform, profile or
# compression name means, exactly as it is the one authority on source discovery.
function(cna_add_content)
    set(_cna_content_options QUIET XNA_COMPATIBLE)
    set(_cna_content_one_value
        TARGET SOURCE_DIR CONTENT_PROJECT OUTPUT_DIR CONFIG_FILE WORKERS CONTENT_EXECUTABLE
        FORMAT XNB_PLATFORM XNB_PROFILE XNB_COMPRESS)
    cmake_parse_arguments(PARSE_ARGV 0 CNA_CONTENT
        "${_cna_content_options}" "${_cna_content_one_value}" "")

    if(CNA_CONTENT_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR
            "cna_add_content: argument(s) require a value: "
            "${CNA_CONTENT_KEYWORDS_MISSING_VALUES}")
    endif()
    if(CNA_CONTENT_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "cna_add_content: unknown argument(s): ${CNA_CONTENT_UNPARSED_ARGUMENTS}")
    endif()
    foreach(_required IN ITEMS TARGET OUTPUT_DIR)
        if(NOT CNA_CONTENT_${_required})
            message(FATAL_ERROR "cna_add_content: ${_required} is required")
        endif()
    endforeach()
    if(CNA_CONTENT_SOURCE_DIR AND CNA_CONTENT_CONTENT_PROJECT)
        message(FATAL_ERROR
            "cna_add_content: SOURCE_DIR and CONTENT_PROJECT are alternatives; give one")
    endif()
    if(NOT CNA_CONTENT_SOURCE_DIR AND NOT CNA_CONTENT_CONTENT_PROJECT)
        message(FATAL_ERROR "cna_add_content: SOURCE_DIR or CONTENT_PROJECT is required")
    endif()
    if(TARGET "${CNA_CONTENT_TARGET}")
        message(FATAL_ERROR
            "cna_add_content: target '${CNA_CONTENT_TARGET}' already exists")
    endif()

    cmake_path(ABSOLUTE_PATH CNA_CONTENT_OUTPUT_DIR
        BASE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" NORMALIZE
        OUTPUT_VARIABLE _cna_content_output)
    if(CNA_CONTENT_SOURCE_DIR)
        cmake_path(ABSOLUTE_PATH CNA_CONTENT_SOURCE_DIR
            BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE
            OUTPUT_VARIABLE _cna_content_source)
        if(NOT IS_DIRECTORY "${_cna_content_source}")
            message(FATAL_ERROR
                "cna_add_content: SOURCE_DIR is not a directory: ${_cna_content_source}")
        endif()
    else()
        cmake_path(ABSOLUTE_PATH CNA_CONTENT_CONTENT_PROJECT
            BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE
            OUTPUT_VARIABLE _cna_content_source)
        if(NOT EXISTS "${_cna_content_source}" OR IS_DIRECTORY "${_cna_content_source}")
            message(FATAL_ERROR
                "cna_add_content: CONTENT_PROJECT is not a file: ${_cna_content_source}")
        endif()
        cmake_path(GET _cna_content_source EXTENSION LAST_ONLY _cna_content_project_extension)
        if(NOT _cna_content_project_extension STREQUAL ".contentproj")
            message(FATAL_ERROR
                "cna_add_content: CONTENT_PROJECT must name a .contentproj: "
                "${_cna_content_source}")
        endif()
        unset(_cna_content_project_extension)
        # A project carries its own platform, profile, compression and per-item components, and the
        # coordinator reads them from it. Accepting an option that would silently lose to the file
        # is worse than refusing it.
        foreach(_conflicting IN ITEMS FORMAT XNB_PLATFORM XNB_PROFILE XNB_COMPRESS CONFIG_FILE)
            if(CNA_CONTENT_${_conflicting})
                message(FATAL_ERROR
                    "cna_add_content: ${_conflicting} cannot accompany CONTENT_PROJECT; the "
                    "project carries its own")
            endif()
        endforeach()
        if(CNA_CONTENT_XNA_COMPATIBLE)
            message(FATAL_ERROR
                "cna_add_content: XNA_COMPATIBLE cannot accompany CONTENT_PROJECT; a .contentproj "
                "is built with XNA's leniency already")
        endif()
    endif()

    set(_cna_content_format)
    if(DEFINED CNA_CONTENT_FORMAT)
        if(NOT CNA_CONTENT_FORMAT MATCHES "^(cnb|xnb)$")
            message(FATAL_ERROR
                "cna_add_content: FORMAT must be cnb or xnb, not '${CNA_CONTENT_FORMAT}'")
        endif()
        set(_cna_content_format "${CNA_CONTENT_FORMAT}")
    endif()
    foreach(_xnb_only IN ITEMS XNB_PLATFORM XNB_PROFILE XNB_COMPRESS)
        if(CNA_CONTENT_${_xnb_only} AND NOT _cna_content_format STREQUAL "xnb")
            message(FATAL_ERROR
                "cna_add_content: ${_xnb_only} needs FORMAT xnb")
        endif()
    endforeach()

    set(_cna_content_config)
    if(DEFINED CNA_CONTENT_CONFIG_FILE)
        cmake_path(ABSOLUTE_PATH CNA_CONTENT_CONFIG_FILE
            BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE
            OUTPUT_VARIABLE _cna_content_config)
        if(NOT EXISTS "${_cna_content_config}" OR IS_DIRECTORY "${_cna_content_config}")
            message(FATAL_ERROR
                "cna_add_content: CONFIG_FILE is not a file: ${_cna_content_config}")
        endif()
    endif()

    set(_cna_content_workers 1)
    if(DEFINED CNA_CONTENT_WORKERS)
        if(NOT "${CNA_CONTENT_WORKERS}" MATCHES "^([1-9]|[1-5][0-9]|6[0-4])$")
            message(FATAL_ERROR
                "cna_add_content: WORKERS must be an integer between 1 and 64")
        endif()
        set(_cna_content_workers "${CNA_CONTENT_WORKERS}")
    endif()

    set(_cna_content_dependencies)
    if(CNA_CONTENT_CONTENT_EXECUTABLE)
        set(_cna_content_compiler "${CNA_CONTENT_CONTENT_EXECUTABLE}")
    elseif(CMAKE_CROSSCOMPILING)
        message(FATAL_ERROR
            "cna_add_content: cross-compiling requires CONTENT_EXECUTABLE to name a host "
            "cna-content executable")
    else()
        set(_cna_content_compiler "$<TARGET_FILE:cna_content_tool>")
        list(APPEND _cna_content_dependencies cna_content_tool)
    endif()

    set(_cna_content_command
        "${_cna_content_compiler}" build "${_cna_content_source}" -o "${_cna_content_output}")
    if(_cna_content_config)
        list(APPEND _cna_content_command --config "${_cna_content_config}")
    endif()
    if(_cna_content_format)
        list(APPEND _cna_content_command --format "${_cna_content_format}")
    endif()
    if(CNA_CONTENT_XNB_PLATFORM)
        list(APPEND _cna_content_command --xnb-platform "${CNA_CONTENT_XNB_PLATFORM}")
    endif()
    if(CNA_CONTENT_XNB_PROFILE)
        list(APPEND _cna_content_command --xnb-profile "${CNA_CONTENT_XNB_PROFILE}")
    endif()
    if(CNA_CONTENT_XNB_COMPRESS)
        list(APPEND _cna_content_command --xnb-compress "${CNA_CONTENT_XNB_COMPRESS}")
    endif()
    if(CNA_CONTENT_XNA_COMPATIBLE)
        list(APPEND _cna_content_command --xna-compatible)
    endif()
    list(APPEND _cna_content_command --workers "${_cna_content_workers}")
    if(CNA_CONTENT_QUIET)
        list(APPEND _cna_content_command --quiet)
    endif()

    add_custom_target("${CNA_CONTENT_TARGET}"
        COMMAND ${_cna_content_command}
        DEPENDS ${_cna_content_dependencies}
        COMMENT "Building CNA content: ${_cna_content_source} -> ${_cna_content_output}"
        VERBATIM
    )
    if(CNA_CONTENT_SOURCE_DIR)
        set_property(TARGET "${CNA_CONTENT_TARGET}" PROPERTY
            CNA_CONTENT_SOURCE_DIR "${_cna_content_source}")
    else()
        set_property(TARGET "${CNA_CONTENT_TARGET}" PROPERTY
            CNA_CONTENT_CONTENT_PROJECT "${_cna_content_source}")
    endif()
    set_property(TARGET "${CNA_CONTENT_TARGET}" PROPERTY
        CNA_CONTENT_OUTPUT_DIR "${_cna_content_output}")
    set_property(TARGET "${CNA_CONTENT_TARGET}" PROPERTY
        CNA_CONTENT_CONFIG_FILE "${_cna_content_config}")
    set_property(TARGET "${CNA_CONTENT_TARGET}" PROPERTY
        CNA_CONTENT_WORKERS "${_cna_content_workers}")
    set_property(TARGET "${CNA_CONTENT_TARGET}" PROPERTY
        CNA_CONTENT_FORMAT "${_cna_content_format}")
endfunction()
