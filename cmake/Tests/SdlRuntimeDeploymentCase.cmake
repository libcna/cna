# SPDX-License-Identifier: MS-PL

foreach(_required IN ITEMS CNA_SDL_RUNTIME_MODULE CNA_SDL_RUNTIME_PROBE CNA_SDL_RUNTIME_WORKDIR)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "${_required} is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${CNA_SDL_RUNTIME_WORKDIR}")
set(_runtime_directory "${CNA_SDL_RUNTIME_WORKDIR}/runtime")
file(MAKE_DIRECTORY "${_runtime_directory}")
foreach(_dll IN ITEMS SDL3.dll SDL3_image.dll SDL3_mixer.dll)
    file(WRITE "${_runtime_directory}/${_dll}" "${_dll} deployment fixture\n")
endforeach()

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -S "${CNA_SDL_RUNTIME_PROBE}"
        -B "${CNA_SDL_RUNTIME_WORKDIR}/build"
        -DTHIRD_PARTY_SDL=${CNA_SDL_RUNTIME_MODULE}
        -DRUNTIME_DIRECTORY=${_runtime_directory}
    RESULT_VARIABLE _configure_result
    OUTPUT_VARIABLE _configure_output
    ERROR_VARIABLE _configure_error)
if(NOT _configure_result EQUAL 0)
    message(FATAL_ERROR
        "SDL runtime deployment configure failed (${_configure_result})\n"
        "stdout:\n${_configure_output}\n"
        "stderr:\n${_configure_error}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${CNA_SDL_RUNTIME_WORKDIR}/build"
        --target sdl_runtime_consumer --parallel 1
    RESULT_VARIABLE _build_result
    OUTPUT_VARIABLE _build_output
    ERROR_VARIABLE _build_error)
if(NOT _build_result EQUAL 0)
    message(FATAL_ERROR
        "SDL runtime deployment build failed (${_build_result})\n"
        "stdout:\n${_build_output}\n"
        "stderr:\n${_build_error}")
endif()

foreach(_dll IN ITEMS SDL3.dll SDL3_image.dll SDL3_mixer.dll)
    if(NOT EXISTS "${CNA_SDL_RUNTIME_WORKDIR}/build/${_dll}")
        message(FATAL_ERROR "SDL runtime helper did not deploy ${_dll}")
    endif()
endforeach()
