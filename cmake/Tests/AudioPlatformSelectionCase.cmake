# SPDX-License-Identifier: MS-PL

if(NOT DEFINED CNA_AUDIO_SELECTION_FILE OR NOT DEFINED CNA_AUDIO_SELECTION_CASE OR
   NOT DEFINED CNA_AUDIO_SELECTION_EXPECTED)
    message(FATAL_ERROR "audio selection test requires file, case and expected text")
endif()

set(_cna_audio_selection_command "${CMAKE_COMMAND}")
if(NOT CNA_AUDIO_SELECTION_CASE STREQUAL "DEFAULT")
    list(APPEND _cna_audio_selection_command
        "-DCNA_AUDIO_PLATFORM=${CNA_AUDIO_SELECTION_CASE}")
endif()
list(APPEND _cna_audio_selection_command -P "${CNA_AUDIO_SELECTION_FILE}")

execute_process(
    COMMAND ${_cna_audio_selection_command}
    RESULT_VARIABLE _cna_audio_selection_result
    OUTPUT_VARIABLE _cna_audio_selection_stdout
    ERROR_VARIABLE _cna_audio_selection_stderr
)
set(_cna_audio_selection_output
    "${_cna_audio_selection_stdout}${_cna_audio_selection_stderr}")

if(CNA_AUDIO_SELECTION_CASE STREQUAL "DEFAULT" OR
   CNA_AUDIO_SELECTION_CASE STREQUAL "SDL3" OR
   CNA_AUDIO_SELECTION_CASE STREQUAL "SDL2" OR
   CNA_AUDIO_SELECTION_CASE STREQUAL "NULL")
    if(NOT _cna_audio_selection_result EQUAL 0)
        message(FATAL_ERROR
            "CNA_AUDIO_PLATFORM=${CNA_AUDIO_SELECTION_CASE} was rejected:\n"
            "${_cna_audio_selection_output}")
    endif()
else()
    if(_cna_audio_selection_result EQUAL 0)
        message(FATAL_ERROR
            "CNA_AUDIO_PLATFORM=${CNA_AUDIO_SELECTION_CASE} unexpectedly succeeded")
    endif()
endif()

string(FIND "${_cna_audio_selection_output}" "${CNA_AUDIO_SELECTION_EXPECTED}"
       _cna_audio_selection_expected_at)
if(_cna_audio_selection_expected_at EQUAL -1)
    message(FATAL_ERROR
        "CNA_AUDIO_PLATFORM=${CNA_AUDIO_SELECTION_CASE} output did not contain "
        "'${CNA_AUDIO_SELECTION_EXPECTED}':\n${_cna_audio_selection_output}")
endif()
