#!/usr/bin/env bash
# plans/plan_dx5.md design decision 11: run a Windows cross-compiled .exe (DIRECTX5 renderer, CNA's real
# DirectX 5 -- DirectDraw v4 + Direct3D v3 FVF DrawPrimitive) under Wine against real DirectDraw
# v4, using this project's own dedicated Wine prefix.
#
# Usage: scripts/run-wine-directx5.sh <path-to.exe> [args...]
#
# Unlike scripts/run-wine-dxvk9.sh/run-wine-dxvk.sh, this deliberately does NOT install or gate on
# DXVK: docs/directx-legacy-renderers-analysis.md section 4 confirms DXVK does not translate
# DirectDraw at all, so a vanilla Wine prefix's own builtin ddraw.dll IS the real DirectDraw
# implementation for this renderer -- no extra setup beyond `wineboot --init`.
#
# Set CNA_DX5_WINEPREFIX to point at a different prefix; defaults to ~/.wine-cna-dx1 -- DIRECTX5
# deliberately REUSES DIRECTX1/DIRECTX2/DIRECTX3's own already-initialized prefix rather than creating a fresh
# one (plans/plan_dx5.md's own DX5-0 spike ran entirely against ~/.wine-cna-dx1 and confirmed it works
# for DIRECTX5's IDirectDraw4 + Direct3D v3 needs, so there is nothing DX5-specific a separate prefix
# would buy).
#
# This wrapper also ASSERTS that Wine's real ddraw.dll actually engaged (not a silently-missing/
# no-op DirectDraw implementation), the same DIRECTX2 equivalent of run-wine-dxvk9.sh's "DXVK: <version>"
# gate -- a `trace:ddraw:` WINEDEBUG channel line only appears when the real ddraw.dll handled at
# least one DirectDraw call. Set CNA_DX5_SKIP_DDRAW_GATE=1 for a binary that legitimately never
# creates a DirectDraw object at all.
set -uo pipefail

if [ "$#" -lt 1 ]; then
    echo "usage: $0 <path-to.exe> [args...]" >&2
    exit 2
fi

export WINEPREFIX="${CNA_DX5_WINEPREFIX:-$HOME/.wine-cna-dx1}"
export WINEDEBUG="${CNA_DX5_WINEDEBUG:-+ddraw}"

# Real bug found earlier in this session family (2026-07-20, DIRECTX1/DIRECTX2): setting DISPLAY alone is NOT
# enough in a sandbox that also has a real Wayland session (WAYLAND_DISPLAY set) -- Wine prefers
# its Wayland driver over X11 whenever WAYLAND_DISPLAY is present, regardless of DISPLAY. Unset it
# here so this wrapper always forces Wine's X11 driver against whatever DISPLAY the caller set.
unset WAYLAND_DISPLAY

if [ ! -f "${WINEPREFIX}/system.reg" ]; then
    echo "error: WINEPREFIX '${WINEPREFIX}' is not an initialized Wine prefix." >&2
    echo "Set it up first: WINEPREFIX=${WINEPREFIX} wineboot --init" >&2
    exit 1
fi

logFile="$(mktemp "${TMPDIR:-/tmp}/cna-dx5-ddraw-log.XXXXXX")"
trap 'rm -f "$logFile"' EXIT

wine "$@" 2>&1 | tee "$logFile"
wineExit="${PIPESTATUS[0]}"

if [ "${CNA_DX5_SKIP_DDRAW_GATE:-0}" != "1" ]; then
    if ! grep -Eq ':ddraw:' "$logFile"; then
        echo "error: DIRECTX5's ddraw-engagement gate failed -- no 'trace:ddraw:' WINEDEBUG line was" >&2
        echo "seen in this run's output, so real Wine ddraw.dll may never have actually handled a" >&2
        echo "DirectDraw call. This invalidates any DIRECTX5 pixel-test result from this run -- fix the" >&2
        echo "Wine prefix rather than ignoring this." >&2
        echo "(Set CNA_DX5_SKIP_DDRAW_GATE=1 for a binary that legitimately never opens a DirectDraw object.)" >&2
        exit 3
    fi
fi

exit "$wineExit"
