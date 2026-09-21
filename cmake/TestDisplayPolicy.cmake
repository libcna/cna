# SPDX-License-Identifier: MS-PL
#
# plans/plan_gpu_test_isolation.md GTI-0001: which X display the GPU/window-creating tests use.
#
# Nearly a thousand renderer tests are registered with
#     ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}"
# and ctest applies that per test, overriding whatever DISPLAY the caller exported. The default used
# to be ":0" -- on a developer machine, the live desktop -- so a plain `ctest` opened hundreds of
# windows on the owner's screen (plans/plan_vulkan_parity.md VKPAR-0015 records it happening).
#
# The policy now:
#   * CNA_TEST_DISPLAY defaults to EMPTY. A test then carries no DISPLAY at all and inherits the
#     caller's, so where the tests appear is decided by how they are launched, not by the build tree.
#     The standard automated launcher is tools/platform/run_gpu_tests_private.sh: a private headless
#     compositor with a private rootful Xwayland on a free display number, DRI3 and the real GPU.
#   * The live desktop is opt-in. A value naming display :0 is honoured only together with
#     CNA_TEST_ALLOW_LIVE_DISPLAY=ON; without it, it is reset to empty. That also migrates every
#     existing build tree, whose cache still holds the old ":0" default.
#   * Any other value (e.g. ":99", an Xvfb) is honoured as before.

include_guard(GLOBAL)

set(CNA_TEST_DISPLAY "" CACHE STRING
    "DISPLAY forced on GPU/window-creating tests; empty (default) = inherit the caller's. :0 also needs CNA_TEST_ALLOW_LIVE_DISPLAY=ON")
option(CNA_TEST_ALLOW_LIVE_DISPLAY
    "Allow CNA_TEST_DISPLAY to name the live desktop display (:0): tests will open windows on it" OFF)

if(CNA_TEST_DISPLAY MATCHES "^:0(\\.[0-9]+)?$" AND NOT CNA_TEST_ALLOW_LIVE_DISPLAY)
    message(STATUS "CNA: CNA_TEST_DISPLAY=${CNA_TEST_DISPLAY} names the live desktop and is reset to empty; "
                   "tests now inherit the caller's DISPLAY. To really run them on the desktop, configure "
                   "with -DCNA_TEST_DISPLAY=${CNA_TEST_DISPLAY} -DCNA_TEST_ALLOW_LIVE_DISPLAY=ON.")
    set_property(CACHE CNA_TEST_DISPLAY PROPERTY VALUE "")
    set(CNA_TEST_DISPLAY "")
endif()

# With an empty CNA_TEST_DISPLAY the registrations still spell "DISPLAY=" -- which would give every
# test an EMPTY display rather than the caller's. Rather than edit ~990 registrations, drop that one
# entry from every test's ENVIRONMENT once all tests exist. Tests discovered at test time
# (gtest_discover_tests) never had a DISPLAY entry and already inherit.
function(_cna_test_display_directories out dir)
    set(_dirs "${dir}")
    get_property(_subs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(_sub IN LISTS _subs)
        _cna_test_display_directories(_more "${_sub}")
        list(APPEND _dirs ${_more})
    endforeach()
    set(${out} "${_dirs}" PARENT_SCOPE)
endfunction()

function(cna_strip_empty_test_display)
    _cna_test_display_directories(_dirs "${CMAKE_SOURCE_DIR}")
    set(_changed 0)
    foreach(_dir IN LISTS _dirs)
        get_property(_tests DIRECTORY "${_dir}" PROPERTY TESTS)
        foreach(_test IN LISTS _tests)
            get_test_property("${_test}" ENVIRONMENT DIRECTORY "${_dir}" _env)
            if(NOT _env OR NOT "DISPLAY=" IN_LIST _env)
                continue()
            endif()
            list(REMOVE_ITEM _env "DISPLAY=")
            set_tests_properties("${_test}" DIRECTORY "${_dir}" PROPERTIES ENVIRONMENT "${_env}")
            math(EXPR _changed "${_changed} + 1")
        endforeach()
    endforeach()
    message(STATUS "CNA: ${_changed} GPU/window tests inherit the caller's DISPLAY (CNA_TEST_DISPLAY is empty; "
                   "run them privately with tools/platform/run_gpu_tests_private.sh)")
endfunction()

if(CNA_TEST_DISPLAY STREQUAL "")
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
        cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL cna_strip_empty_test_display)
    else()
        # get_test_property/set_tests_properties gained DIRECTORY in 3.28. Older CMake leaves the
        # empty DISPLAY in place: tests then fail to open a display, which is safe -- they cannot
        # reach the desktop -- but not useful. Set CNA_TEST_DISPLAY explicitly there.
        message(WARNING "CNA: CMake ${CMAKE_VERSION} < 3.28 cannot clear the per-test DISPLAY; with "
                        "CNA_TEST_DISPLAY empty the GPU tests will have no display. Set CNA_TEST_DISPLAY.")
    endif()
endif()
