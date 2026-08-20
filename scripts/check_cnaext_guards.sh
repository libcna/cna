#!/usr/bin/env bash
# plans/plan_modern.md MOD-3: a real, automated proof that the CNA::Graphics engine layer stays fully
# opt-in. Every production file of modules/graphics-ext (and every engine-layer header the plan adds
# on top) must be wrapped in `#ifdef CNA_CNAEXT` ... `#endif // CNA_CNAEXT`, because that guard --
# not the CMake option alone -- is what makes a default build (CNA_CNAEXT=OFF) compile the layer to
# nothing. A file that loses its guard still builds in both configurations, so the regression is
# invisible until an XNA-only consumer picks up an engine-layer symbol it should never see.
#
# Pure text check: no compiled binary, no renderer, no display. Registered as the
# CnaExt_GuardDiscipline CTest.
#
# Usage: scripts/check_cnaext_guards.sh <repo-root>
set -uo pipefail

repo_root="${1:-}"
if [ -z "$repo_root" ]; then
    echo "usage: $0 <repo-root>" >&2
    exit 2
fi

module_dir="${repo_root}/modules/graphics-ext"
if [ ! -d "${module_dir}/include" ] || [ ! -d "${module_dir}/src" ]; then
    echo "error: graphics-ext module directories not found under ${repo_root}" >&2
    exit 1
fi

failures=0
checked=0

report() {
    echo "FAIL: $1" >&2
    failures=$((failures + 1))
}

# Production sources and headers only. tests/ and examples/ carry the same guard by convention but
# are allowed to open with an explanatory comment block and to provide a CNA_CNAEXT=OFF fallback
# main(), so they are checked separately (opening-guard requirement only) below.
while IFS= read -r -d '' file; do
    checked=$((checked + 1))
    rel="${file#${repo_root}/}"

    if ! grep -q '^#ifdef CNA_CNAEXT$' "$file"; then
        report "${rel}: missing '#ifdef CNA_CNAEXT' -- the engine layer must not compile in a default build"
        continue
    fi

    if ! grep -q '^#endif // CNA_CNAEXT$' "$file"; then
        report "${rel}: missing the closing '#endif // CNA_CNAEXT' marker comment"
        continue
    fi

    # The guard must be the outermost construct: nothing but comments, #pragma once and #include
    # of another already-guarded engine-layer header may precede it, and it must be the last
    # preprocessor line of the file. Anything declared outside it leaks into a default build.
    first_code_line="$(grep -n -v -E '^\s*(//|/\*|\*|$)' "$file" | head -1 | cut -d: -f2-)"
    case "$first_code_line" in
        '#pragma once'|'#ifdef CNA_CNAEXT'|'#include'*) ;;
        *) report "${rel}: first non-comment line is '${first_code_line}' -- expected #pragma once or the CNA_CNAEXT guard" ;;
    esac

    last_guard_line="$(grep -n -E '^#(ifdef|endif)' "$file" | tail -1 | cut -d: -f2-)"
    if [ "$last_guard_line" != '#endif // CNA_CNAEXT' ]; then
        report "${rel}: last preprocessor line is '${last_guard_line}' -- the CNA_CNAEXT guard must close the file"
    fi
done < <(find "${module_dir}/include" "${module_dir}/src" -type f \( -name '*.hpp' -o -name '*.cpp' \) -print0 | sort -z)

# Unit tests are globbed into the single CnaTests binary regardless of the option, so each one must
# carry the guard itself. The guard may sit after an explanatory header comment and after
# #include <gtest/gtest.h>, so only its presence is required here -- not its position.
while IFS= read -r -d '' file; do
    checked=$((checked + 1))
    rel="${file#${repo_root}/}"
    if ! grep -q '^#ifdef CNA_CNAEXT$' "$file"; then
        report "${rel}: missing '#ifdef CNA_CNAEXT' -- graphics-ext unit tests are linked into CnaTests in every configuration"
    fi
done < <(find "${module_dir}/tests" -type f -name '*.cpp' -print0 2>/dev/null | sort -z)

# Examples own their registration, so an example may rely on its CMake gate instead of an in-file
# guard -- but then that gate has to actually exist. Every executable-defining call in the module's
# examples/CMakeLists.txt must sit inside an if() block that tests CNA_CNAEXT; otherwise a default
# build would try to compile engine-layer code that compiled away to an empty translation unit.
examples_cmake="${module_dir}/examples/CMakeLists.txt"
if [ -f "$examples_cmake" ]; then
    checked=$((checked + 1))
    awk_report="$(awk '
        /^[[:space:]]*if[[:space:]]*\(/  { depth++; gated[depth] = (index($0, "CNA_CNAEXT") > 0) || gated[depth-1] }
        /^[[:space:]]*endif[[:space:]]*\(/ { gated[depth] = 0; depth-- }
        /^[[:space:]]*(add_executable|cna_graphics_ext_test)[[:space:]]*\(/ {
            if (depth == 0 || !gated[depth]) print "  line " NR ": " $0
        }
    ' "$examples_cmake")"
    if [ -n "$awk_report" ]; then
        report "modules/graphics-ext/examples/CMakeLists.txt: executable(s) registered outside any CNA_CNAEXT gate:"
        echo "$awk_report" >&2
    fi
fi

if [ "$checked" -eq 0 ]; then
    echo "error: no graphics-ext files were checked -- the module layout changed and this script did not" >&2
    exit 1
fi

if [ "$failures" -ne 0 ]; then
    echo "checked ${checked} file(s), ${failures} CNA_CNAEXT guard violation(s)" >&2
    exit 1
fi

echo "checked ${checked} file(s): every graphics-ext file is guarded by CNA_CNAEXT"
exit 0
