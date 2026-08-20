# NEXT_platform.md — SDL3/CNA platform separation

> Continuity document for the platform-abstraction campaign. The authoritative task list is
> **`plans/plan_platform.md`** (PLAT-1…PLAT-141 plus lettered follow-ups); this file records state,
> discoveries and the next starting point. The design note that started it is `cnaplatform.md`.
>
> This file exists separately from the root `NEXT.md` following the repository's own convention
> for subsystem campaigns (`NEXT_skia.md`, `NEXTinput.md`, `NEXTaudio.md`, `NEXT_gdi.md`, …).
> **`NEXT.md` had no record of this campaign at all** until this file was added and cross-linked.

**Branch:** `feature/platform`
**Last updated:** 2026-08-13

---

## 0. Current checkpoint (supersedes stale counts below)

**2026-08-17 — post-merge re-audit.** Read this before the 2026-08-13 checkpoint underneath it.

The ledger is now **163 decisions: 162 implemented, PLAT-45 deliberately cut** — the original 155
(141 numeric IDs plus 14 lettered follow-ups) and Phase 11's 8 `PLAT-SDL2-n` rows, which are all
closed. Every SDL2 task carries real evidence; none is 🟨 or ⏳ any more.

**The 2026-08-13 record was true of the tree it was written against and had stopped being true.**
Work merged afterwards reopened three guarantees with no row's status changing:

1. `PIXIJS` arrived constructing from `SDL_Window*`, overriding three methods PLAT-59/60 had
   deleted (so it did not compile), with no descriptor and no registry entry (so
   `-DCNA_GRAPHICS_RENDERER=PIXIJS` was a hard CMake error) — and the response had been to run
   `sdl_ratchet.py --update --allow-increase`, taking PLAT-121's floor from 0/0 to 4 files / 14
   references. Now migrated onto `GraphicsRendererCreateArgs`/`RendererSurfaceInfo`, linking no
   windowing library, and the floor is **data** in `sdl_budget.json` that `--allow-increase` cannot
   override.
2. `CNA_AUDIO_PLATFORM=SDL2` and `=NULL` could not build `cna_audio` at all.
3. The CI gate step ran five of seven gates, and ran the ratchet **without** `--strict`.

Four more, each found by building or running something for the first time: `CnaTests` linked both
SDL generations in an SDL2 build; SDL2 advertised `multipleDisplays` with a null service; SDL2
mistranslated every navigation/editing/keypad key (one range test spanned F1..F24 across a scancode
gap those keys sit inside); and 32 sound-engine assertions had never run outside the SDL3 profile.

**Current verification, all on an Xvfb display rather than the dummy video driver:**

| Profile | Full non-network run | Registered platform CTest |
|---|---:|---:|
| SDL2 + SDL2 audio + OPENGLES3 (`cmake-build-sdl2`) | **6,680 passed / 70 skipped / 0 failed** | **4/4** (incl. `CnaSdl2PlatformTests`, `CnaSdl2AudioDeviceTests`) |

SDL2 is now a real parameter of the implementation-neutral suite: **87/87** conformance instances
over SDL2, Headless and Terminal. `cna_demo_2d --smoke 6` runs to completion on an SDL2-created
GL ES 3.2 context. All seven gates pass, ratchet strict at **0/0**.

**The lesson, because it will recur:** a gate only covers the configurations somebody builds and
runs. Three of these survived a "completed and re-audited" review because the configuration that
exposes them had never been executed end to end.

### 2026-08-13 checkpoint (historical)

All 155 ledger decisions have a terminal status: **154 implemented and PLAT-45 deliberately cut**.
A post-completion source and behavioural audit on 2026-08-13 corrected capability refusals,
fullscreen semantics, surface-frame validation, SDL graphics global-state handling and several
exception-safety gaps. The strict production ratchet is now **0 files / 0 references** outside the
four renderer allowlist entries.

The final serial non-network runs are SDL3 **6,553 passed / 55 skipped**, HEADLESS **6,367 / 49**
and TERMINAL **6,349 / 56**, all with zero failures. Both registered platform CTest entries pass in
all three profiles. The five ENet/UDP suites remain excluded because this execution sandbox denies
their socket operations; see the completion record in `plans/plan_platform.md`.

---

## 1. READ THIS FIRST — the build-configuration trap

There are **three current** build directories and they are not interchangeable. A change can
compile and pass in two of them while not being compiled *at all* in the third.

| Directory | Configure | Covers |
|---|---|---|
| `cmake-build-debug` | default (`CNA_PLATFORM=SDL3`, `CNA_GRAPHICS_RENDERER=HEADLESS`) | the SDL3 platform implementation |
| `cmake-build-headless` | `-DCNA_PLATFORM=HEADLESS` | the second platform implementation; the conformance suite's other arm |
| `cmake-build-terminal` | `-DCNA_PLATFORM=TERMINAL -DCNA_GRAPHICS_RENDERER=HEADLESS -DCNA_AUDIO_PLATFORM=NULL` | terminal selection plus SDL-free silent playback |

There is currently **no `cmake-build-devices` directory**. To avoid another multi-gigabyte build
tree, devices changes are checked by temporarily configuring the existing `cmake-build-debug`
with `-DCNA_DEVICES=ON`, building/testing it, and then configuring it back to
`-DCNA_DEVICES=OFF` and rebuilding. Do not leave that cache in the temporary state.

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
cmake --build cmake-build-debug    --target CnaTests -j4 && ./cmake-build-debug/Debug/bin/CnaTests
cmake --build cmake-build-headless --target CnaTests -j4 && ./cmake-build-headless/CnaTests
cmake --build cmake-build-terminal --target CnaTests -j4 && ./cmake-build-terminal/CnaTests
cmake -S . -B cmake-build-debug -DCNA_DEVICES=ON
cmake --build cmake-build-debug --target CnaTests -j4 && ./cmake-build-debug/CnaTests
cmake -S . -B cmake-build-debug -DCNA_DEVICES=OFF
cmake --build cmake-build-debug --target CnaTests -j4

cd cmake-build-debug && ctest -R CnaPlatform --output-on-failure   # SDL_VIDEODRIVER=dummy suites

python3 tools/platform/check_contract.py      # probe completeness + Doxygen coverage
python3 tools/platform/sdl_ratchet.py         # remaining SDL coupling vs budget
python3 tools/platform/hot_path_lint.py       # design decision 4
python3 tools/platform/sdl_inventory.py --check
```

**Do not treat a full suite under `SDL_VIDEODRIVER=dummy` as the display-backed gate.** PLAT-81
made every `MouseCursor` test display-independent, but genuine SDL window/event tests still need
real video or their registered, deliberately scoped dummy-driver suites.

---

## 2. Validation status

| Variant | Result |
|---|---|
| `cmake-build-debug` | **6,553 passed / 55 skipped / 0 failed** (five network suites excluded) |
| `cmake-build-headless` | **6,367 passed / 49 skipped / 0 failed** (five network suites excluded) |
| historical dedicated devices run | **6471 passed, 0 failed**; the current workflow uses the temporary debug-cache toggle above |
| `cmake-build-terminal` | **6,349 passed / 56 skipped / 0 failed** (five network suites excluded) |

PLAT-141's final conformance run mechanically lists **29 cases per implementation** (19 general +
10 window). The default build runs all three implementations (87 instances), while the HEADLESS-
and TERMINAL-selected builds each run Headless + Terminal (58 instances). The registered
`CnaPlatformWindowTests` + `CnaPlatformTests` also pass; in a restricted sandbox set
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

Ratchet: **0 files / 0 references** of direct SDL coupling outside the PLAT-3 allowlist, down from
the 253 / 3641 baseline. Contract: **28 headers, 623 documented declarations**, all SDL-free.

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

**127 ✅ · 3 🟨 · 24 ⬜ · 0 ⛔ · 1 ❌** across `plans/plan_platform.md` — **82 %** of the 155
task rows complete.

- **Phase 0** (inventory, gates, baselines) — done except PLAT-7 (performance baseline).
- **Phase 1** (the contract) — done. 27 headers under `modules/platform/include/CNA/Platform/`.
- **Phase 2** (SDL3 implementation) — largely done.
- **Phase 3** (runtime) — `Game` owns the platform; timing, cursor and the event loop are migrated;
  `GraphicsDeviceManager` is SDL-free, and PLAT-55's registered golden oracle passes through the
  new path. PLAT-50 delegates `GameWindow` behavior to the platform window, PLAT-102 removed its
  last SDL-native consumer, and PLAT-51 replaced the public SDL escape hatch with
  `NativeWindowHandle` without an alias.
- **Phase 4** (renderers) — PLAT-57's boundary decision, PLAT-58/59/60/61's common-interface
  cleanup, PLAT-62's platform-owned `GraphicsDevice` window and PLAT-63's display-service-backed
  `GraphicsAdapter` are complete. PLAT-64 removed native image operations from
  `Texture2D`/`TextureCube`; PLAT-65 replaced the final SDL3_image backend with the already-vendored
  stb decoder/encoder and a CNA bilinear scaler. The stale raw-window transition was also closed:
  `GraphicsDevice` now carries only `IPlatformWindow`, creation requirements are typed contract
  values, and `cna_graphics_core` has no direct SDL/SDL3_image link. PLAT-66 made platform
  client-coordinate↔logical-game conversion a `WindowId`-registry contract and removed native
  window resolution from mouse/touch conversion. PLAT-67 then migrated all five EasyGL identities
  onto `IPlatformGlContext` plus `RendererSurfaceInfo`; the family is now completely SDL-free.
  Continue at PLAT-68.
  See §6 for why most cannot be built here.
- **Phase 5** (input) — five redundant backends are deleted; `Keyboard`, `Mouse` and `MouseCursor`
  now consume typed platform services. Cursor creation, including custom RGBA images, is owned by
  the selected platform and no SDL cursor type remains in the public input API.
  `PlatformInputBridge` consumes the complete event vocabulary in the production path.
- **Phase 6** (audio) — **all nine tasks complete**: independent playback/recording contracts,
  orthogonal selection, SDL3 devices, contract-driven `AudioMixer` output, SDL-free XNA
  sound-effect / queued-stream / microphone ownership, platform-neutral WaveBank/XACT file IO,
  and a paced SDL-free `NullAudioDevice` exercised by the same conformance suite as SDL3.
- **Phase 7** (services) — clipboard, power, locale, system info, URL, dialogs done.
- **Phase 8** (headless + conformance) — done except PLAT-118.
- **Phase 9** (gates, perf, docs) — not started.
- **Phase 10** (terminal) — **all 13 done**: PLAT-129 (spike), 130 (skeleton + selection), 131
  (session lifecycle + restoration), 132 (surface presenter), 133 (damage tracking), 134 (colour
  ladder, landed with 132), 135 (frame budget), 136 (`SIGWINCH`), 137 (exact Kitty keyboard),
  138 (synthetic keyboard fallback), 139 (SGR-1006 mouse), 140 (capability/refusal profile), 141
  (full conformance closure). The phase is complete.

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
| *(none)* | — | No implementation task is currently blocked; PLAT-102 unblocked PLAT-51. |

---

## 6. Environment limits (affects what can be validated here)

- **The FNA reference tree is absent.** `CLAUDE.md` names
  `/rv/data/library/github.com/FNA-XNA/FNA` authoritative; it does not exist in this environment.
  Behavioural-fidelity questions are therefore answered from in-repo evidence (several source
  comments record FNA-verified conclusions), and anything with no such evidence is marked
  `needs_human` rather than guessed.
- **`sharp-runtime`, `easy-gl` and `meta-gl` are available as sibling repositories.** The three
  native EasyGL identities configure here; OPENGLES2/OPENGLES3 were fully built and tested and
  OPENGL33's production target was built. WEBGL1/WEBGL2 require an Emscripten toolchain, which is
  unavailable. `free-direct` is absent, so `FREEDIRECT` cannot configure.
- **Vendored third-party is only** SDL, SDL_image, SDL_mixer, cgltf, enet, stb. Vulkan, DirectX,
  Magnum, Skia, bgfx, WickedEngine, Diligent, LLGL, FNA3D, wgpu-native, Blend2D, ShivaVG and
  PortableGL are all unavailable, so most of Phase 4's renderer families cannot be compiled here.
- No GPU and no display server; `SDL_VIDEODRIVER=dummy` is the only usable video driver.

---

## 7. Immediate next steps

1. **Migrate the standalone GL profiles (PLAT-68).** Move `OPENGL1`, `OPENGL2`, `OPENGL4` and
   `OPENGLES1` from direct native-toolkit context/window calls onto the `IPlatformGlContext` pattern
   proven by PLAT-67. Preserve each profile's requested version, compatibility/core/ES mode,
   context-recovery behavior, swap interval, resize and coordinate semantics.

2. **Completed Phase 6 reference.** PLAT-91…99 now provide independent SDL-free playback and
   recording contracts, orthogonal `SDL3`/`NULL` selection, an SDL3 device edge, and a paced
   `NullAudioDevice`. The memory-backed mixer, sound effects, dynamic streams and microphones all
   consume those boundaries; WaveBank/XACT file IO was already standard C++. The parameterised
   production-device suite runs SDL3 + NULL in the default build and NULL alone in the terminal
   build. NULL playback drives both S16 and F32 mixer output without initializing SDL audio;
   recording remains truthfully unsupported rather than inventing a fake microphone. No Phase 6
   work remains—continue the Phase 4 dependency chain in item 1.

---

## 8. Standing conventions for this campaign

- One task, one commit; stage by explicit filename (never `git add -A` across the tree).
- Every new contract header must be added to `ContractIsSdlFreeTests.cpp` — `check_contract.py`
  fails otherwise, by design.
- A presence capability is `false` **only** when the corresponding service is null and the call
  refuses deterministically. Quality flags are deliberately different: `exactKeyboardState=false`
  and `pixelAccurateMouse=false` describe an available but approximate input service. The
  conformance suite pairs presence capabilities and leaves these two quality boundaries explicit.
- Gates are verified against a deliberately introduced regression before being trusted, then the
  probe is removed.
