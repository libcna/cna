# NEXT_platform.md — SDL3/CNA platform separation

> Continuity document for the platform-abstraction campaign. The authoritative task list is
> **`plan_platform.md`** (PLAT-1…PLAT-141 plus lettered follow-ups); this file records state,
> discoveries and the next starting point. The design note that started it is `cnaplatform.md`.
>
> This file exists separately from the root `NEXT.md` following the repository's own convention
> for subsystem campaigns (`NEXT_skia.md`, `NEXTinput.md`, `NEXTaudio.md`, `NEXT_gdi.md`, …).
> **`NEXT.md` had no record of this campaign at all** until this file was added and cross-linked.

**Branch:** `feature/platform`
**Last updated:** 2026-08-12

---

## 1. READ THIS FIRST — the build-configuration trap

There are **three** build directories and they are not interchangeable. A change can compile and
pass in two of them while not being compiled *at all* in the third.

| Directory | Configure | Covers |
|---|---|---|
| `cmake-build-debug` | default (`CNA_PLATFORM=SDL3`, `CNA_GRAPHICS_RENDERER=HEADLESS`) | the SDL3 platform implementation |
| `cmake-build-headless` | `-DCNA_PLATFORM=HEADLESS` | the second platform implementation; the conformance suite's other arm |
| `cmake-build-devices` | `-DCNA_DEVICES=ON -DCNA_GRAPHICS_RENDERER=HEADLESS -DCNA_PLATFORM=SDL3` | **all of `modules/devices-ext` and `modules/devices`** |
| `cmake-build-terminal` | `-DCNA_PLATFORM=TERMINAL -DCNA_GRAPHICS_RENDERER=HEADLESS` | the terminal platform *as the selected one* (PLAT-130) |

`TerminalPlatform` itself is compiled in **every** POSIX configuration, not only that last one —
same arrangement as `HeadlessPlatform`, so the conformance suite always has three implementations
live in one process. `cmake-build-terminal` exists to prove the *selection* works: that
`CNA_PLATFORM=TERMINAL` configures, that it becomes the factory default, and that nothing else in
the tree assumed the default was SDL3.

`CNA_DEVICES` defaults to **OFF**. Every file in `modules/devices-ext` is wrapped in
`#ifdef CNA_DEVICES`, so with it off the whole module — implementation *and* tests — compiles to
nothing and its tests pass vacuously.

This was discovered at PLAT-90a, by which point **seven** devices-ext migrations (PLAT-100, 101,
103, 104, 107, plus the message box and file dialog rewrites) had been committed and reported as
verified without ever being compiled. They turned out to be correct, but that was luck, not
process. Always build `cmake-build-devices` before claiming a devices-ext change works.

Do **not** create build directories in the scratchpad — see `CLAUDE.md`, *Build locations &
caching*. Cap parallelism at `-j4`.

### Commands

```bash
cmake --build cmake-build-debug    --target CnaTests -j4 && ./cmake-build-debug/CnaTests
cmake --build cmake-build-headless --target CnaTests -j4 && ./cmake-build-headless/CnaTests
cmake --build cmake-build-devices  --target CnaTests -j4 && ./cmake-build-devices/CnaTests

cd cmake-build-debug && ctest -R CnaPlatform --output-on-failure   # SDL_VIDEODRIVER=dummy suites

python3 tools/platform/check_contract.py      # probe completeness + Doxygen coverage
python3 tools/platform/sdl_ratchet.py         # remaining SDL coupling vs budget
python3 tools/platform/hot_path_lint.py       # design decision 4
python3 tools/platform/sdl_inventory.py --check
```

**Do not run the full suite under `SDL_VIDEODRIVER=dummy`.** Five tests need real video and fail
under it (`MouseCursorTest` ×3, `Sdl3PlatformTest.PollEventsDiscardsStaleBufferContent`,
`GameWindowTest.MinimizeAndRestoreEXT_UsingSdlWindow`). The registered ctest suites set the dummy
driver themselves, scoped by `--gtest_filter`, which is why they pass.

---

## 2. Validation status

| Variant | Result |
|---|---|
| `cmake-build-debug` | **5812 passed, 0 failed** |
| `cmake-build-headless` | **5655 passed, 0 failed** |
| `cmake-build-devices` | **6471 passed, 0 failed** |
| `cmake-build-terminal` | **6315 passed, 0 failed** |

PLAT-137's focused terminal/session/conformance matrix was rerun after the exact Kitty path
landed: **120 passed** in `cmake-build-debug`, and **103 passed** in each of
`cmake-build-headless` and `cmake-build-terminal` (only environment/configuration skips). The
registered `CnaPlatformWindowTests` + `CnaPlatformTests` also pass; in a restricted sandbox set
`XDG_DATA_HOME` to a writable `/tmp` path so the preferences-path write test measures the service
rather than the sandbox's read-only home directory.

**Do not run a full suite while a build is saturating the machine.**
`TwoProcessLoopbackTest.HostMigrationPromotesOneSurvivorAndTheOtherReconnectsAcrossRealProcesses`
spawns two real processes and gives host migration a 30-second budget. It hit that budget once
here, purely because a four-way build was running alongside it; unloaded it completes in ~0.7 s.
A timeout in that test is a load symptom, not a regression — check the machine before
investigating the code.

**Run `CnaTests` from the repository root, not from the build directory.** Dozens of content,
media and audio-tag tests load fixture files by repo-relative path, so running
`cd cmake-build-debug && ./CnaTests` fails about 120 of them with assertion errors that look like
real product bugs. `gtest_discover_tests` sets `WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"`
(REMED-BUILD-001) precisely so ctest does not hit this; a bare run has to do it manually:
`./cmake-build-debug/CnaTests` from `/home/user/cna`.

The per-variant totals differ because the variants configure different option sets, not because
tests are missing: `TERMINAL` drops the `Sdl3*` test files (they reference symbols only the SDL3
selection compiles) and `cmake-build-debug` carries non-default options from earlier sessions.

Ratchet: **225 files / 3423 references** of direct SDL coupling outside the PLAT-3 allowlist, down
from the 253 / 3641 baseline. Contract: 24 headers, 383 documented declarations, all SDL-free.

The gtest binary has **no known failing tests**. The long-standing
`GraphicsDeviceValidationTest.SetRenderTargets_FourTargets_DoesNotThrow` failure was fixed —
see §4.

### Three failures the *full* `ctest` run has, and this campaign did not cause

The numbers above come from running `./CnaTests` directly. A full `ctest` in `cmake-build-debug`
also runs 250-odd registered harnesses and probes that the gtest binary does not, and three of
them fail. Each was **verified to fail identically on a stashed working tree**, so none is a
regression from this campaign — but they were not visible before, and they should not be mistaken
for one later:

| Test | Failure | Nature |
|---|---|---|
| `ModuleLinkClosure_probe_storage` | `FORBIDDEN link inputs: modules/platform/libcna_platform.a` | Build-graph. `modules/storage` links `cna_platform` PRIVATE (PLAT-109), but a static library cannot link anything privately — it propagates to consumers regardless, so the probe's minimal-closure rule and PLAT-109's edge contradict each other. Needs a decision: widen the probe's allowlist, or stop `StorageDevice` reaching the platform directly. |
| `Headless_Smoke` | `ArgumentOutOfRangeException: The requested primitive range exceeds the bound index buffer (primitiveCount 10)` | Product bug in the headless renderer harness, unrelated to the platform layer. |
| `Headless_PresentLifecycle` | every one of 22 legs reports `[SKIP] no usable display`, and the supervisor still exits non-zero | Harness. An all-skipped run should not be a failure in an environment with no display server. |

---

## 3. Where the campaign stands

**80 ✅ · 12 🟨 · 57 ⬜ · 5 ⛔ · 1 ❌** across `plan_platform.md` — about **54 %** of the 149
actionable rows done, counting partials.

- **Phase 0** (inventory, gates, baselines) — done except PLAT-7 (performance baseline).
- **Phase 1** (the contract) — done. 24 headers under `modules/platform/include/CNA/Platform/`.
- **Phase 2** (SDL3 implementation) — largely done.
- **Phase 3** (runtime) — `Game` owns the platform, timing and cursor migrated,
  `GraphicsDeviceManager` SDL-free. PLAT-47/50/51 blocked, see §5.
- **Phase 4** (renderers) — not started. 20 tasks, 46 identities. See §6 for why most cannot be
  built here.
- **Phase 5** (input) — four backends deleted, the scancode and keycode vocabularies defined.
  PLAT-78 blocked on four contract gaps, see §5.
- **Phase 6** (audio) — not started.
- **Phase 7** (services) — clipboard, power, locale, system info, URL, dialogs done.
- **Phase 8** (headless + conformance) — done except PLAT-118.
- **Phase 9** (gates, perf, docs) — not started.
- **Phase 10** (terminal) — **9 of 13 done**: PLAT-129 (spike), 130 (skeleton + selection), 131
  (session lifecycle + restoration), 132 (surface presenter), 133 (damage tracking), 134 (colour
  ladder, landed with 132), 135 (frame budget), 136 (`SIGWINCH`), 137 (exact Kitty keyboard).
  **Remaining: PLAT-138, 139, 140, 141** — see §7. The
  conformance suite already runs green against `Terminal` as a third implementation, so PLAT-141's
  claim holds for the surface that exists today; the row stays open until the capabilities it is
  meant to cover are actually turned on.

---

## 4. Technical discoveries worth not rediscovering

**A namespace and an enum shared a name.** `CNA::Platform` existed as *both* the build-target enum
(`modules/core/include/CNA/Platform.hpp`) and the new namespace. That is ill-formed — any TU
including both headers failed with "redeclared as different kind of entity" — and the build was
green only because no file happened to include both. Renamed to `CNA::TargetPlatform`;
`modules/platform/tests/CNA/Platform/NamespaceCollisionTests.cpp` is a TU that includes both and
fails to compile if it ever recurs.

**`HeadlessRenderer` advertised MRT and then refused it.** `SupportsCapability` answers `true` by
default, so it reported `MultipleRenderTargets` as available while `SetRenderTargets` threw for
any count above one. Two tests asserted the two halves and both "passed", in different files.
Fixed in the renderer (headless rasterises nothing for one target either, and it is the renderer
the suite runs on). The guard is
`GraphicsDeviceCapabilityTest.TheMultipleRenderTargetCapabilityMatchesWhatBindingActuallyDoes`,
which ties capability to behaviour for *every* renderer this suite builds against.

**The contract promised vocabularies it did not have.** `KeyEvent::scancode` was documented as
"CNA's own value space" and `keycode` as "matching `Keys`" — both were raw `uint32_t` carrying
SDL's values through. Now `CNA::Platform::Scancode` (USB HID usage IDs) and
`CNA::Platform::KeyCode` (Windows Virtual-Key codes). Both adopt published standards rather than
inventing numbering. `KeyboardSnapshot::modifiers` had the same defect and is now `KeyModifier`.

**`modules/input` cannot be included from `modules/platform`.** The dependency runs one way, which
is why `KeyCode` is a separate enum rather than a reference to XNA's `Keys`. Their value-for-value
correspondence is verified by `KeyCodeMatchesXnaKeysTests` in `modules/input` — the only layer that
can see both types.

**A test for a crash path has to be able to fail, and proving that takes its own mode.**
`TerminalRestorationTests` spawns a harness that dies six different ways under a pseudo-terminal
the test owns. Five ways must leave the terminal restored — but *every* assertion in the file
would also pass if the pty quietly reset itself between spawns, or if the echo check could never
read false. So the harness has a sixth mode, `leak`, which takes the terminal over and calls
`_exit()`: no destructor, no `atexit`, no signal handler, genuinely unrecoverable. Its assertions
are the inverse of the others'. That mode is what makes the other five mean something.

**A pseudo-terminal's buffer is a few kilobytes, and a full truecolor frame is tens.** A test that
presents a frame to a pty nobody is reading does not fail — it **deadlocks**, because the write
waits for a reader that is the same thread. `PseudoTerminalDrain` in
`modules/platform/tests/CNA/Platform/PseudoTerminalHarness.hpp` exists for that and nothing else.
Any new terminal test that presents a full frame needs one.

**A median beat an exponential average, and the tests are what showed it.** The frame budget has
two demands that pull against each other: one slow write must not throttle a fast terminal, and a
link that really degraded must be believed within a few frames. An EMA has one weight controlling
both, and the first honest attempt failed its own convergence test — 100 MB/s smoothed toward
10 KB/s over ten frames was still reporting 5.6 MB/s. A median of the last five ignores an outlier
entirely and follows a sustained change as soon as it fills half the window.

**Twenty-one conformance tests matched no ctest filter and had never run.** `PlatformWindowConformance`
— the window half of PLAT-116's suite — was invisible to `CnaPlatformTests`, whose filter token
`*PlatformConformance*` does **not** match the string `PlatformWindowConformance`. Found at
PLAT-130 by running the registered filter and grepping its `--gtest_list_tests` output, which is
the only way to see this: a test that is never selected reports nothing, and the suite is green
either way. It now runs under `CnaPlatformWindowTests`, where the dummy video driver is already
set — otherwise the SDL3 parameterisation would skip for want of a display and only the
implementations that need no display would have been exercised.

**Pointer identity is not a service identity.** An "already started" cache in
`CNA::Input::Sensors` keyed on the service address broke when a destroyed platform's address was
reused. Passed alone, failed in the suite. Static accessors have no lifetime to hang state on.

**A lossy round trip makes a sound-looking test wrong.** `KeyCodeTableEquivalenceTests` compared
two tables via `SDL_GetKeyName`/`GetKeyFromName`. That naming is many-to-one — `SDLK_RETURN2` is
named "Return" — so it reported failures where the tables agreed exactly (verified: 129 cases
each, zero difference). The round trip is now checked before it is trusted.

---

## 5. Blocked tasks (`needs_human` where noted)

| Task | Blocked on | Why |
|---|---|---|
| PLAT-47 | PLAT-78 | `Game::PollEvents` hands every raw `SDL_Event` to the bridge before its own switch. Polling both queues would drain events twice. |
| PLAT-50, PLAT-51 | Phase 4 | `GameWindow` wraps the `SDL_Window*` the **renderer** hands it. Until `IGraphicsRenderer` produces an `IPlatformWindow*` there is nothing to hold. |
| PLAT-77f | PLAT-78 | `GetModStateEXT` is a live query while `KeyboardSnapshot` is per-frame; re-pointing only it would put keyboard state on two clocks. |
| PLAT-102 | PLAT-50 | `DisplayInfo` takes a `GameWindow&` and calls `GetNativeSdlWindowEXT()`. Also needs a safe-area concept the contract lacks. |
| PLAT-78 | PLAT-78c–f | Four gaps between what the bridge consumes and what `PlatformEvent` carries. |

**PLAT-78c–f, in order of severity:**

1. **PLAT-78c — total loss.** `SDL_EVENT_TEXT_EDITING_CANDIDATES` has no `PlatformEvent`
   alternative *and* no case in `MapSdlEvent`.
   `TextInputEXT::INTERNAL_OnTextEditingCandidates` would become unreachable and IME candidate UI
   would stop appearing for CJK input.
2. **PLAT-78d — field gap.** `TouchEvent` has no `dx`/`dy`; the bridge reads `tfinger.dx/dy` on
   motion and feeds `TouchPanel`. Every motion would report a zero delta.
3. **PLAT-78e — four silent numeric divergences.** Negative-axis divisor (the bridge uses 32767
   and carries a comment that 32768 "diverged from FNA at every non-endpoint negative sample"; the
   mapper does exactly the rejected thing), thumbstick Y inversion, trigger clamping range, and
   raw-vs-translated axis/button indices.
4. **PLAT-78f — behaviour change, not a gap.** The bridge never reads `wheel.direction`; the
   mapper negates on `SDL_MOUSEWHEEL_FLIPPED`. Migrating would reverse scrolling on flipped-wheel
   systems.

---

## 6. Environment limits (affects what can be validated here)

- **The FNA reference tree is absent.** `CLAUDE.md` names
  `/rv/data/library/github.com/FNA-XNA/FNA` authoritative; it does not exist in this environment.
  Behavioural-fidelity questions are therefore answered from in-repo evidence (several source
  comments record FNA-verified conclusions), and anything with no such evidence is marked
  `needs_human` rather than guessed.
- **Only `sharp-runtime` is a sibling repository.** `easy-gl` and `free-direct` are absent, so the
  five EasyGL GL profiles and `FREEDIRECT` cannot configure.
- **Vendored third-party is only** SDL, SDL_image, SDL_mixer, cgltf, enet, stb. Vulkan, DirectX,
  Magnum, Skia, bgfx, WickedEngine, Diligent, LLGL, FNA3D, wgpu-native, Blend2D, ShivaVG and
  PortableGL are all unavailable, so most of Phase 4's renderer families cannot be compiled here.
- No GPU and no display server; `SDL_VIDEODRIVER=dummy` is the only usable video driver.

---

## 7. Immediate next steps

1. **Phase 10 — `TerminalPlatform`**, continuing at **PLAT-138**. Nine of thirteen rows are done
   (129–137); four remain: **138, 139, 140, 141**.

   **What already exists, and must be used rather than re-derived.** Everything is under
   `modules/platform/src/Terminal/`:

   | File | What it does |
   |---|---|
   | `TerminalCapabilityProbe.*` | Detects tty-ness, colour depth and the **Kitty keyboard protocol**. Takes its descriptors as parameters so it is testable under a pty. `hasKittyKeyboard` is the flag PLAT-137 vs PLAT-138 turns on. |
   | `TerminalSession.*`, `TerminalSessionController.*` | The session takes raw mode, alternate screen, hidden cursor, Kitty keyboard and optional SGR-1006 mouse, then gives all of it back on **every** exit path. The controller shares the one process session through per-use RAII leases; PLAT-138 reuses the keyboard lease without Kitty flags, and PLAT-139 acquires the already-defined mouse lease. |
   | `TerminalKeyboard.*` | Exact canonical Kitty decoder, held-key snapshot and shared event queue. PLAT-138 should extend this decoder with a legacy/synthetic mode rather than create a second reader for the same descriptor. |
   | `TerminalFrameGrid.*` | RGBA → glyph grid. `TerminalCell` has `operator==` for the diff. |
   | `TerminalAnsiWriter.*` | Grid → ANSI, all four colour rungs, full frame and diff. |
   | `TerminalFrameBudget.*` | Median-of-five measured throughput; drops frames rather than blocking. |
   | `TerminalResizeWatcher.*` | `SIGWINCH` → a flag → a `WindowEvent` at poll time. |
   | `TerminalSurfacePresenter.*` | Ties them together and holds the presentation lease on the shared session. |

   **The structural decision is now implemented.** `TerminalSessionController` stays dormant at
   platform construction and owns the process's single session only while a presenter, keyboard
   or mouse lease exists. Changing the set of users rebuilds the immutable signal-safe restoration
   record with the union of their modes; the presenter watches a generation counter and forces a
   full redraw if that rebuild invalidated the alternate screen. PLAT-138 and PLAT-139 must add to
   this controller/decoder path, not open a parallel session or race a second reader against it.

   **Testing terminal code in CI.** This environment has no terminal: output is redirected, so
   `isatty` is false and every interesting path refuses. Two mechanisms already exist and both
   should be reused:
   - `PseudoTerminalHarness.hpp` (in `modules/platform/tests/CNA/Platform/`, on `CnaTests`'
     include path) — `PseudoTerminal`, `PseudoTerminalDrain`, `RunUnderPseudoTerminal`.
   - Spawned harnesses under `tools/platform/` for anything that insists on the process's own
     standard descriptors, registered in `cmake/Harnesses.cmake` and given to `CnaTests` as a
     compile definition in `cmake/UnitTests.cmake`. **Adding one of those definitions rebuilds
     every test TU**, so expect a long build.

   A test that skips in CI is a test that never runs. This campaign has already been bitten by
   exactly that once, and both terminal harnesses exist to avoid repeating it.

   **Remember to add new suite names to the `CnaPlatformTests` gtest filter** in
   `cmake/UnitTests.cmake` — a suite absent from that filter is never run by ctest, silently.

2. **Phase 4, narrow slice.** PLAT-57's written decision, then PLAT-59/60/61 — the
   `IGraphicsRenderer` interface changes that the PLAT-3 audit says free STUB/HEADLESS/SOFTWARE/
   PORTABLEGL with no per-renderer work. Anything needing an absent dependency is marked
   blocked-on-toolchain with the missing library named.
3. **PLAT-78c–f** where in-repo evidence decides the answer.

---

## 8. Standing conventions for this campaign

- One task, one commit; stage by explicit filename (never `git add -A` across the tree).
- Every new contract header must be added to `ContractIsSdlFreeTests.cpp` — `check_contract.py`
  fails otherwise, by design.
- A capability is `false` **only** when the corresponding service is null and the call refuses
  deterministically. The conformance suite enforces the pairing.
- Gates are verified against a deliberately introduced regression before being trusted, then the
  probe is removed.
