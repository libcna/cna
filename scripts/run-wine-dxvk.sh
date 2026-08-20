#!/usr/bin/env bash
# plans/plan_dx.md DX-3: run a Windows cross-compiled .exe (D3D11 renderer) under Wine
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
# plans/plan_dx.md DX-85: this wrapper also automatically ASSERTS that DXVK itself
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
#
# Set CNA_D3D11_VIRTUAL_DESKTOP to a Wine desktop specification such as
# `CNA,1280x1024` when the caller runs below Xvfb. Wine's X11 driver does not expose a plain
# Xvfb screen as a Windows display on every host, whereas Wine's own desktop does. This mode uses
# a short batch trampoline so the detached desktop process still returns the viewer's stdout and
# exact exit status to CI/L7 callers.
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
virtualRoot=""
batchFile=""
childLog=""
exitFile=""
launchLog=""
launcherPid=""

cleanup()
{
    if [ -n "$virtualRoot" ]; then
        if [ -n "$launcherPid" ]; then
            kill "$launcherPid" >/dev/null 2>&1 || true
        fi
        WINEPREFIX="$WINEPREFIX" wineserver -k >/dev/null 2>&1 || true
        rm -f "$batchFile" "$childLog" "$exitFile" "$launchLog"
        rmdir "$virtualRoot" 2>/dev/null || true
    fi
    rm -f "$logFile"
}
trap cleanup EXIT

toWinePath()
{
    local value="$1"
    if [[ "$value" == /* ]]; then
        value="Z:${value//\//\\}"
    fi
    printf '%s' "$value"
}

fromWineZPath()
{
    local value="$1"
    if [[ "$value" =~ ^[Zz]:\\ ]]; then
        value="${value:2}"
        printf '%s' "${value//\\//}"
    elif [[ "$value" == /* ]]; then
        printf '%s' "$value"
    fi
}

appendBatchArgument()
{
    local value
    value="$(toWinePath "$1")"
    if [[ "$value" == *'"'* || "$value" == *$'\n'* || "$value" == *$'\r'* ]]; then
        echo "error: virtual-desktop argument contains an unsupported quote or newline" >&2
        exit 2
    fi
    value="${value//%/%%}"
    printf '"%s" ' "$value" >> "$batchFile"
}

if [ -n "${CNA_D3D11_VIRTUAL_DESKTOP:-}" ]; then
    if [[ ! "${CNA_D3D11_VIRTUAL_DESKTOP}" =~ ^[A-Za-z0-9_-]+,[0-9]+x[0-9]+$ ]]; then
        echo "error: CNA_D3D11_VIRTUAL_DESKTOP must look like CNA,1280x1024" >&2
        exit 2
    fi

    virtualRoot="$(mktemp -d "${TMPDIR:-/tmp}/cna-d3d11-virtual.XXXXXX")"
    batchFile="${virtualRoot}/run.bat"
    childLog="${virtualRoot}/child.log"
    exitFile="${virtualRoot}/exit.txt"
    launchLog="${virtualRoot}/launch.log"
    batchWine="$(toWinePath "$batchFile")"
    childLogWine="$(toWinePath "$childLog")"
    exitFileWine="$(toWinePath "$exitFile")"
    captureHost=""
    expectCapturePath=0
    for argument in "$@"; do
        if [ "$expectCapturePath" -eq 1 ]; then
            captureHost="$(fromWineZPath "$argument")"
            break
        fi
        if [ "$argument" = "--capture" ]; then
            expectCapturePath=1
        fi
    done
    if [ -n "$captureHost" ] && [ -e "$captureHost" ]; then
        # Never accept a stale output as evidence that the detached GUI child completed.
        captureHost=""
    fi

    printf '@echo off\r\n' > "$batchFile"
    for argument in "$@"; do
        appendBatchArgument "$argument"
    done
    printf '> "%s" 2>&1\r\n' "$childLogWine" >> "$batchFile"
    printf '> "%s" echo %%ERRORLEVEL%%\r\n' "$exitFileWine" >> "$batchFile"

    wine explorer "/desktop=${CNA_D3D11_VIRTUAL_DESKTOP}" cmd.exe /d /c "$batchWine" \
        > "$launchLog" 2>&1 &
    launcherPid=$!

    completed=0
    captureCompleted=0
    diagnosticCompleted=0
    previousCaptureSize=-1
    stableCaptureChecks=0
    for ((attempt = 0; attempt < 600; ++attempt)); do
        if [ -n "$captureHost" ] && [ -s "$captureHost" ]; then
            captureSize="$(stat -c '%s' "$captureHost" 2>/dev/null || printf '0')"
            if [ "$captureSize" = "$previousCaptureSize" ]; then
                stableCaptureChecks=$((stableCaptureChecks + 1))
            else
                stableCaptureChecks=0
                previousCaptureSize="$captureSize"
            fi
            if [ "$stableCaptureChecks" -ge 3 ]; then
                captureCompleted=1
                break
            fi
        fi
        if [ -n "$captureHost" ]; then
            # `start /wait` reports the desktop shell's early status on Wine, not necessarily the
            # still-running GUI child. For capture invocations, the viewer's stable PNG or its
            # top-level diagnostic is authoritative instead.
            if [ -s "$childLog" ] && grep -q '^cna-gltf-viewer:' "$childLog"; then
                diagnosticCompleted=1
                break
            fi
        elif [ -s "$exitFile" ]; then
            completed=1
            break
        fi
        sleep 0.05
    done

    if [ "$captureCompleted" -eq 1 ]; then
        # Let the detached child execute its final Exit() frame and flush redirected diagnostics.
        sleep 1
    fi
    cat "$launchLog" "$childLog" 2>/dev/null | tee "$logFile"
    if [ "$captureCompleted" -eq 1 ]; then
        # Wine's virtual-desktop shell keeps some GUI executables attached after their requested
        # one-shot capture and Exit(). A stable, newly-created PNG is the completion contract for
        # capture calls; cleanup terminates only this isolated Wine prefix's lingering desktop.
        wineExit=0
    elif [ "$diagnosticCompleted" -eq 1 ]; then
        wineExit=1
    elif [ "$completed" -ne 1 ]; then
        echo "error: Wine virtual-desktop child did not finish within 30 seconds" >&2
        wineExit=124
    else
        wineExit="$(tr -cd '0-9-' < "$exitFile")"
        if [[ ! "$wineExit" =~ ^-?[0-9]+$ ]]; then
            echo "error: Wine virtual-desktop child returned an invalid exit marker" >&2
            wineExit=125
        fi
    fi
else
    wine "$@" 2>&1 | tee "$logFile"
    wineExit="${PIPESTATUS[0]}"
fi

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
