# SPDX-License-Identifier: MS-PL
#
# plans/plan_win32_native_validation.md WINNATIVE-0018: the CNA clipboard against a real Windows
# application.
#
# cna_win32_native_desktop's `clipboard` check already proves that what CNA writes is readable
# through the raw Win32 clipboard API and vice versa -- but both halves of that are the same
# process. The Windows clipboard is a per-window-station rendezvous between *processes*, with
# delayed rendering, ownership transfer and format negotiation in the middle, and the only way to
# exercise that is to put another program on the other side.
#
# Notepad is the right other program: it is present on every Windows, it takes CF_UNICODETEXT, and
# it is old enough to use the clipboard exactly the way the API describes.
#
# Both directions are checked:
#   CNA -> Notepad   CNA sets the text; Notepad pastes it; the pasted text is read back out.
#   Notepad -> CNA   Notepad copies its own text; CNA reads it.
#
# This MUST run on the interactive desktop -- see win32_run_interactive.ps1 for why -- because a
# window station has its own clipboard and Notepad is on the user's.
#
# Usage:
#   win32_clipboard_interop.ps1 -Harness C:\cna\build\win32-standalone\cna_win32_native_desktop.exe

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $Harness,
    [string] $OutDir = 'C:\cna\report\clipboard'
)

$ErrorActionPreference = 'Continue'
$ProgressPreference = 'SilentlyContinue'
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$failures = 0
function Check([string] $name, [bool] $ok, [string] $detail) {
    Write-Output ("  {0} {1,-42} {2}" -f $(if ($ok) { 'ok  ' } else { 'FAIL' }), $name, $detail)
    if (-not $ok) { $script:failures++ }
}

if (-not (Test-Path $Harness)) { Write-Output "harness not found: $Harness"; exit 2 }

Add-Type -AssemblyName System.Windows.Forms
$shell = New-Object -ComObject WScript.Shell

function Start-Notepad {
    $p = Start-Process notepad -PassThru
    # Notepad has to own the foreground before SendKeys means anything.
    for ($i = 0; $i -lt 50 -and -not $p.MainWindowHandle; $i++) { Start-Sleep -Milliseconds 100; $p.Refresh() }
    Start-Sleep -Milliseconds 700
    $null = $shell.AppActivate($p.Id)
    Start-Sleep -Milliseconds 400
    return $p
}

function Stop-Notepad($p) {
    # Killed rather than closed: a close would raise the "save changes?" dialog and block the run.
    try { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue } catch { }
    Start-Sleep -Milliseconds 300
}

$samples = @(
    @{ name = 'ascii'; text = 'the quick brown fox jumps over the lazy dog' },
    @{ name = 'czech'; text = [char]0x0159 + [char]0x00ED + [char]0x010D + 'ka ' + [char]0x017E + [char]0x00E1 + 'dn' + [char]0x00FD },
    @{ name = 'unicode-mix'; text = 'alpha ' + [char]0x03B1 + ' omega ' + [char]0x03A9 + ' dash ' + [char]0x2014 }
)

Write-Output 'CNA -> Notepad (CNA sets the clipboard, Notepad pastes it):'
foreach ($sample in $samples) {
    & $Harness --clipboard-put $sample.text | Out-Null
    if ($LASTEXITCODE -ne 0) { Check "cnaToNotepad.$($sample.name)" $false 'the harness could not set the clipboard'; continue }

    $notepad = Start-Notepad
    $shell.SendKeys('^v')                # paste what CNA put there
    Start-Sleep -Milliseconds 600
    $shell.SendKeys('^a')                # select all of it back
    Start-Sleep -Milliseconds 300
    $shell.SendKeys('^c')                # and copy it, so Notepad is now the clipboard owner
    Start-Sleep -Milliseconds 600
    $roundTripped = (& $Harness --clipboard-get) -join "`n"
    Stop-Notepad $notepad

    $ok = $roundTripped.Trim() -ceq $sample.text
    Check "cnaToNotepad.$($sample.name)" $ok $(if ($ok) { "$($sample.text.Length) chars survived the round trip through Notepad" } else { "got '$roundTripped'" })
}

Write-Output 'Notepad -> CNA (Notepad types and copies, CNA reads):'
foreach ($sample in $samples) {
    # Clear the clipboard first, so a pass cannot be the previous value still sitting there.
    [System.Windows.Forms.Clipboard]::Clear()
    Start-Sleep -Milliseconds 200

    $notepad = Start-Notepad
    # SendKeys reserves + ^ % ~ ( ) { } [ ]; these samples contain none of them, and the escaping
    # rule is deliberately not reimplemented here -- a harness that needs it is testing SendKeys.
    $shell.SendKeys($sample.text)
    Start-Sleep -Milliseconds 600
    $shell.SendKeys('^a')
    Start-Sleep -Milliseconds 300
    $shell.SendKeys('^c')
    Start-Sleep -Milliseconds 700
    $read = (& $Harness --clipboard-get) -join "`n"
    Stop-Notepad $notepad

    $ok = $read.Trim() -ceq $sample.text
    Check "notepadToCna.$($sample.name)" $ok $(if ($ok) { "CNA read $($read.Trim().Length) chars written by another process" } else { "got '$($read.Trim())'" })
}

Write-Output ''
Write-Output $(if ($failures -eq 0) { 'RESULT: the clipboard is shared with a real Windows application in both directions' }
               else { "RESULT: $failures clipboard interop check(s) failed" })
exit $(if ($failures -eq 0) { 0 } else { 1 })
