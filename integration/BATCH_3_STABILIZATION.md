# BATCH_3_STABILIZATION.md — Batch 3 stabilization and checkpoint record

Companion to `BATCH_0_COMPLETE.md` / `BATCH_1_STABILIZATION.md` / `BATCH_2_STABILIZATION.md`.
Covers the two Batch 3 lanes (`sokol`, `diligent`), the stabilization pass after `diligent`'s
integration, and the checkpoint decision.

**Scope.** `diligent` was integrated in this session and Batch 3 was then stabilized. No third lane
was begun, no published history was rewritten, `audit/` was not touched, and **nothing was pushed**.
Host: HP EliteBook 840 G9 (12th Gen i7-1260P, 15 GiB RAM, 16 GiB swap), the same machine Batch 2's
retake and the `sokol` lane ran on.

---

## 1. Identity

| Field | Before | After |
|---|---|---|
| Integration HEAD | `37066e453` (the `sokol` merge) | **`aa9f3fb51`** (the `diligent` merge) |
| Planning HEAD | `530925b33` | `530925b33` → this session's documentation commit |
| Lane merges on `integration/post-audit-phase1` | 12 | **13** |
| Logical inventory | 21 total / 12 integrated / 9 pending | **21 total / 13 integrated / 8 pending** |
| Pending lanes | `direct2d`, `gl`, `diligent`, `gdi`, `glide`, `llgl`, `skia`, `html-dom`, `metal` | `direct2d`, `gl`, `gdi`, `glide`, `llgl`, `skia`, `html-dom`, `metal` — **8** |

Start gate, all verified before anything was modified: integration HEAD `37066e45` and planning HEAD
`530925b3` as expected; exactly twelve lane merges with `sokol` twelfth; `diligent` pending; no
thirteenth lane begun; Batch 0/1/2 checkpoint ancestry intact and all four checkpoint tags annotated
and **GPG-good**; `feature/sokol` and `feature/diligent` unmoved and byte-equal to their archive
tags; both writable worktrees clean; four user stashes present and untouched; GPG signing preflight
good (key `0AADA55F`, `commit.gpgsign` true, `tag.gpgsign` unset so `git tag -s` is passed
explicitly).

## 2. `sokol` — re-verified, unchanged

The twelfth lane was **not reopened**. Its refs are exactly where it left them
(`feature/sokol` = `261ea700`, `adapt/sokol` = `9fb83a99`, archive tag unchanged), and its shared
changes — `OcclusionQuery::Dispose(bool)`, `ShaderEffect::Dispose(bool)` and the
`CNA_BACKEND_SOKOL` window flag — were treated as part of the integration baseline and **not
reverted to ease `diligent`'s integration**.

**One genuine Batch 3 stabilization fact was corrected here:** `SOKOL` was missing from the in-repo
`CLAUDE.md` `CNA_GRAPHICS_BACKEND` list — a gap in the twelfth lane's own registration union, found
while adding `DILIGENT` to that same line. Because `sokol` is the other Batch 3 lane and this is
Batch 3's own stabilization, it was fixed rather than deferred. `MAGNUM`'s absence from
`README.md`'s list is **Batch 2's** gap and was deliberately left alone.

**Sokol focused control, built from the adapted sources: 37/37, zero failures.** This control was
run — rather than skipped — precisely because this lane's registration union touches shared files
`sokol` introduced or modified: `cmake/BackendSelection.cmake`, `cmake/CnaLibrary.cmake`,
`GraphicsDevice::getBackendWindowFlags()`, `WireFrameTriangleOracle.hpp`,
`GraphicsBackendTypeTests.cpp` and three shared test fixtures. Sokol is unaffected by every one.

## 3. `diligent` — the thirteenth lane

Full record: **`integration/lanes/diligent.md`**. Summary:

| Field | Value |
|---|---|
| Original / archive | `feature/diligent` `1ab12b50`, `archive/preintegration/diligent-20260804`, both unchanged |
| Adaptation | `adapt/diligent` → `27f7dcef`, **70 signed commits** (65 replayed 1:1 + 5 added) |
| Merge | **`aa9f3fb5`**, signed, `--no-ff`, zero conflicts, merged tree **byte-identical** to `adapt/diligent` |
| Dependency | DiligentCore **`v2.5.6`**, fetched and built from source, **nothing vendored, no carried patch**, Apache-2.0 |
| Native API | runtime-selected; here **Vulkan on lavapipe** by default and **OpenGL 4.5 on llvmpipe** for the `_OpenGL` variants |
| Interface drift | **one** reference to the removed `GpuDrawParams::instanceVb` |
| Public identity | **31st** — `DILIGENT`, genuinely its own backend, not an alias |

## 4. Consolidated current-head baseline

| Instrument | Result |
|---|---|
| Diligent dedicated suites | **78 registered · 69 passed · 8 failed · 1 skipped** |
| Diligent corpus (`ctest -j1`, whole tree) | **5816 registered · 5800 passed · 8 failed · 7 truthful skips** |
| Sokol focused control (adapted sources) | **37 / 37 / 0** |
| EasyGL continuity (adapted sources) | **6190 registered · 296 `(Not Run)` · 5894 executed · 5893 passed · 1 failed · 6 skips** |
| Diligent sanitizers (representative 13-harness subset) | **0 ASan errors · 0 CNA-originating UBSan errors · 0 CNA-owned leaks**; `detect_leaks=0` control **23/25** |

### 4.1 Failure classification — every failure, individually

| # | Failure | Class |
|---|---|---|
| 1 | `Diligent_DepthBias` (Vulkan) | Pre-existing, in the lane's own pre-adaptation baseline. Constant-`DepthBias` has no observable effect on this software rasterizer; 6 of its 7 checks pass. Two independent precedents: `D9-62`, `Vulkan_DepthBias`. **Environment limitation** |
| 2–7 | `Diligent_Instanced_OpenGL`, `Diligent_InstancedStride_OpenGL`, `Diligent_MultiSampleMask_OpenGL`, `Diligent_ReferenceStencil_OpenGL`, `Diligent_RenderTargetMipGen_OpenGL`, `Diligent_DepthBias_OpenGL` | Pre-existing, all six in the pre-adaptation baseline and all recorded by `DILIGENT-66`. Upstream/driver, not CNA |
| 8 | `Diligent_InstanceBindingOffsets_OpenGL` | **New this session** (`DILIGENT-69`), and **not a CNA defect**: the same already-root-caused Mesa/llvmpipe per-instance-divisor limitation as #2/#3 — under GL the per-instance attribute reads as zero for every instance, so all instances draw stacked at the origin. The same test is **12/12 on Vulkan** |
| 9 | `TwoProcessLoopbackTest.HostMigration…` (EasyGL control only) | The known networking **Outcome C** coin flip. **3/3 green** re-run in isolation. It did not fire in the Diligent corpus at all |

**Zero regressions.** Seven of the eight Diligent failures are the pre-adaptation baseline's exact
set, and the eighth joins an existing documented class.

### 4.2 The `(Not Run)` entries, both explained

- **296 in the EasyGL control** — the dedicated EasyGL harnesses do not compile in this environment
  (`PixelTestGame.hpp` cannot resolve `SDL3/SDL.h` under the EasyGL configuration). **Control-proven
  pre-existing**, first recorded by `sokol`; the EasyGL instrument is therefore its gtest corpus.
- **1 in the Diligent corpus** — `StrictXnaApiSurfaceCheck_Compile_Run`, whose binary was outside the
  built target set. Built and re-run in this session: **passes**. Not a defect.
- **38 in the sanitizer run** — deliberate: only 13 of 32 harnesses were built with sanitizers, which
  is what "representative suite" means here.

### 4.3 Runtime identities

| Configuration | Device |
|---|---|
| Diligent, default preference order | **Vulkan**, `Using physical device 'llvmpipe (LLVM 19.1.7, 256 bits)'` — Mesa's **lavapipe** ICD |
| Diligent, `CNA_DILIGENT_DEVICE=opengl` | **OpenGL 4.5 (Compatibility Profile)**, Mesa 25.0.7, **llvmpipe** |
| Sokol / EasyGL controls | desktop GL on llvmpipe |
| Display | Xvfb `:101`, 1920×1080×24, `SDL_VIDEODRIVER=x11` |

A real Intel Iris Xe GPU is present and exposed to Vulkan on this host, but under Xvfb the surface
is served by lavapipe — the same software-device situation every prior batch measured, and the one
the lane's own records assume.

## 5. Sanitizer consolidation

Runtimes proven linked rather than assumed: `ldd` reports `libasan.so.8` and `libubsan.so.1`, and the
binary exports 54 `__asan_`/`__ubsan_` symbols.

- **ASan: 0 errors.** No use-after-free, no out-of-bounds, no double release, no stale pointer, no
  uninitialized backend state.
- **UBSan: 126 runtime errors, 0 of them CNA-originating.** All are inside third-party code —
  `BufferVkImpl.hpp/cpp` and `DeviceContextVkImpl.cpp` (misaligned `CtxDynamicData`, an upstream
  DiligentCore Vulkan alignment issue) and glslang's `hlslParseHelper.cpp`. Verified by grep for a
  CNA source path over the whole run: zero hits.
- **Leaks: 0 CNA-owned.** Every leak block was attributed by its **real allocating frame** (frame
  #1, not the ASan interceptor): 1522 resolve to DiligentCore / the lavapipe driver / Mesa, and the
  remaining 477 are STL allocations of DiligentCore's own types (`FramebufferCache`,
  `ObjectsRegistry`, `VulkanObjectWrapper`, `SamplerDesc`). CNA frames appear only deeper in those
  stacks, as the caller that drove a Diligent allocation — Diligent's ownership, not CNA's.
  **No CNA-originating leak was suppressed.**

**One recorded deviation:** `-fno-sanitize=vptr` is required. UBSan's vptr check needs RTTI and
DiligentCore builds glslang/SPIRV-Tools with `-fno-rtti`, so leaving it on fails the link with
`undefined reference to typeinfo for glslang::TShader`. ASan is untouched and every other UBSan
check remains enabled; the disabled check could not have applied to `-fno-rtti` code anyway.

## 6. History, signature and attribution gate

| Check | Result |
|---|---|
| Commits in `37066e45..aa9f3fb5` | 71 (70 lane commits + the merge) |
| Signatures | **71 × `U`** — good signature, uncertified key, this project's normal state. Zero `N`, zero `E` |
| Authors / committers | **71 × `Robert Vokac <robertvokac@robertvokac.com>`** |
| Attribution sweep | **zero hits** across subjects, bodies, authors, committers |
| `range-diff` vs the archive tag | **65/65 pairs in order** — 28 byte-identical (exactly the Robert-authored originals), 37 differing (exactly the Claude-authored ones), plus 5 adaptation commits appended |
| `git diff --check` | clean |
| Original refs | `feature/sokol` and `feature/diligent` both unmoved and byte-equal to their archive tags |

## 7. New findings

**One: `DILIGENT-69`**, recorded on `plans/plan_diligent.md`. It is *not* a CNA defect and not a
supported-path defect — it records why the session's own new instancing test shows red under the
OpenGL device type, so that red is attributable rather than unexplained. It joins `DILIGENT-66`'s
existing, already-root-caused GL divisor class and closes when that does.

No other independent defect was discovered. The four defects validation found were all
**adaptation-owned** and all fixed in-lane before the merge (`integration/lanes/diligent.md` §5);
none is a lane-owned or supported-path defect, so none warranted a ticket under the project's
convention.

## 8. Host, thermal and resource record

**This machine has a real thermal problem in the `balanced` power profile, and it was measured, not
assumed.** Package id 0 (`coretemp-isa-0000`) is the control sensor throughout.

| Phase | Package id 0 |
|---|---|
| Session start, idle | **54 °C** |
| First build at `-j4` under `balanced` | **95 °C within ~2 minutes** — build stopped immediately |
| Git-replay work under `balanced` (06:39–06:43) | **two samples at 100 °C**, two more at 96–97 °C |
| Link + test run in a gap between `powerprofilesctl launch` holds (07:14) | **93 °C** |
| Every build and test run under a held power-saver profile | **48–60 °C sustained** |
| Final | 50–55 °C |

**The mechanism behind the excursions is worth recording for future sessions:**
`powerprofilesctl launch -p power-saver -- <cmd>` holds the profile **only while that one command
runs**. Between commands the machine reverts to `balanced`, and every excursion above 84 °C happened
in exactly such a gap. The session therefore switched to a **persistent
`powerprofilesctl set power-saver`**, restored to `balanced` at the end. Under the persistent hold,
`-j8` sustains ~55 °C.

Individual-core turbo excursions are reported separately and did not alone drive policy; the
sustained readings above are what policy followed. Heavy work was stopped at the 95 °C reading and
resumed only after cooling, per the batch rule.

| Resource | Value |
|---|---|
| Max compilation parallelism | **`-j8`** — the session cap, reached once, for the sanitizer build only, on measured evidence (55 °C at `-j6`, 12 GiB RAM free). Eight was never exceeded |
| Ordinary builds | `-j6` after `-j4` evidence supported the increase |
| RAM | never below **10.4 GiB available** of 15 GiB |
| Swap | **516 MiB at start → 870 MiB peak** of 16 GiB. No sustained swapping |
| ccache | shared `/media/robertvokac/claude/tmp/cna/ccache`, 1.2 GiB of 5 GiB at session start |

### 8.1 Build trees

| Tree | Purpose | Size |
|---|---|---|
| `cmake-build-diligent-pre` | pre-adaptation baseline (created) | 13 G |
| `cmake-build-diligent` | ordinary Diligent (created) | ~13 G |
| `cmake-build-diligent-asan` | sanitizer Diligent (created) | 15 G |
| `cmake-build-diligent-sokol` | Sokol control from adapted sources (created) | — |
| `cmake-build-diligent-easygl` | EasyGL continuity from adapted sources (created) | — |

All on the owner-designated partition `/media/robertvokac/claude/tmp/cna/`. Nothing was built in the
scratchpad, `/tmp`, `/var/tmp` or `/dev/shm`. `~/deps/DiligentCore` was reused at its exact pin and
**not re-cloned**. No unrelated build tree was deleted and no final all-backend cleanup was
performed.

### 8.2 One environment interruption, recorded

Mid-session the harness killed every tracked background task, including the **Xvfb `:101` server a
previous session had started** and an in-flight build. Xvfb was restarted detached on the same
display and the build resumed; no result predating the interruption was reused without re-running.

## 9. Batch 3 stabilization result

| # | Check | Result |
|---|---|---|
| 1 | `sokol` green on its focused gates | ✅ 37/37 from the adapted sources |
| 2 | `diligent` focused gates green | ✅ 69/78, all 8 failures classified, zero regressions |
| 3 | EasyGL continuity green | ✅ 5893/5894 executed, the one failure 3/3 green in isolation |
| 4 | Shared changes do not regress the other lane | ✅ Sokol control run precisely because shared `sokol` files were touched |
| 5 | Sanitizer gates acceptable | ✅ 0 ASan, 0 CNA-originating UBSan, 0 CNA-owned leaks |
| 6 | Capability declarations truthful | ✅ exhaustive eleven-member switch, every `true` backed by a test or a direct measurement |
| 7 | No open supported-path production defect from either lane | ✅ none |
| 8 | Lane refs, archive tags and history clean | ✅ |
| 9 | 12 prior merges + `diligent` = 13 | ✅ |
| 10 | No fourteenth lane begun | ✅ |
| 11 | `audit/` untouched | ✅ |
| 12 | Four user stashes untouched | ✅ |
| 13 | All modified worktrees clean | ✅ |
| 14 | `git diff --check` | ✅ clean |

## 10. Checkpoint decision — **READY**

Every condition the batch brief sets for OUTCOME READY is met: both Batch 3 lanes accepted, no open
supported-path production defect from either, focused lane tests acceptable, shared continuity
controls acceptable, sanitizer gates acceptable, provenance clean, signatures clean, exactly
**13 integrated / 8 pending**, no fourteenth lane begun, `audit/` untouched, relevant worktrees
clean, `git diff --check` clean.

**Local signed annotated tag `integration/checkpoint-batch3-20260807` → `aa9f3fb51`.**
No conflicting Batch 3 tag existed beforehand. No existing tag was moved, retargeted or recreated.
**Nothing was pushed.**
