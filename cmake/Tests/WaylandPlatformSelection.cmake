# SPDX-License-Identifier: MS-PL
#
# plans/plan_wayland.md WAYLAND-0023: what `CNA_PLATFORM=WAYLAND` promises at configure time.
#
# Three things, each checked against the real cmake/PlatformWayland.cmake through a fixture project
# that includes it exactly as the build does:
#
#   * a machine with the Wayland development files offers WAYLAND;
#   * a machine without them and `CNA_PLATFORM=WAYLAND` fails LOUDLY, naming the missing piece and
#     the package to install -- never falling back to SDL3 or to X11 through Xwayland;
#   * asking for nothing selects SDL3 as it always did: adding this backend changed no default.
#
# "Without them" is pkg-config pointed at an empty directory (`PKG_CONFIG_LIBDIR`), which is how a
# machine with no Wayland development files looks to CMake.

foreach(_required IN ITEMS CNA_SOURCE_DIR CNA_WORK_DIR CNA_GENERATOR)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "WaylandPlatformSelection.cmake requires -D${_required}=...")
    endif()
endforeach()

file(REMOVE_RECURSE "${CNA_WORK_DIR}")
set(_fixture "${CNA_WORK_DIR}/fixture")
file(MAKE_DIRECTORY "${CNA_WORK_DIR}/empty-pkgconfig")

file(WRITE "${_fixture}/CMakeLists.txt" "
cmake_minimum_required(VERSION 3.23)
project(CnaWaylandSelection LANGUAGES C CXX)
include(\"${CNA_SOURCE_DIR}/cmake/PlatformWayland.cmake\")
cna_detect_wayland()
message(STATUS \"fixture: CNA_WAYLAND_AVAILABLE=[\${CNA_WAYLAND_AVAILABLE}]\")
message(STATUS \"fixture: reason=[\${CNA_WAYLAND_UNAVAILABLE_REASON}]\")
message(STATUS \"fixture: summary=[\${CNA_WAYLAND_SUMMARY}]\")
if(CNA_FIXTURE_REQUIRE AND NOT CNA_WAYLAND_AVAILABLE)
    # The message the real cmake/PlatformSelection.cmake produces, shortened to its substance.
    message(FATAL_ERROR \"CNA: CNA_PLATFORM=WAYLAND was requested but this machine cannot build \"
                        \"it.\\nReason: \${CNA_WAYLAND_UNAVAILABLE_REASON}\")
endif()
")

function(_configure name require pkgconfig out_result out_log)
    set(_command "${CMAKE_COMMAND}" -S "${_fixture}" -B "${CNA_WORK_DIR}/${name}" -G "${CNA_GENERATOR}"
        "-DCNA_FIXTURE_REQUIRE=${require}")
    if(pkgconfig)
        set(_command ${CMAKE_COMMAND} -E env "PKG_CONFIG_LIBDIR=${pkgconfig}" "PKG_CONFIG_PATH=" ${_command})
    endif()
    execute_process(COMMAND ${_command} RESULT_VARIABLE _result OUTPUT_VARIABLE _stdout ERROR_VARIABLE _stderr)
    set(${out_result} "${_result}" PARENT_SCOPE)
    set(${out_log} "${_stdout}${_stderr}" PARENT_SCOPE)
endfunction()

# 1. This machine, as the build sees it.
_configure(present ON "" _result _log)
if(NOT _result EQUAL 0)
    if(_log MATCHES "CNA_PLATFORM=WAYLAND was requested")
        message(STATUS "this machine has no Wayland development files; the refusal names them")
        if(NOT _log MATCHES "(libwayland-dev|wayland-devel|wayland-protocols|libxkbcommon-dev|libwayland-bin|pkg-config)")
            message(FATAL_ERROR "The refusal named no package to install:\n${_log}")
        endif()
        file(REMOVE_RECURSE "${CNA_WORK_DIR}")
        return()
    endif()
    message(FATAL_ERROR "The fixture failed for another reason:\n${_log}")
endif()
if(NOT _log MATCHES "CNA_WAYLAND_AVAILABLE=\\[(1|TRUE|ON)\\]")
    message(FATAL_ERROR "Wayland is not available here, yet the fixture configured:\n${_log}")
endif()
if(NOT _log MATCHES "summary=\\[wayland-client [0-9]")
    message(FATAL_ERROR "The summary does not name the libraries found:\n${_log}")
endif()

# 2. The same machine with pkg-config finding nothing: a loud refusal naming the package.
_configure(absent ON "${CNA_WORK_DIR}/empty-pkgconfig" _result _log)
if(_result EQUAL 0)
    message(FATAL_ERROR
        "With no Wayland development files, CNA_PLATFORM=WAYLAND configured successfully; it must "
        "fail loudly instead:\n${_log}")
endif()
if(NOT _log MATCHES "CNA_PLATFORM=WAYLAND was requested")
    message(FATAL_ERROR "The failure is not the selection's own diagnostic:\n${_log}")
endif()
if(NOT _log MATCHES "(libwayland-dev|wayland-devel|wayland-protocols|libxkbcommon-dev|libwayland-bin)")
    message(FATAL_ERROR "The refusal named no package to install:\n${_log}")
endif()
if(_log MATCHES "SDL3 will be used|falling back")
    message(FATAL_ERROR "The refusal offered a fall back instead of failing:\n${_log}")
endif()

# 3. Without pkg-config at all: the same, naming pkg-config.
_configure(absent-detect OFF "${CNA_WORK_DIR}/empty-pkgconfig" _result _log)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "Detection itself must not fail when nothing is found:\n${_log}")
endif()
if(NOT _log MATCHES "CNA_WAYLAND_AVAILABLE=\\[(0|FALSE|OFF)?\\]")
    message(FATAL_ERROR "Wayland was reported available with no development files:\n${_log}")
endif()
if(NOT _log MATCHES "reason=\\[[^]]*(wayland-client|wayland-protocols|xkbcommon|wayland-scanner|pkg-config)")
    message(FATAL_ERROR "The reason does not name what is missing:\n${_log}")
endif()

# 4. The default is what it always was. Read from the selection file itself: configuring the whole
# project twice to check a default would cost minutes and prove the same one line.
file(READ "${CNA_SOURCE_DIR}/cmake/PlatformSelection.cmake" _selection)
if(NOT _selection MATCHES "set\\(CNA_PLATFORM \"SDL3\" CACHE STRING")
    message(FATAL_ERROR "The default platform is no longer SDL3; adding a backend must change no default.")
endif()

file(REMOVE_RECURSE "${CNA_WORK_DIR}")
message(STATUS "CNA_PLATFORM=WAYLAND is offered where it can be built, refuses loudly where it cannot, "
               "and the default selection is still SDL3")
