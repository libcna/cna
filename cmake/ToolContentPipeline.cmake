# plans/plan_content_pipeline.md CP-006: unified CNA Content Pipeline front end.
add_executable(cna_content_tool
    tools/content/content.cpp
)
set_target_properties(cna_content_tool PROPERTIES OUTPUT_NAME "cna-content")
target_include_directories(cna_content_tool PRIVATE
    ${CNA_SOURCE_DIR}/tools/common
)
target_link_libraries(cna_content_tool PRIVATE cna_content)
cna_link_sharp_runtime(cna_content_tool PRIVATE)

# Adds a build target that delegates content compilation to the same cna-content executable users
# invoke manually. The target intentionally runs whenever requested; cna-content's content-hashed
# manifest makes an identical run a cheap, correct no-op without teaching CMake a second dependency
# model. SOURCE_DIR is relative to the caller's source directory, OUTPUT_DIR to its binary directory.
# Cross builds require an explicit host CONTENT_EXECUTABLE because a target-platform tool cannot be
# executed by the host build.
function(cna_add_content)
    set(_cna_content_options QUIET)
    set(_cna_content_one_value TARGET SOURCE_DIR OUTPUT_DIR CONTENT_EXECUTABLE)
    cmake_parse_arguments(PARSE_ARGV 0 CNA_CONTENT
        "${_cna_content_options}" "${_cna_content_one_value}" "")

    if(CNA_CONTENT_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "cna_add_content: unknown argument(s): ${CNA_CONTENT_UNPARSED_ARGUMENTS}")
    endif()
    foreach(_required IN ITEMS TARGET SOURCE_DIR OUTPUT_DIR)
        if(NOT CNA_CONTENT_${_required})
            message(FATAL_ERROR "cna_add_content: ${_required} is required")
        endif()
    endforeach()
    if(TARGET "${CNA_CONTENT_TARGET}")
        message(FATAL_ERROR
            "cna_add_content: target '${CNA_CONTENT_TARGET}' already exists")
    endif()

    cmake_path(ABSOLUTE_PATH CNA_CONTENT_SOURCE_DIR
        BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE
        OUTPUT_VARIABLE _cna_content_source)
    cmake_path(ABSOLUTE_PATH CNA_CONTENT_OUTPUT_DIR
        BASE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" NORMALIZE
        OUTPUT_VARIABLE _cna_content_output)
    if(NOT IS_DIRECTORY "${_cna_content_source}")
        message(FATAL_ERROR
            "cna_add_content: SOURCE_DIR is not a directory: ${_cna_content_source}")
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
    if(CNA_CONTENT_QUIET)
        list(APPEND _cna_content_command --quiet)
    endif()

    add_custom_target("${CNA_CONTENT_TARGET}"
        COMMAND ${_cna_content_command}
        DEPENDS ${_cna_content_dependencies}
        COMMENT "Building CNA content: ${_cna_content_source} -> ${_cna_content_output}"
        VERBATIM
    )
    set_property(TARGET "${CNA_CONTENT_TARGET}" PROPERTY
        CNA_CONTENT_SOURCE_DIR "${_cna_content_source}")
    set_property(TARGET "${CNA_CONTENT_TARGET}" PROPERTY
        CNA_CONTENT_OUTPUT_DIR "${_cna_content_output}")
endfunction()
