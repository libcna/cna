#!/usr/bin/env bash
# plan_direct2d.md D2D-123: mechanically enforce "never more than 2 parallel build/test jobs" for
# Direct2D-relevant build commands, rather than trusting review to catch an unbounded --parallel/-j
# (or a cap above 2) every time it is added.
#
# Usage: scripts/verify-direct2d-parallel-jobs.sh
# Exit code 0 = every scanned build command is capped at <=2; 1 = at least one violation.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

FILES=(
    ".github/workflows/d3d-windows-ci.yml"
    "cmake/Tests/Direct2DTests.cmake"
    "cmake/UnitTests.cmake"
    "docs/direct2d-renderer.md"
    "docs/direct2d-mip-storage-spike.md"
    "plan_direct2d.md"
    "scripts/run-wine-direct2d.sh"
    "scripts/run-proton-direct2d.sh"
)

fail=0

check_file() {
    local file="$1"
    [ -f "$file" ] || return 0
    local lineno=0
    while IFS= read -r line || [ -n "$line" ]; do
        lineno=$((lineno + 1))
        # Only executable-looking build invocations are policy inputs; prose may legitimately
        # discuss an old "bare --parallel" defect. Keeping the required numeric cap on the same
        # logical line also prevents a trailing continuation from hiding an unbounded command.
        if ! echo "$line" | grep -qE -- '(^[[:space:]]*(cmake[[:space:]]+--build|ninja|make)([[:space:]]|$)|run:[[:space:]]*cmake[[:space:]]+--build)'; then
            continue
        fi
        local bounded_flag
        bounded_flag=$(echo "$line" | grep -oE -- '(--parallel|-j)[[:space:]=]*[0-9]+' | head -n 1 || true)
        if [ -z "$bounded_flag" ]; then
            echo "VIOLATION: $file:$lineno: build command has no numeric parallel cap: $line" >&2
            fail=1
            continue
        fi
        local value
        value=$(echo "$bounded_flag" | grep -oE '[0-9]+$')
        if [ "$value" -gt 2 ]; then
            echo "VIOLATION: $file:$lineno: parallel job flag exceeds the cap of 2: $line" >&2
            fail=1
        fi
    done < "$file"
}

for f in "${FILES[@]}"; do
    check_file "$f"
done

if [ "$fail" -eq 0 ]; then
    echo "OK: every scanned Direct2D build command caps parallelism at 2 or fewer jobs."
fi
exit "$fail"
