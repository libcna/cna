# SPDX-License-Identifier: MS-PL
#
# plans/plan_gpu_test_isolation.md GTI-0001/GTI-0006: the decisions cmake/TestDisplayPolicy.cmake
# makes, as pure functions with no cache or directory side effects -- so
# cmake/Tests/TestDisplayPolicyCase.cmake can run them in `cmake -P` script mode, in milliseconds,
# instead of configuring the whole project once per case.

include_guard(GLOBAL)

# The ctest ENVIRONMENT_MODIFICATION entry every test carries (GTI-0006). libwayland, asked to
# connect with no WAYLAND_DISPLAY at all, does not fail: it connects to $XDG_RUNTIME_DIR/wayland-0,
# which on a developer machine is the owner's live desktop. An EMPTY value makes it fail instead.
# `string_append` with nothing to append is the one ctest operation that turns "unset" into "empty"
# while leaving an explicitly exported value -- the private runner's compositor, say -- untouched.
set(CNA_TEST_WAYLAND_GUARD "WAYLAND_DISPLAY=string_append:")

# Whether an X DISPLAY value names display 0 -- by convention, and on this project's development
# machines in fact, the live desktop. Every spelling Xlib accepts for it counts: `:0`, a screen
# suffix, the explicit `unix`/loopback hosts, and the socket path form.
function(cna_test_display_names_live_desktop value out_var)
    set(_live FALSE)
    if(value MATCHES "^(unix|localhost|127\\.0\\.0\\.1)?:0(\\.[0-9]+)?$" OR
       value MATCHES "^/tmp/\\.X11-unix/X0(\\.[0-9]+)?$")
        set(_live TRUE)
    endif()
    set(${out_var} ${_live} PARENT_SCOPE)
endfunction()

# The value CNA_TEST_DISPLAY ends up with. A live-desktop value survives only with the explicit
# opt-in; otherwise it becomes empty (tests inherit the caller's DISPLAY) and out_reset says so.
function(cna_resolve_test_display value allow_live out_value out_reset)
    cna_test_display_names_live_desktop("${value}" _live)
    if(_live AND NOT allow_live)
        set(${out_value} "" PARENT_SCOPE)
        set(${out_reset} TRUE PARENT_SCOPE)
    else()
        set(${out_value} "${value}" PARENT_SCOPE)
        set(${out_reset} FALSE PARENT_SCOPE)
    endif()
endfunction()

# One test's ENVIRONMENT and ENVIRONMENT_MODIFICATION after the policy: the empty DISPLAY= entry
# dropped when CNA_TEST_DISPLAY is empty (the test inherits instead), and the Wayland guard added
# once. Values are CMake lists; either may be empty or NOTFOUND.
function(cna_apply_test_display_policy_to environment modification display_empty add_guard
         out_environment out_modification)
    if(NOT environment)
        set(environment "")
    endif()
    if(NOT modification)
        set(modification "")
    endif()
    if(display_empty)
        list(REMOVE_ITEM environment "DISPLAY=")
    endif()
    if(add_guard AND NOT "${CNA_TEST_WAYLAND_GUARD}" IN_LIST modification)
        list(APPEND modification "${CNA_TEST_WAYLAND_GUARD}")
    endif()
    set(${out_environment} "${environment}" PARENT_SCOPE)
    set(${out_modification} "${modification}" PARENT_SCOPE)
endfunction()
