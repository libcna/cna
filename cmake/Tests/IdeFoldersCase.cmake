# SPDX-License-Identifier: MS-PL
foreach(_required IN ITEMS CNA_IDE_FOLDER_MODULE CNA_IDE_FOLDER_PROBE CNA_IDE_FOLDER_WORKDIR)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "${_required} is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${CNA_IDE_FOLDER_WORKDIR}")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -S "${CNA_IDE_FOLDER_PROBE}"
        -B "${CNA_IDE_FOLDER_WORKDIR}"
        "-DCNA_IDE_FOLDER_MODULE=${CNA_IDE_FOLDER_MODULE}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _error)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR
        "IDE folder contract configure failed (${_result})\n"
        "stdout:\n${_output}\nstderr:\n${_error}")
endif()
