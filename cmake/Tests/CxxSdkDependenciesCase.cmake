# SPDX-License-Identifier: MS-PL
#
# 2026-10-26 dependency audit, finding C-02: the C++ SDK bundles FNA3D only when something in it
# links FNA3D. A compiled-Effect renderer needs MojoShader alone, and building FNA3D for its SDK
# needs SDL -- which an SDL-free configuration does not have.
#
#   cmake -DCNA_SOURCE_DIR=<repo> -DCNA_WORK_DIR=<dir> [-DCNA_GENERATOR=<g>] \
#         -P cmake/Tests/CxxSdkDependenciesCase.cmake

cmake_minimum_required(VERSION 3.20)

foreach(_required IN ITEMS CNA_SOURCE_DIR CNA_WORK_DIR)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "CxxSdkDependenciesCase.cmake requires -D${_required}=...")
    endif()
endforeach()
file(REMOVE_RECURSE "${CNA_WORK_DIR}")
set(_generator "")
if(DEFINED CNA_GENERATOR)
    set(_generator -G "${CNA_GENERATOR}")
endif()

set(_failures 0)
foreach(_case IN ITEMS "compiled-effects|mojoshader" "fna3d-renderer|FNA3D;mojoshader" "fused|FNA3D")
    string(REPLACE "|" ";" _fields "${_case}")
    list(GET _fields 0 _shape)
    list(SUBLIST _fields 1 -1 _want)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${CNA_SOURCE_DIR}/cmake/Tests/CxxSdkDependenciesProbe"
            -B "${CNA_WORK_DIR}/${_shape}" ${_generator}
            "-DCNA_SOURCE_DIR=${CNA_SOURCE_DIR}" "-DFIXTURE_SHAPE=${_shape}"
        RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
    if(NOT _rc EQUAL 0)
        message(SEND_ERROR "CxxSdkDependenciesCase: ${_shape}: configure failed:\n${_out}${_err}")
        math(EXPR _failures "${_failures} + 1")
        continue()
    endif()
    file(READ "${CNA_WORK_DIR}/${_shape}/bundled.txt" _bundled)
    if(NOT _bundled STREQUAL "${_want}")
        message(SEND_ERROR "CxxSdkDependenciesCase: ${_shape}: bundled [${_bundled}], expected [${_want}]")
        math(EXPR _failures "${_failures} + 1")
    else()
        message(STATUS "ok: ${_shape} bundles ${_bundled}")
    endif()
endforeach()

if(_failures GREATER 0)
    message(FATAL_ERROR "CxxSdkDependenciesCase: ${_failures} case(s) failed")
endif()
file(REMOVE_RECURSE "${CNA_WORK_DIR}")
message(STATUS "CxxSdkDependenciesCase: all cases passed")
