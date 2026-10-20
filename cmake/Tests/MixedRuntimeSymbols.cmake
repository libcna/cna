# SPDX-License-Identifier: MS-PL
execute_process(COMMAND "${CNA_NM}" -C -g --defined-only "${CNA_PROBE}"
    RESULT_VARIABLE _result OUTPUT_VARIABLE _symbols ERROR_VARIABLE _error)
if(NOT _result EQUAL 0 OR _symbols STREQUAL "")
    message(FATAL_ERROR "Cannot inspect mixed-runtime consumer: ${_result}: ${_error}")
endif()
string(REGEX MATCHALL "[0-9a-fA-F]+ T Microsoft::Xna::Framework::Graphics::[^\n]+"
    _duplicates "${_symbols}")
list(LENGTH _duplicates _count)
if(_count)
    message(FATAL_ERROR "Mixed-runtime consumer defines ${_count} strong Graphics symbols; expected zero")
endif()
message(STATUS "Mixed-runtime consumer defines zero strong Graphics symbols")
if(CNA_CHECK_NEEDED)
    execute_process(COMMAND "${CNA_READELF}" -d "${CNA_PROBE}"
        RESULT_VARIABLE _result OUTPUT_VARIABLE _dynamic ERROR_VARIABLE _error)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "Cannot inspect dynamic dependencies: ${_error}")
    endif()
    if(NOT _dynamic MATCHES "\\(NEEDED\\)[^\n]*\\[libcna\\.so\\]"
       OR _dynamic MATCHES "\\(NEEDED\\)[^\n]*cna_mixed_runtime_unused")
        message(FATAL_ERROR "Expected CNA retained and unrelated unused DSO omitted: ${_dynamic}")
    endif()
    message(STATUS "CNA is retained; --as-needed is restored for unrelated libraries")
endif()
