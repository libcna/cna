# SPDX-License-Identifier: MS-PL
#
# plans/plan_win32_native_validation.md WINNATIVE-0023: build the vendored SDL once into the
# persistent prefix that both the real configure and the standalone harness resolve SDL from.
#
# The harness globs <repo>/.sdl-prebuilt-<System>-<Processor>* and appends "<match>/install" to
# CMAKE_PREFIX_PATH. Building SDL there once is far cheaper than a second full-framework configure
# and produces the same artefact the real build would have, so the SDL3 and SDL2 regressions
# measure the backend rather than the dependency's build system.
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
# SDL2 shares the prefix rather than taking one of its own: the two install into disjoint
# subtrees (include/SDL2 and lib/cmake/SDL2 against include/SDL3 and cmake/SDL3Config.cmake), the
# harness globs one prefix, and CNA's SDL2 backend is an independent edge that has to be exercised
# on this operating system too. Its source is not a submodule -- cmake/ThirdPartySDL2.cmake fetches
# a pinned SDL 2.30.11 at configure time -- so the guest is given that same revision to build.
#
# Usage: win32_build_sdl_prebuilt.ps1 [-Version 3|2] [-Force]

[CmdletBinding()]
param(
    [ValidateSet('2', '3')]
    [string] $Version  = '3',
    [string] $Source   = '',
    [string] $Prefix   = 'C:/src/cna/.sdl-prebuilt-Windows-AMD64',
    [string] $Build    = '',
    [int]    $Parallel = 6,
    [switch] $Force
)

if (-not $Source) { $Source = if ($Version -eq '3') { 'C:/src/cna/third_party/SDL' } else { 'C:/cna/src/sdl2' } }
if (-not $Build)  { $Build  = "C:/cna/build/sdl$Version" }

$ProgressPreference = 'SilentlyContinue'
$ErrorActionPreference = 'Continue'

if (-not (Test-Path "$Source/CMakeLists.txt")) { Write-Output "no SDL source at $Source"; exit 2 }

# SDL3 installs <prefix>/cmake/SDL3Config.cmake; SDL2 installs <prefix>/lib/cmake/SDL2 and spells
# its config file sdl2-config.cmake. Both spellings are searched rather than assumed -- a guard
# written against the wrong one reports "not installed" for a good install and rebuilds SDL every
# time, which is the bug the first version of this script had.
function Find-SdlConfig {
    param([string] $Root, [string] $Ver)
    if (-not (Test-Path $Root)) { return $null }
    foreach ($name in @("SDL$Ver`Config.cmake", "sdl$Ver-config.cmake")) {
        $hit = Get-ChildItem -Path $Root -Recurse -Filter $name -ErrorAction SilentlyContinue |
               Select-Object -First 1
        if ($hit) { return $hit }
    }
    return $null
}

if ((-not $Force) -and (Find-SdlConfig "$Prefix/install" $Version)) {
    Write-Output "SDL$Version already installed"
    exit 0
}

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { Write-Output 'no MSVC installation'; exit 2 }
cmd /c "`"$(Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat')`" >nul 2>&1 && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($matches[1])" -Value $matches[2] }
}

# SDL2 spells its own switches differently (SDL_TEST, and no SDL_EXAMPLES at all), so the two are
# not one flag list with a version substituted into it.
$options = if ($Version -eq '3') {
    @('-DSDL_SHARED=ON', '-DSDL_STATIC=OFF', '-DSDL_TESTS=OFF', '-DSDL_EXAMPLES=OFF', '-DSDL_INSTALL_TESTS=OFF')
} else {
    @('-DSDL_SHARED=ON', '-DSDL_STATIC=OFF', '-DSDL_TEST=OFF', '-DSDL2_DISABLE_SDL2MAIN=ON')
}

Write-Output "=== configure SDL$Version ==="
cmake -S $Source -B $Build -G Ninja `
      -DCMAKE_BUILD_TYPE=Release `
      "-DCMAKE_INSTALL_PREFIX=$Prefix/install" `
      @options 2>&1 | Select-Object -Last 6
if ($LASTEXITCODE -ne 0) { Write-Output 'SDL CONFIGURE FAILED'; exit 1 }

Write-Output "=== build + install SDL$Version ==="
cmake --build $Build --parallel $Parallel --target install 2>&1 | Select-Object -Last 5
if ($LASTEXITCODE -ne 0) { Write-Output 'SDL BUILD FAILED'; exit 1 }

$config = Find-SdlConfig "$Prefix/install" $Version
Write-Output "installed: $($null -ne $config)$(if ($config) { " ($($config.FullName))" })"
Write-Output "free GB: $([math]::Round((Get-PSDrive C).Free/1GB,2))"
