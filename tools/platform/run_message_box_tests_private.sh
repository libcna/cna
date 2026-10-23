#!/usr/bin/env bash
# SPDX-License-Identifier: MS-PL
# Native modal-dialog keyboard regression. Never inject keys on the owner's desktop.
# Usage: bash tools/platform/run_message_box_tests_private.sh <existing-build-dir> [gtest options]
set -u
script_dir=$(cd -- "$(dirname -- "$0")" && pwd)
if [[ ${1:-} != --inside ]]; then
    build_dir=$(realpath -- "${1:-cmake-build-multi}") || exit 1
    if [[ $# -gt 0 ]]; then shift; fi
    exec bash "$script_dir/run_gpu_tests_private.sh" --exec bash "$0" --inside \
        "$build_dir/CnaPlatformModuleTests" "$@"
fi
shift
binary=$1
shift
if [[ ${WAYLAND_DISPLAY:-} != cna-weston-* || ${DISPLAY:-} == :0 ]]; then
    echo "Refusing keyboard injection outside the private test display" >&2
    exit 1
fi
command -v xdotool >/dev/null || { echo "SKIP: xdotool is required" >&2; exit 77; }
command -v xwininfo >/dev/null || { echo "SKIP: xwininfo is required" >&2; exit 77; }
command -v xev >/dev/null || { echo "SKIP: xev is required" >&2; exit 77; }
helpers=()
cleanup() {
    for pid in "${helpers[@]}"; do kill "$pid" 2>/dev/null || true; done
    for pid in "${helpers[@]}"; do wait "$pid" 2>/dev/null || true; done
}
trap cleanup EXIT
for spec in Return:Return Escape:Escape SingleReturn:Return SingleEscape:Escape; do
    title="CNA dialog ${spec%%:*} regression"
    key=${spec#*:}
    # Event-driven discovery, bounded so a missing dialog is a failure, not an infinite wait.
    timeout -k 2s 20s bash -c '
        # The native toolkit uses a WM_NAME encoding that xdotool search cannot
        # decode. xwininfo can read it. Wait for root-window events, never sleep/retry
        # a failed key action, and retain the outer timeout for missing windows.
        coproc events { exec stdbuf -oL xev -root -event substructure; }
        event_pid=$events_PID
        event_fd=${events[0]}
        trap '\''kill "$event_pid" 2>/dev/null || true; wait "$event_pid" 2>/dev/null || true'\'' EXIT
        while :; do
            window=$(xwininfo -root -tree | awk -v title="$1" '\''index($0, "\"" title "\"") { print $1; exit }'\'')
            if [[ -n $window ]]; then break; fi
            read -r -u "$event_fd" _ || exit 1
        done
        echo "Injecting $2 into private dialog $window ($1)"
        xdotool windowfocus --sync "$window" && xdotool key --clearmodifiers "$2"
    ' _ "$title" "$key" &
    helpers+=("$!")
done
# Exercise SDL's native X11 dialog. SDL's optional zenity route has its own keyboard
# semantics and is not the X11 toolkit under test. Restrict only the test child's
# executable search path; keyboard helpers keep the normal tools path.
timeout -k 2s 25s env PATH="$(dirname -- "$binary")" SDL_VIDEODRIVER=x11 \
    CNA_TEST_NATIVE_MESSAGE_BOX=1 "$binary" \
    --gtest_filter='Sdl3DialogKeyboardTest.*' "$@"
result=$?
for pid in "${helpers[@]}"; do
    if ! wait "$pid"; then
        if [[ $result -eq 0 ]]; then result=1; fi
    fi
done
helpers=()
exit "$result"
