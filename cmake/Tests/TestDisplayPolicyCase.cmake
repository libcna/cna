# SPDX-License-Identifier: MS-PL
#
# plans/plan_gpu_test_isolation.md GTI-0006: the live-desktop policy of cmake/TestDisplayPolicy.cmake,
# run for real. Every decision the policy makes lives in cmake/TestDisplayPolicyRules.cmake as a pure
# function; this script calls them in `cmake -P` script mode with fixed inputs and fails on any
# answer other than the one the policy promises. Same shape as RendererDefaultCase.cmake.
#
# Input: CNA_TEST_DISPLAY_RULES_FILE -- path to cmake/TestDisplayPolicyRules.cmake.

# Script mode starts with every policy OLD; the rules use IN_LIST (CMP0057) like the project does.
cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED CNA_TEST_DISPLAY_RULES_FILE)
    message(FATAL_ERROR "the display-policy test needs CNA_TEST_DISPLAY_RULES_FILE")
endif()
include("${CNA_TEST_DISPLAY_RULES_FILE}")

set(_failures 0)
macro(_expect what actual expected)
    if(NOT "${actual}" STREQUAL "${expected}")
        message(SEND_ERROR "${what}: got '${actual}', expected '${expected}'")
        math(EXPR _failures "${_failures} + 1")
    endif()
endmacro()

# Which X displays count as the live desktop.
foreach(_live IN ITEMS ":0" ":0.0" ":0.1" "unix:0" "localhost:0" "127.0.0.1:0.0" "/tmp/.X11-unix/X0")
    cna_test_display_names_live_desktop("${_live}" _answer)
    _expect("'${_live}' names the live desktop" "${_answer}" "TRUE")
endforeach()
foreach(_private IN ITEMS "" ":1" ":01" ":10" ":99" ":2.0" "unix:5" "/tmp/.X11-unix/X3" "otherhost:0x")
    cna_test_display_names_live_desktop("${_private}" _answer)
    _expect("'${_private}' is not the live desktop" "${_answer}" "FALSE")
endforeach()

# CNA_TEST_DISPLAY resolution: the live desktop only with the explicit opt-in; everything else kept.
cna_resolve_test_display(":0" OFF _value _reset)
_expect(":0 without the opt-in is reset" "${_value}|${_reset}" "|TRUE")
cna_resolve_test_display(":0.0" OFF _value _reset)
_expect(":0.0 without the opt-in is reset" "${_value}|${_reset}" "|TRUE")
cna_resolve_test_display("unix:0" OFF _value _reset)
_expect("unix:0 without the opt-in is reset" "${_value}|${_reset}" "|TRUE")
cna_resolve_test_display(":0" ON _value _reset)
_expect(":0 with the opt-in is kept" "${_value}|${_reset}" ":0|FALSE")
cna_resolve_test_display(":99" OFF _value _reset)
_expect("a private display is kept" "${_value}|${_reset}" ":99|FALSE")
cna_resolve_test_display("" OFF _value _reset)
_expect("empty stays empty (tests inherit)" "${_value}|${_reset}" "|FALSE")

# One test's properties after the policy.
cna_apply_test_display_policy_to("SDL_VIDEODRIVER=x11;DISPLAY=" "NOTFOUND" TRUE TRUE _env _mod)
_expect("the empty DISPLAY entry is dropped" "${_env}" "SDL_VIDEODRIVER=x11")
_expect("the Wayland guard is added" "${_mod}" "WAYLAND_DISPLAY=string_append:")
cna_apply_test_display_policy_to("DISPLAY=:99" "" TRUE TRUE _env _mod)
_expect("an explicit DISPLAY is not touched" "${_env}" "DISPLAY=:99")
cna_apply_test_display_policy_to("NOTFOUND" "FOO=set:1;WAYLAND_DISPLAY=string_append:" FALSE TRUE _env _mod)
_expect("an existing guard is not duplicated" "${_mod}" "FOO=set:1;WAYLAND_DISPLAY=string_append:")
_expect("a test with no environment keeps none" "${_env}" "")
cna_apply_test_display_policy_to("DISPLAY=" "" FALSE FALSE _env _mod)
_expect("a forced display keeps its entry" "${_env}" "DISPLAY=")
_expect("no guard where it does not apply (Windows)" "${_mod}" "")

# The guard is the one ctest operation with the property the policy needs: "unset" becomes "empty",
# an exported value is appended nothing. Pinned so an edit to a different operation cannot pass.
_expect("the guard's exact spelling" "${CNA_TEST_WAYLAND_GUARD}" "WAYLAND_DISPLAY=string_append:")

if(_failures GREATER 0)
    message(FATAL_ERROR "${_failures} display-policy expectation(s) failed")
endif()
message(STATUS "display policy: every expectation held")
