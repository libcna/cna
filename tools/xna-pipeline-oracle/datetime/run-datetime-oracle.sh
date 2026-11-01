#!/usr/bin/env bash
# SPDX-License-Identifier: MS-PL
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
refs="${CNA_XNA40_REFERENCES:-/rv/tmp/samples/_tools/xna-game-studio-4-refresh/admin/Program Files/Microsoft XNA/XNA Game Studio/v4.0/References/Windows/x86}"
prefix="${CNA_XNA40_WINEPREFIX:-$HOME/.wine-cna-xna40}"
zone="${CNA_XNA40_DATETIME_TZ:-Etc/GMT-2}"

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <output-directory>" >&2
    exit 2
fi
for program in mcs wine; do
    command -v "$program" >/dev/null || { echo "Missing $program" >&2; exit 77; }
done
for dll in Microsoft.Xna.Framework.dll Microsoft.Xna.Framework.Content.Pipeline.dll; do
    [ -f "$refs/$dll" ] || { echo "Missing $refs/$dll" >&2; exit 77; }
done
[ -d "$prefix" ] || { echo "Missing existing XNA Wine prefix: $prefix" >&2; exit 77; }
mkdir -p "$1"
output="$(cd "$1" && pwd)"
refs="$(cd "$refs" && pwd)"
scratch="$(mktemp -d "${TMPDIR:-/tmp}/cna-datetime-oracle.XXXXXX")"
trap 'rm -rf "$scratch"' EXIT

mcs -sdk:4 -platform:x86 -target:exe -nologo -out:"$scratch/DateTimeOracle.exe" \
    -r:"$refs/Microsoft.Xna.Framework.dll" "$here/DateTimeOracle.cs"

"$repo/tools/platform/run_gpu_tests_private.sh" --exec env \
    WINEPREFIX="$prefix" WINEDEBUG=-all WINEDLLOVERRIDES=winedbg.exe=d WAYLAND_DISPLAY= TZ="$zone" \
    bash -c '
        pipeline=$(wine winepath -w "$2")
        output=$(wine winepath -w "$3")
        wine "$1" "$pipeline" "$output"
    ' cna-datetime "$scratch/DateTimeOracle.exe" \
    "$refs/Microsoft.Xna.Framework.Content.Pipeline.dll" "$output" \
    2>&1 | tee "$output/observations.txt"
