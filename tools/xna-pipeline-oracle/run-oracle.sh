#!/usr/bin/env bash
# SPDX-License-Identifier: MS-PL
#
# plans/plan_xnapipeline_parity.md XNAPP-011: compile and run the XNA 4.0 Content Pipeline
# public-API oracle against a legally installed XNA Game Studio 4.0 and write the inventory.
#
# The oracle is plain C# with no XNA compile-time reference, so it is compiled here with mono's
# `mcs` (or `csc.exe` on Windows) and executed under the real .NET Framework 4.0 in a Wine prefix:
# two of the importer assemblies are mixed-mode x86 and only the Microsoft runtime loads them.
#
# Nothing Microsoft owns is copied anywhere but the ignored build directory; the committed output
# is the JSON inventory alone.
#
#   CNA_XNA40_REFERENCES  directory holding Microsoft.Xna.Framework.Content.Pipeline*.dll
#   CNA_XNA40_WINEPREFIX  Wine prefix with .NET Framework 4.0 (default ~/.wine-cna-xna40)
#   $1                    output JSON path (default tests/reference/xna40/content-pipeline-api.json)
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
out="${1:-$repo/tests/reference/xna40/content-pipeline-api.json}"
refs="${CNA_XNA40_REFERENCES:-/rv/tmp/samples/_tools/xna-game-studio-4-refresh/admin/Program Files/Microsoft XNA/XNA Game Studio/v4.0/References/Windows/x86}"
prefix="${CNA_XNA40_WINEPREFIX:-$HOME/.wine-cna-xna40}"
build="$repo/build/xna-pipeline-oracle"

if [ ! -f "$refs/Microsoft.Xna.Framework.Content.Pipeline.dll" ]; then
    echo "run-oracle: no Microsoft.Xna.Framework.Content.Pipeline.dll under $refs" >&2
    echo "run-oracle: set CNA_XNA40_REFERENCES to an XNA Game Studio 4.0 References/Windows/x86 directory" >&2
    exit 3
fi
if [ ! -d "$prefix/drive_c/windows/Microsoft.NET/Framework/v4.0.30319" ]; then
    echo "run-oracle: no .NET Framework 4.0 in Wine prefix $prefix" >&2
    exit 3
fi
command -v mcs >/dev/null || { echo "run-oracle: mcs (mono C# compiler) not found" >&2; exit 3; }
command -v wine >/dev/null || { echo "run-oracle: wine not found" >&2; exit 3; }

mkdir -p "$build" "$(dirname "$out")"
mcs -sdk:4 -platform:x86 -target:exe -nologo -out:"$build/PipelineApiOracle.exe" "$here/PipelineApiOracle.cs"

win_refs="$(env WINEPREFIX="$prefix" WINEDEBUG=-all wine winepath -w "$refs" 2>/dev/null)"
win_out="$(env WINEPREFIX="$prefix" WINEDEBUG=-all wine winepath -w "$build/content-pipeline-api.json" 2>/dev/null)"
env -u WAYLAND_DISPLAY -u DISPLAY WINEPREFIX="$prefix" WINEDEBUG=-all \
    wine "$build/PipelineApiOracle.exe" "$win_refs" "$win_out"

# Normalize line endings defensively and publish atomically.
tr -d '\r' < "$build/content-pipeline-api.json" > "$out.tmp"
mv "$out.tmp" "$out"
echo "run-oracle: wrote $out ($(wc -c < "$out") bytes, sha256 $(sha256sum "$out" | cut -c1-16)...)"
