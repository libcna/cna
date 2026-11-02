# SPDX-License-Identifier: MS-PL
# MTH-BUILD-002 / BUILD-R1: real producer locking, publication and recovery with tiny fake SDLs.
# cmake -DCNA_SOURCE_DIR=<repo> -DCNA_WORK_DIR=<work> -P <this file>
cmake_minimum_required(VERSION 3.20)

foreach(_required IN ITEMS CNA_SOURCE_DIR CNA_WORK_DIR)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "SdlPrebuiltPublicationCase requires -D${_required}=...")
    endif()
endforeach()
if(NOT DEFINED CNA_GENERATOR)
    set(CNA_GENERATOR "Ninja")
endif()

if(DEFINED CNA_PRODUCER)
    # The watch helper needs a real directory scope, unavailable in cmake -P.
    # Each background script therefore configures its own tiny parent project.
    execute_process(COMMAND "${CMAKE_COMMAND}" -G "${CNA_GENERATOR}"
        -S "${CNA_WORK_DIR}/source" -B "${CNA_WORK_DIR}/${CNA_PRODUCER}-build"
        "-DCNA_SOURCE_DIR=${CNA_SOURCE_DIR}" "-DCNA_WORK_DIR=${CNA_WORK_DIR}"
        "-DCNA_GENERATOR=${CNA_GENERATOR}" "-DCNA_PRODUCER=${CNA_PRODUCER}"
        RESULT_VARIABLE _result)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "SDL fixture producer ${CNA_PRODUCER} failed (${_result})")
    endif()
    return()
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/SdlPrebuiltPublication/run.py"
        --cmake "${CMAKE_COMMAND}" --source "${CNA_SOURCE_DIR}" --work "${CNA_WORK_DIR}"
        --generator "${CNA_GENERATOR}"
    RESULT_VARIABLE _result)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "SdlPrebuiltPublicationCase failed (${_result}); see ${CNA_WORK_DIR}")
endif()
