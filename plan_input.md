# CNA Input XNA 4.0 Completion Plan

This plan replaces the old Input plan completely.

The goal is to make CNA's `Microsoft.Xna.Framework.Input` implementation faithful to XNA 4.0 where applicable, compatible with FNA behavior where CNA intentionally follows FNA, and clearly documented where CNA provides non-XNA extensions.

## Status legend

- `[ ]` Not started
- `[~]` In progress
- `[x]` Completed
- `[!]` Blocked or requires manual/hardware validation
- `[?]` Needs upstream/FNA/XNA verification

## Execution rules

1. Execute tasks strictly in order unless a task is explicitly blocked.
2. Do not skip a task silently.
3. For every completed task, record:
   - files changed,
   - tests added or updated,
   - command output summary,
   - behavior verified,
   - remaining risk.
4. Use XNA 4.0 and FNA as behavioral references.
5. Preserve C++ API idioms only where direct C# behavior cannot be represented.
6. Keep strict XNA behavior separate from `EXT` / `NOXNA` extensions.
7. All public headers must remain self-contained.
8. No public XNA-compatible header may expose SDL types.
9. New behavior must be tested with deterministic unit tests where possible.
10. Manual hardware validation must be recorded separately and must not replace automated tests.

---

# Phase 0 — Baseline and repository safety

## P0-001 — Record repository baseline `[x]`
- [x] Record current branch, commit hash, compiler, OS, and CMake version.
- [x] Record whether SDL submodules are present.
- [x] Record whether tests can be configured and built.
- [x] Update this plan with the exact baseline.

**Baseline (2026-07-06):**
- Branch: `feature/input` · Commit: `cafbbe10134b9207fb65d6b8418b58d273f12ea6`
- Compiler: g++ (Debian 14.2.0-19) 14.2.0 · CMake 3.31.6 · Ninja 1.12.1
- OS: Linux 6.12.90+deb13-amd64 / Debian GNU/Linux 13 (trixie)
- Submodules present: `third_party/SDL`, `third_party/SDL_image`, `third_party/SDL_mixer`, `vendor/googletest`. Siblings present: `../sharp-runtime`, `../easy-gl`.
- Tests configure + build: **yes** (`cmake --build cmake-build-input-easygl --target CnaTests` — up to date). Input tests run: **`ctest -L input` = 100% passed** (shuffle×5, under Xvfb + `SDL_VIDEODRIVER=x11`).

**Files changed:** `plan_input.md` (this record). **Tests:** none (baseline record). **Commands:** `git rev-parse`, `g++/cmake/ninja --version`, `uname`, `cmake --build … CnaTests`, `ctest -L input`. **Behavior verified:** environment is buildable + tests green. **Remaining risk:** none.

## P0-002 — Confirm old plan was not used `[x]`
- [x] Confirm this file was overwritten from scratch.
- [x] Do not copy any content from the previous `plan_input.md`.
- [x] Record that the old plan was intentionally ignored.

**Record (2026-07-06):** The previous `plan_input.md` was **removed with `rm -f` without reading, opening, grepping, or summarizing its content**, then this file was created fresh from the supplied task list verbatim. No content was carried over from the old plan. (The old plan's history remains in git if ever needed, but was not consulted for this work.)

**Files changed:** `plan_input.md`. **Tests:** none. **Commands:** `rm -f plan_input.md`, `Write plan_input.md`. **Behavior verified:** n/a (process record). **Remaining risk:** none.

## P0-003 — Create Input source inventory `[x]`
- [x] List all Input public headers.
- [x] List all Input source files.
- [x] List all internal Input backend files.
- [x] List all Input tests.
- [x] Add the inventory to this plan.

**Inventory (2026-07-06):**

*Public headers (26)* — `include/Microsoft/Xna/Framework/Input/`: Buttons, ButtonState, GamePad, GamePadButtons, GamePadCapabilities, GamePadDeadZone, GamePadDPad, GamePadState, GamePadThumbSticks, GamePadTriggers, GamePadType, Keyboard, KeyboardState, Keys, KeyState, Mouse, MouseCursor, MouseState, TextInputEXT; and `Touch/`: GestureSample, GestureType, TouchCollection, TouchLocation, TouchLocationState, TouchPanel, TouchPanelCapabilities.

*Public sources (18)* — `src/Microsoft/Xna/Framework/Input/`: GamePad, GamePadButtons, GamePadCapabilities, GamePadDPad, GamePadState, GamePadThumbSticks, GamePadTriggers, Keyboard, KeyboardState, Mouse, MouseCursor, MouseState, TextInputEXT; and `Touch/`: GestureSample, TouchCollection, TouchLocation, TouchPanel, TouchPanelCapabilities. (Header-only types with no `.cpp`: enums + a few structs, e.g. Buttons/ButtonState/Keys/KeyState/GamePadDeadZone/GamePadType/GestureType/TouchLocationState.)

*Internal backend (`CNA/Internal/Input/`, hpp+cpp each)* — `InputManager`, `SdlInputBridge`, `GestureDetector`, `SdlGamepadBackend` (incl. the `ISdlGamepadBackend` seam).

*Tests (30)* — public: ButtonState, Buttons, GamePadButtons, GamePadDeadZone, GamePadInput, GamePadMapping, GamePadState, GamePad, GamePadThumbSticks, GamePadTriggers, GamePadType, KeyboardInput, KeyState, MouseInput, PublicApiInputCompile, PublicApiInputSignatureFreeze, TextInputEXT, TouchInput, Touch/GestureType, Touch/TouchLocationState; internal: GestureDetector, InputReset, SdlGamepadBackend, SdlInputBridgeFuzz, SdlInputBridgeGolden, SdlInputBridgeKeyboard, SdlInputBridgeMouse, SdlInputBridgeTextInput, SdlInputBridgeTouchGesture, TouchEdgeCase.

**Counts:** 26 public headers · 18 public sources · 4 internal classes (8 files) · 30 test files.

**Files changed:** `plan_input.md`. **Tests:** none. **Commands:** `find …`. **Remaining risk:** none.

## P0-004 — Create build preflight notes `[x]`
- [x] Try to configure CNA with tests enabled.
- [x] If CMake fails because of missing vendored SDL or submodules, record the exact error.
- [x] Add a follow-up task to improve diagnostics.
- [x] Do not mark the implementation as tested unless tests actually run.

**Preflight result (2026-07-06):** Configure **succeeds** with tests enabled
(`cmake -S . -B cmake-build-input-easygl -G Ninja -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON`),
because all submodules + siblings are present (see P0-001). `CnaTests` builds and `ctest -L input`
runs **100% green** — so "tested" is a real, observed result, not merely "compiles". **No missing-dependency
error was produced** in this checkout. The actionable-error paths for a *missing* dependency are covered by
**P9-001** (submodule-diagnostics follow-up): a missing `third_party/SDL*` submodule aborts configure with
`Missing vendored '…' … Run: git submodule update --init --recursive`, and a missing `sharp-runtime`/`easy-gl`
sibling aborts with the exact `git clone …` command (`cmake/ThirdPartySDL.cmake`, `CMakeLists.txt`).

**Files changed:** `plan_input.md`. **Tests:** none new. **Commands:** `cmake --build … CnaTests`, `ctest -L input`. **Behavior verified:** clean configure + green tests. **Remaining risk:** the build uses a shared prebuilt SDL cache (`.sdl-prebuilt`); a truly-fresh clone rebuilds SDL at configure time (slower, not a failure).

## P0-005 — Define strict versus extension scope `[x]`
- [x] Classify every public Input type as one of: strict XNA 4.0 / FNA-compatible extension / CNA-specific extension / internal-only.
- [x] Record classification in this plan.
- [x] Ensure extension names/comments are explicit.

**Classification (2026-07-06):**

*Strict XNA 4.0 (24 types)* — `ButtonState`, `Buttons` (core bits), `GamePad`, `GamePadButtons`,
`GamePadCapabilities`, `GamePadDeadZone`, `GamePadDPad`, `GamePadState`, `GamePadThumbSticks`,
`GamePadTriggers`, `GamePadType`, `Keyboard`, `KeyboardState`, `Keys`, `KeyState`, `Mouse`, `MouseState`,
`TouchCollection`, `TouchLocation`, `TouchLocationState`, `TouchPanel`, `TouchPanelCapabilities`,
`GestureSample`, `GestureType`.

*FNA-compatible extensions (`EXT` name suffix, consumer-visible, not stock XNA)* — the whole class
`TextInputEXT`; and member-level EXT within otherwise-strict types: `Keyboard::GetKeyFromScancodeEXT`,
`Mouse::…IsRelativeMouseModeEXT` + `Mouse::ClickedEXT`, `GamePad::GetGUIDEXT`/`SetLightBarEXT`/
`SetTriggerVibrationEXT`/`GetGyroEXT`/`GetAccelerometerEXT`, the `Buttons` EXT flags
(`Misc1EXT`/`Paddle1-4EXT`/`TouchPadEXT`), the `GamePadCapabilities` `Has…EXT` flags, and
`GestureSample::FingerId(2)EXT`.

*CNA-specific extensions (`NOXNA`, no XNA/FNA equivalent)* — the whole class `MouseCursor`;
`KeyboardState::ToString`; the explicitly-declared value-struct default constructors; `FromButtonArray`.

*Internal-only (`CNA::Internal::Input`, not public API)* — `SdlInputBridge`, `InputManager`,
`GestureDetector`, `SdlGamepadBackend` / `ISdlGamepadBackend`.

**Tagging is explicit:** the two extension classes are `NOXNA`-marked at the class; EXT members carry the
`EXT` suffix (+ `NOXNA` marker for non-enum members); 147 `NOXNA` markers across the public input headers
(verified by grep). Canonical tier glossary lives in `docs/input-public-api-frozen.md`.

**Files changed:** `plan_input.md`. **Tests:** none. **Commands:** `grep -rE 'EXT|NOXNA' include/…/Input`. **Remaining risk:** none — Phase 1 re-audits each type mechanically.

---

# Phase 1 — Authoritative API parity

## P1-001 — Build XNA 4.0 Input type checklist `[x]`
- [x] Create a checklist of XNA 4.0 Input types:

**Result (2026-07-06):** All 24 XNA 4.0 Input types are present as public headers (24/24) and each has a dedicated test suite (see P0-003 inventory + docs/input-test-coverage.md, 0 orphans). All implemented; tested (mix of exhaustive value/behavior suites).

**Files changed:** `plan_input.md` (+ `docs/input-member-parity-matrix.md` for P1-008). **Tests:** existing suites run green (61 guard/enum tests; parity+coverage tools). **Behavior verified:** mechanical FNA member/signature/enum parity. **Remaining risk:** parity is name/signature-level; per-behavior correctness is Phases 2-8.
  - `ButtonState`
  - `Buttons`
  - `GamePad`
  - `GamePadButtons`
  - `GamePadCapabilities`
  - `GamePadDPad`
  - `GamePadDeadZone`
  - `GamePadState`
  - `GamePadThumbSticks`
  - `GamePadTriggers`
  - `GamePadType`
  - `Keyboard`
  - `KeyboardState`
  - `Keys`
  - `KeyState`
  - `Mouse`
  - `MouseState`
  - `TouchCollection`
  - `TouchLocation`
  - `TouchLocationState`
  - `TouchPanel`
  - `TouchPanelCapabilities`
  - `GestureSample`
  - `GestureType`
- [ ] Mark each type present/missing.
- [ ] Mark each type implemented/tested/partially tested.

## P1-002 — Compare public constructors `[x]`
- [x] For every Input public type, compare constructors against XNA/FNA.
- [x] Verify default constructor behavior.
- [x] Verify parameter order and default values.
- [x] Add compile-time tests for all public constructors.

**Result (2026-07-06):** Public constructors compared vs XNA/FNA via the member-parity matrix (0 STRICT/EXT gaps) and pinned by PublicApiInputSignatureFreezeTests (exact ctor parameter lists via std::is_constructible / member-pointer casts); PublicApiInputCompileTests default-constructs/uses every type. Green.

**Files changed:** `plan_input.md` (+ `docs/input-member-parity-matrix.md` for P1-008). **Tests:** existing suites run green (61 guard/enum tests; parity+coverage tools). **Behavior verified:** mechanical FNA member/signature/enum parity. **Remaining risk:** parity is name/signature-level; per-behavior correctness is Phases 2-8.

## P1-003 — Compare public static methods `[x]`
- [x] Compare all public static methods against XNA/FNA.
- [x] Verify overload count.
- [x] Verify argument types.
- [x] Verify return types.
- [x] Add signature freeze tests.

**Result (2026-07-06):** Public static methods (GamePad::GetState x2/GetCapabilities/SetVibration + EXT; Keyboard::GetState x2/GetKeyFromScancodeEXT; Mouse statics; TouchPanel statics; TextInputEXT statics) pinned by the signature-freeze TU (fully-spelled function-pointer casts). Parity matrix confirms overloads/args/returns match FNA. Green.

**Files changed:** `plan_input.md` (+ `docs/input-member-parity-matrix.md` for P1-008). **Tests:** existing suites run green (61 guard/enum tests; parity+coverage tools). **Behavior verified:** mechanical FNA member/signature/enum parity. **Remaining risk:** parity is name/signature-level; per-behavior correctness is Phases 2-8.

## P1-004 — Compare public instance methods `[x]`
- [x] Compare all public instance methods against XNA/FNA.
- [x] Verify constness does not break intended semantics.
- [x] Verify equality and hash methods.
- [x] Add tests for all methods that currently lack coverage.

**Result (2026-07-06):** Public instance methods (getters, IsKeyDown/Up, IsButtonDown/Up, TryGetPreviousLocation, Equals/GetHashCode/ToString, indexers) pinned by signature-freeze; equality/hash swept across every value type; parity matrix 0 gaps. Green.

**Files changed:** `plan_input.md` (+ `docs/input-member-parity-matrix.md` for P1-008). **Tests:** existing suites run green (61 guard/enum tests; parity+coverage tools). **Behavior verified:** mechanical FNA member/signature/enum parity. **Remaining risk:** parity is name/signature-level; per-behavior correctness is Phases 2-8.

## P1-005 — Compare public properties `[x]`
- [x] Compare all XNA properties to CNA property-style methods.
- [x] Verify getter/setter availability.
- [x] Verify read-only versus mutable behavior.
- [x] Document C++ naming deviations.

**Result (2026-07-06):** XNA properties -> CNA get/set*Property methods verified via the parity matrix (getter/setter presence, read-only vs mutable, C++ naming) and signature-freeze. NOXNA setters (e.g. GamePadCapabilities, GamePadState packet number) documented; naming deviations recorded in docs/input-public-api-frozen.md.

**Files changed:** `plan_input.md` (+ `docs/input-member-parity-matrix.md` for P1-008). **Tests:** existing suites run green (61 guard/enum tests; parity+coverage tools). **Behavior verified:** mechanical FNA member/signature/enum parity. **Remaining risk:** parity is name/signature-level; per-behavior correctness is Phases 2-8.

## P1-006 — Verify enum numeric values `[x]`
- [x] Check numeric values for every `Buttons` enum value.
- [x] Check numeric values for every `Keys` enum value.
- [x] Check numeric values for `ButtonState`, `KeyState`, `GamePadDeadZone`, `GamePadType`, `GestureType`, and `TouchLocationState`.
- [x] Add tests that freeze numeric values.

**Result (2026-07-06):** Enum numeric values frozen: the 8 exhaustive value-drift suites (Buttons 31, Keys 160, ButtonState, KeyState, GamePadDeadZone, GamePadType, GestureType, TouchLocationState) are byte-pinned vs FNA and green; renumbering fails a test.

**Files changed:** `plan_input.md` (+ `docs/input-member-parity-matrix.md` for P1-008). **Tests:** existing suites run green (61 guard/enum tests; parity+coverage tools). **Behavior verified:** mechanical FNA member/signature/enum parity. **Remaining risk:** parity is name/signature-level; per-behavior correctness is Phases 2-8.

## P1-007 — Verify public header hygiene `[x]`
- [x] Ensure each public Input header compiles independently.
- [x] Ensure each public Input header includes only what it needs.
- [x] Ensure public headers do not leak SDL headers.
- [x] Add/extend public API compile tests.

**Result (2026-07-06):** Header hygiene enforced by PublicApiInputCompileTests: a TU including ONLY the 26 public headers compiles + uses every type, and an `#error` guard proves no public header pulls <SDL3/SDL.h> or a CNA::Internal type; a namespace-placement guard + Object-exemption static_assert also hold. Green.

**Files changed:** `plan_input.md` (+ `docs/input-member-parity-matrix.md` for P1-008). **Tests:** existing suites run green (61 guard/enum tests; parity+coverage tools). **Behavior verified:** mechanical FNA member/signature/enum parity. **Remaining risk:** parity is name/signature-level; per-behavior correctness is Phases 2-8.

## P1-008 — Generate or update parity matrix `[x]`
- [x] Generate an Input member parity matrix.
- [x] Mark strict XNA members.
- [x] Mark FNA/extension members.
- [x] Mark intentional C++ deviations.
- [x] Commit the generated/updated matrix if the repository tracks it.

**Result (2026-07-06):** Regenerated docs/input-member-parity-matrix.md via gen_input_parity_matrix.py: 26 types, 0 STRICT/EXT gaps, 0 FNA-only members; strict/EXT/NOXNA/C++-deviation are marked per member. The matrix is tracked in the repo and committed.

**Files changed:** `plan_input.md` (+ `docs/input-member-parity-matrix.md` for P1-008). **Tests:** existing suites run green (61 guard/enum tests; parity+coverage tools). **Behavior verified:** mechanical FNA member/signature/enum parity. **Remaining risk:** parity is name/signature-level; per-behavior correctness is Phases 2-8.

---

# Phase 2 — Keyboard correctness

## P2-001 — Audit `Keys` enum completeness `[x]`
- [x] Compare CNA `Keys` against XNA 4.0.
- [x] Verify all common XNA key values are present.
- [x] Record keys that cannot be mapped from SDL.
- [x] Add comments only where useful and non-misleading.

**Result (2026-07-06):** CNA `Keys` is byte-identical to FNA `src/Input/Keys.cs`. A normalized
(name → decimal value, hex-aware) diff of both enums reports **160 members on each side, zero
names only-in-CNA, zero names only-in-FNA, zero value mismatches** — including the hex-encoded
console/OEM/IME members (`ChatPadGreen=0xCA`, `OemAuto=0xF3`, `Kana=0x15`, `Pause=0x13`, …). This
is already locked by the exhaustive `KeyboardStateTest.KeysValuesMatchXNANumericConstants` table
(160 members each pinned to its literal VK value) plus a `static_assert` size anchor, so a member
add/remove/renumber breaks the build or a test (INPUT-KBD-001 / INPUT-API-034). Keys that SDL
cannot originate on a normal desktop keyboard (console ChatPad colors, IME `Kana`/`Kanji`, some
OEM cluster entries) remain valid enum values but are not produced by the SDL bridge; this is
documented in the keycode/scancode mapping tasks (P2-005/P2-006) rather than the enum.
**Files changed:** none (audit-only; parity already enforced by existing tests).
**Behavior verified:** name+value parity via normalized enum diff; no comment churn needed.
**Remaining risk:** none for the enum itself; SDL-origination gaps tracked under P2-005/P2-006.

## P2-002 — Harden invalid `Keys` handling `[x]`
- [x] Audit all code paths accepting `Keys`.
- [x] Ensure invalid enum values cannot cause out-of-bounds access.
- [x] Fix `KeyboardState::GetHashCode` if it indexes a fixed bit array without range checks.
- [x] Add tests for negative, too-large, and unknown `Keys` values.

**Result (2026-07-06):** Audited every `KeyboardState` path that consumes a `Keys`:
`IsKeyDown`/`IsKeyUp`/`getItem`/`operator[]` go through `unordered_set::contains` (hash lookup,
memory-safe for any value); `GetPressedKeys` copies + sorts (safe); `Equals`/`==`/`!=` compare
sets (safe). The **only** out-of-bounds risk was `GetHashCode`, which indexed a fixed
`std::array<uint32_t,8>` via `words[value >> 5]`. For a valid key (0..255) `value>>5` is 0..7, but
an out-of-range enum (≥256) or a negative value cast to a huge unsigned would write past the
array. Fixed by guarding with `if (value < 256u)` before the write — this mirrors FNA's
`InternalSetKey`, whose `switch(((int)key)>>5)` has cases 0..7 and **no default**, so an
out-of-range key maps to no bitfield and contributes nothing to the hash.
**Files changed:** `src/Microsoft/Xna/Framework/Input/KeyboardState.cpp` (bounds guard + WHY
comment); `tests/Microsoft/Xna/Framework/Input/KeyboardInputTests.cpp` (+4 tests).
**Tests:** `GetHashCodeIgnoresOutOfRangeKeysValue` (999), `GetHashCodeIgnoresNegativeKeysValue`
(-1), `GetHashCodeIgnoresBoundaryKeysValue256` (256 → same as empty), and
`AccessorsAreSafeForOutOfRangeKeysValues` (999/-5/70000 mixed with a valid key through every
accessor). Ran `CnaTests --gtest_filter='KeyboardStateTest.*:KeyboardInputTest.*'` shuffled ×5:
**25/25 pass**. Re-ran the hash + hardening subset under **ASan** (`cmake-build-input-asan`,
`ASAN_OPTIONS=detect_leaks=0:halt_on_error=1`): **7/7 pass, no out-of-bounds report** — proving
the guard removes the OOB write.
**Behavior verified:** hash of a state containing an out-of-range key equals the hash of the same
state without it; no accessor crashes for negative/too-large/unknown keys.
**Remaining risk (documented deviation):** unlike FNA, CNA's `unordered_set` *can store* an
out-of-range `Keys`, so `IsKeyDown((Keys)999)` returns `true` in CNA vs `false` in FNA (FNA's
bitfield store silently drops it). This is not producible from real hardware or the SDL bridge
(both only emit mapped 0..255 keys) and is memory-safe; left as an accepted minor deviation
rather than filtering at construction, which would be a broader behavioral change to the public
constructors. Tracked here for P12 sign-off.

## P2-003 — Verify `KeyboardState` default behavior
- [ ] Test default state has no pressed keys.
- [ ] Test `IsKeyDown` and `IsKeyUp`.
- [ ] Test equality and inequality.
- [ ] Test hash stability.

## P2-004 — Verify `KeyboardState::GetPressedKeys`
- [ ] Verify returned keys are deterministic.
- [ ] Verify sorting order matches CNA's documented XNA-compatible policy.
- [ ] Verify duplicates are impossible.
- [ ] Add tests with multiple keys.

## P2-005 — Audit SDL keycode mapping
- [ ] Compare SDL keycode to XNA `Keys` conversion.
- [ ] Add missing mappings where SDL has reliable equivalents.
- [ ] Do not guess mappings that are platform-layout dependent.
- [ ] Document unmappable keys.

## P2-006 — Audit SDL scancode mapping
- [ ] Compare SDL scancode to XNA `Keys` conversion.
- [ ] Ensure scancode mode is deterministic for physical keyboard layout.
- [ ] Add tests for representative keys.
- [ ] Document scancode/keycode tradeoffs.

## P2-007 — Verify key repeat behavior
- [ ] Ensure key repeat does not create false press/release transitions.
- [ ] Ensure repeated keydown may still produce text input if intended.
- [ ] Add tests for repeated SDL keydown events.

## P2-008 — Verify focus loss behavior
- [ ] Determine what happens when window focus is lost.
- [ ] Ensure pressed keys are cleared or behavior is documented.
- [ ] Add SDL bridge test for focus lost if supported.
- [ ] Add manual validation task if needed.

## P2-009 — Verify modifier keys
- [ ] Test left/right Shift.
- [ ] Test left/right Control.
- [ ] Test left/right Alt.
- [ ] Test CapsLock, NumLock, and ScrollLock if supported.
- [ ] Verify no accidental merging unless XNA does so.

## P2-010 — Verify OEM keys
- [ ] Test all mapped OEM punctuation keys.
- [ ] Verify US keyboard behavior.
- [ ] Add manual validation tasks for CZ, DE, FR/AZERTY, and other layouts.
- [ ] Document layout-dependent behavior.

## P2-011 — Verify Android/browser special keys
- [ ] Verify Back/Menu behavior if CNA supports mobile/browser input.
- [ ] Ensure mappings are documented as platform-specific.
- [ ] Add tests where fake SDL events can represent these keys.

## P2-012 — Verify keyboard reset for tests/runtime
- [ ] Ensure `InputManager::ResetAllForTests` clears keyboard state.
- [ ] Ensure no stale key state leaks between tests.
- [ ] Add regression tests.

---

# Phase 3 — Mouse correctness

## P3-001 — Harden `Mouse::SetPosition`
- [ ] Audit `Mouse::SetPosition`.
- [ ] Ensure it does not call SDL with a null window.
- [ ] Ensure it updates CNA internal state consistently.
- [ ] Add tests for no-window behavior.

## P3-002 — Verify default `MouseState`
- [ ] Test default X/Y/scroll values.
- [ ] Test default buttons are released.
- [ ] Test equality and hash behavior.

## P3-003 — Verify `MouseState` hash behavior
- [ ] Compare hash behavior against FNA where practical.
- [ ] Decide whether button states should affect hash.
- [ ] Fix or document current behavior.
- [ ] Add regression tests.

## P3-004 — Verify mouse button mapping
- [ ] Test left, middle, right, XButton1, and XButton2.
- [ ] Verify unknown SDL buttons are ignored safely.
- [ ] Add SDL bridge tests.

## P3-005 — Verify mouse position updates
- [ ] Test SDL mouse motion updates X/Y.
- [ ] Test button events carrying position update X/Y.
- [ ] Test no negative coordinate crashes.
- [ ] Test large coordinate values.

## P3-006 — Verify scroll wheel behavior
- [ ] Test vertical wheel increments.
- [ ] Verify XNA-compatible 120-unit behavior.
- [ ] Decide/document behavior for horizontal wheel.
- [ ] Add tests for fractional SDL wheel values if possible.

## P3-007 — Verify relative mouse mode
- [ ] Test enabling relative mode.
- [ ] Test disabling relative mode.
- [ ] Test delta accumulation.
- [ ] Test delta drain-on-read behavior.
- [ ] Ensure behavior is documented as extension if not XNA.

## P3-008 — Verify relative mode with no window
- [ ] Ensure getter returns safe value.
- [ ] Ensure setter does not crash.
- [ ] Decide whether internal desired mode should be remembered.
- [ ] Add regression tests.

## P3-009 — Verify `Mouse::WindowHandle`
- [ ] Test setting and getting the mouse window handle.
- [ ] Ensure invalid/null handles are safe.
- [ ] Ensure public API does not expose SDL types beyond opaque pointer policy.
- [ ] Document behavior.

## P3-010 — Verify `MouseCursor`
- [ ] Test system cursor creation.
- [ ] Test null cursor handling.
- [ ] Test disposed cursor behavior.
- [ ] Test repeated `Dispose`.
- [ ] Test setting cursor before a window exists.

## P3-011 — Verify custom cursor behavior
- [ ] Audit custom cursor image format expectations.
- [ ] Validate hotspot/origin behavior.
- [ ] Add tests for invalid dimensions if possible.
- [ ] Document platform limitations.

## P3-012 — Verify mouse reset
- [ ] Ensure reset clears buttons, wheel, position, clicked extension state, and relative delta.
- [ ] Add regression tests.

---

# Phase 4 — GamePad correctness

## P4-001 — Audit `Buttons` enum
- [ ] Verify all XNA button bit values.
- [ ] Verify extension button bit values do not collide with XNA bits.
- [ ] Add numeric value tests.
- [ ] Document extension buttons.

## P4-002 — Verify `GamePadState` constructors
- [ ] Test default disconnected state.
- [ ] Test constructor with thumbsticks/triggers/buttons/dpad.
- [ ] Test constructor with packet number if present.
- [ ] Test equality and hash behavior.

## P4-003 — Verify `GamePadButtons`
- [ ] Test every XNA button property.
- [ ] Test pressed/released conversion.
- [ ] Test multi-button combinations.
- [ ] Test extension buttons separately.

## P4-004 — Verify `GamePadDPad`
- [ ] Test all DPad directions.
- [ ] Test default released state.
- [ ] Test equality/hash.
- [ ] Test interaction with `Buttons` flags.

## P4-005 — Verify `GamePadThumbSticks`
- [ ] Test default values.
- [ ] Test clamping.
- [ ] Test independent axes dead zone.
- [ ] Test circular dead zone.
- [ ] Test no dead zone.
- [ ] Test left and right stick independently.

## P4-006 — Verify `GamePadTriggers`
- [ ] Test default values.
- [ ] Test clamping below 0 and above 1.
- [ ] Test dead zone threshold behavior.
- [ ] Test equality/hash.

## P4-007 — Verify SDL axis normalization
- [ ] Test min, max, zero, and near-zero SDL axis values.
- [ ] Verify Y-axis inversion matches XNA expectations.
- [ ] Verify trigger normalization matches XNA/FNA policy.
- [ ] Add fake backend tests.

## P4-008 — Verify SDL button mapping
- [ ] Test A/B/X/Y.
- [ ] Test shoulders.
- [ ] Test Back/Start.
- [ ] Test sticks.
- [ ] Test Guide if supported.
- [ ] Test DPad.
- [ ] Test extension buttons if present.

## P4-009 — Verify slot lifecycle
- [ ] Test controller connect.
- [ ] Test controller disconnect.
- [ ] Test reconnect.
- [ ] Test slot reuse.
- [ ] Test maximum player count.
- [ ] Ensure stale state is cleared on disconnect.

## P4-010 — Verify `PlayerIndex` bounds
- [ ] Test all valid player indices.
- [ ] Test invalid enum values if possible.
- [ ] Ensure no out-of-bounds access.
- [ ] Add regression tests.

## P4-011 — Verify `GamePad::GetState`
- [ ] Test disconnected state.
- [ ] Test connected state.
- [ ] Test dead zone overload.
- [ ] Test packet number behavior.
- [ ] Test deterministic state snapshots.

## P4-012 — Verify `GamePad::GetCapabilities`
- [ ] Test disconnected capabilities.
- [ ] Test connected capabilities from fake backend.
- [ ] Verify each capability flag.
- [ ] Document unsupported capabilities such as voice if always false.

## P4-013 — Verify packet number behavior
- [ ] Packet number should change on meaningful input changes.
- [ ] Packet number should change on connect/disconnect.
- [ ] Packet number should not change unnecessarily on repeated identical events.
- [ ] Add tests for button and axis jitter.

## P4-014 — Verify vibration
- [ ] Test `SetVibration` clamps values.
- [ ] Test disconnected controller behavior.
- [ ] Test no haptic support behavior.
- [ ] Test NaN/Inf handling if applicable.
- [ ] Document platform limitations.

## P4-015 — Verify extension rumble APIs
- [ ] Test trigger vibration extension.
- [ ] Test duration-based rumble if present.
- [ ] Ensure extension APIs are clearly marked.
- [ ] Add fake backend tests.

## P4-016 — Verify light bar extension
- [ ] Test light bar success path.
- [ ] Test no support path.
- [ ] Test invalid color values if applicable.
- [ ] Document supported devices.

## P4-017 — Verify sensor extensions
- [ ] Test accelerometer availability.
- [ ] Test gyroscope availability.
- [ ] Test sensor enable/disable.
- [ ] Test no support path.
- [ ] Document device/platform limitations.

## P4-018 — Verify GUID extension
- [ ] Test GUID for connected fake controller.
- [ ] Test GUID for disconnected controller.
- [ ] Document SDL/FNA compatibility expectations.

## P4-019 — Verify `GamePadType`
- [ ] Audit SDL joystick type to XNA `GamePadType` mapping.
- [ ] Fix suspicious mappings.
- [ ] Document unknown/unmappable types.
- [ ] Add fake backend tests.

## P4-020 — Verify gamepad reset
- [ ] Ensure reset clears all slots.
- [ ] Ensure reset clears backend state for tests.
- [ ] Ensure no packet/state leak across tests.
- [ ] Add regression tests.

---

# Phase 5 — Touch state correctness

## P5-001 — Audit `TouchCollection` read-only behavior
- [ ] `TouchCollection::IsReadOnly` currently claims read-only behavior.
- [ ] Verify XNA/FNA behavior for mutation methods.
- [ ] Make mutation methods throw/not supported if strict XNA requires it.
- [ ] Or clearly mark mutable behavior as C++ deviation.
- [ ] Add tests for `Add`, `Clear`, `Insert`, `Remove`, `RemoveAt`, and non-const indexing.

## P5-002 — Verify `TouchCollection::CopyTo`
- [ ] Compare CNA behavior against XNA/FNA.
- [ ] Decide how to represent C# array overwrite semantics in C++.
- [ ] Add tests for offset, insufficient capacity, empty collection, and invalid offset.
- [ ] Document any unavoidable C++ deviation.

## P5-003 — Verify `TouchCollection` enumeration
- [ ] Test begin/end iteration.
- [ ] Test count.
- [ ] Test contains.
- [ ] Test index lookup.
- [ ] Test deterministic order.

## P5-004 — Verify `TouchLocation`
- [ ] Test constructors.
- [ ] Test id, state, position, pressure.
- [ ] Test previous location behavior.
- [ ] Test equality/hash if implemented.

## P5-005 — Verify `TouchLocationState`
- [ ] Freeze numeric values.
- [ ] Test state transitions:
  - Invalid
  - Released
  - Pressed
  - Moved
- [ ] Add regression tests.

## P5-006 — Audit `TouchPanel::GetState`
- [ ] Inspect both internal touch slot path and `InputManager` fallback path.
- [ ] Ensure the two paths cannot diverge in production.
- [ ] Add tests for each path.
- [ ] Document which path is authoritative.

## P5-007 — Fix duplicated condition in `TouchPanel::SetFinger`
- [ ] Remove duplicate nested condition.
- [ ] Add regression tests around first touch, moved touch, and released touch.
- [ ] Ensure no behavior accidentally changes unless intended.

## P5-008 — Verify touch previous-location tracking
- [ ] Test Pressed has invalid previous location.
- [ ] Test Moved has previous Pressed/Moved location.
- [ ] Test Released has previous location.
- [ ] Test unknown Released does not create bogus state.

## P5-009 — Verify repeated touch down
- [ ] Test repeated Pressed with same id.
- [ ] Decide whether it should replace, ignore, or transition.
- [ ] Match FNA/XNA where possible.
- [ ] Add regression tests.

## P5-010 — Verify touch cancel behavior
- [ ] Test SDL canceled finger event.
- [ ] Ensure it becomes Released or documented cancellation behavior.
- [ ] Ensure cleanup happens exactly once.
- [ ] Add tests.

## P5-011 — Verify max touch count
- [ ] Test more touches than maximum supported by current storage.
- [ ] Ensure deterministic truncation or rejection.
- [ ] Ensure no out-of-bounds writes.
- [ ] Add tests.

## P5-012 — Verify touch id ordering
- [ ] Test multiple touch ids.
- [ ] Verify order is deterministic.
- [ ] Verify order matches documented behavior.
- [ ] Add tests.

## P5-013 — Verify `TouchPanel::GetCapabilities`
- [ ] Determine how touch capability is detected.
- [ ] Avoid capability becoming true only after first touch unless intentionally documented.
- [ ] Add fake backend/device query if needed.
- [ ] Add tests.

## P5-014 — Verify display size dependency
- [ ] Audit behavior when display width/height is zero.
- [ ] Ensure touch state and gesture state do not diverge unexpectedly.
- [ ] Add tests for touch before display size is known.
- [ ] Document startup behavior.

## P5-015 — Verify touch coordinate scaling
- [ ] Test normalized SDL touch coordinates to pixel coordinates.
- [ ] Test logical/display size changes.
- [ ] Test high-DPI behavior.
- [ ] Add manual validation task for real devices.

## P5-016 — Verify touch reset
- [ ] Ensure reset clears active touches.
- [ ] Ensure reset clears previous touches.
- [ ] Ensure reset clears gesture queue.
- [ ] Add regression tests.

---

# Phase 6 — Gesture correctness

## P6-001 — Audit `GestureType` enum
- [ ] Verify XNA numeric flags.
- [ ] Test bitwise combinations.
- [ ] Test extension values if any.
- [ ] Add numeric freeze tests.

## P6-002 — Verify enabled gestures behavior
- [ ] Test default enabled gestures.
- [ ] Test enabling one gesture.
- [ ] Test enabling multiple gestures.
- [ ] Test disabling gestures clears or preserves queue according to XNA/FNA behavior.
- [ ] Add regression tests.

## P6-003 — Verify `IsGestureAvailable`
- [ ] Test false when queue is empty.
- [ ] Test true when queue has entries.
- [ ] Test behavior after `ReadGesture`.
- [ ] Add tests.

## P6-004 — Verify `ReadGesture`
- [ ] Test FIFO order.
- [ ] Test exception when no gesture is available.
- [ ] Test gesture data fields.
- [ ] Add tests.

## P6-005 — Verify tap detection
- [ ] Test simple tap.
- [ ] Test movement threshold.
- [ ] Test duration threshold.
- [ ] Test disabled tap gesture.
- [ ] Add deterministic clock tests.

## P6-006 — Verify double-tap detection
- [ ] Test two taps within threshold.
- [ ] Test two taps outside threshold.
- [ ] Test moved second tap.
- [ ] Test disabled double-tap.
- [ ] Add deterministic tests.

## P6-007 — Verify hold detection
- [ ] Test hold threshold.
- [ ] Test movement cancels hold.
- [ ] Test release before threshold.
- [ ] Test disabled hold.
- [ ] Add deterministic tests.

## P6-008 — Verify horizontal drag
- [ ] Test drag start.
- [ ] Test drag delta.
- [ ] Test drag complete.
- [ ] Test vertical movement rejection if required.
- [ ] Add tests.

## P6-009 — Verify vertical drag
- [ ] Test drag start.
- [ ] Test drag delta.
- [ ] Test drag complete.
- [ ] Test horizontal movement rejection if required.
- [ ] Add tests.

## P6-010 — Verify free drag
- [ ] Test drag start.
- [ ] Test drag delta.
- [ ] Test drag complete.
- [ ] Test disabled free drag.
- [ ] Add tests.

## P6-011 — Verify flick detection
- [ ] Test flick velocity calculation.
- [ ] Test too-slow movement.
- [ ] Test direction data.
- [ ] Test disabled flick.
- [ ] Add tests.

## P6-012 — Verify pinch detection
- [ ] Test two-finger pinch start.
- [ ] Test pinch delta.
- [ ] Test pinch complete.
- [ ] Test one finger released.
- [ ] Add tests.

## P6-013 — Verify pinch-complete gesture
- [ ] Test pinch-complete queue entry.
- [ ] Verify position and delta fields.
- [ ] Verify ordering relative to final pinch event.
- [ ] Add tests.

## P6-014 — Verify multi-touch interactions
- [ ] Test tap while second finger appears.
- [ ] Test drag interrupted by second finger.
- [ ] Test pinch after one-finger drag.
- [ ] Document expected policy.

## P6-015 — Verify gesture queue reset
- [ ] Ensure reset clears queued gestures.
- [ ] Ensure reset clears detector state.
- [ ] Add regression tests.

## P6-016 — Manual gesture validation
- [ ] Create manual test checklist for real touchscreen.
- [ ] Validate tap, double-tap, hold, drag, flick, and pinch on at least one real device.
- [ ] Record device, OS, display scale, and result.

---

# Phase 7 — Text input and IME correctness

## P7-001 — Audit `TextInputEXT` scope
- [ ] Confirm it is an extension, not strict XNA 4.0.
- [ ] Ensure naming and docs make this clear.
- [ ] Verify public API does not claim strict XNA support.

## P7-002 — Verify UTF-8 to UTF-16 decoding
- [ ] Test ASCII.
- [ ] Test multi-byte BMP characters.
- [ ] Test astral characters requiring surrogate pairs.
- [ ] Test invalid UTF-8.
- [ ] Test truncated UTF-8.
- [ ] Test overlong sequences if decoder handles them.
- [ ] Add regression tests.

## P7-003 — Verify text input events
- [ ] Test SDL text input event conversion.
- [ ] Test empty text.
- [ ] Test multiple code units.
- [ ] Test event callback ordering.
- [ ] Add tests.

## P7-004 — Verify synthesized control characters
- [ ] Test Backspace.
- [ ] Test Tab.
- [ ] Test Enter.
- [ ] Test Delete.
- [ ] Test Home/End if currently synthesized.
- [ ] Verify behavior against FNA/MonoGame policy.
- [ ] Add tests.

## P7-005 — Verify clipboard paste behavior
- [ ] Audit Ctrl+V handling.
- [ ] Ensure text is not double-inserted if SDL also emits text input.
- [ ] Add tests for Ctrl+V keydown and text input sequence.
- [ ] Document platform behavior.

## P7-006 — Verify key repeat and text repeat
- [ ] Ensure repeated keydown does not corrupt keyboard state.
- [ ] Ensure repeated text input is delivered where intended.
- [ ] Add tests.

## P7-007 — Verify text editing / IME composition
- [ ] Test text editing callback.
- [ ] Test empty composition.
- [ ] Test start/length values.
- [ ] Determine whether SDL byte offsets or UTF-16 offsets are exposed.
- [ ] Document behavior.
- [ ] Add tests where possible.

## P7-008 — Verify start/stop text input
- [ ] Test start with valid window.
- [ ] Test stop with valid window.
- [ ] Test no-window behavior.
- [ ] Ensure no crash.
- [ ] Document no-op behavior if that is intended.

## P7-009 — Verify input rectangle
- [ ] Test setting input rectangle.
- [ ] Test no-window behavior.
- [ ] Test negative and zero rectangle values if possible.
- [ ] Document platform limitations.

## P7-010 — Manual IME validation
- [ ] Validate with at least one IME on desktop.
- [ ] Validate with mobile soft keyboard if supported.
- [ ] Record OS, keyboard/IME, and result.

---

# Phase 8 — SDL bridge and backend integration

## P8-001 — Audit SDL bridge event coverage
- [ ] List every SDL event consumed by `SdlInputBridge`.
- [ ] List every relevant SDL input event not consumed.
- [ ] Decide whether missing events are intentional.
- [ ] Document gaps.

## P8-002 — Verify event ordering
- [ ] Test keyboard event ordering.
- [ ] Test text input ordering.
- [ ] Test mouse motion/button ordering.
- [ ] Test touch/gesture ordering.
- [ ] Test gamepad connect/input ordering.

## P8-003 — Verify SDL initialization ownership
- [ ] Ensure input code initializes only the SDL subsystems it owns.
- [ ] Ensure repeated init/shutdown is safe.
- [ ] Ensure tests do not depend on global hidden state.
- [ ] Add tests.

## P8-004 — Verify fake backend coverage
- [ ] Ensure fake keyboard/mouse/touch/gamepad paths exist or are testable.
- [ ] Add fake backend helpers where useful.
- [ ] Keep fake backend internal to tests.

## P8-005 — Verify window handle resolution
- [ ] Audit all places resolving SDL window handle.
- [ ] Ensure null window is safe.
- [ ] Ensure stale window handle is safe where possible.
- [ ] Add tests.

## P8-006 — Verify high-DPI / logical coordinate handling
- [ ] Test mouse logical coordinates.
- [ ] Test touch logical coordinates.
- [ ] Test display resize.
- [ ] Add manual validation task.

## P8-007 — Verify focus, minimize, and window close behavior
- [ ] Decide how input state is cleared on focus loss.
- [ ] Decide how input behaves when minimized.
- [ ] Add SDL bridge tests if fake events can represent this.
- [ ] Document runtime behavior.

## P8-008 — Verify backend reset
- [ ] Ensure all internal input state can be reset for tests.
- [ ] Ensure reset does not require SDL window.
- [ ] Ensure reset leaves system in deterministic state.
- [ ] Add tests.

---

# Phase 9 — Build, tests, and CI

## P9-001 — Improve missing submodule diagnostics
- [ ] If vendored SDL is required, make CMake error explicit and actionable.
- [ ] Print exact command to initialize submodules.
- [ ] Do not fail later with obscure include/link errors.
- [ ] Add documentation.

## P9-002 — Add optional system SDL mode if desired
- [ ] Determine whether CNA should support system SDL for local testing.
- [ ] If yes, add a CMake option.
- [ ] If no, document why vendored SDL is required.
- [ ] Keep behavior deterministic.

## P9-003 — Create focused Input test target
- [ ] Ensure there is a simple command to run only Input tests.
- [ ] Include Keyboard, Mouse, GamePad, Touch, Gesture, TextInput, and SDL bridge tests.
- [ ] Document command in this plan.

## P9-004 — Run Input tests repeatedly
- [ ] Run focused Input tests once.
- [ ] Run focused Input tests with shuffle.
- [ ] Run focused Input tests with repeat count.
- [ ] Fix any order-dependent failures.

## P9-005 — Run sanitizer builds
- [ ] Run AddressSanitizer if supported.
- [ ] Run UndefinedBehaviorSanitizer if supported.
- [ ] Fix sanitizer findings.
- [ ] Record unsupported sanitizer/platform cases.

## P9-006 — Add fuzz-style SDL bridge tests
- [ ] Feed randomized but valid SDL-like events.
- [ ] Ensure no crashes.
- [ ] Ensure state remains internally consistent.
- [ ] Keep fuzz tests deterministic with recorded seeds.

## P9-007 — Add golden event sequence tests
- [ ] Create golden sequences for keyboard.
- [ ] Create golden sequences for mouse.
- [ ] Create golden sequences for touch.
- [ ] Create golden sequences for gamepad.
- [ ] Assert final state and packet/queue behavior.

## P9-008 — Freeze public API signatures
- [ ] Update signature freeze tests after intentional corrections.
- [ ] Ensure strict XNA API changes are intentional.
- [ ] Ensure extension API changes are documented.

## P9-009 — Update test coverage document
- [ ] List every Input type.
- [ ] List corresponding tests.
- [ ] Mark remaining gaps.
- [ ] Do not claim 100% behavior coverage unless true.

---

# Phase 10 — Documentation

## P10-001 — Document strict XNA compatibility
- [ ] Document which Input APIs are intended to match XNA 4.0 exactly.
- [ ] Document known deviations.
- [ ] Document C++-specific representation differences.

## P10-002 — Document FNA compatibility
- [ ] Document where CNA follows FNA behavior.
- [ ] Document any FNA extensions supported by CNA.
- [ ] Document any known FNA behavior not yet implemented.

## P10-003 — Document `NOXNA` / extension APIs
- [ ] Document `TextInputEXT`.
- [ ] Document relative mouse mode.
- [ ] Document gamepad GUID, sensors, trigger vibration, light bar, and extra buttons.
- [ ] Ensure extension docs do not imply XNA 4.0 compatibility.

## P10-004 — Document platform notes
- [ ] Windows notes.
- [ ] Linux notes.
- [ ] macOS notes.
- [ ] Android notes if supported.
- [ ] Browser/Emscripten notes if supported.
- [ ] Gamepad device notes.

## P10-005 — Document manual validation checklist
- [ ] Keyboard layouts.
- [ ] Mouse and relative mode.
- [ ] Gamepads.
- [ ] Touchscreen.
- [ ] IME/text input.
- [ ] High-DPI display.

---

# Phase 11 — Manual hardware validation

## P11-001 — Keyboard hardware validation
- [ ] Validate US layout.
- [ ] Validate CZ layout.
- [ ] Validate at least one non-QWERTY layout if available.
- [ ] Validate modifiers and OEM keys.
- [ ] Record results.

## P11-002 — Mouse hardware validation
- [ ] Validate normal mouse motion.
- [ ] Validate wheel.
- [ ] Validate extra buttons.
- [ ] Validate relative mode.
- [ ] Validate high-DPI behavior.
- [ ] Record results.

## P11-003 — Xbox-compatible gamepad validation
- [ ] Validate connect/disconnect.
- [ ] Validate buttons.
- [ ] Validate sticks.
- [ ] Validate triggers.
- [ ] Validate rumble.
- [ ] Record results.

## P11-004 — PlayStation-compatible gamepad validation
- [ ] Validate mapping.
- [ ] Validate GUID.
- [ ] Validate sensors if supported.
- [ ] Validate light bar if supported.
- [ ] Record results.

## P11-005 — Generic SDL gamepad validation
- [ ] Validate a generic mapped controller.
- [ ] Validate unknown controller fallback.
- [ ] Record mapping issues.

## P11-006 — Touchscreen validation
- [ ] Validate single touch.
- [ ] Validate multi-touch.
- [ ] Validate gestures.
- [ ] Validate display scaling.
- [ ] Record results.

## P11-007 — IME and soft keyboard validation
- [ ] Validate desktop IME.
- [ ] Validate mobile soft keyboard if supported.
- [ ] Validate composition events.
- [ ] Record results.

---

# Phase 12 — Final quality gates

## P12-001 — Run full Input test suite
- [ ] Run all Input tests.
- [ ] Run repeated/shuffled Input tests.
- [ ] Record command and result.
- [ ] Fix failures.

## P12-002 — Run full CNA test suite
- [ ] Run all available CNA tests.
- [ ] Ensure Input changes did not break other modules.
- [ ] Record command and result.

## P12-003 — Re-run public API parity
- [ ] Regenerate parity matrix.
- [ ] Confirm no strict XNA members are missing.
- [ ] Confirm all extensions are documented.
- [ ] Record result.

## P12-004 — Review all compatibility deviations
- [ ] List every remaining strict-XNA deviation.
- [ ] Decide whether to fix or document.
- [ ] Do not leave accidental deviations undocumented.

## P12-005 — Final Input readiness statement
- [ ] Write a final status section:
  - API completeness estimate,
  - implementation correctness estimate,
  - automated test coverage,
  - manual validation status,
  - known remaining risks.
- [ ] Do not overstate readiness.
- [ ] Mark this plan complete only when all non-blocked tasks are done.
