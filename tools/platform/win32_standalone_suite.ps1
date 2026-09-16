# SPDX-License-Identifier: MS-PL
#
# plans/plan_win32_native_validation.md WINNATIVE-0023: build and run the standalone platform
# harness for one CNA_PLATFORM selection on native Windows, and summarise the result.
#
# This is the regression instrument for "did hardening the Win32 backend break the other backends
# on this operating system". The standalone harness is the platform module plus its own suite and
# nothing else, so a result from it is about the platform layer rather than about the framework.
#
# The suite runs through win32_run_interactive.ps1, never directly: an SSH session is session 0 on
# a Service-0x0-<luid>$ window station with no desktop, and a windowing backend asked to create a
# window there fails for reasons that have nothing to do with the backend. The scheduled task with
# an interactive token puts it on WinSta0\Default, where a real user's program runs.
#
# Usage: win32_standalone_suite.ps1 -Platform SDL3 [-Build ...] [-Parallel 6]

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $Platform,
    [string] $Source   = 'C:/src/cna/tools/platform/standalone_tests',
    [string] $Build    = '',
    [int]    $Parallel = 6,
    [int]    $TimeoutSeconds = 1800
)

$ProgressPreference = 'SilentlyContinue'
$ErrorActionPreference = 'Continue'
if (-not $Build) { $Build = "C:/cna/build/std-$($Platform.ToLower())" }

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { Write-Output 'no MSVC installation'; exit 2 }
cmd /c "`"$(Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat')`" >nul 2>&1 && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($matches[1])" -Value $matches[2] }
}

Write-Output "=== configure $Platform ==="
cmake -S $Source -B $Build -G Ninja `
      "-DCNA_PLATFORM=$Platform" -DCMAKE_BUILD_TYPE=Release 2>&1 | Select-Object -Last 8
if ($LASTEXITCODE -ne 0) { Write-Output 'CONFIGURE FAILED'; exit 1 }

Write-Output "=== build $Platform ==="
cmake --build $Build --parallel $Parallel 2>&1 | Select-Object -Last 6
if ($LASTEXITCODE -ne 0) { Write-Output 'BUILD FAILED'; exit 1 }

$xml = "C:\cna\report\platform-$Platform.xml"
Remove-Item $xml -ErrorAction SilentlyContinue

Write-Output "=== run $Platform suite on the interactive desktop ==="
& C:\src\cna\tools\platform\win32_run_interactive.ps1 `
    -Exe (Join-Path $Build 'cna_platform_tests.exe') `
    -Arguments @("--gtest_output=xml:$xml") `
    -WorkingDirectory $Build -Name "plat-$Platform" -TimeoutSeconds $TimeoutSeconds -Quiet
$code = $LASTEXITCODE

if (Test-Path $xml) {
    [xml]$r = Get-Content $xml
    $skipped = $r.SelectNodes('//testcase[skipped]').Count
    Write-Output ("$Platform : {0} tests, {1} failures, {2} errors, {3} skipped (exit {4})" -f `
        $r.testsuites.tests, $r.testsuites.failures, $r.testsuites.errors, $skipped, $code)
    $r.SelectNodes('//testcase[failure]') | Select-Object -First 20 |
        ForEach-Object { Write-Output "   FAIL $($_.classname).$($_.name)" }
} else {
    # No XML from a non-zero exit is the signature of a process that died before GoogleTest could
    # write one -- a missing DLL (0xC0000135) looks exactly like this, so say the code in hex too.
    Write-Output ("$Platform : no XML (exit {0} = 0x{1:X8})" -f $code, $code)
}
exit $code
