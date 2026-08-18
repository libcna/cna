#!/usr/bin/env bash
# plan_modern.md MOD-7: the CNA::Graphics engine layer builds one Doxygen module page, with no
# undocumented public member.
#
# Usage: scripts/check_cnaext_doxygen.sh <repo-root>
#
# Exits 77 (ctest's SKIP code) when doxygen is not installed -- the check is about this repository's
# headers, and a container without doxygen should not report them as broken.

set -uo pipefail

root="${1:?usage: check_cnaext_doxygen.sh <repo-root>}"

if ! command -v doxygen >/dev/null 2>&1; then
    echo "doxygen is not installed; skipping the engine-layer documentation check"
    exit 77
fi

cd "$root" || exit 1
mkdir -p build/doxygen-cnaext || exit 1

output="$(doxygen docs/Doxyfile.cnaext 2>&1)"
status=$?

if [ "$status" -ne 0 ]; then
    echo "$output"
    echo
    echo "doxygen reported problems in the CNA::Graphics headers (WARN_AS_ERROR is on)."
    echo "Every public member of the engine layer needs a Doxygen block; see CLAUDE.md."
    exit 1
fi

page="build/doxygen-cnaext/html/group__cnaext__engine.html"
if [ ! -f "$page" ]; then
    echo "doxygen succeeded but produced no cnaext_engine group page ($page)."
    echo "The @defgroup lives in modules/graphics-ext/include/CNA/Graphics/CNAEXT.hpp and every"
    echo "other header joins it with @addtogroup inside its namespace."
    exit 1
fi

# The page exists even when nothing joined the group, which is the failure worth catching: a header
# whose @addtogroup sits outside its namespace documents fine and simply is not on the page.
classes="$(grep -o 'classCNA_1_1Graphics_1_1[A-Za-z]*\.html' "$page" | sort -u | wc -l)"
if [ "$classes" -lt 20 ]; then
    echo "the cnaext_engine group page lists only $classes classes, which is too few to be right."
    echo "A header's @addtogroup block must sit *inside* its namespace or its types never join."
    exit 1
fi

echo "doxygen: the cnaext_engine group page lists $classes classes, with no undocumented members."
