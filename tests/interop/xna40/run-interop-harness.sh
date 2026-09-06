#!/usr/bin/env bash
# SPDX-License-Identifier: MS-PL
#
# plans/plan_xnapipeline_parity.md XNAPP-280: run the interoperability harness against a genuine
# Microsoft XNA 4.0 runtime.
#
# The harness is a Windows program and XNA 4.0 is Windows-only, so this runs it under Wine against
# the XNA 4.0 Refresh runtime installed in a prefix. It needs a display because the harness makes
# a real GraphicsDevice; a virtual one is fine, and is what the default uses.
#
#   CNA_XNA40_REFERENCES  directory holding Microsoft.Xna.Framework*.dll
#   CNA_XNA40_WINEPREFIX  Wine prefix with .NET Framework 4.0 and the XNA runtime
#   CNA_XNA40_DISPLAY     X display (default :99)
#   $1                    fixture directory (default tests/assets/xnb/cna/windows/uncompressed)
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
fixtures="${1:-$repo/tests/assets/xnb/cna/windows/uncompressed}"
refs="${CNA_XNA40_REFERENCES:-/rv/tmp/samples/_tools/xna-game-studio-4-refresh/admin/Program Files/Microsoft XNA/XNA Game Studio/v4.0/References/Windows/x86}"
prefix="${CNA_XNA40_WINEPREFIX:-$HOME/.wine-cna-xna40}"
build="$repo/build/xna-interop"

command -v mcs >/dev/null  || { echo "run-interop-harness: mcs not found" >&2; exit 3; }
command -v wine >/dev/null || { echo "run-interop-harness: wine not found" >&2; exit 3; }
for dll in Microsoft.Xna.Framework.dll Microsoft.Xna.Framework.Graphics.dll Microsoft.Xna.Framework.Game.dll; do
    [ -f "$refs/$dll" ] || { echo "run-interop-harness: missing $refs/$dll" >&2; exit 3; }
done
[ -d "$fixtures" ] || { echo "run-interop-harness: no fixtures at $fixtures" >&2; exit 3; }

# The Microsoft assemblies are copied only into the ignored build directory, beside the harness,
# so the CLR finds them without a GAC; nothing Microsoft owns reaches the repository.
rm -rf "$build"; mkdir -p "$build"
cp "$refs/Microsoft.Xna.Framework.dll" "$refs/Microsoft.Xna.Framework.Graphics.dll" \
   "$refs/Microsoft.Xna.Framework.Game.dll" "$build/"
cp "$fixtures"/* "$build/"

mcs -sdk:4 -platform:x86 -target:exe -nologo -out:"$build/CnaXnbInterop.exe" \
    -r:"$build/Microsoft.Xna.Framework.dll" -r:"$build/Microsoft.Xna.Framework.Graphics.dll" \
    -r:"$build/Microsoft.Xna.Framework.Game.dll" "$here/Program.cs"

win_fix="$(env WINEPREFIX="$prefix" WINEDEBUG=-all wine winepath -w "$build" 2>/dev/null)"
env -u WAYLAND_DISPLAY DISPLAY="${CNA_XNA40_DISPLAY:-:99}" WINEPREFIX="$prefix" WINEDEBUG=-all \
    wine "$build/CnaXnbInterop.exe" "$win_fix"
