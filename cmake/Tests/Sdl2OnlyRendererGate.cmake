# SPDX-License-Identifier: MS-PL

if(NOT DEFINED CNA_SDL2_ONLY_GUARD_FILE)
    message(FATAL_ERROR "SDL2-only renderer gate test requires CNA_SDL2_ONLY_GUARD_FILE")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -DCNA_PLATFORM=SDL2
        -DCNA_AUDIO_PLATFORM=SDL2
        -DCNA_GRAPHICS_RENDERER=SDL_RENDERER
        -P "${CNA_SDL2_ONLY_GUARD_FILE}"
    RESULT_VARIABLE _cna_direct_sdl3_result
    OUTPUT_VARIABLE _cna_direct_sdl3_stdout
    ERROR_VARIABLE _cna_direct_sdl3_stderr)
if(_cna_direct_sdl3_result EQUAL 0)
    message(FATAL_ERROR "SDL2-only guard accepted SDL_RENDERER's direct SDL3 dependency")
endif()
string(FIND "${_cna_direct_sdl3_stdout}${_cna_direct_sdl3_stderr}"
    "without a direct SDL3 dependency" _cna_guard_text_at)
if(_cna_guard_text_at EQUAL -1)
    message(FATAL_ERROR "SDL2-only guard rejected SDL_RENDERER for an unexpected reason")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -DCNA_PLATFORM=SDL2
        -DCNA_AUDIO_PLATFORM=SDL2
        -DCNA_GRAPHICS_RENDERER=OPENGLES3
        -P "${CNA_SDL2_ONLY_GUARD_FILE}"
    RESULT_VARIABLE _cna_independent_result)
if(NOT _cna_independent_result EQUAL 0)
    message(FATAL_ERROR "SDL2-only guard rejected the SDL-independent OPENGLES3 renderer")
endif()
