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

## A3-001 — `MouseState` (struct) `[ ]`
- [ ] FNA `Input/MouseState.cs`; CNA `MouseState.hpp`/`.cpp`; test `MouseInputTests.cpp`.
- [ ] Members: ctor, X/Y, ScrollWheelValue, 5 button props, `Equals`/`==`/`!=`, `GetHashCode`. Per-member.

## A3-002 — `Mouse` (static class) `[ ]`
- [ ] FNA `Input/Mouse.cs`; CNA `Mouse.hpp`/`.cpp`; tests `MouseInputTests.cpp` + bridge.
- [ ] Members: `GetState`, `SetPosition`, `WindowHandle` get/set, relative-mode EXT, `ClickedEXT`. Per-member.

## A3-003 — `MouseCursor` (class, NOXNA) `[ ]`
- [ ] FNA `Input/Mouse.cs` (MouseCursor is CNA-specific — verify against XNA MouseCursor semantics);
  CNA `MouseCursor.hpp`/`.cpp`; test in `MouseInputTests.cpp`.
- [ ] Members: stock cursor statics, `FromTexture2D`, `Dispose`, move-ctor, handle. Per-member (Xvfb-gated).

---

# Phase 4 — GamePad types

## A4-001 — `GamePadButtons` (struct) `[ ]`
- [ ] FNA `Input/GamePadButtons.cs`; CNA `GamePadButtons.hpp`/`.cpp`; test `GamePadButtonsTests.cpp`.
- [ ] Members: ctors, per-button props, EXT props, `Equals`/`==`/`!=`, `GetHashCode`. Per-member.

## A4-002 — `GamePadDPad` (struct) `[ ]`
- [ ] FNA `Input/GamePadDPad.cs`; CNA `GamePadDPad.hpp`/`.cpp`; test (GamePadState/mapping suites).
- [ ] Members: ctors, Up/Down/Left/Right, `Equals`/`==`/`!=`, `GetHashCode`. Per-member.

## A4-003 — `GamePadThumbSticks` (struct) `[ ]`
- [ ] FNA `Input/GamePadThumbSticks.cs`; CNA `GamePadThumbSticks.hpp`/`.cpp`; test `GamePadThumbSticksTests.cpp`.
- [ ] Members: ctors, Left/Right, dead-zone application, `Equals`/`==`/`!=`, `GetHashCode`. Per-member.

## A4-004 — `GamePadTriggers` (struct) `[ ]`
- [ ] FNA `Input/GamePadTriggers.cs`; CNA `GamePadTriggers.hpp`/`.cpp`; test `GamePadTriggersTests.cpp`.
- [ ] Members: ctors, Left/Right clamp, `Equals`/`==`/`!=`, `GetHashCode`. Per-member.

## A4-005 — `GamePadCapabilities` (struct) `[ ]`
- [ ] FNA `Input/GamePadCapabilities.cs`; CNA `GamePadCapabilities.hpp`/`.cpp`; test (GamePad suites).
- [ ] Members (~73 decls): IsConnected, GamePadType, every `Has*` property (XNA + EXT), `Equals`/`==`/`!=`,
  `GetHashCode`. Per-member — this is the largest surface.

## A4-006 — `GamePadState` (struct) `[ ]`
- [ ] FNA `Input/GamePadState.cs`; CNA `GamePadState.hpp`/`.cpp`; test `GamePadStateTests.cpp`.
- [ ] Members: ctors (default + full), IsConnected, PacketNumber, Buttons/DPad/ThumbSticks/Triggers,
  `IsButtonDown`/`IsButtonUp`, `Equals`/`==`/`!=`, `GetHashCode`. Per-member.

## A4-007 — `GamePad` (static class) `[ ]`
- [ ] FNA `Input/GamePad.cs`; CNA `GamePad.hpp`/`.cpp`; tests `GamePadTests.cpp` + `GamePadInputTests.cpp`.
- [ ] Members: `GetState` (×overloads incl. dead-zone), `GetCapabilities`, `SetVibration`, EXT
  (`GetGUIDEXT`/`SetLightBarEXT`/`SetTriggerVibrationEXT`/`GetGyroEXT`/`GetAccelerometerEXT`). Per-member.

---

# Phase 5 — Touch types

## A5-001 — `TouchLocation` (struct) `[ ]`
- [ ] FNA `Input/Touch/TouchLocation.cs`; CNA `Touch/TouchLocation.hpp`/`.cpp`; test `TouchInputTests.cpp`.
- [ ] Members: ctors (3-arg/5-arg/default), Id/State/Position, `TryGetPreviousLocation`, `Equals`/`==`/`!=`,
  `GetHashCode`, `ToString`. Per-member.

## A5-002 — `TouchCollection` (struct) `[ ]`
- [ ] FNA `Input/Touch/TouchCollection.cs`; CNA `Touch/TouchCollection.hpp`/`.cpp`; test `TouchInputTests.cpp`.
- [ ] Members: ctors, Count/IsConnected/IsReadOnly, `operator[]` (const+mutable), Contains, FindById, CopyTo,
  IndexOf, Add/Clear/Remove/RemoveAt/Insert, begin/end, empty. Per-member.

## A5-003 — `TouchPanelCapabilities` (struct) `[ ]`
- [ ] FNA `Input/Touch/TouchPanelCapabilities.cs`; CNA `Touch/TouchPanelCapabilities.hpp`/`.cpp`; test
  (`TouchInputTests.cpp` / `TouchEdgeCaseTests.cpp`).
- [ ] Members: ctors, IsConnected, MaximumTouchCount. Per-member.

## A5-004 — `GestureSample` (struct) `[ ]`
- [ ] FNA `Input/Touch/GestureSample.cs`; CNA `Touch/GestureSample.hpp`/`.cpp`; test `TouchInputTests.cpp`.
- [ ] Members: ctors (public + internal-equiv), GestureType, Timestamp, Position/Position2, Delta/Delta2,
  FingerId/FingerId2 EXT. Per-member.

## A5-005 — `TouchPanel` (static class) `[ ]`
- [ ] FNA `Input/Touch/TouchPanel.cs`; CNA `Touch/TouchPanel.hpp`/`.cpp`; tests `TouchInputTests.cpp` +
  `TouchEdgeCaseTests.cpp` + bridge.
- [ ] Members: `GetState`, `GetCapabilities`, EnabledGestures get/set, IsGestureAvailable, ReadGesture,
  DisplayWidth/Height/Orientation, WindowHandle, EnqueueGesture/SetFinger/Update/INTERNAL_onTouchEvent
  (NOXNA), reset. Per-member.

---

# Phase 6 — Text input

## A6-001 — `TextInputEXT` (static class, NOXNA) `[ ]`
- [ ] FNA `Input/TextInputEXT.cs`; CNA `TextInputEXT.hpp`/`.cpp`; tests `TextInputEXTTests.cpp` + bridge.
- [ ] Members: `TextInput`/`TextEditing` multicast events, WindowHandle, IsTextInputActive,
  IsScreenKeyboardShown (×overloads), StartTextInput/StopTextInput, SetInputRectangle,
  INTERNAL_OnTextInput/OnTextEditing, reset. Per-member.

---

# Phase 7 — Internal classes (`CNA::Internal::Input`)

## A7-001 — `InputManager` `[ ]`
- [ ] CNA `InputManager.hpp`/`.cpp`; tests `InputResetTests.cpp` + subsystem suites.
- [ ] Members: every `Set*`/`Get*State` accessor (keyboard/mouse/gamepad/touch), reset entry points.
  Verify each mutates/reads the accumulated singleton correctly; per-member test.

## A7-002 — `GestureDetector` `[ ]`
- [ ] FNA `Input/Touch/GestureDetector.cs`; CNA `GestureDetector.hpp`/`.cpp`; test `GestureDetectorTests.cpp`.
- [ ] Members: OnPressed/OnMoved/OnReleased/OnUpdate, test-clock hooks, reset. Verify the gesture state
  machine + thresholds vs FNA; per-member/behavior test.

## A7-003 — `SdlGamepadBackend` / `ISdlGamepadBackend` `[ ]`
- [ ] CNA `SdlGamepadBackend.hpp`/`.cpp`; test `SdlGamepadBackendTests.cpp` + `FakeSdlGamepadBackend.hpp`.
- [ ] Members: the `ISdlGamepadBackend` seam methods (open/close/rumble/led/sensor/…) + real impl. Verify each
  is exercised via the fake; seam never leaks into the XNA layer.

## A7-004 — `SdlInputBridge` `[ ]`
- [ ] CNA `SdlInputBridge.hpp`/`.cpp`; tests: all `SdlInputBridge*` + golden + fuzz.
- [ ] Members: `ProcessEvent` (every SDL case), init/reset, window resolution, UTF-8 decode, id maps, test
  hooks. Verify each event case + helper; per-case test.

---

# Phase 8 — Cross-cutting final gates

## A8-001 — Regenerate member parity matrix `[ ]`
- [ ] Re-run `tools/input_parity/gen_input_parity_matrix.py`; confirm 0 STRICT/EXT gaps, 0 FNA-only; commit.

## A8-002 — Regenerate test-coverage document `[ ]`
- [ ] Re-run `tools/input_parity/check_input_test_coverage.py`; confirm every type has a dedicated suite;
  update `docs/input-test-coverage.md`.

## A8-003 — Re-verify signature + enum freeze `[ ]`
- [ ] Confirm `PublicApiInputSignatureFreezeTests` + `PublicApiInputCompileTests` + enum-value suites still
  green; confirm no public API drift from the audit.

## A8-004 — Full input suite + backends + sanitizer `[ ]`
- [ ] `ctest -L input` green on EasyGL/Vulkan/bgfx/SDL_RENDERER; ASan+UBSan-clean; record counts.

## A8-005 — Final perfection statement `[ ]`
- [ ] Write a final status: per-type member coverage, tests-per-member confirmation, deviations, manual-HW
  status (`[!]`), remaining risks. Do not overstate. Mark this plan complete only when all non-blocked tasks
  are done.

---

## Notes carried forward (do not lose)

- Documented intentional deviations live in `docs/input-fna-fidelity.md` (`DEC-06..DEC-20` + named entries).
- Manual/hardware validation (real keyboards/mice/gamepads/touchscreen/IME, high-DPI) is **out of automated
  scope** — it stays in `docs/devices-hardware-checklist.md` / `docs/demo-input-checklist.md` and is marked
  `[!]` (see the prior plan's Phase 11 in git history).
- Public XNA API is frozen (`docs/input-public-api-frozen.md`); the parity matrix
  (`docs/input-member-parity-matrix.md`) is the member-level source of truth.
