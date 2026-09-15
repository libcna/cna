# Native Platform Validation Plan (X11 real desktop + Win32 on Debian/Wine)

## Status summary (top of file is source of truth)

The integrated Win32 + X11 tree (plans/plan_native_platforms_integration.md) was validated on a
real Debian 13 workstation: AMD Radeon 780M, GNOME 48 on Wayland, the X11 backend running under
**Xwayland** (never native Xorg -- none exists on this machine). **23 defects were found and fixed**
(NPV-0101..NPV-0123), each reproduced first and each with a regression test that fails without its
fix; the table below lists them with their commits.

What is proven: the X11 backend's suites on private Xvfb servers (mapping, 59 integration, 18
window-manager tests) and, on the real desktop, the integration suite, hardware GLX (radeonsi) and
hardware Vulkan (RADV, validation layer clean), the MIT-SHM presenter, 300-cycle GL/Vulkan/presenter
lifetimes without leaks attributable to CNA, the clipboard against xclip/xsel including 4 MiB INCR,
all compiled renderers of the SDL-free demos, and the SDL-free dependency proof. Full ctest of the
SDL-free X11 build: 9208/9254 before the fixes below it prompted; every remaining failure is
classified (pre-existing, environment, or parallel-run artifact) in "Regression runs".

What is **not** proven here, and why: the focus- and input-dependent desktop scenarios (lifecycle,
wm, stress, keyboard, text, mouse, relative, soak, and NPV-0113 on mutter) -- the GNOME session
locked itself (idle timeout) before they could run, a locked session gives no window focus, and
injecting input there would type into the lock screen; the commands to run them are in "Remaining
gaps". No physical person tested anything. Win32 evidence is MinGW + Wine only and is **NOT native
Windows validation**; docs/testing-win32-native.md and tools/platform/validate_win32_native.ps1 are
ready for the first native run (a Windows 10 VirtualBox VM the owner prepared).

What comes next, in the recommended order with estimates and what each step needs from the owner,
is in "Next steps" -- first of all a CI that has been red for unrelated reasons since before this
pass.

**Continued on 2026-09-15 on branch `x11`** (the owner approved the X11 part of "Next steps", about
120 hours; Win32 and Wayland out of scope): CI defects NPV-0124..NPV-0126 and NPV-0128..NPV-0131,
the INCR follow-up NPV-0127, **gamepads and joysticks through Linux evdev**, **sound for SDL-free
builds**, **input-method composition** and **exclusive fullscreen through XRandR**
(`plans/plan_x11.md` X11-0150..X11-0153) are done, and so is the rest of the X11 list,
`plans/plan_x11.md` Phase M (X11-0154..X11-0158).
The session stayed locked throughout, so the desktop scenarios below remain unrun.

Session events recorded honestly: gnome-shell crashed at 21:23:27 (untrapped BadWindow on its own
X_SendEvent, core dump) and the owner logged in again -- no process of this validation was connected
to `:0` then (the last had exited at 21:04:08); the second external monitor was disconnected during
the evening, so the late desktop runs saw one monitor; the session locked at about 22:15.

Legend: ✅ done and verified · 🟨 partially done · ⬜ not started · ⛔ blocked (reason in the row) ·
➖ not applicable here (reason in the row).

## Phase 0 — baseline

| Fact | Value |
|---|---|
| Repository | `libcna/cna` |
| Branch | `next` (tracking `origin/next`) |
| Starting HEAD | `e716fb384a57117d4c4ed01349c1309dac1a1c4f` — `docs(NPI-0018..0023): finalize integration plan status and next steps` |
| Merge base with `origin/next` | `e716fb384a57117d4c4ed01349c1309dac1a1c4f` (the branch was level with `origin/next` at start) |
| Pre-existing working-tree change (preserved, never staged) | ` D tests/assets/media/video/video_xnb_object_fixture.xnb` |
| Concurrent sessions | Other agents use this machine and this working tree (a Codex session was open in the repo). All commits in this plan stage files by explicit path; unrelated changes are never swept in. |
| Local git identity | `Robert Vokac <robertvokac@robertvokac.com>` (repository-local config) |

### Host

| Item | Value |
|---|---|
| OS | Debian GNU/Linux 13 (trixie), `/etc/debian_version` 13.6 |
| Kernel | `6.12.107+deb13-amd64` (Debian 6.12.107-1, 2026-08-29), x86_64 |
| Machine | Lenovo ThinkPad T14 (laptop panel `eDP-1` disabled; two external monitors) |
| CPU | AMD Ryzen 7 PRO 7840U w/ Radeon 780M Graphics, 8 cores / 16 threads |
| RAM | 30 GiB, no swap |
| GPU | AMD Radeon 780M, PCI `1002:15bf` (Phoenix1), **integrated, real hardware** |
| Mesa | 25.0.7-2+deb13u1 (radeonsi for GL, RADV for Vulkan; llvmpipe/lavapipe also installed) |
| Vulkan loader | 1.4.309; validation layers 1.4.309 installed |
| Compilers | GCC 14.2.0 (Debian 14.2.0-19), Clang 19.1.7 |
| Build tools | CMake 3.31.6, Ninja 1.12.1, GNU Make 4.4.1, mold, ccache 4.11.2 |
| Wine | wine-10.0 (Debian 10.0~repack-6), wine64 + wine32; DXVK 2.6 (`dxvk-wine64`); Wine's own builtin `d3d12.dll`/`d3d12core.dll` (vkd3d inside Wine) |
| MinGW | x86_64-w64-mingw32 GCC 14 (win32 and posix thread models), mingw-w64 runtime 12.0.0, binutils 2.44 |

### Tools obtained without root

`sudo` needs a password, so nothing was installed system-wide. `xclip` 0.13, `xsel` 1.2.1 and
`openbox` 3.6.1 (+ `libobrender32v5`, `libobt2v5`, `libimlib2t64`) were fetched with
`apt-get download` into `~/deps/debs` and extracted into `~/deps/xtools-root`; wrappers live in
`~/deps/xtools/bin` (the openbox wrapper points `XDG_CONFIG_DIRS`/`XDG_DATA_DIRS` at the extracted
config and themes — without that openbox exits at start-up with "Unable to load a theme").

## Phase 1 — the graphical session (classified, not assumed)

| Question | Answer | Evidence |
|---|---|---|
| Session type | **Wayland** (GNOME Shell 48.7 / mutter 48.7), logind session 2 on seat0, `Type=wayland`, active | `loginctl show-session 2` |
| X server a desktop X11 client talks to | **Xwayland 24.1.6 on `:0`**, rootless, `-noreset`, started by mutter; XAUTHORITY `/run/user/1000/.mutter-Xwaylandauth.*` | `ps`, `xdpyinfo` (vendor "The X.Org Foundation", release 12401006, extension `XWAYLAND`) |
| Native Xorg session available? | **No.** No Xorg process exists; only Xwayland and several Xvfb servers belonging to other agents (`:99`, `:197`, …). | `ps -eo args` |
| The shell's `DISPLAY` | `:99` — **another agent's Xvfb**, not the desktop. Every real-desktop run in this plan sets `DISPLAY=:0` and the mutter Xwayland XAUTHORITY explicitly. | `ps` shows `Xvfb :99` |
| Window manager on `:0` | mutter (EWMH: `_NET_SUPPORTING_WM_CHECK` present, `_NET_SUPPORTED` includes `_NET_WM_STATE_FULLSCREEN`) | `xprop -root` |
| Input method | `XMODIFIERS=@im=ibus`, root `XIM_SERVERS=@server=ibus`, **but no ibus process is running** | `ps` |
| Keyboard layouts | `us` and `cz+qwerty` (GNOME input sources) | `gsettings get org.gnome.desktop.input-sources sources` |

Every "real desktop" result below is therefore **XWayland under GNOME/mutter**, never native Xorg.
Xvfb results are labelled Xvfb.

## Phase 2 — real GPU vs software rendering

| Item | Value |
|---|---|
| GLX on `:0` | direct rendering yes; vendor `AMD`; renderer `AMD Radeon 780M (radeonsi, phoenix, LLVM 19.1.7, DRM 3.61, 6.12.107+deb13-amd64)`; core profile 4.6, compat 4.6, GLSL 4.60, GLES 3.2; `Accelerated: yes` — **hardware** |
| Vulkan GPU0 | `AMD Radeon 780M (RADV PHOENIX)`, `PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU`, driver radv Mesa 25.0.7, API 1.4.305 — **hardware** |
| Vulkan GPU1 | `llvmpipe (LLVM 19.1.7, 256 bits)`, `PHYSICAL_DEVICE_TYPE_CPU` — **software**, used only where labelled |
| Xvfb GLX | Mesa swrast (software) — used for CI-equivalent runs and labelled as such |

## Phase 13/14 environment facts (display)

| Item | Value |
|---|---|
| Monitors (XRandR on `:0`) | early evening: **2 active**, `DP-1` primary 2048x1152+0+0 and `DP-2` 2048x1152+2048+0, both 600x340 mm; screen 4096x1152. From about 22:10: **1**, the laptop panel `eDP-1` 1536x960 (the external monitors had been disconnected) -- the late runs saw one monitor, and `displays.multi-monitor` skipped |
| Physical monitors | 2 × Dell S2725DC, 2560x1440 @ 143.97 Hz |
| Desktop scaling | mutter `scale-monitor-framebuffer`, scale 1.25 per monitor; Xwayland is *not* in native-scaling mode, so X clients see a logical 2048x1152 per monitor and are upscaled by the compositor |
| `Xft.dpi` | 96 (so CNA's documented policy gives display scale 1.0 and `highDpi` false) |

## Task table

| ID | Task | Status | Notes / evidence |
|----|------|--------|------------------|
| NPV-0001 | Phase 0 baseline | ✅ | see Phase 0 above |
| NPV-0002 | Phase 1/2 session and GPU classification | ✅ | see Phase 1/2 above |
| NPV-0003 | Phase 3 full integrated test baseline | ✅ | full ctest of the SDL-free X11 build on a private Xvfb, 9254 tests: see "Regression runs" |
| NPV-0004 | Phase 4 ASan/LSan/UBSan on the merged tree | ✅ | `build-asan/` (`-DCNA_SANITIZE=address,undefined`, GCC 14, `-Wno-error=maybe-uninitialized` because GCC 14 reports a known false positive inside libstdc++ `<regex>` under sanitizers in sharp-runtime). LSan confirmed active first with a deliberately leaking probe. Private Xvfb + openbox: X11 mapping 30/30, X11 integration 52/52, window-manager 16/16, full platform module suite 432 passed / 2 skipped, audio 225 passed / 8 skipped (no hardware), content audio/WAV subset 74/74 — 0 ASan errors, 0 UBSan reports, 0 unsuppressed leaks. Found and fixed NPV-0102, NPV-0103, NPV-0104, NPV-0105. The only suppression (`tools/platform/lsan_x11_mesa.supp`, `leak:libGLX_mesa.so`) is for a Mesa swrast leak reproduced byte-for-byte by a CNA-free 20-line program; the RADV ICD's 2x128-byte init leak is recorded, not suppressed. |
| NPV-0005 | Phase 5 real X11 window lifecycle | 🟨 | Xvfb + openbox: harness `lifecycle` 8/8 (1/2/4 windows x3 rounds, 300 rapid create/destroy, X resources 6 -> 6). Real desktop: **not valid tonight** -- run on a locked session (see Remaining gaps). NPV-0101 found. |
| NPV-0006 | Phase 6 long-running window stress | 🟨 | Xvfb + openbox: `stress --ops 10000` 5/5, RSS/fds/X resources flat. Real desktop: pending an unlocked session. |
| NPV-0007 | Phase 7 keyboard | 🟨 | NPV-0117 found and fixed (layouts); pure table tests on US/CZ/FR/DE/RU data; private-server layout switch test. The uinput-driven `keyboard` scenario on `:0` is pending an unlocked session. |
| NPV-0008 | Phase 8 text input | ⬜ | pending an unlocked session (`text`); IME stays unsupported (`ime=0`) by design |
| NPV-0009 | Phase 9 mouse | 🟨 | integration suite on `:0` (wheel, buttons, capture after NPV-0012 test fix) passed; uinput `mouse` scenario pending an unlocked session |
| NPV-0010 | Phase 10 XInput2 raw relative mouse | 🟨 | first real-desktop runs found NPV-0109/0110/0111 (fixed, tested); rerun of `relative` pending an unlocked session |
| NPV-0011 | Phase 11 clipboard interoperability | ✅ | real desktop: 23/23 (xclip/xsel, ASCII/Czech/emoji/600 KiB/4 MiB both ways, ownership loss, owner exit); NPV-0112, NPV-0115, NPV-0119 found and fixed. Native-Wayland peer unavailable: wl-clipboard gets no data-control protocol from mutter (reported, not faked). |
| NPV-0012 | Phase 12 real window-manager validation | 🟨 | Xvfb + openbox: WM suite 18/18, harness `wm` 13/13. On `:0` (mutter) before the lock: integration suite 53/54 then 54/54 after two tests were made WM-aware; NPV-0113 found (fixed; scripted mutter-like WM test on Xvfb). Harness `wm`/`lifecycle` on `:0` pending an unlocked session. |
| NPV-0013 | Phase 13 XRandR/display validation | 🟨 | `displays` 8/8 + 1 skip (one monitor) on `:0`; 7/7 on Xvfb. The two-monitor runs of the earlier evening are in the integration suite (DisplayEnumerationAgreesWithItsCapability, AWindowIsAssociatedWithTheDisplayItOverlaps). Hot-plug not exercised (the owner's displays are not to be touched). |
| NPV-0014 | Phase 14 DPI/scale | ✅ | `Xft.dpi` 96 under mutter fractional scaling without Xwayland native scaling: display scale 1.0, `highDpi` false -- the documented policy; recorded in docs/platform-x11.md "Under Xwayland" |
| NPV-0015 | Phase 15 hardware GLX | ✅ | `glx` 15/15 on radeonsi (GL 4.6 core, frames verified by read-back, resize, fullscreen 1536x960, two contexts, 2.1 compat, 4.6 core); again 15/15 after NPV-0121 (GLX resolved at run time). Behind the lock screen vsync'd presentation runs at ~1 fps (compositor throttling); swap interval 0 gave 229 fps. |
| NPV-0016 | Phase 16 hardware Vulkan X11 | ✅ | `vulkan --validation` 6/6 on RADV (integrated GPU), 0 validation errors/warnings; lavapipe on Xvfb 6/6 (software, labelled) |
| NPV-0017 | Phase 17 renderer spot-checks | ✅ | SDL-free multi-renderer build on `:0`: `cna_demo_2d` with OPENGL33, VULKAN (RADV), SOFTWARE, HEADLESS -- all exit 0; `cna_house3d_demo` aborted with three of them -> NPV-0118, fixed; all four pass on `:0` and in `X11_House3D_SmokeTest_*` on Xvfb |
| NPV-0018 | Phase 18 software presenter | ✅ | `presenter` 7/7 on `:0` (MIT-SHM) and on Xvfb, 16-bit Xvfb too (XPutPixel path); NPV-0116 found (4x faster, pixel-identical by exhaustive test) |
| NPV-0019 | Phase 19 GLX/Vulkan/resource lifetime stress | ✅ | `lifetime --iterations 300 --validation` 13/13 on `:0` (300 GL, 100 RADV, 300 presenter cycles; RSS +4 kB, fds and CNA's X resources flat) and on Xvfb with lavapipe. Mesa's WSI leaks a GC per swapchain (and lavapipe its ShmSegs) -- reproduced with a CNA-free program, reported not counted. |
| NPV-0020 | Phase 20 SDL-free real-hardware proof | ✅ | `cmake-build-multi` (X11, `CNA_ENABLE_SDL=OFF`, NULL audio, OPENGL33;VULKAN;SOFTWARE;HEADLESS): no SDL in ldd/readelf/nm of the demos; NPV-0106 and NPV-0114 found (configure, and a sibling's `find_package(SDL3)`); NPV-0121 removed libGLX from `NEEDED`; NPV-0123 made the whole SDL-free build build. Hardware GL and Vulkan ran from it. |
| NPV-0021 | Phase 21 SDL3 regression on this machine | ✅ | full SDL3 ctest, 9039/9077, every failure classified -- "Regression runs" |
| NPV-0022 | Phase 22 optional performance measurements | ✅ | presenter cost per frame before/after NPV-0116 (optimised build); GLX 229 fps unthrottled on a hidden window. SDL3-vs-X11 frame-time comparison not done (the session was locked). |
| NPV-0023 | Phase 23 Win32 MinGW cross-build validation | ✅ | see "Win32: NOT NATIVE WINDOWS VALIDATION" |
| NPV-0024 | Phase 24 Wine Win32 validation | ✅ | same section |
| NPV-0025 | Phase 25 Wine D3D11 validation | ✅ | same section |
| NPV-0026 | Phase 26 Wine D3D12 validation | ✅ | same section |
| NPV-0027 | Phase 27 Win32 SDL-free dependency proof | ✅ | same section |
| NPV-0028 | Phase 28 native-Windows-deferred list | ✅ | "Native Windows validation deferred" below |
| NPV-0029 | Phase 29 future Windows 10 validation script/doc | ✅ | docs/testing-win32-native.md, tools/platform/validate_win32_native.ps1 (written where no PowerShell exists; not yet executed) |
| NPV-0030 | Phase 30 representative CNA applications | ✅ | demos above (X11) and under Wine; NPV-0118 found |
| NPV-0031 | Phase 31 soak test | 🟨 | 60 s soak on Xvfb (no input: uinput never targets an Xvfb); the 10-minute hardware soak with input is pending an unlocked session |
| NPV-0032 | Phase 32 error/log audit | ✅ | "Error and log audit" below |
| NPV-0033 | Phase 33 capability truthfulness audit | ✅ | "Capability truthfulness" below |
| NPV-0034 | Phase 34 bug fixing | ✅ | 23 defects, "Defects found" |
| NPV-0035 | Phase 35 broad regression after fixes | ✅ | full ctest of the SDL-free X11 build and of the SDL3 build, classified -- "Regression runs" |
| NPV-0036 | Phase 36 final git audit | ✅ | every commit of the pass (CNA and sharp-runtime `88c12f15`) authored and committed by Robert Vokac, no attribution trailers, the pre-existing deleted fixture never staged, nothing pushed |

## Defects found

Numbered NPV-01xx. Each was reproduced before it was fixed, and each fix has a regression test
that fails without it.

| ID | Defect | Found by | Fix | Commit |
|----|--------|----------|-----|--------|
| NPV-0101 | **X11 window registry kept raw pointers to destroyed windows.** `X11Window` never told the platform it was being destroyed, so `windows_` and the mouse/text-input/GL/Vulkan registries dangled: the next `PollEvents` walked freed memory (`FindWindowByXid`), the last live window's close request produced no `QuitEvent` (the dead window still counted), a close request queued for an already-destroyed window became `CloseRequested(0)` + `QuitEvent`, and moving focus after the focused window was destroyed **segfaulted** (dangling `X11TextInput::focused_`). | three new `X11Live` tests reproduced it without a sanitizer (2 failures + a SIGSEGV) | `X11WindowHost` (mirrors `Win32WindowHost`): the window reports its destruction first, identity-matched; unknown-window `WM_DELETE_WINDOW` is ignored; text input drops its focus pointer | `5bdafe8a0` |
| NPV-0102 | **Heap-use-after-free at exit of every X11 application using the XNA API.** `X11ErrorPolicy`'s registry vector and mutex were function-local statics created after CurrentPlatform's lazy-default owner, so `exit()` destroyed them first and `~X11Connection` → `Unregister` touched freed memory. | ASan, at the end of `CnaAudioTests`; reproduced deterministically by the new `cna_platform_x11_exit_harness` (old code: exit 1 with the UAF; fixed: exit 0) | the policy's state is immortal | `8501d0d78` |
| NPV-0103 | `SetNetWmState` called `memcpy(nullptr, nullptr, 0)` for an existing but empty `_NET_WM_STATE` (undefined behaviour; glibc declares both pointers `nonnull`). | UBSan, `PlatformWindowConformance.StateChangesFollowTheOperationalErrorContract/X11` | copy only a non-empty list | `80ce8a498` |
| NPV-0104 | **SDL-free builds refused float/MS-ADPCM/IMA-ADPCM XNB SoundEffect transcoding** ("requires CNA_AUDIO_PLATFORM=SDL3 because no decoder is available") — a stale `#ifdef SOUND_ENABLED` left from before the decoder became CNA's own and SDL-free. | `XnbContentPipelineTest.SoundEffectCodecMatrixPreservesDecodedPcmAndMetadata` failing under `CNA_AUDIO_PLATFORM=NULL` | guard removed; the decoder is compiled for every audio platform | `314806feb` |
| NPV-0105 | **`SoundEffect::Duration` was zero for every effect under NULL/SDL2 audio** (only the SDL3 mixer was consulted). | `CnbSoundEffectCodecTest.ASoundEffectCnbLoadsThroughContentManager` failing under NULL audio | frames and rate recorded from the PCM16 data in every build; new `SoundEffectDurationTests.cpp` runs on every audio platform (the SDL3-only `SoundEffectTests.cpp` is excluded from NULL builds, which is why nothing caught it) | `6c4a97fff` |
| NPV-0106 | **`CNA_ENABLE_SDL=OFF` could not configure with any GPU renderer** (OPENGL33/VULKAN/SOFTWARE): the renderer example suites link `SDL3::SDL3` unconditionally (923 configure errors). The SDL-free CI cell only ever used HEADLESS. | configuring `cmake-build-multi` (X11, SDL OFF, `OPENGL33;VULKAN;SOFTWARE;HEADLESS`) | those three suites skip, with a STATUS line, when no SDL3 target exists | `d5dcb8ff1` |
| NPV-0107 | **The window-manager suite would evict a real desktop's window manager** (`openbox --replace` on whatever `DISPLAY` names; gtest-discovered copies inherit the shell's `DISPLAY`), and it reported ctest "Passed" with 14 of 15 tests skipped when openbox died at start-up. Its close test also used `xdotool windowclose` (XDestroyWindow), not the close-button protocol it claimed. | reading the fixture before pointing it at `:0`; a broken user-space openbox | uses a running EWMH window manager when present, never `--replace`; distinguishes "openbox died" (skip) from "WM advertises fullscreen but CNA missed it" (fail); close goes through `_NET_CLOSE_WINDOW`; external destroy is its own test | `f0149125e` |
| NPV-0108 | **Xvfb launcher without `-noreset`**: the server regenerated whenever the last client disconnected, the next test's `XOpenDisplay` failed with ECONNRESET, and alternate `X11Live` tests skipped as "cannot reach the X server" (ctest: Passed). | an `LD_PRELOAD` shim on `XOpenDisplay` logging errno 104 | `-noreset` | `aa61dbe28` |
| NPV-0109 | **Relative mouse held the pointer grab while another window had focus**, and lost it for good after a focus round trip. | first real-desktop `relative` run on `:0` (mutter) | the grab is *wanted* while relative mode is on and *held* only while the window is mapped and focused (or there is no window manager); re-engaged on focus/map with a 100 ms retry | `d9573bbb3` |
| NPV-0110 | **Relative motion truncated each XInput2 raw report to an integer**: slow, steady movement of a high-resolution device became no movement. | `rawprobe` on `:0`: fractional raw deltas | fractional carry between reports | `d9573bbb3` |
| NPV-0111 | `SetCapture` replaced the relative grab (losing confinement and the hidden cursor) when both were requested. | reading the grab code during NPV-0109 | capture defers to a held relative grab | `d9573bbb3` |
| NPV-0112 | **INCR clipboard reads stopped early**: the stale PropertyNotify from the INCR announcement was taken as "next chunk", so a paste from xsel (4000-byte chunks) stopped at 4000 bytes and a large one from xclip came back empty. | harness `clipboard` against xsel/xclip | stale notifications drained; only a zero-length chunk ends the transfer | `ef032621f` |
| NPV-0113 | **Restore() of a minimised window did nothing on GNOME**: mutter keeps iconified windows mapped, so ICCCM's map-to-de-iconify never reached it. | harness `lifecycle`/`wm` on `:0` | Restore also sends `_NET_ACTIVE_WINDOW` (source 1) when minimised; a scripted mutter-like WM test on Xvfb fails without it | `552785dcb` |
| NPV-0114 | **`CNA_ENABLE_SDL=OFF` still found and linked SDL**: the sibling easy-gl's example ran `find_package(SDL3 QUIET)`, found `/usr/local` SDL3 and added an SDL-linked executable to the default target. CNA's own binaries were clean. | `SDL3_DIR` in the SDL-OFF cache; `--trace-expand` | `CMAKE_DISABLE_FIND_PACKAGE_<SDL*>` under OFF; `CnaSdlOffFindsNoSdlPackage` fixture test | `b94e84862` |
| NPV-0115 | **Pastes from CNA intermittently empty**: a SelectionClear queued before the application copied again was processed afterwards and threw the new text away. | harness `clipboard`: xsel read "" in 1 run of 4 | SelectionClear ignored while the server says CNA still owns the selection; deterministic Xvfb test | `0ca3be27a` |
| NPV-0116 | **Software presenter too slow**: per-pixel shift discovery, division and `XPutPixel` dispatch -- ~30 ms per 1080p frame even optimised (62/265 ms unoptimised), over a 60 Hz budget before any rendering. | harness `presenter` cost per frame | channel layout and source columns per frame; direct 32-bit writes in host byte order, XPutPixel kept for every other layout: 1.5-1.9 ms (800x600), 7.2-8.5 ms (1080p); exhaustive packer-vs-old-formula test | `affad3a0d` |
| NPV-0117 | **Key codes never followed a layout switch** (the table was always rebuilt from XKB group 0), and on a Czech layout the number row reported `OemPlus`/nothing instead of `D1`..`D0`. The rebuild also ran on every modifier change. | keyboard/layout review on the `us,cz+qwerty` desktop; SDL3's own keymap rules as the parity reference | current group with XKB's out-of-range rule, SDL3's `latin_letters`/`french_numbers` rules, rebuild only on group/map/new-keyboard changes, `MappingNotify` handled; pure table tests + a private-server layout-switch test | `941e1e70f` |
| NPV-0118 | **`cna_house3d_demo` aborted on its first frame** with OPENGL33, SOFTWARE and HEADLESS (not X11-specific): it cleared depth on the Game's implicit device, which has no depth buffer, and SOFTWARE-333 (`08c9cae96`) made that the XNA exception. | representative-application runs on `:0` | the demo asks a GraphicsDeviceManager for Depth24; `X11_House3D_SmokeTest_<renderer>` per compiled renderer (the SDL3 EasyGL smoke test never runs in SDL-free builds) | `2716710fc` |
| NPV-0119 | **An INCR paste of a CNA selection was abandoned** when another client took the clipboard mid-transfer (the requestor waited forever), and continued in the NEW text when the application copied again. | a full ctest hung in `ALargeSelectionTransfersThroughIncrToAnExternalClient` | each transfer serves its own copy; SelectionClear no longer ends transfers under way (Qt/GTK behaviour); two external-requestor tests (old code: 131070 of 409635 bytes; spliced text) | `0980a1aee` |
| NPV-0120 | **X11 live suites hung and collided under a full ctest**: fixed-time pumping then a blocking join; a machine-wide `pkill xclip`; and gtest discovery re-registering the private-server suites to run eight at a time on the ambient DISPLAY (a shared Xvfb, or a developer's desktop). | the same hung run | pump until the reader finishes (under `timeout`), stop owners by PID, `TEST_FILTER` keeps those suites out of discovery (their aggregates run them on private servers) | `de90e9ef1` |
| NPV-0121 | **Every X11 build linked libGLX**, so HEADLESS/SOFTWARE X11 binaries needed a GL implementation to start and all five `ModuleLinkClosure_NativeSdkFree_*` gates failed. | full ctest of the SDL-free X11 HEADLESS build | GLX 1.3 entry points resolved at run time (as the Vulkan loader already was); `openGlContext` false without a GL library; hardware GLX 15/15 afterwards | `12dbfedff` |
| NPV-0122 | `X11IsSdlFree` scan failed from the source root: `__FILE__` is relative under the mandatory `CCACHE_BASEDIR`. | same full ctest | absolute backend path passed by CMake | `33a10f1af` |
| NPV-0123 | **`cmake --build` of an SDL-free HEADLESS build failed**: five Headless example tests called SDL directly ("Not Run" in ctest). | same full build | smoke test's SDL check only where SDL exists (9 vs 10 checks); the four SDL-harness controls registered only with SDL3 | `2c6efc807` |
| NPV-0124 | **Every CI workflow failed at configure on `next`**, long before this pass: sibling repositories were cloned from `develop`, and CNA `next` asks for a sharp-runtime component only sharp-runtime `next` has ("Unknown Sharp Runtime component 'Xml.Serialization'"). A topic branch cut from `next` then fell back to `develop` too. | CI runs 34929323742 and 34870105761 (same failing set) | `scripts/ci/clone_siblings.sh`: the pushed branch, a pull request's source and target, then `next`, then `develop` -- whichever each sibling actually has; all 13 pinned clones in 10 workflows | `608c9494b`, `2dbe8f847` |
| NPV-0125 | The Wine job built everything and died with `wine64: command not found` before a test ran (Ubuntu 24.04's wine64 package installs no such command). | CI, once past configure | installs `wine` and calls the `wine` launcher, which picks the 64-bit loader itself | `f261cf644` |
| NPV-0126 | **CI's SDL-free coverage missed where the defects were**: no xclip (so `X11ClipboardInterop` always skipped) and only HEADLESS, where NPV-0106/0114/0121 could not show. | reviewing the matrix against NPV-0106/0114/0121 | xclip + xsel in the SDL-free cell; new `x11-sdl-free-gpu` job (X11 + OPENGL33;VULKAN;SOFTWARE;HEADLESS, SDL OFF) that fails on any SDL in the cache, `NEEDED` or symbols and runs both games with every renderer on Xvfb; green on its first run | `ccd20214c` |
| NPV-0127 | An INCR requestor that died mid-transfer kept its transfer (and, since NPV-0119, its copy of the text) for the life of the process. | the NPV-0119 follow-up list | StructureNotify on the requestor, transfer dropped at its DestroyNotify; real INCR paste from another connection destroyed after one chunk | `f14d5ea85` |
| NPV-0128 | The content pipeline's media decoder compiled only against FFmpeg 7: FFmpeg 6 (the CI runners) declares `swr_convert`'s input `const uint8_t **`. | CI, once past configure | explicit cast, valid against both declarations (as `VideoDecoder.cpp` already did) | `5e8f58707` |
| NPV-0129 | The native MSVC job could not build the Win32 harness: UTF-8 literals read in the ANSI code page (C2015). | the job's first run in this pass | `/utf-8` under MSVC for the standalone harness | `860977a0c` |
| NPV-0130 | **Every Linux CI build failed in `SpriteFontContentPipeline.cpp`** (`'RasterGlyph' was not declared`): XNASWEEP-131/137 made the font-sheet route use the atlas packer, which sat inside `#if defined(CNA_HAVE_FREETYPE)`, and the runners have no FreeType. Only builds with FreeType -- this workstation's -- compiled. | CI run 34942450340, all nine Linux cells | the guard covers only the FreeType code; the packer is compiled in every build. The translation unit compiles with and without `CNA_HAVE_FREETYPE`; 114 sprite-font tests unchanged (the one failure, a HEADLESS render-target readback, fails identically without the change) | `0a3f9a24a` |
| NPV-0131 | **The SDL-free X11 cell could never pass its test step**: it builds the focused `CnaPlatformModuleTests` (not part of `all`), then runs ctest entries registered against `CnaTests`, which it never builds -- "Could not find executable", every entry Not Run. Hidden until now because the cell had never got past configure (NPV-0124) or compile (NPV-0128/0130). | CI run 34947184495 | `CNA_PLATFORM_CTEST_BINARY` (`CnaTests` by default) names the binary the CnaPlatform*/CnaX11* entries run; the cell sets `CnaPlatformModuleTests` and also runs `CnaX11EvdevTests`. Locally, the cell's ctest line through the focused binary: 6 passed, the window-manager suite skipped (no openbox here) | `41e8fc898` |

## Next steps (recommended order, 2026-09-15)

Estimates are for this kind of work in this repository; each step lists what it needs from the
owner.

Progress on branch `x11` (2026-09-15): step 1 -- NPV-0124, 0125, 0126, 0128, 0129, 0130, 0131;
what is left red is the one Win32 test below. Step 4 -- the INCR follow-up is NPV-0127; `globalPointer`
still waits for the owner. Step 5 -- Linux evdev is `plans/plan_x11.md` X11-0150; Win32 XInput is
out of this branch's scope. Steps 6-8 (X11 parts) are `plans/plan_x11.md` X11-0151..X11-0158,
all done: audio (0151), XIM (0152), exclusive fullscreen (0153), XDND (0154), touch and pens (0155),
per-monitor scale (0156), `PRIMARY` (0157) and clipboard formats (0158). The gaps left after them
are `plans/plan_x11.md` Phase N (X11-0160..).

| # | Step | Estimate | Needs | Notes |
|---|------|----------|-------|-------|
| 1 | **Make `platform-ci.yml` green** | 1-2 h | nothing | Almost every cell of "Platform implementation matrix" fails at configure, on `next` and long before this pass (run 34929323742 has the same failing set as 34870105761 at the starting HEAD): the workflow clones the sibling `sharp-runtime` from `develop`, which lacks the `Xml.Serialization` component CNA `next` asks for ("Unknown Sharp Runtime component 'Xml.Serialization'"); sharp-runtime has a `next` branch that has it. The native Windows job (`windows-latest`, real Windows Server, WARP rather than a GPU) runs only on `workflow_dispatch`, so it never runs on a push. The Wine job fails at "Run the platform contract and Win32 suites under Wine" -- cause not yet investigated. While there: install `xclip` in the SDL-free X11 cell (X11ClipboardInterop skips without it) and add an SDL-free cell with a GPU renderer (OPENGL33), the configuration NPV-0106, NPV-0114 and NPV-0121 slipped through. |
| 2 | **Finish the X11 desktop validation** | 1-2 h | an unlocked session left alone for the run (or 20-30 min of the owner's time for `interactive`) | the commands under "Remaining gaps"; includes NPV-0113 on mutter |
| 3 | **Native Windows validation of Win32** | 3-6 h, plus fixing what it finds | the Windows 10 VirtualBox VM (disk attached) and a way in: `VBoxManage guestcontrol` with Guest Additions, a shared folder, or SSH; about an hour of the owner's time for the interactive checklist | docs/testing-win32-native.md; the script's first run is also its own test |
| 4 | X11 follow-ups | 2-4 h | owner's decision on `globalPointer` | `globalPointer` under Xwayland; INCR entries of a requestor that died |
| 5 | Controllers on both platforms | Win32 XInput 6-10 h; Linux evdev 15-25 h | owner's go-ahead | Win32 design already sketched in plans/plan_win32.md §15; the evdev work also serves a future Wayland backend |
| 6 | Native audio for SDL-free builds | 30-50 h | owner's go-ahead | the largest gap of an SDL-free game: NULL audio only today |
| 7 | IME | Win32 8-12 h (plans/plan_win32.md §15); X11 via XIM 10-15 h | owner's go-ahead | both report `ime=false` truthfully today |
| 8 | The rest of the deferred list | X11: exclusive fullscreen 6-10 h, XDND + touch 12-18 h, per-monitor DPI/PRIMARY/formats 7-11 h; Win32: tray 2 h | owner's go-ahead | Wayland backend (50-80 h) only on explicit permission |

## Win32: NOT NATIVE WINDOWS VALIDATION

Everything in this section ran on Debian 13 with MinGW-w64 (GCC 14, win32 threads) and **Wine 10.0**.
Wine reimplements the Windows API; none of this is evidence about Windows itself.

| Item | Result |
|---|---|
| Cross-build blocker found | sharp-runtime `Process.cpp` included `<poll.h>` unconditionally, so no Windows cross-build of CnaTests-scale targets compiled. Fixed in sharp-runtime `88c12f15` (local, not pushed). |
| `tools/platform/standalone_tests` → `cmake-build-win32`, Debug and Release | 0 warnings, 0 errors; `cna_platform_tests.exe`, `cna_win32_directx_probe.exe` |
| Full CNA cross-build → `cmake-build-d3d11` (Release, `CNA_PLATFORM=WIN32`, `CNA_ENABLE_SDL=OFF`, NULL audio, DIRECTX11;DIRECTX12;SOFTWARE;HEADLESS) | `cna_demo_2d.exe`, `cna_demo_renderer_selection.exe`; 24 warnings, none in CNA platform code (16 `-Wdeprecated` in vendored draco headers, 8 GCC 14 flow warnings inside libstdc++) |
| Platform suite under Wine (Xvfb), Debug and Release | **385 passed, 1 skipped** each (`PlatformConformance.AnUnsupportedCapabilityRefusesNamingItself/Win32`: no unsupported capability to name) |
| D3D probe under Wine, private Xvfb | window, D3D11 device/swap chain/clear/present/resize, D3D12 device/queue/swap chain/resize, close request -- all ok (WineD3D on llvmpipe: software) |
| D3D probe under Wine on the real desktop (AMD 780M) | default prefix (WineD3D → OpenGL/radeonsi; Wine's vkd3d → Vulkan/RADV for D3D12): all stages ok. DXVK prefix (`dxvk-setup`, DXVK 2.6 → RADV): D3D11 ok; D3D12 "unavailable" (E_NOINTERFACE: Wine's vkd3d `d3d12.dll` asks the adapter for a Wine-private interface DXVK's DXGI does not implement -- an environment mix, reported by the probe as unavailability, exit 0) |
| `cna_demo_2d.exe --smoke` under Wine | Xvfb: DIRECTX11, DIRECTX12, SOFTWARE ok. Real desktop, default prefix: DIRECTX11 (feature level 11_1 via WineD3D), DIRECTX12 (feature level 11_1 via vkd3d), SOFTWARE ok. DXVK prefix: DIRECTX11 (DXVK) and SOFTWARE ok; DIRECTX12 fails as above -- the explicitly requested renderer cannot be created, and the exception ends the process through `terminate()` |
| SDL-free dependency proof (PE) | `cna_demo_2d.exe` imports d3d11, d3d12, D3DCOMPILER_47, dxgi, GDI32, KERNEL32, msvcrt, ole32, OPENGL32, SHELL32, USER32 -- no SDL; the only "SDL_" strings are CNA's own renderer identity names `SDL_GPU`/`SDL_RENDERER`. The standalone test executables additionally need `libstdc++-6.dll` and `libgcc_s_seh-1.dll` beside them. |

## Native Windows validation deferred

Not attempted, deliberately (no Windows installation or VM was to be created here). A Windows 10
VirtualBox VM prepared by the owner is the intended place; docs/testing-win32-native.md has the
procedure and tools/platform/validate_win32_native.ps1 the automated half. Open items:

1. The platform suite, the D3D probe and the demo on real Windows (the script; its own first run).
2. DXGI flip-model presentation, tearing, fullscreen transitions and device removal on a Windows
   GPU driver (in a VM: the virtual adapter; D3D12 probably unavailable there -> ENVIRONMENT).
3. Per-monitor DPI v2 and `WM_DPICHANGED` across monitors of different scale.
4. Keyboard layouts (Czech), dead keys and AltGr through the Windows keyboard stack.
5. IME composition and candidate windows (Microsoft Pinyin, Japanese IME).
6. Clipboard with real applications, including large and non-ASCII text.
7. Raw-input relative mouse, high-resolution wheels, capture while dragging.
8. Focus/activation rules, Alt+Tab, Win+L and resume, Aero Snap, taskbar minimise/restore.
9. Exclusive fullscreen's display-mode change and its restoration (including on process kill).
10. XInput controllers.

## Regression runs

**Full ctest, SDL-free X11 build** (`cmake-build-x11`: X11, HEADLESS, NULL audio, `CNA_ENABLE_SDL=OFF`;
private Xvfb `:95`; `ctest -j8`): 9208 of 9254 passed. The 46 others, each classified:

| Class | Tests | Evidence |
|---|---|---|
| Pre-existing, independent of this work | 13 content/renderer tests (`CnbTexture*`, `Cnj*`, `ContentManagerSkinnedModelTest.*`, `XnbContainerFuzzTest`, `XnbContentPipelineTest.SpriteFont...`, `GltfRendererIndexWidthPolicy`) and `GraphicsDeviceCapabilityTest.SupportsMultipleRenderTargets` | fail identically with the pre-session CnaTests binary (SDL3, built 2026-09-13, before any commit of this pass) |
| Pre-existing gates owned elsewhere | `CApi*` (5: the recorded ABI baseline predates a renderer identity added since), `CNAEXT_MatrixCompleteness`, `CNAEXT_NoPosixSetenv` (RLGL examples), `CnaXnbModelCorpusSweep` (Draco fixtures now build because libdraco is installed), `CnaGltfConformanceL0` (contains the index-width failure) | their own messages; nothing in them touches platform code |
| Parallel-run artifacts | 5 ENet tests, `LeaderboardWriterTest.SettingRating...` | pass run alone |
| Found and fixed here | 5 `ModuleLinkClosure_NativeSdkFree_*` (NPV-0121), 2 `X11IsSdlFree.*` (NPV-0122), 5 `Headless_*` "Not Run" (NPV-0123), 5 X11 live duplicates colliding on the shared display (NPV-0119/NPV-0120) | fixed; see Defects |

After the fixes the X11 build registers 9179 tests (no X11 live duplicates), builds completely, and
the platform/X11 aggregates, the 20 module link-closure gates and the 45 Headless tests pass.

**Full ctest, SDL3 build** (`cmake-build-debug`: SDL3 platform and audio, HEADLESS, `CNA_ENABLE_VIDEO=OFF`;
rebuilt with every change of this pass; private Xvfb; `ctest -j8`): 9039 of 9077 passed. The 38
others:

| Class | Tests | Evidence |
|---|---|---|
| The same pre-existing set as the X11 run | 23 (13 content/renderer, `SupportsMultipleRenderTargets`, `CApi*` x5, `CNAEXT_*` x2, `CnaGltfConformanceL0`, `CnaXnbModelCorpusSweep`) | as above |
| This build has no FFmpeg (`CNA_ENABLE_VIDEO=OFF`) and the tests assume a decoder | `XnaBuildDeterminism.*` x3, `XnaSourceToOutput.*` x3, `XnaAudioContent/XnaAudioProcessors.RefusalsMatchXna`, `XnaContentProjectCommandLine...AudioSource...`, `XnaPipelineGenuineRuntimeBuiltFamilies`, `MediaLibraryTestFixture` Album/Playlist duration | "this build has no audio decoder" / "no media decoder" / the VideoProcessor cannot read a WMV's dimensions; all pass in the X11 build, which has FFmpeg |
| Parallel-run artifacts | `XnaRouteScaling.TheCoordinatorIsNotQuadratic...`, `CnaInputTests` | pass run alone |
| Environment | `Headless_Smoke` check A ("SDL video never initialised") | with a DISPLAY set, the SDL3 *platform* initialises SDL video; the check is unchanged by NPV-0123, which only compiles it out where no SDL exists |

No failure in either run traces to a change of this pass.

## Capability truthfulness

| Capability (X11 on Xwayland) | Reported | Observed | Verdict |
|---|---|---|---|
| `openGlContext` | true | hardware GLX works; after NPV-0121 it also requires a loadable GL library | truthful |
| `vulkanSurface` | true | RADV swapchains present | truthful |
| `relativeMouse` | true | works while the window has focus (NPV-0109) | truthful |
| `clipboard` | true | 23/23 against X clients | truthful |
| `borderlessFullscreen` | true (mutter advertises it) | GLX rendered at 1536x960 fullscreen | truthful |
| `highDpi` | false | Xft.dpi 96 by policy | truthful by the documented policy |
| `ime` / `inputDeviceEnumeration` | false | not implemented | truthful |
| `globalPointer` | true | reads are exact only over X windows under Xwayland; stale elsewhere | **over-claims under Xwayland** -- documented (docs/platform-x11.md "Under Xwayland"); follow-up below. SDL3 claims it unconditionally too. |

## Error and log audit

Every desktop and Xvfb log of this pass was scanned: the only X protocol error is the deliberate
`BadValue` of `ADeliberateBadRequestDoesNotEndTheProcessAndLeavesTheConnectionUsable`; no sanitizer
output; the D3D12 debug-layer warning under Wine is expected. The compositor journal during the
runs shows two non-fatal mutter assertions (`meta_window_set_stack_position_no_sync`) at the start
of the clipboard scenario, when the wl-copy probe ran -- most likely wl-copy's own focus surface on
the locked session; not attributed to CNA. Before the harness switched its WM requests to real
timestamps, mutter logged "Tried to ping window ... with a bad serial" for them (harness, fixed).

## Upstream and environment findings (not CNA defects)

- Mesa 25.0.7's X11 Vulkan WSI leaves a GC per swapchain on the application's connection (RADV and
  lavapipe), and lavapipe on Xwayland also its three `ShmSeg` attachments -- reproduced by a
  CNA-free program: an Xlib window, `VK_KHR_xlib_surface`, then create and destroy surface, device
  and swapchain in a loop while counting the client's resources per type through X-Resource (GC
  2, 3, 4 ... after each full teardown).
- Mesa swrast GLX leaks ~100 KB per display connection (LSan; CNA-free reproduction) -- the only
  LSan suppression.
- wl-clipboard cannot reach mutter's clipboard (no data-control protocol); GNOME's lock screen
  denies focus to every application window; mutter throttles presentation to hidden windows.
- gnome-shell 48.7 aborted at 21:23:27 on an untrapped BadWindow from its own `X_SendEvent` (no CNA
  process connected).

## Remaining gaps

**Needs an unlocked desktop session** (then, from the repository root, `DISPLAY=:0` and the session's
Xwayland XAUTHORITY):
`cna_x11_desktop_validation lifecycle`, `wm --toggles 100` (includes NPV-0113 on mutter), `stress --ops 10000`,
`keyboard`, `text`, `mouse`, `relative`, `soak --seconds 600`, and a full-length `glx`/`vulkan` for
unthrottled frame rates. The keyboard scenario switches layouts with Super+Space and restores them.

**Needs a person**: `cna_x11_desktop_validation interactive` (real keys, real mouse), IME with a running
ibus.

**Needs native Windows**: the list above.

**Follow-ups found here, not done** (each a decision for its owner):
- `globalPointer` under Xwayland: report false there, or narrow the contract.
- Examples that clear depth without a GraphicsDeviceManager since SOFTWARE-333 (NPV-0118 fixed only
  the 3D house demo): an audit for the graphics owners.
- ~~An INCR requestor that dies mid-transfer leaves its entry (and copy) until the next transfer to
  that window.~~ Fixed, NPV-0127.
- ~~The CI SDL-free cell does not install xclip~~ (fixed, NPV-0126); it still runs a fixed test
  subset, which is how NPV-0121/0122/0123 went unnoticed.
- **Win32, for the Win32 phase:** `Win32RendererBridge.TheHandleSurvivesAResizeUnchanged` fails on
  the native `windows-latest` runner as well as under Wine (client height 749 where 768 was asked;
  CI run 34942450340). Most likely the runner's 1024x768 desktop -- Windows keeps a captioned window
  within the screen, so a 1024x768 client area cannot exist there -- which would make it the test's
  assumption rather than a backend defect; not investigated further on this branch.

**Deferred on the owner's decision (2026-09-14) -- planned, not started**, estimates as given:
gamepad over Linux evdev with hotplug, XNA mapping and rumble (15-25 h); native audio without SDL --
PipeWire/Pulse/ALSA output plus a non-SDL decoder/mixer, the largest gap of SDL-free builds (30-50 h);
IME through XIM (10-15 h); exclusive fullscreen through XRandR (6-10 h); XDND and XI2 touch/pen
(12-18 h); per-monitor DPI, PRIMARY selection, more clipboard formats (7-11 h). A native Wayland
backend (50-80 h) waits for the owner's explicit go-ahead.
