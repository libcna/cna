# NEXT_platform.md — SDL3/CNA platform separation

> Continuity document for the platform-abstraction campaign. The authoritative task list is
> **`plan_platform.md`** (PLAT-1…PLAT-141 plus lettered follow-ups); this file records state,
> discoveries and the next starting point. The design note that started it is `cnaplatform.md`.
>
> This file exists separately from the root `NEXT.md` following the repository's own convention
> for subsystem campaigns (`NEXT_skia.md`, `NEXTinput.md`, `NEXTaudio.md`, `NEXT_gdi.md`, …).
> **`NEXT.md` had no record of this campaign at all** until this file was added and cross-linked.

**Branch:** `claude/cna-platform-sdl3-separation-pxuc33`
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
| `cmake-build-debug` | **5703 passed, 0 failed** |
| `cmake-build-headless` | **5596 passed, 0 failed** |
| `cmake-build-devices` | **6412 passed, 0 failed** |

Ratchet: **225 files / 3423 references** of direct SDL coupling outside the PLAT-3 allowlist, down
from the 253 / 3641 baseline. Contract: 24 headers, 383 documented declarations, all SDL-free.

There are currently **no known failing tests**. The long-standing
`GraphicsDeviceValidationTest.SetRenderTargets_FourTargets_DoesNotThrow` failure was fixed —
see §4.

---

## 3. Where the campaign stands

71 ✅ · 12 🟨 · 66 ⬜ · 5 ⛔ · 1 ❌ across `plan_platform.md`.

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
- **Phase 10** (terminal) — not started; unblocked now that Phase 8 is done.

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

1. **Phase 10 — `TerminalPlatform`** (PLAT-129…141). Unblocked and buildable. Its value beyond the
   feature: a second genuinely different implementation stresses the contract far harder than
   `HeadlessPlatform`, which implements everything by doing nothing. Every capability gap found so
   far came from making a real caller work.
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
