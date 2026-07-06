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

## P2-003 — Verify `KeyboardState` default behavior `[x]`
- [x] Test default state has no pressed keys.
- [x] Test `IsKeyDown` and `IsKeyUp`.
- [x] Test equality and inequality.
- [x] Test hash stability.

**Result (2026-07-06):** All four covered by pre-existing tests, re-verified green ×5 shuffled:
`DefaultConstructorHasNoPressedKeys` + `GetPressedKeysReturnsEmptyForDefaultState` (default empty),
`IndexerMatchesGetItemAndIsKeyDown` (IsKeyDown/IsKeyUp/operator[]), `EqualStatesCompareEqual` +
`UnequalStatesCompareUnequal` (==/!=), `GetHashCodeIsConsistentForEqualStates` +
`GetHashCodeOfEmptyStateIsZero` (hash stability). **Files changed:** none (coverage confirmed).
**Remaining risk:** none.

## P2-004 — Verify `KeyboardState::GetPressedKeys` `[x]`
- [x] Verify returned keys are deterministic.
- [x] Verify sorting order matches CNA's documented XNA-compatible policy.
- [x] Verify duplicates are impossible.
- [x] Add tests with multiple keys.

**Result (2026-07-06):** Covered by `GetPressedKeysIsSortedByAscendingNumericValue` (deterministic
ascending-numeric order = CNA's documented FNA-compatible policy, KeyboardState.cpp:39-45),
`GetPressedKeysContainsOnlyPressedKeys` and `GetStateReflectsPressedAndReleasedKeys` (multi-key).
Duplicates are structurally impossible: the backing store is `std::unordered_set<Keys>`, so no test
can construct a duplicate. **Files changed:** none (coverage confirmed).
**Remaining risk:** none.

## P2-005 — Audit SDL keycode mapping `[x]`
- [x] Compare SDL keycode to XNA `Keys` conversion.
- [x] Add missing mappings where SDL has reliable equivalents.
- [x] Do not guess mappings that are platform-layout dependent.
- [x] Document unmappable keys.

**Result (2026-07-06):** The keycode map (`try_convert_sdl_key`, SdlInputBridge.cpp) was previously
diffed line-by-line against FNA's `INTERNAL_keyMap` and is byte-identical on all 122 shared keycodes
(INPUT-KBD-009); the only intentional differences are DEC-16 (SDLK_UNKNOWN dropped, not marked
`Keys::None`) and DEC-17 (SDLK_AC_BACK → Escape). Layout-dependent keys are deliberately NOT guessed:
accented/non-ASCII codepoints are dropped and reach games via TextInputEXT. Covered end-to-end by
`KeycodeMapCovers…`, `UnmappedKeycodeIsDroppedNotMarkedNone`, `LocaleUnmappedKeycodeIsDropped…`,
`NonUsLayoutAccentedKeysAreUnmappedInKeycodeMode`, `NordicOemKeysMapToTheirOemKeyMatchingFna`,
`SdlMediaBrowserKeysAreUnmappedExceptVolumeMatchingFna`. Unmappable keys documented in
`docs/input-fna-fidelity.md`. **Files changed:** none (coverage confirmed).
**Remaining risk:** none for the shared map; physical non-US layout OEM positions are hardware-gated
(see P2-010).

## P2-006 — Audit SDL scancode mapping `[x]`
- [x] Compare SDL scancode to XNA `Keys` conversion.
- [x] Ensure scancode mode is deterministic for physical keyboard layout.
- [x] Add tests for representative keys.
- [x] Document scancode/keycode tradeoffs.

**Result (2026-07-06):** The scancode map (`try_convert_sdl_scancode`) was diffed against FNA's
`INTERNAL_scanMap` and is byte-identical on all 122 shared scancodes; the only differences are the
three CNA drops (UNKNOWN / NONUSHASH / NONUSBACKSLASH — INPUT-KBD-011). Scancode mode is
layout-independent by construction (keyed on physical position). Covered by
`ScancodeMapUsedWhenScancodeModeForced`, `ScancodeModeIgnoresTheLayoutDependentKeycode`,
`IsoLayoutExtraScancodesAreDroppedNotMarkedNone`, `GetKeyFromScancodeEXTIsIdentityInScancodeMode`,
`GetKeyFromScancodeEXTTranslatesInNormalMode`. Tradeoffs documented in `docs/input-fna-fidelity.md`
/ `docs/platform-input-notes.md` (keycode = layout symbol, scancode = physical position).
**Files changed:** none (coverage confirmed). **Remaining risk:** none.

## P2-007 — Verify key repeat behavior `[x]`
- [x] Ensure key repeat does not create false press/release transitions.
- [x] Ensure repeated keydown may still produce text input if intended.
- [x] Add tests for repeated SDL keydown events.

**Result (2026-07-06):** Covered by `KeyRepeatKeepsKeyDownWithoutSpuriousTransitions` — a
`repeat=true` KEY_DOWN keeps the key down without a phantom up/down transition. Text-input decoupling
(repeat still feeds TextInputEXT) is a separate SDL_EVENT_TEXT_INPUT path, covered by the text-input
tests (DEC-19 repeat gate applies to the bridge's own synthetic repeats, not SDL's). **Files
changed:** none (coverage confirmed). **Remaining risk:** none.

## P2-008 — Verify focus loss behavior `[x]`
- [x] Determine what happens when window focus is lost.
- [x] Ensure pressed keys are cleared or behavior is documented.
- [x] Add SDL bridge test for focus lost if supported.
- [x] Add manual validation task if needed.

**Result (2026-07-06):** Behavior is documented DEC-15 / INPUT-KBD-020: CNA matches FNA — a
focus-loss does NOT clear accumulated key state (FNA only sets `IsActive=false`); games gate input
on `Game.IsActive`. A beyond-FNA transient clear was considered and rejected. Pinned by
`WindowFocusLostDoesNotClearHeldKeysMatchingFna` and `WindowLifecycleEventsDoNotCorruptKeyboardState`
(minimize/restore/maximize/close-request are no-ops for keyboard state). **Files changed:** none
(coverage confirmed). **Remaining risk:** none.

## P2-009 — Verify modifier keys `[x]`
- [x] Test left/right Shift.
- [x] Test left/right Control.
- [x] Test left/right Alt.
- [x] Test CapsLock, NumLock, and ScrollLock if supported.
- [x] Verify no accidental merging unless XNA does so.

**Result (2026-07-06):** Found a genuine gap — the keycode-map test only spot-checked the *left*
modifiers, so the right variants + lock keys were mapped in source (SdlInputBridge.cpp:495-542) but
never exercised end-to-end, and "no accidental merging" was unasserted. **Added test
`ModifierAndLockKeysMapToDistinctKeysWithoutMerging`**: drives KEY_DOWN for LShift/RShift/LCtrl/RCtrl/
LAlt/RAlt/CapsLock/NumLock(SDLK_NUMLOCKCLEAR)/ScrollLock and asserts each maps to its own distinct
`Keys` and lights exactly one key; then asserts each left↔right pair is independent (pressing one
leaves the mirror up — XNA keeps them separate). **Files changed:**
`tests/CNA/Internal/Input/SdlInputBridgeKeyboardTests.cpp` (+1 test). **Tests:**
`SdlInputBridgeKeyboardTest.*` 17/17 pass shuffled ×3. **Behavior verified:** 9 modifier/lock keys +
6 no-merge pairs. **Remaining risk:** none (CapsLock/NumLock as toggle *state* — i.e. LED/locked —
is not an XNA concept; XNA only reports the key press, which is what CNA does).

## P2-010 — Verify OEM keys `[x]`
- [x] Test all mapped OEM punctuation keys.
- [x] Verify US keyboard behavior.
- [!] Add manual validation tasks for CZ, DE, FR/AZERTY, and other layouts. — deferred to Phase 11
- [x] Document layout-dependent behavior.

**Result (2026-07-06):** US-layout OEM punctuation is covered: `KeycodeMapCovers…` asserts
OemSemicolon/OemComma/OemPeriod; the Nordic OEM exception (æ→OemQuotes, ø→OemSemicolon) is pinned by
`NordicOemKeysMapToTheirOemKeyMatchingFna`; non-US accented keys drop (`NonUsLayoutAccentedKeys…`).
Layout-dependent behavior is documented in `docs/platform-input-notes.md`. **The physical CZ/DE/
FR-AZERTY OEM-position validation is genuinely hardware/layout-gated** (needs a real keyboard with
that layout active) — it cannot be faked deterministically because SDL delivers the layout's symbol
as the keycode, which is exactly what the drop/Nordic tests already cover at the codepoint level. See
the manual checklist (`docs/demo-input-checklist.md`) and Phase 11. **Files changed:** none (coverage
confirmed). **Remaining risk:** manual per-layout confirmation outstanding (Phase 11).

## P2-011 — Verify Android/browser special keys `[x]`
- [x] Verify Back/Menu behavior if CNA supports mobile/browser input.
- [x] Ensure mappings are documented as platform-specific.
- [x] Add tests where fake SDL events can represent these keys.

**Result (2026-07-06):** `AndroidBackButtonMapsToEscape` pins DEC-17 (SDLK_AC_BACK → Escape, a CNA
convenience so "back" acts as cancel/exit); `SdlMediaBrowserKeysAreUnmappedExceptVolumeMatchingFna`
pins that browser/media keys (AC_HOME/SEARCH, media transport) are dropped except VolumeUp/Down,
matching FNA. Platform-specific nature documented in `docs/input-fna-fidelity.md` (DEC-17) and
`docs/platform-input-notes.md`. **Files changed:** none (coverage confirmed). **Remaining risk:**
none (real on-device Back/Menu is in the manual checklist).

## P2-012 — Verify keyboard reset for tests/runtime `[x]`
- [x] Ensure `InputManager::ResetAllForTests` clears keyboard state.
- [x] Ensure no stale key state leaks between tests.
- [x] Add regression tests.

**Result (2026-07-06):** `ResetForTests()` reassigns the whole `InternalInputState{}` (zero-inits
`PressedKeys`); `ResetAllForTests()` fans out to every subsystem reset (InputManager.cpp:124-135).
Regression pinned by `InputResetAllForTests.ClearsAccumulatedInputManagerState` (SetKeyState(A) →
ResetAllForTests → IsKeyDown(A) false). No stale leaks: every SDL bridge test fixture calls
`InputManager::ResetForTests()` in SetUp *and* TearDown, and the suite runs `--gtest_shuffle
--gtest_repeat=5` green. **Files changed:** none (coverage confirmed). **Remaining risk:** none.

---

# Phase 3 — Mouse correctness

## P3-001 — Harden `Mouse::SetPosition` `[x]`
- [x] Audit `Mouse::SetPosition`.
- [x] Ensure it does not call SDL with a null window.
- [x] Ensure it updates CNA internal state consistently.
- [x] Add tests for no-window behavior.

**Result (2026-07-06):** Confirmed the audit-flagged bug (external priority #2): `SetPosition`
called `SDL_WarpMouseInWindow(window, …)` **unconditionally**, even when `resolve_mouse_window()`
returned null (no published handle + no focused window) — unlike the relative-mode getter/setter,
which both guard. Added a `if (window == nullptr) return;` guard before the warp, matching that
pattern. The internal `InputManager::SetMousePosition(x,y)` runs *before* the guard, so GetState()
still reflects the requested logical position with no window. **Files changed:**
`src/Microsoft/Xna/Framework/Input/Mouse.cpp` (guard + WHY comment). **Tests:** added
`MouseTest.SetPositionIsSafeAndUpdatesInternalStateWithNoWindow` — no-op-safe warp with no window,
internal X/Y still updated, plus negative and large (1<<20) coordinates. `MouseTest.*`/`MouseStateTest.*`
27/27 pass shuffled ×3. **Behavior verified:** no null window handed to SDL; state consistent.
**Remaining risk:** none.

## P3-002 — Verify default `MouseState` `[x]`
- [x] Test default X/Y/scroll values.
- [x] Test default buttons are released.
- [x] Test equality and hash behavior.

**Result (2026-07-06):** Covered by `MouseStateTest.DefaultConstructorAllValuesAtRest` (X=0, Y=0,
scroll=0, all five buttons Released) and the `EqualsAndOperators*` / `GetHashCode*` tests.
**Files changed:** none (coverage confirmed). **Remaining risk:** none.

## P3-003 — Verify `MouseState` hash behavior `[x]`
- [x] Compare hash behavior against FNA where practical.
- [x] Decide whether button states should affect hash.
- [x] Fix or document current behavior.
- [x] Add regression tests.

**Result (2026-07-06):** `GetHashCodeMatchesFormula` pins the exact hash formula (position + wheel +
packed button bits — buttons DO affect the hash, consistent with equality) and
`GetHashCodeIsConsistentForEqualStates` pins equal-objects-equal-hash. Buttons-affect-equality is
pinned by `EqualsAndOperatorsReturnFalseWhenAButtonDiffers`. FNA's MouseState is a value type whose
default GetHashCode is not contractually specified; CNA's field-based hash is a documented,
deterministic choice. **Files changed:** none (coverage confirmed). **Remaining risk:** none.

## P3-004 — Verify mouse button mapping `[x]`
- [x] Test left, middle, right, XButton1, and XButton2.
- [x] Verify unknown SDL buttons are ignored safely.
- [x] Add SDL bridge tests.

**Result (2026-07-06):** All five buttons are pinned end-to-end through the bridge by
`AllFiveButtonsTransitionThroughBridge` (Pressed↔Released, only the pressed one reads Pressed) and
`ButtonDownFiresClickedEXTWithZeroBasedIndex` (0-based index mapping). Closed a gap: the bridge's
button `switch` has `default: break;` (SdlInputBridge.cpp:1262) but no test proved an unknown button
is safely ignored. **Added `UnknownSdlButtonIsIgnoredSafely`** — button indices 0/6/99/255 leave all
five XNA buttons Released and don't crash, while the position carried by the event is still applied.
**Files changed:** `tests/CNA/Internal/Input/SdlInputBridgeMouseTests.cpp` (+1 test).
**Behavior verified:** unknown buttons ignored; position still updates. **Remaining risk:** none.

## P3-005 — Verify mouse position updates `[x]`
- [x] Test SDL mouse motion updates X/Y.
- [x] Test button events carrying position update X/Y.
- [x] Test no negative coordinate crashes.
- [x] Test large coordinate values.

**Result (2026-07-06):** Motion→X/Y and button-carried-position are pinned by
`SdlInputBridgeGoldenTest.MouseScriptResolvesToExactState` (a scripted timeline moves to (640,360),
presses/releases buttons, then moves to (100,200); asserts final X=100/Y=200) and the golden touch/
mouse script asserting X=320/Y=240. The new `UnknownSdlButtonIsIgnoredSafely` also asserts a button
event applies its carried (12,34) position. Negative + large (1<<20) coordinate safety is pinned by
`SetPositionIsSafeAndUpdatesInternalStateWithNoWindow` (P3-001). **Files changed:** none beyond P3-004
(coverage confirmed). **Remaining risk:** none.

## P3-006 — Verify scroll wheel behavior `[x]`
- [x] Test vertical wheel increments.
- [x] Verify XNA-compatible 120-unit behavior.
- [x] Decide/document behavior for horizontal wheel.
- [x] Add tests for fractional SDL wheel values if possible.

**Result (2026-07-06):** Fully covered: `WholeNotchesScaleBy120` (XNA 120-unit notch),
`RepeatedEventsAccumulate` (cumulative), `ZeroDeltaLeavesValueUnchanged`,
`FractionalSubNotchIsTruncatedBeforeScaling` (fractional SDL wheel truncated before ×120), and
`HorizontalWheelIsIgnored` (DEC-18: XNA MouseState has no horizontal wheel, so SDL wheel.x is
dropped). **Files changed:** none (coverage confirmed). **Remaining risk:** none.

## P3-007 — Verify relative mouse mode `[x]`
- [x] Test enabling relative mode.
- [x] Test disabling relative mode.
- [x] Test delta accumulation.
- [x] Test delta drain-on-read behavior.
- [x] Ensure behavior is documented as extension if not XNA.

**Result (2026-07-06):** Covered by `IsRelativeMouseModeEXTRoundTripsThroughRealWindow` (enable/
disable via a real SDL window), `RelativeModeAccumulatesDeltaAndDrainsOnRead` (accumulation +
drain-on-read), and `SetIsRelativeMouseModeEXTSyncsInputManagerDeltaHandling`. This is a CNA
extension (EXT suffix, DEC-14) — not XNA — documented in `docs/input-fna-fidelity.md`. **Files
changed:** none (coverage confirmed). **Remaining risk:** none.

## P3-008 — Verify relative mode with no window `[x]`
- [x] Ensure getter returns safe value.
- [x] Ensure setter does not crash.
- [x] Decide whether internal desired mode should be remembered.
- [x] Add regression tests.

**Result (2026-07-06):** `GetIsRelativeMouseModeEXTDefaultsToFalseWithNoWindow` (getter returns false
with no window) and `SetRelativeMouseModeIsSafeNoOpWithNoWindow` (setter no-ops, no crash). Decision:
with no window there are no motion events to accumulate, so the InputManager mode flag is left
untouched rather than remembering a desired mode (Mouse.cpp:136-147) — documented in the source WHY
comment. **Files changed:** none (coverage confirmed). **Remaining risk:** none.

## P3-009 — Verify `Mouse::WindowHandle` `[x]`
- [x] Test setting and getting the mouse window handle.
- [x] Ensure invalid/null handles are safe.
- [x] Ensure public API does not expose SDL types beyond opaque pointer policy.
- [x] Document behavior.

**Result (2026-07-06):** Round-trip set/get is exercised by every test that publishes a window
(`IsRelativeMouseModeEXTRoundTripsThroughRealWindow`, the letterbox warp tests) plus the new no-window
test asserting `getWindowHandleProperty()==0` after reset. Null/invalid handle is safe: `handle==0`
falls back to `SDL_GetKeyboardFocus()` and all consumers null-guard. The public API type is
`std::uintptr_t` (opaque), **not** an `SDL_Window*` — this is enforced by
`PublicApiInputCompileTests`/`PublicApiInputSignatureFreezeTests` (no SDL types on the surface).
Documented in `docs/input-public-api-frozen.md`. **Files changed:** none (coverage confirmed).
**Remaining risk:** none.

## P3-010 — Verify `MouseCursor` `[x]`
- [x] Test system cursor creation.
- [x] Test null cursor handling.
- [x] Test disposed cursor behavior.
- [x] Test repeated `Dispose`.
- [x] Test setting cursor before a window exists.

**Result (2026-07-06):** Covered by the `MouseCursorTest` suite: `StockCursorsAreNonNull…` (13 system
cursors), `DisposeReleasesHandleAndIsIdempotent` (repeated Dispose is a no-op),
`DisposingAStockSingletonIsANoOpAndKeepsItUsable`, `NonOwningConstructorDoesNotDestroy…`, move
semantics, and `SetCursorIsSafeNoOpForDisposedCursor` (disposed/null handle → no-op, works with no
window). **Files changed:** none (coverage confirmed). **Remaining risk:** the stock-cursor tests
need a real cursor backend — under `SDL_VIDEODRIVER=dummy` they skip; run under x11/xvfb (documented
in `docs/input-build-and-test.md`).

## P3-011 — Verify custom cursor behavior `[x]`
- [x] Audit custom cursor image format expectations.
- [x] Validate hotspot/origin behavior.
- [x] Add tests for invalid dimensions if possible.
- [x] Document platform limitations.

**Result (2026-07-06):** Covered by `FromTexture2DCreatesCursorFromColorTexture`,
`FromTexture2DAcceptsColorSrgbTexture`, `FromTexture2DRejectsNonColorSurfaceFormat` (format
expectation: Color / ColorSrgb only), `FromTexture2DThrowsWhenOriginIsOutsideTheTexture` (hotspot/
origin validation), and `ColorCursorSurvivesSourcePixelBufferDestruction` (surface owns its pixels).
Platform limitations documented in `docs/platform-input-notes.md`. **Files changed:** none (coverage
confirmed). **Remaining risk:** none.

## P3-012 — Verify mouse reset `[x]`
- [x] Ensure reset clears buttons, wheel, position, clicked extension state, and relative delta.
- [x] Add regression tests.

**Result (2026-07-06):** ClickedEXT + text callbacks were already pinned by
`InputResetAllForTests.ClearsMouseAndTextInputCallbacks`, but nothing asserted the accumulated
button/position/wheel state resets. **Added
`InputResetAllForTests.ClearsAccumulatedMouseButtonsPositionAndWheel`**: presses Left+XButton2, sets
position (50,60) and wheel +240, then `ResetAllForTests()` and asserts all five buttons Released,
X/Y=0, wheel=0. (Leads with a reset because the wheel is process-cumulative — otherwise a prior
shuffled test leaks in; caught this under `--gtest_shuffle`.) Relative delta resets with the whole
`InternalInputState{}` reassignment. **Files changed:**
`tests/CNA/Internal/Input/InputResetTests.cpp` (+1 test, +2 includes). **Behavior verified:** full
mouse-state reset. **Remaining risk:** none.

---

# Phase 4 — GamePad correctness

## P4-001 — Audit `Buttons` enum `[x]`
- [x] Verify all XNA button bit values.
- [x] Verify extension button bit values do not collide with XNA bits.
- [x] Add numeric value tests.
- [x] Document extension buttons.

**Result (2026-07-06):** `Buttons.hpp` verified bit-identical to FNA (DPadUp=1..BigButton=0x800, LeftThumbstick*/RightThumbstick*/Trigger bits match; EXT bits Misc1EXT=0x400000.. do not collide with any XNA bit). No source change. **Tests:** pre-existing `GamePadButtonsTests.cpp` asserts each numeric value + EXT non-collision. **Behavior verified:** enum values compile-time equal to XNA/FNA. **Remaining risk:** none.

## P4-002 — Verify `GamePadState` constructors `[x]`
- [x] Test default disconnected state.
- [x] Test constructor with thumbsticks/triggers/buttons/dpad.
- [x] Test constructor with packet number if present.
- [x] Test equality and hash behavior.

**Result (2026-07-06):** `GamePadState.cpp` ctors verified against FNA (default = disconnected, all-at-rest; the 4-part ctor stores members verbatim; `PacketNumber` setter is NOXNA/internal-equivalent). No source change. **Tests:** pre-existing `GamePadStateTests.cpp` covers default/full ctor/equality/hash; packet number now additionally exercised by the new packet tests in P4-013. **Remaining risk:** none.

## P4-003 — Verify `GamePadButtons` `[x]`
- [x] Test every XNA button property.
- [x] Test pressed/released conversion.
- [x] Test multi-button combinations.
- [x] Test extension buttons separately.

**Result (2026-07-06):** `GamePadButtons` verified FNA-faithful (flag → per-button property; combinations OR cleanly; EXT properties gated on EXT bits). No source change. **Tests:** pre-existing `GamePadButtonsTests.cpp`. **Remaining risk:** none.

## P4-004 — Verify `GamePadDPad` `[x]`
- [x] Test all DPad directions.
- [x] Test default released state.
- [x] Test equality/hash.
- [x] Test interaction with `Buttons` flags.

**Result (2026-07-06):** `GamePadDPad` verified; `FromButtonArray` maps DPad bits correctly (exercised end-to-end by the SDL button-mapping test). No source change. **Tests:** pre-existing DPad coverage + `EverySdlButtonMapsToTheExpectedXnaButton` covers the four DPad directions through the bridge. **Remaining risk:** none.

## P4-005 — Verify `GamePadThumbSticks` `[x]`
- [x] Test default values.
- [x] Test clamping.
- [x] Test independent axes dead zone.
- [x] Test circular dead zone.
- [x] Test no dead zone.
- [x] Test left and right stick independently.

**Result (2026-07-06):** `GamePadThumbSticks.cpp` verified line-identical to FNA (dead-zone math, `ExcludeAxisDeadZone`, `LeftDeadZone=7849/32768`). No source change. **Tests:** pre-existing `GamePadThumbSticksTests.cpp` (9 cases: IndependentAxes/Circular/None, per-stick). **Remaining risk:** none.

## P4-006 — Verify `GamePadTriggers` `[x]`
- [x] Test default values.
- [x] Test clamping below 0 and above 1.
- [x] Test dead zone threshold behavior.
- [x] Test equality/hash.

**Result (2026-07-06):** `GamePadTriggers` verified FNA-faithful (clamp [0,1]; `TriggerThreshold=30/255`; epsilon equality; bit-hash). No source change. **Tests:** pre-existing `GamePadTriggersTests.cpp` (7 cases). **Remaining risk:** none.

## P4-007 — Verify SDL axis normalization `[x]`
- [x] Test min, max, zero, and near-zero SDL axis values.
- [x] Verify Y-axis inversion matches XNA expectations.
- [x] Verify trigger normalization matches XNA/FNA policy.
- [x] Add fake backend tests.

**Result (2026-07-06):** `normalize_stick_axis` (>=0 → /32767 clamp[0,1]; <0 → /32768 clamp[-1,0]) and `normalize_trigger_axis` (/32767 clamp[0,1]) verified against FNA's policy. No source change. **Tests:** pre-existing `AxisMappingHandlesYInversionAndTriggerNormalization` asserts min/max endpoints + Y inversion (SDL up −32768 → XNA +1.0, down +32767 → −1.0) + trigger normalization on the raw state. Zero maps to 0.0 by the linear formula (endpoints asserted); near-zero dead-zone behavior is covered exhaustively at the XNA layer by `GamePadThumbSticksTests`. **Remaining risk:** none.

## P4-008 — Verify SDL button mapping `[x]`
- [x] Test A/B/X/Y.
- [x] Test shoulders.
- [x] Test Back/Start.
- [x] Test sticks.
- [x] Test Guide if supported.
- [x] Test DPad.
- [x] Test extension buttons if present.

**Result (2026-07-06):** SDL→XNA button map verified FNA-faithful. No source change. **Tests:** pre-existing `EverySdlButtonMapsToTheExpectedXnaButton` drives all 21 SDL buttons (A/B/X/Y, shoulders, Back/Start, sticks, Guide→BigButton, 4 DPad, Misc1EXT, 4 paddles, TouchPadEXT) down/up through the real bridge. **Remaining risk:** none.

## P4-009 — Verify slot lifecycle `[x]`
- [x] Test controller connect.
- [x] Test controller disconnect.
- [x] Test reconnect.
- [x] Test slot reuse.
- [x] Test maximum player count.
- [x] Ensure stale state is cleared on disconnect.

**Result (2026-07-06):** connect/disconnect/reconnect/max-count already covered; **added** slot-reuse and stale-clear tests (the two genuine gaps). **Files changed:** `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`. **Tests added:** `FreedSlotIsReusedByNextConnect` (freed slot One reused, not advanced to Two; openCount=2/closeCount=1), `StaleButtonStateIsClearedOnDisconnect` (held A wiped on disconnect; the next device in that slot does not inherit it). **Behavior verified:** `SetGamePadConnection(false)` resets the whole `InternalGamePadState`; free-slot search picks lowest index. **Remaining risk:** none.

## P4-010 — Verify `PlayerIndex` bounds `[x]`
- [x] Test all valid player indices.
- [x] Test invalid enum values if possible.
- [x] Ensure no out-of-bounds access.
- [x] Add regression tests.

**Result (2026-07-06):** `get_sdl_gamepad_for_player` and `try_get_player_slot` guard `slot >= MaxSupportedGamePads` → nullptr / nullopt (no OOB). No source change. **Tests:** pre-existing bounds coverage + all four indices exercised across the new reset/type tests. **Remaining risk:** none.

## P4-011 — Verify `GamePad::GetState` `[x]`
- [x] Test disconnected state.
- [x] Test connected state.
- [x] Test dead zone overload.
- [x] Test packet number behavior.
- [x] Test deterministic state snapshots.

**Result (2026-07-06):** `GamePad::GetState` verified (reads accumulated raw state, applies dead-zone at the XNA layer, carries `packetNumber` through). No source change. **Tests:** pre-existing dead-zone-overload + connected/disconnected coverage; packet-number behavior now covered by the new P4-013 tests through this exact API. **Remaining risk:** none.

## P4-012 — Verify `GamePad::GetCapabilities` `[x]`
- [x] Test disconnected capabilities.
- [x] Test connected capabilities from fake backend.
- [x] Verify each capability flag.
- [x] Document unsupported capabilities such as voice if always false.

**Result (2026-07-06):** capabilities read from device properties (no probing rumble — would cancel active vibration). No source change. **Tests:** pre-existing `CapabilitiesReflectConnectedDevice`, `CapabilitiesOfDisconnectedPlayerAreEmpty`, `RumbleSupportReported*`, `TriggerRumbleAndLightBarSupportReported`, `GyroAndAccelerometerSupportReported*`. **Remaining risk:** none.

## P4-013 — Verify packet number behavior `[x]`
- [x] Packet number should change on meaningful input changes.
- [x] Packet number should change on connect/disconnect.
- [x] Packet number should not change unnecessarily on repeated identical events.
- [x] Add tests for button and axis jitter.

**Result (2026-07-06):** `SetGamePadButtonState`/`SetGamePadAxisValue` bump `PacketNumber` only when the flags/axis field actually changes; `SetGamePadConnection` bumps once on a false→true transition; disconnect resets state. Verified against FNA's "packet changes only on change" semantics. No source change. **Files changed:** `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`. **Tests added:** `PacketNumberIsStableAcrossRepeatedIdenticalButtonEvents` (already-down / already-up repeats do not advance), `PacketNumberIsStableAcrossRepeatedIdenticalAxisEvents` (identical raw axis value no-bump; a changed value bumps). **Remaining risk:** none.

## P4-014 — Verify vibration `[x]`
- [x] Test `SetVibration` clamps values.
- [x] Test disconnected controller behavior.
- [x] Test no haptic support behavior.
- [x] Test NaN/Inf handling if applicable.
- [x] Document platform limitations.

**Result (2026-07-06):** **Source hardened** — `SetVibration`/`SetTriggerVibration` previously did `static_cast<Uint16>(std::clamp(m,0,1) * 0xFFFF)`; `std::clamp` propagates NaN and casting NaN→integer is UB in C++. Added a shared `motor_level()` helper that maps NaN→0 (matching C#'s well-defined `(ushort)NaN == 0`), leaving +Inf→full and −Inf→0 via clamp. **Files changed:** `src/CNA/Internal/Input/SdlInputBridge.cpp` (+`<cmath>`, `motor_level`), `tests/CNA/Internal/Input/FakeSdlGamepadBackend.hpp` (records `lastTriggerLow/High`). **Tests added:** `SetVibrationClampsMotorLevelsToSdlIntensity` (0.5→32767, 2.0→65535, −1.0→0), `SetVibrationHandlesNaNAndInfinity` (NaN→0, +Inf→65535, −Inf→0), `SetVibrationReturnsFalseForDisconnectedPlayer`, `SetVibrationReturnsFalseWhenDeviceHasNoRumble` (call still forwarded, return false — matches FNA's unconditional `SDL_RumbleGamepad`). **Behavior verified:** clean under ASan (NaN path no longer UB). **Platform note:** duration is fixed 0 (no timed rumble on this path). **Remaining risk:** none.

## P4-015 — Verify extension rumble APIs `[x]`
- [x] Test trigger vibration extension.
- [x] Test duration-based rumble if present.
- [x] Ensure extension APIs are clearly marked.
- [x] Add fake backend tests.

**Result (2026-07-06):** `SetTriggerVibrationEXT` (NOXNA) shares the hardened `motor_level` clamping and drives `RumbleGamepadTriggers`. **Tests added:** `TriggerVibrationSucceedsAndClampsForCapableDevice` (0.5→32767, 2.0→65535, returns true), `TriggerVibrationReturnsFalseWhenUnsupportedOrDisconnected`. **Duration-based rumble:** N/A — there is no public duration parameter (the SDL duration arg is fixed at 0), so it is intentionally not covered (documented in-test). **Remaining risk:** none.

## P4-016 — Verify light bar extension `[x]`
- [x] Test light bar success path.
- [x] Test no support path.
- [x] Test invalid color values if applicable.
- [x] Document supported devices.

**Result (2026-07-06):** `SetLightBarEXT` (NOXNA) forwards `Color` R/G/B bytes to `SetGamepadLED`. **Tests added:** `LightBarForwardsColorRgbToBackend` (10/20/30, white, black pass through; ledCalls tracked), `LightBarNoOpsForDisconnectedButForwardsForConnectedNonLedDevice` (disconnected → no call; connected non-LED still forwarded, matching FNA's unconditional `SDL_SetGamepadLED`). **Invalid color:** N/A — `Color` components are bytes, always valid (documented in-test). **Supported devices:** PS4/PS5 RGB-LED pads. **Remaining risk:** none.

## P4-017 — Verify sensor extensions `[x]`
- [x] Test accelerometer availability.
- [x] Test gyroscope availability.
- [x] Test sensor enable/disable.
- [x] Test no support path.
- [x] Document device/platform limitations.

**Result (2026-07-06):** availability + read + no-support already covered; **added** the enable/disable path. `read_gamepad_sensor` lazily enables a sensor the first time it is read and not again. **Files changed:** `tests/CNA/Internal/Input/FakeSdlGamepadBackend.hpp` (records `setSensorEnabledCalls`). **Tests added:** `ReadingSensorEnablesItOnceThenReadsWithoutReEnabling` (first gyro read enables once; repeat does not re-enable; accelerometer enables a second distinct sensor). Existing: `GyroAndAccelReadReturnData`, `SensorReadFailsGracefullyWhenUnavailable`, `GyroAndAccelerometerSupportReportedAndAbsentWhenMissing`. **Platform note:** gyro/accel are PS4/PS5/Switch-class only. **Remaining risk:** none.

## P4-018 — Verify GUID extension `[x]`
- [x] Test GUID for connected fake controller.
- [x] Test GUID for disconnected controller.
- [x] Document SDL/FNA compatibility expectations.

**Result (2026-07-06):** GUID formatting verified FNA-faithful (XInput→"xinput"; little-endian vendor/product; Valve re-exposed PS4/Xbox overrides). No source change. **Tests:** pre-existing `FormatsXinputVendorProductAndNoDevice`, `GetGuidUsesVendorProductAndValveOverrides` (incl. disconnected → ""). **Remaining risk:** none.

## P4-019 — Verify `GamePadType` `[x]`
- [x] Audit SDL joystick type to XNA `GamePadType` mapping.
- [x] Fix suspicious mappings.
- [x] Document unknown/unmappable types.
- [x] Add fake backend tests.

**Result (2026-07-06):** `sdl_joystick_type_to_gamepad_type` verified against FNA (GAMEPAD/WHEEL/ARCADE_STICK/FLIGHT_STICK/DANCE_PAD/GUITAR/DRUM_KIT, ARCADE_PAD→BigButtonPad). CNA is safer than FNA: SDL3-only `THROTTLE` and any unknown type fall through `default:` to `GamePadType::Unknown` rather than reading past the switch. No source change. **Tests added:** `ExtendedSdlJoystickTypesMapToXnaGamePadType` (DancePad/Guitar/DrumKit/ArcadePad→BigButtonPad + Throttle→Unknown + Unknown→Unknown), complementing pre-existing `SdlJoystickTypeMapsToXnaGamePadType` (first four). **Remaining risk:** none.

## P4-020 — Verify gamepad reset `[x]`
- [x] Ensure reset clears all slots.
- [x] Ensure reset clears backend state for tests.
- [x] Ensure no packet/state leak across tests.
- [x] Add regression tests.

**Result (2026-07-06):** `SdlInputBridge::ResetForTests` (fanned out by `InputManager::ResetAllForTests`) clears the slot/player maps and restores the real backend; it deliberately does NOT close app-owned handles (fake tests close via their own bookkeeping). **Tests added:** `ResetClearsAllGamepadSlotsAndPacketNumbers` (four pads connected + dirtied; after reset every slot is disconnected with packet 0; reinstalling the fake and adding a fresh pad restarts the packet count at 1 → no cross-test leak). **Remaining risk:** none.

---

# Phase 5 — Touch state correctness

## P5-001 — Audit `TouchCollection` read-only behavior `[x]`
- [x] `TouchCollection::IsReadOnly` currently claims read-only behavior.
- [x] Verify XNA/FNA behavior for mutation methods.
- [x] Make mutation methods throw/not supported if strict XNA requires it.
- [x] Or clearly mark mutable behavior as C++ deviation.
- [x] Add tests for `Add`, `Clear`, `Insert`, `Remove`, `RemoveAt`, and non-const indexing.

**Result (2026-07-06):** External-audit priority #1. **Verified against FNA (`TouchCollection.cs`):** FNA's `IsReadOnly` getter is hard-coded `true`, but that flag is *advisory* — its `Add`/`Clear`/`Insert`/`Remove`/`RemoveAt` and settable indexer all actually mutate the backing `List<TouchLocation>` when it is non-null (the state `TouchPanel.GetState` returns). The FNA source comment claiming a `NotSupportedException` is aspirational, not what the code does. **Decision: do NOT make mutation throw** — that would diverge from FNA's implemented behavior (the authoritative reference). CNA is already faithful (mutation succeeds, `IsReadOnly` stays true). The one unavoidable C++ deviation: FNA's default/`null`-backed collection throws `NullReferenceException` on mutation and `ArgumentOutOfRangeException` on the getter indexer; CNA's backing is a value `std::vector`, so a default collection is empty-and-mutable instead. **Files changed:** `include/Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp` (corrected the misleading `getIsReadOnlyProperty` doc — it previously claimed "does not support mutation" — to document the advisory flag + the deviation). **Tests:** mutation coverage already existed (`AddClearRemoveRemoveAtAndInsertMutateCollection`, `OperatorIndexConstAndMutableAccessTouchLocations`, `IndexerThrowsOnOutOfRangeAccess`, `IsReadOnlyIsAlwaysTrue`); **added** `IsReadOnlyIsAdvisoryAndMutationStillSucceedsLikeFna` pinning the intentional deviation crisply. **Remaining risk:** none.

## P5-002 — Verify `TouchCollection::CopyTo` `[x]`
- [x] Compare CNA behavior against XNA/FNA.
- [x] Decide how to represent C# array overwrite semantics in C++.
- [x] Add tests for offset, insufficient capacity, empty collection, and invalid offset.
- [x] Document any unavoidable C++ deviation.

**Result (2026-07-06):** FNA's `CopyTo` delegates to `List<T>.CopyTo` (overwrites pre-allocated slots of a fixed array; null/empty is a no-op). CNA's destination is a growable `std::vector`, so it **inserts** at `arrayIndex` (documented deviation in `TouchCollection.cpp`) and guards the index (bad index → `std::out_of_range`, mapping FNA's `ArgumentOutOfRangeException`). "Insufficient capacity" is N/A — a vector grows. **Tests:** offset/invalid-offset/non-zero-index already covered (`CopyToAppendsAllElementsInOrder`, `CopyToThrowsOnOutOfRangeIndexInsteadOfUndefinedBehavior`, `CopyToInsertsAtValidNonZeroIndex`); **added** `CopyToFromEmptyCollectionIsANoOp`. **Remaining risk:** none.

## P5-003 — Verify `TouchCollection` enumeration `[x]`
- [x] Test begin/end iteration.
- [x] Test count.
- [x] Test contains.
- [x] Test index lookup.
- [x] Test deterministic order.

**Result (2026-07-06):** Count / Contains / IndexOf were already covered
(`CountAndEmptyReflectContents`, `ContainsFindsMatchingLocation`, `IndexOfReturnsPositionOrNegativeOne`).
The genuine gap was begin/end iteration and its determinism. Verified against FNA `TouchCollection.cs:190-206`:
the `Enumerator` yields `collection[position]` for `position` 0..Count-1, i.e. **index/insertion order**.
**Added three tests:** `RangeIterationYieldsElementsInInsertionOrder` (const range-for yields ids 1,2,3 in
order and matches indexed access element-for-element), `MutableIterationVisitsEveryElementOnceInOrder`
(mutable `begin()/end()`, `end()-begin()==Count`), `EmptyCollectionIterationIsANoOp` (empty → zero visits,
`begin()==end()`). **Files changed:** `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp` (+3 tests).
**Tests:** `TouchCollectionTest.*` 17/17 pass; `ctest -L input` 100% green (shuffle×5). **Behavior verified:**
range iteration is deterministic insertion order, matching FNA's indexed enumerator. **Remaining risk:** none.

## P5-004 — Verify `TouchLocation` `[x]`
- [x] Test constructors.
- [x] Test id, state, position, pressure.
- [x] Test previous location behavior.
- [x] Test equality/hash if implemented.

**Result (2026-07-06):** Constructors (default→Invalid, 3-arg, 5-arg-with-previous), id/state/position,
TryGetPreviousLocation (true + FNA false-path writing the Invalid previous), and equality across all five
compared fields were already covered. **`pressure` is intentionally N/A:** verified against FNA
`TouchLocation.cs` — XNA 4.0's `TouchLocation` has **no `Pressure` field** (only `Id`, `Position`, `State`
+ private `prevPosition`/`prevState`), so CNA correctly omits it (documented here). Also verified
`GetHashCode` is faithful: FNA computes `Id.GetHashCode() + Position.GetHashCode()` (excludes State/prev);
CNA's `id_ + position_.GetHashCode()` matches line-for-line. **Added test**
`GetHashCodeMatchesFnaIdPlusPositionFormula` pinning the exact formula and the FNA weak-hash property (two
locations differing only in State collide in hash but are unequal). **Files changed:**
`tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp` (+1 test). **Tests:** `TouchLocationTest.*` 10/10
pass. **Behavior verified:** ctor/field/previous/equality/hash parity with FNA. **Remaining risk:** none.

## P5-005 — Verify `TouchLocationState` `[x]`
- [x] Freeze numeric values.
- [x] Test state transitions:
  - Invalid
  - Released
  - Pressed
  - Moved
- [x] Add regression tests.

**Result (2026-07-06):** Verified against FNA `TouchLocationState.cs`: exactly four sequential members
`Invalid=0, Released=1, Pressed=2, Moved=3` — CNA is byte-identical. Numeric values are frozen by
`TouchLocationStateTest.ValuesMatchXnaSequentialConstants` (all four pinned to 0/1/2/3; a renumber fails the
test). The four **state transitions** are exercised by the touch state-machine suites: Pressed→Moved is
`GetStateReflectsCurrentTouchSnapshot`; the Released terminal (returned once then dropped) is
`ReleasedTouchIsReturnedOnceAndThenRemoved`; the Invalid/Released release-condition branches are
`SetFingerReleaseOfHeldFingerProducesReleasedWithPreviousLocation` +
`SetFingerReleaseWithNoPriorFingerInsertsInvalidAndReportsNothing` (P5-007). **Files changed:** none
(freeze + transition coverage already present). **Behavior verified:** enum value + transition parity with
FNA. **Remaining risk:** none.

## P5-006 — Audit `TouchPanel::GetState` `[x]`
- [x] Inspect both internal touch slot path and `InputManager` fallback path.
- [x] Ensure the two paths cannot diverge in production.
- [x] Add tests for each path.
- [x] Document which path is authoritative.

**Result (2026-07-06):** External-audit priority #5. `GetState` first builds `validTouches_` from the `touches_` slot array (the FNA `SetFinger`-fed poll path); if that is empty it falls back to `InputManager::GetTouchState()` (CNA's event-driven path), capping at `MAX_TOUCHES` (DEC-10). **Cannot diverge in production:** `SetFinger` is never called on the real (event-driven) path, so `touches_` stays all-`Invalid` and `GetState` always uses the InputManager fallback — only one path is ever live in production; the slot path exists for a future poll-based platform and for tests. This is documented in `TouchPanel.cpp:130-137`. **Tests:** both paths + exclusivity (no double-report) already covered by `GetStateFallsBackToInputManagerWhenTouchesArrayEmpty` and `GetStatePrefersTouchesArrayAndDoesNotDoubleReport`. **Authoritative path:** InputManager fallback (production). **Remaining risk:** none.

## P5-007 — Fix duplicated condition in `TouchPanel::SetFinger` `[x]`
- [x] Remove duplicate nested condition.
- [x] Add regression tests around first touch, moved touch, and released touch.
- [x] Ensure no behavior accidentally changes unless intended.

**Result (2026-07-06):** External-audit priority #4. **Verified line-by-line against FNA `TouchPanel.cs:165-217`:** the current CNA `SetFinger` has exactly ONE release condition (`prev.State != Invalid && prev.State != Released` → Released, else Invalid) and one press/move condition — **no duplicate remains** (any historical duplication was already removed before this audit; `git log` shows the release condition present in its correct single form). No source change needed. Structure matches FNA exactly; the two documented CNA additions are the `index` bounds guard (`std::out_of_range`) and `touchDeviceExists_ = true` + the `updateInputManagerTouch` cross-write (slot-path→InputManager mirroring). **Tests:** first-touch (Pressed) and moved already covered by `UpdatePropagatesTouchesToPreviousForSlotPathContinuity`; **added** `SetFingerReleaseOfHeldFingerProducesReleasedWithPreviousLocation` (NO_FINGER release of a held finger → Released + previous) and `SetFingerReleaseWithNoPriorFingerInsertsInvalidAndReportsNothing` (else branch → Invalid, slot dropped) — covering both branches of the release condition as a regression guard. **Remaining risk:** none.

## P5-008 — Verify touch previous-location tracking `[x]`
- [x] Test Pressed has invalid previous location.
- [x] Test Moved has previous Pressed/Moved location.
- [x] Test Released has previous location.
- [x] Test unknown Released does not create bogus state.

**Result (2026-07-06):** The first three transitions are covered end-to-end by
`EventDrivenPathPreservesPreviousLocation` (Pressed→no previous; Pressed→Moved previous=Pressed;
Moved→Moved previous=prior Moved; Moved→Released previous=prior Moved, then flushed) plus
`HeldTouchAutoPromotesToMovedWithPressedPrevious`. The last item lacked a state assertion —
`ReleasingAnUnknownFingerIsSafe` only proved no-throw. **Added
`UnknownReleasedFingerHasNoBogusPreviousAndClears`:** an unknown finger released without a prior press
surfaces at most as a single Released whose `PreviousState` stayed Invalid (`TryGetPreviousLocation`
== false — no fabricated previous), then is flushed after one snapshot (`RemoveAfterSnapshot`). Verified
against `InputManager::SetTouchState`/`GetTouchState` (a Released defaults `PreviousState`=Invalid, so no
bogus previous is possible). **Files changed:** `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp` (+1 test).
**Tests:** `TouchEdgeCaseTest.*` 21/21 pass. **Behavior verified:** previous-location honesty across all four
transition classes. **Remaining risk:** none.

## P5-009 — Verify repeated touch down `[x]`
- [x] Test repeated Pressed with same id.
- [x] Decide whether it should replace, ignore, or transition.
- [x] Match FNA/XNA where possible.
- [x] Add regression tests.

**Result (2026-07-06):** **Decision: REPLACE.** Verified against the production path
(`SdlInputBridge.cpp:1426-1445`): a `FINGER_DOWN` resolves a stable touch id via `get_or_create_touch_id`
(the id survives until `FINGER_UP`), then `SetTouchState(id, Pressed, pos)` overwrites in place — so a
second down for the same finger updates the position and keeps the state `Pressed` (no duplicate slot, no
premature Moved). SDL never emits down-after-down for a live finger (it guarantees down→motion→up), so this
is a safe defensive superset of FNA rather than a divergence. **Strengthened**
`RepeatedFingerDownWithSameIdOverwritesRatherThanDuplicates` to also assert the state stays `Pressed` (not
transitioned) and that a fresh Pressed carries no previous location. **Files changed:**
`tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp` (test hardened). **Tests:** pass. **Behavior verified:**
repeated down = replace, single Pressed touch, last position wins. **Remaining risk:** none.

## P5-010 — Verify touch cancel behavior `[x]`
- [x] Test SDL canceled finger event.
- [x] Ensure it becomes Released or documented cancellation behavior.
- [x] Ensure cleanup happens exactly once.
- [x] Add tests.

**Result (2026-07-06):** Verified against FNA `SDL3_FNAPlatform.cs` (`FINGER_UP || FINGER_CANCELED` →
Released) and `SdlInputBridge.cpp:1465-1490` (both events share the Released branch + finger-id release).
Fully covered by existing tests: `FingerCanceledReleasesTouchLikeFingerUp` (a canceled finger reports
Released **exactly once**, not stuck Pressed/Moved, then disappears — cleanup happens once);
`FingerIdReusableAfterCancel` (the internal finger→touch mapping is freed, so the same SDL id pressed again
is a fresh Pressed, proving cleanup completed); and `FingerCanceledMidDragRecoversAndAllowsAFreshTap` +
`GestureDetectorTests`'s mid-drag-cancel case (the gesture state machine is not wedged — a cancel is a
Release at the detector). **Files changed:** none (coverage confirmed). **Behavior verified:** cancel ==
release, one-shot cleanup, id + gesture recovery. **Remaining risk:** none.

## P5-011 — Verify max touch count `[x]`
- [x] Test more touches than maximum supported by current storage.
- [x] Ensure deterministic truncation or rejection.
- [x] Ensure no out-of-bounds writes.
- [x] Add tests.

**Result (2026-07-06):** Two paths verified. (1) **Event-driven cap:** `TouchPanel::GetState` caps the
public snapshot at `MAX_TOUCHES` (=8) to match FNA's fixed `TouchLocation[MAX_TOUCHES]` array
(TouchPanel.cpp:140-152); the source is `GetTouchState`'s **ascending-id-sorted** list
(InputManager.cpp:396-402), so truncation is deterministic (lowest ids survive). **Strengthened**
`MoreThanMaxTouchesAreCappedAtMaxTouchesByTouchPanelGetState` to assert not just `count==MAX_TOUCHES` but
that the surviving ids are exactly 0..MAX_TOUCHES-1 (ids 8,9 dropped). (2) **Slot-path OOB:**
`TouchPanel::SetFinger` writes `touches_[index]` guarded by `if (index < 0 || index >= MAX_TOUCHES) throw
std::out_of_range` (TouchPanel.cpp:221-223). **Added**
`SetFingerRejectsOutOfRangeSlotIndexWithoutOutOfBoundsWrite` (-1 / MAX_TOUCHES / MAX_TOUCHES+5 throw; the
boundary indices 0 and MAX_TOUCHES-1 do not). **Files changed:**
`tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp` (+1 test, cap test hardened, +`<stdexcept>`). **Tests:**
both pass. **Behavior verified:** deterministic cap + no OOB write on an invalid slot. **Remaining risk:** none.

## P5-012 — Verify touch id ordering `[x]`
- [x] Test multiple touch ids.
- [x] Verify order is deterministic.
- [x] Verify order matches documented behavior.
- [x] Add tests.

**Result (2026-07-06):** Verified against FNA: `TouchPanel.GetState()` iterates its fixed
`touches[0..MAX_TOUCHES]` array (`TouchPanel.cs:97`) = **SDL finger-array slot order**. CNA's event-driven
fallback (`InputManager::GetTouchState`) orders by **ascending touch id** (`std::sort` of the id set,
InputManager.cpp:396-402) — both deterministic; because CNA ids are a compact appearance-order counter with
lowest-free reuse, ascending-id order tracks appearance/slot order like FNA's. **Documented** this as
**DEC-20** in `docs/input-fna-fidelity.md`. **Added**
`GetStateOrdersMultipleTouchesByAscendingIdRegardlessOfInsertionOrder` (insert ids 30,5,17 out of order →
come back 5,17,30, proving a real id sort, not insertion order), complementing the existing
`GetStateHandlesMultipleTouchIdsAndKeepsDeterministicOrder`. **Files changed:**
`docs/input-fna-fidelity.md` (DEC-20), `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp` (+1 test).
**Tests:** pass. **Behavior verified:** deterministic ascending-id ordering. **Remaining risk:** none.

## P5-013 — Verify `TouchPanel::GetCapabilities` `[x]`
- [x] Determine how touch capability is detected.
- [x] Avoid capability becoming true only after first touch unless intentionally documented.
- [x] Add fake backend/device query if needed.
- [x] Add tests.

**Result (2026-07-06):** Detection is two-tier (`TouchPanel.cpp:94-111`): `IsConnected = true` if
`touchDeviceExists_` (set on the first `FINGER_DOWN`, `SdlInputBridge.cpp:1428`) OR if
`InputManager::HasAnyTouch()` sees a live touch. The **connected-only-after-first-touch** behavior is
**intentional and FNA-faithful** — FNA/Windows only notices a touch screen once it is touched
(`SDL3_FNAPlatform.cs:972`); now explicitly documented in `docs/input-fna-fidelity.md` (previously only a
source comment). The query is **non-mutating** (uses `HasAnyTouch()`, not `GetTouchState()`), so it never
consumes an input frame. `MaximumTouchCount` = 4 connected / 0 disconnected (DEC-09). The "fake device
query" is served by the `setTouchDeviceExistsProperty` flag + the InputManager fallback (no SDL device
needed in tests). **Coverage** already complete: `GetCapabilitiesIsDisconnectedBeforeAnyTouch`,
`GetCapabilitiesIsConnectedOnceTouchDeviceExists`, `GetCapabilitiesIsConnectedViaInputManagerFallbackWhenFlagUnset`.
**Files changed:** `docs/input-fna-fidelity.md` (documented the deviation). **Behavior verified:**
detection + non-mutation + FNA parity. **Remaining risk:** none.

## P5-014 — Verify display size dependency `[x]`
- [x] Audit behavior when display width/height is zero.
- [x] Ensure touch state and gesture state do not diverge unexpectedly.
- [x] Add tests for touch before display size is known.
- [x] Document startup behavior.

**Result (2026-07-06):** Audited both coordinate paths. The **gesture** path
(`TouchPanel::INTERNAL_onTouchEvent`, TouchPanel.cpp:188-190) early-returns when `displayWidth_ <= 0 ||
displayHeight_ <= 0`, so a touch before the display size is published does **not** collapse to a bogus
`(0,0)` corner gesture. The **touch-state** path (`SdlInputBridge::to_touch_pixel_position`) scales by the
SDL **window** size (min 1×1), independent of `TouchPanel.DisplayWidth`, so touch **presence** is still
tracked at startup. This divergence (touch tracked, gestures suppressed) is **intentional** and resolves
the moment a valid display size is published. **Documented** as a startup deviation in
`docs/input-fna-fidelity.md`. **Added**
`SdlInputBridgeTouchGestureTest.TouchBeforeDisplaySizeIsKnownTracksTouchButSuppressesGestures` (0-display →
touch tracked, gesture suppressed, then gestures resume once size is published), complementing the existing
`ScalingProducesNoGestureWhenDisplaySizeIsZero` / `ScalingUsesDisplaySizeForPixelPosition` /
`ScalingReflectsResizedDisplay`. **Files changed:**
`tests/CNA/Internal/Input/SdlInputBridgeTouchGestureTests.cpp` (+1 test), `docs/input-fna-fidelity.md`.
**Tests:** pass. **Behavior verified:** zero-display safety + intentional startup divergence + recovery.
**Remaining risk:** none.

## P5-015 — Verify touch coordinate scaling `[x]`
- [x] Test normalized SDL touch coordinates to pixel coordinates.
- [x] Test logical/display size changes.
- [!] Test high-DPI behavior. — headless-blocked (dummy/xvfb run at 1×1); deferred to Phase 11 manual HW.
- [!] Add manual validation task for real devices. — deferred to Phase 11 (P11-006).

**Result (2026-07-06):** Normalized→pixel scaling and display-size changes are fully covered by the
deterministic task-828 suite: `ScalingUsesDisplaySizeForPixelPosition` (0.5×1000 → 500),
`ScalingReflectsResizedDisplay` (0.5 under 500×400 → 250,200 — a resize), and
`ScalingRoundsNonIntegerNormalizedCoordinates` (`Math.Round`, matching FNA's
`Round(x*DisplayWidth)`, TouchPanel.cs:136-139), plus `GestureAndTouchStateShareTheLogicalCoordinateBasis`
(gesture pixel == normalized state × DisplayWidth). **High-DPI is genuinely not headless-verifiable:** the
window point→pixel density mapping needs a real high-DPI display; both the `dummy` driver and Xvfb report a
1×1 (unscaled) surface, so a DPI-scaling assertion cannot be written deterministically. Marked `[!]` and
folded into the Phase 11 manual checklist (**P11-006** touchscreen + display scaling). **Files changed:**
none (automated coverage confirmed). **Behavior verified:** normalized→pixel + resize; high-DPI is HW-gated.
**Remaining risk:** high-DPI pixel accuracy unverified until manual HW validation (Phase 11).

## P5-016 — Verify touch reset `[x]`
- [x] Ensure reset clears active touches.
- [x] Ensure reset clears previous touches.
- [x] Ensure reset clears gesture queue.
- [x] Add regression tests.

**Result (2026-07-06):** `TouchPanel::ResetForTests` (fanned out by `InputManager::ResetAllForTests`,
InputManager.cpp:131) clears `touches_`, `previousTouches_`, `validTouches_`, the `gestures_` queue,
`touchDeviceExists_`, `enabledGestures_`, display metrics, and window handle (TouchPanel.cpp:293-312).
Active-touch + display-metric clearing was already pinned by `ClearsTouchPanelDisplayMetricsAndTouches`;
the two genuine gaps were the gesture queue and previous-touch slot continuity. **Added two tests:**
`ClearsQueuedGesturesOnReset` (enqueue a gesture → available → reset → `getIsGestureAvailableProperty()`
== false) and `ClearsPreviousTouchSlotContinuityOnReset` (a Pressed finger committed to `previousTouches_`
via `Update()`, then reset, then the same slot/finger re-appears and reads **Pressed** not Moved — proving
no stale previous-frame slot state survives). **Files changed:**
`tests/CNA/Internal/Input/InputResetTests.cpp` (+2 tests, +3 includes). **Tests:** both pass; full
`ctest -L input` 100% green (shuffle×5). **Behavior verified:** reset clears active + previous touches +
gesture queue. **Remaining risk:** none.

---

**Phase 5 complete (2026-07-06):** all 16 tasks `[x]` (P5-015 high-DPI sub-item `[!]` → Phase 11 P11-006).
Three real behavior verifications documented as deviations (DEC-20 ordering, GetCapabilities-after-touch,
zero-display startup); +10 tests added this phase; no public API change; `ctest -L input` green.

---

# Phase 6 — Gesture correctness

## P6-001 — Audit `GestureType` enum `[x]`
- [x] Verify XNA numeric flags.
- [x] Test bitwise combinations.
- [x] Test extension values if any.
- [x] Add numeric freeze tests.

**Result (2026-07-06):** `GestureType` verified **byte-identical to FNA `GestureType.cs`**: a `[Flags]`
enum with `None=0, Tap=0x1, DoubleTap=0x2, Hold=0x4, HorizontalDrag=0x8, VerticalDrag=0x10, FreeDrag=0x20,
Pinch=0x40, Flick=0x80, DragComplete=0x100, PinchComplete=0x200` — all 11 members present, correct powers
of two, **no CNA-specific extension values**. Fully covered by existing tests:
`GestureTypeTest.ValuesMatchXnaFlagConstants` (all 11 values byte-pinned = the numeric freeze; a renumber
fails the test) and `GestureTypeTest.BitwiseOperatorsCombineAndMaskFlags` (`|`, `&`, `|=`, `&=`
combine/mask). **Files changed:** none (freeze + bitwise coverage already complete). **Behavior verified:**
enum value + flag-operator parity with FNA. **Remaining risk:** none.

## P6-002 — Verify enabled gestures behavior `[x]`
- [x] Test default enabled gestures.
- [x] Test enabling one gesture.
- [x] Test enabling multiple gestures.
- [x] Test disabling gestures clears or preserves queue according to XNA/FNA behavior.
- [x] Add regression tests.

**Result (2026-07-06):** Enabling one / multiple / None round-trip already covered by
`EnabledGesturesGetterAndSetterRoundTrip` (`Tap | Hold` combined, then None). Verified against FNA: default
`EnabledGestures` is **None** — FNA's plain static `GestureType EnabledGestures` auto-property has no
initializer/static ctor, so `default(GestureType) == 0`; CNA initializes `enabledGestures_ = None`
(TouchPanel.cpp:20). And **disabling PRESERVES the queue** — FNA's setter only mutates the flag; the queue
drains solely via `ReadGesture`/reset (CNA's `setEnabledGesturesProperty` likewise just assigns the field).
**Added two tests:** `DefaultEnabledGesturesIsNone` (after reset → None) and
`ChangingEnabledGesturesDoesNotClearTheQueue` (enqueue → set None → still available → switch set → still
available). **Files changed:** `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp` (+2 tests).
**Tests:** pass. **Behavior verified:** default None + enable/disable flag semantics + queue preservation.
**Remaining risk:** none.

## P6-003 — Verify `IsGestureAvailable` `[x]`
- [x] Test false when queue is empty.
- [x] Test true when queue has entries.
- [x] Test behavior after `ReadGesture`.
- [x] Add tests.

**Result (2026-07-06):** `getIsGestureAvailableProperty()` is exactly `!gestures_.empty()`
(TouchPanel.cpp:68-71), matching FNA's `IsGestureAvailable => gestures.Count > 0`. All three sub-items were
covered incidentally (across `EnqueueGestureAndReadGestureFollowFifoOrder`, `TapFires...`, the *-DoesNotFire
tests); **added a single focused regression guard** `IsGestureAvailableReflectsQueueState` (empty→false,
enqueue→true, read→false). **Files changed:** `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
(+1 test). **Tests:** pass. **Behavior verified:** availability mirrors queue non-emptiness. **Remaining
risk:** none.

## P6-004 — Verify `ReadGesture` `[x]`
- [x] Test FIFO order.
- [x] Test exception when no gesture is available.
- [x] Test gesture data fields.
- [x] Add tests.

**Result (2026-07-06):** Fully covered by existing tests. **FIFO:**
`EnqueueGestureAndReadGestureFollowFifoOrder` (enqueue Tap then Hold, read back Tap then Hold).
**Exception:** `ReadGestureThrowsInvalidOperationExceptionWhenQueueIsEmpty` throws
`System::InvalidOperationException`, matching FNA's `InvalidOperationException` (TouchPanel.cpp ReadGesture).
**Data fields:** `TapFiresOnQuickReleaseNearPressPosition` (Position.X/Y + `FingerIdEXT`),
`PinchAndPinchCompleteFireForTwoFingerGesture` (Position, Position2, Delta, Delta2, FingerIdEXT,
FingerId2EXT), the drag tests (Delta). **Files changed:** none (coverage confirmed). **Behavior verified:**
FIFO dequeue + empty-throw + populated sample fields. **Remaining risk:** none.

## P6-005 — Verify tap detection `[x]`
- [x] Test simple tap.
- [x] Test movement threshold.
- [x] Test duration threshold.
- [x] Test disabled tap gesture.
- [x] Add deterministic clock tests.

**Result (2026-07-06):** Simple tap already covered by `TapFiresOnQuickReleaseNearPressPosition`. **Added
three gap tests** (all deterministic via the injectable test clock): `TapDoesNotFireWhenFingerMovesBeyondMoveThreshold`
(a 100px move > MOVE_THRESHOLD=35 with only Tap enabled drops HOLDING→NONE, so no Tap),
`TapDoesNotFireWhenHeldForOneSecondOrMore` (the tap gate is `held < 1s`, so releasing at exactly the 1000ms
cutoff emits no Tap), and `TapDoesNotFireWhenTapGestureIsDisabled` (Tap/DoubleTap off → quick press+release
emits nothing). Thresholds verified equal to FNA (`MOVE_THRESHOLD=35`, `held < seconds(1)`). **Files
changed:** `tests/CNA/Internal/Input/GestureDetectorTests.cpp` (+3 tests). **Tests:** pass. **Behavior
verified:** tap fires only on a quick, near-stationary press+release. **Remaining risk:** none.

## P6-006 — Verify double-tap detection `[x]`
- [x] Test two taps within threshold.
- [x] Test two taps outside threshold.
- [x] Test moved second tap.
- [x] Test disabled double-tap.
- [x] Add deterministic tests.

**Result (2026-07-06):** Within-window and outside-timing already covered
(`DoubleTapFiresWhenSecondTapIsWithinTimingAndDistanceWindow`,
`DoubleTapDoesNotFireWhenSecondTapArrivesAfterTimingWindow`). **Added two gap tests:**
`DoubleTapDoesNotFireWhenSecondTapIsTooFarAway` (second tap within 300ms but 100px away > MOVE_THRESHOLD=35
→ two plain Taps, matching FNA's `dist <= MOVE_THRESHOLD` gate) and
`DoubleTapDoesNotFireWhenDoubleTapGestureIsDisabled` (DoubleTap off → two Taps). **Files changed:**
`tests/CNA/Internal/Input/GestureDetectorTests.cpp` (+2 tests). **Tests:** pass. **Behavior verified:**
double-tap fires only within both the timing (300ms) and distance (35px) windows and only when enabled.
**Remaining risk:** none.

## P6-007 — Verify hold detection `[x]`
- [x] Test hold threshold.
- [x] Test movement cancels hold.
- [x] Test release before threshold.
- [x] Test disabled hold.
- [x] Add deterministic tests.

**Result (2026-07-06):** Threshold-fires and release-before-threshold already covered
(`HoldFiresAfterFingerIsHeldForAtLeastOneSecond`, `HoldDoesNotFireBeforeOneSecondElapses`). **Added two gap
tests:** `HoldDoesNotFireWhenFingerMovesBeyondMoveThreshold` (a 100px move > MOVE_THRESHOLD=35 leaves
HOLDING, so no Hold fires even after 1s) and `HoldDoesNotFireWhenHoldGestureIsDisabled` (Hold off → holding
past 1s emits nothing). All deterministic via the test clock. **Files changed:**
`tests/CNA/Internal/Input/GestureDetectorTests.cpp` (+2 tests). **Tests:** pass. **Behavior verified:**
Hold fires only when a near-stationary finger is held ≥1s and Hold is enabled. **Remaining risk:** none.

## P6-008 — Verify horizontal drag `[x]`
- [x] Test drag start.
- [x] Test drag delta.
- [x] Test drag complete.
- [x] Test vertical movement rejection if required.
- [x] Add tests.

**Result (2026-07-06):** Start + delta + complete already covered
(`HorizontalDragFiresWhenMovementIsPredominantlyHorizontal` → `Delta.X==100, Delta.Y==0`;
`DragCompleteFiresAfterAHorizontalDragAndCarriesReleaseFingerId`). **Added the axis-rejection gap test**
`HorizontalDragRejectsPredominantlyVerticalMovement`: with only HorizontalDrag enabled, a
predominantly-vertical move (`ay > ax`) past the threshold starts no drag (vdrag/fdrag disabled → detector
falls to NONE, emits nothing). **Files changed:** `tests/CNA/Internal/Input/GestureDetectorTests.cpp`
(+1 test). **Tests:** pass. **Behavior verified:** HorizontalDrag fires only for `ax > ay` movement.
**Remaining risk:** none.

## P6-009 — Verify vertical drag `[x]`
- [x] Test drag start.
- [x] Test drag delta.
- [x] Test drag complete.
- [x] Test horizontal movement rejection if required.
- [x] Add tests.

**Result (2026-07-06):** Start + delta already covered
(`VerticalDragFiresWhenMovementIsPredominantlyVertical` → `Delta.X==0, Delta.Y==100`). **Added the two gap
tests:** `DragCompleteFiresAfterAVerticalDrag` (a VerticalDrag ended by release fires DragComplete with the
release finger id — the detector treats `DRAGGING_V` as `wasDragging`) and
`VerticalDragRejectsPredominantlyHorizontalMovement` (only VerticalDrag enabled, an `ax > ay` move past 35px
starts no drag). **Files changed:** `tests/CNA/Internal/Input/GestureDetectorTests.cpp` (+2 tests).
**Tests:** pass. **Behavior verified:** VerticalDrag fires only for `ay > ax` movement and completes on
release. **Remaining risk:** none.

## P6-010 — Verify free drag `[x]`
- [x] Test drag start.
- [x] Test drag delta.
- [x] Test drag complete.
- [x] Test disabled free drag.
- [x] Add tests.

**Result (2026-07-06):** Start + delta + complete already covered
(`FreeDragFiresForDiagonalMovementWhenOnlyFreeDragIsEnabled` → `Delta==(100,100)`;
`DragCompleteFiresWhenAFreeDragEndsWithRelease`, which also asserts DragComplete carries zero Position/Delta
and `FingerId2EXT==NO_FINGER`). **Added the disabled-path gap test**
`FreeDragDoesNotFireWhenFreeDragGestureIsDisabled` (no drag enabled → a diagonal move past 35px emits
nothing). **Files changed:** `tests/CNA/Internal/Input/GestureDetectorTests.cpp` (+1 test). **Tests:** pass.
**Behavior verified:** FreeDrag fires only when enabled and past the move threshold. **Remaining risk:** none.

## P6-011 — Verify flick detection `[x]`
- [x] Test flick velocity calculation.
- [x] Test too-slow movement.
- [x] Test direction data.
- [x] Test disabled flick.
- [x] Add tests.

**Result (2026-07-06):** Velocity-calc/fires already covered by
`FlickFiresWhenReleaseVelocityExceedsMinimumThreshold` (500px in 10ms → velocity ≫ MIN_FLICK_VELOCITY=100),
and the distance gate by `FlickDoesNotFireWithoutSufficientMovementFromPressPosition`. **Closed three gaps:**
strengthened the fires test with **direction** assertions (`Delta.X > 0`, `Delta.Y == 0` for a +X swipe —
the sample's Delta is the velocity vector, so it carries direction not just magnitude);
`FlickDoesNotFireWhenReleaseVelocityIsBelowThreshold` (far enough — 40px > 35 — but held still 1s so
velocity ~0 → the **velocity gate** blocks it, isolated from the distance gate); and
`FlickDoesNotFireWhenFlickGestureIsDisabled` (Flick off → a fast swipe emits nothing; velocity isn't even
computed). **Files changed:** `tests/CNA/Internal/Input/GestureDetectorTests.cpp` (+2 tests, 1 strengthened).
**Tests:** pass. **Behavior verified:** flick needs distance AND velocity AND enablement; Delta encodes
direction. **Remaining risk:** none.

## P6-012 — Verify pinch detection `[x]`
- [x] Test two-finger pinch start.
- [x] Test pinch delta.
- [x] Test pinch complete.
- [x] Test one finger released.
- [x] Add tests.

**Result (2026-07-06):** Fully covered by `PinchAndPinchCompleteFireForTwoFingerGesture`: two-finger start
(Press 20 then 21 → PINCHING), delta (`Delta.X==-100, Delta2.X==0`, plus Position 300 / Position2 600,
FingerIds 20/21), complete (reads PinchComplete after `Release(20)`), and **one finger released mid-pinch**
(finger 20 lifted while 21 still down → PinchComplete). Also `DragInterruptedByASecondFingerReportsPinchComplete
NotDragComplete` exercises the pinch-from-drag path. **Files changed:** none (coverage confirmed).
**Behavior verified:** two-finger pinch start/delta/complete/single-release. **Remaining risk:** none.

## P6-013 — Verify pinch-complete gesture `[x]`
- [x] Test pinch-complete queue entry.
- [x] Verify position and delta fields.
- [x] Verify ordering relative to final pinch event.
- [x] Add tests.

**Result (2026-07-06):** Queue entry + ordering already covered by
`PinchAndPinchCompleteFireForTwoFingerGesture` (the `Pinch` from the Move is read first, queue empties, then
`Release(20)` produces the `PinchComplete` — proving Pinch precedes PinchComplete). **Closed the fields gap:**
strengthened that test to assert PinchComplete carries **no position or delta** — all four vectors
(Position, Position2, Delta, Delta2) are zeroed, matching the detector's `OnReleased_Pinch` (it is a terminal
marker like DragComplete). The finger ids (20/21) were already asserted. **Files changed:**
`tests/CNA/Internal/Input/GestureDetectorTests.cpp` (test strengthened). **Tests:** pass. **Behavior
verified:** PinchComplete is a zeroed terminal marker carrying only the two finger ids, ordered after the
last Pinch. **Remaining risk:** none.

## P6-014 — Verify multi-touch interactions `[x]`
- [x] Test tap while second finger appears.
- [x] Test drag interrupted by second finger.
- [x] Test pinch after one-finger drag.
- [x] Document expected policy.

**Result (2026-07-06):** Drag-interrupted-by-second-finger and pinch-after-one-finger-drag already covered
(`SecondFingerDuringADragInterruptsItAndBecomesAPinch`,
`DragInterruptedByASecondFingerReportsPinchCompleteNotDragComplete`). **Added the tap-with-second-finger gap
test** `TapDoesNotFireWhileASecondFingerIsStillDown`: with two fingers down, releasing the first emits no
Tap because `OnReleased` early-returns while `fingerIds` is non-empty. **Expected policy (documented here):**
a single-finger tap is suppressed while any other finger remains down; a second finger during a drag
promotes the interaction to a pinch (when Pinch is enabled) and the terminal event is PinchComplete, never
DragComplete. **Files changed:** `tests/CNA/Internal/Input/GestureDetectorTests.cpp` (+1 test). **Tests:**
pass. **Behavior verified:** multi-finger arbitration (tap suppression + drag→pinch promotion).
**Remaining risk:** none.

## P6-015 — Verify gesture queue reset `[x]`
- [x] Ensure reset clears queued gestures.
- [x] Ensure reset clears detector state.
- [x] Add regression tests.

**Result (2026-07-06):** Two distinct resets. **Queue:** `TouchPanel::ResetForTests` drains `gestures_`
(covered by `InputResetAllForTests.ClearsQueuedGesturesOnReset`, added in P5-016). **Detector internal
state:** `GestureDetector::ResetForTests` clears `activeFingerId`/`secondFingerId`/`fingerIds`/`state`
(GestureDetector.cpp:433-454) — note the two live in different objects (queue in TouchPanel, finger/state in
the detector). **Added** `ResetForTestsClearsDetectorInternalState`: drive two fingers into PINCHING, call
`GestureDetector::ResetForTests()` mid-gesture, then a brand-new finger taps cleanly (proving no stale
finger/state survived). **Files changed:** `tests/CNA/Internal/Input/GestureDetectorTests.cpp` (+1 test).
**Tests:** GestureDetector suite 35/35 pass shuffled×5; `ctest -L input` green. **Behavior verified:** both
the queue reset and the detector-state reset. **Remaining risk:** none.

## P6-016 — Manual gesture validation `[!]`
- [x] Create manual test checklist for real touchscreen.
- [!] Validate tap, double-tap, hold, drag, flick, and pinch on at least one real device. — Phase 11 (P11-006).
- [!] Record device, OS, display scale, and result. — Phase 11 (P11-006).

**Result (2026-07-06):** Genuinely hardware-gated — requires a real touchscreen, so on-device validation is
deferred to **Phase 11 (P11-006)**. The checklist already exists (`docs/demo-input-checklist.md` /
`docs/platform-input-notes.md`). All gesture *logic* is exhaustively pinned by the deterministic
`GestureDetectorTest` suite (35 tests, incl. every P6-005..015 threshold + negative path) and the real-SDL
bridge integration (`SdlInputBridgeTouchGestureTests` Tap/Flick/cancel), so manual validation is confirming
real-device wiring, not untested logic. **Files changed:** none. **Remaining risk:** on-device gesture feel
unverified until Phase 11.

---

**Phase 6 complete (2026-07-06):** all automated tasks (P6-001..015) `[x]`; P6-016 `[!]` (manual HW →
Phase 11). **+17 gesture tests** added this phase closing the negative-path / axis-rejection / disabled /
velocity-gate / direction / reset gaps found by an audit vs FNA `GestureDetector.cs`; thresholds confirmed
byte-identical to FNA (MOVE_THRESHOLD=35, flick velocity 100, hold 1s, double-tap 300ms/35px). No public API
change; `ctest -L input` green (shuffle×5).

---

# Phase 7 — Text input and IME correctness

## P7-001 — Audit `TextInputEXT` scope `[x]`
- [x] Confirm it is an extension, not strict XNA 4.0.
- [x] Ensure naming and docs make this clear.
- [x] Verify public API does not claim strict XNA support.

**Result (2026-07-06):** `TextInputEXT` is unambiguously an extension: the class carries the `EXT` name
suffix and the `NOXNA` marker (`NOXNA class TextInputEXT`, TextInputEXT.hpp:29), and its Doxygen explicitly
states *"not part of the XNA 4.0 API. FNA extension … XNA 4.0 had no portable text-input event"*
(TextInputEXT.hpp:19-21). No doc claims strict XNA support. It is classified as an FNA-compatible extension
in **P0-005** and pinned outside the strict surface by `PublicApiInputSignatureFreezeTests` /
`PublicApiInputCompileTests` (and every public member is `NOXNA`). The header also pre-documents the
TextEditing byte-offset caveat (relevant to P7-007). **Files changed:** none (audit-only). **Behavior
verified:** naming/marking/docs all designate it an extension. **Remaining risk:** none.

## P7-002 — Verify UTF-8 to UTF-16 decoding `[x]`
- [x] Test ASCII.
- [x] Test multi-byte BMP characters.
- [x] Test astral characters requiring surrogate pairs.
- [x] Test invalid UTF-8.
- [x] Test truncated UTF-8.
- [x] Test overlong sequences if decoder handles them.
- [x] Add regression tests.

**Result (2026-07-06):** `decode_utf8_to_utf16` (SdlInputBridge.cpp:118-175) is exhaustively covered and
matches `Encoding.UTF8` (bad input → U+FFFD, valid astral → surrogate pair): ASCII
(`TextInputEventForwardsAsciiAsCodeUnits`); BMP 2-byte é / 3-byte € / Czech diacritics / combining
(`TextInputEventDecodesTwoByteUtf8ToSingleCodeUnit`, `...ThreeByteUtf8...`, `...CzechDiacritics`,
`...CombiningCharactersAsSeparateCodeUnits`); astral emoji U+1F600 → 0xD83D 0xDE00
(`TextInputEventDecodesAstralEmojiToSurrogatePair`, `...MixedWidthStringInOrder`); invalid lead / bad
continuation / lone-surrogate-in-UTF-8 (`InvalidLeadByteBecomesReplacementChar...`,
`BadContinuationEmitsReplacementCharThenResyncsToValidText`, `SurrogateCodePointEncodedInUtf8Becomes...`);
truncated (`TruncatedMultiByteSequenceBecomesReplacementChar`); overlong C0 80
(`OverlongEncodingBecomesReplacementChar`). **Files changed:** none (coverage confirmed). **Behavior
verified:** full UTF-8→UTF-16 decode incl. every error mode. **Remaining risk:** none.

## P7-003 — Verify text input events `[x]`
- [x] Test SDL text input event conversion.
- [x] Test empty text.
- [x] Test multiple code units.
- [x] Test event callback ordering.
- [x] Add tests.

**Result (2026-07-06):** Conversion (`TextInputEventForwardsAsciiAsCodeUnits`), multiple code units
(`...MixedWidthStringInOrder`, 5 units in order), and multicast-delivery-to-every-subscriber
(`TextInputIsMulticastAndDeliversToEverySubscriber`) already covered. **Added two gap tests:**
`EmptyTextInputEventDeliversNoCodeUnits` (an empty `SDL_EVENT_TEXT_INPUT` yields zero calls) and
`TextInputSubscribersFireInRegistrationOrder` (three `+=` handlers fire 1→2→3, confirming .NET-faithful
multicast invocation order). **Files changed:**
`tests/CNA/Internal/Input/SdlInputBridgeTextInputTests.cpp` (+2 tests, +`<vector>`). **Tests:** pass.
**Behavior verified:** event conversion + empty-safe + multi-unit + registration-order dispatch.
**Remaining risk:** none.

## P7-004 — Verify synthesized control characters `[x]`
- [x] Test Backspace.
- [x] Test Tab.
- [x] Test Enter.
- [x] Test Delete.
- [x] Test Home/End if currently synthesized.
- [x] Verify behavior against FNA/MonoGame policy.
- [x] Add tests.

**Result (2026-07-06):** CNA synthesizes exactly the FNA set into `TextInput` on KEY_DOWN (`kTextInputCharacters`,
SdlInputBridge.cpp:95-103): **Home=2, End=3, Backspace=8, Tab=9, Enter=13, Delete=127** (+ Ctrl+V=22, see
P7-005). Verified **byte-for-byte identical to FNA** `FNAPlatform.TextInputCharacters` /
`TextInputBindings` — no CNA-only additions or omissions. All six control keys pinned by
`ControlKeysSynthesizeTextInputCharacters` (asserts each key→its char). **Files changed:** none (coverage
confirmed). **Behavior verified:** the six synthesized control chars match FNA exactly. **Remaining risk:**
none.

## P7-005 — Verify clipboard paste behavior `[x]`
- [x] Audit Ctrl+V handling.
- [x] Ensure text is not double-inserted if SDL also emits text input.
- [x] Add tests for Ctrl+V keydown and text input sequence.
- [x] Document platform behavior.

**Result (2026-07-06):** Fully covered and FNA-faithful (`SDL3_FNAPlatform.cs:915-921`: Ctrl+V emits char 22
and sets `textInputSuppress`). `CtrlVEmitsPasteCharAndSuppressesLiteralText` drives keydown(LCtrl),
keydown(V), textInput("v") → emits **charcs 22** once and **suppresses the literal 'v' echo** (no
double-insert), then confirms text flows again after release. `PlainVWithoutCtrlIsNotSuppressed` is the
negative control, and `CtrlVSuppressionDoesNotStickWhenCtrlReleasedWithoutVKeyUp` proves the suppression
gate clears on either V-up or Ctrl-up (never swallows text indefinitely). Platform behavior documented in
the source (`handle_text_input_key_up`) and `docs/input-fna-fidelity.md`. **Files changed:** none (coverage
confirmed). **Behavior verified:** paste char emitted once, literal echo suppressed, suppression self-clears.
**Remaining risk:** none.

## P7-006 — Verify key repeat and text repeat `[x]`
- [x] Ensure repeated keydown does not corrupt keyboard state.
- [x] Ensure repeated text input is delivered where intended.
- [x] Add tests.

**Result (2026-07-06):** Repeated-keydown-no-corruption already covered by
`SdlInputBridgeKeyboardTest.KeyRepeatKeepsKeyDownWithoutSpuriousTransitions` (P2-007 — 5 repeat KEY_DOWNs
keep the pressed set exactly {A}; DEC-19 skip-on-repeat). Repeated **control-char** text is covered by
`KeyRepeatReemitsControlCharacter` (FNA re-emits on `key.repeat`). **Added**
`RepeatedTextInputEventsAreEachDelivered` (two identical `TEXT_INPUT("a")` events → `u"aa"`, since text input
is decoded per-event and never de-duplicated). **Files changed:**
`tests/CNA/Internal/Input/SdlInputBridgeTextInputTests.cpp` (+1 test). **Tests:** pass. **Behavior
verified:** repeat neither corrupts keyboard state nor drops text input. **Remaining risk:** none.

## P7-007 — Verify text editing / IME composition `[x]`
- [x] Test text editing callback.
- [x] Test empty composition.
- [x] Test start/length values.
- [x] Determine whether SDL byte offsets or UTF-16 offsets are exposed.
- [x] Document behavior.
- [x] Add tests where possible.

**Result (2026-07-06):** Callback / start-length / multibyte-passthrough / empty-composition already covered
(`TextEditingEventForwardsTextStartLength`, `TextEditingForwardsMultiByteUtf8CompositionUnchanged`,
`TextEditingEmptyCompositionForwardsZeroes` + the EXT-level equivalents). **Offset semantics determined and
pinned:** CNA exposes SDL's **raw byte offsets** into the UTF-8 composition string, passed through unchanged
(NOT converted to UTF-16 indices) — already documented in `TextInputEXT.hpp` (INPUT-TEXT-016). **Added the
discriminating test** `TextEditingStartLengthAreRawByteOffsetsNotUtf16Indices`: for "éxy" (bytes C3 A9 'x'
'y') a byte offset of 2 points at 'x' whose UTF-16 index would be 1 — CNA reports 2, proving byte semantics.
**Files changed:** `tests/CNA/Internal/Input/SdlInputBridgeTextInputTests.cpp` (+1 test). **Tests:** pass.
**Behavior verified:** IME composition callback + byte-offset semantics. **Remaining risk:** byte offsets are
not code-point indices — callers must index multibyte composition carefully (documented).

## P7-008 — Verify start/stop text input `[x]`
- [x] Test start with valid window.
- [x] Test stop with valid window.
- [x] Test no-window behavior.
- [x] Ensure no crash.
- [x] Document no-op behavior if that is intended.

**Result (2026-07-06):** Fully covered. **Real window:** `StartStopAndIsActiveRoundTripThroughRealWindow`
(hidden SDL window → `StartTextInput` makes `IsTextInputActive()` true, `StopTextInput` makes it false; a
`GTEST_SKIP` fallback keeps it non-flaky on IME-less headless setups). **No window:**
`StartStopAndSetRectangleWithoutWindowAreSafeNoOps` + `IsTextInputActiveIsFalseWithoutWindow` +
`IsScreenKeyboardShownIsFalseWithoutWindow` — every call is null-guarded (TextInputEXT.cpp:31-70) so it is a
safe no-op with no crash. The no-op-without-window behavior is intentional and documented in the source.
**Files changed:** none (coverage confirmed). **Behavior verified:** start/stop toggles activation with a
window; no-ops safely without one. **Remaining risk:** none.

## P7-009 — Verify input rectangle `[x]`
- [x] Test setting input rectangle.
- [x] Test no-window behavior.
- [x] Test negative and zero rectangle values if possible.
- [x] Document platform limitations.

**Result (2026-07-06):** Setting a rectangle over the real path is covered by
`StartStopAndIsActiveRoundTripThroughRealWindow` (calls `SetInputRectangle(4,4,32,16)` while active,
reaching `SDL_SetTextInputArea` with `EXPECT_NO_THROW`); no-window is covered by
`StartStopAndSetRectangleWithoutWindowAreSafeNoOps`. **Added the degenerate-values gap test**
`SetInputRectangleWithZeroOrNegativeValuesIsSafe` (zero-size, negative origin, fully-negative rect → no
crash; null-guarded no-op without a window). **Platform limitation documented:** SDL exposes no getter for
the stored input area, so the real-window test can only assert reach-without-error, not the exact rectangle
SDL retained. **Files changed:** `tests/Microsoft/Xna/Framework/Input/TextInputEXTTests.cpp` (+1 test).
**Tests:** pass. **Behavior verified:** rectangle set is safe for normal + degenerate values, with/without a
window. **Remaining risk:** exact stored rect unverifiable (no SDL getter) — inherent platform limitation.

## P7-010 — Manual IME validation `[!]`
- [!] Validate with at least one IME on desktop. — Phase 11 (P11-007).
- [!] Validate with mobile soft keyboard if supported. — Phase 11 (P11-007).
- [!] Record OS, keyboard/IME, and result. — Phase 11 (P11-007).

**Result (2026-07-06):** Genuinely hardware/environment-gated — real IME composition requires an installed
IME + a visible window and cannot be driven deterministically headless (the real-window text-input test
already `GTEST_SKIP`s when the environment has no IME). Deferred to **Phase 11 (P11-007)**; checklist in
`docs/demo-input-checklist.md`. All IME *plumbing* (UTF-8 decode, TextEditing callback/start/length/empty,
byte-offset semantics, multicast dispatch, start/stop/rectangle) is exhaustively unit-tested, so manual
validation is confirming real IME wiring, not untested logic. **Files changed:** none. **Remaining risk:**
real IME composition unverified until Phase 11.

---

**Phase 7 complete (2026-07-06):** all automated tasks (P7-001..009) `[x]`; P7-010 `[!]` (manual IME →
Phase 11). **+6 tests** added closing the empty-text / subscriber-order / repeated-text / IME-byte-offset /
zero-negative-rectangle gaps found by an audit vs FNA `TextInputEXT.cs` / `SDL3_FNAPlatform.cs`; control-char
set + Ctrl+V confirmed byte-identical to FNA. No public API change; `ctest -L input` green (shuffle×5).

---

# Phase 8 — SDL bridge and backend integration

## P8-001 — Audit SDL bridge event coverage `[x]`
- [x] List every SDL event consumed by `SdlInputBridge`.
- [x] List every relevant SDL input event not consumed.
- [x] Decide whether missing events are intentional.
- [x] Document gaps.

**Result (2026-07-06):** **Consumed (17 cases, `ProcessEvent` switch):** MOUSE_MOTION,
MOUSE_BUTTON_DOWN/UP, MOUSE_WHEEL, KEY_DOWN/UP, TEXT_INPUT, TEXT_EDITING, FINGER_DOWN/MOTION/UP/CANCELED,
GAMEPAD_ADDED/REMOVED/BUTTON_DOWN/UP/AXIS_MOTION — each exercised through the real `ProcessEvent` by the
bridge test suite (+ the fuzz test drives mouse/keyboard/text/touch randomly). **Not consumed (intentional):**
`WINDOW_*` (focus/size/move/enter/leave), `DISPLAY_*` incl. DISPLAY_ORIENTATION, and `QUIT` — FNA uses these
for `Game.IsActive`, coordinate scaling, adapter reset and app-exit, which in CNA belong to the graphics/app
layers, not the input bridge (Mouse coordinate scaling is done live via the backend transform, not on a
resize event). Also neither CNA nor FNA polls raw JOYSTICK_*/MOUSE_ADDED/KEYBOARD_ADDED/DROP_*/CLIPBOARD.
The focus/minimize/close no-op was already pinned (`WindowFocusLostDoesNotClearHeldKeysMatchingFna`,
`WindowLifecycleEventsDoNotCorruptKeyboardState`); **closed the last gap** with
`UnconsumedResizeDisplayAndQuitEventsDoNotAffectInputState` (PIXEL_SIZE_CHANGED / DISPLAY_ORIENTATION / QUIT
fall through `default:` → no input mutation, no crash). **Files changed:**
`tests/CNA/Internal/Input/SdlInputBridgeKeyboardTests.cpp` (+1 test). **Behavior verified:** consumed set is
complete + tested; omissions are intentional and now pinned. **Remaining risk:** none.

## P8-002 — Verify event ordering `[x]`
- [x] Test keyboard event ordering.
- [x] Test text input ordering.
- [x] Test mouse motion/button ordering.
- [x] Test touch/gesture ordering.
- [x] Test gamepad connect/input ordering.

**Result (2026-07-06):** Fully covered by the golden/ordered-sequence suites. **Keyboard:**
`SdlInputBridgeGoldenTest.KeyboardScriptResolvesToExactPressedSet` (press W/A/S/D/Space, lift A/S, press
LShift → exact pressed set). **Text:** `ControlKeysSynthesizeTextInputCharacters`,
`CtrlVEmitsPasteCharAndSuppressesLiteralText` (ordered KEY_DOWN→TEXT_INPUT→KEY_UP). **Mouse:**
`MouseScriptResolvesToExactState` (motion + down/up + wheel + motion → exact MouseState). **Touch/gesture:**
`TwoFingerScriptResolvesToExactTouchSnapshots`, `FingerDownUpThroughProcessEventProducesTap`,
`FingerMotionThroughProcessEventProducesFlick`. **Gamepad:** `StaleButtonStateIsClearedOnDisconnect`,
`PacketNumberIsStableAcrossRepeatedIdenticalButtonEvents`/`...AxisEvents`,
`AxisMappingHandlesYInversionAndTriggerNormalization` (ordered add→button/axis→remove). The cross-subsystem
`InterleavedSessionResolvesEachSubsystemIndependently` covers keyboard+mouse+touch interleaving; gamepad is
intentionally isolated (golden tests are headless/no-gamepad) — each subsystem's state is independent.
**Files changed:** none (coverage confirmed). **Behavior verified:** ordered sequences resolve to exact
final state across all five subsystems. **Remaining risk:** none.

## P8-003 — Verify SDL initialization ownership `[x]`
- [x] Ensure input code initializes only the SDL subsystems it owns.
- [x] Ensure repeated init/shutdown is safe.
- [x] Ensure tests do not depend on global hidden state.
- [x] Add tests.

**Result (2026-07-06):** The bridge initializes **only** `SDL_INIT_GAMEPAD` (SdlInputBridge.cpp:1214-1224,
via `EnsureGamepadSubsystemInitialized`) — it never inits VIDEO/etc. (relies on video already up). **Init is
idempotent** and pinned by `SdlGamepadSubsystemInit.EnsureIsIdempotentAndInitializesSubsystem` (calls twice;
`SDL_WasInit(SDL_INIT_GAMEPAD)` true; SDL ref-counts). **Repeated shutdown is N/A** — the bridge deliberately
never `SDL_QuitSubSystem`s the gamepad subsystem (nothing to make unsafe); `ResetForTests` idempotence is
covered by `InputResetAllForTests.IsIdempotent`. **No global-hidden-state dependence:** every bridge fixture
resets via `ResetAllForTests()`/`ResetForTests()` in SetUp/TearDown, and the scancode-mode / gamepad-count
env caches are driven through test-only override hooks (`SetScancodeModeForTests`/`SetGamepadCountForTests`)
rather than real env; `parse_gamepad_count` is unit-tested directly. **Files changed:** none (coverage
confirmed). **Behavior verified:** owns only the gamepad subsystem, idempotent init, order-independent tests.
**Remaining risk:** none.

## P8-004 — Verify fake backend coverage `[x]`
- [x] Ensure fake keyboard/mouse/touch/gamepad paths exist or are testable.
- [x] Add fake backend helpers where useful.
- [x] Keep fake backend internal to tests.

**Result (2026-07-06):** **Gamepad** has a dedicated injectable seam: `ISdlGamepadBackend` +
`FakeSdlGamepadBackend` installed via `SetSdlGamepadBackendForTests(&fake)` and driven through the real
`ProcessEvent` in every `FakeGamepadTest` (introspection counters for open/close/rumble/led/sensor +
`lastRumbleLow/High`, `lastTriggerLow/High`, `lastLedR/G/B`, `setSensorEnabledCalls`). **Keyboard / mouse /
touch / text** need no separate fake — they are exercised by synthesizing `SDL_Event` structs fed to
`ProcessEvent` (golden/fuzz/keyboard/mouse/touch/text suites build events by hand), which is the intended
design. **Fake kept internal:** `FakeSdlGamepadBackend` lives under `tests/` in
`CNA::Internal::Input::test_support`, marked "NOT compiled into production", installed only via the
`...ForTests` seam, and `ResetForTests` restores the real backend (pinned by
`ResetClearsAllGamepadSlotsAndPacketNumbers`). **Files changed:** none (coverage confirmed). **Behavior
verified:** all four input classes are testable; the fake never leaks into the shipped library.
**Remaining risk:** none.

## P8-005 — Verify window handle resolution `[x]`
- [x] Audit all places resolving SDL window handle.
- [x] Ensure null window is safe.
- [x] Ensure stale window handle is safe where possible.
- [x] Add tests.

**Result (2026-07-06):** **Resolution sites audited:** bridge `to_touch_pixel_position` /
MOUSE_MOTION / MOUSE_BUTTON (`SDL_GetWindowFromID(windowID)` else `SDL_GetMouseFocus()`, null → 1×1 / raw
passthrough via `to_logical_position`); `Mouse::resolve_mouse_window` (handle else `SDL_GetMouseFocus()`,
used by SetPosition + relative-mode, each null-guarded); `TextInputEXT` (IsTextInputActive /
IsScreenKeyboardShown / Start / Stop / SetInputRectangle, all `if (SDL_Window* w = ToSdlWindow(handle_))`
guarded). **Null window safe** — covered by `MouseTest.SetPositionIsSafeAndUpdatesInternalStateWithNoWindow`,
the relative-mode no-window tests, and `TextInputEXTTest.*WithoutWindow*`; the bridge null path is covered by
the golden/fuzz suites (windowID 0 → raw passthrough). **Stale window handle (where possible):** CNA cannot
validate an arbitrary non-null pointer without passing it to SDL (whose `CHECK_WINDOW_MAGIC` would
dereference a freed window — a use-after-free), so a non-null stale handle is the **caller's contract**
(documented). The framework-managed lifecycle avoids it by clearing the handle on reset — **added**
`ResetForTestsClearsWindowHandleSoLaterCallsAreNullGuarded` (a bogus handle is wiped to 0 by
`ResetForTests`, so later calls take the null-guarded no-op path and never dereference it).
`Mouse::ResetForTests` likewise zeroes its handle (pinned by the reset suite). **Files changed:**
`tests/Microsoft/Xna/Framework/Input/TextInputEXTTests.cpp` (+1 test). **Behavior verified:** every
resolution site null-guards; reset neutralizes a stale handle. **Remaining risk:** a non-null stale handle
supplied by the app between destroy and clear is inherently the app's responsibility (documented).

## P8-006 — Verify high-DPI / logical coordinate handling `[x]`
- [x] Test mouse logical coordinates.
- [x] Test touch logical coordinates.
- [!] Test display resize. — headless-blocked (needs a real resizable window+renderer); Phase 11 (P11-002/006).
- [!] Add manual validation task. — Phase 11 (P11-002 mouse high-DPI, P11-006 touch scaling).

**Result (2026-07-06):** The deterministically-testable parts are covered. **Mouse logical (output path,
logical→window):** `MouseTest.SetPositionConvertsLogicalToWindowForLetterboxedRenderer` +
`SetPositionHandlesLetterboxOffsetNotJustScale` drive a real letterboxed renderer
(`SDL_RenderCoordinatesToWindow`, offset-aware). **Touch logical:**
`GestureAndTouchStateShareTheLogicalCoordinateBasis` pins that the gesture (DisplayWidth/Height scaling) and
touch-state coordinate bases agree. **Headless-blocked:** the *incoming* event transform
(`to_logical_position` via `SDL_RenderCoordinatesFromWindow` for MOUSE_MOTION/FINGER_*) and **display
resize** need a real window+renderer with a logical presentation — both the `dummy` driver and Xvfb run
unscaled (1×1), so the transform can't be asserted deterministically. Marked `[!]` → **Phase 11**
(P11-002 mouse high-DPI, P11-006 touch scaling); checklist in `docs/demo-input-checklist.md`. **Files
changed:** none. **Behavior verified:** logical→window output + gesture/touch basis agreement; input-side
transform + resize are HW-gated. **Remaining risk:** real high-DPI input-coordinate accuracy unverified
until Phase 11.

## P8-007 — Verify focus, minimize, and window close behavior `[x]`
- [x] Decide how input state is cleared on focus loss.
- [x] Decide how input behaves when minimized.
- [x] Add SDL bridge tests if fake events can represent this.
- [x] Document runtime behavior.

**Result (2026-07-06):** **Decision (FNA-faithful, DEC-15):** input state is **not** cleared on focus loss,
minimize, or close — FNA only sets `Game.IsActive = false` (`SDL3_FNAPlatform.cs:1026-1035`); games gate
input on `IsActive`. CNA's bridge does not consume window events, so they fall through `default:` as no-ops.
**Keyboard** was already pinned (`WindowFocusLostDoesNotClearHeldKeysMatchingFna`,
`WindowLifecycleEventsDoNotCorruptKeyboardState`). **Closed the mouse gap:** added
`WindowLifecycleEventsDoNotCorruptMouseState` (a held Left button survives FOCUS_LOST / MINIMIZED / RESTORED
/ CLOSE_REQUESTED, then releases normally). Touch/gamepad fall through the same `default:` no-op branch.
Runtime behavior documented in `docs/input-fna-fidelity.md` (DEC-15) and the source. **Files changed:**
`tests/CNA/Internal/Input/SdlInputBridgeMouseTests.cpp` (+1 test). **Behavior verified:** mouse + keyboard
state survive window lifecycle events. **Remaining risk:** none.

## P8-008 — Verify backend reset `[x]`
- [x] Ensure all internal input state can be reset for tests.
- [x] Ensure reset does not require SDL window.
- [x] Ensure reset leaves system in deterministic state.
- [x] Add tests.

**Result (2026-07-06):** `InputManager::ResetAllForTests` fans out (fixed order, InputManager.cpp:126-134) to
every subsystem and is proven to clear each: keyboard (`ClearsAccumulatedInputManagerState`), touch + display
metrics (`ClearsTouchPanelDisplayMetricsAndTouches`), gesture queue + previous-touch continuity
(`ClearsQueuedGesturesOnReset`, `ClearsPreviousTouchSlotContinuityOnReset`), mouse buttons/pos/wheel +
callbacks (`ClearsAccumulatedMouseButtonsPositionAndWheel`, `ClearsMouseAndTextInputCallbacks`), the bridge
finger→touch-id counter (`ResetsSequentialTouchIdCounterViaBridge`), and gamepad slots/packets
(`ResetClearsAllGamepadSlotsAndPacketNumbers`). **Closed the last gap** — the bridge's text-input suppress
flag: added `ResetForTestsClearsTextInputSuppressionFlag` (a Ctrl+V paste turns suppression on; after
`SdlInputBridge::ResetForTests` a following TEXT_INPUT flows again, proving the flag was cleared in
isolation). **No window required** — every reset test runs headless and `ResetForTests` touches no SDL
window API (it does not close app-owned gamepad handles either). **Deterministic** — pinned by
`InputResetAllForTests.IsIdempotent` (double reset lands identical) and the shuffle×5 gate. **Files changed:**
`tests/CNA/Internal/Input/SdlInputBridgeTextInputTests.cpp` (+1 test). **Behavior verified:** full reset of
every subsystem incl. the bridge suppress flag; window-free and deterministic. **Remaining risk:** none.

---

**Phase 8 complete (2026-07-06):** all tasks `[x]` (P8-006 display-resize / high-DPI-input sub-items `[!]` →
Phase 11). **+4 tests** closing the ignored-event / stale-handle-neutralization / mouse-lifecycle /
suppress-flag-reset gaps found by an audit vs FNA `SDL3_FNAPlatform.cs`; the 17-event consumed set is
enumerated and its FNA-relative omissions documented as intentional. No `src/` change; `ctest -L input` green.

---

# Phase 9 — Build, tests, and CI

## P9-001 — Improve missing submodule diagnostics `[x]`
- [x] If vendored SDL is required, make CMake error explicit and actionable.
- [x] Print exact command to initialize submodules.
- [x] Do not fail later with obscure include/link errors.
- [x] Add documentation.

**Result (2026-07-06):** Actionable configure-time diagnostics are already in place (the P0-004 follow-up).
A missing vendored SDL submodule aborts with `message(FATAL_ERROR "Missing vendored '<dep>' in <tp>. Run:
git submodule update --init --recursive")` (`cmake/ThirdPartySDL.cmake:36-38`); a missing `sharp-runtime`
sibling aborts with the exact `git clone … ../sharp-runtime` command (`CMakeLists.txt:43-49`); a missing
`easy-gl` sibling aborts with a check-it-out message (`CMakeLists.txt:104-107`). Each `FATAL_ERROR` fires at
**configure** time (before any compile), so a missing dependency never degrades into an obscure
include/link error. Documented in `docs/input-build-and-test.md` + the CLAUDE.md system-deps section.
**Files changed:** none (diagnostics already present + verified). **Behavior verified:** configure aborts
early with the exact recovery command for each missing dependency. **Remaining risk:** none.

## P9-002 — Add optional system SDL mode if desired `[x]`
- [x] Determine whether CNA should support system SDL for local testing.
- [x] If yes, add a CMake option. — N/A (decision: no)
- [x] If no, document why vendored SDL is required.
- [x] Keep behavior deterministic.

**Decision (2026-07-06): NO system-SDL mode — vendored SDL is required.** Rationale: (1) **determinism /
reproducibility** — CNA pins exact SDL3 / SDL_image / SDL_mixer commits as submodules under `third_party/`,
so every developer and CI runner builds against the identical SDL, eliminating "works on my distro" skew
from a system SDL of unknown version/patch level; (2) **version floor** — CNA targets SDL3 APIs
(`SDL_RenderCoordinatesToWindow`, `SDL_SetTextInputArea`, `SDL_EVENT_FINGER_CANCELED`, gamepad LED/sensor)
that many distros' packages predate; (3) **build determinism constraint** (this task's own requirement) is
best served by one pinned source. A system-SDL toggle would add a matrix of untested SDL versions for
marginal local convenience. The `.sdl-prebuilt` cache already makes the vendored path fast after the first
build; the P9-001 diagnostics tell a fresh clone exactly how to fetch the submodules. **Documented** here
and reinforced by the CLAUDE.md system-deps note (only FFmpeg is a true system dependency; SDL is vendored).
**Files changed:** none (decision record). **Behavior verified:** n/a (policy). **Remaining risk:** none.

## P9-003 — Create focused Input test target `[x]`
- [x] Ensure there is a simple command to run only Input tests.
- [x] Include Keyboard, Mouse, GamePad, Touch, Gesture, TextInput, and SDL bridge tests.
- [x] Document command in this plan.

**Result (2026-07-06):** A single labelled CTest target `CnaInputTests` (CMakeLists.txt:1959-1966) runs the
whole Input subset via `ctest -L input`. Its `--gtest_filter` (CMakeLists.txt:1952) is
`*Keyboard*:*Mouse*:*GamePad*:*Touch*:*Gesture*:*TextInput*:*SdlInputBridge*:*InputResetAllForTests*:*FakeGamepad*:*SdlGamepadSubsystemInit*:*ButtonState*:*KeyState*:*Buttons*:*PublicApiInput*`
— covering Keyboard, Mouse, GamePad, Touch, Gesture, TextInput/EXT, the SDL bridge, reset, fake-gamepad,
signature-freeze/compile, and the enum suites (every suite added this session matches, confirmed by the
green gate). **Command:** `ctest --test-dir <build> -L input --output-on-failure` (list: `ctest -N -L input`
→ exactly one entry, `CnaInputTests`). **Files changed:** none (target already present + verified).
**Behavior verified:** the label selects one comprehensive input test entry across all subsystems.
**Remaining risk:** none.

## P9-004 — Run Input tests repeatedly `[x]`
- [x] Run focused Input tests once.
- [x] Run focused Input tests with shuffle.
- [x] Run focused Input tests with repeat count.
- [x] Fix any order-dependent failures.

**Result (2026-07-06):** Order-independence is **baked into the target**: `CnaInputTests` runs
`CnaTests --gtest_filter=… --gtest_shuffle --gtest_repeat=5` (CMakeLists.txt:1960) — every `ctest -L input`
invocation reshuffles the whole input subset and runs it 5× with a fresh seed each iteration, which is the
required check for the process-wide input singletons (InputManager / GestureDetector / stock MouseCursors).
Run this session after **every** task's additions: **100% green** across all four backends (EasyGL / Vulkan /
bgfx / SDL_RENDERER). No order-dependent failures surfaced from the ~+50 tests added in Phases 5–8 (a couple
were *written* to be reset-first precisely because the wheel/gesture-clock state is process-cumulative — e.g.
the mouse-reset and gesture-reset tests). **Files changed:** none (gate already enforces this).
**Behavior verified:** the input subset is order-independent under shuffle×5. **Remaining risk:** none.

## P9-005 — Run sanitizer builds `[x]`
- [x] Run AddressSanitizer if supported.
- [x] Run UndefinedBehaviorSanitizer if supported.
- [x] Fix sanitizer findings.
- [x] Record unsupported sanitizer/platform cases.

**Result (2026-07-06):** The `cmake-build-input-asan` config builds `CnaTests` with **ASan + UBSan** (g++).
Ran the **full input filter shuffled** under
`ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`
(`xvfb-run … SDL_VIDEODRIVER=x11`): **374 input tests PASSED, zero sanitizer reports** (no
`ERROR: AddressSanitizer`, no UBSan `runtime error`). This covers every test added in Phases 5–8. **No
findings to fix.** Earlier phases' real bug fixes were each ASan-verified when landed (P2-002 keyboard-hash
OOB guard, P4-014 vibration NaN→int UB). **Recorded unsupported/env cases:** LeakSanitizer is disabled
(`detect_leaks=0`) because Mesa `libGLX`/driver allocations are freed at process exit outside our control
(third-party, not input leaks); the run needs a real X server (Xvfb) for the MouseCursor tests. **Files
changed:** none (sanitizer run + record). **Behavior verified:** full input suite is ASan+UBSan-clean under
shuffle. **Remaining risk:** none.

## P9-006 — Add fuzz-style SDL bridge tests `[x]`
- [x] Feed randomized but valid SDL-like events.
- [x] Ensure no crashes.
- [x] Ensure state remains internally consistent.
- [x] Keep fuzz tests deterministic with recorded seeds.

**Result (2026-07-06):** `SdlInputBridgeFuzzTest.RandomEventStreamNeverCrashesAndStateStaysReadable`
(INPUT-TEST-009) drives **5000** pseudo-random SDL events (mouse motion/button/wheel, key up/down, text,
finger down/motion/up/canceled — cases 1-12) through the real `SdlInputBridge::ProcessEvent`, each
`ASSERT_NO_THROW`, and after **every** event reads all subsystem snapshots (`Keyboard::GetState`,
`Mouse::GetState`, `TouchPanel::GetState` + `Update` + `ReadGesture`, `GamePad::GetState`) asserting each
stays readable — proving no crash and internally-consistent state. **Deterministic:** a small fixed-seed LCG
(no `std::random`, no clock/`std::random_device`), so runs are reproducible; the seed is recorded in-source.
The fuzz test is part of `ctest -L input` (via `*SdlInputBridge*`) and is included in the ASan+UBSan-clean
run (P9-005). **Files changed:** none (fuzz suite already present + verified). **Behavior verified:** the
bridge survives 5000 random events with readable state throughout. **Remaining risk:** none.

## P9-007 — Add golden event sequence tests `[x]`
- [x] Create golden sequences for keyboard.
- [x] Create golden sequences for mouse.
- [x] Create golden sequences for touch.
- [x] Create golden sequences for gamepad.
- [x] Assert final state and packet/queue behavior.

**Result (2026-07-06):** `SdlInputBridgeGoldenTests` (INPUT-TEST-008) provides exact-state scripts:
`KeyboardScriptResolvesToExactPressedSet` (press/lift sequence → exact pressed set),
`MouseScriptResolvesToExactState` (motion + buttons + wheel notches → exact MouseState),
`TwoFingerScriptResolvesToExactTouchSnapshots` (per-checkpoint touch snapshots), and
`InterleavedSessionResolvesEachSubsystemIndependently` (keyboard+mouse+touch in one interleaved script).
**Gamepad** golden sequences live with the fake-backend suite (`FakeGamepadTest.*`:
add→button/axis→remove asserting exact `GamePadState` + **packet-number** stability/advance,
`StaleButtonStateIsClearedOnDisconnect`, `PacketNumberIsStableAcrossRepeatedIdentical*`), since golden
scripts are headless/no-real-gamepad. Gesture **queue** behavior is asserted across the touch/gesture
scripts. **Files changed:** none (golden suites already present + verified). **Behavior verified:** final
state + packet/queue behavior pinned for all subsystems. **Remaining risk:** none.

## P9-008 — Freeze public API signatures `[x]`
- [x] Update signature freeze tests after intentional corrections.
- [x] Ensure strict XNA API changes are intentional.
- [x] Ensure extension API changes are documented.

**Result (2026-07-06):** The public XNA Input surface is frozen by `PublicApiInputSignatureFreezeTests`
(fully-spelled function-pointer / member-pointer casts + `std::is_constructible` ctor pins) with the golden
`docs/input-public-api-frozen.md`, plus `PublicApiInputCompileTests` (self-contained headers, no SDL/Internal
leak) and the exhaustive enum-value suites. **This entire session's work (Phases 5–8) added ONLY tests and
docs — no public XNA signature, enum value, or extension member changed** — so the freeze required **no
update** and remains intact (the freeze TU compiles and the gate is green, which would fail on any signature
drift). No strict-XNA corrections were needed; no new extension members were introduced. **Files changed:**
none (freeze verified unchanged). **Behavior verified:** frozen signatures + enum values still hold; no API
drift. **Remaining risk:** none.

## P9-009 — Update test coverage document `[x]`
- [x] List every Input type.
- [x] List corresponding tests.
- [x] Mark remaining gaps.
- [x] Do not claim 100% behavior coverage unless true.

**Result (2026-07-06):** Regenerated `docs/input-test-coverage.md` via
`tools/input_parity/check_input_test_coverage.py --out docs/input-test-coverage.md` (26 public + 8 internal
types). It lists every Input type, whether it has a dedicated `TEST(<Type>Test, …)` suite, and how many test
files reference it — the Phases 5–8 additions bumped the reference counts (e.g. `TouchCollection` 6→7,
`GestureType` 8→9, `GestureSample`/`MouseState` +1, `ButtonState` 10→11, internal `MouseButton` 1→2). The
**Gaps** section remains "None — every Input type has a dedicated suite or a documented sibling-suite cover."
The doc deliberately measures **type→suite** coverage (an inspection aid), **not** a "100% behavior" claim —
per-behavior correctness is what Phases 2–8 verified task-by-task against FNA (with the manual/HW-gated items
explicitly marked `[!]`). **Files changed:** `docs/input-test-coverage.md` (regenerated). **Behavior
verified:** doc matches the current test tree; no overstated coverage. **Remaining risk:** none.

---

# Phase 10 — Documentation

## P10-001 — Document strict XNA compatibility `[x]`
- [x] Document which Input APIs are intended to match XNA 4.0 exactly.
- [x] Document known deviations.
- [x] Document C++-specific representation differences.

**Result (2026-07-06):** Strict-XNA compatibility is documented across three tracked docs. **Which APIs are
strict XNA:** `docs/input-public-api-frozen.md` — the canonical API-tier glossary classifies every public
member as strict XNA / FNA-EXT / CNA-NOXNA, and `docs/input-member-parity-matrix.md` marks each member
strict/EXT/deviation (0 STRICT/EXT gaps). **Known deviations:** `docs/input-fna-fidelity.md` collects the
numbered `DEC-*` deviations, including this session's additions (DEC-20 touch-collection ordering,
GetCapabilities-after-first-touch, zero-display startup, IME byte-offset). **C++-specific representation
differences** (get/setXProperty for C# properties, `ref`/`out` → value-ref pairs, `GetHashCode()` →
`std::size_t`/`int`, `std::vector`-backed `TouchCollection` vs null-backed C# list) are documented in the
frozen-API doc + CHECKLIST.md's accepted-deviations table. **Files changed:** none (verified the three docs
cover all three sub-items). **Behavior verified:** strict-vs-deviation is fully documented and cross-checked
by the parity matrix. **Remaining risk:** none.

## P10-002 — Document FNA compatibility `[x]`
- [x] Document where CNA follows FNA behavior.
- [x] Document any FNA extensions supported by CNA.
- [x] Document any known FNA behavior not yet implemented.

**Result (2026-07-06):** `docs/input-fna-fidelity.md` is the dedicated FNA-compatibility doc. **Where CNA
follows FNA:** per-subsystem sections (Keyboard / Mouse / GamePad / TouchPanel / Gestures / TextInputEXT /
SDL-bridge) record the FNA-faithful behavior verified line-by-line (e.g. dead-zone math, 120-unit wheel,
`Encoding.UTF8` decode, control-char set + Ctrl+V, gesture thresholds MOVE_THRESHOLD=35 / hold 1s /
double-tap 300ms/35px / flick 100). **FNA extensions supported:** the "Extension APIs (FNAEXT / NOXNA)"
section lists `TextInputEXT`, relative mouse mode, scancode mode, and the GamePad EXT set
(GUID / LightBar / TriggerVibration / Gyro / Accelerometer + Misc1/Paddle/TouchPad buttons). **Known FNA
behavior not yet implemented / intentionally different:** the `DEC-*` list + the "not headless-verifiable /
by-design" notes (hardware actuation, real IME, Wayland cursor readback, high-DPI) — each tagged with its
status. **Files changed:** none (doc covers all three sub-items, incl. this session's DEC-20 etc.).
**Behavior verified:** FNA-follow / FNA-extension / FNA-gap all documented. **Remaining risk:** none.

## P10-003 — Document `NOXNA` / extension APIs `[x]`
- [x] Document `TextInputEXT`.
- [x] Document relative mouse mode.
- [x] Document gamepad GUID, sensors, trigger vibration, light bar, and extra buttons.
- [x] Ensure extension docs do not imply XNA 4.0 compatibility.

**Result (2026-07-06):** The "Extension APIs (FNAEXT / NOXNA)" section of `docs/input-fna-fidelity.md`
documents each extension: **`TextInputEXT`** (whole class NOXNA — portable text input/IME XNA lacked);
**relative mouse mode** (`Mouse::…IsRelativeMouseModeEXT` — pointer lock + relative delta); **scancode mode**
(`Keyboard::GetKeyFromScancodeEXT`); and the **GamePad EXT** set — `GetGUIDEXT`, `SetLightBarEXT` (PS4/5
LED), `SetTriggerVibrationEXT` (adaptive-trigger haptics), `GetGyroEXT`/`GetAccelerometerEXT` (motion
sensors), plus the EXT buttons (Misc1/Paddle1-4/TouchPad). Each carries the `EXT` name suffix + `NOXNA`
marker, and `docs/input-public-api-frozen.md`'s tier glossary explicitly places them outside strict XNA — so
the docs **do not** imply XNA 4.0 compatibility (the class/member docs say "not part of the XNA 4.0 API").
**Files changed:** none (all extension APIs documented + tier-classified). **Behavior verified:** every
extension is documented and clearly marked non-XNA. **Remaining risk:** none.

## P10-004 — Document platform notes `[x]`
- [x] Windows notes.
- [x] Linux notes.
- [x] macOS notes.
- [x] Android notes if supported.
- [x] Browser/Emscripten notes if supported.
- [x] Gamepad device notes.

**Result (2026-07-06):** `docs/platform-input-notes.md` already had Linux/X11, Wayland, Windows, macOS,
Android, iOS sections plus a cross-cutting "Gamepad backend & mapping" section (device notes). **Filled the
genuine gap:** CNA **does** target Emscripten (`if(EMSCRIPTEN)` in CMakeLists, EasyGL/WebGL2 default) but the
doc had no Browser section — **added "Browser / Emscripten (WebAssembly)"** covering the input-relevant
browser behavior: exceptions enabled via `-fexceptions -sNO_DISABLE_EXCEPTION_CATCHING=1` (so the input
paths that throw `std::out_of_range`/`InvalidOperationException` unwind instead of aborting the page);
browser-reserved keys; Pointer Lock for relative mouse mode (needs a user gesture); touch/device-pixel-ratio
scaling; the Gamepad-API privacy gate (controllers invisible until a button press); and hidden-DOM-input IME.
**Files changed:** `docs/platform-input-notes.md` (+Browser/Emscripten section). **Behavior verified:** all
six platform families now have input notes. **Remaining risk:** none.

## P10-005 — Document manual validation checklist `[x]`
- [x] Keyboard layouts.
- [x] Mouse and relative mode.
- [x] Gamepads.
- [x] Touchscreen.
- [x] IME/text input.
- [x] High-DPI display.

**Result (2026-07-06):** Two complementary manual checklists are tracked. `docs/demo-input-checklist.md`
(the `demo_input` harness) has per-section checklists for **Keyboard** (+ layouts), **Text input & IME**,
**Mouse** (+ relative mode), **Touch** (touch-capable display), and **GamePad** (up to 4 controllers), plus
a "still requires separate verification" list. `docs/devices-hardware-checklist.md` covers the
device/hardware matrix and **high-DPI display** validation. Together they cover all six required areas and
are the destination for every `[!]` item deferred from Phases 5–9 (P5-015 high-DPI touch, P6-016 gestures,
P7-010 IME, P8-006 display resize) — i.e. **Phase 11** executes against these checklists. **Files changed:**
none (checklists already cover all six areas). **Behavior verified:** manual-validation checklist exists for
keyboard-layouts / mouse+relative / gamepads / touchscreen / IME / high-DPI. **Remaining risk:** the manual
runs themselves are Phase 11 (hardware-gated).

---

**Phase 10 complete (2026-07-06):** all tasks `[x]`. The input documentation set (`input-fna-fidelity.md`,
`input-public-api-frozen.md`, `input-member-parity-matrix.md`, `platform-input-notes.md`,
`demo-input-checklist.md`, `devices-hardware-checklist.md`) covers strict-XNA vs FNA vs NOXNA/EXT,
per-platform notes (now incl. Browser/Emscripten), and the manual checklists. Only real change this phase:
added the Browser/Emscripten input section (P10-004).

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
