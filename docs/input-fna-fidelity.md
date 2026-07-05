<!-- SPDX-License-Identifier: MS-PL -->
# CNA Input — FNA Fidelity Notes

CNA's `Input` namespace uses **FNA** (`Microsoft.Xna.Framework.Input` + its SDL platform layer) as
the authoritative behavioral reference. This document records, per input area: (a) behavior that
matches FNA, (b) **intentional** CNA deviations (with the reason), and (c) known **gaps / TODO**.
It reflects the Phase I13/I14 code-vs-FNA audit (tasks 954–956).

CNA is partially derived from / behaviorally based on FNA (see the repository's attribution/licensing
notes); those notices are intact and must not be removed.

## Architecture note (task 953)

FNA is **poll-based**: each frame it re-reads the full device state from SDL
(`SDL_GetKeyboardState`, `SDL_GetGamepad*`, `SDL_GetTouchFingers`, …). CNA is **event-driven**:
`SdlInputBridge::ProcessEvent` translates each SDL event and mutates `InputManager`'s accumulated
state; `Get*State()` reads that snapshot. Input state is therefore updated **at the moment each SDL
event is processed** (during the host's event pump), not sampled once per frame. The two models are
behaviorally equivalent for well-formed SDL event streams; the differences below are where they are
not exactly identical.

---

## Keyboard

| Aspect | Status |
|---|---|
| `Keys` enum numeric values | **Matches FNA exactly** (all values, incl. hex outliers `Pause`=0x13, `Kana`=0x15, `ChatPadGreen`=0xCA, `OemCopy`=0xf2, …). |
| SDL keycode/scancode → `Keys` maps | **Faithful port** of FNA `INTERNAL_keyMap`/`INTERNAL_scanMap`. |
| `NONUSHASH`/`NONUSBACKSLASH` → `Keys::None` | Inherited FNA `FIXME`; ISO-layout keys with no XNA equivalent. Stays in lock-step with FNA. |
| `GetPressedKeys()` ordering, `GetHashCode`, `this[Keys]` | Matches FNA (ascending numeric order; 8×32-bit XOR hash). |
| Repeated key-down while already down | State de-dupes (`unordered_set`) — matches FNA. |
| Key-up without prior key-down | No-op — matches FNA. |
| Focus-loss keyboard reset | **Neither FNA nor CNA clears keys on focus loss** (DEC-15: match FNA, accepted). See "SDL bridge" below for the event-driven consequence. |

**Intentional deviations:**
- **DEC-16 (accepted):** unmapped keycodes (`'é'`, `SDLK_UNKNOWN`) are **dropped** rather than pushed as
  `Keys::None`. FNA's `ToXNAKey` returns `Keys.None` and then does `Keyboard.keys.Add(Keys.None)`
  (`SDL3_FNAPlatform.cs:905-908`), leaving a meaningless "None" key marked pressed; CNA's drop is cleaner.
  Tested (`UnmappedKeycodeIsDroppedNotMarkedNone`).
- **DEC-17 (accepted):** `SDLK_AC_BACK` → `Keys::Escape` (Android/browser Back button) — a CNA-only
  convenience not in FNA, so "back" acts as cancel/exit on those platforms. Tested
  (`AndroidBackButtonMapsToEscape`).
- **DEC-19 (accepted — matches FNA):** text-synthesis on key-down re-emits control chars gated on SDL's
  `repeat` flag — **the same gate FNA uses** (`else if (evt.key.repeat)`, `SDL3_FNAPlatform.cs:923`), so
  this is not a deviation. Tested (`KeyRepeatReemitsControlCharacter`).

---

## Mouse

| Aspect | Status |
|---|---|
| Button → `ClickedEXT` index (`button-1`, down-only) | **Matches FNA exactly.** |
| `SetPosition` relative-mode early-return | **Matches FNA.** |
| `SetPosition` with null window | Safe no-op warp; caches requested position. |
| Wheel `×120` scaling | **Matches FNA** (cast-to-int **before** ×120 — whole notches only; task 927). |

**Intentional deviations:**
- **Wheel:** fixed in Phase I13/I14 to truncate the SDL float to a whole notch before scaling, so
  `ScrollWheelValue` stays a clean multiple of 120 exactly like XNA. (Previously multiply-then-cast
  leaked sub-notch precision-wheel motion.)
- **DEC-18 (accepted):** SDL's horizontal wheel (`wheel.x`) is **ignored** — XNA/FNA `MouseState` exposes
  only the vertical `ScrollWheelValue`, so there is no property to route horizontal scroll to. Tested
  (`HorizontalWheelIsIgnored`).
- **Logical→window scaling:** CNA converts logical→window at `SetPosition` time via the graphics
  backend (`TransformLogicalToWindow` / `SDL_RenderCoordinatesToWindow`); FNA scales at `GetState`
  read time. Equivalent for the common case (see `plan.md` a-0001).
- **`ClickedEXT` is multicast (DEC-06, fixed 2026-07-05):** now a `System::MulticastAction<int>` matching
  FNA's `public static Action<int>` — `+=` adds subscribers, `=` replaces, `= nullptr` clears. (Was a
  single `std::function`; the second-subscriber-lost gap is closed.)
- **Relative-mode cache (DEC-14, accepted 2026-07-05):** the public
  `Mouse::getIsRelativeMouseModeEXTProperty()` reads SDL **live** (`SDL_GetWindowRelativeMouseMode`),
  matching FNA — there is no cache at the API boundary. Separately, the SDL-agnostic `InputManager` keeps
  its own `RelativeMode` flag to gate relative-delta accumulation/draining in `GetMouseState`; because
  `InputManager` must not depend on SDL, that flag is written by `Mouse::setIsRelativeMouseModeEXTProperty`
  (which updates SDL **and** `InputManager` together) rather than read from SDL. It cannot diverge through
  CNA's own API; it would only desync if a caller toggled SDL relative mode *directly*, bypassing the CNA
  setter — which is out of contract. Accepted as-is (having `InputManager` read SDL would break the
  input-layer/SDL boundary). The live round-trip and the delta behaviour are both tested.

---

## GamePad

| Aspect | Status |
|---|---|
| Dead-zone constants + math (independent/circular/none) | **Matches FNA exactly** (`7849`/`8689`/`30`). |
| Thumbstick Y-sign, trigger normalization | Matches FNA (`/-axis`, `/32767`). |
| SDL button → `Buttons` mapping (all 21, incl. paddles/touchpad/guide) | **Matches FNA exactly.** |
| Duplicate add / unknown remove / no-free-slot | Safe; duplicate-add is **safer** than FNA (no leak). |
| `SetVibration`/`SetTriggerVibration`/`SetLightBar`/`GetGyro`/`GetAccelerometer`/`GetGUID` | **Faithful ports.** |
| `ToString()` | Matches FNA (type name). |

**Real bugs fixed in Phase I13/I14:**
- **`SDL_INIT_GAMEPAD` was never initialized** → no gamepad events were ever delivered. The gamepad
  subsystem is now initialized (idempotently, with the background-events hint) via
  `SdlInputBridge::EnsureGamepadSubsystemInitialized()`, so hot-plugged **and** already-connected pads
  become visible (tasks 907/908).
  - **Startup invariant:** `Game::DoInitialize()` calls it explicitly **once**, right after the
    graphics device is created and **before the first event pump and the first `Update()`** (both
    `Game::Run()` and `Game::RunOneFrame()` funnel through `DoInitialize()`). So a pad connected
    before frame one is enumerated by SDL from the start. `SdlInputBridge::ProcessEvent()` also calls
    it lazily as a **defensive fallback** for any host that pumps SDL events without Game's loop.
- **`GetCapabilities` cancelled active rumble**: it probed rumble support with
  `SDL_RumbleGamepad(0,0,0)` (which *stops* vibration). Now it reads non-mutating capability
  properties (`SDL_PROP_GAMEPAD_CAP_*`), so reading capabilities no longer cancels a game's
  `SetVibration` (task 922).

**Intentional / documented deviations:**
- `FNA_GAMEPAD_NUM_GAMEPADS` is clamped to **4** because `PlayerIndex` is the frozen XNA enum
  (One–Four); FNA leaves it unclamped. Behavior matches FNA for every usable value 0–4.
- **`PacketNumber`** increments on raw per-field changes (event-driven) rather than FNA's
  once-per-poll-on-processed-change. Honors the XNA contract (equal number ⟹ no visible change) for
  connect/button/coarse-axis; a raw axis wobble entirely within the dead-zone can still bump it.
- `GamePadState::GetHashCode()` hashes `buttons ^ packetNumber*31` (consistent for equal states) vs
  FNA's reflection-based `ValueType.GetHashCode()`. Deliberate; other sub-structs match FNA.
- **DEC-20 (accepted):** `GetGUID`/`GetCapabilities` are computed **live** each call rather than cached at
  connect like FNA. Same values for a connected controller (a timing/impl detail, not a behavioral gap);
  `GetCapabilities` also deliberately avoids the zero-magnitude rumble probe FNA's cache path implies
  (would cancel active vibration — INPUT-GAMEPAD-012).

**Fake-SDL unit coverage (Phase I15 — no real hardware):** an internal injectable seam
(`ISdlGamepadBackend`, production = real SDL) lets a `FakeSdlGamepadBackend` drive the real
`SdlInputBridge` event path in tests. Now headless-tested: pre-connected visibility, duplicate add
(no leak/second slot), unknown remove, remove-closes-correct-handle + disconnect, `>4`
no-free-slot, `FNA_GAMEPAD_NUM_GAMEPADS` parsing + slot limits, **all 21** `SDL_GamepadButton`
mappings, axis Y-inversion + trigger normalization, connected/disconnected capabilities,
rumble/trigger/LED support true/false, **reading capabilities not cancelling active rumble**,
gyro/accel present/absent + read + graceful failure, and GUID formatting (xinput /
vendor+product little-endian / Valve overrides).

**Remaining gaps (real hardware / manual only — NOT a code gap):** the fake proves CNA's
*translation and bookkeeping* are correct; it cannot prove the *physical device acts* — an actual
rumble motor spinning, real trigger haptics, a real sensor's live values, or genuine OS hot-plug /
per-controller GUID. Those stay in `docs/input-manual-verification-results.md`, kept **separate**
from the fake-backend unit tests above.

---

## TouchPanel / TouchCollection / TouchLocation

| Aspect | Status |
|---|---|
| `GetState` previous-location (Pressed/Moved/Released) | Matches FNA (Phase I12). |
| `TouchLocation` `Equals`/`GetHashCode`/`==`/`!=` | **Matches FNA.** |
| `SDL_EVENT_FINGER_CANCELED` | **Fixed (task 892):** now released like `FINGER_UP` (was unhandled → stuck touch). |
| `GetCapabilities()` side effects | **Fixed (task 894):** now uses non-mutating `InputManager::HasAnyTouch()`; no longer consumes a touch frame. |
| `TouchCollection::CopyTo` | **Fixed (task 902):** out-of-range index now throws `std::out_of_range` (was UB). |
| Empty/default semantics, out-of-range indexer, `IsReadOnly=true` | Equivalent to FNA (empty vector replaces null sentinel; `out_of_range` for bad index). |

**Intentional / documented deviations:**
- **Touch IDs** are a compact sequential counter (1,2,3,…) rather than FNA's cast SDL finger id. IDs
  are opaque to games. Overflow only after ~2³¹ distinct fingers in one session (theoretical).
- **`MAX_TOUCHES` / `MaximumTouchCount` (DEC-09 + DEC-10, fixed 2026-07-05):** now matches FNA on both
  counts. `GetCapabilities` reports `MaximumTouchCount = 4` (FNA: "MaximumTouchCount is completely bogus;
  for any touch device, XNA always reports 4", `SDL3_FNAPlatform.cs`), `0` when disconnected — this is a
  fixed XNA-compat value, NOT the tracking cap. `TouchPanel::GetState()` caps the public snapshot at
  `MAX_TOUCHES (8)`, matching FNA's fixed `TouchLocation[MAX_TOUCHES]` array (the event-driven
  `InputManager` map is internally unbounded, but the public state never exceeds 8).
- `TouchPanel::Update()` copies current→previous **before** the gesture update; FNA does gesture update
  first (`TouchPanel.cs:219`). **Confirmed inert (DEC-13, 2026-07-05):** `GestureDetector::OnUpdate()`
  operates only on the gesture detector's own state and never reads or writes `touches_`/`previousTouches_`,
  so the two statements touch disjoint state and the ordering is unobservable. Pinned by
  `TouchEdgeCaseTest.UpdatePropagatesTouchesToPreviousForSlotPathContinuity`.
- `TryGetPreviousLocation` now writes the out-param on **every** path (DEC-12, fixed 2026-07-05): it
  assigns `TouchLocation(Id, prevState, prevPosition)` and returns `prevState != Invalid`, matching FNA
  exactly (on the `false` path the out-param is the Invalid previous location, not left untouched).

---

## Gestures

`GestureDetector` reproduces FNA's tap / double-tap / hold / drag / flick / pinch state machine.
Covered by `GestureDetectorTests` and the end-to-end `SdlInputBridgeTouchGestureTests` (including the
new `FINGER_CANCELED` release path). Broader parameterized regression coverage across every gesture
type + interruption is partial (task 906).

---

## TextInputEXT / TextEditing

| Aspect | Status |
|---|---|
| `StartTextInput`/`StopTextInput`/`SetInputRectangle`/active-window | **Faithful ports** (+ null-window guards). |
| UTF-8 → UTF-16 decode (BMP + astral surrogate pairs) | **Matches FNA** (`Encoding.UTF8.GetChars` equivalent). Exhaustively tested. |
| `TextInput` code-unit type | `charcs`/UTF-16 code unit, matching FNA's `Action<char>` (Phase I9 task 806). |

**Intentional / documented deviations:**
- **Multicast callbacks (DEC-06, fixed 2026-07-05):** `TextInput`/`TextEditing` are now
  `System::MulticastAction<...>` matching FNA's `event Action<...>` — `+=` adds subscribers, `=` replaces,
  `= nullptr` clears. (Were single `std::function`s; the second-subscriber-lost gap is closed.)
- **`TextEditing` string is UTF-8** (`std::string`) vs FNA's decoded UTF-16 string; `start`/`length`
  index bytes vs UTF-16 units. Documented.
- **Malformed UTF-8 emits U+FFFD (DEC-08, fixed 2026-07-05):** `decode_utf8_to_utf16` now substitutes
  U+FFFD for an invalid lead byte, an ill-formed sequence (one per maximal subpart, resyncing to the next
  valid text), an overlong encoding, a UTF-16 surrogate code point, or an out-of-range code point —
  matching FNA's `Encoding.UTF8` replacement fallback. (Was silently skipped; unreachable via SDL, which
  guarantees well-formed UTF-8, but now FNA-faithful.)
- Empty composition emits `("", 0, 0)` vs FNA's `(null, 0, 0)` (`std::string` cannot be null).

---

## SDL bridge robustness

- `ProcessEvent` switches over all supported event types with a safe `default: break;` — unknown
  events are ignored (task 949). Unmapped keys/buttons/axes are dropped without side effects.
- Window handles are derived defensively (`SDL_GetWindowFromID` → `SDL_GetMouseFocus()` fallback);
  `nullptr` windows are handled everywhere (task 950).
- **Focus loss (task 951 / DEC-15) — DECISION: match FNA, no clear (accepted 2026-07-05).** On
  `SDL_EVENT_WINDOW_FOCUS_LOST` neither FNA nor CNA clears accumulated input state. FNA is **also
  event-driven and accumulating** — its `Keyboard.keys` list is mutated by KEY_DOWN/KEY_UP
  (`SDL3_FNAPlatform.cs:905-940`), and on focus loss it *only* sets `game.IsActive = false`
  (`SDL3_FNAPlatform.cs:1026-1035`); it never clears keys. So FNA has the **identical** stuck-key edge
  case (a held key whose up-event is delivered to another window) — not merely the same behavior with a
  different consequence. CNA therefore matches FNA in both behavior and consequence, which is the correct
  call under the FNA-fidelity principle. A beyond-FNA `ClearTransientState()` on focus loss was considered
  and **rejected** (it would silently diverge from the reference); the XNA-standard mitigation is for the
  game to gate input on `Game.IsActive`. Pinned by
  `SdlInputBridgeKeyboardTest.WindowFocusLostDoesNotClearHeldKeysMatchingFna`.
- **Coordinate consistency (task 952):** mouse and the `InputManager` touch snapshot both use
  renderer-logical space and are consistent. The **gesture** path feeds `GestureDetector` in a
  display-size pixel basis, which can differ from renderer-logical space when logical size ≠
  backbuffer size (letterboxed). `displayOrientation_` is stored but not applied to coordinates.
  Flagged for targeted verification.

---

## Definition of Done for Input (task 958)

Input is "done" when **all** of these hold — not before:

1. Full configure succeeds in a **complete** checkout (submodules + sibling repos present).
2. All input unit tests pass.
3. Fake-SDL / gamepad tests pass (**satisfied — Phase I15**: 20 device-level tests via the injectable
   `ISdlGamepadBackend`). Real-hardware *actuation* remains manual-only (see above).
4. Docs updated (this file + `input-build-and-test.md`).
5. Intentional FNA deviations listed (this file).
6. No stale `Status: PARTIAL` comments unless still true.

Coverage is **not** claimed as "100% FNA fidelity". Phase I15 added the fake SDL gamepad layer, so
the gamepad SDL-bound *translation/bookkeeping* paths are now headless-tested (not just audited); what
remains is real-hardware *actuation*, which is manual-only. See `plan_input.md` for the per-task status.
