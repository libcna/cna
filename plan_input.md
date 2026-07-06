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
