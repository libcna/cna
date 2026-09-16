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
| WINNATIVE-0009 | Full integrated CNA + sharp-runtime MSVC build | ✅ | **0 failures, 38 executables, `CnaTests.exe` linked**; sixteen Windows-build defects found and fixed on the way (§2) |
| WINNATIVE-0010 | Win32 platform suite on native Windows | ✅ | **389 tests: 386 passed, 3 skipped, 0 failed** on the interactive desktop |
| WINNATIVE-0011 | `PlatformConformanceTests` on native Win32 | ✅ | 38 + 20 parameterized cases in the run above, 1 skip by design |
| WINNATIVE-0012 | Direct3D 11/12 probe on a CNA platform HWND | ✅ | D3D11 complete; D3D12 `DXGI_ERROR_UNSUPPORTED` (environment) |
| WINNATIVE-0013 | Window-lifecycle stress and leak harness | ✅ | 9 250 operations, every counter flat (§3) |
| WINNATIVE-0014 | Full `CnaTests` on native Windows | ✅ | **8123 tests run to completion, 0 errors**; the stack-overflow crash fixed (F17), failures classified (§2, §3) |
| WINNATIVE-0015 | SDL-free proof by PE dependency inspection | ✅ | 38 executables, `dumpbin /dependents`: no SDL artifact built, no SDL import (§3) |
| WINNATIVE-0016 | DPI at 100/125/150/200 % | ✅ | measured against both an unaware and a DPI-aware host; §3 |
| WINNATIVE-0017 | Keyboard, text input, mouse, Raw Input on the real desktop | ✅ | 24 checks through `SendInput`; §3 |
| WINNATIVE-0018 | Clipboard against a real Windows application | ✅ | 10/10 against Notepad, both directions; §3 |
| WINNATIVE-0019 | COM lifetime and host-ownership policy | ✅ | §3; dialogs/message box still to run |
| WINNATIVE-0020 | WGL/OpenGL through the guest's Mesa SVGA3D stack | 🔄 | a real WGL 3.3 core context is created, made current, swapped and destroyed by `Win32GraphicsServices.AContextEitherIsCreatedAndUsableOrFailsExplicitly` in the native suite run (it is not among the skips); the `wgl` check in the desktop harness records the driver and the context-cycle GDI count |
| WINNATIVE-0021 | A real CNA application, and a soak run | ⬜ | |
| WINNATIVE-0022 | MSVC AddressSanitizer on the lifecycle-heavy tests | ✅ | 389 tests, 0 failures, **0 AddressSanitizer reports** across the suite, the stress and the desktop checks |
| WINNATIVE-0023 | SDL3 / SDL2 / HEADLESS regression on native Windows | 🔄 | HEADLESS **161 tests, 0 failures**; SDL3 **336 tests, 0 failures, 7 skipped**; SDL2 in progress (§3) |
| WINNATIVE-0024 | Linux regression after every generic fix | 🔄 | full Linux rebuild exit 0; `CnaPlatformModuleTests` **511 tests, 501 passed, 10 skipped, 0 failed**; `CnaMathTests` 857 passed; `CnaContentTests` at its pre-existing 13 |

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

### WINNATIVE-F8 — the SDL-free Direct3D configuration could not configure *(fixed)*

`CNA_PLATFORM=WIN32 + CNA_ENABLE_SDL=OFF + CNA_GRAPHICS_RENDERER=DIRECTX11 + CNA_BUILD_TESTS=ON`
— the configuration this workstream exists to prove — did not reach a build at all:
`cna_directx11_test`/`cna_directx12_test` linked `SDL3::SDL3` unconditionally, and with SDL off that
target does not exist.

The link was vestigial for nearly the whole corpus: of the 123 shared graphics example programs
those fixtures are built from, **two** include an SDL header and **none** call `SDL_Init` or
`SDL_CreateWindow`. It is now guarded the way `modules/renderers/headless/examples` already guards
it. One fixture is genuinely SDL-coupled (`viewport_reset_after_resize_test.cpp` reads the physical
size out of the `SDL_Window` behind the CNA window), so the inventory gained a `REQUIRES_SDL` flag
and skips it with a configure-time message naming it. The SDL-free build is a real configuration,
it is one parity fixture short, and it says which one.

### WINNATIVE-F9..F16 — CNA has never been fully built for Windows *(fixed)*

Once the configure succeeded, the build found **eight** defects in a row that no Windows toolchain
could have got past. Several are not MSVC-specific at all — the mingw-w64 cross-build fails on them
identically — which means these translation units had never been compiled for Windows by anything.

| | Where | What |
|---|---|---|
| F9 | `XnaPipelineBridge.cpp` | `relative.native().starts_with("..")`. `path::native()` is `std::wstring` on Windows, so this compiled on Linux and nowhere else. Asked as a path question now, which also stops a directory named `..config` being mistaken for an escape from the content root. |
| F10 | `XnbBuiltInWriters.cpp` | The `OrderedDictionary` *include* was guarded by `SHARP_RUNTIME_HAS_NATIVE_INT128` while the type was used unconditionally. The two are unrelated — it was inside the guard by proximity to `System::Decimal`, which genuinely needs one. Every compiler with native `__int128` compiled the file; MSVC, which has none, did not. |
| F11 | `HttpNotificationChannel.cpp` | `<winsock2.h>` drags in `<windows.h>`, whose `ERROR` macro collides with `CNA::LogLevel::ERROR` in the `CNA/Logger.hpp` below it. The repository records this pitfall twice already (`ENetHostHandle.hpp` undefines it, `DirectX12Renderer.cpp` orders around it); this file followed neither. `modules/phone` is recent enough that it appears never to have been compiled for Windows. |
| F12 | sharp-runtime `Core.Base` | `System/Guid.cpp` calls `BCryptGenRandom` and nothing linked `bcrypt`, so every Windows consumer compiled the entire tree and then failed at **link** time. `Security.Cryptography.Random` already links it for the same reason. Fixed in the sibling repository (`33da53f5`). |
| F13 | `cmake/RendererDescriptorGate.cmake` | The gate compiled Vulkan's descriptor in any configuration that did not *select* Vulkan — including every one with no Vulkan SDK. `fatal error: vulkan/vulkan.h: No such file or directory`. The list beside it already names bgfx and the DirectX families for exactly this; Vulkan joins them, but conditionally (`find_package(Vulkan QUIET)`), because the descriptor *should* be compiled where the headers exist. |
| F14 | `IntermediateSerializer.cpp` | The last unguarded `System::Decimal`. sharp-runtime's header hard-errors without native `__int128`, which MSVC has not; every other Decimal site in CNA was already behind `SHARP_RUNTIME_HAS_NATIVE_INT128`. Its test suite is excluded rather than guarded — Decimal is woven through the fixtures, and a serializer without Decimal would fail those cases for the right reason anyway. |
| F15 | repo-wide, MSVC | `fatal error C1128: number of sections exceeded object file format limit: compile with /bigobj`. COFF caps a translation unit at 65 279 sections and MSVC emits one per COMDAT; ELF has no such limit, so GCC and Clang never see it. `/bigobj` set beside `/utf-8`. |
| F16 | `PushNotificationSender.cpp` | `ssize_t` is POSIX and MSVC does not define it — one of the few defects here that genuinely *only* cl.exe can show, since mingw-w64 supplies it anyway. `::send` also differs in both directions between the platforms; the length is narrowed once and the result compared as a signed 64-bit count. |

The pattern across F5 and F9–F16 is worth stating plainly: almost none of these are "the code is
wrong". They are places where one toolchain was quietly covering for the code — mingw-w64's
libstdc++ defines `NOMINMAX` and `ssize_t` for you, GCC has `__int128`, ELF has no section limit,
Linux's `path::native()` is narrow — and the cover was mistaken for portability.

### WINNATIVE-F17 — CnaTests overflowed the Windows stack, and the product did not *(fixed)*

**Symptom.** The first native run of `CnaTests` died at
`XnaContentProjectCommandLine.TheProcessorDecidesWhichReadingOfAnAudioSourceIsBuilt` with exit
`-1073741571` = `0xC00000FD`, **STATUS_STACK_OVERFLOW** — taking the whole run with it, before
GoogleTest could write its XML. Reproducible in isolation, and in both tests whose content project
names a `.wma`.

**Measurement.** Windows reserves 1 MB of stack per thread; Linux reserves 8. Bisected by rewriting
the header of the built executable with `editbin /STACK:`:

```
1024 KB  ->  0xC00000FD   stack overflow
1536 KB  ->  exit 1       runs, and fails the same way Linux does
2048 KB+ ->  exit 1
```

So a little over 1.2 MB — a large binary's ordinary appetite, not a runaway recursion.

**The check that decided the fix.** A linker flag on the test binary would have made the suite
green either way, so before adding one: the same content project was built through
**`cna-content.exe`**, the shipping tool, which runs the same `RunContentCompiler` on the same
machine. It completed inside the default 1 MB and refused the undecodable source cleanly —
`exit 1`, with the identical message Linux prints:

```
error:   Import (CNA.CompressedSoundImporter): this build has no audio decoder, so a compressed
         source cannot be read as a sound effect; build it as a song, or use a build with a decoder.
```

Had that crashed too, the defect would have been in the pipeline and `/STACK:` would have **hidden**
it — every Windows application doing a content build would still have crashed while the suite
reported success. It did not, so the finding is exactly what it appears to be, and
`target_link_options(CnaTests PRIVATE /STACK:8388608)` is the right answer. 8 MB to match the Linux
default, so a test that fits on one platform fits on the other.

**Separately:** that test fails on Linux too — this build has no audio decoder, so the importer
refuses and the assertion on `exitCode == 0` does not hold. An environment-dependent failure that
predates this branch, not something introduced or fixed here.

### WINNATIVE-F18 — a D3D11 device costs ~6 kernel handles that never come back *(not CNA's)*

The repo-root `CnaTests` run began reporting `D3D11CreateDevice failed, hr=0x887A0004` partway
through, while a freshly launched probe on the same machine at the same moment created a device
without trouble. Something accumulates in a long-lived process.

`cna_win32_d3d11_cycle` answers it by measurement rather than inference. Each cycle creates a CNA
window, a device, a swap chain and a render-target view, clears, presents, releases all of it in a
renderer's order, destroys the window, and samples the process's own counters. Three runs:

| run | cycles | USER | GDI | handles | private |
|---|---|---|---|---|---|
| **control — windows only, no D3D** | 300 | 2 → 2 | 2 → 2 | **114 → 114** | **1.7 → 1.7 MB** |
| HARDWARE (VBoxSVGA) | 300 | 2 → 2 | 2 → 2 | 144 → 1938 | 2.5 → 64 MB |
| WARP (software rasteriser) | 300 | 2 → 2 | 2 → 2 | 144 → 1938 | 2.5 → 64 MB |

Read together these settle the attribution:

* **CNA is not responsible.** The control is the identical loop with the device creation removed,
  and it is perfectly flat over 300 cycles — matching the window-lifecycle stress, which is flat
  over 500. CNA's window create/destroy costs nothing.
* **VirtualBox is not responsible.** WARP is Microsoft's own rasteriser with no vendor driver
  underneath, and it leaks at exactly the same rate — ~6 handles and ~205 KB per device.
* **It is not lazy reclamation.** The settle phase pumps messages and waits two seconds before
  re-sampling; the count does not move (1938 before, 1938 after).

What remains is the Direct3D 11 / DXGI runtime as used here. This machine has no debugger
installed, so attributing it further — Microsoft's runtime versus something this harness does not
release — is beyond what was measured, and is left as that rather than asserted.

**Consequence, which is the part that matters.** A real application creates one device and is
unaffected. A *test binary* that creates a device per test accumulates handles until the runtime
refuses more, which is exactly what the native `CnaTests` run hit after several thousand graphics
tests. The graphics failures in that run are therefore an artefact of one process making thousands
of devices, not evidence about CNA's renderer.

### WINNATIVE-F7 — the clipboard interop harness was measuring PowerShell *(fixed)*

The first Notepad interop run reported six failures on non-ASCII samples, and **none of them were
CNA's**. Text handed to a native process on a PowerShell command line is re-encoded in the ANSI
code page, and a native process's stdout is decoded in the console code page, so both ends of the
harness's own transport were lossy — a Czech sample came back as "17 characters", which is its
UTF-8 byte count read as single bytes.

Recorded because the conclusion nearly went the other way: a harness defect that looks exactly like
an encoding bug in the thing under test is the most expensive kind. The transport is now a file of
UTF-8 bytes with an explicit no-BOM encoding on the PowerShell side, and all ten checks pass.

### WINNATIVE-F19 — the oracle corpora were read with a regex MSVC cannot run *(fixed)*

**174 of the 761 `CnaTests` failures on Windows were one defect**, and it was not in any product
code. Sixteen fixtures each carried a copy of the same corpus reader, which picked apart a line of
`{"case": "...", "result": "..."}` with

```
std::regex("\\{\"case\": \"([^\"]*)\", \"result\": \"((?:[^\"\\\\]|\\\\.)*)\"\\}")
```

MSVC's `<regex>` matches by **recursing once per repetition**. A `result` of any length therefore
exhausts the thread's stack and throws `regex_error(error_stack)` rather than matching. libstdc++
matches the same pattern iteratively, which is why Linux has never seen it in the years this
pattern has been copied from fixture to fixture. This is the same class of defect as F17: a
platform-dependent stack budget, found only by running on the platform.

Two things made it cost more than its count:

* the throw happens inside a function-local `static`'s initializer, so GoogleTest attributed all
  174 to **"unknown file"** rather than to any test — which is why they did not group with the
  suites they belong to when the failures were first counted;
* the reader that throws returns an **empty corpus**, so the fixtures compared against nothing. A
  reader that answers nothing looks exactly like a product that answers wrongly, and the first
  reading of these failures was that Windows content handling was broken.

`tests/support/CNA/TestSupport/OracleCorpus.hpp` replaces the pattern with a scan of the grammar
the corpora actually use, shared by all sixteen. It preserves the distinction the two regex
spellings made — a field written `[^"]*` holds no escapes and ends at the first quotation mark, one
written `(?:[^"\\]|\\.)*` ends at the first **unescaped** one — because collapsing them changes what
a value containing a backslash means.

Equivalence was measured rather than argued: a probe ran the old regex and the new scan over every
`.json` line under `tests/reference` — **208 614 lines, 985 two-field and 265 four-field records,
0 disagreements**. `tests/OracleCorpusTests.cpp` (13 cases) then pins the edges the shipped corpora
never reach, including a 200 000-character result that is read rather than refused.

### WINNATIVE-F20 — the sync deleted the prebuilt SDL prefix on every run *(fixed, tooling)*

A second defect in this workstream's own tooling, recorded for the same reason as F4. The guest-side
checkout ends in `git clean -qfdx`, and `.sdl-prebuilt-<system>-<arch>/` — the prefix both the real
configure and the standalone harness resolve SDL from — is untracked and inside the repository. Every
sync removed it.

It presented as a **regression that was not one**: the SDL3 harness configured successfully, a fix
was committed for an unrelated missing-DLL failure, and the very next configure of the *same build
directory* failed with `Could not find SDL3`. Nothing about SDL had changed; the sync in between had
deleted the prefix. The clean now excludes it. The SDL build tree lives outside the repository under
`C:\cna\build\sdl3`, so `git clean` never reached it and restoring the prefix cost one install step
rather than a full SDL compile.

### WINNATIVE-F21 — MSVC does not lex a raw string literal inside a macro argument *(worked around)*

Adding `tests/OracleCorpusTests.cpp` produced `error C2017: illegal escape sequence` from MSVC and
nothing at all from GCC. MSVC's default preprocessor does not recognise a raw string literal in a
macro argument; it re-lexes the text as an ordinary string. The line this test needs —

```cpp
EXPECT_FALSE(ReadOracleCase(R"({"case": "a", "result": "b\")", name, result));
```

— deliberately ends in a backslash, so the re-lexing turns `\)` into an escape sequence that does
not exist, and the translation unit does not compile.

Worth stating precisely, because the obvious reading is wrong: **the repository already has 77 raw
string literals inside assertion macros and they all build under MSVC.** They are not safe by
construction, they are safe by content — every one of them happens to spell only legal escapes. A
trailing backslash is the case that does not, and it is exactly the case a parser test needs.

`/Zc:preprocessor` makes MSVC's preprocessor conformant and would remove the class outright. It is
deliberately **not** adopted here: it changes how every macro in the repository expands, which is a
decision for its own task with its own full-suite evidence, not a side effect of adding a test. The
literals are named instead, which costs one line each.

### WINNATIVE-F22 — a Linux test ends the `CnaTests` process *(pre-existing, NOT fixed — out of scope)*

Found while collecting the Linux baseline this workstream needs, and recorded rather than fixed
because it is not Windows, not Win32, and not this branch's.

`Sdl3XErrorHandlerTest.ADeliberateBadRequestDoesNotEndTheProcessAndLeavesTheConnectionUsable`
**ends the process**. In a full `CnaTests` run it is reached, prints its `[ RUN ]` line, and the
binary exits 1 with no further output and no `--gtest_output` XML written at all — which is Xlib's
default error handler calling `exit()`, the precise thing the test asserts cannot happen. Every test
after it in the run is lost, which is why no Linux baseline had been collected.

Run on its own it **skips**, with "libX11 is loaded but XChangeProperty/XSync were not resolvable":
`dlsym(RTLD_DEFAULT, ...)` finds nothing because nothing has pulled libX11 into the process yet. So
the test only executes as part of a larger run, and when it executes, it fails in the one way that
takes the rest of the suite with it.

Not caused by this branch: its only changes under `modules/platform/` are three Win32 test files
(`git diff --stat next..HEAD -- modules/platform/`), and nothing here touches the SDL3 backend, the
X11 backend or the error handler.

The Linux baseline in §3 is therefore collected with `--gtest_filter=-Sdl3XErrorHandlerTest.*`, and
says so.

### WINNATIVE-F23 — one escaped exception discarded an entire Windows run *(fixed)*

`CnaTests` ended on native Windows at **exit 3, with no results XML at all**, twice, at the same
test. An aborting process does not write its report, so the exit code was very nearly the only
evidence there was.

`GraphicsDeviceRendererTest.StartupDiagnosticNeverWritesToStdout` captures stdout and stderr and
then constructs a `GraphicsDevice`. GoogleTest's capturers are **process-global** and are released
only by `GetCaptured*()`, so when that construction throws — which it does here, once the D3D11
device budget is spent (F18) — both stay installed. The next test in the process to capture
anything hits `Only one stdout capturer can exist at a time`, which is a `GTEST_CHECK_`, so it calls
`abort()`. One test's escaped exception silently discarded **every test after it**, about 4 400 of
them.

Both captures are now released on every path, and a device that cannot be constructed skips this
test naming the reason. The audio suite already had exactly this shape
(`SoundEffectTests.cpp:1020-1030`) — it is the house pattern, and this was the one place missing it.
A survey of the other 5 files that use capture found no further instance.

This does not fix the exhaustion underneath it. It stops the exhaustion from costing the whole
measurement — which is the difference between a suite that reports 600 failures and one that reports
nothing.

**Why it appeared only now.** The first full Windows run completed 8 123 tests. The difference is
F19: with the corpus reader fixed, 174 tests that used to throw out of a static initializer before
doing anything now actually run — and a number of them build content and create graphics devices.
Fixing one defect pushed the process past a limit that had never been reached, which is worth
recording as its own lesson: the run that gets further is the run that finds the next defect.

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

### The other platform backends, on the same machine

Hardening Win32 must not have cost the backends that already worked. The standalone harness is the
platform module plus its own suite, so each of these is a statement about the platform layer and
nothing else. All three run on the interactive desktop, through `win32_standalone_suite.ps1`.

```
HEADLESS : 161 tests, 0 failures, 0 errors, 0 skipped   (exit 0)
SDL3     : 336 tests, 0 failures, 0 errors, 7 skipped   (exit 0)
```

SDL3's seven skips are all capabilities this environment does not have, not behaviours that failed:
two sensor tests, two Vulkan tests (no `vulkan-1.dll`), two for a **primary selection**, which is an
X11 concept Windows has no equivalent of, and the conformance case that needs an unsupported
capability to name.

The SDL3 run also produced the one genuine defect in this phase: the harness copied the SDL runtime
DLL next to the executable only under `if(MINGW)`, so the MSVC build linked correctly and then died
at startup with `0xC0000135` (`STATUS_DLL_NOT_FOUND`) — as an exit code of `-1073741515` and no test
XML at all. The copy is now done for any Windows toolchain, and the runner prints a missing-XML exit
code in hex, because the decimal spelling says nothing and the hex one names the problem.

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

### SDL-free, proved by the PE import tables

`CNA_PLATFORM=WIN32 + CNA_ENABLE_SDL=OFF + CNA_AUDIO_PLATFORM=NULL + CNA_GRAPHICS_RENDERER=DIRECTX11`,
built with MSVC. Two questions, both answered by inspection rather than by the configure log:

1. **was anything named SDL built?** No `*.dll`, `*.lib` or `*.exe` matching `sdl` exists anywhere
   in the build tree.
2. **does any executable import one?** `dumpbin /nologo /dependents` over all **38** executables:
   none imports an SDL DLL.

The union of everything those 38 executables import is small enough to read in full, which is the
real evidence:

```
KERNEL32 USER32 GDI32 ADVAPI32 SHELL32 ole32        the Win32 backend's own edges
d3d11 D3DCOMPILER_47 OPENGL32                        the renderer and WGL
bcrypt                                               sharp-runtime's Guid.NewGuid (F12)
MSVCP140 MSVCP140_ATOMIC_WAIT VCRUNTIME140[_1]       the MSVC C++ runtime
api-ms-win-crt-*                                     the UCRT
```

Nothing else. `bcrypt` appearing here is also the visible proof that the `Core.Base` link fix (F12)
is in effect rather than merely committed.

### The Linux side did not move

Every fix here is generic C++ or CMake, so the question "did fixing Windows break Linux" is a real
one. Measured on this branch, native Linux (`CNA_PLATFORM=SDL3`, HEADLESS renderer, Debug):

```
full rebuild of cmake-build-debug        exit 0, no failed targets
CnaPlatformModuleTests   511 tests   501 passed, 10 skipped, 0 failed
CnaMathTests             857 tests   857 passed
CnaContentPipelineTests              builds and runs; the 44 tests touching
                                     the changed files all pass
CnaContentTests                      13 failures -- the same 13 as the baseline
```

The MinGW-w64 cross-build of `CNA_PLATFORM=WIN32 + CNA_ENABLE_SDL=OFF` with the
DIRECTX11/DIRECTX12/SOFTWARE/HEADLESS multi-renderer set also builds end to end again, which it had
not done since the Windows-only defects landed.

### The Linux baseline, so a Windows number can be read against something

`CnaContentTests` has **13 failures on Linux at the baseline commit `4cf33c2b`**, unrelated to this
workstream: `CnbTextureContentManagerTest` (2), `CnbTextureCubeProducerTest`,
`CnjCapabilityMatrixTest`, `CnjEffectTest`, `CnjStockEffectTest`, `CnjTexture3DTest`,
`ContentManagerSkinnedModelTest` (3) and their duplicates in the summary. Measured twice — once
with this branch's sources and once after checking the three touched files back out to the
baseline — and the same 13 fail both ways.

Recorded because a Windows run of the same suite will show them too, and a reader who does not know
they predate this branch would attribute them to it.

**The whole-suite Linux baseline**, collected on this branch after the F19 corpus fix, from the
repository root under Xvfb (`DISPLAY=:99`):

```
8939 tests · 25 failures · 0 errors · 479 skipped
```

Two suites are **excluded**, and the exclusion is part of the measurement rather than a footnote:

* `Sdl3XErrorHandlerTest` — it **ends the process** (F22), so nothing after it is measured at all;
* `XnaDifferentialBuildTest` — it invokes the DirectX SDK's `fxc.exe` under Wine, and the shared
  Wine prefix on this host wedges in `wineboot --init`. Not a CNA defect and not reproducible on
  demand; the same suite had completed in an earlier run on the same binary.

The 25 failures are the 13 content ones above plus `XnaBuildDeterminism` (3), `XnaSourceToOutput`
(3), `XnaAudioContent`/`XnaAudioProcessors`/`XnaContentProjectCommandLine` (3, all of which name a
build configured without a media decoder), `GltfRendererIndexWidthPolicy`,
`XnbContainerFuzzTest`, `XnbContentPipelineTest`, `GraphicsDeviceCapabilityTest` and
`MediaLibraryTestFixture` (2, both about song durations).

That last pair matters for reading the Windows run: **`MediaLibraryTestFixture` already fails twice
on Linux**, so a Windows failure in that fixture is only interesting if it is a *different* test.

### The host-ownership promises, measured

`docs/platform-win32.md` says the backend never touches process-global policy. Captured before the
platform exists and again after a window has been created and pumped, on the interactive desktop:

```
dpi awareness    unaware  -> unaware          (the backend never calls SetProcessDpiAwareness*)
current directory C:\cna\build\win32-standalone -> unchanged
error mode       32769    -> 32769
timer resolution 15625 us -> 15625 us         (no timeBeginPeriod anywhere)
com apartment    uninitialised -> main-STA    (CNA adds a reference; it took nothing from a host)
```

The timer figure is the one Wine could not produce: 15.625 ms is Windows' real default tick, and a
stray `timeBeginPeriod(1)` would show here as 1000 us. Under Wine the same probe reads 1000 us
before *and* after, so the promise was untestable there.

### Under MSVC's AddressSanitizer

`/fsanitize=address`, RelWithDebInfo, the whole platform module and its whole suite, run on the
interactive desktop:

```
cna_platform_tests      389 tests, 0 failures, 3 skipped     0 AddressSanitizer reports
cna_win32_native_stress 400-iteration lifecycle budget       0 AddressSanitizer reports
cna_win32_native_desktop 9 check groups, 0 failures          0 AddressSanitizer reports
```

Nothing in the window registry, the adopted-window path, the `WM_NCCREATE`/`WM_NCDESTROY` pointer
discipline, the UTF conversions, the clipboard's global memory or the COM wrappers is touched after
it is freed, or written past its end.

One caveat worth stating, because it nearly invalidated the run: `-DCMAKE_CXX_FLAGS=` *replaces*
CMake's MSVC default rather than adding to it, and that default carries `/EHsc`. The first
configure therefore produced a sanitized binary whose exceptions did not unwind — in a codebase
that reports refusals by throwing, that is not the program under test. cl.exe warned about it
(C4530) forty times. The script restates the defaults now.

### DPI, and why the first run of it proved nothing

Setting the desktop to 125, 150 and 200 % and re-measuring gave scale 1.0 and 96 dpi every time.
That is not a defect: Windows **virtualises** DPI for a process that has not declared awareness, and
CNA deliberately never declares it — `docs/platform-win32.md` makes the process-wide policy the
host's. The scaling was genuinely applied; the display CNA enumerated shrank 1920x1080 → 1280x720 →
960x540 as the scale rose, which is exactly the virtual screen an unaware process is given.

So the run was consistent, and it exercised one half of the contract. The other half needs a host
that behaves like a real game: `--dpi-aware` declares per-monitor-v2 for the process **before** the
platform is created. Both, on the same desktop at 150 %:

| | unaware host | **DPI-aware host** |
|---|---|---|
| `GetDpiForWindow` | 96 (100 %) | **144 (150 %)** |
| `IPlatformWindow::GetDisplayScale` | 1.000 | **1.500** |
| agreement with Windows | ok | **ok — 1.5 vs 1.5** |
| `GetPixelSize` vs `GetClientRect` | ok, 640x480 | **ok, 640x480** |
| display CNA enumerates | 1280x720, contentScale 1.0 | **1920x1080, contentScale 1.5** |
| DPI awareness, before → after creating the platform | unaware → unaware | **per-monitor-v2 → per-monitor-v2** |

Four things at once: CNA reports the **real** DPI when the host declares awareness; the
**virtualised** one when it does not; it never changes the policy in either direction; and the
display enumeration follows the same rule as the window. The last row is the host-ownership promise
verified from the demanding side — not merely "CNA left the default alone", but "CNA respected a
policy the host had already set".

### Synthetic input on the real desktop

`SendInput` enters the same raw input stream a physical keyboard does, and only reaches the
foreground window — so this is the check that could not exist in session 0. 24 assertions, all
passing:

```
key.A / F1 / CapsLock / ArrowLeft            scancode and keycode both correct
key.LeftShift vs RightShift                  distinct in BOTH scancode and keycode
key.LeftCtrl vs RightCtrl                    distinguished by the extended flag alone
key.Keypad5                                  scancode=Keypad5 keycode=NumPad5
text.ascii / czech / euro                    1 / 2 / 3 UTF-8 bytes, exact
text.surrogate-pair (U+1F300)                4 bytes -- the two UTF-16 units recombined
mouse.left / middle / right / x1 / x2        buttons 1..5, X1 and X2 distinguished by mouseData
wheel.vertical / horizontal                  y=+1 and x=-1
relativeMode.togglesCleanly                  8 enable/disable cycles
relativeMode, window destroyed while enabled clip released to the full 1920x1080 virtual screen
```

The last one is the case that matters: a clip rectangle or hidden cursor surviving the window it
belonged to leaves the whole desktop unusable, so it is checked against `GetClipCursor` and
`CURSOR_SHOWING` rather than against CNA's opinion of its own state.

### Clipboard against Notepad

Driven through Notepad's edit control with `WM_PASTE`/`WM_COPY` — what a keystroke turns into —
rather than SendKeys, which cannot type outside the current layout. Ten checks, both directions,
all passing: ASCII, Czech, Greek with an em dash, an emoji pair (two surrogate pairs), and 5000
characters.

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

The individual instruments, each runnable on its own once the guest is synced:

| Tool | What it does |
|---|---|
| `windows_vm_exec.sh` | one command in the guest over SSH; `--stdin`, `--push`, `--pull`, `--reboot`, `--start` |
| `windows_vm_sync.sh` | this exact commit into `C:\src\cna` by git bundle; `CNA_WIN_PAYLOADS` ships gitlink trees |
| `win32_run_interactive.ps1` | runs a program on **WinSta0\Default** via a scheduled task with an interactive token |
| `win32_build_sdl_prebuilt.ps1` | builds SDL3 (`-Version 3`, default) or SDL 2.30.11 (`-Version 2`) into the shared prefix |
| `win32_standalone_suite.ps1` | `-Platform WIN32\|SDL3\|SDL2\|HEADLESS` — configure, build and run the platform harness |
| `win32_asan_build.ps1` | the harness under MSVC's AddressSanitizer; `-Run` also runs stress and desktop checks |
| `win32_dpi_matrix.sh` | sets `LogPixels`, reboots, measures, and restores the original scaling on exit |
| `win32_clipboard_interop.ps1` | drives Notepad's edit control to check the clipboard against a real application |
| `win32_soak.ps1` | the long-running lifecycle soak |

Two of these encode a lesson rather than a convenience, and changing them back would re-open a
defect this workstream already paid for: `win32_dpi_matrix.sh` **rebuilds the harness in the guest
first** (the sync ships source, not binaries, and two runs silently measured a stale executable),
and `windows_vm_sync.sh` **excludes `.sdl-prebuilt-*` from its `git clean`** (F20).

Everything the guest builds lives under `C:\cna\build\`, outside the repository, so a sync's
`git clean -x` cannot reach it and an incremental rebuild stays incremental:

```
C:\cna\build\full-win32-d3d11-nosdl   the whole framework + CnaTests, WIN32 + DIRECTX11, SDL off
C:\cna\build\win32-standalone[-asan]  the standalone platform harness, plain and sanitized
C:\cna\build\std-{sdl3,sdl2,headless} the harness for the other platform selections
C:\cna\build\{sdl3,sdl2}              the SDL sub-builds that install into the shared prefix
```
