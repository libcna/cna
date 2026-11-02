#!/usr/bin/env bash
# SPDX-License-Identifier: MS-PL
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
refs="${CNA_XNA40_REFERENCES:-/rv/tmp/samples/_tools/xna-game-studio-4-refresh/admin/Program Files/Microsoft XNA/XNA Game Studio/v4.0/References/Windows/x86}"
prefix="${CNA_XNA40_WINEPREFIX:-$HOME/.wine-cna-xna40}"
if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <output-directory>" >&2
    exit 2
fi
for program in mcs wine; do
    command -v "$program" >/dev/null || { echo "Missing $program" >&2; exit 77; }
done
reference="$refs/Microsoft.Xna.Framework.dll"
[ -f "$reference" ] || { echo "Missing $reference" >&2; exit 77; }
[ -d "$prefix" ] || { echo "Missing existing XNA Wine prefix: $prefix" >&2; exit 77; }
mkdir -p "$1"
output="$(cd "$1" && pwd)"
scratch="$(mktemp -d "${TMPDIR:-/tmp}/cna-microphone-start-oracle.XXXXXX")"
trap 'rm -rf "$scratch"' EXIT
mcs -sdk:4 -platform:x86 -nologo -out:"$scratch/MicrophoneStartOracle.exe" \
    "-r:$reference" "$here/MicrophoneStartOracle.cs"

"$repo/tools/platform/run_gpu_tests_private.sh" --exec env \
    WINEPREFIX="$prefix" WINEDEBUG=-all WINEDLLOVERRIDES=winedbg.exe=d WAYLAND_DISPLAY= \
    wine "$scratch/MicrophoneStartOracle.exe" 2>&1 | tee "$output/observations.txt"
