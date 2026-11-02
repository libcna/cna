# SPDX-License-Identifier: MS-PL
# MTH-BUILD-001: exercise the production policy using a tiny native ELF runtime.
cmake_minimum_required(VERSION 3.30)
foreach(_required IN ITEMS CNA_SOURCE_DIR CNA_WORK_DIR)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "SharedRuntimeIncrementalCase requires -D${_required}=...")
    endif()
endforeach()
if(NOT DEFINED CNA_GENERATOR)
    set(CNA_GENERATOR Ninja)
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_LIST_DIR}/SharedRuntimeIncremental/run.py"
    --cmake "${CMAKE_COMMAND}" --source "${CNA_SOURCE_DIR}" --work "${CNA_WORK_DIR}"
    --generator "${CNA_GENERATOR}" ${CNA_FIXTURE_ARGS}
    RESULT_VARIABLE _result)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "SharedRuntimeIncrementalCase failed (${_result}); see ${CNA_WORK_DIR}")
endif()
