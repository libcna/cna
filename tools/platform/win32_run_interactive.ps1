# SPDX-License-Identifier: MS-PL
#
# Run a program on the REAL interactive Windows desktop, from a session that is not on it.
#
# Why this exists
# ---------------
# Windows OpenSSH gives an SSH client a logon session of its own -- session 0, on a window station
# named `Service-0x0-<luid>$`, with its own `Default` desktop. That is a perfectly good place to
# compile and to run tests that never touch the screen, and a completely wrong place to judge any
# of this:
#
#   * window visibility, activation, focus and Z-order -- there is no shell there, no taskbar, and
#     nothing to Alt+Tab between;
#   * the clipboard -- it is per window station, so a "CNA to Notepad" round trip run from SSH
#     would be talking to a clipboard no Notepad can see;
#   * monitors and DPI -- an invisible window station reports a default display, not the one the
#     user is looking at;
#   * anything to do with the foreground-lock rules, which are about the interactive session.
#
# Measured on the validation VM: an SSH process reports session 0, window station
# `Service-0x0-2346c6$`, while the logged-on desktop is session 1. So every check in
# plans/plan_win32_native_validation.md that is about the desktop is launched through here.
#
# How
# ---
# Task Scheduler, registered through its COM API with logon type `InteractiveToken` (3). That logon
# type takes the token of whoever is interactively logged on at run time, which means the task
# needs no stored password -- nothing secret is written down here or anywhere else. The task is
# registered, run, waited for, and deleted.
#
# Usage:
#   win32_run_interactive.ps1 -Exe C:\path\to\thing.exe [-Arguments a,b] [-WorkingDirectory dir]
#                             [-OutDir C:\cna\report] [-Name tag] [-TimeoutSeconds 900]
#                             [-Environment 'NAME=value','OTHER=value']
#
# -Environment exists because the task runs with the logged-on user's environment, not this
# session's: a variable set here never reaches the program. Each entry becomes a `set` line in the
# wrapper, ahead of the program (plans/plan_directx12_parity.md DX12-0006: CNA_D3D12_ADAPTER=warp).
#
# stdout and stderr land in <OutDir>\<Name>.out.txt / .err.txt and are echoed here; the exit code of
# the program becomes the exit code of this script. 124 means it outlived its deadline.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $Exe,
    [string[]] $Arguments = @(),
    [string] $WorkingDirectory = '',
    [string] $OutDir = 'C:\cna\report\interactive',
    [string] $Name = '',
    [int] $TimeoutSeconds = 900,
    [string[]] $Environment = @(),
    [switch] $Quiet
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

if (-not $Name) { $Name = [System.IO.Path]::GetFileNameWithoutExtension($Exe) }
if (-not $WorkingDirectory) { $WorkingDirectory = Split-Path -Parent $Exe }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = (Resolve-Path -LiteralPath $OutDir).Path

$stdout = Join-Path $OutDir "$Name.out.txt"
$stderr = Join-Path $OutDir "$Name.err.txt"
$rcFile = Join-Path $OutDir "$Name.rc.txt"
Remove-Item $stdout, $stderr, $rcFile -Force -ErrorAction SilentlyContinue

# A .cmd wrapper rather than passing the program straight to the task: it is what captures the exit
# code and the two streams on the far side, where nothing of this session survives.
$quoted = ($Arguments | ForEach-Object { if ($_ -match '[\s"]') { '"' + ($_ -replace '"', '\"') + '"' } else { $_ } }) -join ' '
$cmd = Join-Path $OutDir "$Name.run.cmd"
# UTF-8 without a BOM, switched to code page 65001 on its second line. It used to be written as
# ASCII, which turned every character of a non-ASCII executable, argument or working directory
# into '?', so the task ran a path that did not exist and the run could only time out -- the one
# case this runner exists for on a Unicode-path check. cmd.exe decodes each batch line with the
# console code page current when it reads it, so the chcp line has to come before any path; a
# BOM would be read as part of the first line, which is why there is none.
$setLines = @($Environment | Where-Object { $_ } | ForEach-Object {
    if ($_ -notmatch '^[A-Za-z_][A-Za-z0-9_]*=') { throw "win32_run_interactive: -Environment entry '$_' is not NAME=value" }
    # `set "NAME=value"` keeps trailing spaces and characters cmd would otherwise interpret.
    "set `"$_`""
})
$wrapper = @(
    '@echo off'
    'chcp 65001 >nul'
) + $setLines + @(
    "cd /d `"$WorkingDirectory`""
    "`"$Exe`" $quoted 1> `"$stdout`" 2> `"$stderr`""
    "echo %ERRORLEVEL% > `"$rcFile`""
) -join "`r`n"
[System.IO.File]::WriteAllText($cmd, $wrapper + "`r`n", (New-Object System.Text.UTF8Encoding($false)))

$taskName = "CNA_Interactive_$Name"
$service = New-Object -ComObject 'Schedule.Service'
$service.Connect()
$root = $service.GetFolder('\')

try { $root.DeleteTask($taskName, 0) } catch { }

$def = $service.NewTask(0)
$def.RegistrationInfo.Description = 'CNA Win32 native validation -- run on the interactive desktop'
$def.Settings.Enabled = $true
$def.Settings.Hidden = $false
$def.Settings.DisallowStartIfOnBatteries = $false
$def.Settings.StopIfGoingOnBatteries = $false
$def.Settings.ExecutionTimeLimit = 'PT0S'          # no limit of its own; this script holds the deadline
$def.Settings.MultipleInstances = 3                # stop any previous instance
$action = $def.Actions.Create(0)                   # TASK_ACTION_EXEC
$action.Path = $cmd
$action.WorkingDirectory = $WorkingDirectory

# TASK_CREATE_OR_UPDATE (6), TASK_LOGON_INTERACTIVE_TOKEN (3), no password.
$null = $root.RegisterTaskDefinition($taskName, $def, 6, $null, $null, 3)

$task = $root.GetTask($taskName)
$null = $task.Run($null)

$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
$code = $null
while ((Get-Date) -lt $deadline) {
    if (Test-Path $rcFile) {
        Start-Sleep -Milliseconds 300     # let the redirections flush
        $code = [int]((Get-Content $rcFile -Raw).Trim())
        break
    }
    # 0 = TASK_STATE_UNKNOWN, 3 = READY, 4 = RUNNING. A task that reached READY without writing the
    # exit-code file never actually started -- report that rather than waiting out the deadline.
    if ($task.State -eq 3 -and -not (Test-Path $stdout) -and ((Get-Date) - $task.LastRunTime).TotalSeconds -gt 20) {
        break
    }
    Start-Sleep -Milliseconds 500
}

if ($null -eq $code) {
    try { $task.Stop(0) } catch { }
    if (-not $Quiet) { Write-Output "win32_run_interactive: '$Name' did not finish within $TimeoutSeconds s (task last result 0x$('{0:X}' -f $task.LastTaskResult))" }
    $code = 124
}

foreach ($f in @($stdout, $stderr)) {
    if ((Test-Path $f) -and (Get-Item $f).Length -gt 0 -and -not $Quiet) {
        Get-Content $f | ForEach-Object { Write-Output $_ }
    }
}

try { $root.DeleteTask($taskName, 0) } catch { }
exit $code
