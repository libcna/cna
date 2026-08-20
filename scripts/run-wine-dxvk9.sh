#!/usr/bin/env bash
# plans/plan_dx9.md D9-5: run a Windows cross-compiled .exe (D3D9 renderer) under Wine
# with DXVK, using this project's own dedicated Wine prefix.
#
# Usage: scripts/run-wine-dxvk9.sh <path-to.exe> [args...]
#
# This is a NEW script, deliberately not a shared edit to scripts/run-wine-dxvk.sh
# (plans/plan_dx9.md cold-start §4: that file is D3D11/D3D12's, touched only to
# register tests, and this repo's own convention keeps every renderer's runtime
# wrapper independent).
#
# Set CNA_D3D9_WINEPREFIX to point at a different prefix; defaults to
# ~/.wine-cna-d3d11 -- verified in plans/plan_dx9.md's "Development environment"
# section that this prefix's own `dxvk-setup install` (run for D3D11/DX-2)
# already symlinks system32/d3d9.dll to DXVK's d3d9.dll.so with "d3d9"="native"
# in the registry, so D3D9 needs no separate prefix setup. Do NOT confuse this
# env var with D3D11's own CNA_D3D11_WINEPREFIX -- same physical prefix by
# default, deliberately distinct names (plans/plan_dx9.md cold-start §4) so D3D9 test
# infra never silently inherits D3D11's own env-var contract.
#
# DXVK_LOG_PATH/DXVK_LOG_LEVEL/DXVK_HUD are honored from the calling
# environment if already set (e.g. a CTest driver script), otherwise default to
# a log level that's enough to confirm DXVK (not WineD3D) actually handled the
# run.
#
# plans/plan_dx9.md design decision 17 / D9-5: this wrapper also automatically
# ASSERTS that DXVK itself (not a silent WineD3D fallback) handled the run,
# mirroring plans/plan_dx.md's DX-85 gate -- a "DXVK: <version>" log line that only
# DXVK's own logger ever prints (vanilla WineD3D never does). Without this, a
# broken/missing DXVK install could silently degrade every D3D9 pixel test in
# this suite to validating WineD3D's own (different) Direct3D 9 implementation
# instead, and green CTest output alone would never reveal it. Set
# CNA_D3D9_ALLOW_WINED3D=1 to bypass this gate for a deliberate one-off
# non-DXVK diagnostic run -- do not set this for normal test runs. Set
# CNA_D3D9_SKIP_DXVK_GATE=1 for a binary that legitimately never creates a D3D9
# device at all (e.g. a future DirectX9_Common pure-function mapping-table check,
# D9-23) -- such a run never prints a DXVK line for entirely unrelated,
# legitimate reasons, and this gate would otherwise misreport it as a WineD3D
# fallback.
#
# NOTE: do NOT route the offline shader compiler (D9-71, fxc_tool.exe) through
# this script. Compiling a shader never opens a D3D9 device, so the DXVK
# marker never appears and this gate would reject a legitimate compile run for
# an unrelated reason. fxc_tool.exe also needs ~/.wine-cna-d3d9-spike (the real
# Microsoft d3dcompiler_47.dll), not this script's DXVK-carrying prefix -- see
# dx9-spike/README.md.
set -uo pipefail

if [ "$#" -lt 1 ]; then
    echo "usage: $0 <path-to.exe> [args...]" >&2
    exit 2
fi

export WINEPREFIX="${CNA_D3D9_WINEPREFIX:-$HOME/.wine-cna-d3d11}"
export WINEDEBUG="${WINEDEBUG:--all}"
export DXVK_LOG_LEVEL="${DXVK_LOG_LEVEL:-info}"

if [ ! -f "${WINEPREFIX}/system.reg" ]; then
    echo "error: WINEPREFIX '${WINEPREFIX}' is not an initialized Wine prefix." >&2
    echo "Set it up first: WINEPREFIX=${WINEPREFIX} wineboot --init && WINEPREFIX=${WINEPREFIX} dxvk-setup install" >&2
    exit 1
fi

logFile="$(mktemp "${TMPDIR:-/tmp}/cna-d3d9-dxvk-log.XXXXXX")"
trap 'rm -f "$logFile"' EXIT

wine "$@" 2>&1 | tee "$logFile"
wineExit="${PIPESTATUS[0]}"

if [ "${CNA_D3D9_ALLOW_WINED3D:-0}" != "1" ] && [ "${CNA_D3D9_SKIP_DXVK_GATE:-0}" != "1" ]; then
    if ! grep -Eq 'DXVK: [0-9]+\.[0-9]+' "$logFile"; then
        echo "error: D9-5 gate failed -- no 'DXVK: <version>' log line was seen in this run's" >&2
        echo "output, so this looks like a silent WineD3D fallback rather than a real DXVK-backed" >&2
        echo "Direct3D 9 run. This invalidates any D3D9-semantics pixel-test result from this" >&2
        echo "run -- fix the DXVK install rather than ignoring this." >&2
        echo "(Set CNA_D3D9_ALLOW_WINED3D=1 to deliberately bypass this for a one-off non-DXVK diagnostic run.)" >&2
        exit 3
    fi
fi

exit "$wineExit"
