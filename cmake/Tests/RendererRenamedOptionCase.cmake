# SPDX-License-Identifier: MS-PL
#
# Proves that an obsolete renderer feature option is rejected by name even when its value is OFF.
# A removed option left unread in CMake's cache would otherwise look harmless while silently doing
# nothing, which is precisely why the SDLGPU rename refuses the old spelling instead of aliasing it.

if(NOT DEFINED CNA_RENAMED_OPTION_CASE_FILE OR
   NOT DEFINED CNA_RENAMED_OPTION_CASE_OLD OR
   NOT DEFINED CNA_RENAMED_OPTION_CASE_NEW)
    message(FATAL_ERROR
        "renamed-option test requires the selection file, old option and new option")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-D${CNA_RENAMED_OPTION_CASE_OLD}=OFF"
        -P "${CNA_RENAMED_OPTION_CASE_FILE}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../.."
    RESULT_VARIABLE _cna_case_result
    OUTPUT_VARIABLE _cna_case_stdout
    ERROR_VARIABLE _cna_case_stderr
)
set(_cna_case_output "${_cna_case_stdout}${_cna_case_stderr}")

if(_cna_case_result EQUAL 0)
    message(FATAL_ERROR
        "${CNA_RENAMED_OPTION_CASE_OLD}=OFF was accepted; the obsolete spelling must be refused "
        "rather than retained as an unused cache entry")
endif()

set(_cna_expected_fragments
    "CMake option ${CNA_RENAMED_OPTION_CASE_OLD}"
    "was renamed to"
    "${CNA_RENAMED_OPTION_CASE_NEW}"
    "the old spelling is not an alias")
foreach(_cna_expected IN LISTS _cna_expected_fragments)
    string(FIND "${_cna_case_output}" "${_cna_expected}" _cna_expected_at)
    if(_cna_expected_at EQUAL -1)
        message(FATAL_ERROR
            "renamed-option refusal did not contain '${_cna_expected}':\n${_cna_case_output}")
    endif()
endforeach()
