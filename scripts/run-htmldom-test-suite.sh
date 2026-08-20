#!/usr/bin/env bash
# plans/plan_html_dom.md HTMLDOM-112: the one official command that builds and runs every HTML_DOM browser
# test page. Before this script existed, run-htmldom-browser-test.sh only ever served the smoke
# page, so reproducing the pixel/stress/dispose PASS counts documented in docs/html-dom-renderer.md
# and plans/plan_html_dom.md required four separate manual `cmake --build --target ...` plus four manual/
# adjusted harness invocations -- this collapses that to one command.
#
# Usage: scripts/run-htmldom-test-suite.sh [build-dir]
#        build-dir defaults to cmake-build-htmldom; it must already be configured, e.g.:
#          emcmake cmake -S . -B cmake-build-htmldom -DCNA_GRAPHICS_RENDERER=HTML_DOM \
#            -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache
#
# Exit code 0 only when every page passes every one of its own checks.
set -euo pipefail

BUILD_DIR="${1:-cmake-build-htmldom}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    echo "error: ${BUILD_DIR} is not a configured build -- run:" >&2
    echo "  emcmake cmake -S . -B ${BUILD_DIR} -DCNA_GRAPHICS_RENDERER=HTML_DOM \\" >&2
    echo "    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache" >&2
    exit 2
fi

# Org-wide build rule: cap parallelism at 3, never -j$(nproc).
echo "==> Building every HTML_DOM browser test target (cna_test_htmldom_all, -j3)"
cmake --build "${BUILD_DIR}" --target cna_test_htmldom_all -j3

PAGES=(smoke pixel stress dispose host-integration memory)
# Distinct ports per page: harmless for this script's own sequential runs, but keeps two concurrent
# invocations of this script (e.g. two CI jobs sharing a runner) from colliding on one fixed port.
declare -A PORTS=([smoke]=8731 [pixel]=8732 [stress]=8733 [dispose]=8734 [host-integration]=8735
                  [memory]=8736)
FAILED=()

for page in "${PAGES[@]}"; do
    echo ""
    echo "==> Running ${page}"
    if CNA_HTMLDOM_TEST_PORT="${PORTS[$page]}" "${SCRIPT_DIR}/run-htmldom-browser-test.sh" "${BUILD_DIR}" "${page}"; then
        echo "==> ${page}: PASS"
    else
        echo "==> ${page}: FAIL"
        FAILED+=("${page}")
    fi
done

echo ""
echo "=== HTML_DOM browser test suite summary ==="
if [[ ${#FAILED[@]} -eq 0 ]]; then
    echo "All 6 pages passed."
    exit 0
else
    echo "${#FAILED[@]} page(s) failed: ${FAILED[*]}"
    exit 1
fi
