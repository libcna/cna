# SPDX-License-Identifier: MS-PL
# Mold can localize every definition in a small executable whose framework
# calls are all satisfied by the DSO. Inspect local symbols too: an empty
# global-only table is valid, and a hidden duplicate must not evade this audit.
cmake_minimum_required(VERSION 3.20)
execute_process(COMMAND "${CNA_NM}" -C --defined-only "${CNA_PROBE}"
    RESULT_VARIABLE _result OUTPUT_VARIABLE _symbols ERROR_VARIABLE _error)
if(NOT _result EQUAL 0 OR _symbols STREQUAL "")
    message(FATAL_ERROR "Cannot inspect mixed-runtime consumer: ${_result}: ${_error}")
endif()
# A duplicate is a Graphics function the runtime (CNA_RUNTIME) itself defines strongly. Inline
# functions and template instantiations -- an implicit move constructor, a std::vector of vertices
# -- are emitted again by every consumer that uses them; ld.bfd leaves those copies weak (W), mold
# localizes them (t), so their binding cannot tell them from a second runtime. Mangled names keep
# std:: instantiations whose return type is a Graphics type out of the Graphics namespace.
set(_graphics "_ZN[KRO]*9Microsoft3Xna9Framework8Graphics[^\n]*")
execute_process(COMMAND "${CNA_NM}" -D --defined-only "${CNA_RUNTIME}"
    RESULT_VARIABLE _result OUTPUT_VARIABLE _runtime ERROR_VARIABLE _error)
string(REGEX MATCHALL "[0-9a-fA-F]+ T ${_graphics}" _runtime_lines "${_runtime}")
if(NOT _result EQUAL 0 OR NOT _runtime_lines)
    message(FATAL_ERROR "Cannot read the runtime's Graphics definitions: ${_result}: ${_error}")
endif()
list(TRANSFORM _runtime_lines REPLACE "^[0-9a-fA-F]+ T " "")
execute_process(COMMAND "${CNA_NM}" --defined-only "${CNA_PROBE}"
    RESULT_VARIABLE _result OUTPUT_VARIABLE _mangled ERROR_VARIABLE _error)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "Cannot inspect mixed-runtime consumer: ${_result}: ${_error}")
endif()
string(REGEX MATCHALL "[0-9a-fA-F]+ [Tt] ${_graphics}" _probe_lines "${_mangled}")
list(TRANSFORM _probe_lines REPLACE "^[0-9a-fA-F]+ [Tt] " "")
set(_duplicates)
foreach(_name IN LISTS _probe_lines)
    if(_name IN_LIST _runtime_lines)
        list(APPEND _duplicates "${_name}")
    endif()
endforeach()
if(_duplicates)
    list(REMOVE_DUPLICATES _duplicates)
    list(LENGTH _duplicates _count)
    list(JOIN _duplicates "\n  " _listing)
    message(FATAL_ERROR "Mixed-runtime consumer defines ${_count} Graphics functions the runtime "
        "defines; expected zero:\n  ${_listing}")
endif()
message(STATUS "Mixed-runtime consumer defines none of the runtime's Graphics functions")
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
