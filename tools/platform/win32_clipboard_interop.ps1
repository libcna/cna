# SPDX-License-Identifier: MS-PL
#
# plans/plan_win32_native_validation.md WINNATIVE-0018: the CNA clipboard against a real Windows
# application.
#
# cna_win32_native_desktop's `clipboard` check already proves that what CNA writes is readable
# through the raw Win32 clipboard API and vice versa -- but both halves of that are the same
# process. The Windows clipboard is a per-window-station rendezvous between *processes*, with
# ownership transfer and format negotiation in the middle, and the only way to exercise that is to
# put another program on the other side.
#
# Notepad is the right other program: present on every Windows, uses CF_UNICODETEXT, and old enough
# to use the clipboard exactly the way the API describes.
#
# It is driven through its edit control with WM_PASTE / WM_COPY / WM_SETTEXT / WM_GETTEXT, not
# through SendKeys. SendKeys was tried first and is the wrong tool twice over: it cannot type
# characters outside the current keyboard layout (a Czech sample arrived as "rcka dn", a Greek one
# as "alpha a omega O"), and it depends on which window happens to have the foreground, which under
# a parallel build is a race. WM_PASTE and WM_COPY use the real system clipboard exactly as a
# keystroke would -- they are what the keystroke turns into -- while being deterministic and
# Unicode-clean.
#
# This MUST run on the interactive desktop -- see win32_run_interactive.ps1 -- because a window
# station has its own clipboard and Notepad is on the user's.
#
# Usage:
#   win32_clipboard_interop.ps1 -Harness C:\cna\build\win32-standalone\cna_win32_native_desktop.exe

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $Harness,
    [string] $ScratchDir = 'C:\cna\tmp\clipboard'
)

$ErrorActionPreference = 'Continue'
$ProgressPreference = 'SilentlyContinue'
New-Item -ItemType Directory -Force -Path $ScratchDir | Out-Null
$putFile = Join-Path $ScratchDir 'put.txt'
$getFile = Join-Path $ScratchDir 'get.txt'
# No BOM: the harness reads the file as raw UTF-8 bytes and puts exactly those on the clipboard,
# so a BOM would become three characters of payload.
$utf8 = New-Object System.Text.UTF8Encoding($false)

function Set-ClipboardThroughCna([string] $text) {
    [System.IO.File]::WriteAllText($putFile, $text, $utf8)
    & $Harness --clipboard-put-file $putFile | Out-Null
    return $LASTEXITCODE -eq 0
}

function Get-ClipboardThroughCna() {
    if (Test-Path $getFile) { Remove-Item $getFile -Force }
    & $Harness --clipboard-get-file $getFile | Out-Null
    if (-not (Test-Path $getFile)) { return $null }
    return [System.IO.File]::ReadAllText($getFile, $utf8)
}

if (-not (Test-Path $Harness)) { Write-Output "harness not found: $Harness"; exit 2 }

Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class Edit {
  public const int WM_SETTEXT = 0x000C, WM_GETTEXT = 0x000D, WM_GETTEXTLENGTH = 0x000E;
  public const int WM_COPY = 0x0301, WM_PASTE = 0x0302, WM_CLEAR = 0x0303, EM_SETSEL = 0x00B1;
  [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
  public static extern IntPtr FindWindowExW(IntPtr parent, IntPtr after, string cls, string title);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)]
  public static extern IntPtr SendMessageW(IntPtr h, int msg, IntPtr w, string l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)]
  public static extern IntPtr SendMessageW(IntPtr h, int msg, IntPtr w, StringBuilder l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)]
  public static extern IntPtr SendMessageW(IntPtr h, int msg, IntPtr w, IntPtr l);
  public static string GetText(IntPtr h) {
    int n = (int)SendMessageW(h, WM_GETTEXTLENGTH, IntPtr.Zero, IntPtr.Zero);
    var sb = new StringBuilder(n + 2);
    SendMessageW(h, WM_GETTEXT, (IntPtr)(n + 1), sb);
    return sb.ToString();
  }
}
"@

$failures = 0
function Check([string] $name, [bool] $ok, [string] $detail) {
    Write-Output ("  {0} {1,-42} {2}" -f $(if ($ok) { 'ok  ' } else { 'FAIL' }), $name, $detail)
    if (-not $ok) { $script:failures++ }
}

function Open-NotepadEdit {
    $p = Start-Process notepad -PassThru
    $edit = [IntPtr]::Zero
    for ($i = 0; $i -lt 100 -and $edit -eq [IntPtr]::Zero; $i++) {
        Start-Sleep -Milliseconds 100
        $p.Refresh()
        if ($p.MainWindowHandle -ne [IntPtr]::Zero) {
            # Windows 10's Notepad hosts a plain EDIT; a future one may host RichEdit instead, so
            # both are looked for rather than assumed.
            foreach ($cls in @('Edit', 'RichEditD2DPT')) {
                $edit = [Edit]::FindWindowExW($p.MainWindowHandle, [IntPtr]::Zero, $cls, $null)
                if ($edit -ne [IntPtr]::Zero) { break }
            }
        }
    }
    return @{ Process = $p; Edit = $edit }
}

function Close-Notepad($n) {
    # Killed rather than closed: a close raises the save-changes dialog, and a modal dialog would
    # block the run.
    try { Stop-Process -Id $n.Process.Id -Force -ErrorAction SilentlyContinue } catch { }
    Start-Sleep -Milliseconds 200
}

$samples = @(
    @{ name = 'ascii';       text = 'the quick brown fox jumps over the lazy dog' },
    @{ name = 'czech';       text = [string]([char]0x0159 + [char]0x00ED + [char]0x010D + 'ka ' + [char]0x017E + [char]0x00E1 + 'dn' + [char]0x00FD) },
    @{ name = 'greek-dash';  text = [string]('alpha ' + [char]0x03B1 + ' omega ' + [char]0x03A9 + ' dash ' + [char]0x2014) },
    @{ name = 'emoji';       text = [string]([char]0xD83D + [char]0xDCCB + ' clipboard ' + [char]0xD83C + [char]0xDF00) },
    @{ name = 'long';        text = ('abcdefghij' * 500) }
)

Write-Output 'CNA -> Notepad (CNA owns the clipboard, Notepad pastes from it):'
foreach ($s in $samples) {
    if (-not (Set-ClipboardThroughCna $s.text)) { Check "cnaToNotepad.$($s.name)" $false 'the harness could not set the clipboard'; continue }
    $n = Open-NotepadEdit
    if ($n.Edit -eq [IntPtr]::Zero) { Check "cnaToNotepad.$($s.name)" $false 'no Notepad edit control'; Close-Notepad $n; continue }
    [void][Edit]::SendMessageW($n.Edit, [Edit]::WM_PASTE, [IntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 250
    $pasted = [Edit]::GetText($n.Edit)
    Close-Notepad $n
    $ok = $pasted -ceq $s.text
    Check "cnaToNotepad.$($s.name)" $ok $(if ($ok) { "Notepad pasted $($pasted.Length) chars CNA put there" } else { "Notepad holds $($pasted.Length) chars, expected $($s.text.Length)" })
}

Write-Output 'Notepad -> CNA (Notepad owns the clipboard, CNA reads it):'
foreach ($s in $samples) {
    # Hand the clipboard something else first, so a pass cannot be the previous value still there.
    [void](Set-ClipboardThroughCna "sentinel-$($s.name)")
    $n = Open-NotepadEdit
    if ($n.Edit -eq [IntPtr]::Zero) { Check "notepadToCna.$($s.name)" $false 'no Notepad edit control'; Close-Notepad $n; continue }
    [void][Edit]::SendMessageW($n.Edit, [Edit]::WM_SETTEXT, [IntPtr]::Zero, $s.text)
    [void][Edit]::SendMessageW($n.Edit, [Edit]::EM_SETSEL, [IntPtr]0, [IntPtr](-1))
    [void][Edit]::SendMessageW($n.Edit, [Edit]::WM_COPY, [IntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 350
    $read = Get-ClipboardThroughCna
    Close-Notepad $n
    $ok = ($null -ne $read) -and ($read.TrimEnd("`r", "`n") -ceq $s.text)
    Check "notepadToCna.$($s.name)" $ok $(if ($ok) { "CNA read $($s.text.Length) chars another process owned" } else { "CNA read '$read'" })
}

Write-Output ''
Write-Output $(if ($failures -eq 0) { 'RESULT: the system clipboard is shared with a real Windows application in both directions' }
               else { "RESULT: $failures clipboard interop check(s) failed" })
exit $(if ($failures -eq 0) { 0 } else { 1 })
