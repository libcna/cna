# SPDX-License-Identifier: MS-PL

foreach(_required IN ITEMS CNA_CROSS_JSON_MODULE CNA_CROSS_JSON_PROBE CNA_CROSS_JSON_WORKDIR)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "${_required} is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${CNA_CROSS_JSON_WORKDIR}")
file(MAKE_DIRECTORY "${CNA_CROSS_JSON_WORKDIR}/unrelated-sysroot")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -S "${CNA_CROSS_JSON_PROBE}"
        -B "${CNA_CROSS_JSON_WORKDIR}/build"
        -DHEADER_ONLY_JSON=${CNA_CROSS_JSON_MODULE}
        -DUNRELATED_SYSROOT=${CNA_CROSS_JSON_WORKDIR}/unrelated-sysroot
    RESULT_VARIABLE _configure_result
    OUTPUT_VARIABLE _configure_output
    ERROR_VARIABLE _configure_error)
if(NOT _configure_result EQUAL 0)
    message(FATAL_ERROR
        "Cross JSON header probe failed (${_configure_result})\n"
        "stdout:\n${_configure_output}\n"
        "stderr:\n${_configure_error}")
endif()
