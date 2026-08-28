#!/bin/sh
# WEBGPU-150 self-test: drives webgpu_test_launcher.sh through every branch and asserts the exit code,
# using CNA_WEBGPU_LAUNCHER_FORCE_DISPLAY to simulate a present/absent display with no real X server.
# Usage: webgpu_launcher_selftest.sh <path-to-webgpu_test_launcher.sh>
set -u
LAUNCHER="$1"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
pass=0; total=0
check() { # <label> <expected-rc> <actual-rc>
    total=$((total + 1))
    if [ "$2" -eq "$3" ]; then pass=$((pass + 1)); echo "[PASS] $1 (rc=$3)"
    else echo "[FAIL] $1: expected rc=$2 got rc=$3"; fi
}

# 1) Permanently unavailable display -> skip (77), regardless of what the test would do.
CNA_WEBGPU_LAUNCHER_FORCE_DISPLAY=absent CNA_WEBGPU_LAUNCHER_RETRIES=1 sh "$LAUNCHER" true >/dev/null 2>&1
check "absent display -> skip 77" 77 $?

# 2) Available display + a passing test -> success (0).
CNA_WEBGPU_LAUNCHER_FORCE_DISPLAY=present sh "$LAUNCHER" true >/dev/null 2>&1
check "present display + pass -> 0" 0 $?

# 3) Available display + a real, non-display failure -> propagate the exit code (NOT turned into skip).
CNA_WEBGPU_LAUNCHER_FORCE_DISPLAY=present sh "$LAUNCHER" sh -c 'echo "[FAIL] some pixel mismatch"; exit 3' >/dev/null 2>&1
check "present display + real failure -> propagate 3" 3 $?

# 4) Available display + an expected abort (SIGABRT) with a non-display message -> propagate (134),
#    distinguished from "display unavailable" (which would be 77).
CNA_WEBGPU_LAUNCHER_FORCE_DISPLAY=present sh "$LAUNCHER" sh -c 'echo "Cannot present while render targets are bound"; kill -ABRT $$' >/dev/null 2>&1
rc=$?
check "present display + expected abort -> propagate abort (not skip)" 134 "$rc"

# 5) Available display but the test keeps hitting a display/adapter signature -> skip (77) after retries.
CNA_WEBGPU_LAUNCHER_FORCE_DISPLAY=present CNA_WEBGPU_LAUNCHER_RETRIES=2 \
    sh "$LAUNCHER" sh -c 'echo "x11 not available"; exit 1' >/dev/null 2>&1
check "present display + persistent x11-unavailable -> skip 77" 77 $?

# 6) Transient display error: fail with the signature once, then succeed on retry -> success (0).
COUNTER="$TMP/count"; : > "$COUNTER"
CNA_WEBGPU_LAUNCHER_FORCE_DISPLAY=present CNA_WEBGPU_LAUNCHER_RETRIES=2 \
    sh "$LAUNCHER" sh -c '
        n=$(wc -c < "'"$COUNTER"'"); printf x >> "'"$COUNTER"'"
        if [ "$n" -eq 0 ]; then echo "x11 not available"; exit 1; fi
        exit 0' >/dev/null 2>&1
check "present display + transient error -> retry succeeds 0" 0 $?

echo "=== WEBGPU-150 launcher self-test: $pass/$total PASS ==="
[ "$pass" -eq "$total" ]
