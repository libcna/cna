# SPDX-License-Identifier: MS-PL
# Run with the actual CMake under test. No simulated CMAKE_VERSION and no removal
# of caller paths: each scenario has its own small build tree and is reconfigurable.
foreach(_required IN ITEMS CNA_SOURCE_DIR CNA_SHARED_RUNTIME_WORKDIR CNA_SHARED_RUNTIME_COMPILER)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "${_required} is required")
    endif()
endforeach()
find_program(_ccache ccache REQUIRED)
foreach(_mode IN ITEMS DEFAULT ON OFF DIRECT)
    set(_build "${CNA_SHARED_RUNTIME_WORKDIR}/${_mode}")
    set(_options)
    if(_mode STREQUAL "DIRECT")
        list(APPEND _options -DCNA_SHARED_LIBRARY=OFF -DPROBE_DIRECT_HELPER=ON)
    elseif(NOT _mode STREQUAL "DEFAULT")
        list(APPEND _options -DCNA_SHARED_LIBRARY=${_mode})
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}"
        -S "${CNA_SOURCE_DIR}/cmake/Tests/SharedRuntimeProbe" -B "${_build}"
        -G "Unix Makefiles" -DCNA_SOURCE_DIR=${CNA_SOURCE_DIR}
        -UCNA_SHARED_LIBRARY -UPROBE_DIRECT_HELPER
        -DCMAKE_CXX_COMPILER=${CNA_SHARED_RUNTIME_COMPILER}
        -DCMAKE_CXX_COMPILER_LAUNCHER=${_ccache} ${_options}
        RESULT_VARIABLE _result OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
    file(WRITE "${CNA_SHARED_RUNTIME_WORKDIR}/${_mode}-configure.log" "${_out}\n${_err}")
    string(REGEX REPLACE "[ \t\r\n]+" " " _diagnostic "${_out}${_err}")
    if(CMAKE_VERSION VERSION_LESS 3.30 AND (_mode STREQUAL "ON" OR _mode STREQUAL "DIRECT"))
        if(_result EQUAL 0 OR NOT _diagnostic MATCHES "CMake >= 3\\.30"
           OR NOT _diagnostic MATCHES "CNA_SHARED_LIBRARY=OFF")
            message(FATAL_ERROR "${_mode}: expected actionable old-CMake refusal\n${_out}\n${_err}")
        endif()
        message(STATUS "${CMAKE_VERSION}/${_mode}: expected configure refusal verified")
        continue()
    endif()
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "${_mode}: configuration failed\n${_out}\n${_err}")
    endif()
    file(READ "${_build}/selected-mode.txt" _selected)
    string(STRIP "${_selected}" _selected)
    set(_expected ON)
    if(_mode STREQUAL "OFF" OR _mode STREQUAL "DIRECT" OR CMAKE_VERSION VERSION_LESS 3.30)
        set(_expected OFF)
    endif()
    if(NOT _selected STREQUAL _expected)
        message(FATAL_ERROR "${_mode}: selected ${_selected}, expected ${_expected}")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" --build "${_build}" --parallel 1
        RESULT_VARIABLE _result OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
    file(WRITE "${CNA_SHARED_RUNTIME_WORKDIR}/${_mode}-build.log" "${_out}\n${_err}")
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "${_mode}: build failed\n${_out}\n${_err}")
    endif()
    execute_process(COMMAND "${_build}/shared_runtime_probe" RESULT_VARIABLE _result)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "${_mode}: consumer failed (${_result})")
    endif()
    message(STATUS "${CMAKE_VERSION}/${_mode}: mode ${_selected}, build and consumer pass")
endforeach()
