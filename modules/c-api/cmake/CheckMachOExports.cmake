# SPDX-License-Identifier: MS-PL
#
# plans/plan_apple_m4.md AM4-209: CheckElfExports.cmake's question asked of a Mach-O dylib. `nm -D`
# has no meaning there ("File format has no dynamic symbol table"); `nm -gU` lists the defined
# external symbols, which is the export trie's content, with Mach-O's leading underscore.

if(NOT DEFINED CNA_C_API_LIBRARY OR NOT DEFINED CNA_C_API_NM)
    message(FATAL_ERROR "CNA_C_API_LIBRARY and CNA_C_API_NM are required.")
endif()

execute_process(
    COMMAND "${CNA_C_API_NM}" -gU "${CNA_C_API_LIBRARY}"
    RESULT_VARIABLE _cna_c_api_nm_result
    OUTPUT_VARIABLE _cna_c_api_nm_output
    ERROR_VARIABLE _cna_c_api_nm_error
)
if(NOT _cna_c_api_nm_result EQUAL 0)
    message(FATAL_ERROR "Mach-O export inspection failed: ${_cna_c_api_nm_error}")
endif()

string(REPLACE "\n" ";" _cna_c_api_nm_lines "${_cna_c_api_nm_output}")
set(_cna_c_api_export_count 0)
foreach(_cna_c_api_nm_line IN LISTS _cna_c_api_nm_lines)
    if(_cna_c_api_nm_line STREQUAL "")
        continue()
    endif()
    string(REGEX REPLACE ".*[ \t]([^ \t]+)$" "\\1"
        _cna_c_api_symbol "${_cna_c_api_nm_line}")
    if(NOT _cna_c_api_symbol MATCHES "^_cna_[a-z0-9_]+$")
        message(FATAL_ERROR "Unexpected dynamic C API export: ${_cna_c_api_symbol}")
    endif()
    math(EXPR _cna_c_api_export_count "${_cna_c_api_export_count} + 1")
endforeach()

if(_cna_c_api_export_count EQUAL 0)
    message(FATAL_ERROR "The C API library exports no cna_* functions.")
endif()
message(STATUS "The C API library exports ${_cna_c_api_export_count} cna_* functions and nothing else.")
