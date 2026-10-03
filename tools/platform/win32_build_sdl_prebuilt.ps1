# SPDX-License-Identifier: MS-PL
#
# plans/plan_win32_native_validation.md WINNATIVE-0023: build the vendored SDL once into the
# persistent prefix that both the real configure and the standalone harness resolve SDL from.
#
# The harness globs <repo>/.sdl-prebuilt-<System>-<Processor>* and appends "<match>/install" to
# CMAKE_PREFIX_PATH. Building SDL there once is far cheaper than a second full-framework configure
# and produces the same artefact the real build would have, so SDL3 regressions measure the
# backend rather than the dependency's build system.
#
# Two things this script exists to get right:
#   * SDL installs its CMake package files to <prefix>/cmake/SDL3Config.cmake, NOT to the
#     lib/cmake/SDL3 that most projects use. A guard written against lib/cmake reports "not
#     installed" for a perfectly good install and rebuilds SDL from source every time -- this one
#     searches for the config file itself.
#   * the build tree lives under C:\cna\build, outside the repository, so `git clean -x` during a
#     sync cannot reach it. The install prefix is inside the repository and IS untracked, which is
#     why windows_vm_sync.sh has to exclude it explicitly; with the tree kept, a wiped prefix costs
#     one install step rather than a full SDL compile.
#
# Usage: win32_build_sdl_prebuilt.ps1 [-Force]

[CmdletBinding()]
param(
    [string] $Source   = 'C:/src/cna/third_party/SDL',
    [string] $Prefix   = 'C:/src/cna/.sdl-prebuilt-Windows-AMD64',
    [string] $Build    = 'C:/cna/build/sdl3',
    [int]    $Parallel = 6,
    [switch] $Force
)

$ProgressPreference = 'SilentlyContinue'
$ErrorActionPreference = 'Continue'

if (-not (Test-Path "$Source/CMakeLists.txt")) { Write-Output "no SDL source at $Source"; exit 2 }

# SDL3 installs <prefix>/cmake/SDL3Config.cmake rather than the more common lib/cmake location.
# Search for the file instead of assuming the directory, which avoids rebuilding a good install.
function Find-SdlConfig {
    param([string] $Root)
    if (-not (Test-Path $Root)) { return $null }
    $hit = Get-ChildItem -Path $Root -Recurse -Filter 'SDL3Config.cmake' -ErrorAction SilentlyContinue |
           Select-Object -First 1
    return $hit
}

if ((-not $Force) -and (Find-SdlConfig "$Prefix/install")) {
    Write-Output 'SDL3 already installed'
    exit 0
}

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { Write-Output 'no MSVC installation'; exit 2 }
cmd /c "`"$(Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat')`" >nul 2>&1 && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($matches[1])" -Value $matches[2] }
}

$options = @('-DSDL_SHARED=ON', '-DSDL_STATIC=OFF', '-DSDL_TESTS=OFF', '-DSDL_EXAMPLES=OFF', '-DSDL_INSTALL_TESTS=OFF')

Write-Output '=== configure SDL3 ==='
cmake -S $Source -B $Build -G Ninja `
      -DCMAKE_BUILD_TYPE=Release `
      "-DCMAKE_INSTALL_PREFIX=$Prefix/install" `
      @options 2>&1 | Select-Object -Last 6
if ($LASTEXITCODE -ne 0) { Write-Output 'SDL CONFIGURE FAILED'; exit 1 }

Write-Output '=== build + install SDL3 ==='
cmake --build $Build --parallel $Parallel --target install 2>&1 | Select-Object -Last 5
if ($LASTEXITCODE -ne 0) { Write-Output 'SDL BUILD FAILED'; exit 1 }

$config = Find-SdlConfig "$Prefix/install"
Write-Output "installed: $($null -ne $config)$(if ($config) { " ($($config.FullName))" })"
Write-Output "free GB: $([math]::Round((Get-PSDrive C).Free/1GB,2))"
