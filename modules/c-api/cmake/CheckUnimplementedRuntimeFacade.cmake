# SPDX-License-Identifier: MS-PL
#
# `CNA::Runtime` is declared in `modules/runtime/include/CNA/Misc.hpp` and defined nowhere: all five
# of its methods would fail to link if anything called them, and nothing does. `CBIND-037E5` records
# those rows as not-applicable for exactly that reason, and this check is what keeps that record
# honest -- the day somebody implements the facade, the symbols appear, this test fails, and the
# coverage rows have to be revisited rather than quietly staying wrong.

if(NOT DEFINED CNA_RUNTIME_LIBRARY OR NOT DEFINED CNA_RUNTIME_NM)
    message(FATAL_ERROR "CNA_RUNTIME_LIBRARY and CNA_RUNTIME_NM are required.")
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
    if(_cna_runtime_nm_line MATCHES "CNA::Runtime::")
        message(FATAL_ERROR
            "CNA::Runtime now defines symbols (${_cna_runtime_nm_line}). The C API records its "
            "rows as not-applicable because the facade is unimplemented; revisit "
            "tools/c-api/coverage_mappings.json rule 'cna-runtime-facade' before removing this "
            "check.")
    endif()
endforeach()
