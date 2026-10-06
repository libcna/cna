# SPDX-License-Identifier: MS-PL
#
# Platform retirement contract: run the production platform-selection file with every former
# selector, on a Windows and a non-Windows host, and prove each reaches its refusal.
#
#   SDL2                 retired 2026-10-04 -- simply unknown, on every host.
#   WIN32, X11, WAYLAND  CNA's direct backends, retired 2026-10-06 -- refused by name, pointing at
#                        SDL3, which is how those window systems are reached now. Never an alias.

if(NOT DEFINED CNA_PLATFORM_SELECTION_FILE)
    message(FATAL_ERROR "the retired platform-selection test requires the selection file")
endif()

foreach(_cna_retired_host IN ITEMS FALSE TRUE)
    foreach(_cna_retired IN ITEMS SDL2 WIN32 X11 WAYLAND)
        if(_cna_retired STREQUAL "SDL2")
            set(_cna_retired_expected "CNA_PLATFORM=SDL2 is not a known platform")
        else()
            set(_cna_retired_expected "CNA_PLATFORM=${_cna_retired} was removed on 2026-10-06")
        endif()

        execute_process(
            COMMAND "${CMAKE_COMMAND}"
                -DWIN32=${_cna_retired_host}
                -DCNA_PLATFORM=${_cna_retired}
                -P "${CNA_PLATFORM_SELECTION_FILE}"
            RESULT_VARIABLE _cna_retired_result
            OUTPUT_VARIABLE _cna_retired_stdout
            ERROR_VARIABLE _cna_retired_stderr
        )
        set(_cna_retired_output "${_cna_retired_stdout}${_cna_retired_stderr}")

        if(_cna_retired_result EQUAL 0)
            message(FATAL_ERROR
                "CNA_PLATFORM=${_cna_retired} (WIN32=${_cna_retired_host}) unexpectedly succeeded")
        endif()

        string(FIND "${_cna_retired_output}" "${_cna_retired_expected}" _cna_retired_at)
        if(_cna_retired_at EQUAL -1)
            message(FATAL_ERROR
                "CNA_PLATFORM=${_cna_retired} (WIN32=${_cna_retired_host}) did not produce "
                "the retired-value refusal \"${_cna_retired_expected}\":\n"
                "${_cna_retired_output}")
        endif()

        if(NOT _cna_retired STREQUAL "SDL2")
            string(FIND "${_cna_retired_output}" "-DCNA_PLATFORM=SDL3" _cna_retired_hint_at)
            if(_cna_retired_hint_at EQUAL -1)
                message(FATAL_ERROR
                    "CNA_PLATFORM=${_cna_retired} was refused without naming SDL3 as the "
                    "replacement:\n${_cna_retired_output}")
            endif()
        endif()
    endforeach()
endforeach()
