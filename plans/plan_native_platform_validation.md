# Native Platform Validation Plan (X11 real desktop + Win32 on Debian/Wine)

## Status summary (top of file is source of truth)

In progress. The integrated Win32 + X11 tree (plans/plan_native_platforms_integration.md) is being
validated on a real Debian 13 workstation: real GPU, real GNOME desktop, two real monitors. Every
defect found so far is listed in the "Defects found" section with the test that caught it and the
commit that fixed it. Win32 evidence here is MinGW + Wine only and is **NOT native Windows
validation**; the native-Windows items are listed separately and deliberately left open.

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
| Monitors (XRandR on `:0`) | **2 active**: `DP-1` primary 2048x1152+0+0 and `DP-2` 2048x1152+2048+0, both 600x340 mm; screen 4096x1152 |
| Physical monitors | 2 × Dell S2725DC, 2560x1440 @ 143.97 Hz |
| Desktop scaling | mutter `scale-monitor-framebuffer`, scale 1.25 per monitor; Xwayland is *not* in native-scaling mode, so X clients see a logical 2048x1152 per monitor and are upscaled by the compositor |
| `Xft.dpi` | 96 (so CNA's documented policy gives display scale 1.0 and `highDpi` false) |

## Task table

| ID | Task | Status | Notes / evidence |
|----|------|--------|------------------|
| NPV-0001 | Phase 0 baseline | ✅ | see Phase 0 above |
| NPV-0002 | Phase 1/2 session and GPU classification | ✅ | see Phase 1/2 above |
| NPV-0003 | Phase 3 full integrated test baseline | 🟨 | X11 suites on private Xvfb: mapping 30/30, integration 48/48 (+ new tests: 52/52), window-manager 15/15 (+1 new: 16/16) once openbox actually ran. Found the WM suite had silently run 1 of 15 tests (NPV-0107/NPV-0108). Full CnaTests run pending. |
| NPV-0004 | Phase 4 ASan/LSan/UBSan on the merged tree | ✅ | `build-asan/` (`-DCNA_SANITIZE=address,undefined`, GCC 14, `-Wno-error=maybe-uninitialized` because GCC 14 reports a known false positive inside libstdc++ `<regex>` under sanitizers in sharp-runtime). LSan confirmed active first with a deliberately leaking probe. Private Xvfb + openbox: X11 mapping 30/30, X11 integration 52/52, window-manager 16/16, full platform module suite 432 passed / 2 skipped, audio 225 passed / 8 skipped (no hardware), content audio/WAV subset 74/74 — 0 ASan errors, 0 UBSan reports, 0 unsuppressed leaks. Found and fixed NPV-0102, NPV-0103, NPV-0104, NPV-0105. The only suppression (`tools/platform/lsan_x11_mesa.supp`, `leak:libGLX_mesa.so`) is for a Mesa swrast leak reproduced byte-for-byte by a CNA-free 20-line program; the RADV ICD's 2x128-byte init leak is recorded, not suppressed. |
| NPV-0005 | Phase 5 real X11 window lifecycle | 🟨 | defects NPV-0101 found; harness scenario `lifecycle` pending |
| NPV-0006 | Phase 6 long-running window stress | ⬜ | |
| NPV-0007 | Phase 7 keyboard | ⬜ | |
| NPV-0008 | Phase 8 text input | ⬜ | |
| NPV-0009 | Phase 9 mouse | ⬜ | |
| NPV-0010 | Phase 10 XInput2 raw relative mouse | 🟨 | first real-desktop run: see NPV-0109/NPV-0110 |
| NPV-0011 | Phase 11 clipboard interoperability | ⬜ | |
| NPV-0012 | Phase 12 real window-manager validation | 🟨 | existing suites pass on `:0` against mutter (after NPV-0107) — pending harness scenario |
| NPV-0013 | Phase 13 XRandR/display validation | ⬜ | |
| NPV-0014 | Phase 14 DPI/scale | ⬜ | |
| NPV-0015 | Phase 15 hardware GLX | ⬜ | |
| NPV-0016 | Phase 16 hardware Vulkan X11 | ⬜ | |
| NPV-0017 | Phase 17 renderer spot-checks | ⬜ | |
| NPV-0018 | Phase 18 software presenter | ⬜ | |
| NPV-0019 | Phase 19 GLX/Vulkan/resource lifetime stress | ⬜ | |
| NPV-0020 | Phase 20 SDL-free real-hardware proof | 🟨 | NPV-0106 found and fixed: an SDL-free configuration could not even configure with a GPU renderer |
| NPV-0021 | Phase 21 SDL3 regression on this machine | ⬜ | |
| NPV-0022 | Phase 22 optional performance measurements | ⬜ | |
| NPV-0023 | Phase 23 Win32 MinGW cross-build validation | ⬜ | |
| NPV-0024 | Phase 24 Wine Win32 validation | ⬜ | |
| NPV-0025 | Phase 25 Wine D3D11 validation | ⬜ | |
| NPV-0026 | Phase 26 Wine D3D12 validation | ⬜ | |
| NPV-0027 | Phase 27 Win32 SDL-free dependency proof | ⬜ | |
| NPV-0028 | Phase 28 native-Windows-deferred list | ⬜ | |
| NPV-0029 | Phase 29 future Windows 10 validation script/doc | ⬜ | |
| NPV-0030 | Phase 30 representative CNA applications | ⬜ | |
| NPV-0031 | Phase 31 soak test | ⬜ | |
| NPV-0032 | Phase 32 error/log audit | ⬜ | |
| NPV-0033 | Phase 33 capability truthfulness audit | ⬜ | |
| NPV-0034 | Phase 34 bug fixing | 🟨 | see "Defects found" |
| NPV-0035 | Phase 35 broad regression after fixes | ⬜ | |
| NPV-0036 | Phase 36 final git audit | ⬜ | |

## Defects found

Numbered NPV-01xx. Each was reproduced before it was fixed, and each fix has a regression test
that fails without it.

| ID | Defect | Found by | Fix | Commit |
|----|--------|----------|-----|--------|
| NPV-0101 | **X11 window registry kept raw pointers to destroyed windows.** `X11Window` never told the platform it was being destroyed, so `windows_` and the mouse/text-input/GL/Vulkan registries dangled: the next `PollEvents` walked freed memory (`FindWindowByXid`), the last live window's close request produced no `QuitEvent` (the dead window still counted), a close request queued for an already-destroyed window became `CloseRequested(0)` + `QuitEvent`, and moving focus after the focused window was destroyed **segfaulted** (dangling `X11TextInput::focused_`). | three new `X11Live` tests reproduced it without a sanitizer (2 failures + a SIGSEGV) | `X11WindowHost` (mirrors `Win32WindowHost`): the window reports its destruction first, identity-matched; unknown-window `WM_DELETE_WINDOW` is ignored; text input drops its focus pointer | pending |
| NPV-0102 | **Heap-use-after-free at exit of every X11 application using the XNA API.** `X11ErrorPolicy`'s registry vector and mutex were function-local statics created after CurrentPlatform's lazy-default owner, so `exit()` destroyed them first and `~X11Connection` → `Unregister` touched freed memory. | ASan, at the end of `CnaAudioTests`; reproduced deterministically by the new `cna_platform_x11_exit_harness` (old code: exit 1 with the UAF; fixed: exit 0) | the policy's state is immortal | pending |
| NPV-0103 | `SetNetWmState` called `memcpy(nullptr, nullptr, 0)` for an existing but empty `_NET_WM_STATE` (undefined behaviour; glibc declares both pointers `nonnull`). | UBSan, `PlatformWindowConformance.StateChangesFollowTheOperationalErrorContract/X11` | copy only a non-empty list | pending |
| NPV-0104 | **SDL-free builds refused float/MS-ADPCM/IMA-ADPCM XNB SoundEffect transcoding** ("requires CNA_AUDIO_PLATFORM=SDL3 because no decoder is available") — a stale `#ifdef SOUND_ENABLED` left from before the decoder became CNA's own and SDL-free. | `XnbContentPipelineTest.SoundEffectCodecMatrixPreservesDecodedPcmAndMetadata` failing under `CNA_AUDIO_PLATFORM=NULL` | guard removed; the decoder is compiled for every audio platform | pending |
| NPV-0105 | **`SoundEffect::Duration` was zero for every effect under NULL/SDL2 audio** (only the SDL3 mixer was consulted). | `CnbSoundEffectCodecTest.ASoundEffectCnbLoadsThroughContentManager` failing under NULL audio | frames and rate recorded from the PCM16 data in every build; new `SoundEffectDurationTests.cpp` runs on every audio platform (the SDL3-only `SoundEffectTests.cpp` is excluded from NULL builds, which is why nothing caught it) | pending |
| NPV-0106 | **`CNA_ENABLE_SDL=OFF` could not configure with any GPU renderer** (OPENGL33/VULKAN/SOFTWARE): the renderer example suites link `SDL3::SDL3` unconditionally (923 configure errors). The SDL-free CI cell only ever used HEADLESS. | configuring `cmake-build-multi` (X11, SDL OFF, `OPENGL33;VULKAN;SOFTWARE;HEADLESS`) | those three suites skip, with a STATUS line, when no SDL3 target exists | pending |
| NPV-0107 | **The window-manager suite would evict a real desktop's window manager** (`openbox --replace` on whatever `DISPLAY` names; gtest-discovered copies inherit the shell's `DISPLAY`), and it reported ctest "Passed" with 14 of 15 tests skipped when openbox died at start-up. Its close test also used `xdotool windowclose` (XDestroyWindow), not the close-button protocol it claimed. | reading the fixture before pointing it at `:0`; a broken user-space openbox | uses a running EWMH window manager when present, never `--replace`; distinguishes "openbox died" (skip) from "WM advertises fullscreen but CNA missed it" (fail); close goes through `_NET_CLOSE_WINDOW`; external destroy is its own test | pending |
| NPV-0108 | **Xvfb launcher without `-noreset`**: the server regenerated whenever the last client disconnected, the next test's `XOpenDisplay` failed with ECONNRESET, and alternate `X11Live` tests skipped as "cannot reach the X server" (ctest: Passed). | an `LD_PRELOAD` shim on `XOpenDisplay` logging errno 104 | `-noreset` | pending |

## Win32: NOT NATIVE WINDOWS VALIDATION

(filled in by NPV-0023..NPV-0027)

## Native Windows validation deferred

(filled in by NPV-0028)
