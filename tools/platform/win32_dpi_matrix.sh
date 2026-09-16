#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# plans/plan_win32_native_validation.md WINNATIVE-0016: what CNA reports at display scalings other
# than 100 %.
#
# This is one of the things a real Windows machine is for. Under Wine there is one monitor at a
# fixed DPI, so every DPI assertion the suite makes is an assertion about 96. Windows changes the
# scaling for a whole session, so the only honest way to test several is to set it, restart, and
# measure -- which is what this does, from Linux, without a person at the keyboard.
#
# Windows reads HKCU\Control Panel\Desktop\LogPixels (with Win8DpiScaling=1) at logon, so each
# scaling needs a restart. The guest has autologon, so it comes back to a logged-on desktop by
# itself and the harness runs there through win32_run_interactive.ps1.
#
# The original scaling is restored at the end, including after a failure or an interrupt: leaving
# someone's machine at 200 % is not an acceptable side effect of a test.
#
# Usage: win32_dpi_matrix.sh [100 125 150 200]

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EXEC="$HERE/windows_vm_exec.sh"
HARNESS='C:\cna\build\win32-standalone\cna_win32_native_desktop.exe'

SCALES=("$@")
[ ${#SCALES[@]} -gt 0 ] || SCALES=(100 125 150 200)

say() { echo "== $*" >&2; }

read_scale() {
  "$EXEC" --stdin <<'PS' 2>/dev/null | tr -d '\r' | tail -1
$d = Get-ItemProperty 'HKCU:\Control Panel\Desktop' -ErrorAction SilentlyContinue
if ($null -ne $d.LogPixels) { $d.LogPixels } else { 96 }
PS
}

set_scale() {
  local dpi="$1"
  "$EXEC" --stdin >/dev/null 2>&1 <<PS
Set-ItemProperty 'HKCU:\Control Panel\Desktop' -Name LogPixels -Value $dpi -Type DWord
Set-ItemProperty 'HKCU:\Control Panel\Desktop' -Name Win8DpiScaling -Value 1 -Type DWord
PS
}

# windows_vm_sync.sh ships SOURCE, not binaries. Running the matrix against a harness built before
# the last sync is how two runs of this script silently measured the previous binary -- the
# --dpi-aware switch was simply ignored, and the result looked like a finding rather than a stale
# executable. Rebuild first, every time; it is a no-op when nothing changed.
say "rebuilding the harness in the guest"
"$EXEC" --stdin >/dev/null 2>&1 <<'PS'
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
cmd /c "`"$(Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat')`" >nul 2>&1 && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($matches[1])" -Value $matches[2] }
}
cmake --build C:/cna/build/win32-standalone --parallel 6 | Out-Null
PS

ORIGINAL="$(read_scale)"
say "original LogPixels: ${ORIGINAL:-96}"

restore() {
  say "restoring LogPixels to ${ORIGINAL:-96}"
  set_scale "${ORIGINAL:-96}"
  "$EXEC" --reboot >/dev/null 2>&1
  say "restored"
}
trap restore EXIT INT TERM

overall=0
for scale in "${SCALES[@]}"; do
  dpi=$(( 96 * scale / 100 ))
  say "--- $scale % (LogPixels $dpi) ---"
  set_scale "$dpi"
  "$EXEC" --reboot >/dev/null 2>&1 || { say "the guest did not come back"; overall=1; continue; }

  "$EXEC" --stdin <<PS
\$ProgressPreference='SilentlyContinue'
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
& C:\src\cna\tools\platform\win32_run_interactive.ps1 \`
    -Exe '$HARNESS' -Arguments @('--dpi-aware','--check','dpi','--check','displays','--check','host-ownership') \`
    -WorkingDirectory 'C:\cna\build\win32-standalone' \`
    -Name 'dpi-$scale' -TimeoutSeconds 300
"harness exit: \$LASTEXITCODE"
PS
  [ $? -eq 0 ] || overall=1
done

exit "$overall"
