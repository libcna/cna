# SPDX-License-Identifier: MS-PL
#
# Run a GoogleTest binary as N fresh processes on the interactive Windows desktop and total the result.
#
# Why sharded: plans/plan_windows_portability_closeout.md measured that one CnaTests process cannot be a
# result on the VirtualBox guest -- the virtual GPU driver leaks per device (F24), so everything after the
# first exhaustion is noise. WARP does not leak, but a DirectX 12 run keeps the same shape so the DX11 and
# DX12 numbers are measured the same way (plans/plan_directx12_parity.md DX12-0006).
#
# Why interactive: every shard goes through win32_run_interactive.ps1, because an SSH session is session 0,
# where windows, DXGI swap chains and the clipboard do not behave as they do on a desktop.
#
# Usage (in the guest):
#   Set-ExecutionPolicy -Scope Process Bypass -Force
#   & C:\src\cna\tools\platform\win32_gtest_shards.ps1 -Exe C:\cna\build\full-win32-d3d12-nosdl\CnaTests.exe `
#       -Shards 27 -Parallel 2 -OutDir C:\cna\report\dx12-round1 -WorkingDirectory C:\src\cna `
#       -Environment 'CNA_D3D12_ADAPTER=warp'
#
# Output: <OutDir>\shardNN.xml per shard, <OutDir>\summary.txt and <OutDir>\summary.json. A shard that
# wrote no XML (a crash, a deadline) is reported with its exit code and the end of its output rather than
# counted as zero tests.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $Exe,
    [int] $Shards = 27,
    [int] $Parallel = 2,
    [Parameter(Mandatory = $true)] [string] $OutDir,
    [string] $WorkingDirectory = '',
    [string[]] $Environment = @(),
    [string] $Filter = '',
    [int] $TimeoutSeconds = 3600,
    [string] $Name = 'shard'
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

if (-not (Test-Path -LiteralPath $Exe)) { throw "win32_gtest_shards: $Exe does not exist" }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = (Resolve-Path -LiteralPath $OutDir).Path
if (-not $WorkingDirectory) { $WorkingDirectory = Split-Path -Parent $Exe }
$runner = Join-Path $PSScriptRoot 'win32_run_interactive.ps1'
$exeTime = (Get-Item -LiteralPath $Exe).LastWriteTime

$started = Get-Date
$pending = [System.Collections.Generic.Queue[int]]::new()
0..($Shards - 1) | ForEach-Object { $pending.Enqueue($_) }
$running = @{}
$results = @{}

function Start-Shard([int] $index) {
    $tag = '{0}{1:D2}' -f $Name, $index
    $xml = Join-Path $OutDir "$tag.xml"
    Remove-Item -LiteralPath $xml -Force -ErrorAction SilentlyContinue
    $arguments = @("--gtest_output=xml:$xml")
    if ($Filter) { $arguments += "--gtest_filter=$Filter" }
    $shardEnv = @("GTEST_TOTAL_SHARDS=$Shards", "GTEST_SHARD_INDEX=$index") + $Environment
    if (($Environment | Where-Object { $_ -match '^CNA_D3D12_(DEBUG_LAYER|GPU_VALIDATION)=1$' })) {
        $shardEnv += "CNA_D3D12_DEBUG_REPORT=$(Join-Path $OutDir "$tag.debuglayer.txt")"
    }
    Start-Job -ScriptBlock {
        param($runner, $exe, $arguments, $wd, $outDir, $tag, $timeout, $shardEnv)
        Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
        & $runner -Exe $exe -Arguments $arguments -WorkingDirectory $wd -OutDir $outDir -Name $tag `
                  -TimeoutSeconds $timeout -Environment $shardEnv -Quiet | Out-Null
        $LASTEXITCODE
    } -ArgumentList $runner, $Exe, $arguments, $WorkingDirectory, $OutDir, $tag, $TimeoutSeconds, $shardEnv
}

while ($pending.Count -gt 0 -or $running.Count -gt 0) {
    while ($running.Count -lt $Parallel -and $pending.Count -gt 0) {
        $index = $pending.Dequeue()
        $running[$index] = Start-Shard $index
    }
    foreach ($index in @($running.Keys)) {
        $job = $running[$index]
        if ($job.State -in @('Completed', 'Failed', 'Stopped')) {
            $code = Receive-Job $job -ErrorAction SilentlyContinue | Select-Object -Last 1
            Remove-Job $job -Force
            $running.Remove($index)
            $results[$index] = $code
            Write-Output ("{0}{1:D2} finished, exit {2}" -f $Name, $index, $code)
        }
    }
    Start-Sleep -Seconds 2
}

$totals = [ordered]@{ tests = 0; failures = 0; errors = 0; skipped = 0; disabled = 0 }
$failing = @()
$broken = @()
$debugLines = @()
foreach ($index in 0..($Shards - 1)) {
    $tag = '{0}{1:D2}' -f $Name, $index
    $xml = Join-Path $OutDir "$tag.xml"
    if (-not (Test-Path -LiteralPath $xml)) {
        $tail = ''
        $out = Join-Path $OutDir "$tag.out.txt"
        if (Test-Path $out) { $tail = ((Get-Content $out -Tail 8) -join ' | ') }
        $broken += "$tag exit $($results[$index]) wrote no XML; last output: $tail"
        continue
    }
    [xml] $report = Get-Content -LiteralPath $xml
    $totals.tests += [int] $report.testsuites.tests
    $totals.failures += [int] $report.testsuites.failures
    $totals.errors += [int] $report.testsuites.errors
    $totals.disabled += [int] $report.testsuites.disabled
    $totals.skipped += $report.SelectNodes('//testcase[skipped]').Count
    $failing += @($report.SelectNodes('//testcase[failure]') | ForEach-Object { "$($_.classname).$($_.name)" })
    $debug = Join-Path $OutDir "$tag.debuglayer.txt"
    if (Test-Path $debug) { $debugLines += Get-Content $debug }
}

$elapsed = [int]((Get-Date) - $started).TotalSeconds
$passed = $totals.tests - $totals.failures - $totals.errors - $totals.skipped
$lines = @(
    "binary   $Exe (built $exeTime)",
    "shards   $Shards, parallel $Parallel, $elapsed s",
    "env      $($Environment -join ' ')",
    ("totals   {0} tests: {1} passed, {2} failed, {3} errored, {4} skipped, {5} disabled" -f `
        $totals.tests, $passed, $totals.failures, $totals.errors, $totals.skipped, $totals.disabled),
    "broken shards: $($broken.Count)"
) + ($broken | ForEach-Object { "  $_" }) + @("failing tests: $($failing.Count)") + ($failing | Sort-Object | ForEach-Object { "  $_" })
if ($debugLines.Count -gt 0) {
    $lines += "debug-layer report lines: $($debugLines.Count)"
    $lines += ($debugLines | Where-Object { $_ -notmatch 'process totals' } | ForEach-Object { "  $_" })
}
$lines | Set-Content -LiteralPath (Join-Path $OutDir 'summary.txt') -Encoding UTF8
[ordered]@{
    binary = $Exe; built = "$exeTime"; shards = $Shards; parallel = $Parallel; seconds = $elapsed
    environment = $Environment; totals = $totals; passed = $passed
    failing = @($failing | Sort-Object); broken = $broken
    debugLayerLines = $debugLines.Count
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutDir 'summary.json') -Encoding UTF8
$lines | ForEach-Object { Write-Output $_ }
if ($broken.Count -gt 0 -or $totals.failures -gt 0 -or $totals.errors -gt 0) { exit 1 }
exit 0
