# SPDX-License-Identifier: MS-PL
#
# plans/plan_native_platform_validation.md NPV-0029: the automated half of validating CNA's Win32
# platform on NATIVE Windows (10 or 11). Everything recorded about Win32 until this script has run
# on a real Windows installation came from MinGW builds under Wine on Linux -- which is NOT native
# Windows validation, and is labelled that way everywhere it is quoted.
#
# What it does:
#   1. records the machine: Windows edition/build, whether it is a virtual machine, GPU, driver,
#      monitors and their DPI, and a dxdiag report;
#   2. runs the platform test suite (cna_platform_tests.exe, GoogleTest, with an XML report);
#   3. runs the Direct3D 11/12 integration probe (cna_win32_directx_probe.exe);
#   4. runs cna_demo_2d.exe for a fixed number of frames with each renderer compiled into it,
#      and records which DLLs the running process actually loaded -- the runtime proof that no
#      SDL library is involved;
#   5. writes report.md and summary.json into the output directory, followed by the checklist of
#      things only a person at the keyboard can check (docs/testing-win32-native.md).
#
# It was written on Linux, where it could not be executed; its first real run is on Windows.
#
# Usage (from an x64 PowerShell 5.1 or 7 prompt; no administrator rights needed):
#   powershell -ExecutionPolicy Bypass -File validate_win32_native.ps1 -BinDir C:\cna\bin -OutDir C:\cna\report
#
# BinDir must contain cna_platform_tests.exe, cna_win32_directx_probe.exe (both from the
# tools/platform/standalone_tests cross-build) and cna_demo_2d.exe (+ its Content folder) from a
# CNA_PLATFORM=WIN32 build. Anything missing is reported as not run, never as passed.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $BinDir,
    [Parameter(Mandatory = $true)] [string] $OutDir,
    [string[]] $Renderers = @('DIRECTX11', 'DIRECTX12', 'SOFTWARE', 'HEADLESS'),
    [int] $SmokeFrames = 600,
    [int] $TimeoutSeconds = 300
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$BinDir = (Resolve-Path -LiteralPath $BinDir).Path
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = (Resolve-Path -LiteralPath $OutDir).Path

$results = New-Object System.Collections.Generic.List[object]
function Add-Result([string] $Check, [string] $Outcome, [string] $Detail) {
    # Outcome: PASS, FAIL, NOT-RUN, ENVIRONMENT (the machine cannot exercise it), INFO.
    $script:results.Add([pscustomobject]@{ check = $Check; outcome = $Outcome; detail = $Detail })
    Write-Host ('{0,-12} {1} -- {2}' -f $Outcome, $Check, $Detail)
}

function Invoke-Captured([string] $Exe, [string[]] $Arguments, [string] $Name, [hashtable] $Environment) {
    # Runs a program with a deadline and its output captured. Returns the exit code, or $null when
    # it had to be killed.
    $stdout = Join-Path $OutDir "$Name.stdout.txt"
    $stderr = Join-Path $OutDir "$Name.stderr.txt"
    $saved = @{}
    if ($Environment) {
        foreach ($key in $Environment.Keys) {
            $saved[$key] = [Environment]::GetEnvironmentVariable($key, 'Process')
            [Environment]::SetEnvironmentVariable($key, $Environment[$key], 'Process')
        }
    }
    try {
        $start = @{ FilePath = $Exe; WorkingDirectory = $BinDir; RedirectStandardOutput = $stdout
                    RedirectStandardError = $stderr; PassThru = $true; NoNewWindow = $true }
        # Windows PowerShell 5.1 rejects an empty -ArgumentList outright.
        if ($Arguments -and $Arguments.Count -gt 0) { $start.ArgumentList = $Arguments }
        $process = Start-Process @start
        # Without touching Handle now, 5.1 can report ExitCode as $null after the process ends.
        $null = $process.Handle
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            try { $process.Kill() } catch { }
            return $null
        }
        return $process.ExitCode
    }
    finally {
        foreach ($key in $saved.Keys) { [Environment]::SetEnvironmentVariable($key, $saved[$key], 'Process') }
    }
}

# --- 1. the machine ---------------------------------------------------------------------------

$os = Get-CimInstance Win32_OperatingSystem
$computer = Get-CimInstance Win32_ComputerSystem
$isVirtual = ($computer.Model -match 'VirtualBox|VMware|Virtual Machine|KVM|QEMU') -or
             ($computer.Manufacturer -match 'innotek|VMware|QEMU|Xen')
Add-Result 'machine.os' 'INFO' ("{0} {1} (build {2}), {3}" -f $os.Caption, $os.Version, $os.BuildNumber, $os.OSArchitecture)
Add-Result 'machine.kind' 'INFO' ($(if ($isVirtual) { "VIRTUAL MACHINE: $($computer.Manufacturer) $($computer.Model) -- native Windows, but GPU results describe the VM's virtual adapter" } else { "physical: $($computer.Manufacturer) $($computer.Model)" }))
foreach ($gpu in Get-CimInstance Win32_VideoController) {
    Add-Result 'machine.gpu' 'INFO' ("{0}, driver {1}, {2}x{3}" -f $gpu.Name, $gpu.DriverVersion, $gpu.CurrentHorizontalResolution, $gpu.CurrentVerticalResolution)
}
try {
    Add-Type -AssemblyName System.Windows.Forms
    foreach ($screen in [System.Windows.Forms.Screen]::AllScreens) {
        Add-Result 'machine.monitor' 'INFO' ("{0} {1} primary={2}" -f $screen.DeviceName, $screen.Bounds, $screen.Primary)
    }
} catch { Add-Result 'machine.monitor' 'INFO' "could not enumerate screens: $($_.Exception.Message)" }
try {
    $dpi = (Get-ItemProperty 'HKCU:\Control Panel\Desktop\WindowMetrics' -Name AppliedDPI -ErrorAction Stop).AppliedDPI
    Add-Result 'machine.dpi' 'INFO' "AppliedDPI $dpi (96 = 100 %)"
} catch { Add-Result 'machine.dpi' 'INFO' 'AppliedDPI not recorded' }
$dxdiag = Join-Path $OutDir 'dxdiag.txt'
try {
    $dx = Start-Process dxdiag -ArgumentList @('/t', $dxdiag) -PassThru
    if ($dx.WaitForExit(120000) -and (Test-Path $dxdiag)) {
        $levels = Select-String -Path $dxdiag -Pattern 'Feature Levels:' | Select-Object -First 1
        Add-Result 'machine.dxdiag' 'INFO' ($(if ($levels) { $levels.Line.Trim() } else { 'written' }))
    } else { Add-Result 'machine.dxdiag' 'INFO' 'dxdiag did not finish within 120 s' }
} catch { Add-Result 'machine.dxdiag' 'INFO' "dxdiag failed: $($_.Exception.Message)" }

# --- 2. the platform test suite ---------------------------------------------------------------

$tests = Join-Path $BinDir 'cna_platform_tests.exe'
if (Test-Path $tests) {
    $xml = Join-Path $OutDir 'platform-tests.xml'
    $code = Invoke-Captured $tests @("--gtest_output=xml:$xml") 'platform-tests' $null
    $summary = ''
    if (Test-Path $xml) {
        [xml] $report = Get-Content -LiteralPath $xml
        $suites = $report.testsuites
        # GoogleTest marks a skipped test with a <skipped> child; the suite totals do not count them.
        $skipped = $report.SelectNodes('//testcase[skipped]').Count
        $summary = "{0} tests, {1} failures, {2} errors, {3} skipped" -f $suites.tests, $suites.failures, $suites.errors, $skipped
    }
    if ($null -eq $code) { Add-Result 'platform-tests' 'FAIL' "killed after $TimeoutSeconds s" }
    elseif ($code -eq 0) { Add-Result 'platform-tests' 'PASS' $summary }
    else { Add-Result 'platform-tests' 'FAIL' "exit $code; $summary (see platform-tests.stdout.txt)" }
} else { Add-Result 'platform-tests' 'NOT-RUN' 'cna_platform_tests.exe is not in BinDir' }

# --- 3. the Direct3D integration probe --------------------------------------------------------

$probe = Join-Path $BinDir 'cna_win32_directx_probe.exe'
if (Test-Path $probe) {
    $code = Invoke-Captured $probe @() 'directx-probe' $null
    if ($null -eq $code) { Add-Result 'directx-probe' 'FAIL' "killed after $TimeoutSeconds s" }
    elseif ($code -eq 0) { Add-Result 'directx-probe' 'PASS' 'every attempted D3D11/D3D12 stage succeeded on a platform HWND' }
    elseif ($code -eq 1) { Add-Result 'directx-probe' 'FAIL' 'a stage failed in a way that indicates a defect (see directx-probe.stdout.txt)' }
    elseif ($code -eq 2) { Add-Result 'directx-probe' 'ENVIRONMENT' 'no Direct3D on this machine: nothing proved, nothing claimed' }
    else { Add-Result 'directx-probe' 'FAIL' "unexpected exit $code" }
} else { Add-Result 'directx-probe' 'NOT-RUN' 'cna_win32_directx_probe.exe is not in BinDir' }

# --- 4. a real application with each renderer -------------------------------------------------

$demo = Join-Path $BinDir 'cna_demo_2d.exe'
if (Test-Path $demo) {
    foreach ($renderer in $Renderers) {
        $name = "demo2d-$renderer"
        $stdout = Join-Path $OutDir "$name.stdout.txt"
        $stderr = Join-Path $OutDir "$name.stderr.txt"
        [Environment]::SetEnvironmentVariable('CNA_GRAPHICS_RENDERER', $renderer, 'Process')
        try {
            $process = Start-Process -FilePath $demo -ArgumentList @('--smoke', "$SmokeFrames") -WorkingDirectory $BinDir `
                -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru -NoNewWindow
            $null = $process.Handle
            # The modules the process really loaded, sampled while it runs. An SDL DLL here would
            # mean the SDL-free Win32 build is not SDL-free at run time, whatever its imports say.
            $modules = @()
            for ($attempt = 0; $attempt -lt 20 -and -not $process.HasExited; $attempt++) {
                Start-Sleep -Milliseconds 250
                try { $modules = @(Get-Process -Id $process.Id -ErrorAction Stop | ForEach-Object { $_.Modules } | ForEach-Object { $_.ModuleName }) } catch { }
                if ($modules.Count -gt 0) { break }
            }
            $finished = $process.WaitForExit($TimeoutSeconds * 1000)
            if (-not $finished) { try { $process.Kill() } catch { } }
            $sdl = @($modules | Where-Object { $_ -match '(?i)sdl' })
            $d3d = @($modules | Where-Object { $_ -match '(?i)^(d3d11|d3d12|dxgi|d3d12core)\.dll$' }) -join ', '
            $log = if (Test-Path $stdout) { (Get-Content -LiteralPath $stdout -Raw) } else { '' }
            $selected = ([regex]::Match($log, 'graphics renderer: [A-Z0-9_]+')).Value
            if (-not $finished) { Add-Result $name 'FAIL' "did not finish $SmokeFrames frames within $TimeoutSeconds s" }
            elseif ($process.ExitCode -ne 0) { Add-Result $name 'FAIL' "exit $($process.ExitCode); $selected (see $name.stdout.txt)" }
            else { Add-Result $name 'PASS' "$SmokeFrames frames; $selected; D3D modules: $(if ($d3d) { $d3d } else { 'none' })" }
            if ($modules.Count -gt 0) {
                if ($sdl.Count -gt 0) { Add-Result "$name.no-sdl-loaded" 'FAIL' ($sdl -join ', ') }
                else { Add-Result "$name.no-sdl-loaded" 'PASS' "$($modules.Count) modules loaded, none of them SDL" }
                $modules | Sort-Object -Unique | Set-Content -LiteralPath (Join-Path $OutDir "$name.modules.txt")
            } else { Add-Result "$name.no-sdl-loaded" 'NOT-RUN' 'the process exited before its modules could be read' }
        } finally { [Environment]::SetEnvironmentVariable('CNA_GRAPHICS_RENDERER', $null, 'Process') }
    }
} else { Add-Result 'demo2d' 'NOT-RUN' 'cna_demo_2d.exe is not in BinDir' }

# --- 5. the report ----------------------------------------------------------------------------

$failed = @($results | Where-Object { $_.outcome -eq 'FAIL' }).Count
$results | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $OutDir 'summary.json')
$lines = @('# CNA Win32 native validation', '',
    "Recorded $(Get-Date -Format s) on $($env:COMPUTERNAME). This IS native Windows$(if ($isVirtual) { ' (inside a virtual machine)' } else { '' }).", '',
    '| Check | Outcome | Detail |', '|---|---|---|')
foreach ($r in $results) { $lines += ('| {0} | {1} | {2} |' -f $r.check, $r.outcome, ($r.detail -replace '\|', '/')) }
$lines += @('', '## Still to be checked by a person', '',
    'Follow the "Interactive checks" section of docs/testing-win32-native.md and record each result here.')
$lines | Set-Content -LiteralPath (Join-Path $OutDir 'report.md') -Encoding UTF8
Write-Host ''
Write-Host "Report: $(Join-Path $OutDir 'report.md') -- $failed failed check(s)."
exit $(if ($failed -gt 0) { 1 } else { 0 })
