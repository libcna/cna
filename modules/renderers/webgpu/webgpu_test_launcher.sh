#!/bin/sh
# WEBGPU-150: preflight/retry launcher for the display/GPU-backed WebGPU CTests (Linux-only, matching
# the test guard). It makes a permanently-unavailable display/GPU return the CTest skip code (77) so a
# headless `ctest -L WebGPU` SKIPs instead of producing dozens of abort failures, while a real
# renderer/test failure is propagated unchanged and an expected abort is left to the test's own
# registration. SKIP_RETURN_CODE 77 is applied directory-wide (cna_apply_skip_convention).
#
# Usage: webgpu_test_launcher.sh <test-exe> [args...]
# Reads DISPLAY (set by the test's ENVIRONMENT property) for the preflight.
# Env knobs (for the self-test): CNA_WEBGPU_LAUNCHER_RETRIES (default 2),
#   CNA_WEBGPU_LAUNCHER_FORCE_DISPLAY = present|absent (bypasses the real display probe).
set -u

SKIP=77
retries="${CNA_WEBGPU_LAUNCHER_RETRIES:-2}"

# A failure carrying one of these signatures means the display or GPU adapter was unavailable (an
# environment problem), NOT a renderer/test defect -- so it is retried, then skipped. Kept narrow so a
# genuine test failure (which prints "[FAIL] ..." / a mismatch) is never mistaken for it.
sig='x11 not available|No available video device|Could not init.*SDL|Failed to (connect|open)( to)? .*display|[Cc]ould not open display|no( suitable| WebGPU| available)? adapter|Failed to (get|request|create) .*adapter|RequestAdapterStatus|timed out waiting for (adapter|device|surface)'

display_present() {
    case "${CNA_WEBGPU_LAUNCHER_FORCE_DISPLAY:-}" in
        present) return 0 ;;
        absent)  return 1 ;;
    esac
    d="${DISPLAY:-}"
    if [ -n "$d" ]; then
        # ":N", ":N.S" or "host:N.S" -> extract N and test the X socket.
        n=$(printf '%s' "$d" | sed 's/^[^:]*://; s/\..*$//')
        [ -n "$n" ] && [ -S "/tmp/.X11-unix/X$n" ] && return 0
    fi
    w="${WAYLAND_DISPLAY:-}"
    if [ -n "$w" ]; then
        rt="${XDG_RUNTIME_DIR:-/run/user/$(id -u 2>/dev/null || echo 0)}"
        { [ -S "$rt/$w" ] || [ -S "$w" ]; } && return 0
    fi
    return 1
}

# 1) Preflight: is a display reachable at all? A short retry absorbs a transient race. If it never
#    appears, skip -- this covers headless runs uniformly, including supervisor/expected-abort tests
#    (which must be skipped, not run, when there is no display to reproduce their behaviour on).
i=0
while [ "$i" -le "$retries" ]; do
    display_present && break
    i=$((i + 1))
    [ "$i" -le "$retries" ] && sleep 1
done
if ! display_present; then
    echo "[SKIP] WEBGPU-150 launcher: no reachable display (DISPLAY='${DISPLAY:-unset}'); skipping." >&2
    exit "$SKIP"
fi

# 2) Run the test. Retry ONLY on a display/adapter-unavailable signature (a transient that slipped past
#    the preflight); after the retries are exhausted, skip. Any other non-zero exit -- a real failure or
#    an expected abort -- is propagated unchanged.
attempt=0
while :; do
    out=$("$@" 2>&1)
    rc=$?
    printf '%s\n' "$out"
    [ "$rc" -eq 0 ] && exit 0
    if printf '%s' "$out" | grep -Eq "$sig"; then
        attempt=$((attempt + 1))
        if [ "$attempt" -le "$retries" ]; then
            echo "[RETRY] WEBGPU-150 launcher: transient display/adapter error (attempt $attempt/$retries), retrying." >&2
            sleep 1
            continue
        fi
        echo "[SKIP] WEBGPU-150 launcher: display/GPU unavailable after $retries retries; skipping." >&2
        exit "$SKIP"
    fi
    exit "$rc"
done
