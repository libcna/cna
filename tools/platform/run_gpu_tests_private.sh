#!/bin/sh
# SPDX-License-Identifier: MS-PL
#
# plans/plan_gpu_test_isolation.md GTI-0002: run a build's ctest suite on a PRIVATE display that
# still has the real GPU -- the standard way to run GPU/window tests on a developer machine.
#
#   tools/platform/run_gpu_tests_private.sh <build-dir> [ctest arguments...]
#   e.g. tools/platform/run_gpu_tests_private.sh cmake-build-vulkan -R '^Vulkan_' -j6
#
# What it builds around ctest:
#   tools/platform/wayland_test_server.sh     headless Weston (GL renderer), private XDG_RUNTIME_DIR,
#                                             no session bus, DISPLAY unset
#     + a rootful Xwayland on a display number the X server picks itself (-displayfd), which has
#       DRI3 and therefore presents Vulkan on the real GPU -- Xvfb cannot (no DRI3)
#     + ctest --test-dir <build-dir> with DISPLAY pointing at that Xwayland, and WAYLAND_DISPLAY at
#       the private compositor for the native Wayland backend's tests
#
# Nothing appears on the owner's desktop. It refuses a build tree whose tests force a DISPLAY of
# their own (CNA_TEST_DISPLAY), because ctest would override the private one with it
# (cmake/TestDisplayPolicy.cmake, GTI-0001).
#
# Known limit: the Wine-based XNA interop/differential tests hang inside the private runtime
# directory; exclude them (-E 'XnaPipelineGenuineRuntime|XnaDifferentialBuildTest') or run them on
# their own, where their harness pins Xvfb :99.
#
# Exit status is ctest's; 77 (skip) when no compositor or Xwayland is available.

set -u

HERE=$(cd "$(dirname "$0")" && pwd)

if [ "${1:-}" = "--inside-private-compositor" ]; then
    shift
    BUILD=$1
    shift
    FD_FILE="$XDG_RUNTIME_DIR/xwayland-display"
    : > "$FD_FILE"
    # -displayfd: the server chooses a free display number and writes it to fd 3 once it listens.
    Xwayland -displayfd 3 -geometry 1920x1080 -nolisten tcp -noreset 3>"$FD_FILE" >/dev/null 2>&1 &
    XW=$!
    trap 'kill "$XW" 2>/dev/null; wait "$XW" 2>/dev/null' EXIT INT TERM
    WAITED=0
    while [ ! -s "$FD_FILE" ] && [ "$WAITED" -lt 100 ]; do
        sleep 0.1
        WAITED=$((WAITED + 1))
    done
    NUMBER=$(tr -d '[:space:]' < "$FD_FILE")
    if [ -z "$NUMBER" ]; then
        echo "SKIP: the private rootful Xwayland did not come up" >&2
        exit 77
    fi
    DISPLAY=":$NUMBER"
    export DISPLAY
    if command -v xdpyinfo >/dev/null 2>&1 && ! xdpyinfo -display "$DISPLAY" 2>/dev/null | grep -q DRI3; then
        echo "WARNING: private display $DISPLAY has no DRI3; Vulkan tests will not be able to present" >&2
    fi
    echo "run_gpu_tests_private: DISPLAY=$DISPLAY (private Xwayland), WAYLAND_DISPLAY=$WAYLAND_DISPLAY (private Weston)" >&2
    ctest --test-dir "$BUILD" "$@"
    exit $?
fi

if [ $# -lt 1 ]; then
    echo "usage: $0 <build-dir> [ctest arguments...]" >&2
    exit 2
fi
BUILD=$(cd "$1" 2>/dev/null && pwd) || { echo "run_gpu_tests_private: no such directory: $1" >&2; exit 2; }
shift
if [ ! -f "$BUILD/CMakeCache.txt" ]; then
    echo "run_gpu_tests_private: $BUILD is not a configured build directory" >&2
    exit 2
fi
FORCED=$(sed -n 's/^CNA_TEST_DISPLAY:[A-Z]*=//p' "$BUILD/CMakeCache.txt")
if [ -n "$FORCED" ]; then
    echo "run_gpu_tests_private: refusing -- $BUILD forces DISPLAY=$FORCED on its tests (CNA_TEST_DISPLAY)," >&2
    echo "so ctest would override the private display. Reconfigure with -DCNA_TEST_DISPLAY= first." >&2
    exit 2
fi
if ! command -v Xwayland >/dev/null 2>&1; then
    echo "SKIP: Xwayland is not installed; the private GPU display needs it" >&2
    exit 77
fi

exec "$HERE/wayland_test_server.sh" --compositor weston --renderer gl \
    "$0" --inside-private-compositor "$BUILD" "$@"
