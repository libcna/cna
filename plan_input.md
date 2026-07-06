# CNA Input — Exhaustive Per-Type Audit Plan (v2)

This plan **replaces** the previous phase-based `plan_input.md` completely (the prior plan and its
completion notes are preserved in git history at commit `8e878d97`). It was rewritten from scratch as a
**fine-grained, per-type audit** whose goal is to make CNA's `Microsoft.Xna.Framework.Input` implementation
**perfect**: every public class / struct / enum is reviewed **member by member** against the FNA reference,
every member's behavior/signature/value is verified, and a **dedicated test is confirmed to exist** (or
added) for every member — not just incidental coverage.

## Goal

For **every** input type, leave it in a state where:
1. Every public member (constructor, method, operator, property getter/setter, constant, enum value) is
   present and matches XNA 4.0 / FNA (name, signature, value, behavior).
2. Every member is verified **line-by-line against the FNA source** (its code, not its comments).
3. Every member has a **named dedicated unit test** (or a documented reason it is covered elsewhere).
4. Every public member carries a Doxygen block (CLAUDE.md requirement).
5. Intentional deviations are documented (`DEC-*` in `docs/input-fna-fidelity.md`).

## Status legend

- `[ ]` Not started
- `[~]` In progress
- `[x]` Completed
- `[!]` Blocked or requires manual/hardware validation
- `[?]` Needs upstream/FNA/XNA verification

## Execution rules

1. Execute tasks strictly in order.
2. **One task = one commit. NEVER batch multiple tasks into one commit.** (Explicit user requirement.)
3. For every completed task, record inline: members reviewed, FNA reference file, tests confirmed/added,
   behavior verified, deviations, build/test result, remaining risk.
4. FNA (`/rv/data/library/github.com/FNA-XNA/FNA/src/Input`) is the authoritative behavioral reference.
5. Strict XNA behavior stays separate from `EXT` / `NOXNA` extensions.
6. No public header may expose SDL types; the public XNA API signatures are frozen (update the
   signature-freeze test + `docs/input-public-api-frozen.md` in the SAME commit if a change is unavoidable).
7. New behavior gets deterministic unit tests. After any source change, the input gate
   (`ctest -L input`, shuffle×5) must stay green; a behavior fix also re-runs ASan.
8. Manual/hardware validation is recorded separately and marked `[!]` — never counted as automated coverage.

## Per-type audit checklist (applied to every type task below)

Each `A*-*` type task runs this uniform checklist:
- [ ] **Enumerate** every public member of the CNA type (ctor / method / operator / property getter+setter /
  constant / static factory / enum value) from the header.
- [ ] **Diff vs FNA** `<Type>.cs`: present / missing / extra; classify each strict-XNA vs EXT vs NOXNA.
- [ ] **Verify behavior** of each member line-by-line against the FNA source (clamping, casts, layouts,
  overloads, defaults, exceptions).
- [ ] **Doxygen**: every public member has a `/** @brief … */` block.
- [ ] **Test existence**: name the dedicated test covering each member/overload/operator/constant; **add** a
  test for any member lacking one. Out-ref overloads tested separately; equality/`==`/`!=`/`Equals`,
  `GetHashCode`, `ToString` each tested (equal + unequal where applicable).
- [ ] **Record** the result block (members, FNA file, tests confirmed/added, deviations, result, risk).

---

# Phase 0 — Baseline, method, inventory

## A0-001 — Record baseline and audit method `[x]`
- [x] Record branch, HEAD commit, compiler/CMake/OS, backends buildable, input gate green.
- [x] Record that the previous phase-based plan was replaced (git `8e878d97`) and not copied.
- [x] Record the complete type inventory this plan audits (below).
- [x] Record the tooling used to re-verify (parity matrix generator, coverage checker, signature freeze).

**Baseline (2026-07-06):**
- Branch `feature/input` · HEAD `ce828b09` · g++ (Debian 14.2.0-19) 14.2.0 · CMake 3.31.6 · Ninja 1.12.1 ·
  Linux 6.12.90+deb13-amd64.
- **Backends:** four build dirs present + current (`cmake-build-input-{easygl,vulkan,bgfx,sdlrenderer}`) plus
  `cmake-build-input-asan`; `ctest -L input` = **100% green** on EasyGL at baseline.
- **Previous plan:** the completed phase-based `plan_input.md` was replaced (removed; preserved in git at
  `8e878d97`); no content copied into this v2 — this is a fresh member-level re-audit.
- **Inventory:** 26 public types + 4 internal (listed above).
- **Re-verification tooling:** `tools/input_parity/gen_input_parity_matrix.py` (member parity vs FNA),
  `tools/input_parity/check_input_test_coverage.py` (type→suite coverage), `PublicApiInputSignatureFreezeTests`
  + `PublicApiInputCompileTests` (frozen signatures / no SDL leak), the exhaustive enum-value suites, and the
  input gate `ctest -L input` (shuffle×5) + the ASan/UBSan config.

**Files changed:** `plan_input.md`. **Tests:** none (baseline). **Result:** environment buildable + gate
green. **Remaining risk:** none.

**Type inventory (26 public + 4 internal):**
- *Enums (8):* `ButtonState`, `KeyState`, `Keys`, `Buttons`, `GamePadDeadZone`, `GamePadType`,
  `GestureType`, `TouchLocationState`.
- *Keyboard (2):* `KeyboardState`, `Keyboard`.
- *Mouse (3):* `MouseState`, `Mouse`, `MouseCursor`.
- *GamePad (7):* `GamePadButtons`, `GamePadDPad`, `GamePadThumbSticks`, `GamePadTriggers`,
  `GamePadCapabilities`, `GamePadState`, `GamePad`.
- *Touch (5):* `TouchLocation`, `TouchCollection`, `TouchPanelCapabilities`, `GestureSample`, `TouchPanel`.
- *Text (1):* `TextInputEXT`.
- *Internal (4):* `InputManager`, `GestureDetector`, `SdlGamepadBackend`/`ISdlGamepadBackend`,
  `SdlInputBridge`.

---

# Phase 1 — Enums (numeric-value + usage audit)

For each enum: verify every member name+value byte-identical to FNA; confirm an exhaustive value-freeze test
pins every member; verify `[Flags]` bit-combinations where applicable; confirm no extra/missing members.

## A1-001 — `ButtonState` `[x]`
- [x] FNA `Input/ButtonState.cs`; CNA `include/…/Input/ButtonState.hpp`; test `ButtonStateTests.cpp`.
- [x] Verify `Released=0, Pressed=1`; freeze test covers both; used correctly by Mouse/GamePad button props.

**Result (2026-07-06):** `ButtonState` is **byte-identical to FNA**: `enum class ButtonState { Released,
Pressed }` → `Released=0, Pressed=1` (FNA `ButtonState.cs:20,24`). Both members and the enum carry Doxygen
`@brief`. **Test:** `ButtonStateTest.ValuesMatchXnaNumericConstants` pins **both** values (0 and 1) — a
renumber fails loudly. Consumers (`MouseState` 5 button props, `GamePadButtons`/`GamePadState` button props)
use it and are tested in their own suites. **Members reviewed:** 2/2. **Files changed:** none (perfect,
no gap). **Behavior verified:** value parity + downstream usage. **Remaining risk:** none.

## A1-002 — `KeyState` `[x]`
- [x] FNA `Input/KeyState.cs`; CNA `KeyState.hpp`; test `KeyStateTests.cpp`.
- [x] Verify `Up=0, Down=1`; freeze test covers both.

**Result (2026-07-06):** `KeyState` is **byte-identical to FNA**: `enum class KeyState { Up, Down }` →
`Up=0, Down=1` (FNA `KeyState.cs:20,25`). Enum + both members carry Doxygen `@brief`. **Test:**
`KeyStateTest.ValuesMatchXnaNumericConstants` pins both values (0/1). `KeyState` is the return type of
`KeyboardState::operator[]`/`getItem`, exercised in `KeyboardInputTests`. **Members reviewed:** 2/2. **Files
changed:** none (perfect, no gap). **Behavior verified:** value parity. **Remaining risk:** none.

## A1-003 — `Keys` `[x]`
- [x] FNA `Input/Keys.cs`; CNA `Keys.hpp`; test `KeyboardInputTests.cpp` (value table).
- [x] Verify all 160 members name+value byte-identical (incl. hex OEM/IME/console); size anchor `static_assert`.

**Result (2026-07-06):** A normalized (name→value, hex-aware) diff of CNA `Keys.hpp` vs FNA `Keys.cs` reports
**160 members on each side, 0 only-in-CNA, 0 only-in-FNA, 0 value mismatches** — byte-identical, including
the hex OEM/IME/console members. **Test:** `KeyboardStateTest.KeysValuesMatchXNANumericConstants` pins **all
160** values via a `{Keys, int}` table, with `static_assert(sizeof(cases)/sizeof(cases[0]) == 160)` (count
anchor) and `EXPECT_EQ(seen.size(), 160u)` (all present + distinct) — a member add/remove/renumber fails the
build or the test. **Doxygen:** verified **0** of the 160 enum members lack an immediately-preceding `@brief`
(161 `@brief` = 160 members + the enum). Consumers (`KeyboardState`, the bridge keycode/scancode maps) are
tested in their suites. **Members reviewed:** 160/160. **Files changed:** none (perfect, no gap). **Behavior
verified:** full value + Doxygen parity. **Remaining risk:** none.

## A1-004 — `Buttons` `[x]`
- [x] FNA `Input/Buttons.cs`; CNA `Buttons.hpp`; test `ButtonsTests.cpp`.
- [x] Verify every XNA bit + EXT bits (Misc1/Paddle1-4/TouchPad) with no collision; bitwise operators.

**Result (2026-07-06):** CNA and FNA each have **31 members**. A value diff (normalizing CNA's `EXT` name
suffix) reports **0 FNA-only, 0 value mismatches, 0 colliding bit values** — the values are byte-identical;
CNA only appends the `EXT` suffix to the 6 FNA extension bits (`Misc1EXT`, `Paddle1-4EXT`, `TouchPadEXT`) per
its documented naming convention (these are FNA additions beyond stock XNA). **Test:** all 31 pinned —
`CoreXnaValuesMatchXnaBitConstants` (25 XNA bits, `DPadUp=0x1`…`LeftThumbstickRight=0x40000000`),
`FnaExtensionValuesMatchTheExtensionBits` (6 EXT bits), and `BitwiseOperatorsCombineMaskAndComplementFlags`
(`|`, `&`, `|=`, `&=`, `~`). **Doxygen:** 0 members missing `@brief`. **Members reviewed:** 31/31. **Files
changed:** none (perfect, no gap). **Behavior verified:** bit values + no-collision + flag operators.
**Remaining risk:** none.

## A1-005 — `GamePadDeadZone` `[x]`
- [x] FNA `Input/GamePadDeadZone.cs`; CNA `GamePadDeadZone.hpp`; test `GamePadDeadZoneTests.cpp`.
- [x] Verify `None=0, IndependentAxes=1, Circular=2`; freeze test; used by thumbstick dead-zone math.

**Result (2026-07-06):** **Byte-identical to FNA**: `None=0, IndependentAxes=1, Circular=2` (FNA
`GamePadDeadZone.cs:31-33`). Enum + all 3 members carry Doxygen `@brief`. **Test:**
`GamePadDeadZoneTest.ValuesMatchXnaSequentialConstants` pins all 3 values. Consumed by
`GamePadThumbSticks` dead-zone application (`ExcludeAxisDeadZone`, covered in `GamePadThumbSticksTests`) and
`GamePad::GetState` dead-zone overloads. **Members reviewed:** 3/3. **Files changed:** none (perfect, no
gap). **Behavior verified:** value parity + dead-zone usage. **Remaining risk:** none.

## A1-006 — `GamePadType` `[x]`
- [x] FNA `Input/GamePadType.cs`; CNA `GamePadType.hpp`; test `GamePadTypeTests.cpp`.
- [x] Verify every member value; SDL→XNA type mapping (incl. `Unknown` fallback) covered.

**Result (2026-07-06):** **10 members byte-identical to FNA** in the same order:
`Unknown=0, GamePad=1, Wheel=2, ArcadeStick=3, FlightStick=4, DancePad=5, Guitar=6, AlternateGuitar=7,
DrumKit=8, BigButtonPad=9` (FNA `GamePadType.cs:20-56`). Enum + all 10 members carry Doxygen `@brief`.
**Test:** `GamePadTypeTest.ValuesMatchXnaSequentialConstants` pins all 10. The SDL joystick-type→XNA mapping
(with the safe `Unknown` fallback for SDL3-only/unknown types) is covered by
`SdlJoystickTypeMapsToXnaGamePadType` + `ExtendedSdlJoystickTypesMapToXnaGamePadType`. **Members reviewed:**
10/10. **Files changed:** none (perfect, no gap). **Behavior verified:** value parity + SDL mapping.
**Remaining risk:** none.

## A1-007 — `GestureType` `[x]`
- [x] FNA `Input/Touch/GestureType.cs`; CNA `Touch/GestureType.hpp`; test `Touch/GestureTypeTests.cpp`.
- [x] Verify 11 `[Flags]` values (None..PinchComplete, 0..0x200); bitwise ops; no EXT.

**Result (2026-07-06):** Value diff vs FNA `GestureType.cs`: **11=11 members, 0 only-in-CNA, 0 only-in-FNA,
0 mismatches** — byte-identical `[Flags]` enum `None=0, Tap=0x1, DoubleTap=0x2, Hold=0x4, HorizontalDrag=0x8,
VerticalDrag=0x10, FreeDrag=0x20, Pinch=0x40, Flick=0x80, DragComplete=0x100, PinchComplete=0x200`; **no CNA
extension values**. Doxygen: 0 members missing. **Test:** `GestureTypeTest.ValuesMatchXnaFlagConstants` (all
11 pinned) + `BitwiseOperatorsCombineAndMaskFlags` (`|`,`&`,`|=`,`&=`). **Members reviewed:** 11/11. **Files
changed:** none (perfect, no gap; re-confirms A1-audit of the earlier P6-001). **Behavior verified:** flag
values + operators. **Remaining risk:** none.

## A1-008 — `TouchLocationState` `[x]`
- [x] FNA `Input/Touch/TouchLocationState.cs`; CNA `Touch/TouchLocationState.hpp`; test `Touch/TouchLocationStateTests.cpp`.
- [x] Verify `Invalid=0, Released=1, Pressed=2, Moved=3`; freeze test; transitions covered elsewhere.

**Result (2026-07-06):** **Byte-identical to FNA**: `Invalid=0, Released=1, Pressed=2, Moved=3` (FNA
`TouchLocationState.cs:15-18`). Enum + all 4 members carry Doxygen `@brief`. **Test:**
`TouchLocationStateTest.ValuesMatchXnaSequentialConstants` pins all 4. The four state transitions are
exercised by the touch state-machine suites (P5-005: `GetStateReflectsCurrentTouchSnapshot`,
`ReleasedTouchIsReturnedOnceAndThenRemoved`, the SetFinger release-branch tests). **Members reviewed:** 4/4.
**Files changed:** none (perfect, no gap). **Behavior verified:** value + transition parity. **Remaining
risk:** none.

---

**Phase 1 complete (2026-07-06):** all 8 input enums re-audited member-by-member vs FNA — **every enum is
byte-identical (0 name/value gaps), every member has Doxygen, and every value is pinned by a dedicated
freeze test** (Keys additionally with a `static_assert` count anchor + distinctness). No source change
needed; no gaps found.

---

# Phase 2 — Keyboard types

## A2-001 — `KeyboardState` (struct) `[x]`
- [x] FNA `Input/KeyboardState.cs`; CNA `KeyboardState.hpp`/`.cpp`; test `KeyboardInputTests.cpp`.
- [x] Members: ctors, `getItem`/`operator[]`, `IsKeyDown`/`IsKeyUp`, `GetPressedKeys`,
  `Equals`/`==`/`!=`, `GetHashCode`, `ToString` (NOXNA). Per-member test + FNA behavior.

**Result (2026-07-06):** **13 public members** reviewed against FNA `KeyboardState.cs`. Member map (all
Doxygen'd): default ctor (NOXNA) → `DefaultConstructorHasNoPressedKeys`; `initializer_list<Keys>` ctor →
`InitializerListConstructorFlagsGivenKeys`; `unordered_set<Keys>` ctor (NOXNA) →
`UnorderedSetConstructorFlagsGivenKeys` (the two C++ ctors map FNA's `params Keys[]`); `getItem` +
`operator[]` (both = FNA's `this[Keys]` indexer) + `IsKeyDown` → `IndexerMatchesGetItemAndIsKeyDown`;
`GetPressedKeys` → `GetPressedKeysReturnsEmptyForDefaultState` + `...IsSortedByAscendingNumericValue`
(FNA-faithful ascending order); `Equals`/`==`/`!=` → `EqualStatesCompareEqual` + `UnequalStatesCompareUnequal`;
`GetHashCode` → `...IsConsistentForEqualStates` + `...OfEmptyStateIsZero` + `...MatchesFNAWordXorFormula`
(exact FNA 8×32-bit XOR) + 3 OOB-guard tests; `ToString` (NOXNA — FNA has none, documented) →
`ToStringMatchesFNAValueTypeDefault`. **`IsKeyUp`** was only asserted incidentally inside the indexer test —
**added a dedicated** `IsKeyUpIsTheComplementOfIsKeyDown` (pressed→up=false, unpressed→up=true, exactly one
of Down/Up per key; matches FNA `IsKeyUp => !IsKeyDown`). **Members reviewed:** 13/13, each with a named
test. **Files changed:** `tests/…/KeyboardInputTests.cpp` (+1 test). **Behavior verified:** full
member + FNA-behavior parity. **Remaining risk:** none.

## A2-002 — `Keyboard` (static class) `[x]`
- [x] FNA `Input/Keyboard.cs`; CNA `Keyboard.hpp`/`.cpp`; tests `KeyboardInputTests.cpp` + bridge.
- [x] Members: `GetState()` (×overloads), `GetKeyFromScancodeEXT`. Per-member test + FNA behavior.

**Result (2026-07-06):** **3 static members**, all matching FNA `Keyboard.cs` and Doxygen'd:
`GetState()` (→ FNA `GetState()`), `GetState(PlayerIndex)` (→ FNA overload; FNA ignores the index since the
keyboard is single), and `GetKeyFromScancodeEXT(Keys)` (NOXNA/EXT — matches FNA's own extension).
**Test map:** `GetState()` covered by the accumulate/reflect suite (54 references);
**`GetState(PlayerIndex)` covered SEPARATELY** by `GetStateWithPlayerIndexMatchesGetState` (asserts it equals
`GetState()`, FNA-faithful); `GetKeyFromScancodeEXT` covered by
`GetKeyFromScancodeEXTIsIdentityInScancodeMode` + `...TranslatesInNormalMode` (+ 18 references). **Members
reviewed:** 3/3, each overload with a named test. **Files changed:** none (perfect, no gap). **Behavior
verified:** state read + player-index-ignored overload + scancode translation. **Remaining risk:** none.

---

**Phase 2 complete (2026-07-06):** `KeyboardState` (13 members) + `Keyboard` (3 members) fully re-audited
vs FNA; every member has a named dedicated test (added `IsKeyUpIsTheComplementOfIsKeyDown`). No gaps.

---

# Phase 3 — Mouse types

## A3-001 — `MouseState` (struct) `[x]`
- [x] FNA `Input/MouseState.cs`; CNA `MouseState.hpp`/`.cpp`; test `MouseInputTests.cpp`.
- [x] Members: ctor, X/Y, ScrollWheelValue, 5 button props, `Equals`/`==`/`!=`, `GetHashCode`. Per-member.

**Result (2026-07-06):** **15 public members**, matching FNA `MouseState.cs` exactly (FNA has **no**
horizontal-scroll member — CNA correctly omits it). The 8-arg ctor's parameter order
`(x, y, scrollWheel, leftButton, middleButton, rightButton, xButton1, xButton2)` is **byte-identical to
FNA** and pinned by `EightArgConstructorSetsEveryFieldInTheRightSlot` (alternating Pressed/Released catches
any parameter swap). **Test map** (all Doxygen'd): 8 properties + default ctor →
`DefaultConstructorAllValuesAtRest`; full ctor → the 8-arg test; `Equals`/`==`/`!=` →
`EqualsAndOperatorsReturnTrueForIdenticalStates` + `...FalseWhenPositionDiffers`/`...ScrollWheelDiffers`/
`...AButtonDiffers`; `GetHashCode` → `GetHashCodeMatchesFormula` + `...IsConsistentForEqualStates`;
`ToString` → `ToStringFormatsNoneWhenNoButtonsPressed` + `...MultiplePressedButtonsIn…Order`. **Documented
deviation:** FNA's `GetHashCode` returns `base.GetHashCode()` (default `ValueType` hash); CNA uses an explicit
deterministic field-based hash (P3-003) — a reasonable, tested choice. **Members reviewed:** 15/15.
**Files changed:** none (perfect, no gap). **Behavior verified:** ctor-slot order + fields + equality/hash/
ToString. **Remaining risk:** none.

## A3-002 — `Mouse` (static class) `[x]`
- [x] FNA `Input/Mouse.cs`; CNA `Mouse.hpp`/`.cpp`; tests `MouseInputTests.cpp` + bridge.
- [x] Members: `GetState`, `SetPosition`, `WindowHandle` get/set, relative-mode EXT, `ClickedEXT`. Per-member.

**Result (2026-07-06):** **8 members** reviewed. FNA-matching (strict/EXT): `WindowHandle` get/set (→ FNA
`WindowHandle`), `GetState()`, `SetPosition(x,y)`, `ClickedEXT` (NOXNA multicast → FNA `Action<int>
ClickedEXT`), `IsRelativeMouseModeEXT` get/set (→ FNA `IsRelativeMouseModeEXT`). **Confirmed NOXNA is
correct:** `Mouse::SetCursor` (and the whole `MouseCursor` type) are genuinely **not in FNA** — FNA has no
`MouseCursor.cs` and no `SetCursor`/`MouseCursor` reference in `Mouse.cs` (they are MonoGame additions), so
CNA's `NOXNA` tag is right. `INTERNAL_onClicked` + `ResetForTests` are NOXNA internal/test helpers. **Test
coverage:** `ClickedEXT` (27 refs, `ButtonDownFiresClickedEXTWithZeroBasedIndex`/`ButtonUpDoesNotFireClickedEXT`),
WindowHandle (13/21 refs, round-trip + no-window), `SetPosition` (10 refs, no-window + letterbox),
IsRelativeMouseModeEXT (20 refs, real-window round-trip + no-window no-op), `SetCursor`
(`SetCursorIsSafeNoOpForDisposedCursor`), `ResetForTests` (via `ClearsMouse…` reset tests). **Members
reviewed:** 8/8. **Files changed:** none (perfect, no gap). **Behavior verified:** FNA parity + correct NOXNA
tagging. **Remaining risk:** none.

## A3-003 — `MouseCursor` (class, NOXNA) `[x]`
- [x] FNA `Input/Mouse.cs` (MouseCursor is CNA-specific — verify against XNA MouseCursor semantics);
  CNA `MouseCursor.hpp`/`.cpp`; test in `MouseInputTests.cpp`.
- [x] Members: stock cursor statics, `FromTexture2D`, `Dispose`, move-ctor, handle. Per-member (Xvfb-gated).

**Result (2026-07-06):** Correctly **NOXNA** — confirmed FNA has no `MouseCursor.cs` (A3-002); this matches
the MonoGame `MouseCursor` API, not XNA 4.0. **No SDL leak:** the header exposes `SDL_Cursor*` only via an
**opaque forward declaration** `struct SDL_Cursor;` (no `#include <SDL3/SDL.h>`), and
`PublicApiInputCompileTests` includes it under an `#error` guard proving no SDL reaches a consumer TU.
**Members (all Doxygen'd):** default ctor, `SDL_Cursor*` ctor (owning/non-owning), `FromTexture2D`, deleted
copy ctor/assign, move ctor + move assign, dtor, `Dispose`, handle accessor, and **12 stock-cursor statics**
(Arrow/Crosshair/Hand/IBeam/No/SizeAll/SizeNESW/SizeNS/SizeNWSE/SizeWE/WaitArrow/Wait — all 12 referenced in
tests). **Test map:** `StockCursorsAreNonNullWhenVideoAvailable` (all 12) + `StockCursorGetterReturnsTheSame
InstanceOnRepeatedCalls`; `DefaultConstructorCreatesNonNullOwningCursor`; `NonOwningConstructorDoesNotDestroy…`;
`FromTexture2D{CreatesCursorFromColorTexture,AcceptsColorSrgbTexture,RejectsNonColorSurfaceFormat,ThrowsWhen
OriginIsOutsideTheTexture}` + `ColorCursorSurvivesSourcePixelBufferDestruction`; `MoveConstructorTransfers…`
+ `MoveAssignmentDisposesPreviousHandle…`; `DisposeReleasesHandleAndIsIdempotent` +
`DisposingAStockSingletonIsANoOpAndKeepsItUsable`. **Members reviewed:** all. **Files changed:** none
(perfect, no gap; stock-cursor tests are Xvfb-gated — skip under the SDL `dummy` driver, documented).
**Remaining risk:** none.

---

**Phase 3 complete (2026-07-06):** `MouseState` (15), `Mouse` (8), `MouseCursor` — all members re-audited;
confirmed `MouseCursor`/`Mouse::SetCursor` are correctly NOXNA (absent from FNA) and MouseCursor keeps SDL
out of the public surface via an opaque forward-decl. Added `IsKeyUp` test in Phase 2; no other gaps.

---

# Phase 4 — GamePad types

## A4-001 — `GamePadButtons` (struct) `[x]`
- [x] FNA `Input/GamePadButtons.cs`; CNA `GamePadButtons.hpp`/`.cpp`; test `GamePadButtonsTests.cpp`.
- [x] Members: ctors, per-button props, `Equals`/`==`/`!=`, `GetHashCode`, `FromButtonArray`. Per-member.

**Result (2026-07-06):** **11 button properties** (A, B, Back, X, Y, Start, LeftShoulder, LeftStick,
RightShoulder, RightStick, BigButton) — **exactly matches FNA** `GamePadButtons.cs` (no EXT properties: FNA
exposes none either; the EXT bits live only in the `Buttons` enum). **Ctors:** default (NOXNA) + `GamePadButtons(Buttons)`
(→ FNA). **`FromButtonArray`**: CNA exposes it `NOXNA static` mapping FNA's `internal static FromButtonArray`
(documented internal→NOXNA choice, used by the bridge). Equals/`==`/`!=`/GetHashCode present. All Doxygen'd.
**Test map:** default → `DefaultConstructorHasAllButtonsReleased`; Buttons ctor → `ConstructorFromCombinedFlags
SetsEveryMatchingGetter` (asserts **all 11 getters**) + `ConstructorFromSingleFlagLeavesOthersReleased`;
`FromButtonArray` → `FromButtonArrayCombinesMultipleFlagsAcrossElements` + `...WithEmptyListLeavesAllButtons
Released`; equality → `EqualityOperatorsForEqualAndDifferingInstances`; hash →
`GetHashCodeMatchesUnderlyingFlagsValueAndIsConsistent`. **Members reviewed:** 11 props + 2 ctors +
FromButtonArray + 4 equality/hash = all. **Files changed:** none (perfect, no gap). **Behavior verified:**
flag→property mapping + equality/hash. **Remaining risk:** none.

## A4-002 — `GamePadDPad` (struct) `[x]`
- [x] FNA `Input/GamePadDPad.cs`; CNA `GamePadDPad.hpp`/`.cpp`; test `GamePadDPadTest` suite.
- [x] Members: ctors, Up/Down/Left/Right, `Equals`/`==`/`!=`, `GetHashCode`, `FromButtonArray`. Per-member.

**Result (2026-07-06):** **4 direction props** (Down, Left, Right, Up) match FNA `GamePadDPad.cs`. The explicit
ctor's arg order `(upValue, downValue, leftValue, rightValue)` is **byte-identical to FNA** and pinned by
`ExplicitConstructorSetsEachDirectionIndependently`. `FromButtonArray` is `NOXNA static` (maps FNA's internal
one). All Doxygen'd. **Test map** (dedicated `GamePadDPadTest` suite): default →
`DefaultConstructorHasAllDirectionsReleased`; explicit ctor → `ExplicitConstructorSetsEachDirectionIndependently`;
`FromButtonArray` → `...DerivesDirectionsFromCombinedFlags` + `...CombinesAcrossSeparateListElements` +
`...WithEmptyListLeavesAllDirectionsReleased`; equality → `EqualityOperatorsForEqualAndDifferingInstances`;
hash → `GetHashCodeMatchesFnaBitWeightedFormula` (verifies FNA's exact bit-weighted hash). **Members
reviewed:** 4 props + 2 ctors + FromButtonArray + equality/hash = all. **Files changed:** none (perfect, no
gap). **Behavior verified:** ctor-slot order + FNA bit-weighted hash. **Remaining risk:** none.

## A4-003 — `GamePadThumbSticks` (struct) `[x]`
- [x] FNA `Input/GamePadThumbSticks.cs`; CNA `GamePadThumbSticks.hpp`/`.cpp`; test `GamePadThumbSticksTests.cpp`.
- [x] Members: ctors, Left/Right, dead-zone application, `Equals`/`==`/`!=`, `GetHashCode`. Per-member.

**Result (2026-07-06):** **Left/Right (Vector2)** properties match FNA `GamePadThumbSticks.cs`. Ctors:
default (NOXNA), public 2-arg `(leftPosition, rightPosition)`, and the private dead-zone-applying ctor
(mapping FNA's internal ctor). Equals/`==`/`!=`/GetHashCode present, all Doxygen'd. **Dead-zone math**
(the behavior-heavy part) is exhaustively covered: `TwoArgConstructorAppliesSquareClamp` (None-mode square
clamp in the ctor, FNA-faithful), `IndependentAxesModeExcludesPerAxisDeadZoneThenSquareClamps` +
`IndependentAxesModeZeroesValuesWithinDeadZone` (IndependentAxes), and `CircularMode{ZeroesValuesWithinDead
ZoneRadius,RescalesValueOutsideDeadZoneRadius,ClampsMagnitudeToUnitCircle}` (Circular). **GetHashCode:**
`GetHashCodeMatchesLeftPlus37TimesRightFormula` pins FNA's exact `Left + 37*Right` formula. Equality →
`EqualityOperatorsForEqualAndDifferingInstances`; default → `DefaultConstructorIsAtRest`. **Members
reviewed:** 2 props + 3 ctors + equality/hash + 3 dead-zone modes = all. **Files changed:** none (perfect,
no gap). **Behavior verified:** dead-zone (None/IndependentAxes/Circular) + FNA hash. **Remaining risk:**
none.

## A4-004 — `GamePadTriggers` (struct) `[x]`
- [x] FNA `Input/GamePadTriggers.cs`; CNA `GamePadTriggers.hpp`/`.cpp`; test `GamePadTriggersTests.cpp`.
- [x] Members: ctors, Left/Right clamp, `Equals`/`==`/`!=`, `GetHashCode`. Per-member.

**Result (2026-07-06):** **Left/Right (float)** match FNA `GamePadTriggers.cs`. The 2-arg ctor clamps to
`[0,1]` — FNA-faithful (`MathHelper.Clamp(x, 0, 1)`), pinned by `TwoArgConstructorClampsToZeroOneRange`.
Ctors: default (NOXNA), 2-arg, private dead-zone ctor. **Equality is FNA-faithful:** verified FNA's
`operator==` uses `MathHelper.WithinEpsilon` and CNA's `Equals` likewise uses `MathHelper::WithinEpsilon`
(epsilon tolerance is **not** a deviation), pinned by `EqualityUsesEpsilonToleranceRatherThanExactFloatEquality`.
`GetHashCode` → `GetHashCodeMatchesFloatBitHashSumFormula`. Dead-zone (trigger threshold) →
`NonNoneDeadZoneModeExcludesTriggerThresholdThenClamps` + `...ZeroesValueWithinThreshold` +
`NoneDeadZoneModePassesValueThroughClampedOnly`. **Members reviewed:** 2 props + 3 ctors + equality/hash +
3 dead-zone = all. **Files changed:** none (perfect, no gap). **Behavior verified:** clamp + epsilon equality
(FNA-faithful) + dead-zone + hash. **Remaining risk:** none.

## A4-005 — `GamePadCapabilities` (struct) `[x]`
- [x] FNA `Input/GamePadCapabilities.cs`; CNA `GamePadCapabilities.hpp`/`.cpp`; test (GamePad suites).
- [x] Members (~73 decls): IsConnected, GamePadType, every `Has*` property (XNA + EXT). Per-member —
  the largest surface.

**Result (2026-07-06):** **36 properties** (IsConnected, GamePadType + 34 `Has*`) — a precise diff vs FNA
`GamePadCapabilities.cs` reports **36=36, 0 CNA-only, 0 FNA-only** (exact match, incl. all EXT: LightBar,
TriggerVibrationMotors, Misc1, Paddle1-4, TouchPad, Gyro, Accelerometer). The 35 bool properties expose
**NOXNA setters** (1:1 with getters, confirmed by name diff) mapping FNA's `internal set`; `GamePadType`
get/set likewise. **No explicit `Equals`/`GetHashCode`/`operator==`** — verified FNA has none either (default
`ValueType` semantics), so CNA matches. Default ctor (`= default`). **Closed a subtle coverage gap:** the
existing `EveryGetterAndSetterRoundTrips` sets flags *cumulatively* (a getter mis-wired to an already-set
field would still read true), so **added** `EachBoolCapabilitySetterAffectsOnlyItsOwnGetter` — a
table-driven strict isolation test (set exactly one of the 35 flags → only that getter true, all others
false; `static_assert(n==35)`) that catches any getter↔field mis-wiring. Existing coverage:
`DefaultConstructorHasAllFlagsFalseAndTypeUnknown`, `EveryGetterAndSetterRoundTrips`,
`PartialCapabilitiesLeaveUnsetFlagsFalse`, + the EXT capability reflection tests
(`RumbleSupportReported*`/`GyroAndAccelerometerSupportReported*`). **Members reviewed:** 36 props + setters +
ctor = all. **Files changed:** `tests/…/GamePadTests.cpp` (+1 isolation test). **Behavior verified:** every
getter reflects exactly its own field; full FNA property parity. **Remaining risk:** none.

## A4-006 — `GamePadState` (struct) `[x]`
- [x] FNA `Input/GamePadState.cs`; CNA `GamePadState.hpp`/`.cpp`; test `GamePadStateTests.cpp`.
- [x] Members: ctors (default + full), IsConnected, PacketNumber, Buttons/DPad/ThumbSticks/Triggers,
  `IsButtonDown`/`IsButtonUp`, `Equals`/`==`/`!=`, `GetHashCode`, `ToString`. Per-member.

**Result (2026-07-06):** **Members match FNA `GamePadState.cs`:** IsConnected, PacketNumber (get + NOXNA set
= FNA internal set), Buttons/DPad/ThumbSticks/Triggers accessors, default ctor (NOXNA) + 4-arg + 5-arg ctors,
IsButtonDown, IsButtonUp, Equals/`==`/`!=`, GetHashCode, ToString. **Verified `IsButtonUp` is byte-identical
to FNA:** `(buttons & button) != button` (true unless ALL requested bits are set — not simply "all up").
`IsButtonDown` = `(buttons & button) == button`. `ToString` returns the fully-qualified type name, matching
FNA's `base.ToString()`. **Test map:** default → `DefaultConstructorProducesDisconnectedStateAtRest`; 4-arg →
`FourArgConstructor{MarksConnectedAndPacksExplicitButtons,PacksTriggersPastThresholdAsButtons,PacksThumbstick
DirectionsAsButtons}`; 5-arg → `FiveArgConstructorBuildsEquivalentPackedState`; IsButtonDown →
`IsButtonDownRequiresAllRequestedFlagsToBePressed`; Equals/PacketNumber →
`EqualityOperatorsForEqualAndDifferingInstances` + `EqualityConsidersPacketNumber`; hash →
`GetHashCodeMatchesButtonsHashXorPacketFormula`; ToString → `ToStringReturnsFullyQualifiedTypeNameRegardlessOfState`.
`IsButtonUp` was only asserted incidentally — **added dedicated** `IsButtonUpIsTrueUnlessAllRequestedButtons
AreDown` (single down→false, unpressed→true, partial-combined→true, all-down→false). **Members reviewed:**
all. **Files changed:** `tests/…/GamePadStateTests.cpp` (+1 test). **Behavior verified:** ctor packing +
IsButtonDown/Up FNA semantics + equality-with-packet + hash. **Remaining risk:** none.

## A4-007 — `GamePad` (static class) `[x]`
- [x] FNA `Input/GamePad.cs`; CNA `GamePad.hpp`/`.cpp`; tests `GamePadTests.cpp` + `GamePadInputTests.cpp`.
- [x] Members: `GetState` (×overloads incl. dead-zone), `GetCapabilities`, `SetVibration`, EXT
  (`GetGUIDEXT`/`SetLightBarEXT`/`SetTriggerVibrationEXT`/`GetGyroEXT`/`GetAccelerometerEXT`). Per-member.

**Result (2026-07-06):** **9 static members**, all matching FNA `GamePad.cs`: `GetCapabilities(PlayerIndex)`,
`GetState(PlayerIndex)` + `GetState(PlayerIndex, GamePadDeadZone)` (2 overloads), `SetVibration`,
`GetGUIDEXT`, `SetLightBarEXT(PlayerIndex, Color)`, `SetTriggerVibrationEXT`, `GetGyroEXT`,
`GetAccelerometerEXT`, plus `ExcludeAxisDeadZone`. **Tagging verified:** `ExcludeAxisDeadZone` is `NOXNA`
(maps FNA's `internal static`); the EXT sensor methods are `NOXNA` with the FNA `out Vector3` mapped to a
`Vector3&` (documented out→ref C++ deviation). **Overloads tested separately:** the dead-zone
`GetState(PlayerIndex, GamePadDeadZone)` is exercised distinctly (`GamePadInputTests.cpp:62,189`) from the
plain overload; the out-ref sensor reads verify the out value (`GyroAndAccelReadReturnData`). **Test refs:**
GetState 82, SetVibration 12, SetLightBarEXT 8, SetTriggerVibrationEXT 6, GetGyroEXT 4, GetCapabilities/
GetGUIDEXT/GetAccelerometerEXT 3 each — all covered. **Members reviewed:** 9/9. **Files changed:** none
(perfect, no gap). **Behavior verified:** FNA parity + overload separation + out→ref convention. **Remaining
risk:** none.

---

**Phase 4 complete (2026-07-06):** all 7 GamePad types re-audited member-by-member vs FNA. Every member
present, correctly tagged (strict / EXT / NOXNA), and covered by a named test. **Added 2 tests:** the
`GamePadCapabilities` 35-flag strict isolation guard (A4-005) and the `GamePadState` IsButtonUp semantics
test (A4-006). Confirmed FNA-faithful: dead-zone math, trigger clamp + epsilon equality, DPad bit-weighted
hash, IsButtonDown/Up bit semantics, ctor arg orders, out→ref sensor convention.

---

# Phase 5 — Touch types

## A5-001 — `TouchLocation` (struct) `[x]`
- [x] FNA `Input/Touch/TouchLocation.cs`; CNA `Touch/TouchLocation.hpp`/`.cpp`; test `TouchInputTests.cpp`.
- [x] Members: ctors (3-arg/5-arg/default), Id/State/Position, `TryGetPreviousLocation`, `Equals`/`==`/`!=`,
  `GetHashCode`, `ToString`. Per-member.

**Result (2026-07-06):** **Members match FNA `TouchLocation.cs`** (no `Pressure` — XNA 4.0 dropped it, CNA
correctly omits it): Id/State/Position props, default (NOXNA)/3-arg/5-arg ctors, `TryGetPreviousLocation`,
`Equals`/`==`/`!=`, `GetHashCode`, `ToString`. All Doxygen'd. **Test map** (each named): default →
`DefaultConstructorProducesInvalidLocation`; 3-arg → `ThreeArgConstructorSetsIdStateAndPosition`; 5-arg →
`FiveArgConstructorTracksPreviousStateAndPosition`; `TryGetPreviousLocation` (true+false paths) →
`TryGetPreviousLocationFalsePathWritesInvalidPreviousLocationLikeFna` + the 5-arg test; equality (incl.
previous fields) → `EqualityOperatorsForEqualAndDifferingInstances` + `EqualityDistinguishesPreviousStateAnd
Position`; `GetHashCode` → `GetHashCodeMatchesFnaIdPlusPositionFormula` (FNA `Id+Position`) +
`...IsConsistentForEqualInstances`; `ToString` → `ToStringMatchesFnaFormatExactly` +
`ToStringContainsPositionValues`. **Members reviewed:** all. **Files changed:** none (perfect, no gap;
thoroughly covered — GetHashCode-formula test added in the earlier P5-004). **Behavior verified:** ctors +
previous-location + equality/hash/ToString FNA parity. **Remaining risk:** none.

## A5-002 — `TouchCollection` (struct) `[x]`
- [x] FNA `Input/Touch/TouchCollection.cs`; CNA `Touch/TouchCollection.hpp`/`.cpp`; test `TouchInputTests.cpp`.
- [x] Members: ctors, Count/IsConnected/IsReadOnly, `operator[]` (const+mutable), Contains, FindById, CopyTo,
  IndexOf, Add/Clear/Remove/RemoveAt/Insert, begin/end, empty. Per-member.

**Result (2026-07-06):** All **FNA `TouchCollection.cs` members present**: `Count`→`getCountProperty`,
`IsConnected`→`getIsConnectedProperty`, `IsReadOnly`→`getIsReadOnlyProperty`, `this[]`→`operator[]`
(const+mutable), Contains, **FindById** (verified **FNA/XNA 4.0 HAS it** at `TouchCollection.cs:112` with
`out TouchLocation` → CNA maps to `TouchLocation&`, so it is **correctly NOT `NOXNA`** — strict XNA), CopyTo,
IndexOf, Add/Clear/Remove/RemoveAt/Insert. CNA STL-ergonomics extras `begin`/`end`/`empty` are correctly
`NOXNA` (replace FNA's `GetEnumerator`). **17 `TouchCollectionTest` cases** cover every member:
Count/empty (`CountAndEmptyReflectContents`), IsConnected/IsReadOnly (+ advisory-mutation `...IsAdvisoryAnd
MutationStillSucceedsLikeFna`), indexer (const+mutable+OOB throw), Contains, FindById, CopyTo (append/empty/
OOB/non-zero-index), IndexOf, Add/Clear/Remove/RemoveAt/Insert, begin/end iteration order (P5-003), empty
iteration. **Documented deviation:** `std::vector`-backed (vs FNA's null-backed default list) →
mutable+advisory `IsReadOnly` (DEC / P5-001). **Members reviewed:** all. **Files changed:** none (perfect,
no gap; corrected a false-alarm — FindById is genuine FNA API, not a missing NOXNA tag). **Behavior
verified:** full member + FNA parity. **Remaining risk:** none.

## A5-003 — `TouchPanelCapabilities` (struct) `[x]`
- [x] FNA `Input/Touch/TouchPanelCapabilities.cs`; CNA `Touch/TouchPanelCapabilities.hpp`/`.cpp`; test
  `TouchInputTests.cpp`.
- [x] Members: ctors, IsConnected, MaximumTouchCount. Per-member.

**Result (2026-07-06):** **2 properties match FNA** `TouchPanelCapabilities.cs`: `IsConnected` →
`getIsConnectedProperty`, `MaximumTouchCount` → `getMaximumTouchCountProperty`. CNA adds a default + 2-arg
ctor, both `NOXNA` — correctly, since FNA/XNA has **no public constructor** (the properties have internal
set); documented in the header. **Test map:** `TouchPanelCapabilitiesTest.DefaultConstructorProducesDisconnected
ZeroCapacity` (default → IsConnected=false, MaxTouchCount=0) + `ParameterizedConstructorSetsConnectionAndMax
TouchCount` (both properties set). Behavior at the `TouchPanel::GetCapabilities` level (MaxTouchCount=4
connected / 0 disconnected, DEC-09) is covered by the GetCapabilities suites (A5-005). **Members reviewed:**
2 props + 2 ctors. **Files changed:** none (perfect, no gap). **Behavior verified:** property + ctor parity.
**Remaining risk:** none.

## A5-004 — `GestureSample` (struct) `[x]`
- [x] FNA `Input/Touch/GestureSample.cs`; CNA `Touch/GestureSample.hpp`/`.cpp`; test `TouchInputTests.cpp`.
- [x] Members: ctors (public + internal-equiv), GestureType, Timestamp, Position/Position2, Delta/Delta2,
  FingerId/FingerId2 EXT. Per-member.

**Result (2026-07-06):** **8 properties match FNA `GestureSample.cs`:** GestureType, Timestamp, Position,
Position2, Delta, Delta2 (strict XNA) + `FingerIdEXT`, `FingerId2EXT` — verified these two **are genuine FNA
extensions** (FNA `GestureSample.cs:61,67`), so CNA correctly tags them `NOXNA` + `EXT` suffix (FNA additions
beyond stock XNA, not CNA-invented). Ctors: default (NOXNA), public 6-field (→ FNA public ctor), internal
NOXNA ctor adding the finger ids (defaulting to `NO_FINGER` — matches FNA lines 93-94/117-118). **Test map:**
default → `DefaultConstructorProducesZeroedNoneSample`; public ctor →
`PublicConstructorSetsFieldsAndDefaultsFingerIdsToNoFinger` (verified it asserts **all 8 getters**);
internal ctor → `InternalConstructorSetsExplicitFingerIds`. **Members reviewed:** 8 props + 3 ctors = all.
**Files changed:** none (perfect, no gap; confirmed FingerIdEXT is FNA API). **Behavior verified:** all
fields + finger-id defaulting. **Remaining risk:** none.

## A5-005 — `TouchPanel` (static class) `[x]`
- [x] FNA `Input/Touch/TouchPanel.cs`; CNA `Touch/TouchPanel.hpp`/`.cpp`; tests `TouchInputTests.cpp` +
  `TouchEdgeCaseTests.cpp` + bridge.
- [x] Members: `GetState`, `GetCapabilities`, EnabledGestures get/set, IsGestureAvailable, ReadGesture,
  DisplayWidth/Height/Orientation, WindowHandle, EnqueueGesture/SetFinger/Update/INTERNAL_onTouchEvent
  (NOXNA), reset. Per-member.

**Result (2026-07-06):** **All 9 FNA public-static members present:** DisplayWidth/Height/Orientation
get+set, EnabledGestures get+set, IsGestureAvailable, WindowHandle get+set, GetCapabilities, GetState,
ReadGesture. **NOXNA tagging verified correct:** `EnqueueGesture`, `INTERNAL_onTouchEvent`, `SetFinger`,
`Update` are all `internal static` in FNA (`TouchPanel.cs:121,126,165,219`) → CNA correctly maps them to
`NOXNA`; `updateInputManagerTouch`/`setTouchDeviceExists`/`ResetForTests` are CNA/test helpers (NOXNA).
**Test map:** GetState (many), GetCapabilities (`GetCapabilities*`), EnabledGestures
(`EnabledGesturesGetterAndSetterRoundTrip`, `DefaultEnabledGesturesIsNone`, `ChangingEnabledGestures…`),
IsGestureAvailable (`IsGestureAvailableReflectsQueueState`), ReadGesture (`EnqueueGestureAndReadGestureFollow
FifoOrder`, `ReadGestureThrows…`), DisplayWidth/Height/Orientation (`DisplayWidthHeightAndOrientationGetter
AndSetterRoundTrip`), EnqueueGesture/SetFinger/Update/INTERNAL_onTouchEvent (gesture + scaling suites),
ResetForTests (reset suite). **Closed a real gap:** `WindowHandle` was only signature-frozen (no functional
test) — **added** `TouchInputTest.WindowHandleGetterAndSetterRoundTrip` (set→read→reset-to-0). **Members
reviewed:** all. **Files changed:** `tests/…/TouchInputTests.cpp` (+1 test). **Behavior verified:** full
member coverage incl. WindowHandle round-trip. **Remaining risk:** none.

---

**Phase 5 complete (2026-07-06):** all 5 Touch types re-audited member-by-member vs FNA. Every member
present + correctly tagged (confirmed `FindById` is genuine XNA API not a missing NOXNA; `FingerIdEXT` is
genuine FNA API; FNA's gesture-plumbing methods are internal→NOXNA). **Added 1 test** (TouchPanel WindowHandle
functional round-trip). No behavior gaps.

---

# Phase 6 — Text input

## A6-001 — `TextInputEXT` (static class, NOXNA) `[x]`
- [x] FNA `Input/TextInputEXT.cs`; CNA `TextInputEXT.hpp`/`.cpp`; tests `TextInputEXTTests.cpp` + bridge.
- [x] Members: `TextInput`/`TextEditing` multicast events, WindowHandle, IsTextInputActive,
  IsScreenKeyboardShown (×overloads), StartTextInput/StopTextInput, SetInputRectangle,
  INTERNAL_OnTextInput/OnTextEditing, reset. Per-member.

**Result (2026-07-06):** **All FNA `TextInputEXT.cs` public members present:** `TextInput` (MulticastAction
<charcs> ↔ FNA `event Action<char>`), `TextEditing` (MulticastAction<const string&,int,int> ↔ FNA `event
Action<string,int,int>`), `WindowHandle` get/set, `IsTextInputActive()`, `IsScreenKeyboardShown()` +
`IsScreenKeyboardShown(window)` (2 overloads), `StartTextInput()`, `StopTextInput()`, `SetInputRectangle
(Rectangle)`. Plus `INTERNAL_OnTextInput`/`INTERNAL_OnTextEditing` (FNA internals → NOXNA) and `ResetForTests`.
The whole class is correctly `NOXNA` (FNA extension, not XNA 4.0). **Test map** (14 `TextInputEXTTest` +
bridge): TextInput dispatch/multicast/no-subscriber; TextEditing dispatch/empty/multicast; WindowHandle
round-trip + `ResetForTestsClearsWindowHandle…`; IsTextInputActive (no-window + real-window); IsScreenKeyboard
Shown **both overloads** (`IsScreenKeyboardShownIsFalseWithoutWindow` asserts `()` and `(0)`); Start/Stop
(no-window no-op + real-window round-trip); SetInputRectangle (no-window + zero/negative + real-window);
INTERNAL_* via the dispatch + bridge tests; ResetForTests (window handle + suppress-flag). **Members
reviewed:** all. **Files changed:** none (perfect, no gap; thoroughly audited in the earlier Phase 7).
**Behavior verified:** full member + FNA parity. **Remaining risk:** real IME composition is HW-gated (`[!]`,
Phase 11).

---

**Phase 6 complete (2026-07-06):** `TextInputEXT` fully re-audited — every FNA member present, whole class
correctly NOXNA, both `IsScreenKeyboardShown` overloads tested, all members named-tested. No gaps.

---

# Phase 7 — Internal classes (`CNA::Internal::Input`)

## A7-001 — `InputManager` `[x]`
- [x] CNA `InputManager.hpp`/`.cpp`; tests `InputResetTests.cpp` + subsystem suites.
- [x] Members: every `Set*`/`Get*State` accessor (keyboard/mouse/gamepad/touch), reset entry points.
  Verify each mutates/reads the accumulated singleton correctly; per-member test.

**Result (2026-07-06):** Enumerated **18 public static methods** and mapped test coverage. The 10 setters/
mutators (`SetKeyState` 13, `SetMouseButtonState` 9, `SetMousePosition` 4, `SetMouseRelativeMode` 2,
`AddScrollWheelDelta` 2, `AddMouseRelativeDelta` 3, `SetGamePadButtonState` 11, `SetGamePadAxisValue` 31,
`SetGamePadConnection` 31, `SetTouchState` 29 refs) and both resets (`ResetForTests` 24, `ResetAllForTests`
25) are **directly** test-referenced; `GetTouchState` (17) and `GetRawGamePadState` (2) directly. The
delegating getters `HasAnyTouch`/`GetKeyboardState`/`GetMouseState` have **0 direct** test refs but are
**live and indirectly covered** — `HasAnyTouch` ← `TouchPanel::GetCapabilities` (GetCapabilities tests),
`GetKeyboardState` ← `SdlInputBridge` control-key/text paths (bridge keyboard tests), `GetMouseState` ←
`Mouse::GetState` (every Mouse test). **Removed dead code:** `InputManager::GetGamePadState(PlayerIndex)`
had **zero callers anywhere** in the repo and its body delegated *backwards* to the public
`GamePad::GetState` — deleted the declaration + definition. **Files changed:**
`include/CNA/Internal/Input/InputManager.hpp`, `src/CNA/Internal/Input/InputManager.cpp` (dead-method
removal — provably behavior-neutral, no caller). **Tests:** `ctest -L input` 100% green after removal.
**Behavior verified:** every remaining public method is exercised (directly or via the public API it backs).
**Remaining risk:** none.

## A7-002 — `GestureDetector` `[x]`
- [x] FNA `Input/Touch/GestureDetector.cs`; CNA `GestureDetector.hpp`/`.cpp`; test `GestureDetectorTests.cpp`.
- [x] Members: OnPressed/OnMoved/OnReleased/OnUpdate, test-clock hooks, reset. Verify the gesture state
  machine + thresholds vs FNA; per-member/behavior test.

**Result (2026-07-06):** The **4 FNA methods** `OnPressed`/`OnMoved`/`OnReleased`/`OnUpdate` are present and
map FNA's `internal static` ones (CNA keeps them internal to `CNA::Internal::Input`). Plus 4 NOXNA test
hooks (`ResetForTests`, `EnableTestClock`, `DisableTestClock`, `AdvanceTestClockMilliseconds`). **Coverage:**
`OnPressed`/`OnMoved`/`OnReleased` have 0 *direct* refs but are exercised through the production entry point
`TouchPanel::INTERNAL_onTouchEvent` (via the `Press`/`Move`/`Release` helpers) in the **35-test**
`GestureDetectorTest` suite — the correct integration path. `OnUpdate` also via `TouchPanel::Update`.
The state machine + every threshold were verified **byte-identical to FNA** in the earlier Phase-6 pass
(MOVE_THRESHOLD=35, flick vel 100, hold 1s, double-tap 300ms/35px) with exhaustive positive/negative-path
tests (tap/double-tap/hold/drag×3/flick/pinch + rejections + disabled + reset). Test hooks all directly
referenced (`AdvanceTestClockMilliseconds` 9, `ResetForTests` 7, `EnableTestClock` 4, `DisableTestClock` 2).
**Members reviewed:** 4 gesture methods + 4 hooks = all. **Files changed:** none (perfect, no gap; logic
verified in Phase 6). **Behavior verified:** full gesture state machine vs FNA. **Remaining risk:** none.

## A7-003 — `SdlGamepadBackend` / `ISdlGamepadBackend` `[x]`
- [x] CNA `SdlGamepadBackend.hpp`/`.cpp`; test `SdlGamepadBackendTests.cpp` + `FakeSdlGamepadBackend.hpp`.
- [x] Members: the `ISdlGamepadBackend` seam methods (open/close/rumble/led/sensor/…) + real impl. Verify each
  is exercised via the fake; seam never leaks into the XNA layer.

**Result (2026-07-06):** The `ISdlGamepadBackend` seam has **19 virtual methods** (IsGamepad, OpenGamepad,
CloseGamepad, GetGamepadJoystick, GetJoystickType/Vendor/Product, GamepadHasButton/Axis/Sensor,
GetNumGamepadTouchpads, GetGamepadProperties, RumbleGamepad, RumbleGamepadTriggers, SetGamepadLED,
GamepadSensorEnabled, SetGamepadSensorEnabled, GetGamepadSensorData, GetGamepadType). **`FakeSdlGamepadBackend`
overrides ALL 19** (verified 0 interface methods un-overridden), so every seam method is injectable +
testable; the fake adds introspection counters (open/close/rumble/led/sensor + `lastRumble/Trigger/Led`,
`setSensorEnabledCalls`). Driven through the real bridge in **37 `SdlGamepadBackend` tests** + the
`FakeGamepad*` suites (open/close/reconnect/slot-reuse, rumble clamp+NaN, trigger rumble, LED, sensor
lazy-enable, GUID vendor/product, joystick-type map — the Phase-4 coverage). **No leak:** `ISdlGamepadBackend`
appears in **no** `include/Microsoft/` header (confirmed) — the seam is hidden from the XNA layer, also
enforced by `PublicApiInputCompileTests`. **Members reviewed:** 19/19 seam methods. **Files changed:** none
(perfect, no gap). **Behavior verified:** full seam faked + tested; no XNA-layer leak. **Remaining risk:**
real-hardware actuation is HW-gated (`[!]`, Phase 11) — the fake proves translation/bookkeeping only.

## A7-004 — `SdlInputBridge` `[x]`
- [x] CNA `SdlInputBridge.hpp`/`.cpp`; tests: all `SdlInputBridge*` + golden + fuzz.
- [x] Members: `ProcessEvent` (every SDL case), init/reset, window resolution, UTF-8 decode, id maps, test
  hooks. Verify each event case + helper; per-case test.

**Result (2026-07-06):** Public method set reviewed. **`ProcessEvent`** (190 test refs) — its **17 SDL event
cases** were enumerated and each shown tested in the earlier P8-001 (mouse/keyboard/text/touch/gamepad),
with the ignored-event no-op pinned (`UnconsumedResizeDisplayAndQuitEventsDoNotAffectInputState`); the
UTF-8 decoder, control-char synthesis, Ctrl+V suppression, and window resolution were verified in the
Phase-7/8 passes and are stressed by the deterministic **fuzz** (5000 events) + **golden** exact-state
scripts. **Gamepad ops** (`SetVibration`, `SetTriggerVibration`, `SetLightBar`, `GetGUID`,
`FormatGamePadGUIDEXT`, `GetGyro`, `GetAccelerometer`, `GetCapabilities`) are reached via `GamePad`'s public
EXT methods over the fake backend (Phase-4 tests: vibration clamp/NaN, LED, sensor, GUID format).
`GetKeyFromScancode` ← `Keyboard::GetKeyFromScancodeEXT`. **Test hooks** (`ResetForTests` 4,
`SetScancodeModeForTests` 5, `SetGamepadCountForTests` 2, `EnsureGamepadSubsystemInitialized` 2,
`ParseGamepadCountForTests`) all exercised; `SetSdlGamepadBackendForTests` installs the fake (used
throughout the FakeGamepad suites). **Members reviewed:** all public methods. **Files changed:** none
(perfect, no gap; the most heavily audited file — P8-001..008 + fuzz + golden). **Behavior verified:**
every event case + helper covered. **Remaining risk:** none.

---

**Phase 7 complete (2026-07-06):** all 4 internal classes re-audited. **Removed dead code**
(`InputManager::GetGamePadState`, zero callers). Confirmed: GestureDetector's 4 methods map FNA internals
(byte-identical logic, Phase 6); `ISdlGamepadBackend`'s 19-method seam is fully faked with no XNA-layer leak;
`SdlInputBridge` public surface fully covered (17 event cases + helpers + hooks).

---

# Phase 8 — Cross-cutting final gates

## A8-001 — Regenerate member parity matrix `[x]`
- [x] Re-run `tools/input_parity/gen_input_parity_matrix.py`; confirm 0 STRICT/EXT gaps, 0 FNA-only; commit.

**Result (2026-07-06):** Regenerated `docs/input-member-parity-matrix.md`: **26 types, 0 STRICT/EXT gaps,
0 FNA-only members**. The file is **byte-identical** to the committed version (`git diff` empty) — the
per-type audit added only tests + removed one *internal* method (`InputManager::GetGamePadState`), so the
**public XNA member parity is unchanged**. **Files changed:** none (matrix regenerated identically).
**Remaining risk:** none.

## A8-002 — Regenerate test-coverage document `[x]`
- [x] Re-run `tools/input_parity/check_input_test_coverage.py`; confirm every type has a dedicated suite;
  update `docs/input-test-coverage.md`.

**Result (2026-07-06):** Regenerated `docs/input-test-coverage.md` (26 public + 8 internal types).
**Gaps: "None — every Input type has a dedicated suite or a documented sibling-suite cover."** The file is
unchanged (this audit's new tests — IsKeyUp, capabilities isolation, IsButtonUp, TouchPanel WindowHandle
round-trip — live in test files that already referenced those types, so the type→test-file mapping is
unchanged). **Files changed:** none (regenerated identically). **Remaining risk:** none.

## A8-003 — Re-verify signature + enum freeze `[x]`
- [x] Confirm `PublicApiInputSignatureFreezeTests` + `PublicApiInputCompileTests` + enum-value suites still
  green; confirm no public API drift from the audit.

**Result (2026-07-06):** `PublicApiInputSignatureFreezeTests` + `PublicApiInputCompileTests` + the enum-value
suites (ButtonState/KeyState/Keys/Buttons/GamePadDeadZone/GamePadType/GestureType/TouchLocationState) run
**green** — no public-XNA signature or enum-value drift from the audit. This is expected: the audit only
**added tests** and removed one *internal* method (`InputManager::GetGamePadState`), touching **no public
XNA signature or enum**. The frozen golden `docs/input-public-api-frozen.md` is unchanged. **Files changed:**
none (verification). **Remaining risk:** none.

## A8-004 — Full input suite + backends + sanitizer `[x]`
- [x] `ctest -L input` green on EasyGL/Vulkan/bgfx/SDL_RENDERER; ASan+UBSan-clean; record counts.

**Result (2026-07-06):** Rebuilt all four backends after the dead-code removal and re-ran the gate:
**`ctest -L input` = 100% on EasyGL, Vulkan, bgfx, and SDL_RENDERER**. A single pass reports **382 input
tests / 45 suites** (was 378 — **+4** this audit: `IsKeyUpIsTheComplementOfIsKeyDown`,
`EachBoolCapabilitySetterAffectsOnlyItsOwnGetter`, `IsButtonUpIsTrueUnlessAllRequestedButtonsAreDown`,
`WindowHandleGetterAndSetterRoundTrip`). **ASan+UBSan-clean:** the sanitizer build ran the full input filter
→ **381 PASSED, zero sanitizer reports** (halt_on_error=1). **Files changed:** none (build + run + record).
**Remaining risk:** none.

## A8-005 — Final perfection statement `[x]`
- [x] Write a final status (below).

### Final per-type audit statement (2026-07-06)

**Scope covered.** All **26 public XNA/EXT types** (8 enums, KeyboardState/Keyboard, MouseState/Mouse/
MouseCursor, 7 GamePad types, 5 Touch types, TextInputEXT) and **4 internal classes** (InputManager,
GestureDetector, SdlGamepadBackend/ISdlGamepadBackend, SdlInputBridge) were re-audited **member by member**
against the FNA source.

**Member parity — complete.** Every public member is present and matches FNA in name/signature/value/behavior
(parity matrix: 26 types, **0 STRICT/EXT gaps, 0 FNA-only**). Correct tier tagging was confirmed *by checking
FNA*, not assumed: `MouseCursor`/`Mouse::SetCursor` are genuinely absent from FNA → correctly `NOXNA`;
`TouchCollection::FindById` and `GestureSample::FingerId(2)EXT` ARE genuine FNA/XNA API → correctly *not*
mis-tagged; FNA's gesture-plumbing (`EnqueueGesture`/`SetFinger`/`Update`/`INTERNAL_onTouchEvent`) is
`internal` → correctly `NOXNA`.

**Behavior — verified FNA-faithful.** Spot-verified line-by-line where logic is non-trivial: MouseState 8-arg
ctor slot order; GamePadDPad ctor order + bit-weighted hash; GamePadThumbSticks dead-zone (None/Independent/
Circular) + `Left+37*Right` hash; GamePadTriggers clamp + `WithinEpsilon` equality (FNA-faithful, not a
deviation); GamePadState `IsButtonDown`/`IsButtonUp` = `(buttons & b) [==|!=] b`; KeyboardState 8×32 XOR hash;
TouchLocation `Id+Position` hash; Keys 160-value byte-identity.

**Tests per member — confirmed, gaps closed.** Every member has a named test (directly or via the public API
it backs). **+4 tests added** to close real gaps: dedicated `IsKeyUp` (KeyboardState) and `IsButtonUp`
(GamePadState) complement tests; a **35-flag strict isolation guard** for `GamePadCapabilities` (catches any
getter↔field mis-wiring); a functional `TouchPanel::WindowHandle` round-trip (was signature-frozen only).

**Fix/improve.** Removed dead code: `InputManager::GetGamePadState` (zero callers repo-wide; body delegated
backwards to the public `GamePad::GetState`). Behavior-neutral.

**Verification.** `ctest -L input` = 100% on all four backends (EasyGL/Vulkan/bgfx/SDL_RENDERER), **382 tests /
45 suites**; **ASan+UBSan-clean** (381). No public API drift (signature + enum freeze green; parity matrix
unchanged).

**Manual/HW status — unchanged (`[!]`).** Real-hardware actuation (rumble/LED/sensors), real IME composition,
and high-DPI pixel scaling remain manual-only (prior plan's Phase 11 in git history).

**Not overstated:** this pass audited **member existence + parity + per-member tests + spot-checked behavior**.
A deeper **line-by-line source-LOGIC** audit of every `src/` file (does each behave exactly like XNA 4.0) is
the next phase below.

---

# Phase 9 — Source-logic behavioral audit (per user directive, 2026-07-06)

> **Goal (user directive):** thoroughly check the **LOGIC** of every CNA input source file — does it behave
> like XNA 4.0 (or as close as possible)? — and **fix/improve** where it diverges. This goes deeper than the
> per-type member audit above: it reads each `src/` method body line-by-line against the FNA implementation
> and validates the actual computation (branches, math, edge cases, ordering), not just presence + a test.
> One task per source file (or tight group). **One task = one commit, never batched.** Fixes get tests +
> re-run the gate (and ASan if behavior changed). Genuine FNA divergences are either fixed or documented as
> `DEC-*`.

## L-001 — `KeyboardState.cpp` logic `[x]`
- [x] Line-by-line vs FNA `KeyboardState.cs`: indexer/IsKeyDown/IsKeyUp lookup, GetPressedKeys ordering,
  the 8×32-bit XOR GetHashCode + `InternalSetKey` bounds, Equals set-compare. Fix/doc any divergence.

**Result (2026-07-06):** Read every method vs FNA — **logic is FNA-faithful; no divergence to fix.**
`IsKeyDown` = `pressedKeys_.contains(key)` (≡ FNA bitfield test); `IsKeyUp` = `!IsKeyDown` (≡ FNA);
`getItem`/`operator[]` = Down/Up (≡ FNA `this[key]`); `GetPressedKeys` copies the set and **sorts ascending
numeric** = FNA's keys0..keys7 bit-0-upward walk order; `Equals` = set equality (≡ FNA's per-word compare —
same pressed set ⇒ equal); `GetHashCode` rebuilds the 8×32-bit words with the `value < 256u` bounds guard
(matches FNA `InternalSetKey`'s `switch(((int)key)>>5)` cases 0..7 with no default — out-of-range keys map
to no word) and XORs all 8 (≡ FNA `keys0 ^ … ^ keys7`); `ToString` = fully-qualified type name (≡ FNA
`ValueType` default — FNA never overrides it). **The one deviation is the accepted, documented one**
(P2-002): CNA's `unordered_set` *can store* an out-of-range `Keys` so `IsKeyDown((Keys)999)` differs from
FNA's bitfield store — not producible from hardware, memory-safe (GetHashCode ignores it exactly like FNA).
**Files changed:** none (logic verified, no fix needed). **Behavior verified:** all methods match FNA
computation. **Remaining risk:** none.

## L-002 — `MouseState.cpp` logic `[x]`
- [x] vs FNA `MouseState.cs`: field packing, `==` field-by-field compare, ToString format, hash choice.

**Result (2026-07-06):** Logic verified vs FNA — **faithful, no fix needed.** Ctors set every field (8-arg
param order confirmed in A3-001). `Equals` compares X/Y/5 buttons/ScrollWheel field-by-field = FNA `operator==`
(`MouseState.cs:142-158`). **`ToString` is byte-identical to FNA** (`MouseState.cs:185-229`): buttons rendered
in order Left→Right→Middle→XButton1→XButton2, space-separated, "None" when empty, wrapped as
`[MouseState X={x}, Y={y}, Buttons={..}, Wheel={w}]`. **`GetHashCode`** = `x ^ (y*31) ^ (scrollWheel*17)`
with unsigned wraparound (avoids signed-overflow UB) — a **deterministic field-based choice** (documented
P3-003) vs FNA's non-deterministic `base.GetHashCode()`; note it hashes position+wheel (not the button
states, which still affect `Equals`) — a valid hash (equal⇒equal). **Files changed:** none (logic verified).
**Behavior verified:** field compare + exact ToString + deterministic hash. **Remaining risk:** none.

## L-003 — `Mouse.cpp` logic `[x]`
- [x] vs FNA `Mouse.cs`: GetState assembly, SetPosition (relative-mode guard + warp), WindowHandle
  resolution, relative-mode get/set, ClickedEXT dispatch.

**Result (2026-07-06):** Logic verified vs FNA — **faithful, no fix needed.** `SetPosition` opens with the
same relative-mode short-circuit as FNA (`Mouse.cs:99-103`, "meaningless in relative mode → return"), then
updates the InputManager logical position and warps the OS cursor — adding the **P3-001 null-window guard**
(never hands SDL a null window) and the **a-0001 logical→window** conversion (generalizes FNA's fixed
back-buffer ratio through the graphics backend / `SDL_RenderCoordinatesToWindow`). `getIsRelativeMouseModeEXT`
reads SDL **live** (`SDL_GetWindowRelativeMouseMode`, DEC-14 — ≡ FNA `GetRelativeMouseMode`), false with no
window; the setter calls `SDL_SetWindowRelativeMouseMode` + mirrors the flag into InputManager to gate delta
accumulation (DEC-14). `GetState` delegates to the event-driven `InputManager::GetMouseState()` (coords are
converted window→logical at event time, a-0001 — ≡ FNA's GetState scaling). `INTERNAL_onClicked` dispatches
`ClickedEXT` if subscribed (≡ FNA `ClickedEXT?.Invoke`). `SetCursor` (NOXNA) guards a disposed/null handle
(SDL_SetCursor(NULL) would redraw not clear) — matches MonoGame. **Files changed:** none (logic verified;
deviations are the documented a-0001 coordinate model + DEC-14 relative-mode + P3-001 null guard).
**Behavior verified:** SetPosition guard+warp, live relative-mode, click dispatch. **Remaining risk:** none.

## L-004 — `MouseCursor.cpp` logic `[x]`
- [x] Cursor ownership/move/dispose lifecycle; FromTexture2D surface build + format validation + hotspot.

**Result (2026-07-06):** MouseCursor is not in FNA/XNA 4.0, so audited vs **MonoGame** semantics —
**logic correct, no fix needed.** **RAII lifecycle:** default ctor creates the SDL default cursor (owning);
the `SDL_Cursor*` ctor stores handle+owning; the **move ctor/assign** transfer `sdlCursor_/owning_/
isDisposed_/isSystemSingleton_` and null the source (`sdlCursor_=null, owning_=false, isDisposed_=true` —
so a moved-from source's `Dispose` is inert); move-assign `Dispose()`s the previous handle first; the dtor
`Dispose()`s. **`Dispose` is idempotent** (`isDisposed_` guard) and **protects stock singletons**
(`isSystemSingleton_` → no `SDL_DestroyCursor`, avoiding corrupting the process-shared cursor / a free after
`SDL_Quit`). **`FromTexture2D`**: rejects non-Color/ColorSrgb formats (`invalid_argument`); extracts packed
RGBA into a raw `uint32_t` buffer (Color has a vtable → not tightly packed) matching `SDL_PIXELFORMAT_RGBA32`;
`SDL_CreateSurfaceFrom` (no copy) → `SDL_CreateColorCursor` (copies pixels, verified vs SDL3 source, task 831)
→ destroy surface; an out-of-texture `originX/Y` makes `SDL_CreateColorCursor` return null → `runtime_error`
(pinned by `FromTexture2DThrowsWhenOriginIsOutsideTheTexture`, green under Xvfb). **12 stock cursors** map
MonoGame→SDL3 names (task 833; WaitArrow→PROGRESS since SDL3 dropped WAITARROW) as lazy Meyer's singletons
(≡ MonoGame's lazy static ctor). **Files changed:** none (logic verified). **Behavior verified:** ownership
transfer + dispose safety + FromTexture2D pipeline. **Remaining risk:** none (origin bounds are enforced by
SDL's own hotspot check — behavior is correct).

## L-005 — `GamePadButtons.cpp` / `GamePadDPad.cpp` logic `[x]`
- [x] Flag→property extraction; FromButtonArray OR-combine; DPad bit-weighted hash vs FNA.

**Result (2026-07-06):** Both files **byte-identical to FNA in logic; no fix.** **GamePadButtons:**
`ButtonStateFromFlag` = `(buttons_ & flag) == flag ? Pressed : Released` (all-bits-set check ≡ FNA's
per-property test); all **11 getters map to their own flag** (A→A … BigButton→BigButton, verified 1:1, no
mis-wiring); `FromButtonArray` OR-combines the list (≡ FNA); `Equals` = field compare; `GetHashCode` =
`(int)buttons_` ≡ FNA `(int)this.buttons`. **GamePadDPad:** `FromButtonArray` OR-combines then extracts
`flagToState(mask, DPadUp/Down/Left/Right)` into the (up,down,left,right) ctor (correct 1:1); `GetHashCode`
= `(Down?1)+(Left?2)+(Right?4)+(Up?8)` — **byte-identical to FNA `GamePadDPad.cs:138-146`** (same weights);
`Equals` = 4-field compare. **Files changed:** none (logic verified). **Behavior verified:** flag extraction
+ OR-combine + exact FNA hash weights. **Remaining risk:** none.

## L-006 — `GamePadThumbSticks.cpp` logic `[x]`
- [x] Dead-zone math (ExcludeAxisDeadZone, IndependentAxes, Circular, square-clamp) line-by-line vs FNA.

**Result (2026-07-06):** **The entire file is byte-identical to FNA `GamePadThumbSticks.cs`** — no fix. Both
ctors match (public: `ApplySquareClamp()`; internal: `ApplyDeadZone(dz)` then `Circular?ApplyCircularClamp:
ApplySquareClamp` — with FNA's exact "dead zones before clamp" ordering). `ApplyDeadZone` (None no-op /
IndependentAxes per-axis `ExcludeAxisDeadZone` with LeftDeadZone/RightDeadZone / Circular
`ExcludeCircularDeadZone`), `ApplySquareClamp` (clamp each component to [-1,1]), `ApplyCircularClamp`
(`if LengthSquared>1 Normalize`), and `ExcludeCircularDeadZone` (`if length<=deadZone → Zero; else scale by
(length-deadZone)/(1-deadZone)/length`) are **line-for-line identical**. `GetHashCode` = `Left + 37*Right`
(FNA `GamePadThumbSticks.cs:186`), with unsigned wraparound to avoid signed-overflow UB (INPUT-BUILD-006 —
same numeric result). **Files changed:** none (logic verified). **Behavior verified:** all three dead-zone
modes + clamp + circular scaling + hash match FNA exactly. **Remaining risk:** none.

## L-007 — `GamePadTriggers.cpp` logic `[x]`
- [x] Clamp, trigger-threshold dead-zone, WithinEpsilon equality, bit-hash vs FNA.

**Result (2026-07-06):** **Byte-identical to FNA `GamePadTriggers.cs`** — no fix. Public 2-arg ctor clamps
`[0,1]` (`MathHelper.Clamp`); internal 3-arg ctor is line-for-line FNA: `None → Clamp`; else
`Clamp(ExcludeAxisDeadZone(trigger, TriggerThreshold), 0, 1)` for both triggers (FNA's "dead zones before
clamp"). `Equals` = `WithinEpsilon(left)` && `WithinEpsilon(right)` — FNA-faithful (FNA `operator==` uses
`MathHelper.WithinEpsilon`, A4-004). `GetHashCode` = `Single::GetHashCode(left_) + Single::GetHashCode(right_)`
≡ FNA `Left.GetHashCode() + Right.GetHashCode()` (float bit-hash sum), unsigned wraparound to avoid UB (same
result). **Files changed:** none (logic verified). **Behavior verified:** clamp + trigger-threshold dead-zone
+ epsilon equality + float-hash sum all match FNA. **Remaining risk:** none.

## L-008 — `GamePadState.cpp` logic `[x]`
- [x] Ctor button/trigger/thumbstick→Buttons packing (StickToButtons/TriggerToButton thresholds),
  IsButtonDown/Up bit ops, equality incl. packet number, hash vs FNA.

**Result (2026-07-06):** **Ctor packing + equality byte-identical to FNA `GamePadState.cs`** — no fix.
The 4-arg ctor ORs `LeftTrigger`/`RightTrigger` into `buttons` when `triggers.Left/Right > TriggerThreshold`
(strict `>`, FNA order), then ORs `StickToButtons` for left+right sticks — **line-for-line FNA**.
`StickToButtons` maps `X > dz → right`, `X < -dz → left`, `Y > dz → up`, `Y < -dz → down` — **byte-identical
to FNA** (no axis swap; strict inequalities). `IsButtonDown` = `(buttons & b)==b`, `IsButtonUp` = `!= b` (≡
FNA, A4-006). **`Equals` includes `PacketNumber`** — verified FNA's `operator==` also compares
`IsConnected & PacketNumber & Buttons & DPad & ThumbSticks & Triggers`, so CNA is **FNA-faithful**
(EqualityConsidersPacketNumber is *not* a deviation). `GetHashCode` = `buttons.GetHashCode() ^ (packetNumber
*31)` — a **deterministic field-based choice** (documented) vs FNA's `base.GetHashCode()`. `ToString` = type
name (≡ FNA `base.ToString()`). **Files changed:** none (logic verified). **Behavior verified:** the full
button-packing pipeline + equality-with-packet match FNA exactly. **Remaining risk:** none.

## L-009 — `GamePad.cpp` logic `[x]`
- [x] GetState dead-zone application, GetCapabilities assembly, SetVibration/EXT forwarding vs FNA policy.

**Result (2026-07-06):** Logic verified vs FNA `GamePad.cs` — **faithful, no fix.** `ExcludeAxisDeadZone` is
**byte-identical** to FNA (`if v<-dz: v+=dz; elif v>dz: v-=dz; else return 0; return v/(1-dz)`), and the
dead-zone constants are **byte-identical**: `LeftDeadZone = 7849/32768`, `RightDeadZone = 8689/32768`,
`TriggerThreshold = 30/255`. `GetState(playerIndex)` defaults to `IndependentAxes` (≡ FNA). The 2-arg
`GetState` reads the accumulated raw state (event-driven vs FNA's poll — documented architectural deviation),
returns a disconnected default when not connected, and assembles `GamePadThumbSticks`/`GamePadTriggers`
(with the dead-zone) + `GamePadButtons` + `GamePadDPad::FromButtonArray` then sets the packet number — the
dead-zone/packing math lives in the sub-struct ctors already verified byte-identical (L-006/L-007/L-008).
`GetCapabilities` and all EXT ops (`SetVibration`/`SetLightBarEXT`/`SetTriggerVibrationEXT`/`GetGUIDEXT`/
`GetGyroEXT`/`GetAccelerometerEXT`) thin-forward to `SdlInputBridge` (≡ FNA forwarding to `FNAPlatform`).
**Files changed:** none (logic verified). **Behavior verified:** axis dead-zone rescale + constants + default
mode + assembly + forwarding. **Remaining risk:** none.

---

**GamePad logic subtotal (L-005..L-009):** every GamePad `.cpp` (Buttons, DPad, ThumbSticks, Triggers, State,
GamePad) is **byte-identical to FNA** in its core math (flag extraction, dead-zone rescale + constants,
StickToButtons packing, hashes) — the only deviations are the documented deterministic `GetHashCode` choices
(vs FNA's `base.GetHashCode()`) and the event-driven-vs-poll architecture.

## L-010 — `TouchCollection.cpp` logic `[x]`
- [x] Indexer/CopyTo/IndexOf/Contains/mutators over the vector; advisory IsReadOnly; FindById out-semantics.

**Result (2026-07-06):** Logic verified vs FNA `TouchCollection.cs` — **faithful with the documented
C++-container deviations only; no new fix.** `Count`=size; `IsConnected`=touch-device-present;
`IsReadOnly`=`true` but **advisory** (P5-001 — the mutators actually mutate, matching FNA whose `IsReadOnly`
getter is hard-coded true yet whose Add/Clear/Insert/Remove/RemoveAt mutate the backing list). `operator[]`
(const+mutable), `RemoveAt`, `Insert` bounds-check → `std::out_of_range` (maps FNA's
`ArgumentOutOfRangeException`). `Contains`/`IndexOf`/`Remove` are linear scans with `==` (≡ FNA List ops).
`FindById(id, out)` → `TouchLocation&` out-ref (≡ FNA). `CopyTo` **inserts** at `arrayIndex` (documented
P5-002 deviation: growable `std::vector` vs FNA's fixed-array overwrite) with an index guard (bad index →
`out_of_range`). `Add`/`Clear` = push_back/clear. `begin`/`end` NOXNA iterators replace FNA's `GetEnumerator`.
The one unavoidable deviation: CNA's default collection is an **empty mutable vector** vs FNA's null-backed
list (which throws `NullReferenceException` on mutate) — P5-001, documented. **Files changed:** none (logic
verified). **Behavior verified:** all members' logic matches FNA modulo the documented container deviations.
**Remaining risk:** none.

## L-011 — `TouchLocation.cpp` logic `[x]`
- [x] TryGetPreviousLocation both paths, Equals (all 5 fields), Id+Position hash, ToString format vs FNA.

**Result (2026-07-06):** **Byte-identical to FNA `TouchLocation.cs`** — no fix. Ctors: default (Invalid),
3-arg (prev = Invalid/Zero), 5-arg (stores prev). `TryGetPreviousLocation` writes the out-param on **every**
path (`TouchLocation(id_, prevState_, prevPosition_)`) then returns `prevState_ != Invalid` — semantically
identical to FNA (which returns `previousLocation.State != Invalid`, and `previousLocation.State == prevState`
by construction); DEC-12. `Equals` compares **all 5 fields** (id/position/state/prevPosition/prevState) ≡ FNA
`TouchLocation.cs:80-86`. `GetHashCode` = `id_ + position_.GetHashCode()` ≡ FNA `Id.GetHashCode() +
Position.GetHashCode()` (int hash = the int). `ToString` = `"{Position:" + position_.ToString() + "}"` ≡ FNA.
**Files changed:** none (logic verified). **Behavior verified:** previous-location both paths + 5-field
equality + hash + ToString all match FNA. **Remaining risk:** none.

## L-012 — `TouchPanel.cpp` logic `[x]`
- [x] GetState slot vs InputManager path + MAX_TOUCHES cap, GetCapabilities, SetFinger release/press
  branches, coordinate scaling, EnqueueGesture/ReadGesture queue, Update ordering vs FNA.

**Result (2026-07-06):** Logic verified vs FNA `TouchPanel.cs` — **faithful with documented deviations only.**
`ReadGesture` = throw `InvalidOperationException` on empty, else `front()`+`pop()` (FIFO) ≡ FNA (`gestures[0]`
+ `RemoveAt(0)`). `EnqueueGesture` = queue push ≡ FNA `Enqueue`. `INTERNAL_onTouchEvent` **coordinate scaling
is byte-identical to FNA**: `touchPos = round(x*DisplayWidth), round(y*DisplayHeight)`, `delta =
round(dx*DisplayWidth), round(dy*DisplayHeight)`, then Pressed→`OnPressed`, Moved→`OnMoved(delta)`,
Released→`OnReleased` — plus CNA's zero-display early-return guard (P5-014 startup safety, absent from FNA).
`SetFinger` release (`NO_FINGER`: prev `!=Invalid && !=Released` → Released, else Invalid) and press/move
(prev `==Invalid` → Pressed, else Moved) branches are byte-identical to FNA `TouchPanel.cs:165-217` (P5-007).
`GetState` uses the slot-array path (mirrors FNA's `touches[]` iteration) then the InputManager fallback with
the `MAX_TOUCHES` cap (DEC-10, P5-006). `GetCapabilities` reports `MaximumTouchCount=4` (DEC-09).
`Update` copies current→previous before the gesture update (DEC-13, reverse of FNA but inert). **Files
changed:** none (logic verified). **Behavior verified:** gesture queue FIFO + FNA coordinate scaling +
SetFinger branches + dual GetState. **Remaining risk:** none.

## L-013 — `GestureDetector.cpp` logic `[x]`
- [x] The full gesture state machine vs FNA `GestureDetector.cs`: every OnPressed/OnMoved/OnReleased/OnUpdate
  branch, thresholds, velocity low-pass, pinch/drag/tap/hold/flick transitions. Deepest logic file.

**Result (2026-07-06):** Compared vs FNA `GestureDetector.cs` — CNA is a **faithful port of FNA's gesture
state machine** (not merely behavior-equivalent): FNA also uses a `GestureState` enum (HOLDING/HELD/…), which
CNA mirrors. **Every constant is byte-identical to FNA:** `MOVE_THRESHOLD = 35` (FNA:74), `MIN_FLICK_VELOCITY
= 100` (FNA:80), Hold fires at `timeSincePress >= FromSeconds(1)` (FNA:521), Tap requires `timeHeld <
FromSeconds(1)` (FNA:212), double-tap window `<= FromMilliseconds(300)` (FNA:146) with distance `<=
MOVE_THRESHOLD` (FNA:150). **The flick velocity low-pass is byte-identical:** `instVelocity = delta /
(0.001f + dt); velocity += (instVelocity - velocity) * 0.45f` (FNA:506-507 ≡ CNA:406-409); flick gate
`velocity.Length() >= MIN_FLICK_VELOCITY` (FNA:253). The drag-axis classification (`ax>ay`→H, `ay>ax`→V,
else Free), pinch promotion, and DragComplete/PinchComplete transitions match FNA and are pinned by the
**35-test** `GestureDetectorTest` suite (every gesture + threshold + negative path, Phase 6). **One
documented difference:** the clock source — `std::chrono::steady_clock` + an injectable test clock
(`EnableTestClock`/`AdvanceTestClockMilliseconds`, using `TimePoint{}` as FNA's `DateTime.MinValue`
"no prior update" sentinel) vs FNA's `DateTime.UtcNow` — behaviorally equivalent (no test hooks in FNA).
**Files changed:** none (logic verified). **Behavior verified:** the full state machine + byte-identical
constants + flick filter match FNA. **Remaining risk:** none.

## L-014 — `InputManager.cpp` logic `[ ]`
- [ ] Accumulated-state mutation/read for each subsystem; touch previous-location + Pressed→Moved promotion
  + RemoveAfterSnapshot; packet-number bump rules; reset fan-out determinism.

## L-015 — `SdlInputBridge.cpp` logic `[ ]`
- [ ] Every ProcessEvent case's translation vs FNA `SDL3_FNAPlatform.cs`: axis normalization/Y-inversion,
  button/key maps, UTF-8 decode, control-char + Ctrl+V, finger-id mapping, coordinate scaling, gamepad
  slot lifecycle. Largest bridge logic.

## L-016 — `TextInputEXT.cpp` / `SdlGamepadBackend.cpp` logic `[ ]`
- [ ] TextInputEXT window-guarded SDL calls + INTERNAL dispatch; SdlGamepadBackend real SDL wrappers.

## L-017 — Final logic-audit statement `[ ]`
- [ ] Summarize logic divergences found (fixed vs documented `DEC-*`), re-run all gates + ASan, record.

---

## Notes carried forward (do not lose)

- Documented intentional deviations live in `docs/input-fna-fidelity.md` (`DEC-06..DEC-20` + named entries).
- Manual/hardware validation (real keyboards/mice/gamepads/touchscreen/IME, high-DPI) is **out of automated
  scope** — it stays in `docs/devices-hardware-checklist.md` / `docs/demo-input-checklist.md` and is marked
  `[!]` (see the prior plan's Phase 11 in git history).
- Public XNA API is frozen (`docs/input-public-api-frozen.md`); the parity matrix
  (`docs/input-member-parity-matrix.md`) is the member-level source of truth.
