# Native WebGPU smoke wrapper. CTest itself cannot know whether the executing
# host exposes a desktop display and GPU, so an unavailable desktop is a skip
# while an application/runtime failure on an available desktop remains a fail.

if(NOT DEFINED CNA_WEBGPU_DEMO OR NOT EXISTS "${CNA_WEBGPU_DEMO}")
    message(FATAL_ERROR "CNA WebGPU smoke test executable was not built: '${CNA_WEBGPU_DEMO}'")
endif()

if(NOT DEFINED ENV{WAYLAND_DISPLAY} AND NOT DEFINED ENV{DISPLAY})
    message("[SKIP] CNA WebGPU native smoke: no WAYLAND_DISPLAY or DISPLAY is available")
    return()
endif()

get_filename_component(_demo_directory "${CNA_WEBGPU_DEMO}" DIRECTORY)
execute_process(
    COMMAND "${CNA_WEBGPU_DEMO}" --smoke 120
    WORKING_DIRECTORY "${_demo_directory}"
    TIMEOUT 60
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _stdout
    ERROR_VARIABLE _stderr)

if(_result EQUAL 0)
    message("${_stdout}${_stderr}")
    return()
endif()

set(_combined_output "${_stdout}${_stderr}")
if(_combined_output MATCHES "SDL_InitSubSystem.*failed|SDL_CreateWindow.*failed|unsupported SDL Linux video driver|adapter request failed|device request failed")
    message("[SKIP] CNA WebGPU native smoke: desktop GPU/display is unavailable\n${_combined_output}")
    return()
endif()

message(FATAL_ERROR "CNA WebGPU native smoke failed (exit ${_result}):\n${_combined_output}")
