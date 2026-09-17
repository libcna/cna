# SPDX-License-Identifier: MS-PL
#
# Run a labelled CTest subset on the interactive Windows desktop and total it.
#
# The Direct3D parity fixtures are executables that open a real window and a real swap chain, so they
# belong on the logged-on desktop (session 1), not in the SSH session (session 0), where DXGI answers
# CreateSwapChainForHwnd with DXGI_ERROR_NOT_CURRENTLY_AVAILABLE. This wraps ctest in
# win32_run_interactive.ps1, passes the environment through -Environment (the task does not inherit
# this session's variables), and reads ctest's JUnit report into one summary
# (plans/plan_directx12_parity.md DX12-0008).
#
# Usage (in the guest):
#   Set-ExecutionPolicy -Scope Process Bypass -Force
#   & C:\src\cna\tools\platform\win32_ctest_interactive.ps1 -BuildDir C:\cna\build\full-win32-d3d12-nosdl `
#       -Label DIRECTX12 -OutDir C:\cna\report\dx12-parity-1 -Environment 'CNA_D3D12_ADAPTER=warp'
#
# Output: <OutDir>\ctest-junit.xml, <OutDir>\ctest.out.txt, <OutDir>\summary.txt and summary.json.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $BuildDir,
    [string] $Label = '',
    [string] $Regex = '',
    [Parameter(Mandatory = $true)] [string] $OutDir,
    [string[]] $Environment = @(),
    [int] $Parallel = 2,
    [int] $TimeoutSeconds = 14400,
    [int] $PerTestTimeoutSeconds = 0
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = (Resolve-Path -LiteralPath $OutDir).Path
$ctest = (Get-Command ctest -ErrorAction SilentlyContinue).Source
if (-not $ctest) { $ctest = 'C:\Program Files\CMake\bin\ctest.exe' }
if (-not (Test-Path -LiteralPath $ctest)) { throw "win32_ctest_interactive: ctest.exe not found" }

$junit = Join-Path $OutDir 'ctest-junit.xml'
Remove-Item -LiteralPath $junit -Force -ErrorAction SilentlyContinue
$arguments = @('--test-dir', $BuildDir, '--output-on-failure', '-j', "$Parallel", '--output-junit', $junit)
if ($Label) { $arguments += @('-L', $Label) }
if ($Regex) { $arguments += @('-R', $Regex) }
if ($PerTestTimeoutSeconds -gt 0) { $arguments += @('--timeout', "$PerTestTimeoutSeconds") }

$started = Get-Date
$runner = Join-Path $PSScriptRoot 'win32_run_interactive.ps1'
& $runner -Exe $ctest -Arguments $arguments -WorkingDirectory $BuildDir -OutDir $OutDir -Name 'ctest' `
          -TimeoutSeconds $TimeoutSeconds -Environment $Environment -Quiet | Out-Null
$code = $LASTEXITCODE
$elapsed = [int]((Get-Date) - $started).TotalSeconds

if (-not (Test-Path -LiteralPath $junit)) {
    $tail = ''
    $out = Join-Path $OutDir 'ctest.out.txt'
    if (Test-Path $out) { $tail = ((Get-Content $out -Tail 15) -join "`n") }
    Write-Output "ctest exit $code wrote no JUnit report after $elapsed s`n$tail"
    exit 2
}

[xml] $report = Get-Content -LiteralPath $junit
$cases = @($report.SelectNodes('//testcase'))
$passed = @(); $failed = @(); $skipped = @(); $other = @()
foreach ($case in $cases) {
    $name = $case.name
    if ($case.SelectSingleNode('failure') -or $case.status -eq 'fail') { $failed += $name }
    elseif ($case.SelectSingleNode('skipped') -or $case.status -eq 'skipped' -or $case.status -eq 'disabled') { $skipped += $name }
    elseif ($case.status -eq 'run' -or -not $case.status) { $passed += $name }
    else { $other += "$name ($($case.status))" }
}

$lines = @(
    "build    $BuildDir",
    "filter   label='$Label' regex='$Regex', parallel $Parallel, $elapsed s, ctest exit $code",
    "env      $($Environment -join ' ')",
    ("totals   {0} tests: {1} passed, {2} failed, {3} skipped, {4} other" -f `
        $cases.Count, $passed.Count, $failed.Count, $skipped.Count, $other.Count),
    "failed:"
) + ($failed | Sort-Object | ForEach-Object { "  $_" }) + @("skipped:") + ($skipped | Sort-Object | ForEach-Object { "  $_" })
if ($other.Count) { $lines += @("other:") + ($other | ForEach-Object { "  $_" }) }
$lines | Set-Content -LiteralPath (Join-Path $OutDir 'summary.txt') -Encoding UTF8
[ordered]@{
    build = $BuildDir; label = $Label; regex = $Regex; seconds = $elapsed; exit = $code
    environment = $Environment; total = $cases.Count
    passed = $passed.Count; failed = @($failed | Sort-Object); skipped = @($skipped | Sort-Object); other = $other
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutDir 'summary.json') -Encoding UTF8
$lines | ForEach-Object { Write-Output $_ }
if ($failed.Count -gt 0) { exit 1 }
exit 0
