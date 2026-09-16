# SPDX-License-Identifier: MS-PL
#
# plans/plan_win32_native_validation.md WINNATIVE-0022: the Win32 backend under MSVC's
# AddressSanitizer.
#
# The lifetime-heavy parts of this backend -- the window registry, adopted windows, the WndProc
# pointer discipline across WM_NCCREATE/WM_NCDESTROY, the UTF conversions, the clipboard's global
# memory, the COM wrappers -- are exactly the code where a use-after-free is both easy to write and
# invisible in a passing test. ASan is how that question gets an answer rather than an opinion.
#
# Only the standalone platform harness is built here, deliberately: it is the whole platform module
# plus its whole suite and nothing else, so an ASan report from it is unambiguously about this
# backend. A sanitized build of the entire framework would take far longer and point at more places
# that are not the subject.
#
# Notes that cost a run to learn:
#   * /fsanitize=address is incompatible with /RTC1, which CMake puts in its MSVC Debug flags, so
#     this configures RelWithDebInfo and adds /Zi rather than using Debug;
#   * incremental linking is incompatible with it too;
#   * with the dynamic CRT the instrumented binary needs clang_rt.asan_dynamic-x86_64.dll, which
#     lives beside cl.exe and is not on PATH by default -- so it is copied next to the executables
#     rather than left to a PATH that a scheduled task will not inherit.
#
# Usage: win32_asan_build.ps1 [-Build C:\cna\build\win32-standalone-asan] [-Run]

[CmdletBinding()]
param(
    [string] $Source = 'C:/src/cna/tools/platform/standalone_tests',
    [string] $Build  = 'C:/cna/build/win32-standalone-asan',
    [int]    $Parallel = 4,
    [switch] $Run
)

$ErrorActionPreference = 'Continue'
$ProgressPreference = 'SilentlyContinue'

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { Write-Output 'no MSVC installation'; exit 2 }
cmd /c "`"$(Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat')`" >nul 2>&1 && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($matches[1])" -Value $matches[2] }
}

$asanDll = Get-ChildItem (Join-Path $vs 'VC\Tools\MSVC') -Recurse -Filter 'clang_rt.asan_dynamic-x86_64.dll' -ErrorAction SilentlyContinue |
           Where-Object { $_.FullName -match '\\bin\\Hostx64\\x64\\' } | Select-Object -First 1
if (-not $asanDll) {
    Write-Output 'ASan runtime not found -- the VC.ASAN component is not installed'
    exit 3
}
Write-Output "asan runtime : $($asanDll.FullName)"

Write-Output '--- configure ---'
cmake -S $Source -B $Build -G Ninja `
      -DCNA_PLATFORM=WIN32 `
      -DCMAKE_BUILD_TYPE=RelWithDebInfo `
      "-DCMAKE_CXX_FLAGS=/fsanitize=address /Zi" `
      "-DCMAKE_C_FLAGS=/fsanitize=address /Zi" `
      "-DCMAKE_EXE_LINKER_FLAGS=/INCREMENTAL:NO" 2>&1 | Select-Object -Last 12
if ($LASTEXITCODE -ne 0) { Write-Output "CONFIGURE FAILED ($LASTEXITCODE)"; exit 1 }

Write-Output '--- build ---'
cmake --build $Build --parallel $Parallel 2>&1 | Select-Object -Last 25
if ($LASTEXITCODE -ne 0) { Write-Output "BUILD FAILED ($LASTEXITCODE)"; exit 1 }

Copy-Item $asanDll.FullName $Build -Force
Write-Output "asan runtime copied next to the executables"

if (-not $Run) { exit 0 }

# ASAN_OPTIONS: a report must fail the run rather than be printed and ignored, and the symbolizer
# turns the stack into something a person can act on.
$env:ASAN_OPTIONS = 'exitcode=1:print_stats=0:detect_leaks=0'
Write-Output '--- cna_platform_tests under ASan (interactive desktop) ---'
& C:\src\cna\tools\platform\win32_run_interactive.ps1 `
    -Exe (Join-Path $Build 'cna_platform_tests.exe') `
    -Arguments @("--gtest_output=xml:C:\cna\report\asan-platform-tests.xml") `
    -WorkingDirectory $Build -Name 'asan-platform-tests' -TimeoutSeconds 2400
$testsExit = $LASTEXITCODE
Write-Output "platform tests under ASan: exit $testsExit"

Write-Output '--- lifecycle stress under ASan ---'
& C:\src\cna\tools\platform\win32_run_interactive.ps1 `
    -Exe (Join-Path $Build 'cna_win32_native_stress.exe') `
    -Arguments @('--iterations', '400') `
    -WorkingDirectory $Build -Name 'asan-stress' -TimeoutSeconds 2400
$stressExit = $LASTEXITCODE
Write-Output "stress under ASan: exit $stressExit"

Write-Output '--- desktop checks under ASan ---'
& C:\src\cna\tools\platform\win32_run_interactive.ps1 `
    -Exe (Join-Path $Build 'cna_win32_native_desktop.exe') `
    -Arguments @('--iterations', '200') `
    -WorkingDirectory $Build -Name 'asan-desktop' -TimeoutSeconds 2400
$desktopExit = $LASTEXITCODE
Write-Output "desktop checks under ASan: exit $desktopExit"

exit $(if ($testsExit -eq 0 -and $stressExit -eq 0 -and $desktopExit -eq 0) { 0 } else { 1 })
