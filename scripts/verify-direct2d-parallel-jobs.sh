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
    "docs/direct2d-backend.md"
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
        if ! echo "$line" | grep -qE -- '(--parallel|-j)\b'; then
            continue
        fi
        local value
        value=$(echo "$line" | grep -oE -- '(--parallel|-j)[[:space:]=]*[0-9]*' | grep -oE '[0-9]+$' || true)
        if [ -z "$value" ]; then
            echo "VIOLATION: $file:$lineno: unbounded parallel job flag: $line" >&2
            fail=1
        elif [ "$value" -gt 2 ]; then
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
