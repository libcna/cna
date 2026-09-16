# CNA Win32 backend — native Windows validation and hardening

> **Status: IN PROGRESS.** This workstream takes the `CNA_PLATFORM=WIN32` backend that
> [`plan_win32.md`](plan_win32.md) built and validated *under Wine on Linux*, and re-establishes
> every one of its claims on **real Windows 10 with the real Microsoft toolchain**. Where Wine and
> Windows disagree, Windows is authoritative and the difference is a defect to fix, not a note to
> write.
>
> **Goal:** leave CNA with a Win32 backend whose evidence is native-Windows evidence, and with
> reusable infrastructure (`tools/platform/windows_vm_exec.sh`, `windows_vm_sync.sh`,
> `windows_vm_validate.sh`, `win32_run_interactive.ps1`, `validate_win32_native.ps1`) that lets any
> later session repeat the whole run from Linux with one command.
>
> **Non-goal:** redesigning `Win32Platform`, adding capabilities that are deliberately `false`
> (IME, gamepad, tray, …), or treating a VirtualBox virtual-GPU limitation as a CNA defect.

---

## 0. Baseline

| Fact | Value |
|---|---|
| Baseline commit | `4cf33c2b` (`docs(WAYLAND-…): the final audit`), the tip of `next` |
| Branch | `win32-native-validation`, created from that commit |
| Working tree at start | clean |
| Host | Debian 13, Linux 6.12, x86-64 |
| sharp-runtime | sibling checkout, branch `next`, `88c12f15` |

### The laboratory

| Fact | Value |
|---|---|
| VirtualBox | 7.2.8 r173730 |
| VM name / UUID | `win10_local` / `6de53d51-f55d-4bf5-af20-9033279f6617` |
| Guest | Microsoft Windows 10 Home 22H2, build **19045.2965**, x64 |
| vCPU / RAM | 4 / 8192 MB (raised from 4096 for this workstream) |
| Graphics | VBoxSVGA (WDDM), driver 7.2.8.23730, 256 MB VRAM, 3D acceleration on |
| Guest OpenGL | Mesa 24.0.2, `SVGA3D` driver (the VirtualBox guest GL stack) |
| Guest Additions | 7.2.8 |
| Network | NAT, loopback-only forwards `127.0.0.1:2222 → 22` and `127.0.0.1:5985 → 5985` |
| Automation | OpenSSH Server (`OpenSSH.Server~~~~0.0.1.0`), **public-key only**, default shell PowerShell 5.1 |
| Snapshot | `clean-ssh-no-devtools` — the guest with SSH configured and no development tools |
| Toolchain | MSVC **19.44.35229** (VS Build Tools 2022 17.14, `C:\BuildTools`), Windows SDK **10.0.26100.0**, CMake 3.31.6, Ninja 1.12.1, Git 2.47.1, Python 3.12.8 |
| Disk | 85.9 GB free before; 77.3 GB after the page file grew with the RAM increase; **66.4 GB** after the toolchain |

**The VM has a virtual GPU.** Everything this workstream records is therefore one of two different
things, and they are never conflated:

* **native Windows API validation** — `user32`, `gdi32`, `ole32`, `shell32`, the real message
  loop, the real clipboard, the real MSVC ABI. A VM does not weaken any of this; it *is* Windows.
* **virtual GPU validation** — D3D11/D3D12/WGL through VBoxSVGA. Useful integration evidence,
  and *not* evidence about a physical Windows GPU driver.

Physical-Windows-GPU validation remains **not done** and is recorded as such.

### Two Windows sessions, and why it matters

Windows OpenSSH puts a client in **session 0**, on a window station named `Service-0x0-<luid>$`.
The logged-on desktop is **session 1**, on `WinSta0`. That is a fine place to compile and a wrong
place to judge windows, focus, the clipboard (which is *per window station*), monitors or DPI.

Measured, same machine and same moment: `Screen.AllScreens` reports **1024x768** from session 0 and
**1920x1080 with a 1040-high work area** from session 1. So every desktop-facing check here is
launched through `tools/platform/win32_run_interactive.ps1`, which uses Task Scheduler with logon
type `InteractiveToken` — a logon type that borrows the interactively logged-on user's token at run
time and therefore needs no stored password.

---

## 1. Tasks

| ID | Task | Status | Evidence |
|---|---|---|---|
| WINNATIVE-0001 | Discover VirtualBox and the Windows VM; record its configuration | ✅ | §0 |
| WINNATIVE-0002 | Boot the VM autonomously and reach a logged-in desktop | ✅ | headless start, autologon, console screenshot |
| WINNATIVE-0003 | Reproducible remote automation from Linux (SSH, key auth, no stored password) | ✅ | `windows_vm_exec.sh`; interactive-session bridge in `win32_run_interactive.ps1` |
| WINNATIVE-0004 | Windows environment inventory | ✅ | §0 |
| WINNATIVE-0005 | Pre-toolchain snapshot | ✅ | `clean-ssh-no-devtools` |
| WINNATIVE-0006 | Install the minimum native toolchain | ✅ | §0; §2 records the component-id trap |
| WINNATIVE-0007 | Exact-commit source sync Linux → VM | ✅ | `windows_vm_sync.sh`; full 113 MB once, then 12 KB per commit |
| WINNATIVE-0008 | First MSVC configure and build of the platform module | ✅ | `C:\cna\build\win32-standalone`, MSVC 19.44, clean |
| WINNATIVE-0009 | Full integrated CNA + sharp-runtime MSVC build | 🔄 | configure reaches generate; two defects found and fixed (§2) |
| WINNATIVE-0010 | Win32 platform suite on native Windows | ✅ | **389 tests: 386 passed, 3 skipped, 0 failed** on the interactive desktop |
| WINNATIVE-0011 | `PlatformConformanceTests` on native Win32 | ✅ | 38 + 20 parameterized cases in the run above, 1 skip by design |
| WINNATIVE-0012 | Direct3D 11/12 probe on a CNA platform HWND | ✅ | D3D11 complete; D3D12 `DXGI_ERROR_UNSUPPORTED` (environment) |
| WINNATIVE-0013 | Window-lifecycle stress and leak harness | ✅ | 9 250 operations, every counter flat (§3) |
| WINNATIVE-0014 | Full `CnaTests` on native Windows | ⬜ | |
| WINNATIVE-0015 | SDL-free proof by PE dependency inspection | ⬜ | |
| WINNATIVE-0016 | DPI at 100/125/150/200 % | ⬜ | |
| WINNATIVE-0017 | Keyboard, text input, mouse, Raw Input on the real desktop | ⬜ | |
| WINNATIVE-0018 | Clipboard against a real Windows application | ⬜ | |
| WINNATIVE-0019 | COM lifetime, file dialogs, message box | ⬜ | |
| WINNATIVE-0020 | WGL/OpenGL through the guest's Mesa SVGA3D stack | ⬜ | |
| WINNATIVE-0021 | A real CNA application, and a soak run | ⬜ | |
| WINNATIVE-0022 | MSVC AddressSanitizer on the lifecycle-heavy tests | ⬜ | |
| WINNATIVE-0023 | SDL3 / SDL2 / HEADLESS regression on native Windows | ⬜ | |
| WINNATIVE-0024 | Linux regression after every generic fix | ⬜ | |

---

## 2. Findings

### WINNATIVE-F1 — a DIRECTX11 configure has been broken since 2026-09-08 *(fixed)*

**Symptom.** `cmake -DCNA_GRAPHICS_RENDERER=DIRECTX11 -DCNA_BUILD_TESTS=ON` configures
successfully and then fails at the *generate* step:

```
Cannot find source file: .../modules/renderers/easygl/examples/easygl_rendertargetcube_sample_test.cpp
No SOURCES given to target: cna_test_directx11_rendertargetcube_sample
```

**Root cause.** `fab1a2111` (`feat(SOFTWARE-119): implement CPU render-target cubes`) moved that
test to `modules/graphics/examples/rendertargetcube_sample_test.cpp` and did not update
`cmake/DirectXParityTests.cmake`, which is a central inventory naming sources owned by *other*
modules. Nothing connected the two.

**Why nothing caught it.** Not a Windows problem at all — any `DIRECTX11`/`DIRECTX12` configure
hits it on any host. The job that would have caught it,
`.github/workflows/d3d-windows-ci.yml`, is `workflow_dispatch`-only.

**Fix.** The fixture points at `${CNA_GRAPHICS_EXAMPLES_DIR}`, where the file is; and
`cna_d3d_parity_fixture` now refuses a `SOURCE` that does not exist, naming the fixture, the path
and the inventory file. The inventory will drift again; next time it says so, at configure time.

### WINNATIVE-F2 — three Win32 tests asserted the size of the screen *(fixed)*

**Symptom.** `Win32RendererBridge.TheHandleSurvivesAResizeUnchanged`,
`Win32RendererBridge.TheSwapchainSizeComesFromTheDrawableSizeNotTheLogicalOne` and
`Win32WindowTest.ResizeAlsoMeansTheClientArea` failed with sizes like 964x518 for a requested
1024x768.

**Root cause.** Windows silently clamps a client area to what the monitor's work area can hold once
the window frame is added. A test that names an absolute size and asserts it round-tripped is
therefore also asserting a minimum screen size, without saying so.

**The controlled experiment.** The same `cna_platform_tests.exe`, on the same machine at the same
moment: **passes** on the 1920x1080 interactive desktop, **fails** in session 0 whose service
window station reports 1024x768. The mingw/Wine cross-build fails it too — that prefix has a
1024x768 virtual desktop at 192 DPI, which is exactly where 964x518 comes from.

**Fix.** `modules/platform/tests/CNA/Platform/Win32TestDesktop.hpp` derives the target from
`SPI_GETWORKAREA` and the measured frame. The assertion stays exact — the size must still
round-trip precisely — and the hidden claim about the screen is gone. Wine: **389 tests, 388
passed, 1 skipped, 0 failed**, up from 3 failures.

### WINNATIVE-F3 — `Microsoft.VisualStudio.Component.Windows10SDK.20348` installs nothing

The VS 2022 bootstrapper accepts an unknown `--add` component id, exits **0**, and installs no
Windows SDK at all. The result is an MSVC installation with no `rc.exe`, which CMake reports as
"The C++ compiler is not able to compile a simple test program" — a misleading message for a
missing resource compiler. Asking for `Microsoft.VisualStudio.Workload.VCTools
--includeRecommended` is the form that cannot be silently wrong; it installed SDK 10.0.26100.0.
Environment, not a CNA defect, but recorded because the next person will hit it.

### WINNATIVE-F4 — three transport defects in this workstream's own tooling *(fixed)*

Recorded because the infrastructure is a deliverable. (a) PowerShell serialises `Write-Host` as
CLIXML when stdout is a pipe, corrupting captured results — everything uses `Write-Output`.
(b) `ssh` without `-n` ate the here-doc that `--stdin` was about to read, so the remote command
arrived empty. (c) `git bundle create f ^A <sha>` names no ref and git refuses it as empty, so
every sync after the first silently left the guest on the previous commit — the one way this tool
could have lied about what was being tested. It bundles `refs/heads/<branch>` now.

### WINNATIVE-F5 — `<windows.h>` collisions, and `NOMINMAX` missing repo-wide *(fixed)*

The new stress harness hit both hazards `docs/platform-win32.md` warns hosts about, and one of them
only under MSVC: `CreateWindow` is rewritten to `CreateWindowA` (both toolchains), and `min`/`max`
become function-like macros unless `NOMINMAX` is defined first — which mingw-w64's libstdc++ does
for you in `os_defines.h` and cl.exe does not.

Then the same defect appeared in **production code**. `NOMINMAX` was defined nowhere for CNA's own
targets, and the DirectX 11 renderer reaches `<windows.h>` through `<d3d11.h>`:

```
D3D11RenderTargets.cpp(33): error C2589: '(': illegal token on right side of '::'
D3D11RenderTargets.cpp(33): error C2059: syntax error: ')'
```

— which is what `std::max(1, w / 2)` becomes. Four files, none of which had ever been compiled by
cl.exe. It is now declared once on `cna_project_options`, beside `/utf-8`.
`WIN32_LEAN_AND_MEAN` is deliberately *not* set with it: that removes whole headers rather than two
macros.

This is the shape of most of what this workstream finds — not that the code is wrong, but that one
toolchain was quietly covering for it.

### WINNATIVE-F6 — sharp-runtime's `XmlWriter` cannot be compiled by MSVC *(fixed, in sharp-runtime)*

`XmlWriter::Flush` used `std::fopen`, cl.exe reports that as C4996 ("consider `fopen_s`"), and
sharp-runtime compiles every module with `/W4 /WX`. The full build stopped at 129 of 2109 targets.
Fixed in the sibling repository (`5beb70cf`) with `std::ofstream` rather than a suppression: the
code is standard either way, and the stream closes itself on every path out of the function —
including the throw the old code left a `FILE*` open on. The other `std::fopen`/`std::getenv` call
sites there are inside POSIX-only blocks and are untouched.

### WINNATIVE-F7 — the clipboard interop harness was measuring PowerShell *(fixed)*

The first Notepad interop run reported six failures on non-ASCII samples, and **none of them were
CNA's**. Text handed to a native process on a PowerShell command line is re-encoded in the ANSI
code page, and a native process's stdout is decoded in the console code page, so both ends of the
harness's own transport were lossy — a Czech sample came back as "17 characters", which is its
UTF-8 byte count read as single bytes.

Recorded because the conclusion nearly went the other way: a harness defect that looks exactly like
an encoding bug in the thing under test is the most expensive kind. The transport is now a file of
UTF-8 bytes with an explicit no-BOM encoding on the PowerShell side, and all ten checks pass.

---

## 3. Measurements

### The platform suite, native, on the interactive desktop

```
389 tests from 39 test suites.  386 passed, 3 skipped, 0 failed.
  skipped: Win32GraphicsServices.VulkanInstanceExtensionsNameTheWin32Surface     (no vulkan-1.dll)
  skipped: Win32GraphicsServices.VulkanSurfaceCreationRefusesAMissingInstanceOrWindow
  skipped: EveryImplementation/PlatformConformance.AnUnsupportedCapabilityRefusesNamingItself/Win32
```

The three tests that fail under Wine on the Linux host pass here. Windows is authoritative, and it
says the backend is right and the Wine environment was the problem.

### Direct3D on a CNA platform HWND

```
win32 platform window       ok  -- hwnd=00000000003C01AC, client=640x480
d3d11 device + swap chain   ok
d3d11 back buffer size      ok
d3d11 clear                 ok
d3d11 present               ok
d3d11 resize                ok
d3d11 present after resize  ok
d3d12 device                unavailable -- hr=0x887A0004 (DXGI_ERROR_UNSUPPORTED)
```

D3D11 is *virtual GPU* evidence. D3D12 is an **environment limitation** of the VBoxSVGA adapter,
not a CNA defect, and nothing was changed to chase it.

### Lifecycle stress — 9 250 operations, Windows' own accounting

```
phase              ops    seconds   USER      GDI       handles     threads
create-destroy     500    13.3      5 -> 5    9 -> 9    161 -> 161  6 -> 6
resize            2000     1.4      5 -> 5    9 -> 9    161 -> 161  6 -> 6
show-hide         2000    16.7      5 -> 5    9 -> 9    161 -> 162  6 -> 5
minimize-restore  1000     3.7      5 -> 5    9 -> 9    162 -> 162  5 -> 5
fullscreen         250     1.7      5 -> 5    9 -> 13   162 -> 166  5 -> 6
multiple-windows   500     6.4      5 -> 5   13 -> 13   166 -> 166  6 -> 6
title-round-trip  2000     0.2      5 -> 5   13 -> 13   166 -> 166  6 -> 6
adopt-release     1000     0.2      5 -> 5   13 -> 13   166 -> 166  6 -> 6
```

Five hundred create/destroy cycles leave the USER object count exactly where it started, which is
the measurement Wine could not make: under Wine every `GetGuiResources` counter reads **0**.
`adopt-release` additionally asserts, a thousand times, that a host-owned `HWND` is still alive
after CNA's wrapper is destroyed.

---

## 4. How to repeat this

From a Linux checkout, with the VM present in VirtualBox:

```bash
tools/platform/windows_vm_exec.sh --start     # boot and wait for SSH
tools/platform/windows_vm_sync.sh             # put this exact commit into C:\src\cna
tools/platform/windows_vm_validate.sh         # configure, build, test, collect the report
```

`windows_vm_validate.sh` does all three and copies `report.md`/`summary.json` back into
`reports/win32-native/<timestamp>/` (gitignored — the conclusions belong in this file).
