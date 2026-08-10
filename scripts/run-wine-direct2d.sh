#!/usr/bin/env bash
# Run a Windows cross-compiled Direct2D test through Wine without borrowing D3D11's prefix.
#
# `run-wine-dxvk.sh` remains the common launcher because it centralizes Wine logging and the
# optional DXVK gate. Direct2D must select its own prefix, though: a D3D11-only prefix can lack
# Wine's d2d1 runtime even when the normal Wine installation supports Direct2D correctly.
#
# Usage: scripts/run-wine-direct2d.sh <path-to.exe> [args...]
# Optional: CNA_DIRECT2D_WINEPREFIX=/path/to/prefix
set -euo pipefail

if [ "$#" -lt 1 ]; then
    echo "usage: $0 <path-to.exe> [args...]" >&2
    exit 2
fi

# D2D-120: Direct2D presents to a real HWND, so every run needs a display with a known geometry and
# depth. run-direct2d-virtual-display.sh is the single canonical decision (isolated Xvfb, private
# X authority, cleanup on exit); it is reentrant, so a ctest already launched under it is reused
# rather than nested, and CNA_DIRECT2D_USE_HOST_DISPLAY=1 opts out for interactive debugging.
if [ "${CNA_DIRECT2D_VIRTUAL_DISPLAY:-0}" != "1" ] && [ "${CNA_DIRECT2D_USE_HOST_DISPLAY:-0}" != "1" ]; then
    # run-wine-direct2d.sh is invoked as `bash <path>` by the CTest registration and
    # carries no execute bit, so re-enter it through bash explicitly.
    exec "$(dirname "$0")/run-direct2d-virtual-display.sh" bash "$0" "$@"
fi

# The standard Wine prefix is intentionally the compatible default for the non-Proton test path.
# This launcher performs no prefix mutation; projects that maintain a dedicated Direct2D prefix
# can select it explicitly with CNA_DIRECT2D_WINEPREFIX.
export CNA_D3D11_WINEPREFIX="${CNA_DIRECT2D_WINEPREFIX:-$HOME/.wine}"
exec "$(dirname "$0")/run-wine-dxvk.sh" "$@"
