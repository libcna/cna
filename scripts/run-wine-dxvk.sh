#!/usr/bin/env bash
# plan_dx.md DX-3: run a Windows cross-compiled .exe (D3D11 renderer) under Wine
# with DXVK, using this project's own dedicated Wine prefix.
#
# Usage: scripts/run-wine-dxvk.sh <path-to.exe> [args...]
#
# Set CNA_D3D11_WINEPREFIX to point at a different prefix; defaults to
# ~/.wine-cna-d3d11 (created via `wineboot --init` + `dxvk-setup install`, see
# programs.md §10). DXVK_LOG_PATH/DXVK_LOG_LEVEL/DXVK_HUD are honored from the
# calling environment if already set (e.g. a CTest driver script), otherwise
# default to a log level that's enough to confirm DXVK (not WineD3D) actually
# handled the run.
#
# plan_dx.md DX-85: this wrapper also automatically ASSERTS that DXVK itself
# (not a silent WineD3D fallback) handled the run, using DX-4's own established
# distinguishing signal -- a "DXVK: <version>" log line that only DXVK's own
# logger ever prints (vanilla WineD3D never does). Without this, a broken/
# missing DXVK install could silently degrade every D3D11 pixel test in this
# suite to validating WineD3D's own (different) Direct3D implementation instead,
# and green CTest output alone would never reveal it -- exactly the gap the
# project owner flagged ("pouhé spuštění pod Wine nestačí"). Set
# CNA_D3D11_ALLOW_WINED3D=1 to bypass this gate for a deliberate one-off
# non-DXVK diagnostic run (e.g. reproducing DX-1's own original vanilla-Wine
# spike) -- do not set this for normal test runs. Set CNA_D3D11_SKIP_DXVK_GATE=1
# for a binary that legitimately never creates a D3D11 device at all (e.g.
# DirectX11_Common's pure-function D3DCommon mapping-table checks, DX-11-fmt/
# DX-12-state/DX-16-vtx) -- such a run never prints a DXVK line for entirely
# unrelated, legitimate reasons, and this gate would otherwise misreport it as
# a WineD3D fallback.
set -uo pipefail

if [ "$#" -lt 1 ]; then
    echo "usage: $0 <path-to.exe> [args...]" >&2
    exit 2
fi

export WINEPREFIX="${CNA_D3D11_WINEPREFIX:-$HOME/.wine-cna-d3d11}"
export WINEDEBUG="${WINEDEBUG:--all}"
export DXVK_LOG_LEVEL="${DXVK_LOG_LEVEL:-info}"

if [ ! -f "${WINEPREFIX}/system.reg" ]; then
    echo "error: WINEPREFIX '${WINEPREFIX}' is not an initialized Wine prefix." >&2
    echo "Set it up first: WINEPREFIX=${WINEPREFIX} wineboot --init && WINEPREFIX=${WINEPREFIX} dxvk-setup install" >&2
    exit 1
fi

logFile="$(mktemp "${TMPDIR:-/tmp}/cna-d3d11-dxvk-log.XXXXXX")"
trap 'rm -f "$logFile"' EXIT

wine "$@" 2>&1 | tee "$logFile"
wineExit="${PIPESTATUS[0]}"

if [ "${CNA_D3D11_ALLOW_WINED3D:-0}" != "1" ] && [ "${CNA_D3D11_SKIP_DXVK_GATE:-0}" != "1" ]; then
    # Official DXVK 2.6 binaries print "DXVK: v2.6" while older/package builds print
    # "DXVK: 2.6.0". Both are authoritative DXVK logger markers.
    if ! grep -Eq 'DXVK: v?[0-9]+\.[0-9]+' "$logFile"; then
        echo "error: DX-85 gate failed -- no 'DXVK: <version>' log line was seen in this run's" >&2
        echo "output, so this looks like a silent WineD3D fallback rather than a real DXVK-backed" >&2
        echo "Direct3D 11 run. This invalidates any D3D11-semantics pixel-test result from this" >&2
        echo "run -- fix the DXVK install (see programs.md §10) rather than ignoring this." >&2
        echo "(Set CNA_D3D11_ALLOW_WINED3D=1 to deliberately bypass this for a one-off non-DXVK diagnostic run.)" >&2
        exit 3
    fi
fi

exit "$wineExit"
