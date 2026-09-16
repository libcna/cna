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

# plans/plan_wayland.md WAYLAND-0128: the two selections that would put SDL2 and SDL3 in one
# process are refused, each naming an audio platform that works instead. Without this, the first
# pair built a binary linking both libraries and crashed on its first window, and the second
# failed with CMake's own missing-target error.
foreach(_pair "SDL2|SDL3" "SDL3|SDL2")
    string(REPLACE "|" ";" _parts "${_pair}")
    list(GET _parts 0 _platform)
    list(GET _parts 1 _audio)
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            -DCNA_PLATFORM=${_platform}
            -DCNA_AUDIO_PLATFORM=${_audio}
            -DCNA_GRAPHICS_RENDERER=HEADLESS
            -P "${CNA_SDL2_ONLY_GUARD_FILE}"
        RESULT_VARIABLE _cna_mixed_result
        OUTPUT_VARIABLE _cna_mixed_stdout
        ERROR_VARIABLE _cna_mixed_stderr)
    if(_cna_mixed_result EQUAL 0)
        message(FATAL_ERROR
            "The guard accepted CNA_PLATFORM=${_platform} with CNA_AUDIO_PLATFORM=${_audio}, "
            "which puts two SDL major versions in one process")
    endif()
    set(_cna_mixed_log "${_cna_mixed_stdout}${_cna_mixed_stderr}")
    # CMake wraps a FATAL_ERROR's text, so match a phrase short enough to survive the wrapping.
    string(FIND "${_cna_mixed_log}" "SDL2 and SDL3" _cna_mixed_text_at)
    if(_cna_mixed_text_at EQUAL -1)
        message(FATAL_ERROR
            "CNA_PLATFORM=${_platform} with CNA_AUDIO_PLATFORM=${_audio} was rejected for an "
            "unexpected reason:\n${_cna_mixed_log}")
    endif()
    # The refusal must name a way out, not just say no.
    string(FIND "${_cna_mixed_log}" "CNA_AUDIO_PLATFORM=" _cna_mixed_fix_at)
    if(_cna_mixed_fix_at EQUAL -1)
        message(FATAL_ERROR "The refusal named no audio platform to use instead:\n${_cna_mixed_log}")
    endif()
endforeach()

# Every combination that can actually work still configures: one SDL, or none.
foreach(_pair "SDL2|SDL2" "SDL3|SDL3" "SDL2|NULL" "SDL3|ALSA" "X11|SDL3" "WAYLAND|ALSA" "HEADLESS|NULL")
    string(REPLACE "|" ";" _parts "${_pair}")
    list(GET _parts 0 _platform)
    list(GET _parts 1 _audio)
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            -DCNA_PLATFORM=${_platform}
            -DCNA_AUDIO_PLATFORM=${_audio}
            -DCNA_GRAPHICS_RENDERER=HEADLESS
            -P "${CNA_SDL2_ONLY_GUARD_FILE}"
        RESULT_VARIABLE _cna_single_result
        OUTPUT_VARIABLE _cna_single_stdout
        ERROR_VARIABLE _cna_single_stderr)
    if(NOT _cna_single_result EQUAL 0)
        message(FATAL_ERROR
            "The guard rejected CNA_PLATFORM=${_platform} with CNA_AUDIO_PLATFORM=${_audio}, "
            "which needs at most one SDL:\n${_cna_single_stdout}${_cna_single_stderr}")
    endif()
endforeach()
