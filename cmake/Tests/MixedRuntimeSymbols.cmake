# SPDX-License-Identifier: MS-PL
# Mold can localize every definition in a small executable whose framework
# calls are all satisfied by the DSO. Inspect local symbols too: an empty
# global-only table is valid, and a hidden duplicate must not evade this audit.
execute_process(COMMAND "${CNA_NM}" -C --defined-only "${CNA_PROBE}"
    RESULT_VARIABLE _result OUTPUT_VARIABLE _symbols ERROR_VARIABLE _error)
if(NOT _result EQUAL 0 OR _symbols STREQUAL "")
    message(FATAL_ERROR "Cannot inspect mixed-runtime consumer: ${_result}: ${_error}")
endif()
string(REGEX MATCHALL "[0-9a-fA-F]+ [Tt] Microsoft::Xna::Framework::Graphics::[^\n]+"
    _duplicates "${_symbols}")
list(LENGTH _duplicates _count)
if(_count)
    message(FATAL_ERROR "Mixed-runtime consumer defines ${_count} strong Graphics symbols; expected zero")
endif()
message(STATUS "Mixed-runtime consumer defines zero strong Graphics symbols")
if(CNA_CHECK_ENGINE)
    string(REGEX MATCHALL "[0-9a-fA-F]+ [Tt] Microsoft::Xna::Framework::(Input::|Vector3::)[^\n]+"
        _engine_duplicates "${_symbols}")
    if(_engine_duplicates)
        message(FATAL_ERROR "Module consumer duplicates Input/Math runtime definitions: ${_engine_duplicates}")
    endif()
    message(STATUS "Engine-style consumer defines zero strong Input/Vector3 symbols")
endif()
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
