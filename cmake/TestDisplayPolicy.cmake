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
#   * GTI-0006: no test falls back to the live WAYLAND compositor either. With WAYLAND_DISPLAY
#     unset, libwayland connects to $XDG_RUNTIME_DIR/wayland-0 -- the owner's desktop -- so every
#     test carries CNA_TEST_WAYLAND_GUARD, which turns "unset" into "empty" (connection refused)
#     and leaves an explicitly exported value (the private runner's Weston) as it is.
#
# The decisions themselves are pure functions in cmake/TestDisplayPolicyRules.cmake, exercised in
# script mode by the CnaTestDisplayPolicy_* tests; scripts/check_test_display_isolation.py checks a
# configured tree's actual registrations (CnaTestDisplayIsolation).

include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/TestDisplayPolicyRules.cmake")

set(CNA_TEST_DISPLAY "" CACHE STRING
    "DISPLAY forced on GPU/window-creating tests; empty (default) = inherit the caller's. :0 also needs CNA_TEST_ALLOW_LIVE_DISPLAY=ON")
option(CNA_TEST_ALLOW_LIVE_DISPLAY
    "Allow CNA_TEST_DISPLAY to name the live desktop display (:0): tests will open windows on it" OFF)

cna_resolve_test_display("${CNA_TEST_DISPLAY}" "${CNA_TEST_ALLOW_LIVE_DISPLAY}" _cna_resolved_test_display
                         _cna_test_display_reset)
if(_cna_test_display_reset)
    message(STATUS "CNA: CNA_TEST_DISPLAY=${CNA_TEST_DISPLAY} names the live desktop and is reset to empty; "
                   "tests now inherit the caller's DISPLAY. To really run them on the desktop, configure "
                   "with -DCNA_TEST_DISPLAY=${CNA_TEST_DISPLAY} -DCNA_TEST_ALLOW_LIVE_DISPLAY=ON.")
    set_property(CACHE CNA_TEST_DISPLAY PROPERTY VALUE "")
    set(CNA_TEST_DISPLAY "")
endif()
unset(_cna_resolved_test_display)
unset(_cna_test_display_reset)

# Windows has neither X nor Wayland; nothing there would read the guard.
set(CNA_TEST_WAYLAND_GUARD_APPLIES FALSE)
if(NOT WIN32)
    set(CNA_TEST_WAYLAND_GUARD_APPLIES TRUE)
endif()

# With an empty CNA_TEST_DISPLAY the registrations still spell "DISPLAY=" -- which would give every
# test an EMPTY display rather than the caller's. Rather than edit ~990 registrations, drop that one
# entry from every test's ENVIRONMENT once all tests exist, and add the Wayland guard in the same
# walk. Tests discovered at test time (gtest_discover_tests) are in no directory's TESTS property;
# cmake/UnitTests.cmake gives them the guard through gtest_discover_tests(PROPERTIES ...), and they
# never had a DISPLAY entry.
function(_cna_test_display_directories out dir)
    set(_dirs "${dir}")
    get_property(_subs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(_sub IN LISTS _subs)
        _cna_test_display_directories(_more "${_sub}")
        list(APPEND _dirs ${_more})
    endforeach()
    set(${out} "${_dirs}" PARENT_SCOPE)
endfunction()

function(cna_apply_test_display_policy)
    set(_display_empty FALSE)
    if(CNA_TEST_DISPLAY STREQUAL "")
        set(_display_empty TRUE)
    endif()
    _cna_test_display_directories(_dirs "${CMAKE_SOURCE_DIR}")
    set(_inheriting 0)
    set(_guarded 0)
    foreach(_dir IN LISTS _dirs)
        get_property(_tests DIRECTORY "${_dir}" PROPERTY TESTS)
        foreach(_test IN LISTS _tests)
            get_test_property("${_test}" ENVIRONMENT DIRECTORY "${_dir}" _env)
            get_test_property("${_test}" ENVIRONMENT_MODIFICATION DIRECTORY "${_dir}" _mod)
            cna_apply_test_display_policy_to("${_env}" "${_mod}" ${_display_empty}
                                             ${CNA_TEST_WAYLAND_GUARD_APPLIES} _new_env _new_mod)
            # Each property is written back only when the policy changed it, so an ENVIRONMENT the
            # policy has no business with is never round-tripped through CMake's list handling.
            if(_display_empty AND _env AND "DISPLAY=" IN_LIST _env)
                set_tests_properties("${_test}" DIRECTORY "${_dir}" PROPERTIES ENVIRONMENT "${_new_env}")
                math(EXPR _inheriting "${_inheriting} + 1")
            endif()
            if(NOT "${_new_mod}" STREQUAL "${_mod}")
                set_tests_properties("${_test}" DIRECTORY "${_dir}" PROPERTIES
                    ENVIRONMENT_MODIFICATION "${_new_mod}")
                math(EXPR _guarded "${_guarded} + 1")
            endif()
        endforeach()
    endforeach()
    if(_display_empty)
        message(STATUS "CNA: ${_inheriting} GPU/window tests inherit the caller's DISPLAY (CNA_TEST_DISPLAY is empty; "
                       "run them privately with tools/platform/run_gpu_tests_private.sh)")
    endif()
    if(CNA_TEST_WAYLAND_GUARD_APPLIES)
        message(STATUS "CNA: ${_guarded} configure-time tests never fall back to the default Wayland socket "
                       "(WAYLAND_DISPLAY unset -> empty; an exported value is kept)")
    endif()
endfunction()

if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
    cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL cna_apply_test_display_policy)
else()
    # get_test_property/set_tests_properties gained DIRECTORY in 3.28. Older CMake leaves the
    # empty DISPLAY in place: tests then fail to open a display, which is safe -- they cannot
    # reach the desktop -- but not useful. Set CNA_TEST_DISPLAY explicitly there. The Wayland guard
    # cannot be applied either: run the GPU tests only through tools/platform/run_gpu_tests_private.sh.
    if(CNA_TEST_DISPLAY STREQUAL "")
        message(WARNING "CNA: CMake ${CMAKE_VERSION} < 3.28 cannot clear the per-test DISPLAY; with "
                        "CNA_TEST_DISPLAY empty the GPU tests will have no display. Set CNA_TEST_DISPLAY.")
    endif()
    if(CNA_TEST_WAYLAND_GUARD_APPLIES)
        message(WARNING "CNA: CMake ${CMAKE_VERSION} < 3.28 cannot add the per-test Wayland guard; a test "
                        "run with WAYLAND_DISPLAY unset can reach the live compositor. Use "
                        "tools/platform/run_gpu_tests_private.sh.")
    endif()
endif()
