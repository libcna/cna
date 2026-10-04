# SPDX-License-Identifier: MS-PL
#
# SDL2 retirement contract: run the production platform-selection file with the former selector
# and prove that it reaches the unknown-value refusal. WIN32 is set only to make native X11 and
# Wayland dependency discovery irrelevant in script mode; SDL2 must be unknown on every host.

if(NOT DEFINED CNA_PLATFORM_SELECTION_FILE)
    message(FATAL_ERROR "retired SDL2 platform-selection test requires the selection file")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -DWIN32=TRUE
        -DCNA_PLATFORM=SDL2
        -P "${CNA_PLATFORM_SELECTION_FILE}"
    RESULT_VARIABLE _cna_sdl2_selection_result
    OUTPUT_VARIABLE _cna_sdl2_selection_stdout
    ERROR_VARIABLE _cna_sdl2_selection_stderr
)
set(_cna_sdl2_selection_output
    "${_cna_sdl2_selection_stdout}${_cna_sdl2_selection_stderr}")

if(_cna_sdl2_selection_result EQUAL 0)
    message(FATAL_ERROR "CNA_PLATFORM=SDL2 unexpectedly succeeded")
endif()

string(FIND "${_cna_sdl2_selection_output}"
    "CNA_PLATFORM=SDL2 is not a known platform" _cna_sdl2_unknown_at)
if(_cna_sdl2_unknown_at EQUAL -1)
    message(FATAL_ERROR
        "CNA_PLATFORM=SDL2 did not produce the retired-value refusal:\n"
        "${_cna_sdl2_selection_output}")
endif()
