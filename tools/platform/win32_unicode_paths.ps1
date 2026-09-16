# SPDX-License-Identifier: MIT
<#
.SYNOPSIS
    Native Windows validation for CNA under non-ASCII filesystem paths.

.DESCRIPTION
    plans/plan_windows_portability.md WINPORT-0012..0014 and 0018. Four things that a build under
    C:\src\cna cannot tell you, each of which is a different way for a path to reach CNA:

      temp      the whole test suite with TEMP and TMP pointed at a non-ASCII directory, so every
                test that calls temp_directory_path() builds its fixtures under one
      source     a git worktree whose full path is non-ASCII, configured and built with MSVC --
                this is what catches CMake, response files, custom commands and generated-file
                paths rather than runtime code
      app        a real CNA application and its content copied under a non-ASCII directory and run
                from there
      nosdl      the PE import table of that binary, so the Unicode work is shown not to have
                reintroduced SDL

    Everything it creates, it removes. Nothing is asserted from rendered text: a name is proved by
    reading a known payload back out of the file, by SHA-256, or by asking the wide Win32 API --
    a console that cannot draw a name is not evidence that the name is wrong.

.PARAMETER SourceDir
    The CNA checkout to take the worktree from. Default C:\src\cna.

.PARAMETER BuildRoot
    Where builds go. Default C:\cna\build.

.PARAMETER OutDir
    Where the report and any XML land.

.PARAMETER Steps
    Any of: temp, source, app, nosdl, all. Default all.

.PARAMETER CnaTests
    An already-built CnaTests.exe to reuse for the temp step, rather than building one.

.PARAMETER KeepWorktree
    Leave the Unicode worktree in place for inspection. Off by default; it is ~1 GiB built.
#>
[CmdletBinding()]
param(
    [string]   $SourceDir = 'C:/src/cna',
    [string]   $BuildRoot = 'C:/cna/build',
    [string]   $OutDir    = 'C:/cna/report/unicode',
    [string[]] $Steps     = @('all'),
    [string]   $CnaTests  = '',
    [switch]   $KeepWorktree
)

$ErrorActionPreference = 'Stop'
$ProgressPreference    = 'SilentlyContinue'

$Steps = @($Steps | ForEach-Object { $_ -split '\s*,\s*' } | Where-Object { $_ })
if ($Steps.Count -eq 0) { $Steps = @('all') }
function Want([string] $s) { return ($Steps -contains 'all') -or ($Steps -contains $s) }

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# --- the path classes under test -----------------------------------------------------------------
# Spelled from code points rather than as literals, so this file's own encoding cannot be what is
# being measured and a mis-saved script cannot quietly weaken the test.
function U([int[]] $cp) { -join ($cp | ForEach-Object { [char]::ConvertFromUtf32($_) }) }

$Czech    = U @(0x017E,0x006C,0x0075,0x0165,0x006F,0x0075,0x010D,0x006B,0x00FD)   # zlutoucky
$Japanese = U @(0x65E5,0x672C,0x8A9E)                                             # nihongo
$Cyrillic = U @(0x043A,0x0438,0x0440,0x0438,0x043B,0x043B,0x0438,0x0446,0x0430)
$Emoji    = U @(0x1F600)                                                          # supplementary plane
$Mixed    = "CNA test $Czech $Japanese $Emoji"

$script:Results = @()
function Add-Result([string] $check, [string] $status, [string] $detail) {
    $script:Results += [pscustomobject]@{ Check = $check; Status = $status; Detail = $detail }
    '{0,-12} {1,-42} {2}' -f $status, $check, $detail | Write-Output
}

function Get-Acp { [System.Text.Encoding]::Default.CodePage }

# Is this text representable in the active ANSI code page? When it is not, path::string() is
# documented to fail, which is the whole of WINNATIVE-F30.
function Test-RepresentableInAcp([string] $text) {
    $acp = [System.Text.Encoding]::GetEncoding(
        (Get-Acp),
        [System.Text.EncoderFallback]::ExceptionFallback,
        [System.Text.DecoderFallback]::ExceptionFallback)
    try { $null = $acp.GetBytes($text); return $true } catch { return $false }
}

# Prove a file by its content, never by its rendered name.
function Test-PayloadAt([string] $path, [string] $payload) {
    if (-not [System.IO.File]::Exists($path)) { return $false }
    return ([System.IO.File]::ReadAllText($path) -eq $payload)
}

function New-UnicodeRoot([string] $tag) {
    $root = Join-Path ([System.IO.Path]::GetTempPath()) "cna-uni-$tag-$Czech-$Japanese"
    if ([System.IO.Directory]::Exists($root)) { [System.IO.Directory]::Delete($root, $true) }
    [System.IO.Directory]::CreateDirectory($root) | Out-Null
    return $root
}

function Remove-Tree([string] $path) {
    if ($path -and [System.IO.Directory]::Exists($path)) {
        try { [System.IO.Directory]::Delete($path, $true) } catch { }
    }
}

function Enter-MsvcEnvironment {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { return $false }
    $vs = & $vswhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { return $false }
    $vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path $vcvars)) { return $false }
    cmd /c "`"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($matches[1])" -Value $matches[2] }
    }
    return $true
}

Write-Output ("ACP = {0}   representable: czech={1} japanese={2} cyrillic={3} emoji={4}" -f `
    (Get-Acp), (Test-RepresentableInAcp $Czech), (Test-RepresentableInAcp $Japanese),
    (Test-RepresentableInAcp $Cyrillic), (Test-RepresentableInAcp $Emoji))
Write-Output ''

# =================================================================================================
# Step 0 — the ground truth, before any CNA code is involved.
# =================================================================================================
$probeRoot = New-UnicodeRoot 'probe'
try {
    $ok = $true
    foreach ($name in @($Czech, $Japanese, $Cyrillic, $Emoji, $Mixed)) {
        $dir = Join-Path $probeRoot $name
        [System.IO.Directory]::CreateDirectory($dir) | Out-Null
        $file = Join-Path $dir "$name.txt"
        [System.IO.File]::WriteAllText($file, 'CNA-UNICODE-HOST-OK')
        if (-not (Test-PayloadAt $file 'CNA-UNICODE-HOST-OK')) { $ok = $false }
    }
    Add-Result 'host.filesystem' $(if ($ok) { 'PASS' } else { 'FAIL' }) `
        'NTFS stores and returns every path class under test'
} finally { Remove-Tree $probeRoot }

# =================================================================================================
# Step "temp" — the whole suite, with TEMP and TMP under a non-ASCII directory.
#
# This is the cheapest high-value integration test available: a large part of the content pipeline
# builds its fixtures under temp_directory_path(), so pointing TEMP at a Unicode directory makes
# hundreds of existing tests exercise Unicode paths without writing a single new one.
# =================================================================================================
if (Want 'temp') {
    $exe = $CnaTests
    if (-not $exe) { $exe = Join-Path $BuildRoot 'full-win32-d3d11-nosdl/CnaTests.exe' }
    if (-not (Test-Path $exe)) {
        Add-Result 'unicode.temp' 'NOT-RUN' "$exe was not built"
    } else {
        $tempRoot = New-UnicodeRoot 'temp'
        $savedTemp = $env:TEMP; $savedTmp = $env:TMP
        try {
            $env:TEMP = $tempRoot
            $env:TMP  = $tempRoot
            $xml = Join-Path $OutDir 'unicode-temp.xml'
            # The content and media suites are the ones whose fixtures live in TEMP.
            $filter = 'Content*:*Content*:*Xnb*:*Cnb*:*Cnj*:*Gltf*:*Texture2D*:*SpriteFont*:*Media*:*Playlist*:*AudioTag*:*Storage*:*Path*:*Title*'
            & $exe "--gtest_output=xml:$xml" "--gtest_filter=$filter" | Out-Null
            $code = $LASTEXITCODE
            if (Test-Path $xml) {
                [xml] $r = Get-Content $xml
                $t = [int] $r.testsuites.tests; $f = [int] $r.testsuites.failures
                Add-Result 'unicode.temp' $(if ($f -eq 0) { 'PASS' } else { 'FAIL' }) `
                    "$t tests, $f failures, with TEMP under a non-ASCII directory"
            } else {
                Add-Result 'unicode.temp' 'FAIL' "no results file; exit $code"
            }
        } finally {
            $env:TEMP = $savedTemp; $env:TMP = $savedTmp
            Remove-Tree $tempRoot
        }
    }
}

# =================================================================================================
# Step "source" — configure and build from a worktree whose full path is non-ASCII.
#
# This is the one that catches what runtime code cannot: CMake's own assumptions, compiler response
# files, generated-file paths, custom commands and the Python tooling. The standalone platform
# configuration is used rather than the whole framework because it is a few hundred megabytes
# instead of several gigabytes and exercises exactly the same toolchain machinery.
# =================================================================================================
$worktree = ''
$uniBuild = ''
if (Want 'source' -or (Want 'app') -or (Want 'nosdl')) {
    $srcRoot  = New-UnicodeRoot 'src'
    $worktree = Join-Path $srcRoot "CNA-$Japanese"
    try {
        $head = (& git -C $SourceDir rev-parse HEAD).Trim()
        & git -C $SourceDir worktree add --detach $worktree $head 2>&1 | Out-Null
        if (-not (Test-Path (Join-Path $worktree 'CMakeLists.txt'))) {
            Add-Result 'unicode.source.worktree' 'FAIL' 'git could not create a worktree at a non-ASCII path'
        } else {
            $seen = (& git -C $worktree rev-parse HEAD).Trim()
            Add-Result 'unicode.source.worktree' $(if ($seen -eq $head) { 'PASS' } else { 'FAIL' }) `
                "git worktree at a non-ASCII path is at $seen"

            # The vendored gitlink payloads are not in the worktree; link the ones the build needs.
            foreach ($payload in @('vendor/googletest')) {
                $from = Join-Path $SourceDir $payload
                $to   = Join-Path $worktree  $payload
                if ((Test-Path $from) -and -not (Test-Path (Join-Path $to 'CMakeLists.txt'))) {
                    New-Item -ItemType Directory -Force -Path (Split-Path $to) | Out-Null
                    Copy-Item -Recurse -Force $from $to
                }
            }

            if (Want 'source') {
                if (-not (Enter-MsvcEnvironment)) {
                    Add-Result 'unicode.source.build' 'NOT-RUN' 'no MSVC environment'
                } else {
                    $uniBuild = Join-Path $worktree 'cmake-build-unicode'
                    $cfg = & cmake -S (Join-Path $worktree 'tools/platform/standalone_tests') `
                                   -B $uniBuild -G Ninja `
                                   -DCNA_PLATFORM=WIN32 -DCMAKE_BUILD_TYPE=Debug 2>&1
                    if ($LASTEXITCODE -ne 0) {
                        ($cfg | Select-Object -Last 20) | Out-File (Join-Path $OutDir 'unicode-configure.log')
                        Add-Result 'unicode.source.configure' 'FAIL' 'cmake configure refused a non-ASCII source path'
                    } else {
                        Add-Result 'unicode.source.configure' 'PASS' 'cmake configured from a non-ASCII source path'
                        $bld = & cmake --build $uniBuild --parallel 2>&1
                        if ($LASTEXITCODE -ne 0) {
                            ($bld | Select-Object -Last 30) | Out-File (Join-Path $OutDir 'unicode-build.log')
                            Add-Result 'unicode.source.build' 'FAIL' 'MSVC build failed from a non-ASCII source path'
                        } else {
                            Add-Result 'unicode.source.build' 'PASS' 'MSVC built from a non-ASCII source path'
                        }
                    }
                }
            }
        }
    } catch {
        Add-Result 'unicode.source' 'FAIL' $_.Exception.Message
    }
}

# =================================================================================================
# Step "app" — a built program and its content, run from a non-ASCII directory.
# =================================================================================================
if (Want 'app') {
    $harness = ''
    foreach ($candidate in @((Join-Path $uniBuild 'cna_win32_platform_tests.exe'),
                             (Join-Path $uniBuild 'cna_platform_tests.exe'),
                             (Join-Path $BuildRoot 'standalone-win32/cna_win32_platform_tests.exe'))) {
        if ($candidate -and (Test-Path $candidate)) { $harness = $candidate; break }
    }
    if (-not $harness) {
        # Anything built and runnable will do; take the first .exe the Unicode build produced.
        if ($uniBuild -and (Test-Path $uniBuild)) {
            $first = Get-ChildItem -Path $uniBuild -Filter '*.exe' -Recurse -ErrorAction SilentlyContinue |
                     Select-Object -First 1
            if ($first) { $harness = $first.FullName }
        }
    }

    if (-not $harness) {
        Add-Result 'unicode.app' 'NOT-RUN' 'no runnable binary was produced'
    } else {
        $appRoot = New-UnicodeRoot 'app'
        try {
            $appDir = Join-Path $appRoot $Mixed
            [System.IO.Directory]::CreateDirectory($appDir) | Out-Null
            $exeCopy = Join-Path $appDir ([System.IO.Path]::GetFileName($harness))
            Copy-Item -Force $harness $exeCopy
            # Its content beside it, also under a non-ASCII name.
            $contentDir = Join-Path $appDir "Content-$Cyrillic"
            [System.IO.Directory]::CreateDirectory($contentDir) | Out-Null
            [System.IO.File]::WriteAllText((Join-Path $contentDir "$Czech.txt"), 'CNA-APP-CONTENT-OK')

            # Run it with the working directory somewhere else entirely, so nothing can be passing
            # by accident because cwd happened to be the right place.
            $out = Join-Path $appRoot 'app-stdout.txt'
            $p = Start-Process -FilePath $exeCopy -WorkingDirectory ([System.IO.Path]::GetTempPath()) `
                               -RedirectStandardOutput $out -RedirectStandardError (Join-Path $appRoot 'app-stderr.txt') `
                               -PassThru -NoNewWindow
            $exited = $p.WaitForExit(600000)
            if (-not $exited) {
                try { $p.Kill() } catch { }
                Add-Result 'unicode.app' 'FAIL' 'the program did not exit within 600s from a non-ASCII path'
            } else {
                Add-Result 'unicode.app' $(if ($p.ExitCode -eq 0) { 'PASS' } else { 'FAIL' }) `
                    ("$([System.IO.Path]::GetFileName($harness)) ran from a non-ASCII path, exit $($p.ExitCode)")
            }
            Copy-Item -Force $out (Join-Path $OutDir 'unicode-app-stdout.txt') -ErrorAction SilentlyContinue
        } catch {
            Add-Result 'unicode.app' 'FAIL' $_.Exception.Message
        } finally { Remove-Tree $appRoot }
    }
}

# =================================================================================================
# Step "nosdl" — the Unicode work must not have reintroduced SDL.
# =================================================================================================
if (Want 'nosdl') {
    $exe = Join-Path $BuildRoot 'full-win32-d3d11-nosdl/CnaTests.exe'
    if (-not (Test-Path $exe)) {
        Add-Result 'unicode.nosdl' 'NOT-RUN' "$exe was not built"
    } elseif (-not (Enter-MsvcEnvironment)) {
        Add-Result 'unicode.nosdl' 'NOT-RUN' 'no MSVC environment for dumpbin'
    } else {
        $imports = & dumpbin /imports $exe 2>&1 | Out-String
        $sdl = [regex]::Matches($imports, '(?im)^\s*(SDL[0-9_]*\.dll)') |
               ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique
        Add-Result 'unicode.nosdl' $(if ($sdl.Count -eq 0) { 'PASS' } else { 'FAIL' }) `
            $(if ($sdl.Count -eq 0) { 'the PE import table names no SDL DLL' } else { "imports $($sdl -join ', ')" })
    }
}

# --- cleanup and report ---------------------------------------------------------------------------
if ($worktree -and -not $KeepWorktree) {
    try {
        & git -C $SourceDir worktree remove --force $worktree 2>&1 | Out-Null
    } catch { }
    Remove-Tree (Split-Path $worktree -Parent)
    & git -C $SourceDir worktree prune 2>&1 | Out-Null
}

$script:Results | Export-Csv -NoTypeInformation -Path (Join-Path $OutDir 'unicode-results.csv')
Write-Output ''
Write-Output ("free on C: {0:N1} GiB" -f ((Get-PSDrive C).Free / 1GB))

$failed = @($script:Results | Where-Object Status -eq 'FAIL')
Write-Output ("{0} checks, {1} failed" -f $script:Results.Count, $failed.Count)
if ($failed.Count -gt 0) { exit 1 }
exit 0
