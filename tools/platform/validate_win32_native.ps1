# SPDX-License-Identifier: MS-PL
#
# plans/plan_win32_native_validation.md: the automated half of validating CNA's Win32 platform on
# NATIVE Windows, with the real Microsoft toolchain.
#
# Everything plans/plan_win32.md recorded about Win32 was measured with MinGW cross-builds run
# under Wine on Linux. Wine is a reimplementation of the Windows API, not Windows, and MinGW is not
# the ABI Windows programs are usually built with. This script re-establishes the same claims where
# they can actually be judged: it configures, builds, tests and exercises CNA with MSVC on a real
# Windows installation, and writes down what it measured rather than what was expected.
#
# It runs unattended, from Linux, through tools/platform/windows_vm_validate.sh -- or by hand from
# a normal (not administrator) PowerShell prompt:
#
#   powershell -ExecutionPolicy Bypass -File validate_win32_native.ps1 `
#              -SourceDir C:\src\cna -BuildRoot C:\cna\build -OutDir C:\cna\report
#
# Outcomes are deliberately five-valued, because "did not run" is not a pass and "this machine
# cannot do that" is not a failure:
#
#   PASS         it was exercised and behaved
#   FAIL         it was exercised and did not
#   NOT-RUN      a prerequisite was missing -- never counted as a pass
#   ENVIRONMENT  this machine cannot exercise it (no Direct3D 12, no Vulkan driver, ...)
#   INFO         a recorded fact, not a judgement
#
# Only FAIL sets the exit code. `report.md` and `summary.json` land in OutDir.

[CmdletBinding()]
param(
    [string]   $SourceDir        = 'C:\src\cna',
    [string]   $SharpRuntimeDir  = 'C:\src\sharp-runtime',
    [string]   $BuildRoot        = 'C:\cna\build',
    [string]   $OutDir           = 'C:\cna\report',
    [string[]] $Steps            = @('all'),
    [switch]   $KeepGoing,
    [int]      $StressIterations = 2000,
    [int]      $Parallel         = 4,
    [int]      $TimeoutSeconds   = 5400
)

$ErrorActionPreference = 'Continue'
$ProgressPreference    = 'SilentlyContinue'

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = (Resolve-Path -LiteralPath $OutDir).Path

$script:results = New-Object System.Collections.Generic.List[object]
$script:failed  = 0

function Add-Result([string] $Check, [string] $Outcome, [string] $Detail) {
    $script:results.Add([pscustomobject]@{ check = $Check; outcome = $Outcome; detail = $Detail })
    if ($Outcome -eq 'FAIL') { $script:failed++ }
    # Write-Output, never Write-Host: this script's stdout is a pipe back to Linux, and
    # PowerShell serialises host writes as CLIXML into it.
    Write-Output ('{0,-12} {1,-34} {2}' -f $Outcome, $Check, $Detail)
}

function Want([string] $step) {
    if ($Steps -contains 'all') { return $true }
    return $Steps -contains $step
}

# Runs a program with a deadline, tees its output to OutDir\<name>.log, returns the exit code
# ($null when it had to be killed).
function Invoke-Logged([string] $Name, [string] $Exe, [string[]] $Arguments,
                       [string] $WorkingDirectory = $null, [int] $Timeout = 0) {
    if ($Timeout -le 0) { $Timeout = $TimeoutSeconds }
    $log = Join-Path $OutDir "$Name.log"
    $err = Join-Path $OutDir "$Name.err.log"
    $start = @{
        FilePath               = $Exe
        RedirectStandardOutput = $log
        RedirectStandardError  = $err
        PassThru               = $true
        NoNewWindow            = $true
    }
    if ($Arguments -and $Arguments.Count -gt 0) { $start.ArgumentList = $Arguments }
    if ($WorkingDirectory) { $start.WorkingDirectory = $WorkingDirectory }
    $p = Start-Process @start
    $null = $p.Handle   # Windows PowerShell 5.1 loses ExitCode without this.
    if (-not $p.WaitForExit($Timeout * 1000)) {
        try { $p.Kill() } catch { }
        return $null
    }
    return $p.ExitCode
}

function Tail([string] $Name, [int] $Lines = 25) {
    $log = Join-Path $OutDir "$Name.log"
    $err = Join-Path $OutDir "$Name.err.log"
    $text = @()
    foreach ($f in @($log, $err)) {
        if (Test-Path $f) { $text += (Get-Content $f -Tail $Lines -ErrorAction SilentlyContinue) }
    }
    return (($text | Where-Object { $_ -and $_.Trim() }) -join ' / ')
}

# ================================================================= step: environment

function Step-Environment {
    $cv = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
    $os = Get-CimInstance Win32_OperatingSystem
    $cs = Get-CimInstance Win32_ComputerSystem
    $isVirtual = ($cs.Model -match 'VirtualBox|VMware|Virtual Machine|KVM|QEMU') -or
                 ($cs.Manufacturer -match 'innotek|VMware|QEMU|Xen')
    $script:isVirtual = $isVirtual

    Add-Result 'machine.os' 'INFO' ("{0} {1} build {2}.{3} {4}" -f `
        $os.Caption, $cv.DisplayVersion, $cv.CurrentBuildNumber, $cv.UBR, $os.OSArchitecture)
    Add-Result 'machine.kind' 'INFO' $(if ($isVirtual) {
        "VIRTUAL MACHINE ($($cs.Manufacturer) $($cs.Model)) -- the Windows API is real; the GPU is not a physical one"
    } else { "physical: $($cs.Manufacturer) $($cs.Model)" })
    Add-Result 'machine.cpu' 'INFO' "$($cs.NumberOfLogicalProcessors) logical processors, $([math]::Round($cs.TotalPhysicalMemory/1GB,1)) GB RAM"
    $c = Get-PSDrive C
    Add-Result 'machine.disk' 'INFO' "C: $([math]::Round($c.Free/1GB,1)) GB free of $([math]::Round(($c.Free+$c.Used)/1GB,1)) GB"
    foreach ($gpu in Get-CimInstance Win32_VideoController) {
        Add-Result 'machine.gpu' 'INFO' ("{0}, driver {1}, {2}x{3}" -f `
            $gpu.Name, $gpu.DriverVersion, $gpu.CurrentHorizontalResolution, $gpu.CurrentVerticalResolution)
    }
    try {
        Add-Type -AssemblyName System.Windows.Forms
        foreach ($s in [System.Windows.Forms.Screen]::AllScreens) {
            Add-Result 'machine.monitor' 'INFO' ("{0} {1} primary={2}" -f $s.DeviceName, $s.Bounds, $s.Primary)
        }
    } catch { Add-Result 'machine.monitor' 'INFO' "could not enumerate: $($_.Exception.Message)" }
    try {
        $dpi = (Get-ItemProperty 'HKCU:\Control Panel\Desktop\WindowMetrics' -Name AppliedDPI -ErrorAction Stop).AppliedDPI
        Add-Result 'machine.dpi' 'INFO' "AppliedDPI $dpi ($([math]::Round(100*$dpi/96))% scaling)"
    } catch { Add-Result 'machine.dpi' 'INFO' 'AppliedDPI not recorded (100%)' }

    foreach ($d in @('d3d11.dll','d3d12.dll','dxgi.dll','opengl32.dll','vulkan-1.dll')) {
        $p = Join-Path $env:WINDIR "System32\$d"
        Add-Result "machine.dll.$d" 'INFO' $(if (Test-Path $p) { (Get-Item $p).VersionInfo.FileVersion } else { 'absent' })
    }
}

# ================================================================= step: toolchain

function Enter-MsvcEnvironment {
    if ($env:VSCMD_ARG_TGT_ARCH -eq 'x64') { return $true }
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { return $false }
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
                     -property installationPath 2>$null | Select-Object -First 1
    if (-not $vs) { return $false }
    $vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path $vcvars)) { return $false }
    cmd /c "`"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($matches[1])" -Value $matches[2] }
    }
    return [bool](Get-Command cl -ErrorAction SilentlyContinue)
}

function Step-Toolchain {
    if (-not (Enter-MsvcEnvironment)) {
        Add-Result 'toolchain.msvc' 'NOT-RUN' 'no MSVC x64 environment could be entered'
        return $false
    }
    $clBanner = (& cl.exe 2>&1 | Select-Object -First 1)
    Add-Result 'toolchain.msvc' 'PASS' $clBanner
    Add-Result 'toolchain.sdk' 'INFO' "Windows SDK $env:WindowsSDKVersion (target $env:VSCMD_ARG_TGT_ARCH)"
    foreach ($t in @('cmake','ninja','git','python')) {
        $c = Get-Command $t -ErrorAction SilentlyContinue
        if ($c) {
            $v = switch ($t) {
                'cmake'  { (& cmake --version | Select-Object -First 1) }
                'ninja'  { "ninja $(& ninja --version)" }
                'git'    { (& git --version) }
                'python' { (& python --version 2>&1) }
            }
            Add-Result "toolchain.$t" 'INFO' "$v ($($c.Source))"
        } else { Add-Result "toolchain.$t" 'NOT-RUN' 'absent' }
    }
    return $true
}

# ================================================================= step: the source tree

function Step-Source {
    $ok = $true
    foreach ($pair in @(@{n='cna'; d=$SourceDir}, @{n='sharp-runtime'; d=$SharpRuntimeDir})) {
        if (-not (Test-Path (Join-Path $pair.d '.git'))) {
            Add-Result "source.$($pair.n)" 'NOT-RUN' "no git checkout at $($pair.d)"
            if ($pair.n -eq 'cna') { $ok = $false }
            continue
        }
        Push-Location $pair.d
        $sha   = (& git rev-parse HEAD).Trim()
        $dirty = @(& git status --porcelain).Count
        Pop-Location
        Add-Result "source.$($pair.n)" $(if ($dirty -eq 0) { 'PASS' } else { 'INFO' }) `
            "$sha$(if ($dirty -ne 0) { " (+$dirty uncommitted change(s))" } else { ' (clean)' })"
    }
    return $ok
}

# ================================================================= configurations

# Each configuration is one CMake build directory. The point of having more than one is that the
# claims differ: the standalone harness proves the platform module alone builds and passes with
# MSVC; the SDL-free full build is the one whose PE imports are the SDL-independence evidence.
$script:Configurations = @(
    [pscustomobject]@{
        Name    = 'standalone-win32'
        Source  = 'tools/platform/standalone_tests'
        Args    = @('-DCNA_PLATFORM=WIN32', '-DCMAKE_BUILD_TYPE=Debug')
        Targets = @()
        Why     = 'the platform module and its whole suite, with nothing else in the tree'
    },
    [pscustomobject]@{
        Name    = 'full-win32-d3d11-nosdl'
        Source  = '.'
        Args    = @('-DCMAKE_BUILD_TYPE=RelWithDebInfo',
                    '-DCNA_PLATFORM=WIN32',
                    '-DCNA_ENABLE_SDL=OFF',
                    '-DCNA_AUDIO_PLATFORM=NULL',
                    '-DCNA_GRAPHICS_RENDERER=DIRECTX11',
                    '-DCNA_BUILD_TESTS=ON',
                    '-DCNA_BUILD_EXAMPLES=OFF',
                    '-DCNA_ENABLE_NET=OFF',
                    # Draco is 84 MB of third-party source that decodes a glTF extension; the
                    # documented decoder-free build is the honest way to keep it out of a Win32
                    # validation run rather than a silent omission.
                    '-DCNA_ENABLE_DRACO=OFF')
        Targets = @()
        Why     = 'the real framework, SDL-free, on the Win32 backend'
    }
)

function Invoke-Configuration([object] $config, [switch] $ConfigureOnly) {
    $build = Join-Path $BuildRoot $config.Name
    $src   = if ($config.Source -eq '.') { $SourceDir } else { Join-Path $SourceDir $config.Source }
    $args  = @('-S', $src, '-B', $build, '-G', 'Ninja') + $config.Args
    if ($config.Source -eq '.') { $args += "-DCNA_SHARP_RUNTIME_ROOT=$SharpRuntimeDir" }

    $code = Invoke-Logged "configure-$($config.Name)" 'cmake' $args $SourceDir
    if ($code -ne 0) {
        Add-Result "configure.$($config.Name)" 'FAIL' "cmake exit $code -- $(Tail "configure-$($config.Name)" 12)"
        return $false
    }
    Add-Result "configure.$($config.Name)" 'PASS' $config.Why
    if ($ConfigureOnly) { return $true }

    $buildArgs = @('--build', $build, '--parallel', "$Parallel")
    if ($config.Targets.Count -gt 0) { $buildArgs += @('--target') + $config.Targets }
    $code = Invoke-Logged "build-$($config.Name)" 'cmake' $buildArgs $SourceDir
    if ($code -ne 0) {
        Add-Result "build.$($config.Name)" 'FAIL' "cmake --build exit $code -- $(Tail "build-$($config.Name)" 15)"
        return $false
    }

    # MSVC warnings are a finding in their own right: this tree has never been compiled by cl.exe
    # in full, so the first clean build is also the first warning inventory.
    $log = Join-Path $OutDir "build-$($config.Name).log"
    $warnings = @()
    if (Test-Path $log) {
        $warnings = @(Select-String -Path $log -Pattern ': warning [A-Z]+[0-9]+' -AllMatches |
                      ForEach-Object { ($_.Line -replace '^.*: warning ', 'warning ') } |
                      ForEach-Object { ($_ -split ':')[0] } | Sort-Object -Unique)
    }
    Add-Result "build.$($config.Name)" 'PASS' `
        "built; $($warnings.Count) distinct MSVC warning code(s)$(if ($warnings.Count) { ': ' + ($warnings -join ', ') })"
    return $true
}

# ================================================================= step: GoogleTest suites

function Invoke-GTest([string] $Check, [string] $Exe, [string[]] $ExtraArgs = @(), [int] $Timeout = 0) {
    if (-not (Test-Path $Exe)) { Add-Result $Check 'NOT-RUN' "$Exe was not built"; return }
    $name = Split-Path $Exe -LeafBase
    $xml  = Join-Path $OutDir "$Check.xml"
    $code = Invoke-Logged "gtest-$Check" $Exe (@("--gtest_output=xml:$xml") + $ExtraArgs) `
                          (Split-Path $Exe -Parent) $Timeout
    $summary = ''
    if (Test-Path $xml) {
        [xml] $report = Get-Content -LiteralPath $xml
        $skipped = $report.SelectNodes('//testcase[skipped]').Count
        $disabled = [int] $report.testsuites.disabled
        $summary = "{0} ran, {1} failed, {2} errored, {3} skipped, {4} disabled" -f `
            $report.testsuites.tests, $report.testsuites.failures, $report.testsuites.errors, $skipped, $disabled
        $failures = @($report.SelectNodes('//testcase[failure]') | ForEach-Object { "$($_.classname).$($_.name)" })
        if ($failures.Count -gt 0) {
            $summary += '; failing: ' + (($failures | Select-Object -First 12) -join ', ')
            if ($failures.Count -gt 12) { $summary += ", +$($failures.Count - 12) more" }
        }
    }
    if ($null -eq $code)   { Add-Result $Check 'FAIL' "killed after $(if ($Timeout) { $Timeout } else { $TimeoutSeconds }) s; $summary" }
    elseif ($code -eq 0)   { Add-Result $Check 'PASS' $summary }
    else                   { Add-Result $Check 'FAIL' "exit $code; $summary" }
}

# ================================================================= step: SDL independence

function Step-NoSdl([string] $buildDir) {
    if (-not (Test-Path $buildDir)) { Add-Result 'nosdl.imports' 'NOT-RUN' "no build at $buildDir"; return }

    # 1. nothing named SDL was produced at all.
    $built = @(Get-ChildItem $buildDir -Recurse -Include '*.dll','*.lib','*.exe' -ErrorAction SilentlyContinue |
               Where-Object { $_.Name -match '(?i)sdl' })
    if ($built.Count -gt 0) {
        Add-Result 'nosdl.artifacts' 'FAIL' ("SDL artifacts were built: " + (($built | ForEach-Object { $_.Name } | Select-Object -Unique) -join ', '))
    } else {
        Add-Result 'nosdl.artifacts' 'PASS' 'the SDL-free configuration produced no SDL binary'
    }

    # 2. no executable IMPORTS one. dumpbin is the native PE authority here.
    $dumpbin = Get-Command dumpbin -ErrorAction SilentlyContinue
    if (-not $dumpbin) { Add-Result 'nosdl.imports' 'NOT-RUN' 'dumpbin is not on PATH (needs the MSVC environment)'; return }
    $exes = @(Get-ChildItem $buildDir -Recurse -Filter '*.exe' -ErrorAction SilentlyContinue)
    if ($exes.Count -eq 0) { Add-Result 'nosdl.imports' 'NOT-RUN' 'no .exe in the build tree'; return }
    $offenders = @()
    $inventory = @()
    foreach ($exe in $exes) {
        $deps = & dumpbin.exe /nologo /dependents $exe.FullName 2>&1
        $dlls = @($deps | Where-Object { $_ -match '^\s+\S+\.dll\s*$' } | ForEach-Object { $_.Trim() })
        $inventory += "$($exe.Name): $($dlls -join ' ')"
        if ($dlls | Where-Object { $_ -match '(?i)sdl' }) { $offenders += $exe.Name }
    }
    $inventory | Set-Content -LiteralPath (Join-Path $OutDir 'pe-dependents.txt')
    if ($offenders.Count -gt 0) {
        Add-Result 'nosdl.imports' 'FAIL' ("these import an SDL DLL: " + ($offenders -join ', '))
    } else {
        Add-Result 'nosdl.imports' 'PASS' "$($exes.Count) executable(s) inspected with dumpbin /dependents; none imports SDL (pe-dependents.txt)"
    }
}

# ================================================================= run

Write-Output ''
Write-Output "CNA Win32 native validation -- $(Get-Date -Format s) on $env:COMPUTERNAME"
Write-Output ("source=$SourceDir  build=$BuildRoot  out=$OutDir  steps=" + ($Steps -join ','))
Write-Output ''

if (Want 'environment') { Step-Environment }

$toolchainOk = $true
if (Want 'toolchain') { $toolchainOk = Step-Toolchain }
else { $toolchainOk = Enter-MsvcEnvironment }

$sourceOk = $true
if (Want 'source') { $sourceOk = Step-Source }

$standaloneBuild = Join-Path $BuildRoot 'standalone-win32'
$fullBuild       = Join-Path $BuildRoot 'full-win32-d3d11-nosdl'

if ($toolchainOk -and $sourceOk) {
    if (Want 'standalone') {
        if (Invoke-Configuration ($script:Configurations | Where-Object Name -eq 'standalone-win32')) {
            if (Want 'platform-tests') {
                Invoke-GTest 'platform-tests' (Join-Path $standaloneBuild 'cna_platform_tests.exe')
            }
            if (Want 'directx') {
                $probe = Join-Path $standaloneBuild 'cna_win32_directx_probe.exe'
                if (Test-Path $probe) {
                    $code = Invoke-Logged 'directx-probe' $probe @() $standaloneBuild 600
                    switch ($code) {
                        0       { Add-Result 'directx.probe' 'PASS' 'every attempted D3D11/D3D12 stage succeeded on a CNA platform HWND' }
                        1       { Add-Result 'directx.probe' 'FAIL' "a stage failed -- $(Tail 'directx-probe' 20)" }
                        2       { Add-Result 'directx.probe' 'ENVIRONMENT' 'no Direct3D on this machine' }
                        $null   { Add-Result 'directx.probe' 'FAIL' 'killed on a deadline' }
                        default { Add-Result 'directx.probe' 'FAIL' "unexpected exit $code" }
                    }
                } else { Add-Result 'directx.probe' 'NOT-RUN' 'cna_win32_directx_probe.exe was not built' }
            }
            if (Want 'stress') {
                $stress = Join-Path $standaloneBuild 'cna_win32_native_stress.exe'
                if (Test-Path $stress) {
                    $json = Join-Path $OutDir 'stress.json'
                    $code = Invoke-Logged 'stress' $stress `
                            @('--iterations', "$StressIterations", '--json', $json) $standaloneBuild 3600
                    $detail = Tail 'stress' 40
                    switch ($code) {
                        0       { Add-Result 'stress.lifecycle' 'PASS' "$StressIterations-iteration budget; every phase within tolerance" }
                        2       { Add-Result 'stress.lifecycle' 'ENVIRONMENT' 'the Win32 platform could not be created' }
                        $null   { Add-Result 'stress.lifecycle' 'FAIL' 'killed on a deadline' }
                        default { Add-Result 'stress.lifecycle' 'FAIL' $detail }
                    }
                } else { Add-Result 'stress.lifecycle' 'NOT-RUN' 'cna_win32_native_stress.exe was not built' }
            }
        }
    }

    if (Want 'full') {
        if (Invoke-Configuration ($script:Configurations | Where-Object Name -eq 'full-win32-d3d11-nosdl')) {
            if (Want 'cnatests') {
                Invoke-GTest 'cnatests' (Join-Path $fullBuild 'CnaTests.exe') @() 5400
            }
            if (Want 'ctest') {
                $code = Invoke-Logged 'ctest' 'ctest' @('--test-dir', $fullBuild, '--output-on-failure', '-j', '2') $SourceDir 5400
                $line = ''
                $log = Join-Path $OutDir 'ctest.log'
                if (Test-Path $log) {
                    $line = ((Get-Content $log | Select-String -Pattern 'tests passed|tests failed out of') | Select-Object -Last 1)
                }
                if ($code -eq 0) { Add-Result 'ctest' 'PASS' "$line" }
                elseif ($null -eq $code) { Add-Result 'ctest' 'FAIL' 'killed on a deadline' }
                else { Add-Result 'ctest' 'FAIL' "exit $code; $line" }
            }
            if (Want 'nosdl') { Step-NoSdl $fullBuild }
        }
    }
} else {
    Add-Result 'prerequisites' 'NOT-RUN' 'the toolchain or the source tree was not usable; nothing downstream ran'
}

# ================================================================= the report

$script:results | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $OutDir 'summary.json')

$counts = @{}
foreach ($o in @('PASS','FAIL','NOT-RUN','ENVIRONMENT','INFO')) {
    $counts[$o] = @($script:results | Where-Object { $_.outcome -eq $o }).Count
}

$lines = @(
    '# CNA Win32 native validation', '',
    "Recorded $(Get-Date -Format s) on ``$env:COMPUTERNAME``.", '',
    $(if ($script:isVirtual) {
        'This **is** native Windows, inside a virtual machine. The Windows API, the message loop, ' +
        'the clipboard, the shell and the MSVC ABI are the real ones. The **GPU is virtual**: ' +
        'Direct3D, WGL and Vulkan results below describe the VM''s virtual adapter and are not ' +
        'evidence about a physical Windows GPU driver.'
    } else { 'This is native Windows on physical hardware.' }), '',
    ("PASS {0} / FAIL {1} / NOT-RUN {2} / ENVIRONMENT {3} / INFO {4}" -f `
        $counts['PASS'], $counts['FAIL'], $counts['NOT-RUN'], $counts['ENVIRONMENT'], $counts['INFO']), '',
    '| Check | Outcome | Detail |', '|---|---|---|')
foreach ($r in $script:results) {
    $lines += ('| {0} | {1} | {2} |' -f $r.check, $r.outcome, ($r.detail -replace '\|', '/'))
}
$lines += @('', '## Still to be checked by a person', '',
    'The interactive checks in `docs/testing-win32-native.md` -- real IME composition, Alt+Tab and',
    'foreground-lock behaviour, dragging between monitors of different scale, clipboard against a',
    'real application -- are not in this script and are recorded there.')
$lines | Set-Content -LiteralPath (Join-Path $OutDir 'report.md') -Encoding UTF8

Write-Output ''
Write-Output ("report: $(Join-Path $OutDir 'report.md')  --  $script:failed FAIL")
exit $(if ($script:failed -gt 0) { 1 } else { 0 })
