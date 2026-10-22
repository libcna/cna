# SPDX-License-Identifier: MS-PL
execute_process(COMMAND "${READELF}" -d "${PROBE}"
    RESULT_VARIABLE _result OUTPUT_VARIABLE _dynamic ERROR_VARIABLE _error)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "Cannot inspect ${PROBE}: ${_error}")
endif()
if(_dynamic MATCHES "NEEDED[^\n]*(libcna|libSDL|libwgpu|libav|libenet)")
    message(FATAL_ERROR "Math acquired a runtime/renderer dependency: ${_dynamic}")
endif()
if(STATIC_CPP AND _dynamic MATCHES "NEEDED[^\n]*lib(stdc\\+\\+|c\\+\\+|gcc_s)")
    message(FATAL_ERROR "The package overrode the consumer's static C++ runtime: ${_dynamic}")
endif()
