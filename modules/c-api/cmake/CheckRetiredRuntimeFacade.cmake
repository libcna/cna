# SPDX-License-Identifier: MS-PL
# CNA-DEMOS-ISSUE-150: the standalone Runtime facade was never implemented.
# Keep its public header retired; Game and the platform services own runtime lifetime.
# Restrict the symbol check to the former facade methods: CNA::Runtime::Testing
# is an unrelated namespace used by the native tests.

if(NOT DEFINED CNA_RUNTIME_LIBRARY OR NOT DEFINED CNA_RUNTIME_NM OR
   NOT DEFINED CNA_RUNTIME_INCLUDE_DIRECTORY)
    message(FATAL_ERROR
        "CNA_RUNTIME_LIBRARY, CNA_RUNTIME_NM and CNA_RUNTIME_INCLUDE_DIRECTORY are required.")
endif()

if(EXISTS "${CNA_RUNTIME_INCLUDE_DIRECTORY}/CNA/Misc.hpp")
    message(FATAL_ERROR
        "The retired CNA/Misc.hpp facade is public again. CNA-DEMOS-ISSUE-150 removed its "
        "undefined Runtime methods and unused RuntimeOptions; do not restore a declaration-only API.")
endif()

if(NOT EXISTS "${CNA_RUNTIME_LIBRARY}")
    message(FATAL_ERROR "The runtime archive was not found: ${CNA_RUNTIME_LIBRARY}")
endif()

execute_process(
    COMMAND "${CNA_RUNTIME_NM}" -C --defined-only "${CNA_RUNTIME_LIBRARY}"
    RESULT_VARIABLE _cna_runtime_nm_result
    OUTPUT_VARIABLE _cna_runtime_nm_output
    ERROR_VARIABLE _cna_runtime_nm_error
)
if(NOT _cna_runtime_nm_result EQUAL 0)
    message(FATAL_ERROR "Runtime symbol inspection failed: ${_cna_runtime_nm_error}")
endif()

string(REPLACE "\n" ";" _cna_runtime_nm_lines "${_cna_runtime_nm_output}")
foreach(_cna_runtime_nm_line IN LISTS _cna_runtime_nm_lines)
    if(_cna_runtime_nm_line MATCHES
       "CNA::Runtime::(Initialize|Shutdown|IsGraphicsEnabled|IsAudioEnabled|IsInputEnabled)\\(")
        message(FATAL_ERROR
            "A retired Runtime facade method is defined (${_cna_runtime_nm_line}). "
            "A new runtime API requires an explicit public contract and C binding review.")
    endif()
endforeach()
