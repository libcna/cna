# SPDX-License-Identifier: MS-PL
#
# plans/plan_win32_native_validation.md WINNATIVE-0021: a real CNA application, run for a sustained
# period while the desktop does things to it.
#
# The stress harness answers "do ten thousand repetitions of one operation cost anything". This
# answers a different question: does a normal application, rendering frames, drifting upward in any
# resource while a user minimises it, restores it, resizes it, Alt+Tabs away and back? Those are
# the events a game actually receives, and they arrive through paths -- WM_ACTIVATE, WM_SIZE,
# WM_SYSCOMMAND, the swap chain's response to a resize -- that a tight loop never exercises.
#
# It MUST run on the interactive desktop (win32_run_interactive.ps1): everything it does depends on
# the window being real, foreground and on a window station with a shell.
#
# Usage (inside the interactive session):
#   win32_soak.ps1 -Exe C:\cna\build\...\cna_demo_2d.exe [-Seconds 300] [-Renderer DIRECTX11]

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $Exe,
    [string]   $WorkingDirectory = '',
    [string[]] $Arguments = @(),
    [int]      $Seconds  = 300,
    [int]      $SampleSeconds = 10,
    [string]   $Renderer = '',
    [string]   $ReportDir = 'C:\cna\report\soak'
)

$ErrorActionPreference = 'Continue'
$ProgressPreference = 'SilentlyContinue'
if (-not $WorkingDirectory) { $WorkingDirectory = Split-Path -Parent $Exe }
New-Item -ItemType Directory -Force -Path $ReportDir | Out-Null

if (-not (Test-Path $Exe)) { Write-Output "soak: no such executable: $Exe"; exit 2 }

Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Soak {
  [DllImport("user32.dll")] public static extern uint GetGuiResources(IntPtr p, uint flags);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
  [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, int msg, IntPtr w, IntPtr l);
  public const uint GR_GDIOBJECTS = 0, GR_USEROBJECTS = 1;
  public const int SW_MINIMIZE = 6, SW_RESTORE = 9, SW_MAXIMIZE = 3;
  public const uint SWP_NOMOVE = 0x0002, SWP_NOZORDER = 0x0004, SWP_NOACTIVATE = 0x0010;
}
"@

if ($Renderer) { $env:CNA_GRAPHICS_RENDERER = $Renderer }
Write-Output "soak: $Exe $($Arguments -join ' ')  renderer=$(if ($Renderer) { $Renderer } else { '<default>' })  for $Seconds s"

$start = @{ FilePath = $Exe; WorkingDirectory = $WorkingDirectory; PassThru = $true }
if ($Arguments.Count -gt 0) { $start.ArgumentList = $Arguments }
$app = Start-Process @start
$null = $app.Handle

for ($i = 0; $i -lt 100 -and $app.MainWindowHandle -eq [IntPtr]::Zero -and -not $app.HasExited; $i++) {
    Start-Sleep -Milliseconds 200; $app.Refresh()
}
if ($app.HasExited) { Write-Output "soak: the application exited immediately (code $($app.ExitCode))"; exit 1 }
$hwnd = $app.MainWindowHandle
Write-Output "soak: window 0x$('{0:X}' -f [int64]$hwnd)"

$samples = New-Object System.Collections.Generic.List[object]
function Sample([string] $phase) {
    $app.Refresh()
    if ($app.HasExited) { return }
    $s = [pscustomobject]@{
        second   = [int]((Get-Date) - $started).TotalSeconds
        phase    = $phase
        handles  = $app.HandleCount
        user     = [Soak]::GetGuiResources($app.Handle, [Soak]::GR_USEROBJECTS)
        gdi      = [Soak]::GetGuiResources($app.Handle, [Soak]::GR_GDIOBJECTS)
        threads  = $app.Threads.Count
        wsKb     = [int]($app.WorkingSet64 / 1KB)
        privateKb= [int]($app.PrivateMemorySize64 / 1KB)
    }
    $samples.Add($s)
    Write-Output ("  {0,5}s {1,-12} handles {2,5}  USER {3,4}  GDI {4,4}  threads {5,3}  WS {6,8} KB  private {7,8} KB" -f `
        $s.second, $s.phase, $s.handles, $s.user, $s.gdi, $s.threads, $s.wsKb, $s.privateKb)
}

$started = Get-Date
Start-Sleep -Seconds 3
Sample 'settled'

# The cycle a user puts a window through. Each step is a different message path into the backend,
# and the swap chain has to survive all of them.
$actions = @(
    @{ name = 'minimize'; do = { [void][Soak]::ShowWindow($hwnd, [Soak]::SW_MINIMIZE) } },
    @{ name = 'restore';  do = { [void][Soak]::ShowWindow($hwnd, [Soak]::SW_RESTORE); [void][Soak]::SetForegroundWindow($hwnd) } },
    @{ name = 'resize-s'; do = { [void][Soak]::SetWindowPos($hwnd, [IntPtr]::Zero, 0, 0, 640, 480, [Soak]::SWP_NOMOVE -bor [Soak]::SWP_NOZORDER -bor [Soak]::SWP_NOACTIVATE) } },
    @{ name = 'resize-l'; do = { [void][Soak]::SetWindowPos($hwnd, [IntPtr]::Zero, 0, 0, 1280, 800, [Soak]::SWP_NOMOVE -bor [Soak]::SWP_NOZORDER -bor [Soak]::SWP_NOACTIVATE) } },
    @{ name = 'maximize'; do = { [void][Soak]::ShowWindow($hwnd, [Soak]::SW_MAXIMIZE) } },
    @{ name = 'restore2'; do = { [void][Soak]::ShowWindow($hwnd, [Soak]::SW_RESTORE) } },
    # Focus away and back, which is what Alt+Tab is from the window's point of view.
    @{ name = 'focus-away'; do = { $other = Start-Process notepad -PassThru; Start-Sleep -Milliseconds 800; Stop-Process -Id $other.Id -Force -ErrorAction SilentlyContinue } },
    @{ name = 'focus-back'; do = { [void][Soak]::SetForegroundWindow($hwnd) } }
)

$deadline = $started.AddSeconds($Seconds)
$cycle = 0
while ((Get-Date) -lt $deadline -and -not $app.HasExited) {
    $action = $actions[$cycle % $actions.Count]
    & $action.do
    Start-Sleep -Seconds 2
    if ((($cycle * 2) % $SampleSeconds) -lt 2) { Sample $action.name }
    $cycle++
    $app.Refresh()
}

Sample 'final'
if (-not $app.HasExited) {
    # A close REQUEST, so the application's own shutdown path runs -- the thing a kill would skip.
    [void][Soak]::SendMessageW($hwnd, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)   # WM_CLOSE
    if (-not $app.WaitForExit(15000)) {
        Write-Output 'soak: the application did not exit on WM_CLOSE within 15 s; killing it'
        Stop-Process -Id $app.Id -Force -ErrorAction SilentlyContinue
    } else {
        Write-Output "soak: exited cleanly on WM_CLOSE, code $($app.ExitCode)"
    }
}

$samples | ConvertTo-Json -Depth 3 | Set-Content (Join-Path $ReportDir 'soak.json')

# The verdict. Windows caches and the CRT's heap both grow early and then flatten, so the first
# sample is not the baseline -- the comparison is between the settled middle and the end.
if ($samples.Count -lt 3) { Write-Output 'soak: too few samples to judge'; exit 1 }
$baseline = $samples[[int]($samples.Count / 3)]
$final    = $samples[-1]
$verdict = $true
function Judge([string] $what, [double] $from, [double] $to, [double] $tolerance) {
    $grew = $to -gt ($from + $tolerance)
    if ($grew) { $script:verdict = $false }
    Write-Output ("  {0} {1,-10} {2} -> {3} (tolerance +{4})" -f $(if ($grew) { 'LEAK' } else { 'ok  ' }), $what, $from, $to, $tolerance)
}
Write-Output ''
# Parenthesised: without them PowerShell binds -f as a parameter of Write-Output rather than as the
# format operator, and prints the template literally followed by "-f" and the arguments on lines of
# their own. The verdict header did exactly that until this run.
Write-Output ("verdict (settled sample at {0}s vs final at {1}s):" -f $baseline.second, $final.second)
Judge 'handles'  $baseline.handles $final.handles 24
Judge 'USER'     $baseline.user    $final.user    8
Judge 'GDI'      $baseline.gdi     $final.gdi     12
Judge 'threads'  $baseline.threads $final.threads 2
# Memory is judged proportionally: a frame-rendering application legitimately moves a few MB.
Judge 'privateKb' $baseline.privateKb $final.privateKb ([math]::Max(8192, $baseline.privateKb * 0.25))

Write-Output ''
Write-Output $(if ($verdict) { 'RESULT: nothing grew beyond tolerance over the soak' } else { 'RESULT: a resource grew' })
exit $(if ($verdict) { 0 } else { 1 })
