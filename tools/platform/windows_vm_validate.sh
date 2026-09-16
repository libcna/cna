#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# One command, from Linux, that validates CNA's Win32 backend on real Windows.
#
#   tools/platform/windows_vm_validate.sh [--steps <list>] [--keep-going] [-- <extra ps1 args>]
#
# It starts the VirtualBox Windows guest if it is not running, waits for SSH, puts this working
# copy's exact HEAD commit into the guest, runs tools/platform/validate_win32_native.ps1 there,
# streams its output back, copies the report out to reports/win32-native/<timestamp>/, and exits
# with the script's exit code. Nothing about it needs a person at a keyboard, and nothing about it
# needs a password: see windows_vm_exec.sh for the SSH arrangement.
#
# --steps is passed straight through to the PowerShell script; run it with -Steps help to see the
# list. The default is everything that can run unattended.
#
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
EXEC="$HERE/windows_vm_exec.sh"
SYNC="$HERE/windows_vm_sync.sh"

STEPS=""
KEEP_GOING=0
EXTRA=()
while [ $# -gt 0 ]; do
  case "$1" in
    --steps)      STEPS="$2"; shift 2 ;;
    --keep-going) KEEP_GOING=1; shift ;;
    --)           shift; EXTRA=("$@"); break ;;
    *)            echo "windows_vm_validate: unknown option $1" >&2; exit 2 ;;
  esac
done

say() { echo "== $*" >&2; }

say "1/4 starting the VM and waiting for SSH"
"$EXEC" --start || exit 2

say "2/4 syncing $(git -C "$ROOT" rev-parse --short HEAD) into the guest"
"$SYNC" || exit 2

stamp="$(date -u +%Y%m%dT%H%M%SZ)"
outdir="$ROOT/reports/win32-native/$stamp"
mkdir -p "$outdir"

say "3/4 running validate_win32_native.ps1 in the guest"
guest_out="C:/cna/report/$stamp"
set +e
"$EXEC" -- "powershell -NoProfile -ExecutionPolicy Bypass -File C:/src/cna/tools/platform/validate_win32_native.ps1 \
  -SourceDir C:/src/cna -BuildRoot C:/cna/build -OutDir $guest_out \
  $( [ -n "$STEPS" ] && echo "-Steps $STEPS" ) $( [ "$KEEP_GOING" = 1 ] && echo '-KeepGoing' ) ${EXTRA[*]:-}" \
  2>&1 | tee "$outdir/console.log"
rc=${PIPESTATUS[0]}
set -e

say "4/4 collecting the report into $outdir"
"$EXEC" --pull "$guest_out/." "$outdir/" >/dev/null 2>&1 || say "(no report files to collect)"

say "exit $rc; report in $outdir"
exit "$rc"
