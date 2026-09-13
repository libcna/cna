#!/usr/bin/env bash
# SPDX-License-Identifier: MS-PL
#
# plans/plan_xna_sample_xnb_sweep.md XNASWEEP-172: run the genuine XNA 4.0 framework's
# Matrix.Multiply and Matrix.CreateRotation* over the committed cases and record the exact bits.
#
#   CNA_XNA40_REFERENCES  directory holding Microsoft.Xna.Framework.dll
#   CNA_XNA40_WINEPREFIX  Wine prefix with .NET Framework 4.0 (default ~/.wine-cna-xna40)
#   $1                    output file (default tests/reference/xna40/framework/matrix-oracle.txt)
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
out="${1:-$repo/tests/reference/xna40/framework/matrix-oracle.txt}"
cases="$repo/tests/assets/xna40/framework/matrix-cases.txt"
refs="${CNA_XNA40_REFERENCES:-/rv/tmp/samples/_tools/xna-game-studio-4-refresh/admin/Program Files/Microsoft XNA/XNA Game Studio/v4.0/References/Windows/x86}"
prefix="${CNA_XNA40_WINEPREFIX:-$HOME/.wine-cna-xna40}"
build="$repo/build/xna-pipeline-oracle/matrix"

[ -f "$refs/Microsoft.Xna.Framework.dll" ] || {
    echo "run-matrix-oracle: missing $refs/Microsoft.Xna.Framework.dll" >&2; exit 3; }
[ -f "$cases" ] || {
    echo "run-matrix-oracle: no cases at $cases; run make_matrix_pairs.py" >&2; exit 3; }
command -v mcs >/dev/null  || { echo "run-matrix-oracle: mcs not found" >&2; exit 3; }
command -v wine >/dev/null || { echo "run-matrix-oracle: wine not found" >&2; exit 3; }

mkdir -p "$build" "$(dirname "$out")"
# The Microsoft assembly is copied only into the ignored build directory, beside the driver.
cp "$refs/Microsoft.Xna.Framework.dll" "$build/"
mcs -sdk:4 -platform:x86 -target:exe -nologo -out:"$build/MatrixOracle.exe" \
    -r:"$build/Microsoft.Xna.Framework.dll" "$here/MatrixOracle.cs"
cp "$cases" "$build/cases.txt"
env WINEPREFIX="$prefix" WINEDEBUG=-all wine "$build/MatrixOracle.exe" \
    "$(env WINEPREFIX="$prefix" WINEDEBUG=-all wine winepath -w "$build/cases.txt" 2>/dev/null)" \
    "$(env WINEPREFIX="$prefix" WINEDEBUG=-all wine winepath -w "$build/oracle.txt" 2>/dev/null)"
tr -d '\r' < "$build/oracle.txt" > "$out"
echo "run-matrix-oracle: wrote $(grep -c '^case ' "$out") cases to $out"
