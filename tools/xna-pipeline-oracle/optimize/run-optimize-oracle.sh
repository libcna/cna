#!/usr/bin/env bash
# SPDX-License-Identifier: MS-PL
#
# plans/plan_xna_sample_xnb_sweep.md XNASWEEP-149: run genuine XNA 4.0's MeshHelper.OptimizeForCache
# over a generated probe file and record the order it answers with.
#
#   $1  probe file (default build/xna-sample-sweep/optimize/probes.txt)
#   $2  output file (default build/xna-sample-sweep/optimize/answers.txt)
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
probes="${1:-$repo/build/xna-sample-sweep/optimize/probes.txt}"
answers="${2:-$repo/build/xna-sample-sweep/optimize/answers.txt}"
refs="${CNA_XNA40_REFERENCES:-/rv/tmp/samples/_tools/xna-game-studio-4-refresh/admin/Program Files/Microsoft XNA/XNA Game Studio/v4.0/References/Windows/x86}"
prefix="${CNA_XNA40_WINEPREFIX:-$HOME/.wine-cna-xna40}"
build="$repo/build/xna-pipeline-oracle/optimize"

command -v mcs >/dev/null || { echo "run-optimize-oracle: mcs not found" >&2; exit 3; }
command -v wine >/dev/null || { echo "run-optimize-oracle: wine not found" >&2; exit 3; }
[ -f "$probes" ] || { echo "run-optimize-oracle: missing $probes" >&2; exit 3; }

mkdir -p "$build" "$(dirname "$answers")"
for dll in Microsoft.Xna.Framework.dll Microsoft.Xna.Framework.Content.Pipeline.dll; do
    [ -f "$refs/$dll" ] || { echo "run-optimize-oracle: missing $refs/$dll" >&2; exit 3; }
    cp "$refs/$dll" "$build/"
done
mcs -sdk:4 -platform:x86 -target:exe -nologo -out:"$build/OptimizeForCacheOracle.exe" \
    -r:"$build/Microsoft.Xna.Framework.dll" \
    -r:"$build/Microsoft.Xna.Framework.Content.Pipeline.dll" \
    "$here/OptimizeForCacheOracle.cs"

cp "$probes" "$build/probes.txt"
win_probes="$(env WINEPREFIX="$prefix" WINEDEBUG=-all wine winepath -w "$build/probes.txt" 2>/dev/null)"
win_answers="$(env WINEPREFIX="$prefix" WINEDEBUG=-all wine winepath -w "$build/answers.txt" 2>/dev/null)"
env -u WAYLAND_DISPLAY DISPLAY="${CNA_XNA40_DISPLAY:-:99}" WINEPREFIX="$prefix" WINEDEBUG=-all \
    wine "$build/OptimizeForCacheOracle.exe" "$win_probes" "$win_answers"
tr -d '\r' < "$build/answers.txt" > "$answers"
echo "run-optimize-oracle: $(wc -l < "$answers") answers to $answers"
