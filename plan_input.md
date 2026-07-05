# plan_input.md — CNA Input Audit & Task Plan

> **Status of this document:** Actionable task plan produced from a fresh, code-first audit of the
> Input layer on branch `feature/input` (2026-07-05). Docs were treated as clues, not proof; every
> claim below was cross-checked against source, tests, and — where noted — a real build/test run in
> this checkout. This file replaces the previously-deleted `plan_input.md` that the other input docs
> still reference by name.
>
> **Do not implement tasks from this file in the same pass that reads it.** This is a plan. Pick a
> task, do it, verify it, commit it, then update this file's status column.

---

## 1. Title and scope

**Scope.** This plan covers the CNA Input subsystem end to end:

- `Microsoft::Xna::Framework::Input` (Keyboard, Mouse, GamePad and their state/capability structs, enums).
- `Microsoft::Xna::Framework::Input::Touch` (TouchPanel, TouchCollection, TouchLocation, gestures).
- FNA-style extensions carried inside the XNA namespace but marked `EXT`/`NOXNA` (e.g. `GetGUIDEXT`,
  `SetLightBarEXT`, `TextInputEXT`, gamepad sensors, paddles).
- CNA/MonoGame-derived extensions (`MouseCursor`).
- Internal input plumbing: `CNA::Internal::Input::SdlInputBridge`, `InputManager`, `GestureDetector`,
  `SdlGamepadBackend` / `ISdlGamepadBackend` and the test fake.
- SDL3 event bridging, the injectable gamepad seam, tests, docs, CI, and manual hardware verification.

**Out of scope.** Audio, Graphics, Content, and the separate "Devices" (`Microsoft::Devices::Sensors`)
track are **not** covered here, **except** where Input legitimately touches windowing/platform systems
(window handle plumbing, focus events, high-DPI/logical-coordinate conversion, cursor creation needing
SDL video). The known Graphics bugs (Vulkan DepthStencil / Clear / ReferenceStencil, tracked elsewhere)
are explicitly not this plan's concern.

---

## 2. Current status summary

### 2.1 What exists today (verified from code)

The public XNA 4.0 Input API surface is **present and appears complete** at the type level. All expected
types exist as headers under `include/Microsoft/Xna/Framework/Input/**` (26 headers) with matching
sources for the non-enum types. No method is a stub or throws "not implemented"; EXT gamepad methods
delegate to `SdlInputBridge`. The internal layer (bridge, manager, gesture detector, gamepad seam) is
implemented and event-driven.

### 2.2 Layer classification (verified)

| Layer | Examples | State |
|---|---|---|
| **Strict XNA 4.0 API** | `ButtonState`, `Buttons` (core bits), `GamePad(.GetState/GetCapabilities/SetVibration)`, `GamePadState/Buttons/DPad/ThumbSticks/Triggers/Capabilities`, `GamePadType`, `GamePadDeadZone`, `KeyState`, `Keys`, `Keyboard`, `KeyboardState`, `Mouse`, `MouseState`, `TouchPanel`, `TouchPanelCapabilities`, `TouchCollection`, `TouchLocation`, `TouchLocationState`, `GestureSample`, `GestureType` | Implemented; member-level parity **needs a formal matrix** (§5). |
| **FNA-compatible extensions** (`EXT`/`NOXNA`) | `GamePad::GetGUIDEXT/SetLightBarEXT/SetTriggerVibrationEXT/GetGyroEXT/GetAccelerometerEXT`, `Buttons::{Misc1,Paddle1..4,TouchPad}EXT`, `GamePadCapabilities::Has*EXT`, `Keyboard::GetKeyFromScancodeEXT`, `Mouse::ClickedEXT/IsRelativeMouseModeEXT`, `TextInputEXT`, `GestureSample::FingerId(2)EXT` | Implemented; marked. |
| **CNA/MonoGame extensions** (`NOXNA`) | `MouseCursor` (entire class) | Implemented; leaks SDL types into its public header (§6). |
| **Implementation fidelity** | Ported from FNA with cited source lines | Documented deviations exist (§18); a few remain `not asserted`/`incomplete`. |
| **Test coverage** | GoogleTest under `tests/**/Input/**` | Broad; specific gaps enumerated in §15. |
| **Hardware/manual verification** | `docs/input-manual-verification-results.md` | One dated entry (2026-07-04), **stale** (pre-Phase-I15, "no controller available"). |

### 2.3 Confidence levels

- Type-level API completeness: **High** (enumerated directly from headers).
- Member-level XNA numeric/behavior parity: **High for enum ABI** — as of 2026-07-05 all 8 public Input
  enums are exhaustively value-pinned against FNA (`Keys` 160/160, `Buttons` 31/31, `GamePadType` 10/10,
  `GamePadDeadZone` 3/3, `ButtonState` 2/2, `KeyState` 2/2, `TouchLocationState` 4/4, `GestureType` 11/11;
  INPUT-KBD-001 + INPUT-TEST-001, guarded by INPUT-API-034). **Medium** remains only for struct/method
  behavioral parity not yet captured by the formal member matrix (INPUT-API-027).
- Internal bridge behavior: **High** for what is unit-tested via the fake backend and synthetic events;
  **Low** for real-hardware actuation (rumble, sensors, LED, hotplug, real GUID) — none is headless-verifiable.
- Docs accuracy: **Low** — multiple mutually-contradictory test counts, a stale verification log, a
  `MaximumTouchCount` contradiction, and dozens of dangling `plan_input.md` task references.
- CI: **None exists** (verified: no `.github/`, no `*.yml` outside submodules).

---

## 3. Build and reproducibility status

### 3.1 What was actually run in this checkout (2026-07-05, branch `feature/input`)

- **Backend:** EasyGL. Toolchain present: g++/CMake/Ninja on Debian 13.
- **Build:** `cmake --build cmake-build-input-easygl --target CnaTests` → **succeeded** (no work / clean link).
- **Full suite:** `./cmake-build-input-easygl/CnaTests` → **3248 passed, 2 skipped**
  (`AccelerometerTests.GetCurrentValuePropertyDoesNotThrowWhenSupported`,
  `GyroscopeTests.GetCurrentValuePropertyDoesNotThrowWhenSupported` — Devices track, sensor-gated).
- **Input filter** (the NEXT.md filter) → **259 passed across 33 suites**.
- **Order independence:** input filter under `--gtest_shuffle --gtest_repeat=3` → **259/259 each iteration**.
- **Also run (2026-07-05, INPUT-BUILD-002):** Vulkan, bgfx, and SDL_RENDERER each built `CnaTests` clean
  and passed the input filter **259/259**, order-independent under shuffle×3 → **all 4 backends green**.
- **Still NOT run this session:** any sanitizer build; any real-hardware verification; a true fresh-clone
  build. These remain open gaps, not confirmed-good.

> **Doc reconciliation:** every existing doc's headline counts (NEXT: 2234/257; manual-verification:
> 1964/217; input-backend: 165) are **stale**. The current true numbers are **3248 full / 259 input**.

### 3.2 Build & reproducibility tasks

#### INPUT-BUILD-001 — Fresh-clone reproducibility proof (EasyGL)
- **Priority:** P0 · **Status:** TODO · **Area:** Build
- **Files:** `cmake/ThirdPartySDL.cmake`, `CMakeLists.txt`, `docs/input-build-and-test.md`
- **Problem:** No evidence a *fresh* clone (no `.sdl-prebuilt`, no warm build dir) configures/builds/tests
  without undeclared local state. Docs assert it but nothing proves it.
- **Work:** In a throwaway clone (siblings `sharp-runtime`, `easy-gl` present), run submodule init +
  configure + build `CnaTests` + run input filter. Capture exact commands and timings.
- **Acceptance:** Documented transcript from a clean clone; input filter green; no manual patching needed.
- **Tests:** input filter, full suite.
- **Deps:** none.

#### INPUT-BUILD-002 — 3-backend build+test matrix (Vulkan, bgfx, SDL_RENDERER)
- **Priority:** P0 · **Status:** DONE (2026-07-05) · **Area:** Build
- **Files:** build only; likely-suspect code `SdlGamepadBackend.*`, `SdlInputBridge.cpp`
- **Problem:** Only EasyGL was built/tested on this branch; input is backend-agnostic but this is unproven
  post I13/I14/I15.
- **Work:** Configure+build+run input filter for `-DCNA_GRAPHICS_BACKEND=VULKAN`, `BGFX`,
  `SDL_RENDERER`. Record counts per backend.
- **Acceptance:** Each backend builds; input filter green on each; counts recorded with date/backend/OS.
- **Tests:** input filter per backend, plus `--gtest_shuffle --gtest_repeat=5`.
- **Deps:** INPUT-BUILD-001.
- **Result (2026-07-05, Debian 13):** Fresh build dirs `cmake-build-input-{vulkan,bgfx,sdlrenderer}`.
  All three configured + built `CnaTests` clean (0 build errors) and passed the input filter **259/259**,
  order-independent under `--gtest_shuffle --gtest_repeat=3` (0 failures). Combined with EasyGL (259) →
  **all 4 backends green**; input confirmed backend-agnostic. Still open: sanitizer build (INPUT-BUILD-006)
  and real-hardware verification (INPUT-GAMEPAD-035). Fresh-clone repro (INPUT-BUILD-001) still pending.

#### INPUT-BUILD-003 — Split an `InputTests` filter/label so input is runnable without a fragile string
- **Priority:** P2 · **Status:** TODO · **Area:** Build
- **Files:** `CMakeLists.txt` (test registration), `docs/input-build-and-test.md`
- **Problem:** Input tests are only selectable via a long, drift-prone `--gtest_filter`; the docs print
  three different filter strings. There is no CTest label or dedicated target. Concrete proof of the drift
  risk (2026-07-05): the new `ButtonStateTests`/`KeyStateTests`/`ButtonsTests` (INPUT-TEST-001) silently
  fall outside the current tokens — the interim filter must add `*ButtonState*:*KeyState*:*Buttons*`.
- **Work:** Add a CTest label (e.g. `LABELS input`) via `gtest_discover_tests` properties or a wrapper,
  or a canonical documented filter constant in one place.
- **Acceptance:** `ctest -L input` (or one documented command) runs exactly the input tests; docs point to it.
- **Tests:** the labeled subset equals the intended input set.
- **Deps:** none.

#### INPUT-BUILD-004 — Pin the SDL submodule to an explicit tag
- **Priority:** P1 · **Status:** TODO · **Area:** Build
- **Files:** `.gitmodules`, `third_party/SDL`, `cmake/ThirdPartySDL.cmake`, `docs/input-build-and-test.md`
- **Problem:** `third_party/SDL` tracks upstream `libsdl-org/SDL.git` with no pinned tag/branch; every
  "behavior against SDL3" claim is version-unlocked and non-reproducible.
- **Work:** Pin submodule to a specific SDL3 release tag; record the tag in build docs; note minimum
  SDL3 API version relied upon (SDL3 gamepad/sensor APIs).
- **Acceptance:** `git -C third_party/SDL describe` yields a fixed tag; docs state it; build still passes.
- **Tests:** full build on pinned SDL.
- **Deps:** none.

#### INPUT-BUILD-005 — Actionable error messages for missing submodules & siblings
- **Priority:** P2 · **Status:** TODO · **Area:** Build
- **Files:** `cmake/ThirdPartySDL.cmake`, `CMakeLists.txt`
- **Problem:** FATAL_ERROR messages exist (verified) but were not re-validated after the sibling
  repo path changes; confirm they fire and are correct for SDL/SDL_image/SDL_mixer/googletest/sharp-runtime/easy-gl.
- **Work:** Temporarily hide each dependency; confirm the emitted message names the exact fix command.
- **Acceptance:** Each missing-dependency path prints a correct, copy-pasteable remedy.
- **Tests:** manual configure with each dep missing.
- **Deps:** none.

#### INPUT-BUILD-006 — ASan/UBSan input run
- **Priority:** P1 · **Status:** DONE (2026-07-05) · **Area:** Build
- **Files:** `CMakeLists.txt` (sanitizer option); 5 input `GetHashCode` sources + `Vector2.cpp`; one test.
- **Problem:** No sanitizer run recorded. Input touches raw SDL handles, reinterpret_cast fakes, and
  process-wide statics — prime ASan/UBSan territory.
- **Work:** Add a sanitizer build option; run the input filter under ASan+UBSan.
- **Acceptance:** Input filter passes clean under ASan+UBSan; leaks/UB triaged.
- **Tests:** input filter under sanitizers.
- **Deps:** INPUT-BUILD-001.
- **Result (2026-07-05):** Added a reusable `CNA_SANITIZE` CMake option (e.g. `-DCNA_SANITIZE=address,undefined`;
  instruments CNA + sharp-runtime + tests; vendored SDL/ENet stay prebuilt). Built `cmake-build-input-asan`
  (EasyGL) and ran the input filter: **279/279 pass; ASan (memory) clean**. UBSan found **signed-integer
  overflow in `GetHashCode`** — a real, portable UB (C++ signed overflow is UB; the C# original relied on
  `unchecked` wraparound). Fixed all in-repo occurrences with behavior-preserving unsigned wraparound
  (identical 2's-complement result, matches FNA): `GamePadThumbSticks` (flagged) + latent
  `GamePadTriggers`/`GamePadState`/`MouseState`/`TouchLocation`, plus `Vector2::GetHashCode` (shared
  Framework math, flagged via thumbsticks). Hash tests 152/152 still pass → values unchanged. Re-run:
  **UBSan clean for all in-repo code.**
  - **Cross-repo finding — FIXED (sharp-runtime `90ed3ff`, 2026-07-05):** `TimeSpan` copy-ctor was invoked
    on an invalid-vptr object during static initialization of the global `DateTimeOffset::MinValue/MaxValue/
    UnixEpoch`, which copied the cross-TU globals `DateTime::Min/Max/UnixEpoch` + `TimeSpan::Zero` — a
    static-initialization-order fiasco. Fixed by initializing those `DateTimeOffset` constants from
    self-contained temporaries (constexpr tick constants + fresh `DateTime`/`TimeSpan`), values unchanged;
    sharp-runtime's own DateTimeOffset/DateTime/TimeSpan suites 274/274 pass. After this, the input filter
    is **fully UBSan-clean (0 runtime-error lines)**.
  - **Follow-up (Framework track):** `Vector3`/`Vector4`/`Color`/other `GetHashCode` likely share the same
    signed-overflow pattern (not exercised by the input filter) — same one-line unsigned fix.
  - Leak detection was run with `detect_leaks=0` (the `MouseCursor` stock singletons + SDL globals leak by
    design at process exit); memory-error + UB detection was full.

#### INPUT-BUILD-007 — TSan review of the "single-thread only" claim
- **Priority:** P2 · **Status:** TODO · **Area:** Build
- **Files:** `InputManager.hpp` (thread-safety note), `docs/input-backend.md` §6
- **Problem:** Input state is intentionally unsynchronized process-wide static state. The single-thread
  invariant is asserted but not enforced or tested.
- **Work:** Either add a debug-only thread-id assertion on mutators, or explicitly document that TSan is
  out of scope and why. If asserting, run input filter under TSan.
- **Acceptance:** Documented decision; if implemented, TSan-clean input filter.
- **Tests:** input filter (+ TSan if chosen).
- **Deps:** none.

#### INPUT-BUILD-008 — Headless run inventory (what silently skips)
- **Priority:** P1 · **Status:** TODO · **Area:** Build/Test
- **Files:** `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`, `TextInputEXTTests.cpp`
- **Problem:** `MouseInputTests.cpp` contains ~14 `GTEST_SKIP` guards firing when SDL video/window/renderer
  is unavailable; in headless CI these silently skip and inflate the "pass" count.
- **Work:** Make the runner emit a machine-readable skip report; count skips per environment; document
  which cursor/relative-mode cases require a display.
- **Acceptance:** A recorded list of exactly which input tests skip headless vs with a display server.
- **Tests:** input filter with and without `SDL_VIDEODRIVER`/`DISPLAY`.
- **Deps:** none.

#### INPUT-BUILD-009 — Repeat/shuffle determinism gate
- **Priority:** P1 · **Status:** TODO · **Area:** Test
- **Files:** none (harness), later CI
- **Problem:** Order-independence is asserted but not gated; a future static-state leak could reintroduce
  order dependence.
- **Work:** Standardize `--gtest_shuffle --gtest_repeat=5` for the input filter as a required check.
- **Acceptance:** Documented command; green on all built backends.
- **Tests:** shuffled repeat run.
- **Deps:** INPUT-BUILD-002.

#### INPUT-BUILD-010 — Record & version the canonical test-count baseline
- **Priority:** P1 · **Status:** DONE (2026-07-05) · **Area:** Test/Docs
- **Files:** `docs/input-build-and-test.md`, this file, `docs/input-manual-verification-results.md`
- **Problem:** Four docs cite four different counts; none matches the real numbers.
- **Work:** Establish one authoritative counts table (date, backend, full-suite count, input-filter count,
  filter string used) and reference it from every doc that currently guesses.
- **Acceptance:** Single source of truth; other docs link to it; matches a re-run.
- **Tests:** full suite + input filter.
- **Deps:** INPUT-BUILD-002.
- **Result (2026-07-05):** Added the authoritative **§Test counts** table to `docs/input-build-and-test.md`
  (full **3269 / 2 skipped**; canonical input filter **280**, base **274**; date + toolchain + the exact
  filter string). Delivered together with INPUT-DOC-001, which points every other doc at it.

---

## 4. Source inventory

Verified file lists (branch `feature/input`, 2026-07-05).

**Public headers** — `include/Microsoft/Xna/Framework/Input/`:
`Buttons.hpp, ButtonState.hpp, GamePad.hpp, GamePadButtons.hpp, GamePadCapabilities.hpp,
GamePadDeadZone.hpp, GamePadDPad.hpp, GamePadState.hpp, GamePadThumbSticks.hpp, GamePadTriggers.hpp,
GamePadType.hpp, Keyboard.hpp, KeyboardState.hpp, Keys.hpp, KeyState.hpp, Mouse.hpp, MouseCursor.hpp,
MouseState.hpp, TextInputEXT.hpp`
and `Input/Touch/`: `GestureSample.hpp, GestureType.hpp, TouchCollection.hpp, TouchLocation.hpp,
TouchLocationState.hpp, TouchPanel.hpp, TouchPanelCapabilities.hpp`.

**Public sources** — `src/Microsoft/Xna/Framework/Input/` (non-enum types only): `GamePad.cpp,
GamePadButtons.cpp, GamePadCapabilities.cpp, GamePadDPad.cpp, GamePadState.cpp, GamePadThumbSticks.cpp,
GamePadTriggers.cpp, Keyboard.cpp, KeyboardState.cpp, Mouse.cpp, MouseCursor.cpp, MouseState.cpp,
TextInputEXT.cpp` and `Touch/`: `GestureSample.cpp, TouchCollection.cpp, TouchLocation.cpp,
TouchPanel.cpp, TouchPanelCapabilities.cpp`. (Enums `ButtonState, KeyState, Keys, GamePadType,
GamePadDeadZone, GestureType, TouchLocationState` are header-only — correct.)

**Internal** — `include|src/CNA/Internal/Input/`: `GestureDetector.*, InputManager.*,
SdlGamepadBackend.*, SdlInputBridge.*`.

**Tests** — `tests/CNA/Internal/Input/`: `FakeSdlGamepadBackend.hpp, GestureDetectorTests.cpp,
InputResetTests.cpp, SdlGamepadBackendTests.cpp, SdlInputBridgeKeyboardTests.cpp,
SdlInputBridgeMouseTests.cpp, SdlInputBridgeTextInputTests.cpp, SdlInputBridgeTouchGestureTests.cpp,
TouchEdgeCaseTests.cpp`; `tests/Microsoft/Xna/Framework/Input/`: `GamePadButtonsTests.cpp,
GamePadInputTests.cpp, GamePadMappingTests.cpp, GamePadStateTests.cpp, GamePadTests.cpp,
GamePadThumbSticksTests.cpp, GamePadTriggersTests.cpp, KeyboardInputTests.cpp, MouseInputTests.cpp,
TextInputEXTTests.cpp, TouchInputTests.cpp`.

**Docs** — `docs/`: `input-fna-fidelity.md, input-backend.md, input-build-and-test.md,
platform-input-notes.md, demo-input-checklist.md, input-manual-verification-results.md,
xna-4-api-coverage.md` (+ `NEXT.md` at root).

**Examples** — `examples/demo_input`, `examples/input_smoke`.

**FNA reference** — `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/**` present (authoritative).

#### INPUT-AUDIT-001 — Keep this source inventory current
- **Priority:** P3 · **Status:** TODO · **Area:** Audit
- **Files:** this file §4
- **Problem:** Inventory drifts as files are added/removed.
- **Work:** Add a checklist item to refresh §4 whenever an Input file is added/removed.
- **Acceptance:** §4 matches `find` output at review time.
- **Tests:** n/a.
- **Deps:** none.

#### INPUT-AUDIT-002 — Detect orphaned/untested Input source files
- **Priority:** P2 · **Status:** TODO · **Area:** Audit
- **Files:** all Input sources + tests
- **Problem:** No automated mapping proves every Input source has a corresponding test file.
- **Work:** Script a source→test coverage cross-check (by type name); list any type with no dedicated test.
- **Acceptance:** A generated table; each gap becomes an `INPUT-TEST-*` task.
- **Tests:** n/a (inspection script).
- **Deps:** none.

#### INPUT-AUDIT-003 — Grep-audit for stray SDL includes in public Input headers
- **Priority:** P1 · **Status:** TODO · **Area:** Audit/API
- **Files:** `include/Microsoft/Xna/Framework/Input/**`
- **Problem:** `MouseCursor.hpp` includes `<SDL3/SDL.h>` and exposes `SDL_Cursor*`/`SDL_SystemCursor`
  (verified). Need to confirm no other public header leaks SDL, and decide MouseCursor's fate (§6/§8).
- **Work:** Grep all public Input headers for `SDL`; produce a definitive leak list.
- **Acceptance:** Leak list = `{MouseCursor.hpp}` (or documented additions); each entry has a decision task.
- **Tests:** a header-hygiene compile test (INPUT-API-030).
- **Deps:** none.

#### INPUT-AUDIT-004 — Reconcile task-number namespace collisions in docs
- **Priority:** P2 · **Status:** TODO · **Area:** Audit/Docs
- **Files:** `docs/input-backend.md` §4, `NEXT.md`
- **Problem:** Tasks 868–872 denote *touch previous-location* in `input-backend.md` but *Graphics
  DepthStencil bugs* in NEXT/Graphics. Same numbers, two meanings.
- **Work:** Re-number or namespace one set (input tasks now use the `INPUT-*` scheme in this file).
- **Acceptance:** No number denotes two unrelated things across docs.
- **Tests:** n/a.
- **Deps:** none.

---

## 5. API completeness matrix

**Goal:** a generated-or-manually-verified matrix proving, per type, that every constructor / public
method / property / static / operator / enum value / flag / default / equality / hash / ToString /
exception / namespace-include / XNA-classification is (a) present and (b) FNA/XNA-faithful, with a test.

The matrix must have one row per member and these columns: *Member · Kind · XNA?/FNA-EXT?/CNA-NOXNA? ·
FNA parity checked? · Test name · Notes*. Below is one verification task per type; each task's Acceptance
is "the type's matrix section is filled and every row has a test or an explicit waiver".

Legend for current per-type test status (from the test audit): COVERED / PARTIAL / NONE.

#### INPUT-API-001 — Matrix: `ButtonState` (enum, XNA) — value test DONE
- **Priority:** P2 · **Status:** DONE (value test, 2026-07-05; INPUT-TEST-001) · **Area:** API/Enum
- **Files:** `include/.../Input/ButtonState.hpp`
- **Problem:** Values `Released=0,Pressed=1` are implicit and only used inline; no dedicated numeric test.
- **Work:** Assert numeric values and underlying type vs FNA; add to matrix.
- **Acceptance:** Test pins `Released==0`, `Pressed==1`.
- **Tests:** new `ButtonStateTests`.
- **Deps:** none.

#### INPUT-API-002 — Matrix: `KeyState` (enum, XNA) — value test DONE
- **Priority:** P2 · **Status:** DONE (value test, 2026-07-05; INPUT-TEST-001) · **Area:** API/Enum
- **Files:** `KeyState.hpp`; existing `KeyboardStateTest.KeyStateValuesMatchXNAConstants`
- **Work:** Confirm `Up=0,Down=1`; ensure a standalone test (not only via KeyboardState).
- **Acceptance:** Matrix row filled; value test present.
- **Tests:** extend/keep `KeyStateValuesMatchXNAConstants`.
- **Deps:** none.

#### INPUT-API-003 — Matrix: `Buttons` (flags enum, XNA+EXT) — value+operator test DONE
- **Priority:** P1 · **Status:** DONE (value+operator test, 2026-07-05; INPUT-TEST-001) · **Area:** API/Enum
- **Files:** `Buttons.hpp`
- **Problem:** All 25 core bits + 6 EXT bits are explicit hex; mapping is tested but numeric values are not
  pinned by a dedicated test, and EXT bits must be distinguished from XNA bits.
- **Work:** Assert every core bit value == FNA; assert EXT bits are the FNA extension values and are
  clearly `EXT`; test `operator| & ~ |= &=`.
- **Acceptance:** Value+operator test; EXT bits flagged in matrix.
- **Tests:** new `ButtonsTests`.
- **Deps:** none.

#### INPUT-API-004 — Matrix: `GamePadType` (enum, XNA) — value test DONE
- **Priority:** P2 · **Status:** DONE (value test, 2026-07-05; INPUT-TEST-001) · **Area:** API/Enum
- **Files:** `GamePadType.hpp`
- **Work:** Pin sequential values 0..9 (`Unknown..BigButtonPad`) vs FNA.
- **Acceptance:** Value test present.
- **Tests:** new `GamePadTypeTests`.
- **Deps:** none.

#### INPUT-API-005 — Matrix: `GamePadDeadZone` (enum, XNA) — value test DONE
- **Priority:** P2 · **Status:** DONE (value test, 2026-07-05; INPUT-TEST-001) · **Area:** API/Enum
- **Files:** `GamePadDeadZone.hpp`
- **Work:** Pin `None=0,IndependentAxes=1,Circular=2`.
- **Acceptance:** Value test present.
- **Tests:** new `GamePadDeadZoneTests`.
- **Deps:** none.

#### INPUT-API-006 — Matrix: `GamePadButtons` (struct, XNA) — COVERED (add ToString check)
- **Priority:** P2 · **Status:** TODO · **Area:** API
- **Files:** `GamePadButtons.hpp/.cpp`
- **Work:** Confirm FNA has no `ToString` override (ValueType default) and record that as the expected
  behavior; verify all 11 getters, both ctors, `FromButtonArray`, `==/!=`, `GetHashCode`.
- **Acceptance:** Matrix filled; ToString expectation documented (no content override).
- **Tests:** existing `GamePadButtonsTest` + ToString expectation.
- **Deps:** none.

#### INPUT-API-007 — Matrix: `GamePadDPad` (struct, XNA) — COVERED
- **Priority:** P3 · **Status:** TODO · **Area:** API
- **Files:** `GamePadDPad.hpp/.cpp`
- **Work:** Fill matrix; confirm hash formula vs FNA; ToString expectation.
- **Acceptance:** Matrix filled.
- **Tests:** existing `GamePadDPadTest`.
- **Deps:** none.

#### INPUT-API-008 — Matrix: `GamePadThumbSticks` (struct, XNA) — COVERED
- **Priority:** P3 · **Status:** TODO · **Area:** API
- **Files:** `GamePadThumbSticks.hpp/.cpp`
- **Work:** Fill matrix; verify deadzone-mode private ctor behavior is exercised via `GamePad`.
- **Acceptance:** Matrix filled.
- **Tests:** existing `GamePadThumbSticksTest`.
- **Deps:** none.

#### INPUT-API-009 — Matrix: `GamePadTriggers` (struct, XNA) — COVERED
- **Priority:** P3 · **Status:** TODO · **Area:** API
- **Files:** `GamePadTriggers.hpp/.cpp`
- **Work:** Fill matrix; confirm epsilon-equality is intentional vs FNA exact compare (record deviation if so).
- **Acceptance:** Matrix filled; equality semantics recorded.
- **Tests:** existing `GamePadTriggersTest`.
- **Deps:** none.

#### INPUT-API-010 — Matrix: `GamePadCapabilities` (struct, XNA+EXT) — PARTIAL
- **Priority:** P2 · **Status:** TODO · **Area:** API
- **Files:** `GamePadCapabilities.hpp/.cpp`
- **Problem:** No `==`/`GetHashCode`/`ToString`; setters are `NOXNA` (FNA `internal set`); 10 EXT props.
- **Work:** Confirm FNA has no equality/ToString on this struct (record as expected); verify every
  getter/setter round-trips; classify each EXT prop.
- **Acceptance:** Matrix filled; absence of equality justified against FNA.
- **Tests:** existing capability tests + per-EXT round-trip.
- **Deps:** none.

#### INPUT-API-011 — Matrix: `GamePadState` (struct, XNA) — COVERED
- **Priority:** P2 · **Status:** TODO · **Area:** API
- **Files:** `GamePadState.hpp/.cpp`
- **Work:** Fill matrix; confirm `ToString` returns fully-qualified type name (FNA ValueType default) — this
  is intentional (record); verify both ctors, `IsButtonDown/Up`, `==/!=` incl. PacketNumber, hash.
- **Acceptance:** Matrix filled; ToString-as-typename recorded as FNA-accurate.
- **Tests:** existing `GamePadStateTest`.
- **Deps:** none.

#### INPUT-API-012 — Matrix: `GamePad` (static class, XNA+EXT) — COVERED
- **Priority:** P2 · **Status:** TODO · **Area:** API
- **Files:** `GamePad.hpp/.cpp`
- **Work:** Fill matrix for `GetCapabilities`, both `GetState` overloads, `SetVibration`; classify EXT statics;
  pin the `NOXNA` deadzone constants (`LeftDeadZone=7849/32768`, `RightDeadZone=8689/32768`,
  `TriggerThreshold=30/255`) vs FNA.
- **Acceptance:** Matrix filled; constants asserted.
- **Tests:** existing `GamePadTest` + a constants test.
- **Deps:** none.

#### INPUT-API-013 — Matrix: `Keys` (enum, XNA) — PARTIAL→verify complete
- **Priority:** P1 · **Status:** TODO · **Area:** API/Enum
- **Files:** `Keys.hpp`; existing `KeysValuesMatchXNANumericConstants`
- **Work:** Confirm the existing test covers ALL enum members' numeric values (not a subset); every value
  matches Windows VK / FNA. Flag any missing standard key.
- **Acceptance:** Every `Keys` member is value-pinned.
- **Tests:** extend `KeysValuesMatchXNANumericConstants`.
- **Deps:** none.

#### INPUT-API-014 — Matrix: `KeyboardState` (struct, XNA) — COVERED
- **Priority:** P2 · **Status:** TODO · **Area:** API
- **Files:** `KeyboardState.hpp/.cpp`
- **Work:** Fill matrix; confirm `ToString` = fully-qualified type name (FNA default) is intentional; verify
  all three ctors, indexer, `IsKeyDown/Up`, `GetPressedKeys` ordering, `==/!=`, hash.
- **Acceptance:** Matrix filled.
- **Tests:** existing `KeyboardStateTest`.
- **Deps:** none.

#### INPUT-API-015 — Matrix: `Keyboard` (static class, XNA+EXT) — COVERED
- **Priority:** P3 · **Status:** TODO · **Area:** API
- **Files:** `Keyboard.hpp/.cpp`
- **Work:** Fill matrix for `GetState()`, `GetState(PlayerIndex)`, `GetKeyFromScancodeEXT` (EXT).
- **Acceptance:** Matrix filled.
- **Tests:** existing `KeyboardInputTest`.
- **Deps:** none.

#### INPUT-API-016 — Matrix: `MouseState` (struct, XNA) — COVERED
- **Priority:** P3 · **Status:** TODO · **Area:** API
- **Files:** `MouseState.hpp/.cpp`
- **Work:** Fill matrix; confirm `ToString` formats content (verify format vs FNA `{X:.. Y:.. Buttons:..}`).
- **Acceptance:** Matrix filled; ToString format matched to FNA.
- **Tests:** existing `MouseStateTest`.
- **Deps:** none.

#### INPUT-API-017 — Matrix: `Mouse` (static class, XNA+EXT+CNA) — COVERED
- **Priority:** P2 · **Status:** TODO · **Area:** API
- **Files:** `Mouse.hpp/.cpp`
- **Work:** Fill matrix; classify `ClickedEXT`, `IsRelativeMouseModeEXT`, `INTERNAL_onClicked`,
  `ResetForTests`, `SetCursor`; confirm `WindowHandle` uses `uintptr_t` (no SDL leak).
- **Acceptance:** Matrix filled; each non-XNA member tagged.
- **Tests:** existing `MouseTest`.
- **Deps:** none.

#### INPUT-API-018 — Matrix: `MouseCursor` (class, CNA-NOXNA) — COVERED behavior, API-hygiene open
- **Priority:** P1 · **Status:** TODO · **Area:** API
- **Files:** `MouseCursor.hpp/.cpp`
- **Problem:** Entire class is MonoGame-derived NOXNA and **leaks SDL types in its public header**.
- **Work:** Fill matrix; cross-link to INPUT-API-030/INPUT-MOUSE-018 (SDL-leak decision).
- **Acceptance:** Matrix filled; SDL-leak flagged with a decision task.
- **Tests:** existing `MouseCursorTest`.
- **Deps:** INPUT-AUDIT-003.

#### INPUT-API-019 — Matrix: `TextInputEXT` (static class, FNA-EXT) — COVERED
- **Priority:** P2 · **Status:** TODO · **Area:** API
- **Files:** `TextInputEXT.hpp/.cpp`
- **Work:** Fill matrix; confirm no SDL leak (uses `uintptr_t`, `charcs`); classify callbacks as EXT.
- **Acceptance:** Matrix filled.
- **Tests:** existing `TextInputEXTTest`.
- **Deps:** none.

#### INPUT-API-020 — Matrix: `TouchLocationState` (enum, XNA) — value test DONE
- **Priority:** P2 · **Status:** DONE (value test, 2026-07-05; INPUT-TEST-001) · **Area:** API/Enum
- **Files:** `Touch/TouchLocationState.hpp`
- **Work:** Pin `Invalid=0,Released=1,Pressed=2,Moved=3` vs FNA.
- **Acceptance:** Value test present.
- **Tests:** new `TouchLocationStateTests`.
- **Deps:** none.

#### INPUT-API-021 — Matrix: `GestureType` (flags enum, XNA) — value+operator test DONE
- **Priority:** P2 · **Status:** DONE (value+operator test, 2026-07-05; INPUT-TEST-001) · **Area:** API/Enum
- **Files:** `Touch/GestureType.hpp`
- **Work:** Pin all power-of-two values (`None=0 … PinchComplete=512`) and operator behavior vs FNA.
- **Acceptance:** Value+flag test present.
- **Tests:** new `GestureTypeTests`.
- **Deps:** none.

#### INPUT-API-022 — Matrix: `GestureSample` (struct, XNA+EXT) — PARTIAL
- **Priority:** P2 · **Status:** TODO · **Area:** API
- **Files:** `Touch/GestureSample.hpp/.cpp`
- **Problem:** No `==`/hash/ToString (verify FNA also lacks them); FingerId EXT props.
- **Work:** Fill matrix; verify all 6 getters + 2 EXT getters; confirm 3 ctors.
- **Acceptance:** Matrix filled; equality absence justified vs FNA.
- **Tests:** existing `GestureSampleTest` + property assertions.
- **Deps:** none.

#### INPUT-API-023 — Matrix: `TouchLocation` (struct, XNA) — COVERED (add TryGetPreviousLocation test)
- **Priority:** P2 · **Status:** TODO · **Area:** API
- **Files:** `Touch/TouchLocation.hpp/.cpp`
- **Problem:** `TryGetPreviousLocation` has no dedicated test; false-path does not write the out-param (deviation §18).
- **Work:** Fill matrix; add dedicated true/false `TryGetPreviousLocation` tests; verify `ToString` content.
- **Acceptance:** Matrix filled; both paths tested.
- **Tests:** extend `TouchLocationTest`.
- **Deps:** none.

#### INPUT-API-024 — Matrix: `TouchCollection` (struct, XNA) — COVERED
- **Priority:** P3 · **Status:** TODO · **Area:** API
- **Files:** `Touch/TouchCollection.hpp/.cpp`
- **Work:** Fill matrix; confirm `IsReadOnly` always true, exception guards on `operator[]/CopyTo/RemoveAt/Insert`;
  classify NOXNA STL ergonomics (`begin/end/empty`).
- **Acceptance:** Matrix filled.
- **Tests:** existing `TouchCollectionTest`.
- **Deps:** none.

#### INPUT-API-025 — Matrix: `TouchPanelCapabilities` (struct, XNA) — COVERED
- **Priority:** P3 · **Status:** TODO · **Area:** API
- **Files:** `Touch/TouchPanelCapabilities.hpp/.cpp`
- **Work:** Fill matrix; tie `MaximumTouchCount` value to the §18 decision.
- **Acceptance:** Matrix filled.
- **Tests:** existing `TouchPanelCapabilitiesTest`.
- **Deps:** INPUT-TOUCH-012.

#### INPUT-API-026 — Matrix: `TouchPanel` (static class, XNA+EXT) — COVERED
- **Priority:** P2 · **Status:** TODO · **Area:** API
- **Files:** `Touch/TouchPanel.hpp/.cpp`
- **Work:** Fill matrix; classify NOXNA members (`EnqueueGesture`, `INTERNAL_onTouchEvent`, `SetFinger`,
  `Update`, `ResetForTests`, `touchDeviceExists`); confirm `ReadGesture` throws
  `System::InvalidOperationException` when empty (FNA parity); pin `MAX_TOUCHES=8`, `NO_FINGER=-1`.
- **Acceptance:** Matrix filled.
- **Tests:** existing `TouchInputTest`/`TouchEdgeCaseTest`.
- **Deps:** none.

#### INPUT-API-027 — Generate the matrix mechanically from headers
- **Priority:** P2 · **Status:** TODO · **Area:** API/Tooling
- **Files:** a small inspection script (mentioned, not committed to Input code)
- **Problem:** Hand-maintaining 26 type matrices drifts.
- **Work:** Write a helper that lists public members per Input header and emits a matrix skeleton to
  cross-check against FNA `.cs`. Keep the script outside `src/` (tools/scratch).
- **Acceptance:** Script regenerates the member list; diff against FNA is reviewable.
- **Tests:** n/a.
- **Deps:** none.

#### INPUT-API-028 — Cross-check every type's namespace + include path
- **Priority:** P3 · **Status:** TODO · **Area:** API
- **Files:** all Input headers
- **Work:** Verify each type is in the correct XNA namespace and include path mirrors it (Touch types under
  `Input/Touch`).
- **Acceptance:** All paths/namespaces correct.
- **Tests:** header-hygiene compile test.
- **Deps:** none.

#### INPUT-API-029 — Confirm `GetTypeName()` policy for Input types
- **Priority:** P2 · **Status:** TODO · **Area:** API
- **Files:** all Input types
- **Problem:** CLAUDE.md requires concrete `System::Object` subclasses to override `NOXNA GetTypeName()`.
  No Input type overrides it. Most Input types are value structs / static classes / non-Object — likely
  exempt — but this must be confirmed, not assumed.
- **Work:** Determine which Input types actually inherit `System::Object`; add `GetTypeName()` only where
  the rule applies; document exemptions.
- **Acceptance:** Rule satisfied or explicit exemption recorded per type.
- **Tests:** n/a / compile.
- **Deps:** none.

#### INPUT-API-030 — Public-API-only compile test (header hygiene)
- **Priority:** P1 · **Status:** DONE (2026-07-05) · **Area:** API/Test
- **Files:** `tests/Microsoft/Xna/Framework/Input/PublicApiInputCompileTests.cpp`
- **Problem:** No test compiles a program using ONLY public XNA Input headers; all current tests pull in
  `CNA/Internal/**`. Regressions that leak internal/SDL types would go unnoticed.
- **Work:** Add a translation unit including only public headers, instantiating each public type, that must
  compile without any `CNA/Internal` or (except the known MouseCursor case) `SDL` include.
- **Acceptance:** Compiles clean; fails if a public header starts requiring internal/SDL types.
- **Tests:** the new compile TU.
- **Deps:** INPUT-AUDIT-003.
- **Result (2026-07-05):** Added `PublicApiInputCompileTests.cpp`: includes only public Input headers
  (+`PlayerIndex`), links a never-run `UsePublicInputApi()` that constructs/uses every public type, and a
  preprocessor `#error` guard asserting no public header **except** `Mouse.hpp`/`MouseCursor.hpp` pulls SDL.
  Guard verified live (temporarily moving an SDL-exposing include above it triggers the `#error`).
  **Finding (raises DEC-21 severity):** the leak is not confined to the NOXNA `MouseCursor` — `Mouse.hpp`
  `#include`s `MouseCursor.hpp`, so the **strict-XNA `Mouse` header transitively drags in all of SDL** (this
  was **fixed the same day by INPUT-MOUSE-018**; the guard now covers all 26 public headers with no
  exception). The other 24 public Input headers were already clean. Runs in the full suite; not matched by
  the base input-filter tokens (another INPUT-BUILD-003 data point).

---

## 6. Public API guardrails

#### INPUT-API-031 — Freeze the strict XNA 4.0 Input surface (golden signature list)
- **Priority:** P1 · **Status:** DONE (2026-07-05) · **Area:** API/Guardrail
- **Files:** `docs/input-public-api-frozen.md` (golden snapshot),
  `tests/Microsoft/Xna/Framework/Input/PublicApiInputSignatureFreezeTests.cpp` (enforcement)
- **Problem:** Nothing prevents silent removal/rename of a public XNA member.
- **Work:** Snapshot the public signatures (from INPUT-API-027) as a golden file; add a test/check that
  fails on drift.
- **Acceptance:** Removing a public XNA member fails the check.
- **Tests:** signature-diff check.
- **Deps:** INPUT-API-027.
- **Result (2026-07-05):** Enforced the freeze **in code** rather than as a fragile text diff. Added
  `PublicApiInputSignatureFreezeTests.cpp` — a translation unit that pins the EXACT signature of every
  public member (~250 entries across all 26 public Input types) via fully-spelled function-/member-
  pointer `static_cast`s and `std::is_constructible_v`/special-member traits. Removing or renaming a
  member fails to compile (address-of has no target); a signature/return-type/param change fails the
  `static_cast`; a constructor change fails the trait. Hidden-friend `operator==`/`!=` are frozen via an
  ADL `a==b` expression. Strict-XNA + EXT + stable NOXNA-convenience members are all covered; the
  internal `INTERNAL_*` / `*ForTests` hooks and private members are deliberately excluded (documented).
  The human-readable golden list lives in `docs/input-public-api-frozen.md`; the two are kept in
  lock-step. **Negative-verified:** corrupting one entry (SetVibration `bool`→`void`) fails compilation
  as designed; reverted. Compiles clean on EasyGL and under ASan+UBSan; input filter 289/289 (order-
  independent, shuffle×2; the 3 MouseCursor failures are the known dummy-driver limitation). Like
  INPUT-API-030 it includes only public headers, so it is also a second SDL-containment/standalone
  check. The mechanical header-generated matrix (INPUT-API-027) remains open but is not required for
  this guard — the freeze enumerates the surface directly.

#### INPUT-API-032 — Enforce `EXT`/`NOXNA` tagging on every non-XNA member
- **Priority:** P1 · **Status:** TODO · **Area:** API/Guardrail
- **Files:** all Input headers
- **Problem:** EXT/NOXNA tagging is mostly present but not enforced; a new non-XNA member could slip in
  untagged.
- **Work:** Audit every member against FNA/XNA; ensure non-XNA members carry `EXT` suffix and/or `NOXNA`;
  document the convention.
- **Acceptance:** Every non-XNA member is tagged; audit table recorded.
- **Tests:** grep-based lint (optional).
- **Deps:** §5 matrices.

#### INPUT-API-033 — Guard against internal SDL types in public signatures
- **Priority:** P1 · **Status:** DONE (2026-07-05) · **Area:** API/Guardrail
- **Files:** `include/Microsoft/Xna/Framework/Input/**`
- **Problem:** `MouseCursor.hpp` was a known leak; policy must be explicit and enforced.
- **Work:** Decide MouseCursor's exception (documented, intentional) vs refactor to a pimpl/opaque handle;
  ensure no other header leaks.
- **Acceptance:** Only explicitly-approved SDL exposure remains; INPUT-API-030 enforces it.
- **Tests:** INPUT-API-030 (`PublicApiInputCompileTests`).
- **Deps:** INPUT-AUDIT-003.
- **Result (2026-07-05):** Enforcement guard exists (INPUT-API-030) AND the one real leak is fixed
  (INPUT-MOUSE-018): no public Input header includes an SDL header — the guard now asserts this across all
  26 headers. Only remaining SDL exposure is the intentional forward-declared opaque `SDL_Cursor*` handle
  in the NOXNA `MouseCursor` (no SDL definition/header reaches consumers). Guardrail satisfied.

#### INPUT-API-034 — Prevent accidental public-API drift on enums (values are ABI)
- **Priority:** P2 · **Status:** DONE (2026-07-05) · **Area:** API/Guardrail
- **Files:** `Keys.hpp, Buttons.hpp, GestureType.hpp, GamePadType.hpp, GamePadDeadZone.hpp, ButtonState.hpp, KeyState.hpp, TouchLocationState.hpp`
- **Work:** The enum-value tests (INPUT-API-001..005,013,020,021) collectively pin every value; ensure they
  fail on any renumbering.
- **Acceptance:** Renumbering any enum value fails a test.
- **Tests:** the enum value tests.
- **Deps:** those tasks.
- **Result (2026-07-05):** Verified that **all 8 public Input enums are now exhaustively pinned** —
  every enumerator is asserted against a hardcoded literal, so renumbering any value fails a test and
  removing any member is a compile error (the tests reference members by name). Member-count vs
  assertion-count confirmed exhaustive for each: `Buttons` 31/31 (INPUT-TEST-001, FNA-cross-checked),
  `GamePadType` 10/10, `GamePadDeadZone` 3/3, `ButtonState` 2/2, `KeyState` 2/2, `TouchLocationState`
  4/4, `GestureType` 11/11, and `Keys` 160/160 (INPUT-KBD-001, this session — the last remaining leg,
  INPUT-API-013). No dedicated new test needed: the guardrail is realized by the union of the eight
  exhaustive value suites, all currently green (incl. under ASan+UBSan).

---

## 7. Keyboard plan

Verified facts: keycode map `try_convert_sdl_key` (SdlInputBridge.cpp:456–595) and scancode map
`try_convert_sdl_scancode` (601–734) port FNA's `INTERNAL_scanMap`/`INTERNAL_xnaMap`; scancode mode is
env-gated (`FNA_KEYBOARD_USE_SCANCODES=="1"`, cached); key repeats keep the key down (no re-`SetKeyState`);
`SDLK_AC_BACK→Keys::Escape`; unmapped keycodes are **dropped** (not pushed as `Keys::None`). One live
FIXME at SdlInputBridge.cpp:729 (NONUSHASH/NONUSBACKSLASH scancodes "need verification", mirrors FNA).

#### INPUT-KBD-001 — Full `Keys` numeric parity vs FNA
- **Priority:** P1 · **Status:** DONE (2026-07-05) · **Area:** Keyboard
- **Files:** `Keys.hpp`, `tests/.../KeyboardInputTests.cpp`
- **Problem:** Existing value test may cover a subset.
- **Work:** Assert every `Keys` member equals its FNA/Windows-VK value.
- **Acceptance:** All members pinned; diff vs FNA `Keys.cs` empty.
- **Tests:** `KeysValuesMatchXNANumericConstants` (extended).
- **Deps:** INPUT-API-013.
- **Result (2026-07-05):** Mechanically diffed CNA `Keys.hpp` against the FNA reference
  `src/Input/Keys.cs` (hex-normalized): **160 members, byte-identical — no value drift, no member
  missing on either side.** Replaced the 10-key spot-check `KeysValuesMatchXNANumericConstants` with
  an exhaustive 160-entry parity table (every member referenced by name with its hardcoded literal
  value), plus a `static_assert` size anchor (==160) and a distinct-value check (no two `Keys` share
  a numeric value). Passes locally and clean under ASan+UBSan. Since the table pins by name, removing
  a member is now a compile error and renumbering one fails the `EXPECT_EQ`. This also completes the
  `Keys` (INPUT-API-013) leg required by the enum-ABI guardrail INPUT-API-034.

#### INPUT-KBD-002 — `Keyboard.GetState()` returns accumulated pressed set
- **Priority:** P2 · **Status:** TODO · **Area:** Keyboard
- **Files:** `Keyboard.cpp`, `InputManager.cpp`
- **Work:** Verify GetState reflects the event-accumulated `PressedKeys`; snapshot immutability.
- **Acceptance:** Down/up sequences reflected; snapshot independent of later events.
- **Tests:** existing `KeyboardInputTest` (+ explicit immutability case).
- **Deps:** none.

#### INPUT-KBD-003 — `Keyboard.GetState(PlayerIndex)` semantics
- **Priority:** P3 · **Status:** TODO · **Area:** Keyboard
- **Files:** `Keyboard.cpp`
- **Problem:** XNA keyboard is single; FNA ignores index. Confirm CNA returns the same state for any index.
- **Work:** Test all PlayerIndex values return identical state; document.
- **Acceptance:** Behavior pinned + documented.
- **Tests:** extend `KeyboardInputTest`.
- **Deps:** none.

#### INPUT-KBD-004 — `KeyboardState` construction & immutability
- **Priority:** P3 · **Status:** TODO · **Area:** Keyboard
- **Files:** `KeyboardState.hpp/.cpp`
- **Work:** Verify all three ctors; mutating source set after construction does not change the state.
- **Acceptance:** Immutability held.
- **Tests:** existing `KeyboardStateTest`.
- **Deps:** none.

#### INPUT-KBD-005 — `IsKeyDown`/`IsKeyUp`/indexer parity
- **Priority:** P3 · **Status:** TODO · **Area:** Keyboard
- **Files:** `KeyboardState.cpp`
- **Work:** Verify indexer == `getItem` == `IsKeyDown` for down/up.
- **Acceptance:** Consistent across accessors.
- **Tests:** existing `KeyboardStateTest`.
- **Deps:** none.

#### INPUT-KBD-006 — `GetPressedKeys` ordering & duplicates
- **Priority:** P2 · **Status:** TODO · **Area:** Keyboard
- **Files:** `KeyboardState.cpp`
- **Problem:** Backed by `unordered_set`; FNA returns keys in an order. Ordering must be defined/tested.
- **Work:** Confirm sorted (or FNA-matching) order; ensure no duplicates; document ordering guarantee.
- **Acceptance:** Ordering deterministic and documented.
- **Tests:** existing sorted-order case + a duplicate-input case.
- **Deps:** none.

#### INPUT-KBD-007 — `Keys.None` handling
- **Priority:** P2 · **Status:** DONE (2026-07-05; DEC-16) · **Area:** Keyboard
- **Files:** `SdlInputBridge.cpp`, `KeyboardState.cpp`
- **Problem:** Unmapped keycodes dropped; scancode `UNKNOWN→Keys::None`. Confirm `None` is never reported
  as pressed via the keycode path.
- **Work:** Test that dropped keys never appear; document the keycode-vs-scancode None difference (§18).
- **Acceptance:** No `Keys::None` in pressed set from keycode path.
- **Tests:** extend `SdlInputBridgeKeyboardTests`.
- **Deps:** none.

#### INPUT-KBD-008 — Equality/hash/ToString parity for `KeyboardState`
- **Priority:** P3 · **Status:** TODO · **Area:** Keyboard
- **Files:** `KeyboardState.cpp`
- **Work:** Verify `==/!=`, `GetHashCode` (FNA word-xor), `ToString`=typename; equal states → equal hash.
- **Acceptance:** All parity cases pass.
- **Tests:** existing `KeyboardStateTest`.
- **Deps:** none.

#### INPUT-KBD-009 — SDL keycode-mode mapping completeness
- **Priority:** P1 · **Status:** TODO · **Area:** Keyboard/Bridge
- **Files:** `SdlInputBridge.cpp:456–595`, `SdlInputBridgeKeyboardTests.cpp`
- **Work:** Line-by-line diff `try_convert_sdl_key` vs FNA; cover letters/digits/numpad/oem/modifiers/
  function/media/locale fallbacks (², |, +, ø, æ, é→unmapped).
- **Acceptance:** Diff empty or deviations documented; all groups tested.
- **Tests:** extend keyboard bridge tests.
- **Deps:** none.

#### INPUT-KBD-010 — SDL scancode-mode mapping completeness
- **Priority:** P1 · **Status:** TODO · **Area:** Keyboard/Bridge
- **Files:** `SdlInputBridge.cpp:601–734`, keyboard bridge tests
- **Work:** Diff `try_convert_sdl_scancode` vs FNA `INTERNAL_scanMap`; cover the FIXME line 729 pair.
- **Acceptance:** Diff empty or documented; scancode mode ignores keycode (tested).
- **Tests:** extend keyboard bridge tests.
- **Deps:** INPUT-KBD-011.

#### INPUT-KBD-011 — Resolve the scancode FIXME (SdlInputBridge.cpp:729)
- **Priority:** P2 · **Status:** TODO · **Area:** Keyboard/Bridge
- **Files:** `SdlInputBridge.cpp:729`
- **Problem:** `NONUSHASH`/`NONUSBACKSLASH → Keys::None` flagged "need verification" (mirrors FNA's own doubt).
- **Work:** Verify correct XNA mapping on a non-US (e.g. German ISO) layout; fix or confirm; remove the FIXME
  with a test or a documented deviation.
- **Acceptance:** Mapping decided; FIXME removed; test added.
- **Tests:** scancode mapping test for those scancodes.
- **Deps:** none.

#### INPUT-KBD-012 — `FNA_KEYBOARD_USE_SCANCODES` env behavior + caching
- **Priority:** P2 · **Status:** TODO · **Area:** Keyboard/Bridge
- **Files:** `SdlInputBridge.cpp:79–88`, test seams
- **Work:** Verify only `"1"` enables; cached once; test override seam flips behavior; `GetKeyFromScancodeEXT`
  is identity in scancode mode, translates otherwise.
- **Acceptance:** Both modes tested via the seam.
- **Tests:** existing + extended keyboard bridge tests.
- **Deps:** none.

#### INPUT-KBD-013 — Unmapped SDL key behavior (dropped, not None)
- **Priority:** P2 · **Status:** DONE (2026-07-05; DEC-16) · **Area:** Keyboard/Bridge
- **Files:** `SdlInputBridge.cpp`
- **Work:** Confirm/keep the "unmapped keycode dropped" behavior; record as §18 deviation with rationale.
- **Acceptance:** Test asserts drop; deviation documented.
- **Tests:** existing "unmapped keycode dropped" case.
- **Deps:** none.

#### INPUT-KBD-014 — Non-US layout keys (Czech, German, French)
- **Priority:** P2 · **Status:** TODO · **Area:** Keyboard
- **Files:** `SdlInputBridge.cpp`, tests, `docs/platform-input-notes.md`
- **Work:** Add table-driven tests for representative non-US keys; document mapping gaps (e.g. é/BÉPO).
- **Acceptance:** Representative keys tested; gaps documented.
- **Tests:** new parameterized keyboard test.
- **Deps:** INPUT-KBD-009.

#### INPUT-KBD-015 — IME-related keys (ImeConvert/NoConvert/Kana/Kanji/ProcessKey)
- **Priority:** P3 · **Status:** TODO · **Area:** Keyboard
- **Files:** `Keys.hpp`, `SdlInputBridge.cpp`
- **Work:** Confirm these values exist and are intentionally unmapped from SDL (documented like FNA).
- **Acceptance:** Presence + intentional-omission documented.
- **Tests:** value test only.
- **Deps:** none.

#### INPUT-KBD-016 — Browser/media/system keys mapping
- **Priority:** P3 · **Status:** TODO · **Area:** Keyboard
- **Files:** `SdlInputBridge.cpp`
- **Work:** Verify media/browser keycodes map where FNA maps them; document the ones FNA intentionally omits.
- **Acceptance:** Mapping table matches FNA.
- **Tests:** extend keyboard bridge tests.
- **Deps:** none.

#### INPUT-KBD-017 — ChatPad keys (ChatPadGreen/Orange) policy
- **Priority:** P3 · **Status:** TODO · **Area:** Keyboard
- **Files:** `Keys.hpp`
- **Work:** Confirm values present; document that they are XNA-only console keys with no desktop SDL source.
- **Acceptance:** Documented.
- **Tests:** value test.
- **Deps:** none.

#### INPUT-KBD-018 — Android back key mapping (`SDLK_AC_BACK→Escape`)
- **Priority:** P2 · **Status:** DONE (2026-07-05; DEC-17) · **Area:** Keyboard/Platform
- **Files:** `SdlInputBridge.cpp`, `docs/platform-input-notes.md`
- **Problem:** CNA-only mapping with no FNA equivalent (§18).
- **Work:** Keep behind a documented rationale; test synthetic `AC_BACK`→`Escape`.
- **Acceptance:** Tested + documented as intentional CNA behavior.
- **Tests:** synthetic-event test.
- **Deps:** none.

#### INPUT-KBD-019 — Key repeat behavior
- **Priority:** P2 · **Status:** TODO · **Area:** Keyboard/Bridge
- **Files:** `SdlInputBridge.cpp` (repeat skip)
- **Work:** Confirm repeats keep key down without spurious transitions; confirm text synthesis still fires
  on repeat (see §12 repeat gate).
- **Acceptance:** Repeat holds key; no extra down/up.
- **Tests:** existing key-repeat text case + a state case.
- **Deps:** none.

#### INPUT-KBD-020 — Focus lost / gained key behavior (decision)
- **Priority:** P1 · **Status:** DONE (2026-07-05; DEC-15 accepted — match FNA) · **Area:** Keyboard/Bridge
- **Files:** `SdlInputBridge.cpp`, `InputManager.cpp`, `Game.cpp`
- **Problem:** No key-clearing on `WINDOW_FOCUS_LOST` (matches FNA, but event-driven CNA can leave a key
  stuck). Decision open (§18).
- **Work:** Decide match-FNA (document + test current) vs add a beyond-FNA transient clear; implement one.
- **Acceptance:** Decision recorded; behavior tested.
- **Tests:** new focus-loss test.
- **Deps:** INPUT-BRIDGE-014.

#### INPUT-KBD-021 — Window close/minimize/restore behavior
- **Priority:** P3 · **Status:** TODO · **Area:** Keyboard/Bridge
- **Files:** `SdlInputBridge.cpp`
- **Work:** Confirm these window events don't corrupt keyboard state; document.
- **Acceptance:** No state corruption; documented.
- **Tests:** synthetic window-event test.
- **Deps:** INPUT-KBD-020.

#### INPUT-KBD-022 — Event-pump / main-thread assumptions documented
- **Priority:** P3 · **Status:** TODO · **Area:** Keyboard/Docs
- **Files:** `docs/input-backend.md`, `InputManager.hpp`
- **Work:** State that keyboard state is only as fresh as the last `PollEvents`; single-thread only.
- **Acceptance:** Documented in one place, linked from others.
- **Tests:** n/a.
- **Deps:** none.

---

## 8. Mouse plan

Verified facts: `GetState` returns absolute pos unless relative mode (then returns accumulated delta and
**drains** it to 0); wheel = `(int)wheel.y * 120` (horizontal `wheel.x` intentionally dropped); button-down
fires `Mouse::INTERNAL_onClicked(button-1)` (zero-based); logical↔window conversion at `SetPosition` time;
relative-mode flag cached in `InputManager`. `MouseCursor` is NOXNA MonoGame-derived and leaks SDL types.

#### INPUT-MOUSE-001 — `Mouse.GetState()` position/buttons/scroll
- **Priority:** P2 · **Status:** TODO · **Area:** Mouse
- **Files:** `Mouse.cpp`, `InputManager.cpp`
- **Work:** Verify all 5 buttons + X/Y + scroll reflect events.
- **Acceptance:** All fields correct.
- **Tests:** existing `MouseTest`.
- **Deps:** none.

#### INPUT-MOUSE-002 — `Mouse.SetPosition(x,y)` logical→window
- **Priority:** P2 · **Status:** TODO · **Area:** Mouse
- **Files:** `Mouse.cpp`
- **Work:** Verify logical coords convert to window coords via the graphics transform; test with letterbox.
- **Acceptance:** Warp lands at expected window pixel (unit-level).
- **Tests:** existing letterbox conversion cases.
- **Deps:** none.

#### INPUT-MOUSE-003 — WindowHandle plumbing (uintptr_t, no SDL leak)
- **Priority:** P3 · **Status:** TODO · **Area:** Mouse
- **Files:** `Mouse.hpp/.cpp`
- **Work:** Verify get/set round-trip; SDL_Window* confined to .cpp.
- **Acceptance:** Round-trips; no SDL in header.
- **Tests:** existing + INPUT-API-030.
- **Deps:** none.

#### INPUT-MOUSE-004 — Null / unset window behavior
- **Priority:** P2 · **Status:** TODO · **Area:** Mouse
- **Files:** `Mouse.cpp`
- **Work:** Confirm GetState/SetPosition behave safely with no window set (no crash).
- **Acceptance:** Safe no-op / defined behavior.
- **Tests:** new no-window case (may GTEST_SKIP if requires display).
- **Deps:** none.

#### INPUT-MOUSE-005 — High-DPI / logical vs window coordinates
- **Priority:** P2 · **Status:** TODO · **Area:** Mouse/Platform
- **Files:** `Mouse.cpp`, `SdlInputBridge.cpp` (`to_logical_position`)
- **Problem:** Motion events convert via `SDL_RenderCoordinatesFromWindow` / backend transform. High-DPI
  correctness is unverified on a real HiDPI display.
- **Work:** Unit-test the conversion; document HiDPI manual verification need.
- **Acceptance:** Conversion tested; HiDPI flagged for manual.
- **Tests:** conversion unit test.
- **Deps:** none.

#### INPUT-MOUSE-006 — Relative mouse mode default & accumulation
- **Priority:** P2 · **Status:** TODO · **Area:** Mouse
- **Files:** `Mouse.cpp`, `InputManager.cpp`
- **Work:** Verify default off; enabling accumulates deltas; GetState drains to 0; round-trip toggle flushes.
- **Acceptance:** Matches existing relative-mode cases.
- **Tests:** existing relative-mode cases.
- **Deps:** none.

#### INPUT-MOUSE-007 — External SDL relative-mode desync (suspected)
- **Priority:** P2 · **Status:** DONE (2026-07-05; DEC-14 — accept, getter is live; cache is internal) · **Area:** Mouse
- **Files:** `Mouse.cpp`, `InputManager.cpp`, `docs/input-fna-fidelity.md`
- **Problem:** Cache can desync only if SDL relative mode is toggled outside CNA API (§18).
- **Work:** Decide: read live vs keep cache + document unreachable-via-API. Add test if live-read chosen.
- **Acceptance:** Decision recorded; behavior tested/documented.
- **Tests:** relative-mode round-trip.
- **Deps:** none.

#### INPUT-MOUSE-008 — Button state transitions (all 5)
- **Priority:** P3 · **Status:** TODO · **Area:** Mouse/Bridge
- **Files:** `SdlInputBridge.cpp`
- **Work:** Verify Left/Right/Middle/X1/X2 down/up map correctly.
- **Acceptance:** All transitions correct.
- **Tests:** existing mouse bridge tests.
- **Deps:** none.

#### INPUT-MOUSE-009 — XButton1/XButton2 mapping
- **Priority:** P3 · **Status:** TODO · **Area:** Mouse/Bridge
- **Files:** `SdlInputBridge.cpp`
- **Work:** Confirm SDL X1/X2 → XNA XButton1/2.
- **Acceptance:** Correct.
- **Tests:** extend mouse bridge tests.
- **Deps:** none.

#### INPUT-MOUSE-010 — Wheel value & XNA 120-unit convention
- **Priority:** P2 · **Status:** TODO · **Area:** Mouse/Bridge
- **Files:** `SdlInputBridge.cpp:1248`
- **Work:** Verify whole notches ×120, fractional sub-notch truncated (int cast before ×120), accumulation.
- **Acceptance:** Matches FNA notch truncation.
- **Tests:** existing `SdlInputBridgeMouseWheelTest`.
- **Deps:** none.

#### INPUT-MOUSE-011 — Horizontal wheel policy (dropped)
- **Priority:** P2 · **Status:** DONE (2026-07-05; DEC-18) · **Area:** Mouse/Bridge
- **Files:** `SdlInputBridge.cpp:1249–1258`
- **Problem:** `wheel.x` dropped (NOXNA); XNA has no horizontal wheel. Decision documented but re-affirm (§18).
- **Work:** Keep dropped; document that XNA lacks horizontal wheel; add explicit "x ignored" test.
- **Acceptance:** Documented + tested.
- **Tests:** new "horizontal wheel ignored" case.
- **Deps:** none.

#### INPUT-MOUSE-012 — Motion events → position + relative delta
- **Priority:** P3 · **Status:** TODO · **Area:** Mouse/Bridge
- **Files:** `SdlInputBridge.cpp:1195`
- **Work:** Verify motion sets logical pos and feeds relative delta (only counted when relative mode on).
- **Acceptance:** Correct.
- **Tests:** extend mouse bridge tests.
- **Deps:** none.

#### INPUT-MOUSE-013 — `ClickedEXT` behavior + no-subscriber safety
- **Priority:** P2 · **Status:** TODO · **Area:** Mouse/EXT
- **Files:** `Mouse.cpp`
- **Work:** Verify down fires zero-based `ClickedEXT`; up does not; no-subscriber is safe.
- **Acceptance:** Matches existing cases.
- **Tests:** existing `OnClicked*` cases.
- **Deps:** none.

#### INPUT-MOUSE-014 — `ClickedEXT` single vs multicast decision
- **Priority:** P2 · **Status:** DONE (2026-07-05; DEC-06 — multicast) · **Area:** Mouse/EXT
- **Files:** `Mouse.hpp`, `docs/input-fna-fidelity.md`
- **Problem:** Single `std::function` vs FNA multicast `Action<int>` (§18). Overwriting subscribers silently.
- **Work:** Decide keep-single (document) vs move to `System::EventHandler`/multicast. Implement one.
- **Acceptance:** Decision recorded; if multicast, multi-subscriber test.
- **Tests:** subscriber test.
- **Deps:** none.

#### INPUT-MOUSE-015 — `MouseState` value semantics
- **Priority:** P3 · **Status:** TODO · **Area:** Mouse
- **Files:** `MouseState.cpp`
- **Work:** Verify ctor slots, `Equals/==/!=`, hash consistency, `ToString` content format vs FNA.
- **Acceptance:** All parity cases pass.
- **Tests:** existing `MouseStateTest`.
- **Deps:** INPUT-API-016.

#### INPUT-MOUSE-016 — `MouseCursor` lifecycle & disposal
- **Priority:** P2 · **Status:** TODO · **Area:** Mouse/CNA
- **Files:** `MouseCursor.cpp`
- **Work:** Verify owning vs non-owning destruction, idempotent Dispose, move ctor/assign ownership transfer,
  stock singletons not destroyed.
- **Acceptance:** Matches existing cursor cases.
- **Tests:** existing `MouseCursorTest`.
- **Deps:** none.

#### INPUT-MOUSE-017 — System cursor singletons
- **Priority:** P3 · **Status:** TODO · **Area:** Mouse/CNA
- **Files:** `MouseCursor.cpp`
- **Work:** Verify 12 stock getters return non-null stable singletons; disposing a stock is a no-op.
- **Acceptance:** Stable identity.
- **Tests:** existing cases.
- **Deps:** none.

#### INPUT-MOUSE-018 — `MouseCursor` SDL-in-public-header decision
- **Priority:** P1 · **Status:** DONE (2026-07-05) · **Area:** Mouse/API/Guardrail
- **Files:** `MouseCursor.hpp`, `MouseCursor.cpp`, `PublicApiInputCompileTests.cpp`
- **Problem:** Public header `#include <SDL3/SDL.h>` and exposes `SDL_Cursor*`, `SDL_SystemCursor`.
  Confirmed (INPUT-API-030) to spread transitively: `Mouse.hpp` includes `MouseCursor.hpp`, so the
  strict-XNA `Mouse` header also dragged in SDL — not a NOXNA-only concern.
- **Work:** Decide: (a) accept as documented intentional exception (whole class is NOXNA), or (b) refactor to
  an opaque handle/pimpl so no public XNA header pulls SDL, and forward-declare `MouseCursor` in `Mouse.hpp`.
  Implement chosen path. (Option (b) preferred now that a strict-XNA header is affected.)
- **Acceptance:** Either documented exception in INPUT-API-033 or SDL removed from the public headers; the
  `PublicApiInputCompileTests` guard updated to move `Mouse.hpp`/`MouseCursor.hpp` above the `#error` line.
- **Tests:** INPUT-API-030 (`PublicApiInputCompileTests`).
- **Deps:** INPUT-AUDIT-003.
- **Result (2026-07-05):** Chose **option (b), opaque-handle variant**. Removed `#include <SDL3/SDL.h>` from
  `MouseCursor.hpp`; forward-declared `struct SDL_Cursor;` (pointer to incomplete type — no SDL header
  needed); changed the private `MakeSystem(SDL_SystemCursor)` → `MakeSystem(int)` so the SDL enum stays out
  of the header; added `#include <SDL3/SDL.h>` to `MouseCursor.cpp`. `Mouse.hpp` (which includes
  `MouseCursor.hpp`) therefore no longer transitively pulls SDL. Moved both headers above the compile
  guard's `#error`, which now enforces **no public Input header pulls SDL at all**. Verified: guard passes
  with all 26 public headers above it (it previously *fired* on `MouseCursor.hpp`); mouse/cursor tests
  green on EasyGL (37) + Vulkan (27); full suite 3268; no behavior change. Note: `SDL_Cursor*` remains as
  a **forward-declared opaque handle** in the NOXNA `MouseCursor` API — intended per CLAUDE.md
  ("except where explicitly intended"); the SDL *header/definition* no longer reaches consumers.

#### INPUT-MOUSE-019 — `FromTexture2D` validation
- **Priority:** P2 · **Status:** TODO · **Area:** Mouse/CNA
- **Files:** `MouseCursor.cpp`
- **Work:** Verify Color/ColorSrgb accepted, non-Color rejected, origin-outside-texture throws, cursor
  survives source buffer free.
- **Acceptance:** Matches existing cases.
- **Tests:** existing `FromTexture2D` cases (some GTEST_SKIP headless).
- **Deps:** none.

#### INPUT-MOUSE-020 — SDL init assumptions for cursor creation
- **Priority:** P2 · **Status:** TODO · **Area:** Mouse/Platform
- **Files:** `MouseCursor.cpp`
- **Problem:** Cursor creation needs SDL video; tests GTEST_SKIP headless.
- **Work:** Document the SDL_INIT_VIDEO precondition; ensure graceful behavior when absent.
- **Acceptance:** Documented; no crash without video.
- **Tests:** headless skip is intentional (INPUT-BUILD-008).
- **Deps:** none.

#### INPUT-MOUSE-021 — `SetCursor` behavior incl. disposed cursor
- **Priority:** P3 · **Status:** TODO · **Area:** Mouse/CNA
- **Files:** `Mouse.cpp`
- **Work:** Verify SetCursor applies; disposed cursor → no-op (no crash).
- **Acceptance:** Matches existing case.
- **Tests:** existing `SetCursor disposed no-op`.
- **Deps:** none.

#### INPUT-MOUSE-022 — Wayland/macOS/Windows cursor & warp caveats
- **Priority:** P2 · **Status:** TODO · **Area:** Mouse/Platform
- **Files:** `docs/platform-input-notes.md`
- **Problem:** Wayland `SDL_GetGlobalMouseState`→(0,0); warp/landing readback X11-only.
- **Work:** Consolidate platform caveats; mark which are manual-only.
- **Acceptance:** Caveats documented with platform matrix.
- **Tests:** manual (§4 hardware).
- **Deps:** none.

#### INPUT-MOUSE-023 — Manual: cursor warp + relative mouse on real display
- **Priority:** P2 · **Status:** TODO · **Area:** Mouse/Manual
- **Files:** `docs/input-manual-verification-results.md`, `examples/demo_input`
- **Work:** Manually verify warp landing (X11) and relative-mode capture; record date/OS/backend/SDL.
- **Acceptance:** Dated entry with results.
- **Tests:** manual.
- **Deps:** INPUT-BUILD-004.

---

## 9. GamePad plan

Verified facts: XNA 4-slot model (`MaxSupportedGamePads=4`); `FNA_GAMEPAD_NUM_GAMEPADS` parsed and clamped
to ≤4; subsystem init via `EnsureGamepadSubsystemInitialized()` (startup + lazy in `ProcessEvent`, both
idempotent, sets `SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS`); all 21 SDL buttons mapped (incl. EXT
paddles/misc/touchpad); axis normalize `>=0 /32767`, `<0 /32768`, triggers `/32767`; Y inverted for
thumbstick Y; PacketNumber bumps only on actual change; `GetCapabilities` reads non-mutating cap
properties (deliberately does NOT probe-rumble). The 21-button/6-axis conversion lives in
`SdlInputBridge` (`try_convert_sdl_gamepad_button/axis`); the `ISdlGamepadBackend` seam exposes device
queries and is swappable with `FakeSdlGamepadBackend` (restored to real by `ResetForTests`).

#### INPUT-GAMEPAD-001 — Four-slot model & PlayerIndex mapping
- **Priority:** P2 · **Status:** TODO · **Area:** GamePad
- **Files:** `SdlInputBridge.cpp`, `GamePad.cpp`
- **Work:** Verify slots 0..3 ↔ PlayerIndex One..Four; per-slot isolation.
- **Acceptance:** 4 independent slots.
- **Tests:** existing `GamePadMappingTest`.
- **Deps:** none.

#### INPUT-GAMEPAD-002 — `FNA_GAMEPAD_NUM_GAMEPADS` parsing & clamping
- **Priority:** P2 · **Status:** TODO · **Area:** GamePad
- **Files:** `SdlInputBridge.cpp:42–63`
- **Work:** Verify nullptr/negative/non-numeric → 4; valid → min(n,4); count=0 disables; count=1 single slot.
- **Acceptance:** Matches existing env tests.
- **Tests:** existing `FakeGamepadEnvCount` + more values.
- **Deps:** none.

#### INPUT-GAMEPAD-003 — Subsystem init idempotency (startup + lazy)
- **Priority:** P2 · **Status:** TODO · **Area:** GamePad
- **Files:** `SdlInputBridge.cpp:1175–1191`, `Game.cpp`
- **Work:** Verify init at startup and lazily in ProcessEvent; both safe to call repeatedly.
- **Acceptance:** No double-init issues.
- **Tests:** existing `SdlGamepadSubsystemInit`.
- **Deps:** none.

#### INPUT-GAMEPAD-004 — Hotplug add
- **Priority:** P2 · **Status:** TODO · **Area:** GamePad
- **Files:** `SdlInputBridge.cpp:1454`
- **Work:** Verify ADDED assigns a free slot, opens gamepad, sets connection, bumps packet.
- **Acceptance:** Connect before first frame reflected.
- **Tests:** existing fake-gamepad connect case.
- **Deps:** none.

#### INPUT-GAMEPAD-005 — Hotplug remove
- **Priority:** P2 · **Status:** TODO · **Area:** GamePad
- **Files:** `SdlInputBridge.cpp:1485`
- **Work:** Verify REMOVED closes handle, nulls slot, clears connection.
- **Acceptance:** Disconnect reflected; handle closed.
- **Tests:** existing remove case.
- **Deps:** none.

#### INPUT-GAMEPAD-006 — Duplicate add ignored
- **Priority:** P3 · **Status:** TODO · **Area:** GamePad
- **Files:** `SdlInputBridge.cpp`
- **Work:** Verify re-adding an already-mapped joystick id is a no-op.
- **Acceptance:** No duplicate slot.
- **Tests:** existing duplicate-add case.
- **Deps:** none.

#### INPUT-GAMEPAD-007 — Slot reuse & unknown-remove safety
- **Priority:** P3 · **Status:** TODO · **Area:** GamePad
- **Files:** `SdlInputBridge.cpp`
- **Work:** Verify a freed slot is reusable; removing an unknown id is ignored.
- **Acceptance:** Safe.
- **Tests:** existing cases.
- **Deps:** none.

#### INPUT-GAMEPAD-008 — Over-limit connections refused
- **Priority:** P3 · **Status:** TODO · **Area:** GamePad
- **Files:** `SdlInputBridge.cpp`
- **Work:** Verify >4 (or >env count) pads are refused a slot.
- **Acceptance:** Extra pads not assigned.
- **Tests:** existing >4 refuse case.
- **Deps:** none.

#### INPUT-GAMEPAD-009 — Disconnected state defaults
- **Priority:** P3 · **Status:** TODO · **Area:** GamePad
- **Files:** `GamePad.cpp`, `GamePadState.cpp`
- **Work:** Verify disconnected slot → default at-rest `GamePadState`, IsConnected=false; invalid PlayerIndex
  → disconnected.
- **Acceptance:** Matches existing cases.
- **Tests:** existing `GamePadInputTest`/`GamePadStateTest`.
- **Deps:** none.

#### INPUT-GAMEPAD-010 — `GamePad.GetState` (default + deadzone overload)
- **Priority:** P2 · **Status:** TODO · **Area:** GamePad
- **Files:** `GamePad.cpp`
- **Work:** Verify both overloads; deadzone applied per mode; snapshot immutability.
- **Acceptance:** Both overloads correct.
- **Tests:** existing `GamePadTest`/`GamePadInputTest`.
- **Deps:** none.

#### INPUT-GAMEPAD-011 — `GamePad.GetCapabilities`
- **Priority:** P2 · **Status:** TODO · **Area:** GamePad
- **Files:** `SdlInputBridge.cpp:1036`
- **Work:** Verify connected caps populated from device queries; disconnected → all false / Unknown.
- **Acceptance:** Matches existing cases.
- **Tests:** existing capability tests.
- **Deps:** none.

#### INPUT-GAMEPAD-012 — `GetCapabilities` must not mutate slots or rumble
- **Priority:** P1 · **Status:** TODO · **Area:** GamePad
- **Files:** `SdlInputBridge.cpp:1078–1082`
- **Problem:** FNA-deviation: reads non-mutating cap booleans instead of probing with zero-magnitude rumble
  (which would cancel active vibration).
- **Work:** Keep behavior; assert `GetCapabilities` does not increment `rumbleCalls` on the fake.
- **Acceptance:** No rumble side-effect; deviation documented.
- **Tests:** existing "GetCapabilities doesn't rumble" case.
- **Deps:** none.

#### INPUT-GAMEPAD-013 — `SetVibration` + motor clamping
- **Priority:** P2 · **Status:** TODO · **Area:** GamePad
- **Files:** `SdlInputBridge.cpp` (`SetVibration`)
- **Work:** Verify left/right clamped [0,1]→[0,0xFFFF]; disconnected → false.
- **Acceptance:** Clamp + return value correct.
- **Tests:** existing rumble cases.
- **Deps:** none.

#### INPUT-GAMEPAD-014 — `SetTriggerVibrationEXT`
- **Priority:** P3 · **Status:** TODO · **Area:** GamePad/EXT
- **Files:** `SdlInputBridge.cpp`
- **Work:** Verify trigger-rumble path clamps and calls `RumbleGamepadTriggers`.
- **Acceptance:** Correct call + clamp.
- **Tests:** existing trigger-rumble case.
- **Deps:** none.

#### INPUT-GAMEPAD-015 — `SetLightBarEXT`
- **Priority:** P3 · **Status:** TODO · **Area:** GamePad/EXT
- **Files:** `SdlInputBridge.cpp`
- **Work:** Verify RGB forwarded to `SetGamepadLED`; disconnected no-op.
- **Acceptance:** Correct.
- **Tests:** existing lightbar case.
- **Deps:** none.

#### INPUT-GAMEPAD-016 — `GetGUIDEXT` + format
- **Priority:** P2 · **Status:** DONE (2026-07-05; DEC-20 — live, same values) · **Area:** GamePad/EXT
- **Files:** `SdlInputBridge.cpp:961–1001`
- **Work:** Verify "xinput" when vendor==product==0; else 8-char little-endian hex; Valve overrides
  (PS4/PS5/Xbox); disconnected → empty.
- **Acceptance:** Matches existing GUID cases.
- **Tests:** existing `FakeGamepadGuidFormat` + `FormatGUID` cases.
- **Deps:** none.

#### INPUT-GAMEPAD-017 — `GetGyroEXT`
- **Priority:** P3 · **Status:** TODO · **Area:** GamePad/EXT
- **Files:** `SdlInputBridge.cpp:894`
- **Work:** Verify sensor enabled on first read, 3 floats returned; failure → Zero+false; unsupported → false.
- **Acceptance:** Matches existing cases.
- **Tests:** existing gyro cases.
- **Deps:** none.

#### INPUT-GAMEPAD-018 — `GetAccelerometerEXT`
- **Priority:** P3 · **Status:** TODO · **Area:** GamePad/EXT
- **Files:** `SdlInputBridge.cpp`
- **Work:** Same as gyro for accelerometer.
- **Acceptance:** Matches existing cases.
- **Tests:** existing accel cases.
- **Deps:** none.

#### INPUT-GAMEPAD-019 — All 21 buttons → `Buttons` flags
- **Priority:** P1 · **Status:** TODO · **Area:** GamePad/Bridge
- **Files:** `SdlInputBridge.cpp:271–320`
- **Work:** Verify every SDL button maps to its XNA/EXT flag (A/B/X/Y, shoulders, sticks, dpad, guide→BigButton,
  misc1/paddles/touchpad EXT).
- **Acceptance:** All 21 mapped; EXT flagged.
- **Tests:** existing `EverySdlButtonMapsToTheExpectedXnaButton`.
- **Deps:** none.

#### INPUT-GAMEPAD-020 — DPad mapping
- **Priority:** P3 · **Status:** TODO · **Area:** GamePad/Bridge
- **Files:** `SdlInputBridge.cpp`, `GamePadDPad.cpp`
- **Work:** Verify dpad buttons populate `GamePadDPad` + `Buttons` bits.
- **Acceptance:** Correct.
- **Tests:** existing dpad cases.
- **Deps:** none.

#### INPUT-GAMEPAD-021 — Thumbstick X/Y + Y sign
- **Priority:** P2 · **Status:** TODO · **Area:** GamePad/Bridge
- **Files:** `SdlInputBridge.cpp:343–355,1549–1566`
- **Work:** Verify normalize ranges and Y inversion (SDL down-positive → XNA up-positive).
- **Acceptance:** Sign + range correct.
- **Tests:** existing axis Y-inversion case.
- **Deps:** none.

#### INPUT-GAMEPAD-022 — Trigger normalization
- **Priority:** P3 · **Status:** TODO · **Area:** GamePad/Bridge
- **Files:** `SdlInputBridge.cpp`
- **Work:** Verify triggers `/32767` clamp [0,1].
- **Acceptance:** Correct.
- **Tests:** existing trigger normalization case.
- **Deps:** none.

#### INPUT-GAMEPAD-023 — Dead-zone math (IndependentAxes)
- **Priority:** P2 · **Status:** TODO · **Area:** GamePad
- **Files:** `GamePadThumbSticks.cpp`, `GamePad.cpp`
- **Work:** Verify independent-axis exclude+clamp vs FNA formula.
- **Acceptance:** Matches FNA.
- **Tests:** existing thumbstick independent cases.
- **Deps:** none.

#### INPUT-GAMEPAD-024 — Dead-zone math (Circular)
- **Priority:** P2 · **Status:** TODO · **Area:** GamePad
- **Files:** `GamePadThumbSticks.cpp`
- **Work:** Verify circular rescale outside radius, zero inside, clamp to unit circle.
- **Acceptance:** Matches FNA.
- **Tests:** existing circular cases.
- **Deps:** none.

#### INPUT-GAMEPAD-025 — Dead-zone boundary values
- **Priority:** P2 · **Status:** TODO · **Area:** GamePad
- **Files:** `GamePad.cpp`, `GamePadThumbSticks.cpp`
- **Work:** Add tests exactly at `LeftDeadZone`/`RightDeadZone`/`TriggerThreshold` boundaries (just inside/outside).
- **Acceptance:** Boundary behavior pinned.
- **Tests:** new boundary cases.
- **Deps:** INPUT-API-012.

#### INPUT-GAMEPAD-026 — Raw axis noise / `GetRawGamePadState`
- **Priority:** P2 · **Status:** TODO · **Area:** GamePad
- **Files:** `InputManager.cpp:461`
- **Work:** Verify raw (no-deadzone) snapshot path returns unfiltered values incl. packet number.
- **Acceptance:** Raw path correct.
- **Tests:** new raw-state test.
- **Deps:** none.

#### INPUT-GAMEPAD-027 — PacketNumber semantics (bumps only on change)
- **Priority:** P2 · **Status:** TODO · **Area:** GamePad
- **Files:** `InputManager.cpp:225,247,298`
- **Work:** Verify packet bumps on button/axis/connection change; not on unchanged writes.
- **Acceptance:** Matches existing packet cases.
- **Tests:** existing `GamePadInputTest` packet case.
- **Deps:** none.

#### INPUT-GAMEPAD-028 — PacketNumber behavior on within-dead-zone wobble (task 916)
- **Priority:** P1 · **Status:** DONE (2026-07-05) · **Area:** GamePad
- **Files:** `tests/Microsoft/Xna/Framework/Input/GamePadInputTests.cpp`
- **Problem:** Deviation §18: per-field packet can bump on raw wobble even when visible XNA state is
  unchanged; currently `not asserted`.
- **Work:** Either make packet reflect visible (deadzoned) state, or add a test documenting current raw-bump
  behavior; record the decision.
- **Acceptance:** Behavior asserted by a test; decision recorded.
- **Tests:** new within-deadzone wobble test (via fake backend events).
- **Deps:** INPUT-GAMEPAD-030.
- **Result (2026-07-05):** **Decision: ACCEPT the raw-change bump** (no code change) — see DEC-04. Rationale:
  FNA leaves `PacketNumber` hardcoded to 0 (`GamePadState.cs:124`; SDL has no packet counter), so CNA's
  synthesized incrementing PacketNumber is a NOXNA enhancement, and bumping on any raw axis change
  (dead-zone-independent) matches XInput's `dwPacketNumber`. Pinned with
  `GamePadInputTest.PacketNumberBumpsOnWithinDeadZoneAxisWobbleWhileDeadZonedStateStaysAtRest`: two
  sub-dead-zone X values (0.05→0.15) leave the default-dead-zoned thumbstick at 0.0 both times, yet
  PacketNumber bumps (and `GetState(None)` confirms the raw value changed to 0.15). Used the direct
  `InputManager` axis API (like the sibling packet test), so INPUT-GAMEPAD-030 was not required. GamePad
  suite 73→74; input filter 273→274; order-independent.

#### INPUT-GAMEPAD-029 — Button/trigger threshold behavior (`IsButtonDown`)
- **Priority:** P3 · **Status:** TODO · **Area:** GamePad
- **Files:** `GamePadState.cpp` (`StickToButtons`, `TriggerThreshold`)
- **Work:** Verify triggers-as-buttons and thumbsticks-as-buttons thresholds vs FNA.
- **Acceptance:** Threshold parity.
- **Tests:** existing `GamePadStateTest` + boundary.
- **Deps:** none.

#### INPUT-GAMEPAD-030 — Deliver gamepad axis/button via synthetic SDL events through the fake
- **Priority:** P2 · **Status:** TODO · **Area:** GamePad/Test-infra
- **Files:** `tests/.../FakeSdlGamepadBackend.hpp`, bridge tests
- **Problem:** Some gamepad behaviors are tested at the device layer but not through `ProcessEvent` synthetic
  axis/button events end-to-end.
- **Work:** Add helpers to push synthetic `SDL_EVENT_GAMEPAD_*` and assert resulting `GamePadState`.
- **Acceptance:** End-to-end event→state path covered.
- **Tests:** new bridge gamepad tests.
- **Deps:** none.

#### INPUT-GAMEPAD-031 — `GamePadType` mapping from SDL joystick type
- **Priority:** P3 · **Status:** TODO · **Area:** GamePad
- **Files:** `SdlInputBridge.cpp` (`sdl_joystick_type_to_gamepad_type`)
- **Work:** Verify SDL types map to XNA `GamePadType` values.
- **Acceptance:** Mapping table tested.
- **Tests:** new type-mapping test via fake.
- **Deps:** INPUT-API-004.

#### INPUT-GAMEPAD-032 — Capability flags completeness (touchpad/gyro/accel/rgb/trigger-rumble)
- **Priority:** P2 · **Status:** TODO · **Area:** GamePad
- **Files:** `SdlInputBridge.cpp:1036`
- **Work:** Verify each EXT capability reads from the right SDL property/query; fake toggles each.
- **Acceptance:** Each flag independently verified.
- **Tests:** extend fake capability cases.
- **Deps:** none.

#### INPUT-GAMEPAD-033 — SDL `gamecontrollerdb` mapping assumptions
- **Priority:** P3 · **Status:** TODO · **Area:** GamePad/Platform
- **Files:** `docs/platform-input-notes.md`
- **Work:** Document reliance on SDL's built-in mapping DB and how unknown controllers behave.
- **Acceptance:** Documented.
- **Tests:** manual.
- **Deps:** none.

#### INPUT-GAMEPAD-034 — Fake backend conformance completeness
- **Priority:** P2 · **Status:** TODO · **Area:** GamePad/Test-infra
- **Files:** `tests/.../FakeSdlGamepadBackend.hpp`
- **Work:** Ensure the fake implements every `ISdlGamepadBackend` method with realistic behavior and counters;
  add any missing method parity vs the real backend.
- **Acceptance:** Fake ≡ interface; counters cover rumble/led/close/open.
- **Tests:** existing fake tests.
- **Deps:** none.

#### INPUT-GAMEPAD-035 — Manual hardware verification matrix (Xbox/PS/Switch/generic/BT)
- **Priority:** P1 · **Status:** TODO · **Area:** GamePad/Manual
- **Files:** `docs/demo-input-checklist.md`, `docs/input-manual-verification-results.md`
- **Problem:** Verification log says "Controller: None available"; all actuation is unverified.
- **Work:** With real controllers, verify buttons/axes/dpad/triggers, rumble, trigger-haptics, LED, sensors,
  GUID, hotplug per controller family; record date/hardware/OS/backend/SDL per row.
- **Acceptance:** A dated table with per-controller pass/fail.
- **Tests:** manual.
- **Deps:** INPUT-BUILD-004.

#### INPUT-GAMEPAD-036 — Steam Input / virtual controller caveats
- **Priority:** P3 · **Status:** TODO · **Area:** GamePad/Platform
- **Files:** `docs/platform-input-notes.md`
- **Work:** Document Steam Input remapping and virtual-controller behavior differences.
- **Acceptance:** Documented.
- **Tests:** manual.
- **Deps:** none.

#### INPUT-GAMEPAD-037 — Platform notes (Windows/Linux/macOS/Android/iOS)
- **Priority:** P3 · **Status:** TODO · **Area:** GamePad/Platform
- **Files:** `docs/platform-input-notes.md`
- **Work:** Per-platform gamepad backend notes (XInput/DInput, evdev, GameController, etc.).
- **Acceptance:** Per-platform section exists.
- **Tests:** manual.
- **Deps:** none.

---

## 10. Touch plan

Verified facts: `TouchPanel` static class; `MAX_TOUCHES=8`, `NO_FINGER=-1`; finger→touch id is a compact
sequential counter starting at **1** (not the SDL finger id); `FINGER_CANCELED` handled identically to
`FINGER_UP`; `ReadGesture` throws `System::InvalidOperationException` when empty; `GetState` prefers the
touches array and falls back to `InputManager`; previous-location recorded before Pressed→Moved promotion;
`TryGetPreviousLocation` does **not** write the out-param on the false path (deviation §18). The
`MaximumTouchCount` value (8 vs XNA/FNA 4) is the subject of a doc contradiction and an open decision.

#### INPUT-TOUCH-001 — `TouchPanel.DisplayWidth/Height` round-trip
- **Priority:** P3 · **Status:** TODO · **Area:** Touch
- **Files:** `TouchPanel.cpp`
- **Work:** Verify get/set; used by normalized→pixel scaling.
- **Acceptance:** Round-trips.
- **Tests:** existing `TouchInputTest`.
- **Deps:** none.

#### INPUT-TOUCH-002 — `TouchPanel.DisplayOrientation`
- **Priority:** P2 · **Status:** TODO · **Area:** Touch
- **Files:** `TouchPanel.cpp`
- **Problem:** `displayOrientation_` is stored but "not applied" (fidelity doc task 952 flag).
- **Work:** Decide whether orientation must transform touch coords; implement or document as no-op.
- **Acceptance:** Behavior defined + tested.
- **Tests:** new orientation case.
- **Deps:** none.

#### INPUT-TOUCH-003 — `TouchPanel.EnabledGestures` round-trip & filtering
- **Priority:** P2 · **Status:** TODO · **Area:** Touch/Gesture
- **Files:** `TouchPanel.cpp`, `GestureDetector.cpp`
- **Work:** Verify only enabled gestures are produced; round-trip get/set.
- **Acceptance:** Disabled gestures never enqueued.
- **Tests:** existing + a filtering case.
- **Deps:** none.

#### INPUT-TOUCH-004 — `IsGestureAvailable` semantics
- **Priority:** P3 · **Status:** TODO · **Area:** Touch/Gesture
- **Files:** `TouchPanel.cpp`
- **Work:** Verify true iff a gesture is queued; consistent with `ReadGesture`.
- **Acceptance:** Consistent.
- **Tests:** existing gesture FIFO case.
- **Deps:** none.

#### INPUT-TOUCH-005 — `TouchPanel.WindowHandle`
- **Priority:** P3 · **Status:** TODO · **Area:** Touch
- **Files:** `TouchPanel.cpp`
- **Work:** Verify uintptr_t round-trip; no SDL leak.
- **Acceptance:** Round-trips.
- **Tests:** new case + INPUT-API-030.
- **Deps:** none.

#### INPUT-TOUCH-006 — `GetCapabilities` connected/disconnected/no-side-effect
- **Priority:** P2 · **Status:** TODO · **Area:** Touch
- **Files:** `TouchPanel.cpp`
- **Work:** Verify connected reflects `touchDeviceExists`; disconnected → not connected, count 0; call has no
  side effects and survives reset.
- **Acceptance:** Matches existing edge cases.
- **Tests:** existing `TouchEdgeCaseTest` capability cases.
- **Deps:** INPUT-TOUCH-012.

#### INPUT-TOUCH-007 — `GetState` snapshot + fallback path
- **Priority:** P2 · **Status:** TODO · **Area:** Touch
- **Files:** `TouchPanel.cpp`, `InputManager.cpp:392`
- **Work:** Verify GetState returns a consistent snapshot; prefers touches array; falls back to InputManager.
- **Acceptance:** Matches existing cases.
- **Tests:** existing `TouchEdgeCaseTest`.
- **Deps:** none.

#### INPUT-TOUCH-008 — `ReadGesture` FIFO + empty throws
- **Priority:** P2 · **Status:** TODO · **Area:** Touch/Gesture
- **Files:** `TouchPanel.cpp:140`
- **Work:** Verify FIFO order and `InvalidOperationException` on empty.
- **Acceptance:** Matches existing case.
- **Tests:** existing "ReadGesture throws when empty".
- **Deps:** none.

#### INPUT-TOUCH-009 — `TouchLocation.TryGetPreviousLocation` (both paths)
- **Priority:** P2 · **Status:** DONE (2026-07-05; DEC-12 — match FNA, writes on false) · **Area:** Touch
- **Files:** `TouchLocation.cpp`
- **Problem:** No dedicated test; false-path out-param behavior is a deviation (§18).
- **Work:** Add true-path (returns prev) and false-path (returns false; document out-param non-write) tests.
- **Acceptance:** Both paths pinned.
- **Tests:** extend `TouchLocationTest`.
- **Deps:** INPUT-API-023.

#### INPUT-TOUCH-010 — Pressed/Moved/Released/Invalid transitions
- **Priority:** P2 · **Status:** TODO · **Area:** Touch
- **Files:** `InputManager.cpp`, `TouchPanel.cpp`
- **Work:** Verify Pressed→Moved auto-promotion in `GetState`, Released removed after snapshot, Invalid default.
- **Acceptance:** Transition lifecycle correct.
- **Tests:** existing `TouchEdgeCaseTest` promote/release cases.
- **Deps:** none.

#### INPUT-TOUCH-011 — Canceled finger handling
- **Priority:** P2 · **Status:** TODO · **Area:** Touch/Bridge
- **Files:** `SdlInputBridge.cpp:1426–1433`
- **Work:** Verify CANCELED releases like UP and frees the id; id reusable after.
- **Acceptance:** Matches existing cancel cases.
- **Tests:** existing `FingerCanceled*` cases.
- **Deps:** none.

#### INPUT-TOUCH-012 — Decide `MaximumTouchCount` policy (8 vs 4) — resolves doc contradiction
- **Priority:** P1 · **Status:** DONE (2026-07-05; DEC-09 — report 4, FNA-verified) · **Area:** Touch/Decision
- **Files:** `TouchPanel.cpp`, `TouchPanelCapabilities.cpp`, `docs/input-fna-fidelity.md`,
  `docs/input-backend.md`, `docs/xna-4-api-coverage.md`
- **Problem:** Direct contradiction: fidelity doc says reporting `MAX_TOUCHES (8)` is a **deviation** from
  XNA/FNA's 4; two other docs claim it **matches FNA**. Verify against FNA `TouchPanel.cs`.
- **Work:** Read FNA's actual reported `MaximumTouchCount`; pick strict-XNA(4) / FNA / explicit-CNA(8);
  align code + all three docs; add a test pinning the chosen value.
- **Acceptance:** One consistent value across code+docs+test; contradiction gone.
- **Tests:** capability value test.
- **Deps:** none.

#### INPUT-TOUCH-013 — Sequential CNA touch IDs vs SDL finger IDs
- **Priority:** P2 · **Status:** TODO · **Area:** Touch/Bridge/Decision
- **Files:** `SdlInputBridge.cpp:357–399`
- **Problem:** CNA uses a compact counter (from 1); FNA casts the SDL finger id (§18).
- **Work:** Confirm behavior; document deviation and its observable effects (id values differ from FNA).
- **Acceptance:** Deviation documented; id-allocation tested.
- **Tests:** existing reset/id-reuse cases.
- **Deps:** none.

#### INPUT-TOUCH-014 — Event-driven path max-touch cap decision
- **Priority:** P2 · **Status:** DONE (2026-07-05; DEC-10 — cap GetState at MAX_TOUCHES) · **Area:** Touch/Decision
- **Files:** `SdlInputBridge.cpp`, `InputManager.cpp`, `TouchPanel.cpp`
- **Problem:** Event path is uncapped; FNA implicitly 8 (§18). ">max touches all reported" is currently tested
  as allowed.
- **Work:** Decide cap-to-8 (strict) vs uncapped (document); align with INPUT-TOUCH-012.
- **Acceptance:** Cap policy decided + tested.
- **Tests:** existing >max case (kept or changed per decision).
- **Deps:** INPUT-TOUCH-012.

#### INPUT-TOUCH-015 — Display size zero behavior
- **Priority:** P2 · **Status:** TODO · **Area:** Touch
- **Files:** `TouchPanel.cpp` (scaling)
- **Work:** Verify zero display size does not divide-by-zero; defined fallback.
- **Acceptance:** Safe.
- **Tests:** existing "zero size" scaling case.
- **Deps:** none.

#### INPUT-TOUCH-016 — Normalized SDL coords → pixel coords + rounding
- **Priority:** P2 · **Status:** TODO · **Area:** Touch/Bridge
- **Files:** `SdlInputBridge.cpp`, `TouchPanel.cpp`
- **Work:** Verify 0..1 → pixel using display size; rounding rule for non-integer results.
- **Acceptance:** Matches existing scaling cases (pixel position, resized, non-integer rounding).
- **Tests:** existing scaling cases.
- **Deps:** none.

#### INPUT-TOUCH-017 — Multi-touch deterministic ordering
- **Priority:** P2 · **Status:** TODO · **Area:** Touch
- **Files:** `InputManager.cpp:392` (sort by id)
- **Work:** Verify `GetState` emits touches sorted by id ascending, deterministically.
- **Acceptance:** Deterministic order.
- **Tests:** existing multi-id order case.
- **Deps:** none.

#### INPUT-TOUCH-018 — `TouchCollection.CopyTo` bounds & empty behavior
- **Priority:** P3 · **Status:** TODO · **Area:** Touch
- **Files:** `TouchCollection.cpp`
- **Work:** Verify CopyTo throws out-of-range on bad index/size; empty collection safe.
- **Acceptance:** Matches existing cases.
- **Tests:** existing `TouchCollectionTest`.
- **Deps:** none.

#### INPUT-TOUCH-019 — Touch device capability detection
- **Priority:** P3 · **Status:** TODO · **Area:** Touch/Bridge
- **Files:** `SdlInputBridge.cpp` (`setTouchDeviceExistsProperty`)
- **Work:** Verify first finger event marks device present; reset clears it.
- **Acceptance:** Detection correct.
- **Tests:** existing capability-after-reset case.
- **Deps:** none.

#### INPUT-TOUCH-020 — Manual: real touchscreen
- **Priority:** P2 · **Status:** TODO · **Area:** Touch/Manual
- **Files:** `docs/input-manual-verification-results.md`
- **Work:** Verify multi-touch on real hardware; record date/hardware.
- **Acceptance:** Dated entry.
- **Tests:** manual.
- **Deps:** INPUT-BUILD-004.

#### INPUT-TOUCH-021 — Android/iOS touch behavior
- **Priority:** P3 · **Status:** TODO · **Area:** Touch/Platform
- **Files:** `docs/platform-input-notes.md`
- **Work:** Document mobile touch specifics (coordinate space, orientation, gesture timing).
- **Acceptance:** Documented.
- **Tests:** manual.
- **Deps:** none.

#### INPUT-TOUCH-022 — Desktop simulated touch (if available)
- **Priority:** P3 · **Status:** TODO · **Area:** Touch/Test
- **Files:** `SdlInputBridge.cpp`
- **Work:** If SDL exposes mouse-as-touch or a synthetic device, add a smoke path.
- **Acceptance:** Documented availability; smoke test if feasible.
- **Tests:** optional.
- **Deps:** none.

#### INPUT-TOUCH-023 — `TouchPanel.Update()` copy-order deviation
- **Priority:** P3 · **Status:** DONE (2026-07-05; DEC-13 — confirmed inert) · **Area:** Touch
- **Files:** `TouchPanel.cpp`
- **Problem:** Copies current→previous before gesture update (FNA order reversed; documented inert, §18).
- **Work:** Confirm inert; document; guard with a test if any observable difference exists.
- **Acceptance:** Confirmed inert or difference found+addressed.
- **Tests:** new ordering case.
- **Deps:** none.

#### INPUT-TOUCH-024 — Coordinate-basis consistency (gesture vs renderer-logical) (task 952)
- **Priority:** P2 · **Status:** TODO · **Area:** Touch/Gesture
- **Files:** `SdlInputBridge.cpp`, `TouchPanel.cpp`, `GestureDetector.cpp`
- **Problem:** Fidelity doc flags that gesture path coords vs renderer-logical basis need verification.
- **Work:** Verify gestures and touch state share one coordinate basis; add a test with letterboxing.
- **Acceptance:** Single consistent basis, tested.
- **Tests:** new letterbox gesture-coordinate test.
- **Deps:** none.

#### INPUT-TOUCH-025 — `TouchPanelCapabilities` equality/ToString policy
- **Priority:** P3 · **Status:** TODO · **Area:** Touch
- **Files:** `TouchPanelCapabilities.cpp`
- **Work:** Confirm FNA lacks equality/ToString on this struct; record; no need to add unless FNA has it.
- **Acceptance:** Policy recorded.
- **Tests:** existing.
- **Deps:** INPUT-API-025.

---

## 11. Gesture plan

Verified facts (`GestureDetector.cpp`): states `NONE/HOLDING/HELD/JUST_TAPPED/DRAGGING_{FREE,H,V}/PINCHING`;
constants `MOVE_THRESHOLD=35`, `MIN_FLICK_VELOCITY=100.0f`; DoubleTap window `≤300ms` AND dist `≤35`;
Tap `hold<1s`; Hold `≥1s`; Flick `dist>35 && velocity≥100`; velocity EMA `v += (inst-v)*0.45`, inst=`d/(0.001+dt)`.
Injectable test clock (epoch+1h baseline). DragComplete now has 5 dedicated tests (INPUT-GESTURE-007);
gesture-interruption-by-second-finger and mid-drag cancellation now have 4 tests (INPUT-GESTURE-011/012,
done 2026-07-05) — all three areas previously had ZERO coverage. **Observed behavior pinned by the new
tests:** a second finger mid-drag (with Pinch enabled) converts the drag to a pinch, so its release
reports `PinchComplete`, not `DragComplete`; and because the bridge maps `FINGER_CANCELED`→`Released`,
a canceled drag is indistinguishable from a normal lift at the detector and does emit `DragComplete`
(the detector recovers cleanly — no stuck state).

#### INPUT-GESTURE-001 — Tap
- **Priority:** P3 · **Status:** TODO · **Area:** Gesture
- **Files:** `GestureDetector.cpp`, tests
- **Work:** Verify Tap fires on quick down/up under thresholds; position correct.
- **Acceptance:** Matches existing case.
- **Tests:** existing `GestureDetectorTest.Tap` + bridge Tap.
- **Deps:** none.

#### INPUT-GESTURE-002 — DoubleTap (positive + timing negative)
- **Priority:** P3 · **Status:** TODO · **Area:** Gesture
- **Files:** `GestureDetector.cpp`
- **Work:** Verify fires within 300ms & near; does NOT fire after window.
- **Acceptance:** Matches existing cases.
- **Tests:** existing DoubleTap cases.
- **Deps:** none.

#### INPUT-GESTURE-003 — Hold (positive + before-1s negative)
- **Priority:** P3 · **Status:** TODO · **Area:** Gesture
- **Files:** `GestureDetector.cpp`
- **Work:** Verify Hold ≥1s; not before.
- **Acceptance:** Matches existing cases.
- **Tests:** existing Hold cases.
- **Deps:** none.

#### INPUT-GESTURE-004 — FreeDrag
- **Priority:** P3 · **Status:** TODO · **Area:** Gesture
- **Files:** `GestureDetector.cpp`
- **Work:** Verify diagonal drag classified FreeDrag; delta values.
- **Acceptance:** Matches existing case.
- **Tests:** existing FreeDrag case.
- **Deps:** none.

#### INPUT-GESTURE-005 — HorizontalDrag
- **Priority:** P3 · **Status:** TODO · **Area:** Gesture
- **Files:** `GestureDetector.cpp`
- **Work:** Verify horizontal-dominant drag classification + delta.
- **Acceptance:** Matches existing case.
- **Tests:** existing HorizontalDrag case.
- **Deps:** none.

#### INPUT-GESTURE-006 — VerticalDrag
- **Priority:** P3 · **Status:** TODO · **Area:** Gesture
- **Files:** `GestureDetector.cpp`
- **Work:** Verify vertical-dominant drag classification + delta.
- **Acceptance:** Matches existing case.
- **Tests:** existing VerticalDrag case.
- **Deps:** none.

#### INPUT-GESTURE-007 — DragComplete (was MISSING coverage)
- **Priority:** P1 · **Status:** DONE (2026-07-05) · **Area:** Gesture
- **Files:** `GestureDetector.cpp`, `tests/.../GestureDetectorTests.cpp`
- **Problem:** DragComplete is implemented but had **no test** (grep-confirmed zero references).
- **Work:** Add tests: drag then release emits DragComplete; only when a drag was in progress; correct
  position/delta; enabled-gesture filtering.
- **Acceptance:** DragComplete covered for positive + negative (no drag → no DragComplete).
- **Tests:** new `GestureDetectorTest.DragComplete*`.
- **Deps:** none.
- **Result (2026-07-05):** Added 5 `GestureDetectorTest.DragComplete*` cases — FreeDrag→release (asserts
  type, release fingerId, zero position/delta), HorizontalDrag→release (fingerId), release-without-drag
  (none), sub-`MOVE_THRESHOLD` move (none), and gesture-not-enabled filter (none). Gesture suite 24→29;
  input filter 259→264 on EasyGL; order-independent under `--gtest_shuffle --gtest_repeat=3`; no
  regressions. No detector code changed (behavior was already correct).

#### INPUT-GESTURE-008 — Flick (positive + insufficient-movement negative)
- **Priority:** P3 · **Status:** TODO · **Area:** Gesture
- **Files:** `GestureDetector.cpp`
- **Work:** Verify Flick fires on velocity≥100 & dist>35; not without movement; velocity vector value.
- **Acceptance:** Matches existing cases.
- **Tests:** existing Flick cases + a velocity-value assertion.
- **Deps:** none.

#### INPUT-GESTURE-009 — Pinch + PinchComplete
- **Priority:** P2 · **Status:** TODO · **Area:** Gesture
- **Files:** `GestureDetector.cpp`
- **Work:** Verify two-finger Pinch samples (position/position2/delta/delta2) and PinchComplete on release.
- **Acceptance:** Matches existing case + delta2 assertions.
- **Tests:** existing Pinch case + extended.
- **Deps:** none.

#### INPUT-GESTURE-010 — Multi-finger transitions
- **Priority:** P2 · **Status:** TODO · **Area:** Gesture
- **Files:** `GestureDetector.cpp`
- **Work:** Verify single-finger drag → second finger down → PINCHING transition; and back on lift.
- **Acceptance:** Transitions correct.
- **Tests:** new multi-finger transition test.
- **Deps:** none.

#### INPUT-GESTURE-011 — Gesture interruption by second finger mid-drag (was MISSING)
- **Priority:** P1 · **Status:** DONE (2026-07-05) · **Area:** Gesture
- **Files:** `GestureDetector.cpp`
- **Problem:** No test for a drag interrupted by a second finger arriving mid-drag.
- **Work:** Add tests for drag→pinch interruption and the resulting emitted samples/order.
- **Acceptance:** Interruption behavior pinned.
- **Tests:** new interruption tests.
- **Deps:** none.
- **Result (2026-07-05):** 3 `GestureDetectorTest` cases — `SecondFingerDuringADragInterruptsItAndBecomesAPinch`
  (asserts the next sample is a Pinch with both finger ids and the expected position2/delta),
  `DragInterruptedByASecondFingerReportsPinchCompleteNotDragComplete`, and
  `GestureStateRecoversAfterADragEndsSoAFreshTapStillFires`. No detector code changed.

#### INPUT-GESTURE-012 — Gesture cancellation (finger cancel mid-gesture)
- **Priority:** P2 · **Status:** DONE (2026-07-05) · **Area:** Gesture/Bridge
- **Files:** `SdlInputBridge.cpp`, `GestureDetector.cpp`
- **Work:** Verify a `FINGER_CANCELED` mid-gesture terminates cleanly (no stuck HOLDING/DRAGGING).
- **Acceptance:** Clean termination.
- **Tests:** new cancel-mid-gesture test.
- **Deps:** none.
- **Result (2026-07-05):** `SdlInputBridgeTouchGestureTest.FingerCanceledMidDragRecoversAndAllowsAFreshTap`
  drives a real `SDL_EVENT_FINGER_CANCELED` mid-FreeDrag through `ProcessEvent`, then proves a fresh
  independent finger still produces a Tap (state machine not wedged). Documented finding: a canceled drag
  emits `DragComplete` because the bridge maps cancel→Released (detector can't distinguish) — recovery is
  still clean. No code changed.

#### INPUT-GESTURE-013 — Time thresholds parameterized
- **Priority:** P2 · **Status:** TODO · **Area:** Gesture
- **Files:** `GestureDetector.cpp`
- **Work:** Parameterized tests just inside/outside 300ms (DoubleTap) and 1s (Hold/Tap) via the test clock.
- **Acceptance:** Boundaries pinned.
- **Tests:** new parameterized timing tests.
- **Deps:** none.

#### INPUT-GESTURE-014 — Distance thresholds parameterized
- **Priority:** P2 · **Status:** TODO · **Area:** Gesture
- **Files:** `GestureDetector.cpp`
- **Work:** Parameterized tests at `MOVE_THRESHOLD=35` boundary for drag start / flick / doubletap distance.
- **Acceptance:** Boundaries pinned.
- **Tests:** new parameterized distance tests.
- **Deps:** none.

#### INPUT-GESTURE-015 — Velocity calculation correctness
- **Priority:** P2 · **Status:** TODO · **Area:** Gesture
- **Files:** `GestureDetector.cpp:408–409`
- **Work:** Verify EMA velocity with controlled dt via the test clock; assert Flick velocity vector.
- **Acceptance:** Velocity math pinned.
- **Tests:** new velocity test.
- **Deps:** none.

#### INPUT-GESTURE-016 — Position/delta values across gestures
- **Priority:** P2 · **Status:** TODO · **Area:** Gesture
- **Files:** `GestureDetector.cpp`
- **Work:** Assert `position/position2/delta/delta2` for drag/pinch/flick samples (not just gesture type).
- **Acceptance:** Sample payloads pinned.
- **Tests:** extend gesture tests.
- **Deps:** none.

#### INPUT-GESTURE-017 — Queued gesture ordering
- **Priority:** P3 · **Status:** TODO · **Area:** Gesture
- **Files:** `TouchPanel.cpp` (queue), `GestureDetector.cpp`
- **Work:** Verify multiple gestures dequeue in FIFO order.
- **Acceptance:** FIFO preserved.
- **Tests:** existing enqueue/read case.
- **Deps:** none.

#### INPUT-GESTURE-018 — `ReadGesture` exception when empty
- **Priority:** P3 · **Status:** TODO · **Area:** Gesture
- **Files:** `TouchPanel.cpp:140`
- **Work:** Covered by INPUT-TOUCH-008; ensure gesture suite also asserts it.
- **Acceptance:** Exception asserted.
- **Tests:** existing.
- **Deps:** INPUT-TOUCH-008.

#### INPUT-GESTURE-019 — `EnabledGestures` filtering across all types
- **Priority:** P2 · **Status:** TODO · **Area:** Gesture
- **Files:** `GestureDetector.cpp`
- **Work:** For each gesture type, verify it is suppressed when not enabled and produced when enabled.
- **Acceptance:** Per-type filtering matrix.
- **Tests:** new parameterized filtering test.
- **Deps:** none.

#### INPUT-GESTURE-020 — FNA comparison + document deviations
- **Priority:** P2 · **Status:** TODO · **Area:** Gesture/Docs
- **Files:** FNA `GestureDetector.cs`, `docs/input-fna-fidelity.md`
- **Work:** Diff CNA thresholds/state machine vs FNA; record any intentional deviation (e.g. constant values,
  velocity EMA factor).
- **Acceptance:** Deviation list recorded.
- **Tests:** n/a.
- **Deps:** none.

---

## 12. TextInputEXT plan

Verified facts (`TextInputEXT`, FNA extension, entire class NOXNA): callbacks are single `std::function`
(`TextInput(charcs)`, `TextEditing(const std::string&,int,int)`); window handle as `uintptr_t`; UTF-8
decoded to UTF-16 per code unit (astral → surrogate pair); malformed UTF-8 **skipped** (not U+FFFD); empty
composition emits `("",0,0)`; control-char synthesis table for Home/End/Back/Tab/Enter/Delete/Ctrl+V with a
repeat gate and a suppress flag for the literal 'v' after Ctrl+V.

#### INPUT-TEXT-001 — `StartTextInput`/`StopTextInput`
- **Priority:** P3 · **Status:** TODO · **Area:** Text/EXT
- **Files:** `TextInputEXT.cpp`
- **Work:** Verify start/stop no-op safely without a window; with a real window (GTEST_SKIP fallback) toggles SDL.
- **Acceptance:** Matches existing cases.
- **Tests:** existing `TextInputEXTTest`.
- **Deps:** none.

#### INPUT-TEXT-002 — `TextInput` callback dispatch
- **Priority:** P2 · **Status:** TODO · **Area:** Text/EXT
- **Files:** `TextInputEXT.cpp`, `SdlInputBridge.cpp:1346`
- **Work:** Verify each UTF-16 code unit dispatched; no-subscriber safe.
- **Acceptance:** Matches existing cases.
- **Tests:** existing text-input cases.
- **Deps:** none.

#### INPUT-TEXT-003 — `TextEditing` callback dispatch
- **Priority:** P2 · **Status:** TODO · **Area:** Text/EXT
- **Files:** `TextInputEXT.cpp`, `SdlInputBridge.cpp:1368`
- **Work:** Verify IME draft (string,start,length) dispatched; empty composition `("",0,0)`; no-subscriber safe.
- **Acceptance:** Matches existing cases.
- **Tests:** existing text-editing cases.
- **Deps:** none.

#### INPUT-TEXT-004 — `SetInputRectangle`
- **Priority:** P3 · **Status:** TODO · **Area:** Text/EXT
- **Files:** `TextInputEXT.cpp`
- **Work:** Verify no-op without window; forwards to `SDL_SetTextInputArea` with window.
- **Acceptance:** Safe + forwards.
- **Tests:** existing set-rectangle no-op case.
- **Deps:** none.

#### INPUT-TEXT-005 — Window handle round-trip + null behavior
- **Priority:** P3 · **Status:** TODO · **Area:** Text/EXT
- **Files:** `TextInputEXT.cpp`
- **Work:** Verify uintptr_t get/set; `IsTextInputActive`/`IsScreenKeyboardShown` false without window.
- **Acceptance:** Matches existing cases.
- **Tests:** existing cases.
- **Deps:** none.

#### INPUT-TEXT-006 — UTF-8 → UTF-16 conversion (1/2/3-byte)
- **Priority:** P2 · **Status:** TODO · **Area:** Text/Bridge
- **Files:** `SdlInputBridge.cpp` (`decode_utf8_to_utf16`)
- **Work:** Verify ASCII, 2-byte, 3-byte decode to correct code units; ordering preserved.
- **Acceptance:** Matches existing cases.
- **Tests:** existing UTF-8 cases.
- **Deps:** none.

#### INPUT-TEXT-007 — Surrogate pairs (astral emoji)
- **Priority:** P2 · **Status:** TODO · **Area:** Text/Bridge
- **Files:** `SdlInputBridge.cpp`
- **Work:** Verify astral code point emits a high+low surrogate pair (two callbacks).
- **Acceptance:** Matches existing emoji case.
- **Tests:** existing astral case.
- **Deps:** none.

#### INPUT-TEXT-008 — Malformed UTF-8 handling (skipped vs U+FFFD)
- **Priority:** P2 · **Status:** DONE (2026-07-05; DEC-08 — match FNA, U+FFFD) · **Area:** Text/Bridge/Decision
- **Files:** `SdlInputBridge.cpp`, `docs/input-fna-fidelity.md`
- **Problem:** CNA skips malformed bytes; FNA emits replacement U+FFFD (§18).
- **Work:** Decide skip vs replacement; if keeping skip, add explicit malformed-input test + document.
- **Acceptance:** Decision recorded; behavior tested.
- **Tests:** new malformed-UTF-8 test.
- **Deps:** none.

#### INPUT-TEXT-009 — Control character synthesis
- **Priority:** P2 · **Status:** TODO · **Area:** Text/Bridge
- **Files:** `SdlInputBridge.cpp:90–207`
- **Work:** Verify Home(2)/End(3)/Back(8)/Tab(9)/Enter(13)/Delete(127) synthesized from key events.
- **Acceptance:** Matches existing case.
- **Tests:** existing control-keys case.
- **Deps:** none.

#### INPUT-TEXT-010 — Repeat gate for synthesized text
- **Priority:** P2 · **Status:** DONE (2026-07-05; DEC-19 — matches FNA) · **Area:** Text/Bridge
- **Files:** `SdlInputBridge.cpp`
- **Work:** Verify key repeat re-emits control text (documented deviation: gates on SDL repeat flag vs FNA
  tracked-membership, §18).
- **Acceptance:** Matches existing key-repeat case; deviation documented.
- **Tests:** existing key-repeat re-emit case.
- **Deps:** none.

#### INPUT-TEXT-011 — Ctrl+V synthesis + literal-'v' suppression
- **Priority:** P2 · **Status:** TODO · **Area:** Text/Bridge
- **Files:** `SdlInputBridge.cpp` (index 6 Ctrl+V, `g_textInputSuppress`)
- **Work:** Verify Ctrl+V emits char 22 and suppresses the following literal 'v' TEXT_INPUT; plain 'v' not suppressed.
- **Acceptance:** Matches existing cases + edge cases.
- **Tests:** existing Ctrl+V cases.
- **Deps:** none.

#### INPUT-TEXT-012 — IME composition (manual)
- **Priority:** P2 · **Status:** TODO · **Area:** Text/Manual
- **Files:** `docs/input-manual-verification-results.md`
- **Work:** Manually verify IME composition (TextEditing) on real IME; record date/hardware/OS.
- **Acceptance:** Dated entry.
- **Tests:** manual.
- **Deps:** INPUT-BUILD-004.

#### INPUT-TEXT-013 — Czech keyboard manual verification
- **Priority:** P3 · **Status:** TODO · **Area:** Text/Manual
- **Files:** `docs/input-manual-verification-results.md`
- **Work:** Verify Czech diacritics text entry on real keyboard.
- **Acceptance:** Dated entry.
- **Tests:** manual (automated decode already covered).
- **Deps:** none.

#### INPUT-TEXT-014 — CJK IME manual verification (if practical)
- **Priority:** P3 · **Status:** TODO · **Area:** Text/Manual
- **Files:** `docs/input-manual-verification-results.md`
- **Work:** Verify Japanese/Chinese/Korean composition if an IME is available.
- **Acceptance:** Dated entry or explicit "not available".
- **Tests:** manual.
- **Deps:** none.

#### INPUT-TEXT-015 — Callback single vs multicast decision
- **Priority:** P2 · **Status:** DONE (2026-07-05; DEC-06 — multicast) · **Area:** Text/EXT/Decision
- **Files:** `TextInputEXT.hpp`, `docs/input-fna-fidelity.md`
- **Problem:** Single-subscriber `std::function` vs FNA multicast (§18); a second subscriber overwrites.
- **Work:** Decide keep-single (document) vs `System::EventHandler`/multicast. Implement chosen.
- **Acceptance:** Decision recorded; multi-subscriber test if multicast.
- **Tests:** subscriber test.
- **Deps:** INPUT-MOUSE-014 (shared decision on EXT callback style).

#### INPUT-TEXT-016 — Threading assumptions + UTF-8 byte-indexing note
- **Priority:** P3 · **Status:** TODO · **Area:** Text/Docs
- **Files:** `docs/input-fna-fidelity.md`
- **Problem:** `TextEditing` string is UTF-8 (byte-indexed) vs FNA UTF-16 (§18); single-thread pump.
- **Work:** Document the byte-vs-UTF-16 index difference and threading model.
- **Acceptance:** Documented.
- **Tests:** n/a.
- **Deps:** none.

---

## 13. Internal InputManager plan

Verified facts (`InputManager`): singleton via function-static; holds mouse (with relative deltas), 4
gamepad slots (buttons/axes/packet), pressed-keys set, touch map; `GetMouseState` drains relative deltas;
`GetTouchState` sorts by id, records previous before Pressed→Moved promotion, removes Released after
snapshot; packet bumps only on real change; `ResetAllForTests` runs a deterministic reset chain
(bridge → manager → touchpanel → gesture → mouse → textinput). Unsynchronized single-thread by design.
No focus-loss clearing.

#### INPUT-BRIDGE-101 — Snapshot consistency across `Get*State`
- **Priority:** P2 · **Status:** TODO · **Area:** InputManager
- **Files:** `InputManager.cpp`
- **Work:** Verify each snapshot is internally consistent and independent of later mutations.
- **Acceptance:** Snapshots immutable.
- **Tests:** existing input tests (+ explicit).
- **Deps:** none.

#### INPUT-BRIDGE-102 — Event ordering into state
- **Priority:** P2 · **Status:** TODO · **Area:** InputManager
- **Files:** `InputManager.cpp`, `SdlInputBridge.cpp`
- **Work:** Verify last-writer-wins for same-frame events; ordering deterministic.
- **Acceptance:** Deterministic.
- **Tests:** new ordering test.
- **Deps:** none.

#### INPUT-BRIDGE-103 — Mouse delta draining
- **Priority:** P2 · **Status:** TODO · **Area:** InputManager
- **Files:** `InputManager.cpp:341`
- **Work:** Verify relative deltas drain to 0 on read; accumulation between reads.
- **Acceptance:** Matches existing relative cases.
- **Tests:** existing relative-mode cases.
- **Deps:** none.

#### INPUT-BRIDGE-104 — Touch snapshot lifecycle
- **Priority:** P2 · **Status:** TODO · **Area:** InputManager
- **Files:** `InputManager.cpp:392`
- **Work:** Verify previous-before-promote, remove-after-release ordering (the previous-location bug fix).
- **Acceptance:** Lifecycle correct.
- **Tests:** existing `TouchEdgeCaseTest`.
- **Deps:** none.

#### INPUT-BRIDGE-105 — Gamepad slot state isolation
- **Priority:** P3 · **Status:** TODO · **Area:** InputManager
- **Files:** `InputManager.cpp`
- **Work:** Verify 4 slots independent; disconnect resets one slot only.
- **Acceptance:** Isolation held.
- **Tests:** existing mapping tests.
- **Deps:** none.

#### INPUT-BRIDGE-106 — Packet-number update rules
- **Priority:** P2 · **Status:** TODO · **Area:** InputManager
- **Files:** `InputManager.cpp:225,247,298`
- **Work:** Covered by INPUT-GAMEPAD-027/028; ensure manager-level unit tests exist.
- **Acceptance:** Rules pinned at manager level.
- **Tests:** existing/extended.
- **Deps:** INPUT-GAMEPAD-028.

#### INPUT-BRIDGE-107 — `ResetAllForTests` determinism & idempotency
- **Priority:** P1 · **Status:** TODO · **Area:** InputManager/Test-infra
- **Files:** `InputManager.cpp:124`
- **Work:** Verify reset chain returns every input static to baseline, restores real gamepad backend,
  is idempotent, and keeps tests order-independent.
- **Acceptance:** Matches existing reset cases; shuffle-stable.
- **Tests:** existing `InputResetAllForTests` + shuffle.
- **Deps:** none.

#### INPUT-BRIDGE-108 — Test-only backend separation
- **Priority:** P2 · **Status:** TODO · **Area:** InputManager/Test-infra
- **Files:** `SdlGamepadBackend.cpp`, `InputManager.cpp`
- **Work:** Verify production path always uses the real backend; fake is test-only and restored by reset.
- **Acceptance:** No fake leakage across tests.
- **Tests:** existing reset restores-real case.
- **Deps:** none.

#### INPUT-BRIDGE-109 — Focus-loss / window-destruction clearing (decision)
- **Priority:** P1 · **Status:** DONE (2026-07-05; DEC-15 accepted — match FNA, no `ClearTransientState`) · **Area:** InputManager/Bridge/Decision
- **Files:** `InputManager.cpp`, `SdlInputBridge.cpp`, `Game.cpp`
- **Problem:** No transient-state clear on focus loss or window destruction (§18); event-driven CNA can leave
  a stuck key/button.
- **Work:** Add optional `ClearTransientState()`; decide when to call (focus lost / window destroy) vs
  match-FNA-and-document.
- **Acceptance:** Decision recorded; behavior tested.
- **Tests:** new focus-loss/window-destroy test.
- **Deps:** INPUT-KBD-020.

#### INPUT-BRIDGE-110 — Main-thread assumption (assert or document)
- **Priority:** P2 · **Status:** TODO · **Area:** InputManager
- **Files:** `InputManager.hpp`
- **Work:** Per INPUT-BUILD-007, either add a debug thread-id assert on mutators or document firmly.
- **Acceptance:** Decision applied.
- **Tests:** n/a / debug assert.
- **Deps:** INPUT-BUILD-007.

#### INPUT-BRIDGE-111 — Race/thread-safety policy statement
- **Priority:** P3 · **Status:** TODO · **Area:** InputManager/Docs
- **Files:** `docs/input-backend.md` §6
- **Work:** State the "no locking, single game-loop thread" policy authoritatively; link from InputManager.hpp.
- **Acceptance:** One authoritative statement.
- **Tests:** n/a.
- **Deps:** none.

#### INPUT-BRIDGE-112 — Deterministic test backend for time (gesture clock)
- **Priority:** P3 · **Status:** TODO · **Area:** InputManager/Test-infra
- **Files:** `GestureDetector.cpp` (test clock)
- **Work:** Verify the test clock fully isolates gesture timing; reset restores real clock.
- **Acceptance:** No wall-clock dependence in gesture tests.
- **Tests:** existing gesture tests under shuffle/repeat.
- **Deps:** none.

---

## 14. SDL bridge plan

Verified facts: `SdlInputBridge::ProcessEvent` is the single funnel; handles mouse motion/button/wheel,
key down/up, text input/editing, finger down/motion/up/canceled, gamepad added/removed/button/axis;
`default: break` for everything else (no window-focus, keymap-changed, low-memory handling). Includes
`<SDL3/SDL.h>` and exposes only `ProcessEvent(const SDL_Event&)` as its raw-SDL public surface; all
gamepad SDL calls route through `ISdlGamepadBackend`.

#### INPUT-BRIDGE-001 — SDL event coverage audit
- **Priority:** P2 · **Status:** TODO · **Area:** Bridge
- **Files:** `SdlInputBridge.cpp:1193–1571`
- **Work:** Enumerate handled vs unhandled `SDL_EVENT_*`; decide which unhandled ones matter (focus, keymap
  changed, display/orientation, quit).
- **Acceptance:** Coverage table; each unhandled-but-relevant event has a task.
- **Tests:** synthetic-event tests.
- **Deps:** none.

#### INPUT-BRIDGE-002 — SDL version assumptions
- **Priority:** P2 · **Status:** TODO · **Area:** Bridge/Build
- **Files:** `SdlInputBridge.cpp`, `docs/input-build-and-test.md`
- **Work:** Document the SDL3 APIs relied on (gamepad, sensors, `SDL_RenderCoordinatesFromWindow`, text input
  area); tie to the pinned SDL tag.
- **Acceptance:** Minimum SDL3 documented.
- **Tests:** build on pinned SDL.
- **Deps:** INPUT-BUILD-004.

#### INPUT-BRIDGE-003 — Key mapping funnel tests
- **Priority:** P2 · **Status:** TODO · **Area:** Bridge/Keyboard
- **Files:** `SdlInputBridge.cpp`
- **Work:** Covered by §7; ensure both keycode and scancode paths have synthetic-event coverage.
- **Acceptance:** Both paths covered.
- **Tests:** existing keyboard bridge tests.
- **Deps:** INPUT-KBD-009, INPUT-KBD-010.

#### INPUT-BRIDGE-004 — Mouse mapping funnel tests
- **Priority:** P3 · **Status:** TODO · **Area:** Bridge/Mouse
- **Files:** `SdlInputBridge.cpp`
- **Work:** Covered by §8; ensure button/wheel/motion synthetic coverage.
- **Acceptance:** Covered.
- **Tests:** existing mouse bridge tests.
- **Deps:** none.

#### INPUT-BRIDGE-005 — Gamepad mapping funnel tests
- **Priority:** P2 · **Status:** TODO · **Area:** Bridge/GamePad
- **Files:** `SdlInputBridge.cpp`
- **Work:** Ensure end-to-end synthetic gamepad events (INPUT-GAMEPAD-030) exercise the funnel.
- **Acceptance:** Covered.
- **Tests:** new bridge gamepad tests.
- **Deps:** INPUT-GAMEPAD-030.

#### INPUT-BRIDGE-006 — Touch mapping funnel tests
- **Priority:** P3 · **Status:** TODO · **Area:** Bridge/Touch
- **Files:** `SdlInputBridge.cpp`
- **Work:** Covered by §10/§11; ensure finger down/motion/up/canceled synthetic coverage.
- **Acceptance:** Covered.
- **Tests:** existing touch/gesture bridge tests.
- **Deps:** none.

#### INPUT-BRIDGE-007 — Text input mapping funnel tests
- **Priority:** P3 · **Status:** TODO · **Area:** Bridge/Text
- **Files:** `SdlInputBridge.cpp`
- **Work:** Covered by §12; ensure text-input/editing synthetic coverage.
- **Acceptance:** Covered.
- **Tests:** existing text bridge tests.
- **Deps:** none.

#### INPUT-BRIDGE-008 — Sensor mapping (gyro/accel) via backend
- **Priority:** P3 · **Status:** TODO · **Area:** Bridge/GamePad
- **Files:** `SdlInputBridge.cpp:894`
- **Work:** Verify sensor enable-on-first-read and data read through the seam; failure handling.
- **Acceptance:** Covered via fake.
- **Tests:** existing sensor cases.
- **Deps:** none.

#### INPUT-BRIDGE-009 — Rumble mapping via backend
- **Priority:** P3 · **Status:** TODO · **Area:** Bridge/GamePad
- **Files:** `SdlInputBridge.cpp`
- **Work:** Verify rumble/trigger-rumble clamp and forward through the seam.
- **Acceptance:** Covered via fake.
- **Tests:** existing rumble cases.
- **Deps:** none.

#### INPUT-BRIDGE-010 — Light bar mapping via backend
- **Priority:** P3 · **Status:** TODO · **Area:** Bridge/GamePad
- **Files:** `SdlInputBridge.cpp`
- **Work:** Verify RGB forwards through the seam.
- **Acceptance:** Covered via fake.
- **Tests:** existing lightbar case.
- **Deps:** none.

#### INPUT-BRIDGE-011 — Error handling on SDL failures
- **Priority:** P2 · **Status:** TODO · **Area:** Bridge
- **Files:** `SdlInputBridge.cpp`
- **Work:** Verify graceful handling when SDL calls fail (open gamepad, sensor enable, warp) — no crash,
  defined fallback.
- **Acceptance:** No crash on failure paths.
- **Tests:** fake returning failures.
- **Deps:** none.

#### INPUT-BRIDGE-012 — Logging / debug diagnostics policy
- **Priority:** P3 · **Status:** TODO · **Area:** Bridge
- **Files:** `SdlInputBridge.cpp` (`__ANDROID__` logging blocks)
- **Work:** Review ad-hoc Android logging; standardize or gate behind a debug flag.
- **Acceptance:** Consistent logging policy.
- **Tests:** n/a.
- **Deps:** none.

#### INPUT-BRIDGE-013 — Backend abstraction boundary integrity
- **Priority:** P2 · **Status:** TODO · **Area:** Bridge/API
- **Files:** `SdlInputBridge.hpp`, `SdlGamepadBackend.hpp`
- **Work:** Confirm raw SDL types stay internal (bridge `ProcessEvent` and the gamepad seam only); no XNA
  header pulls these.
- **Acceptance:** Boundary intact (INPUT-API-030 guards).
- **Tests:** INPUT-API-030.
- **Deps:** none.

#### INPUT-BRIDGE-014 — Window focus / lifecycle event handling (decision)
- **Priority:** P1 · **Status:** DONE (2026-07-05; DEC-15 — input clearing decided) · **Area:** Bridge/Decision
- **Note (2026-07-05):** The *input-clearing* question is resolved by DEC-15 (match FNA: the bridge keeps
  ignoring `WINDOW_FOCUS_LOST` for input purposes; pinned by a test). Separately, FNA also sets
  `game.IsActive` on focus-gained/lost — that `Game.IsActive` plumbing is a **Game/windowing-layer** concern
  (not input state), so it is out of scope here and left as a Game-track follow-up.
- **Files:** `SdlInputBridge.cpp` (add cases), `InputManager.cpp`
- **Problem:** `WINDOW_FOCUS_LOST`/`FOCUS_GAINED`/window-destroy are not handled; relates to stuck-input.
- **Work:** Decide whether to handle these (transient clear) per §18; implement chosen path.
- **Acceptance:** Decision recorded; handler or documented no-op with test.
- **Tests:** synthetic window-event test.
- **Deps:** INPUT-BRIDGE-109.

#### INPUT-BRIDGE-015 — Subsystem initialization order & background events
- **Priority:** P2 · **Status:** TODO · **Area:** Bridge
- **Files:** `SdlInputBridge.cpp:1175–1191`, `Game.cpp`
- **Work:** Verify startup init + lazy init cooperate; `SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS` set before
  subsystem init.
- **Acceptance:** Init deterministic; hint applied.
- **Tests:** existing subsystem-init case.
- **Deps:** none.

#### INPUT-BRIDGE-016 — Shutdown behavior
- **Priority:** P2 · **Status:** TODO · **Area:** Bridge
- **Files:** `SdlInputBridge.cpp` (`ResetForTests` does NOT close handles)
- **Problem:** Reset deliberately does not close gamepad handles; real shutdown path is unclear.
- **Work:** Define/verify production shutdown (close opened gamepads, quit subsystem) vs test reset; document.
- **Acceptance:** Shutdown path defined + no leaks (ASan).
- **Tests:** ASan run (INPUT-BUILD-006).
- **Deps:** INPUT-BUILD-006.

#### INPUT-BRIDGE-017 — Fake vs real backend parity
- **Priority:** P2 · **Status:** TODO · **Area:** Bridge/Test-infra
- **Files:** `SdlGamepadBackend.cpp`, `FakeSdlGamepadBackend.hpp`
- **Work:** Ensure every real method has a fake counterpart with matching contract; add missing.
- **Acceptance:** Parity table complete.
- **Tests:** existing fake tests.
- **Deps:** INPUT-GAMEPAD-034.

#### INPUT-BRIDGE-018 — Platform-specific SDL caveats
- **Priority:** P3 · **Status:** TODO · **Area:** Bridge/Platform
- **Files:** `docs/platform-input-notes.md`
- **Work:** Consolidate X11/Wayland/Windows/macOS/mobile SDL input caveats already scattered in docs.
- **Acceptance:** One platform matrix.
- **Tests:** manual.
- **Deps:** none.

---

## 15. Tests plan

Baseline (this checkout, EasyGL, 2026-07-05): full `CnaTests` **3269 passed / 2 skipped**; canonical input
filter **280** (base **274**); order-independent under shuffle×3. The single source of truth is
`docs/input-build-and-test.md` (§Test counts) — cite it, don't restate. No `DISABLED_` input tests;
`GTEST_SKIP` only as headless environment fallback. A public-API compile/header-hygiene guard now exists
(`PublicApiInputCompileTests`, INPUT-API-030). Gaps below become concrete backlog items.

#### INPUT-TEST-001 — Add missing enum-value test suites
- **Priority:** P1 · **Status:** DONE (2026-07-05) · **Area:** Test
- **Files:** new `ButtonStateTests, ButtonsTests, GamePadTypeTests, GamePadDeadZoneTests, KeyStateTests, TouchLocationStateTests, GestureTypeTests`
- **Problem:** These enums are only used inline; no dedicated numeric/flag assertions.
- **Work:** One value/flag test per enum (see §5 tasks).
- **Acceptance:** Every enum value pinned.
- **Tests:** the new suites.
- **Deps:** INPUT-API-001..005,020,021.
- **Result (2026-07-05):** Added 7 test files / 10 cases. Every value cross-checked against the FNA
  reference (`Buttons.cs`, `GestureType.cs`, `GamePadType.cs`, `GamePadDeadZone.cs`,
  `TouchLocationState.cs`, `ButtonState.cs`, `KeyState.cs`) — **all byte-identical**, including the 6
  FNA-extension `Buttons` bits. Also covers the `Buttons`/`GestureType` flag operators (`| & ~ |= &=`).
  Satisfies the value-test acceptance of INPUT-API-001/002/003/004/005/020/021 (matrix-row population
  remains under INPUT-API-027) and backs the enum-ABI guardrail INPUT-API-034. Full suite 3248→3267;
  order-independent under shuffle×3. **Filter gap found:** `ButtonStateTests`/`KeyStateTests`/`ButtonsTests`
  fall outside the current input-filter tokens (they run in the full suite; the convenience filter needs
  `*ButtonState*:*KeyState*:*Buttons*` — folded into INPUT-BUILD-003).

#### INPUT-TEST-002 — DragComplete gesture tests (was zero)
- **Priority:** P1 · **Status:** DONE (2026-07-05) · **Area:** Test/Gesture
- **Files:** `tests/.../GestureDetectorTests.cpp`
- **Work:** Positive/negative DragComplete cases.
- **Acceptance:** DragComplete covered.
- **Tests:** 5 new `GestureDetectorTest.DragComplete*` cases (see INPUT-GESTURE-007 result).
- **Deps:** INPUT-GESTURE-007.

#### INPUT-TEST-003 — Gesture interruption/cancellation tests
- **Priority:** P1 · **Status:** DONE (2026-07-05) · **Area:** Test/Gesture
- **Files:** `tests/.../GestureDetectorTests.cpp`, `SdlInputBridgeTouchGestureTests.cpp`
- **Work:** Second-finger-mid-drag, cancel-mid-gesture (see §11).
- **Acceptance:** Interruption/cancel covered.
- **Tests:** 3 detector interruption cases + 1 bridge cancel case (see INPUT-GESTURE-011/012 results).
- **Deps:** INPUT-GESTURE-011, INPUT-GESTURE-012.

#### INPUT-TEST-004 — ToString expectation tests where FNA defines behavior
- **Priority:** P2 · **Status:** TODO · **Area:** Test
- **Files:** relevant `*Tests.cpp`
- **Work:** Assert ToString for types where FNA defines content (MouseState, TouchLocation) and typename for
  GamePadState/KeyboardState; document types where FNA has no override (no test needed).
- **Acceptance:** ToString behavior pinned where meaningful.
- **Tests:** extend existing suites.
- **Deps:** §5 matrices.

#### INPUT-TEST-005 — Public-API-only compile test
- **Priority:** P1 · **Status:** DONE (2026-07-05) · **Area:** Test/API
- **Files:** `tests/Microsoft/Xna/Framework/Input/PublicApiInputCompileTests.cpp`
- **Work:** Per INPUT-API-030.
- **Acceptance:** Compiles with public headers only.
- **Tests:** the TU.
- **Deps:** INPUT-API-030.
- **Result (2026-07-05):** Delivered together with INPUT-API-030 (same TU). See that task's result.

#### INPUT-TEST-006 — Synthetic gamepad event integration tests
- **Priority:** P2 · **Status:** TODO · **Area:** Test/GamePad
- **Files:** new bridge gamepad tests
- **Work:** Per INPUT-GAMEPAD-030 (event→state end-to-end).
- **Acceptance:** End-to-end covered.
- **Tests:** new suite.
- **Deps:** INPUT-GAMEPAD-030.

#### INPUT-TEST-007 — Boundary/parameterized deadzone & threshold tests
- **Priority:** P2 · **Status:** TODO · **Area:** Test/GamePad
- **Files:** gamepad tests
- **Work:** Per INPUT-GAMEPAD-025/029 boundary cases.
- **Acceptance:** Boundaries pinned.
- **Tests:** new parameterized cases.
- **Deps:** INPUT-GAMEPAD-025.

#### INPUT-TEST-008 — Golden behavior tests (recorded event sequence → state)
- **Priority:** P2 · **Status:** TODO · **Area:** Test
- **Files:** new golden tests
- **Work:** Encode representative event sequences and assert resulting snapshots (regression anchor).
- **Acceptance:** Golden fixtures stable across backends.
- **Tests:** new golden suite.
- **Deps:** none.

#### INPUT-TEST-009 — Fuzz event sequences (bridge robustness)
- **Priority:** P2 · **Status:** DONE (2026-07-05) · **Area:** Test
- **Files:** `tests/CNA/Internal/Input/SdlInputBridgeFuzzTests.cpp` (new)
- **Work:** Feed randomized (seeded, index-derived — no `Math.random`) `SDL_Event` streams through
  `ProcessEvent`; assert no crash / invariant violations.
- **Acceptance:** No crash under fuzz; invariants hold.
- **Tests:** new fuzz test (deterministic seeds).
- **Deps:** none.
- **Result (2026-07-05):** Added `SdlInputBridgeFuzzTests.cpp` — a deterministic LCG (seed
  `0x00C0FFEE`, no wall clock / no `std::random_device`) drives 5000 well-typed but edge-case
  `SDL_Event`s through the real `SdlInputBridge::ProcessEvent`: KEY_DOWN/UP (random keycode +
  scancode + repeat), MOUSE_MOTION/BUTTON_DOWN/UP (incl. unmapped buttons 6-7)/WHEEL, TEXT_INPUT
  (valid / multi-byte / astral / malformed `\xFF` / truncated `x\xC3` / empty), FINGER_DOWN/MOTION/
  UP/CANCELED (small reused id pool to stress the slot maps), and TEXT_EDITING. After every event it
  asserts `ProcessEvent` did not throw and that all four public snapshots (`Keyboard`/`Mouse`/
  `TouchPanel`/`GamePad`) stay readable, periodically pumps `TouchPanel::Update()` and drains
  `ReadGesture()`. Gamepad *device* events are intentionally excluded (they drive the real SDL
  gamepad subsystem — covered separately by the injectable fake backend). Passes locally and is
  **clean under ASan+UBSan** (no memory error / UB over the whole `ProcessEvent` switch), so under
  the sanitizer CI job it doubles as a memory-error/UB net on edge-case field values.

#### INPUT-TEST-010 — Shuffled/repeat determinism (gate)
- **Priority:** P1 · **Status:** TODO · **Area:** Test
- **Files:** harness/CI
- **Work:** Per INPUT-BUILD-009; make it a required check.
- **Acceptance:** Green under shuffle×5 on all backends.
- **Tests:** shuffled repeat.
- **Deps:** INPUT-BUILD-009.

#### INPUT-TEST-011 — Sanitizer test pass
- **Priority:** P1 · **Status:** DONE (2026-07-05) · **Area:** Test
- **Files:** CI
- **Work:** Per INPUT-BUILD-006 (ASan/UBSan).
- **Acceptance:** Clean input filter under sanitizers.
- **Tests:** sanitizer run.
- **Deps:** INPUT-BUILD-006.
- **Result (2026-07-05):** Delivered with INPUT-BUILD-006: input filter 279/279 under ASan+UBSan, ASan
  memory-clean, UBSan clean for all in-repo code (the sole remaining diagnostic is the external
  sharp-runtime `TimeSpan` static-init finding). Wiring this into CI is INPUT-CI-005 (still TODO).

#### INPUT-TEST-012 — Headless skip accounting
- **Priority:** P2 · **Status:** TODO · **Area:** Test
- **Files:** `MouseInputTests.cpp`, `TextInputEXTTests.cpp`
- **Work:** Per INPUT-BUILD-008; ensure skips are reported, not silently counted as pass.
- **Acceptance:** Skip report produced.
- **Tests:** headless vs display runs.
- **Deps:** INPUT-BUILD-008.

#### INPUT-TEST-013 — Regression test per known deviation
- **Priority:** P1 · **Status:** TODO · **Area:** Test
- **Files:** relevant suites
- **Work:** Each §18 deviation gets a test that pins current behavior (so a change is deliberate, not silent).
- **Acceptance:** Every deviation has a pinning test.
- **Tests:** new/extended cases per deviation.
- **Deps:** §18.

#### INPUT-TEST-014 — Regression test per fixed bug
- **Priority:** P2 · **Status:** TODO · **Area:** Test
- **Files:** relevant suites
- **Work:** Ensure the six I13/I14 fixes and the touch previous-location fix each have an explicit regression
  test (most exist; confirm and fill gaps).
- **Acceptance:** Each fixed bug has a named regression test.
- **Tests:** existing/extended.
- **Deps:** none.

#### INPUT-TEST-015 — Docs-vs-behavior agreement tests
- **Priority:** P2 · **Status:** TODO · **Area:** Test/Docs
- **Files:** tests + docs
- **Work:** For each doc claim that is testable (MaximumTouchCount value, wheel ×120, GUID format, deadzone
  constants), add a test asserting exactly what the doc states.
- **Acceptance:** Docs and tests agree by construction.
- **Tests:** targeted assertions.
- **Deps:** INPUT-TOUCH-012.

#### INPUT-TEST-016 — Platform-tagged tests (skip cleanly off-platform)
- **Priority:** P3 · **Status:** TODO · **Area:** Test/Platform
- **Files:** platform-specific tests
- **Work:** Tag Wayland/X11-only tests so they skip with a clear reason off-platform.
- **Acceptance:** Clear platform skips.
- **Tests:** platform runs.
- **Deps:** none.

#### INPUT-TEST-017 — Coverage of out-ref / try-get overloads separately
- **Priority:** P2 · **Status:** TODO · **Area:** Test
- **Files:** `TouchLocation.cpp`, `TouchCollection.cpp`
- **Work:** Ensure out-ref variants (`TryGetPreviousLocation`, `FindById`, `CopyTo`) are tested separately
  from value-returning paths (project rule).
- **Acceptance:** Out-ref overloads independently covered.
- **Tests:** extend touch tests.
- **Deps:** INPUT-TOUCH-009.

#### INPUT-TEST-018 — Manual hardware test harness / checklist wiring
- **Priority:** P2 · **Status:** TODO · **Area:** Test/Manual
- **Files:** `docs/demo-input-checklist.md`, `examples/demo_input`
- **Work:** Expand `demo_input` to exercise relative mouse, cursor warp, gestures, rumble, sensors, light bar
  (currently NOT exercised per the checklist); align checklist to what the demo covers.
- **Acceptance:** Demo exercises the manual-only surface; checklist matches.
- **Tests:** manual.
- **Deps:** none.

#### INPUT-TEST-019 — Equality/hash consistency sweep
- **Priority:** P3 · **Status:** TODO · **Area:** Test
- **Files:** value-type suites
- **Work:** For every type with `==`/hash, assert equal→equal-hash and unequal cases (fill any gaps).
- **Acceptance:** All value types swept.
- **Tests:** extend suites.
- **Deps:** §5 matrices.

#### INPUT-TEST-020 — Per-backend input parity
- **Priority:** P2 · **Status:** DONE (2026-07-05) · **Area:** Test/Build
- **Files:** all input tests
- **Work:** Confirm identical input results across EasyGL/Vulkan/bgfx/SDL_RENDERER (input is backend-agnostic).
- **Acceptance:** Same counts/results per backend.
- **Tests:** input filter per backend.
- **Deps:** INPUT-BUILD-002.
- **Result (2026-07-05):** Identical input-filter result **259/259** on all four backends (EasyGL, Vulkan,
  bgfx, SDL_RENDERER). Parity confirmed. Re-run whenever input code changes.

---

## 16. Documentation plan

Each doc task must: remove contradictions, separate verified-fact from intended-behavior, separate
automated from manual verification, and stamp manual results with exact date/hardware/OS/backend/SDL.

#### INPUT-DOC-001 — Reconcile all test counts to the real baseline
- **Priority:** P1 · **Status:** DONE (2026-07-05) · **Area:** Docs
- **Files:** `NEXT.md`, `docs/input-manual-verification-results.md`, `docs/input-backend.md`, `docs/input-build-and-test.md`
- **Problem:** 2234 vs 1964 vs real full; 257 vs 217 vs 165 vs real input.
- **Work:** Replace all with the single authoritative table (INPUT-BUILD-010); note the filter string used.
- **Acceptance:** One consistent count across all docs.
- **Tests:** re-run to confirm.
- **Deps:** INPUT-BUILD-010.
- **Result (2026-07-05):** Standardized on a **canonical input filter** (base tokens +
  `*ButtonState*:*KeyState*:*Buttons*:*PublicApiInput*`) and put the authoritative counts table in
  `docs/input-build-and-test.md`. Updated the stale counts everywhere: `input-backend.md` ("165" → points
  to the table), `NEXT.md` (§2/§4/§8 259/2234/257 → 3269 full / 280 canonical + pointer; §7 filter made
  canonical). The dated `input-manual-verification-results.md` 2026-07-04 entry is kept verbatim (historical
  record) with a "superseded" pointer rather than rewritten. All non-historical count claims now agree with
  a fresh re-run. (Adding a CTest label so the filter string isn't hand-copied remains INPUT-BUILD-003.)

#### INPUT-DOC-002 — Fix the `MaximumTouchCount` contradiction
- **Priority:** P1 · **Status:** DONE (2026-07-05) · **Area:** Docs
- **Files:** `docs/input-fna-fidelity.md`, `docs/input-backend.md`, `docs/xna-4-api-coverage.md`
- **Work:** After INPUT-TOUCH-012 decides, align all three docs to the same story (8-as-deviation OR 4/FNA).
- **Acceptance:** No contradiction remains.
- **Tests:** INPUT-TEST-015.
- **Deps:** INPUT-TOUCH-012.
- **Result (2026-07-05):** DEC-09 set the value to 4 (FNA-verified). All three docs updated to one story:
  `MaximumTouchCount = 4` (fixed XNA-compat value) and `GetState()` caps at `MAX_TOUCHES = 8` (DEC-10). The
  earlier contradiction (fidelity doc called 8 a deviation; backend/coverage docs claimed 8 "matches FNA")
  is gone.

#### INPUT-DOC-003 — Remove dangling `plan_input.md` task references or map them
- **Priority:** P1 · **Status:** TODO · **Area:** Docs
- **Files:** all input docs
- **Problem:** Docs cite `plan_input.md` tasks 700–959 that vanished when the file was deleted; this new file
  uses the `INPUT-*` scheme.
- **Work:** Replace old numeric task citations with `INPUT-*` IDs (or remove); ensure this file is the single
  plan referenced.
- **Acceptance:** No dangling references; docs point here.
- **Tests:** grep for `plan_input.md task` → only valid pointers.
- **Deps:** none.

#### INPUT-DOC-004 — Refresh `input-fna-fidelity.md` deviation list
- **Priority:** P2 · **Status:** TODO · **Area:** Docs
- **Files:** `docs/input-fna-fidelity.md`
- **Work:** Ensure every §18 deviation is listed with current behavior/FNA behavior/status (accepted/temporary/bug).
- **Acceptance:** Deviation list == §18.
- **Tests:** n/a.
- **Deps:** §18.

#### INPUT-DOC-005 — Update `input-backend.md` (event coverage, thread policy, filter)
- **Priority:** P2 · **Status:** TODO · **Area:** Docs
- **Files:** `docs/input-backend.md`
- **Work:** Correct the input-test-count/filter; document handled/unhandled SDL events; authoritative thread policy.
- **Acceptance:** Accurate and internally consistent.
- **Tests:** n/a.
- **Deps:** INPUT-BRIDGE-001, INPUT-DOC-001.

#### INPUT-DOC-006 — Update `input-build-and-test.md` with real commands + counts
- **Priority:** P2 · **Status:** TODO · **Area:** Docs
- **Files:** `docs/input-build-and-test.md`
- **Work:** Replace deferred-count placeholders; document the 3-backend commands, sanitizer, shuffle, headless
  skip behavior, SDL pin.
- **Acceptance:** A reader can reproduce exactly.
- **Tests:** follow the doc on a clean clone.
- **Deps:** INPUT-BUILD-001..010.

#### INPUT-DOC-007 — Update `platform-input-notes.md` (verified vs documented)
- **Priority:** P2 · **Status:** TODO · **Area:** Docs
- **Files:** `docs/platform-input-notes.md`
- **Work:** Clearly mark which platform behaviors are verified (with date) vs merely documented SDL/OS behavior.
- **Acceptance:** Verified/assumed clearly separated.
- **Tests:** n/a.
- **Deps:** none.

#### INPUT-DOC-008 — Rework `demo-input-checklist.md` to match demo capability
- **Priority:** P3 · **Status:** TODO · **Area:** Docs
- **Files:** `docs/demo-input-checklist.md`
- **Work:** After INPUT-TEST-018 expands the demo, update the checklist; keep unchecked items honest.
- **Acceptance:** Checklist == demo capability.
- **Tests:** manual.
- **Deps:** INPUT-TEST-018.

#### INPUT-DOC-009 — Refresh the manual verification log (new dated entry)
- **Priority:** P1 · **Status:** TODO · **Area:** Docs/Manual
- **Files:** `docs/input-manual-verification-results.md`
- **Problem:** Only entry is 2026-07-04, pre-Phase-I15, "no controller available"; counts stale.
- **Work:** Add a fresh entry with the current build (date/OS/backend/SDL/hardware); when controllers become
  available, add gamepad rows.
- **Acceptance:** Latest entry matches current reality; old entry retained as history.
- **Tests:** manual.
- **Deps:** INPUT-BUILD-010.

#### INPUT-DOC-010 — Update `xna-4-api-coverage.md` Input section
- **Priority:** P2 · **Status:** TODO · **Area:** Docs
- **Files:** `docs/xna-4-api-coverage.md`
- **Work:** Replace optimistic percentages with matrix-derived status (§5); fix the MaximumTouchCount claim.
- **Acceptance:** Coverage claims traceable to the matrix.
- **Tests:** n/a.
- **Deps:** §5, INPUT-TOUCH-012.

#### INPUT-DOC-011 — Update `NEXT.md` Input section
- **Priority:** P2 · **Status:** TODO · **Area:** Docs
- **Files:** `NEXT.md`
- **Work:** Fix branch name inconsistency (`feature/input` vs `feature/input-stabilization`), real counts,
  point §8 task list at this plan.
- **Acceptance:** NEXT.md accurate and consistent.
- **Tests:** n/a.
- **Deps:** INPUT-DOC-001.

#### INPUT-DOC-012 — Mark each deviation accepted / temporary / bug
- **Priority:** P2 · **Status:** TODO · **Area:** Docs
- **Files:** `docs/input-fna-fidelity.md`, this file §18
- **Work:** Assign a disposition to every deviation; bugs become P0/P1 fix tasks.
- **Acceptance:** Every deviation dispositioned.
- **Tests:** n/a.
- **Deps:** §18.

#### INPUT-DOC-013 — Separate "verified fact" from "intended behavior" repo-wide
- **Priority:** P2 · **Status:** TODO · **Area:** Docs
- **Files:** all input docs
- **Work:** Add a convention (e.g. ✅ verified vs 🎯 intended) and apply; no doc asserts unverified behavior as fact.
- **Acceptance:** Convention applied consistently.
- **Tests:** n/a.
- **Deps:** none.

#### INPUT-DOC-014 — Pin SDL version + toolchain in docs
- **Priority:** P2 · **Status:** TODO · **Area:** Docs
- **Files:** `docs/input-build-and-test.md`
- **Work:** Record the pinned SDL tag and the reference toolchain (g++/CMake/Ninja versions) with the build date.
- **Acceptance:** Versions pinned in docs.
- **Tests:** n/a.
- **Deps:** INPUT-BUILD-004.

---

## 17. CI and release criteria

**Verified:** there is currently **no CI** (no `.github/`, no `*.yml` outside submodules). Every green-build
claim is a manual local run.

#### INPUT-CI-001 — Bootstrap CI with submodule + sibling checkout
- **Priority:** P0 · **Status:** DONE (2026-07-05, verified green) · **Area:** CI
- **Files:** `.github/workflows/input-ci.yml`
- **Problem:** No automated build/test exists.
- **Work:** CI job: checkout, `git submodule update --init --recursive`, clone/point siblings sharp-runtime &
  easy-gl, configure, build `CnaTests`, run input filter.
- **Acceptance:** CI green on push for at least EasyGL.
- **Tests:** input filter in CI.
- **Deps:** INPUT-BUILD-001.
- **Result (2026-07-05):** Added `.github/workflows/input-ci.yml` — job `easygl-input` on `ubuntu-24.04`:
  checkout + recursive submodules; clone the **public** siblings `sharp-runtime`/`easy-gl`/`meta-gl`
  (develop, HTTPS, **no token needed**); apt deps (`g++-14`, FFmpeg dev, SDL X11/Wayland/GL/audio dev,
  `xvfb`); cache the prebuilt SDL keyed on the SDL submodule SHA; configure EasyGL + tests; build
  `CnaTests`; run the **canonical input filter under Xvfb** (`--gtest_shuffle --gtest_repeat=3`).
  Pre-flight de-risking done locally: build recipe = the session-proven EasyGL config; siblings confirmed
  public via the GitHub API; `third_party/enet` confirmed **committed** (not a submodule), so NET builds
  with no extra fetch; YAML validated; found that the SDL `dummy` driver makes 3 `MouseCursor` tests FAIL
  (null cursors) and switched to **Xvfb** (real virtual video → the same 280 that pass on a real display).
  Also implicitly covers part of INPUT-CI-004 (canonical filter in CI), INPUT-CI-006 (shuffle), and adds
  the SDL cache. **Verified GREEN** on GitHub Actions (run `a4267bb8`, 2026-07-05): Configure ✓, Build
  `CnaTests` ✓, Run input tests (Xvfb, shuffle×3) ✓. Runs were observed via the public GitHub API (no `gh`
  CLI/token available); Actions logs are auth-gated (403), so failures were surfaced by a temporary
  `if:failure()` step that pushed CMake logs to a throwaway `ci-diag` branch fetched over SSH. **One real
  fix needed:** the first runs failed at SDL3's X11 configure — missing `libxtst-dev` (SDL_X11_XTEST); added
  it and the run went green. Diagnostic scaffolding (`ci-diag` step, `contents:write`, verbose tee) has been
  removed and the `ci-diag` branch deleted. Backend matrix (INPUT-CI-002) and the sanitizer job
  (INPUT-CI-005) remain TODO.

#### INPUT-CI-002 — Backend matrix (EasyGL, Vulkan, bgfx, SDL_RENDERER)
- **Priority:** P1 · **Status:** DONE (2026-07-05, verified green) · **Area:** CI
- **Files:** `.github/workflows/input-ci.yml`
- **Work:** Matrix build+test per backend; input filter must pass on each.
- **Acceptance:** All backends green in CI.
- **Tests:** input filter × backend.
- **Deps:** INPUT-CI-001, INPUT-BUILD-002.
- **Result (2026-07-05):** Expanded the `strategy.matrix` to 5 entries — `EASYGL`, `SDL_RENDERER`, `VULKAN`,
  `BGFX`, and the `EASYGL` ASan+UBSan build — each running the canonical input filter under Xvfb.
  Parameterized `-DCNA_GRAPHICS_BACKEND` per entry; added `libvulkan-dev` (`find_package(Vulkan)`); bgfx is
  FetchContent'd from `bkaradzic/bgfx.cmake` at configure time (`BGFX_BUILD_TOOLS/EXAMPLES/TESTS=OFF`).
  **Verified GREEN** (run `a53e6e8e`, 2026-07-05): all five jobs pass on the first attempt — confirming the
  input tests are backend-agnostic in CI, matching the local INPUT-BUILD-002 result (280 each). Temporary
  per-entry `ci-diag` diagnostics + `contents:write` were used during bring-up and then removed (no failures
  occurred, so no diag branches were created).

#### INPUT-CI-003 — Submodule validation & SDL pin check
- **Priority:** P1 · **Status:** TODO · **Area:** CI
- **Files:** CI workflow
- **Work:** Fail CI if submodules missing or SDL not at the pinned tag.
- **Acceptance:** Drift fails CI.
- **Tests:** CI.
- **Deps:** INPUT-BUILD-004.

#### INPUT-CI-004 — Input test filter/label in CI
- **Priority:** P2 · **Status:** TODO · **Area:** CI
- **Files:** CI workflow
- **Work:** Use the canonical input label/filter (INPUT-BUILD-003) so CI runs exactly the input set + full suite.
- **Acceptance:** CI runs the labeled input set.
- **Tests:** CI.
- **Deps:** INPUT-BUILD-003.

#### INPUT-CI-005 — Sanitizer matrix job
- **Priority:** P1 · **Status:** DONE (2026-07-05, verified green) · **Area:** CI
- **Files:** `.github/workflows/input-ci.yml`
- **Work:** ASan+UBSan job over the input filter.
- **Acceptance:** Sanitizer job green.
- **Tests:** CI.
- **Deps:** INPUT-BUILD-006.
- **Result (2026-07-05):** Refactored `input-ci.yml` into a `strategy.matrix` of `[EasyGL, ASan+UBSan]`
  sharing all setup + the SDL cache (SDL is built uninstrumented, so one cache serves both). The ASan+UBSan
  entry configures with `-DCNA_SANITIZE=address,undefined` and runs the canonical filter under Xvfb with
  `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1` + `UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1`, so any
  UB / memory error fails the job. **Verified GREEN** (run `76922efd`, 2026-07-05): both matrix jobs pass —
  EasyGL ✓ and ASan+UBSan ✓ — so the input filter is ASan+UBSan-clean in CI under `halt_on_error=1` (no UB
  regression; the TimeSpan static-init fix in sharp-runtime `develop` and the in-repo hash fixes both hold).

#### INPUT-CI-006 — Shuffle/repeat determinism job
- **Priority:** P2 · **Status:** TODO · **Area:** CI
- **Files:** CI workflow
- **Work:** Run input filter `--gtest_shuffle --gtest_repeat=5` as a required check.
- **Acceptance:** Determinism job green.
- **Tests:** CI.
- **Deps:** INPUT-BUILD-009.

#### INPUT-CI-007 — Artifact & log upload
- **Priority:** P2 · **Status:** TODO · **Area:** CI
- **Files:** CI workflow
- **Work:** Upload test logs, skip reports, and (on failure) SDL/GL diagnostics as artifacts.
- **Acceptance:** Artifacts retrievable per run.
- **Tests:** CI.
- **Deps:** INPUT-CI-001.

#### INPUT-CI-008 — Coverage report (if feasible)
- **Priority:** P3 · **Status:** TODO · **Area:** CI
- **Files:** CI workflow
- **Work:** Optional gcov/llvm-cov coverage for the input filter.
- **Acceptance:** Coverage summary produced.
- **Tests:** CI.
- **Deps:** INPUT-CI-001.

#### INPUT-CI-009 — Manual-verification gate before "Input stable"
- **Priority:** P1 · **Status:** TODO · **Area:** CI/Release
- **Files:** `docs/input-manual-verification-results.md`, release checklist
- **Work:** Define that "Input stable" requires: 4-backend green, sanitizer green, determinism green, AND a
  current dated hardware-verification entry (incl. a real controller run).
- **Acceptance:** Gate documented; not declarable without the hardware row.
- **Tests:** process.
- **Deps:** INPUT-GAMEPAD-035.

#### INPUT-CI-010 — Pre-merge regression checklist
- **Priority:** P2 · **Status:** TODO · **Area:** CI/Release
- **Files:** release/PR checklist
- **Work:** Checklist: public-API frozen check passes, no new SDL leak, deviation tests intact, docs counts
  updated, input filter green on all backends.
- **Acceptance:** Checklist enforced on input PRs.
- **Tests:** process.
- **Deps:** INPUT-API-031.

---

## 18. Known deviations and decision log

Each entry: **Current** (verified CNA behavior) · **XNA** (if known) · **FNA** (if known) · **Risk** ·
**Decision needed** · **Tests** · **Docs** · **Disposition** (Accept / Fix / Decide). Every deviation must
have a pinning test (INPUT-TEST-013) whatever the disposition.

**DEC-01 — Event-driven vs poll-driven input.**
Current: state updated at SDL-event time, snapshot on `Get*State`. XNA/FNA: poll at `GetState`. Risk: state
freshness tied to `PollEvents`; PacketNumber semantics differ. Decision: accept as the CNA architecture.
Tests: snapshot/packet tests. Docs: input-backend.md. Disposition: **Accept (documented)**.
→ INPUT-BRIDGE-102, INPUT-GAMEPAD-027.

**DEC-02 — Max gamepad count clamped to 4.**
Current: `MaxSupportedGamePads=4`, over-limit refused. XNA: 4 players. FNA: can exceed via extra PlayerIndex.
Risk: 5th+ controller ignored. Decision: accept (XNA PlayerIndex frozen One..Four). Tests: >4 refuse. Docs:
fidelity. Disposition: **Accept**. → INPUT-GAMEPAD-008.

**DEC-03 — `FNA_GAMEPAD_NUM_GAMEPADS` clamped to ≤4.**
Current: parsed then `min(n,4)`. FNA: honors the value. Risk: minor. Decision: accept (follows DEC-02).
Tests: env-count. Disposition: **Accept**. → INPUT-GAMEPAD-002.

**DEC-04 — PacketNumber per-field / within-dead-zone wobble.**
Current: CNA synthesizes PacketNumber and bumps it on any raw field change (connect/button/axis), incl. a
within-dead-zone axis wobble. **FNA leaves `PacketNumber` hardcoded to 0** (`GamePadState.cs:124`; its SDL
backend never sets it — SDL has no packet counter). XInput's `dwPacketNumber` bumps on raw state change,
dead-zone-independent. So CNA's behavior is a NOXNA enhancement over FNA that matches XInput. Risk: an app
diffing PacketNumbers may re-read on sub-dead-zone jitter — a harmless no-op, same as on real XInput. Tests:
`PacketNumberBumpsOnWithinDeadZoneAxisWobbleWhileDeadZonedStateStaysAtRest`. Disposition: **ACCEPTED
(2026-07-05, INPUT-GAMEPAD-028)** — keep the raw-change bump; pinned by test; not a bug. → INPUT-GAMEPAD-028.

**DEC-05 — `GamePadState.GetHashCode` partial-field formula.**
Current: `buttons ^ packetNumber*31`. FNA: reflection-based default. Risk: different hash distribution
(not observable via contract). Decision: accept. Tests: hash consistency. Disposition: **Accept**.
→ INPUT-API-011.

**DEC-06 — EXT callbacks single-subscriber → multicast.**
Was: single `std::function` (a 2nd subscriber silently overwrote the 1st). FNA: **multicast — verified**:
`TextInput`/`TextEditing` are `event Action<...>`, `ClickedEXT` is a `public static Action<int>` field.
sharp-runtime's `System::Action`/`ActionT` are single `std::function` aliases, and `EventHandler<T>` has the
wrong signature (`Object* sender, const T&`) for FNA's `Action<char>`. Disposition: **FIXED (2026-07-05 —
multicast, user-chosen).** Added `System::MulticastAction<Args...>` to sharp-runtime (`b877f1c`; keeps FNA's
exact `void(Args...)` signature; `+=` add / `=` replace / `= nullptr` clear / snapshot-invoke). Rewired all
three EXT callbacks; existing single-subscriber sites compile unchanged; 4 CNA + 7 sharp-runtime
multi-subscriber tests. → INPUT-MOUSE-014, INPUT-TEXT-015.

**DEC-07 — `TextEditing` string is UTF-8 (byte-indexed).**
Current: `std::string` UTF-8, byte start/length. FNA: UTF-16 indexing. Risk: index semantics differ for
multibyte. Decision: accept + document, or convert. Tests: multibyte editing. Disposition: **Accept
(documented)**. → INPUT-TEXT-016.

**DEC-08 — Malformed UTF-8 skipped vs U+FFFD.**
Was: CNA silently dropped malformed bytes. FNA: **emits U+FFFD — verified** (decodes via `Encoding.UTF8`,
`SDL3_FNAPlatform.cs:1172`; its default replacement fallback substitutes U+FFFD, not throw). Disposition:
**FIXED (2026-07-05 — match FNA).** `decode_utf8_to_utf16` now emits U+FFFD for an invalid lead byte, an
ill-formed sequence (one U+FFFD per maximal subpart, resyncing to the next valid text), and — newly
validated — overlong encodings, UTF-16 surrogate code points, and out-of-range code points. 5 tests. (SDL
guarantees valid UTF-8, so this path is defensive/unreachable in practice, but now matches FNA.) →
INPUT-TEXT-008.

**DEC-09 — `MaximumTouchCount` reported as 8 → 4.**
Was: reported `MAX_TOUCHES=8` when connected. **FNA verified: reports 4** ("MaximumTouchCount is completely
bogus; for any touch device, XNA always reports 4", `SDL3_FNAPlatform.cs:2265`). Disposition: **FIXED
(2026-07-05 — report 4).** `GetCapabilities` now returns `MaximumTouchCount = 4` (0 when disconnected) — a
fixed XNA-compat value, NOT the tracking cap (that is `MAX_TOUCHES = 8`; see DEC-10). Reconciled the code +
all three docs, so the 8-vs-4 contradiction (INPUT-DOC-002) is gone; capability tests updated.
→ INPUT-TOUCH-012, INPUT-DOC-002.

**DEC-10 — Event-driven touch path uncapped → capped at 8.**
Was: the `InputManager` fallback reported every finger (uncapped). **FNA verified: fixed
`TouchLocation[MAX_TOUCHES = 8]` array**, so its public state never exceeds 8. Disposition: **FIXED
(2026-07-05 — cap at 8).** `TouchPanel::GetState()` now caps the public snapshot at `MAX_TOUCHES` (keeping
the 8 lowest-id touches); the `InputManager` map stays internally unbounded (an implementation detail). The
`>max` test was rewritten to assert the public cap. → INPUT-TOUCH-014.

**DEC-11 — Sequential CNA touch IDs vs SDL finger IDs.**
Current: compact counter from 1. FNA: casts SDL finger id. Risk: id values differ from FNA; app assumptions.
Decision: accept + document. Tests: id-alloc/reuse. Disposition: **Accept**. → INPUT-TOUCH-013.

**DEC-12 — `TryGetPreviousLocation` false path doesn't write out-param.**
Was: CNA early-returned `false` leaving the out-param untouched. FNA: **always writes — verified**
(`TouchLocation.cs`: `previousLocation = new TouchLocation(Id, prevState, prevPosition); return
previousLocation.State != Invalid;` — a C# `out` param must be assigned on every path). Disposition:
**FIXED (2026-07-05 — match FNA).** CNA now assigns `TouchLocation(id_, prevState_, prevPosition_)`
unconditionally and returns `prevState_ != Invalid`; on the false path the out-param is the Invalid previous
location (`id_`, `Invalid`, `prevPosition_`). Dedicated false-path test added (the existing 5-arg-ctor test
already covers the true path). → INPUT-TOUCH-009.

**DEC-13 — `TouchPanel.Update()` copies current→previous before gesture update.**
CNA: copy-then-`OnUpdate`. FNA: `OnUpdate()` then copy (`TouchPanel.cs:219`) — reversed. Disposition:
**ACCEPTED (2026-07-05) — confirmed inert.** `GestureDetector::OnUpdate()` reads/writes only the gesture
detector's own state and never `touches_`/`previousTouches_` (grep-verified), so the two statements touch
disjoint state and the order is unobservable. Documented + pinned by
`TouchEdgeCaseTest.UpdatePropagatesTouchesToPreviousForSlotPathContinuity`. → INPUT-TOUCH-023.

**DEC-14 — Relative mouse cache desync.**
Clarified on inspection: the **public getter already reads SDL live** (`SDL_GetWindowRelativeMouseMode`) —
matching FNA — so there is no cache at the API boundary. The only cache is the SDL-agnostic `InputManager`'s
`RelativeMode` flag (gates relative-delta accumulation/draining), written by the CNA setter, which updates
SDL **and** `InputManager` together. Disposition: **ACCEPTED (2026-07-05).** Cannot diverge through CNA's
API; would only desync if a caller toggled SDL relative mode *directly*, bypassing the CNA setter (out of
contract). Having `InputManager` read SDL would break the input-layer/SDL boundary. The live round-trip and
the setter→`InputManager` delta sync are both tested. → INPUT-MOUSE-007.

**DEC-15 — No focus-loss / window-destroy input clearing.**
Current: none. FNA: **also none — verified** (FNA is event-driven/accumulating like CNA — `Keyboard.keys`
mutated by KEY_DOWN/KEY_UP, `SDL3_FNAPlatform.cs:905-940` — and on `WINDOW_FOCUS_LOST` it only sets
`game.IsActive=false`, `:1026-1035`, never clearing keys), so FNA has the **identical** stuck-key edge case.
Risk: a held key/button can stick if its up-event is delivered to another window. Disposition: **ACCEPTED
(2026-07-05) — match FNA (no clear).** A beyond-FNA `ClearTransientState()` was considered and rejected
(would silently diverge from the reference); the XNA-standard mitigation is `Game.IsActive`. Also corrected
a doc error that claimed FNA re-polls each frame (it does not). Documented in `docs/input-fna-fidelity.md`;
pinned by `SdlInputBridgeKeyboardTest.WindowFocusLostDoesNotClearHeldKeysMatchingFna`. → INPUT-KBD-020,
INPUT-BRIDGE-014, INPUT-BRIDGE-109.

**DEC-16 — Unmapped keyboard key dropped (not `Keys::None`).**
Current: keycode path drops unmapped keys; scancode `UNKNOWN→None`. **FNA verified: FNA marks `Keys.None`
pressed** — `ToXNAKey` returns `Keys.None` and FNA then does `Keyboard.keys.Add(Keys.None)`
(`SDL3_FNAPlatform.cs:905-908`); CNA's drop is cleaner (no meaningless "None" key). Tested
(`UnmappedKeycodeIsDroppedNotMarkedNone`). Disposition: **ACCEPTED (2026-07-05)**.
→ INPUT-KBD-007, INPUT-KBD-013.

**DEC-17 — Android back key `SDLK_AC_BACK → Keys::Escape`.**
Current: mapped to `Keys::Escape`. **FNA verified: no `AC_BACK` mapping** — CNA-only convenience so "back"
acts as cancel/exit on Android/browser. Tested (`AndroidBackButtonMapsToEscape`). Disposition: **ACCEPTED
(2026-07-05)**. → INPUT-KBD-018.

**DEC-18 — Mouse horizontal wheel dropped.**
Current: `wheel.x` ignored. **FNA/XNA verified: `MouseState` exposes only the vertical `ScrollWheelValue`**
(no horizontal-wheel property), so dropping `wheel.x` is correct. Tested (`HorizontalWheelIsIgnored`).
Disposition: **ACCEPTED (2026-07-05)**. → INPUT-MOUSE-011.

**DEC-19 — Text synthesis gates on SDL repeat flag.**
Current: gates the control-char re-emit on SDL's `repeat` flag. **FNA verified: FNA does the same**
(`else if (evt.key.repeat)`, `SDL3_FNAPlatform.cs:923`) — the earlier "tracked-membership" framing was
wrong, so this is **not a deviation; CNA matches FNA.** Tested (`KeyRepeatReemitsControlCharacter`).
Disposition: **ACCEPTED (2026-07-05) — matches FNA**. → INPUT-TEXT-010.

**DEC-20 — `GetGUID`/`GetCapabilities` computed live vs FNA cached-at-connect.**
Current: live queries; caps use non-mutating property reads (to avoid a rumble-cancelling probe). FNA:
cached at connect. Same values for a connected controller (timing/impl detail, not a behavioral gap).
Tested (existing GUID/caps + no-rumble cases). Disposition: **ACCEPTED (2026-07-05)**.
→ INPUT-GAMEPAD-012, INPUT-GAMEPAD-016.

**DEC-21 — `MouseCursor` (and transitively `Mouse`) public header leaks SDL types.**
Current: `MouseCursor.hpp` includes `<SDL3/SDL.h>`, exposes `SDL_Cursor*`/`SDL_SystemCursor`; whole class is
NOXNA (MonoGame-derived, absent from XNA/FNA). **Confirmed 2026-07-05 (INPUT-API-030): the leak also reaches
the strict-XNA `Mouse` header — `Mouse.hpp` `#include`s `MouseCursor.hpp`**, so any consumer of `Mouse`
pulls in all of SDL. Risk: SDL bleeds into the public XNA include tree via a core type, not just a NOXNA
extension (raises severity). A live compile guard now pins the current scope ({`Mouse.hpp`,`MouseCursor.hpp`}
allowed; all other public Input headers must stay SDL-free). Decision: accept as documented exception OR
hide SDL behind an opaque handle/pimpl in `MouseCursor` and forward-declare it in `Mouse.hpp`. Tests:
`PublicApiInputCompileTests`. Disposition: **FIXED (2026-07-05, option b — INPUT-MOUSE-018).** SDL header
removed from `MouseCursor.hpp` via a forward-declared opaque `SDL_Cursor*`; `Mouse.hpp` no longer
transitively pulls SDL; the compile guard now enforces zero SDL leakage across all public Input headers.
Remaining nuance (optional): the opaque `SDL_Cursor*` type *name* still appears in the NOXNA MouseCursor
API — acceptable as an explicitly-intended handle; could later become `void*` if full name-hiding is wanted.
→ INPUT-MOUSE-018, INPUT-API-033.

**DEC-22 — `ToString` returns type-name for GamePadState/KeyboardState.**
Current: returns fully-qualified type name (FNA ValueType default), not content. XNA/FNA: same. Risk: none.
Decision: accept (this is FNA-accurate). Tests: ToString expectation. Disposition: **Accept**.
→ INPUT-API-011, INPUT-API-014.

---

## 19. Task format (reference)

Every task above follows: **ID** · Priority (P0–P3) · Status (TODO unless verified from code/tests) · Area ·
Files likely affected · Problem/rationale · Work · Acceptance · Tests · Deps. ID prefixes:
`INPUT-AUDIT-*, INPUT-BUILD-*, INPUT-API-*, INPUT-KBD-*, INPUT-MOUSE-*, INPUT-GAMEPAD-*, INPUT-TOUCH-*,
INPUT-GESTURE-*, INPUT-TEXT-*, INPUT-BRIDGE-*, INPUT-TEST-*, INPUT-DOC-*, INPUT-CI-*`. Decision entries use
`DEC-*` and cross-link to their implementing tasks. Every task is scoped to one focused implementation pass.

## 20. Size

This plan contains **265 tasks** across 13 ID families plus **22 decision-log entries**. Priority split:
**P0 ×3, P1 ×43, P2 ×140, P3 ×79**. Per family: AUDIT 4, BUILD 10, API 34, KBD 22, MOUSE 23, GAMEPAD 37,
TOUCH 25, GESTURE 20, TEXT 16, BRIDGE 30, TEST 20, DOC 14, CI 10. Every task is grounded in a verified
code/test/doc fact from the 2026-07-05 audit — none is filler. Large topics (gamepad, touch, gesture,
API matrix) are split into individually-acceptable tasks.

## 21. Accuracy statement

- Behavior is **not** claimed correct merely because a doc says so; docs were cross-checked and several
  contradictions recorded (§2.3, §18, INPUT-DOC-001/002/003).
- Test results reported here were **actually run in this checkout** on 2026-07-05: full `CnaTests`
  **3248 passed / 2 skipped** (EasyGL); input filter **259 passed** on **all four backends** (EasyGL,
  Vulkan, bgfx, SDL_RENDERER — INPUT-BUILD-002); order-independent under `--gtest_shuffle --gtest_repeat=3`.
- **Not run this session** (open gaps, tracked as tasks): any sanitizer build, any real-hardware
  verification, a true fresh-clone build. No build/test command **failed** this session.
- **No reproducibility blocker was found** in this warm checkout, but a true fresh-clone build was not
  attempted — INPUT-BUILD-001/INPUT-CI-001 exist to prove it. The checkout builds and tests pass as recorded.

## 22. Recommended execution order

- **Phase 0 — Reproducible build & source inventory.** INPUT-BUILD-001, -002, -004, -005; INPUT-CI-001;
  INPUT-AUDIT-001..004. Goal: a fresh clone builds all four backends; CI proves it; inventory current.
- **Phase 1 — API matrix & strict-XNA guardrails.** INPUT-API-001..034 (esp. the enum-value tests,
  INPUT-API-030 public-compile test, INPUT-API-031 frozen signatures); INPUT-MOUSE-018/DEC-21 SDL-leak call.
- **Phase 2 — Keyboard/Mouse/GamePad/Touch critical behavior.** INPUT-KBD-009..013,020; INPUT-MOUSE-010,011,014;
  INPUT-GAMEPAD-012,019,025,028,030; INPUT-TOUCH-009,012,014; the DEC-04/09/15/21 decisions.
- **Phase 3 — Gestures & text input.** INPUT-GESTURE-007 (DragComplete), -011/-012 (interruption/cancel);
  INPUT-TEXT-008 (malformed UTF-8), -015 (callback style).
- **Phase 4 — Hardware/manual verification.** INPUT-GAMEPAD-035, INPUT-MOUSE-023, INPUT-TOUCH-020,
  INPUT-TEXT-012/013; refresh the dated verification log (INPUT-DOC-009); manual gate INPUT-CI-009.
- **Phase 5 — Docs cleanup & stability gate.** INPUT-DOC-001..014; INPUT-CI-002..010; declare Input stable
  only when the manual gate (INPUT-CI-009) and pre-merge checklist (INPUT-CI-010) pass.
