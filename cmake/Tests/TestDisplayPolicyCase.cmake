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

# AM4-007: a host without X11/Wayland (macOS) drops display-server driver pins and nothing else.
cna_adapt_test_video_driver_to_host("SDL_VIDEODRIVER=x11;SDL_AUDIODRIVER=dummy" FALSE _env)
_expect("an x11 pin is dropped on a host without one" "${_env}" "SDL_AUDIODRIVER=dummy")
cna_adapt_test_video_driver_to_host("SDL_VIDEODRIVER=wayland" FALSE _env)
_expect("a wayland pin is dropped on a host without one" "${_env}" "")
cna_adapt_test_video_driver_to_host("SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy" FALSE _env)
_expect("a dummy pin is kept" "${_env}" "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
cna_adapt_test_video_driver_to_host("SDL_VIDEODRIVER=offscreen" FALSE _env)
_expect("an offscreen pin is kept" "${_env}" "SDL_VIDEODRIVER=offscreen")
cna_adapt_test_video_driver_to_host(
    "SDL_VIDEODRIVER=x11;CNA_TEST_EXPECT_NATIVE_WINDOW_SYSTEM=X11" FALSE _env)
_expect("a test asserting the X11 window system keeps its pin" "${_env}"
        "SDL_VIDEODRIVER=x11;CNA_TEST_EXPECT_NATIVE_WINDOW_SYSTEM=X11")
cna_adapt_test_video_driver_to_host("SDL_VIDEODRIVER=x11;DISPLAY=:99" TRUE _env)
_expect("a display-server host keeps every pin" "${_env}" "SDL_VIDEODRIVER=x11;DISPLAY=:99")
cna_adapt_test_video_driver_to_host("NOTFOUND" FALSE _env)
_expect("no environment stays none" "${_env}" "")

# The guard is the one ctest operation with the property the policy needs: "unset" becomes "empty",
# an exported value is appended nothing. Pinned so an edit to a different operation cannot pass.
_expect("the guard's exact spelling" "${CNA_TEST_WAYLAND_GUARD}" "WAYLAND_DISPLAY=string_append:")

if(_failures GREATER 0)
    message(FATAL_ERROR "${_failures} display-policy expectation(s) failed")
endif()
message(STATUS "display policy: every expectation held")
