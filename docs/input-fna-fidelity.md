<!-- SPDX-License-Identifier: MS-PL -->
# CNA Input — FNA Fidelity Notes

> **Related input docs (INP-0003):** [plan](../plan_input.md) · [backend](input-backend.md) · [FNA fidelity + deviations](input-fna-fidelity.md) · [member-parity matrix](input-member-parity-matrix.md) · [frozen API + tier glossary](input-public-api-frozen.md) · [test coverage](input-test-coverage.md) · [build & test](input-build-and-test.md) · [platform notes](platform-input-notes.md) · [manual results](input-manual-verification-results.md) · [demo checklist](demo-input-checklist.md)

CNA's `Input` namespace uses **FNA** (`Microsoft.Xna.Framework.Input` + its SDL platform layer) as
the authoritative behavioral reference. This document records, per input area: (a) behavior that
matches FNA, (b) **intentional** CNA deviations (with the reason), and (c) known **gaps / TODO**.
It reflects the Phase I13/I14 code-vs-FNA audit (tasks 954–956).

CNA is partially derived from / behaviorally based on FNA (see the repository's attribution/licensing
notes); those notices are intact and must not be removed.

## Architecture note (task 953)

FNA is **poll-based per public call**: it re-reads device state when `GetState()` runs. CNA publishes
input **once per frame**: keyboard, mouse, mapped gamepads and raw joysticks are whole-device platform
snapshots, while input areas not yet migrated in Phase 5 are accumulated from `PlatformEvent`. Public `Get*State()` reads the
stored frame state, so two reads in one `Update()` cannot observe different native instants. The two
models are behaviorally equivalent for normal game-loop use; the differences below are where they
are not identical.

---

## Keyboard

| Aspect | Status |
|---|---|
| `Keys` enum numeric values | **Matches FNA exactly** (all values, incl. hex outliers `Pause`=0x13, `Kana`=0x15, `ChatPadGreen`=0xCA, `OemCopy`=0xf2, …). |
| SDL keycode/scancode → `Keys` maps | **Faithful port** of FNA `INTERNAL_keyMap`/`INTERNAL_scanMap`. |
| `NONUSHASH`/`NONUSBACKSLASH` (ISO-layout extra keys) | **Dropped**, not marked `Keys::None` — DEC-16-consistent (INPUT-KBD-011). See "Intentional deviations" below. |
| `GetPressedKeys()` ordering, `GetHashCode`, `this[Keys]` | Matches FNA (ascending numeric order; 8×32-bit XOR hash). |
| Repeated key-down while already down | State de-dupes (`unordered_set`) — matches FNA. |
| Key-up without prior key-down | No-op — matches FNA. |
| Focus-loss keyboard reset | **Neither FNA nor CNA clears keys on focus loss** (DEC-15: match FNA, accepted). See "SDL bridge" below for the event-driven consequence. |

**Intentional deviations:**
- **DEC-16 (accepted):** unmapped keycodes (`'é'`, `SDLK_UNKNOWN`) are **dropped** rather than pushed as
  `Keys::None`. FNA's `ToXNAKey` returns `Keys.None` and then does `Keyboard.keys.Add(Keys.None)`
  (`SDL3_FNAPlatform.cs:905-908`), leaving a meaningless "None" key marked pressed; CNA's drop is cleaner.
  Tested (`UnmappedKeycodeIsDroppedNotMarkedNone`).
- **INPUT-KBD-011/019 (accepted, extends DEC-16 to the scancode path):** scancodes with no XNA `Keys`
  value are **dropped** (`std::nullopt`) rather than mapped to `Keys.None`, so — exactly as DEC-16 for
  keycodes — `Keys::None` never enters the pressed set. This covers the no-scancode sentinel
  `SDL_SCANCODE_UNKNOWN` (matching the keycode path's `SDLK_UNKNOWN` drop) and the two ISO-layout extra
  keys `SDL_SCANCODE_NONUSHASH` / `SDL_SCANCODE_NONUSBACKSLASH`, which FNA maps to `Keys.None` (with its
  own `FIXME: … need verification`, `SDL3_FNAPlatform.cs:2615-2617`) and adds to its pressed list. FNA is
  the authoritative reference for the *value* (there is no better XNA mapping — CNA does not invent a
  divergent `OemBackslash`), but the pressed-set-pollution policy follows DEC-16, not FNA. Tested
  (`IsoLayoutExtraScancodesAreDroppedNotMarkedNone`).
- **DEC-17 (accepted):** `SDLK_AC_BACK` → `Keys::Escape` (Android/browser Back button) — a CNA-only
  convenience not in FNA, so "back" acts as cancel/exit on those platforms. Tested
  (`AndroidBackButtonMapsToEscape`).
- **DEC-19 (accepted — matches FNA):** text-synthesis on key-down re-emits control chars gated on SDL's
  `repeat` flag — **the same gate FNA uses** (`else if (evt.key.repeat)`, `SDL3_FNAPlatform.cs:923`), so
  this is not a deviation. Tested (`KeyRepeatReemitsControlCharacter`).

**XNA `Keys` with no desktop SDL source (INPUT-KBD-015 / -016 / -017 — matches FNA):** several `Keys`
values exist in the enum (exhaustively value-pinned by INPUT-KBD-001 / INPUT-API-034) but are never
produced by a desktop SDL event, exactly as in FNA (whose `keyMap`/`scanMap` omit them too — confirmed
byte-for-byte by INPUT-KBD-009/010):
- **IME keys** — `Kana` (21), `Kanji` (25), `ImeConvert` (28), `ImeNoConvert` (29), `ProcessKey` (229):
  Windows IME virtual keys with no SDL keycode/scancode; IME text reaches games via `TextInputEXT`.
- **ChatPad keys** — `ChatPadGreen` (202), `ChatPadOrange` (203): Xbox 360 ChatPad console-only keys with
  no desktop hardware/SDL source.
- **Browser / media keys** — `BrowserBack`…`BrowserHome` (166–172), `MediaNextTrack`…`MediaPlayPause`
  (176–179), `LaunchMail`/`SelectMedia`/`LaunchApplication1|2` (180–183), etc.: present in the enum, but
  the **only** media keys CNA/FNA map from SDL are `VolumeUp`/`VolumeDown`. The rest (SDL delivers
  `SDLK_MEDIA_*`, `SDLK_AC_HOME/SEARCH`, …) are dropped. Tested
  (`SdlMediaBrowserKeysAreUnmappedExceptVolumeMatchingFna`, `ImeAndChatPadKeysExistAndAreConsoleOrImeOnly`).

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
- **DEC-18 (superseded by N-005, corrected 2026-07-17/P1-018):** SDL's horizontal wheel (`wheel.x`) was
  originally dropped entirely — XNA/FNA `MouseState` exposes only the vertical `ScrollWheelValue`, so
  there was no property to route horizontal scroll to. N-005 added a CNAEXT/EXT field instead: `wheel.x`
  is now scaled to the same 120-unit notch and surfaced via
  `MouseState::getHorizontalScrollWheelValueEXTProperty()`
  (`InputManager::AddHorizontalScrollWheelDelta`, wired in `SdlInputBridge::ProcessEvent`). It is
  deliberately excluded from `Equals`/`GetHashCode`/`ToString`/`==`/`!=` so those stay byte-identical to
  FNA. Tested in `SdlInputBridgeMouseTests.cpp` (horizontal-wheel delta/accumulation tests) and
  `MouseInputTests.cpp` (`NineArgConstructorAlsoSetsHorizontalScrollWheelEXT`,
  `HorizontalScrollWheelEXTIsExcludedFromEqualityAndHash`). This entry previously cited a test,
  `HorizontalWheelIsIgnored`, that no longer exists in the repo — the doc had gone stale relative to the
  N-005 change; corrected here.
- **`MouseState::GetHashCode()` (P1-018):** hashes `x_ ^ (y_*31) ^ (scrollWheelValue_*17)`
  (unsigned-wraparound arithmetic to avoid signed-overflow UB) vs. FNA's `return base.GetHashCode();`
  (`MouseState.cs:173-180`), which delegates to `ValueType.GetHashCode()` — a CLR-internal,
  reflection-based algorithm with no fixed, reproducible formula. Deliberate; same pattern already
  accepted for `GamePadState::GetHashCode()` above. Consistent with the `GetHashCode` contract (equal
  states hash equal — `GetHashCodeIsConsistentForEqualStates` in `MouseInputTests.cpp`); button states
  and the horizontal wheel EXT field are intentionally excluded from the formula, matching `Equals`'s
  exclusion of the EXT field.

### MouseCursor

No FNA source exists for `MouseCursor` — confirmed by full-tree search of
`/rv/data/library/github.com/FNA-XNA/FNA`. It is a MonoGame-derived CNAEXT extension
(`include/Microsoft/Xna/Framework/Input/MouseCursor.hpp`), audited against MonoGame's
`MouseCursor.cs`/`MouseCursor.SDL.cs` for task P1-017. Full stock-cursor parity confirmed (all 12:
Arrow, IBeam, Wait, Crosshair, WaitArrow, SizeNWSE, SizeNESW, SizeWE, SizeNS, SizeAll, No, Hand). CNA
intentionally improves on MonoGame's actual SDL-backend behavior by making `Dispose()` a no-op for the
stock-cursor singletons (`isSystemSingleton_` guard) — real MonoGame's `PlatformDispose()` frees the
shared handle unconditionally, corrupting the singleton for all other holders. No accidental
divergences found; move ctor/assignment (a pure C++ addition, no C# analogue) reviewed line-by-line,
self-move-assignment guard now covered by a regression test.
- **Disposed-cursor behavior deviates from the project's general `IDisposable` convention (P3-033,
  accepted):** `MouseCursor::Dispose()` never throws, and — unlike the CLAUDE.md-documented default
  ("throw `std::runtime_error` if used after disposal") — `Mouse::SetCursor` on a disposed cursor is
  an intentional safe no-op (tested by
  `SetCursorIsSafeNoOpForDisposedCursor`). This is deliberate: MonoGame's own `MouseCursor` (the
  source this CNAEXT type is modeled on) defines no post-Dispose exception contract either, and a
  cursor object's only operations are read-only/pass-through (unlike, say, a `Stream`, where
  use-after-dispose hides a real resource-safety bug worth surfacing loudly). `Dispose()` itself
  remains idempotent (double-dispose is a safe no-op — P3-034, tested by
  `DisposeIsIdempotentAndMakesOwnedCursorUnusable`), which does follow the general convention.
- **Native cursor handles are platform-owned (PLAT-81, changed 2026-08-13):** the former public
  `SDL_Cursor*` constructor and `GetSDLCursor()` extension were removed. `MouseCursor` now owns only
  a system-shape value or copied RGBA pixels, and `Mouse::SetCursor` sends that description through
  `IPlatformMouse`. This intentionally supersedes P3-037: keeping the raw accessor would preserve an
  SDL type in the public input API and make non-SDL platforms impossible to implement honestly.
- **Logical→window scaling:** CNA converts logical→window at `SetPosition` time via the graphics
  backend (`TransformLogicalToWindow` / `SDL_RenderCoordinatesToWindow`); FNA scales at `GetState`
  read time. Equivalent for the common case (see INPUT-MOUSE-002 (decision a-0001)).
- **`ClickedEXT` is multicast (DEC-06, fixed 2026-07-05):** now a `System::MulticastAction<int>` matching
  FNA's `public static Action<int>` — `+=` adds subscribers, `=` replaces, `= nullptr` clears. (Was a
  single `std::function`; the second-subscriber-lost gap is closed.)
- **Relative-mode service state (DEC-14, migrated by PLAT-80):** the public getter and setter use
  `IPlatformMouse`; SDL3 keeps the mode next to native window capture and Terminal refuses it
  through the capability contract. `Update()` collects native displacement after the event pump,
  while `ConsumeRelativeDelta()` drains it on each public `GetState()` call. That preserves FNA's
  unusual consume-on-read behaviour without keeping a second public-path cache in `InputManager`.

---

## GamePad

| Aspect | Status |
|---|---|
| Dead-zone constants + math (independent/circular/none) | **Matches FNA exactly** (`7849`/`8689`/`30`). |
| Thumbstick Y-sign, trigger + stick normalization | **Matches FNA exactly** (`/32767` over the whole Sint16 range, `/-32767` for the inverted Y — see the L-015 fix below). |
| SDL button → `Buttons` mapping (all 21, incl. paddles/touchpad/guide) | **Matches FNA exactly.** |
| Duplicate add / unknown remove / no-free-slot | Safe; duplicate-add is **safer** than FNA (no leak). |
| `SetVibration`/`SetTriggerVibration`/`SetLightBar`/`GetGyro`/`GetAccelerometer`/`GetGUID` | **Faithful ports.** |
| `ToString()` | Matches FNA (type name). |

**Fidelity fix (L-015, source-logic audit 2026-07-06):** `normalize_stick_axis` divided the **negative**
half of the SDL stick range by **32768** while the positive half used 32767. FNA divides the **whole** range
by 32767 (`SDL3_FNAPlatform.cs:1814-1822`: `axis / 32767`, `axis / -32767` for Y). The endpoints agreed
(both give ±1 after clamping) but every non-endpoint negative sample diverged (e.g. `-16384` → `-0.5` in CNA
vs FNA's `-0.50001`). `NormalizeGamepadAxis` now applies `clamp(value / 32767, -1, 1)` for the
full range — byte-identical to FNA and shared by event and snapshot translation. Pinned by
`Sdl3EventMapperTests.GamepadAxesMapToCnaVocabularyAndBridgeNumerics`.

**Real bugs fixed in Phase I13/I14:**
- **`SDL_INIT_GAMEPAD` was never initialized** → no gamepad events were ever delivered. The migration
  now acquires `PlatformSubsystem::Gamepad` through `IPlatform` with SDL3's background-events hint
  configured by the platform implementation, so hot-plugged **and** already-connected pads are visible.
  - **Startup invariant:** `Game::DoInitialize()` acquires the subsystem after graphics-device
    creation and **before the first event pump/snapshot update**; both run modes funnel through it.
    Explicit disposal and destructor disposal release the same owned reference.
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
- **`Buttons` (and every other `[Flags]`-equivalent `enum class` in Input) implements `|`/`&`/`~`/`|=`/`&=`
  but not `^`/`^=` (P1-002).** C# gives every enum bitwise `|`/`&`/`^`/`~` for free; C++ `enum class`
  needs each spelled out explicitly. XNA/FNA game code conventionally only ever combines
  (`a | b`) or tests (`(state & flag) == flag`) flag enums, never XORs them, and no CNA source or test
  does either. Accepted as intentionally incomplete relative to what C# *permits* but never *uses* —
  add `operator^`/`operator^=` if a real XOR use case appears, rather than pre-emptively.
- **Out-of-range `PlayerIndex` never throws (P1-003):** FNA's `SDL3_FNAPlatform.cs` gamepad accessors
  (`GetGamePadCapabilities`/`GetGamePadState`/`SetGamePadVibration`/`SetGamePadTriggerVibration`/
  `GetGamePadGUID`/`SetGamePadLightBar`/`GetGamePadGyro`/`GetGamePadAccelerometer`, lines 1796-2074)
  index a fixed `GAMEPAD_COUNT`-sized array with no bounds check, so an out-of-range `PlayerIndex` cast
  throws `IndexOutOfRangeException` in FNA. CNA bounds-checks in every delegation path
  (`InputManager::try_get_player_slot`, `SdlInputBridge::get_sdl_gamepad_for_player`) and returns the
  graceful disconnected/false/empty fallback instead — safer, deliberate, and already pinned by
  `GamePadInputTest.AxisValuesAreClampedAndInvalidPlayerReturnsDisconnectedState`.
- **`GamePad::LeftDeadZone`/`RightDeadZone`/`TriggerThreshold`/`ExcludeAxisDeadZone` are `CNAEXT public`
  (P1-003):** FNA declares these `internal` (`GamePad.cs:21-23,132-147`), relying on same-assembly
  visibility so `GamePadThumbSticks.cs`/`GamePadTriggers.cs` can read them. C++ has no assembly-scoped
  visibility; CNA exposes them as `CNAEXT`-tagged public statics on `GamePad` so `GamePadThumbSticks.cpp`/
  `GamePadTriggers.cpp` (separate translation units) can consume them — the correct translation of FNA's
  `internal`, not an accidental widening of the public surface.
- `GamePadButtons::buttons_` was public in the header (declared before `private:`) despite FNA's field
  being `internal`; fixed to `private` with the existing `friend struct GamePadState;` preserved for
  the same-assembly-style access `GamePadState`'s constructor needs (P1-004).
- **Struct-level audit (P1-005):** every property of `GamePadCapabilities` (25 XNA bool properties +
  `GamePadType` + 10 `…EXT` bool properties) was compared field-by-field against FNA
  (`GamePadCapabilities.cs`): names, order, defaults (all `false` / `GamePadType.Unknown`), and
  getter/setter shape. Zero divergences found. FNA's `internal set` maps to a public `CNAEXT`-tagged
  setter (documented in the struct's own Doxygen comment) since C++ has no `internal` accessibility;
  all 10 EXT properties are correctly `CNAEXT` on both getter and setter. No `VendorId`/`ProductId`
  properties exist on this struct in current FNA — only the 10 boolean `…EXT` flags. Test coverage
  (`GamePadTests.cpp`, `GamePadMappingTests.cpp`, `PublicApiInputSignatureFreezeTests.cpp`,
  `PublicApiInputCompileTests.cpp`) already exercises every getter/setter individually (default state,
  per-flag isolation, round-trip, partial-capability combinations) plus full signature-freeze pinning;
  no gaps found, no new tests added.
- **Struct-level audit (P1-010):** every member of `GamePadTriggers` was compared line-by-line against
  FNA (`GamePadTriggers.cs`): the public 2-arg constructor's `[0,1]` clamp, the private/friend 3-arg
  dead-zone constructor (`GamePadDeadZone.None` clamps only; any other mode runs
  `GamePad::ExcludeAxisDeadZone` before clamping, applied independently to `Left`/`Right`), the
  epsilon-tolerant `==`/`!=`/`Equals`, and `GetHashCode()`'s `Left.GetHashCode() + Right.GetHashCode()`
  formula. Zero divergences found beyond the two already-documented, pre-existing, codebase-wide
  patterns above (`Equals(object obj)` omission, unsigned-wraparound `GetHashCode()` summation). One
  test-coverage gap (not a behavior bug) was closed: the private dead-zone constructor's `Right`
  trigger path had no independent test — added
  `GamePadTriggersTest.NonNoneDeadZoneModeAppliesIndependentlyToBothTriggers`.

**Platform-contract unit coverage (Phase I15 + PLAT-82 — no real hardware):** pure SDL-edge tests
exercise all **21** `SDL_GamepadButton` mappings, all six axes including Y inversion/endpoints and
trigger normalisation, controller kind/model, connection/power/glyph translation and clamped motor
conversion. A complete canned `IPlatformGamepad` then drives the real public `GamePad` surface and
pins four-slot isolation, immutable snapshots, packet numbers, all buttons/axes/dead zones,
capabilities, ordinary/trigger rumble, LED, gyro/accelerometer, player index, touchpad, power,
identity and GUID formatting (xinput / vendor+product little-endian / Valve overrides). The former
`ISdlGamepadBackend` and duplicate `InputManager` mapped-gamepad store were deleted.

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
| `GetCapabilities()` side effects | **Fixed (task 894/PLAT-86):** now peeks at panel-owned event state without advancing a touch frame. |
| `GetState()` read-frequency dependence | **Fixed (INP-AUD-001, 2026-07-16):** `TouchPanel::GetState()` is a pure read; see below. |
| `GetCapabilities()` SDL enumeration | **Fixed (INP-AUD-003, 2026-07-16):** now queries `system_device_backend().GetTouchDevices()` every call, matching FNA; see below. |
| `TouchCollection::CopyTo` | **Fixed (task 902):** out-of-range index now throws `std::out_of_range` (was UB). |
| `TouchCollection::FindById` not-found out-param | **Fixed (P1-022):** now writes the `Invalid` sentinel location (`TouchLocation(-1, Invalid, Vector2.Zero)`) on the not-found path — previously left the caller's out-param untouched. Matches FNA `TouchCollection.cs:125-130`. |
| Empty/default semantics, out-of-range indexer, `IsReadOnly=true` | Equivalent to FNA (empty vector replaces null sentinel; `out_of_range` for bad index). |

**Intentional / documented deviations:**
- **`TouchCollection::CopyTo` inserts rather than overwrites (P1-022):** FNA's `CopyTo`
  (`TouchCollection.cs:105-110`) delegates to `List<T>.CopyTo(T[] array, int arrayIndex)`, which
  overwrites pre-existing slots of a fixed-size destination array starting at `arrayIndex` and throws
  if there isn't enough room past that index. CNA's destination is a growable
  `std::vector<TouchLocation>&`, so `CopyTo` instead **inserts** the source elements at `arrayIndex`,
  shifting later elements right rather than overwriting them — an unavoidable consequence of the
  fixed-array-vs-growable-vector type difference. Out-of-range `arrayIndex` still throws
  `std::out_of_range` (task 902), matching FNA's `ArgumentOutOfRangeException` intent. Pinned by
  `CopyToAppendsAllElementsInOrder`, `CopyToFromEmptyCollectionIsANoOp`,
  `CopyToThrowsOnOutOfRangeIndexInsteadOfUndefinedBehavior`, `CopyToInsertsAtValidNonZeroIndex`.
- **`GestureSample`'s `CNAEXT` default constructor seeds `FingerIdEXT`/`FingerId2EXT` with
  `TouchPanel::NO_FINGER` (P1-020):** a strict `default(GestureSample)` in C# would zero every field
  (`FingerIdEXT == 0`), but `0` is a legitimate real SDL finger id — a zero default would look
  ambiguously like "touching with finger 0" rather than "no finger". CNA deliberately uses the same
  `NO_FINGER` (-1) sentinel both FNA-parity constructors already use (`GestureSample.cs:93-94,
  117-118`) instead. Pinned by `GestureSampleTest.DefaultConstructorProducesZeroedNoneSample`.
- **Touch IDs** are a compact sequential counter (1,2,3,…) rather than FNA's cast SDL finger id. IDs
  are opaque to games. Overflow only after ~2³¹ distinct fingers in one session (theoretical).
- **`MAX_TOUCHES` / `MaximumTouchCount` (DEC-09 + DEC-10, fixed 2026-07-05):** now matches FNA on both
  counts. `GetCapabilities` reports `MaximumTouchCount = 4` (FNA: "MaximumTouchCount is completely bogus;
  for any touch device, XNA always reports 4", `SDL3_FNAPlatform.cs`), `0` when disconnected — this is a
  fixed XNA-compat value, NOT the tracking cap. `TouchPanel::GetState()` caps the public snapshot at
  `MAX_TOUCHES (8)`, matching FNA's fixed `TouchLocation[MAX_TOUCHES]` array (the panel-owned
  event map is internally unbounded, but the public state never exceeds 8).
- **Zero display size at startup (P5-014):** before `GraphicsDevice` publishes the virtual back-buffer
  size, `TouchPanel.DisplayWidth/Height` are `0`. The gesture path (`INTERNAL_onTouchEvent`, TouchPanel.cpp:188)
  **early-returns** when either is `<= 0`, so no touch collapses to a bogus `(0,0)`-corner gesture. Touch
  **presence** is still tracked, because the bridge records it via
  `TouchPanel::INTERNAL_setTouchState(to_touch_pixel_position(...))`,
  which scales by the SDL **window** size (min 1×1), independent of the display metric. So at startup there is
  an **intentional** divergence — touch tracked, gestures suppressed — that resolves the instant a valid
  display size is published (gestures resume). Pinned by
  `PlatformInputBridgeTouchGestureTest.TouchBeforeDisplaySizeIsKnownTracksTouchButSuppressesGestures` and
  `TouchEdgeCaseTest.ScalingProducesNoGestureWhenDisplaySizeIsZero`.
- **`GetCapabilities` SDL enumeration (INP-AUD-003, fixed 2026-07-16):** `TouchPanel::GetCapabilities()`
  now queries `IPlatformInputDevices::HasDevice(Touch)` on every call, matching
  FNA's `GetTouchCapabilities()` (`SDL_GetTouchDevices()` on every query, `SDL3_FNAPlatform.cs:2265-2280`).
  **Previously** it reported `IsConnected = false` for any touchscreen that had not yet been touched —
  it only ever consulted the sticky `touchDeviceExists_` flag (set on the first `FINGER_DOWN`,
  bridge) or the live panel-state peek, never SDL's own device
  list. That was documented at the time as "intentional and FNA-faithful" by analogy with FNA's note
  that *Windows* only notices a touch screen once it is touched (`SDL3_FNAPlatform.cs:972`) — but FNA's
  own `GetTouchCapabilities()` still calls `SDL_GetTouchDevices()` unconditionally on every platform, so
  a real enumerable-but-untouched device on any non-Windows platform was reported disconnected, which
  was not actually FNA-faithful. The fix makes SDL enumeration the primary source; `touchDeviceExists_`
  and the panel-state peek remain as fallbacks specifically for the Windows-style late-enumeration
  case. Still fully non-mutating. Pinned by
  `GetCapabilitiesIsDisconnectedBeforeAnyTouch`, `GetCapabilitiesIsConnectedOnceTouchDeviceExists`,
  `GetCapabilitiesIsConnectedViaPanelStateWhenFlagUnset`, and the
  `TouchCapabilitiesEnumerationTest` fixture (fake-backend enumeration, empty-enumeration-with-sticky-
  flag, empty-enumeration-with-live-touch, and non-mutation cases).
- **Touch collection ordering (DEC-20, P5-012):** FNA's `TouchPanel.GetState()` iterates its fixed
  `touches[0..MAX_TOUCHES]` array (`TouchPanel.cs:97`), so its collection order is **SDL finger-array slot
  order**. CNA's panel-owned event fallback instead orders by **ascending touch id** through its
  ordered map. Both are fully deterministic; because CNA touch ids are a compact
  sequential appearance-order counter (see above) with lowest-free reuse, ascending-id order tracks
  appearance/slot order the same way FNA's does. Order is opaque to games (they index by finger id, not
  position). Pinned by `TouchInputTest.GetStateHandlesMultipleTouchIdsAndKeepsDeterministicOrder` and
  `GetStateOrdersMultipleTouchesByAscendingIdRegardlessOfInsertionOrder`.
- `TouchPanel::Update()` copies current→previous **before** the gesture update; FNA does gesture update
  first (`TouchPanel.cs:219`). **Confirmed inert (DEC-13, 2026-07-05):** `GestureDetector::OnUpdate()`
  operates only on the gesture detector's own state and never reads or writes `touches_`/`previousTouches_`,
  so the two statements touch disjoint state and the ordering is unobservable. Pinned by
  `TouchEdgeCaseTest.UpdatePropagatesTouchesToPreviousForSlotPathContinuity`.
- `TryGetPreviousLocation` now writes the out-param on **every** path (DEC-12, fixed 2026-07-05): it
  assigns `TouchLocation(Id, prevState, prevPosition)` and returns `prevState != Invalid`, matching FNA
  exactly (on the `false` path the out-param is the Invalid previous location, not left untouched).
- **`GetState()` frame-accurate read (INP-AUD-001, fixed 2026-07-16; moved in PLAT-86):** FNA's
  `TouchPanel.GetState()` is a pure collection read; its frame advance (`SetFinger`-equivalent
  polling) happens once per frame in FNA's own `Update()`, not inside the getter
  (`TouchPanel.cs:94-105, 224-228`). CNA's old event-driven input store previously
  advanced `Pressed`→`Moved` promotion, `Released` retirement, and previous-location tracking
  **inline on every call**, so the reported state depended on how many times application code
  called `GetState()` per frame rather than on the frame boundary (two reads in one frame could
  observe `Pressed` then `Moved`; zero reads in a frame silently skipped a promotion/retirement).
  Fixed by splitting the operation: `TouchPanel::GetState()` is a pure snapshot read and
  `TouchPanel::Update()` performs the previous-state promotion/release retirement exactly once per
  frame — itself driven once per
  `Game::Update()` tick via `FrameworkDispatcher::Update()`. Pinned by
  `GetStateIsPureAndRepeatedReadsWithinAFrameAreIdentical`,
  `UpdateWorksEvenWithoutAnIntermediateRead`, and
  `ReleasedTouchIsVisibleForExactlyOnePostAdvanceReadRegardlessOfPriorReads`
  (`tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp`).

---

## Gestures

`GestureDetector` reproduces FNA's tap / double-tap / hold / drag / flick / pinch state machine.
Covered by `GestureDetectorTests` and the end-to-end `PlatformInputBridgeTouchGestureTest` suite (including the
new `FINGER_CANCELED` release path). Broader parameterized regression coverage across every gesture
type + interruption is partial (task 906).

**Threshold constants verified byte-identical to FNA (P6-026/027/028, 2026-07-17):** independently
re-derived from `GestureDetector.cs` rather than trusting prior claims —
`MOVE_THRESHOLD`=35px (`GestureDetector.cpp:34`), `MIN_FLICK_VELOCITY`=100.0f (`:35`), the double-tap
timing window=300ms (`:182`, `TimeSpan.FromMilliseconds(300)` in FNA), the hold threshold=1 second
(`:230`/`:419`, `TimeSpan.FromSeconds(1)` in FNA), and the flick-velocity exponential-smoothing formula
`velocity += (instVelocity - velocity) * 0.45f` where `instVelocity = delta / (0.001f + dt)`
(`GestureDetector.cpp:406-409`, byte-identical to `GestureDetector.cs:504-507`). None of these
thresholds scale with `TouchPanel.DisplayWidth`/`DisplayHeight` in either engine — they are fixed pixel/
time constants (P6-033: confirmed NOT display-size-dependent, matching FNA).

**Intentional / documented deviation — gesture auto-timestamp units (P6-012, found 2026-07-17):**
FNA's `GestureDetector.GetGestureTimestamp()` (`GestureDetector.cs:546-552`) is
`TimeSpan.FromTicks(Environment.TickCount)` — `Environment.TickCount` is a **millisecond** counter, but
`TimeSpan.FromTicks` expects **100ns ticks**, so FNA's own formula has a ~10000x unit mismatch versus
its own doc comment ("XNA calculates gesture timestamps from how long the device has been turned on").
CNA's `GetGestureTimestamp()` (`GestureDetector.cpp:67-74`) instead converts the millisecond count to
ticks correctly (`ms * TimeSpan::TicksPerMillisecond`), producing a dimensionally accurate "time since
epoch" `TimeSpan`. This is a deliberate, accepted deviation, not a bug: `GestureSample.Timestamp` has no
defined absolute reference point in either engine (it is not wall-clock time, and no game-facing
contract depends on its exact scale), only relative ordering/deltas between gesture events matter in
practice, and both formulas remain monotonically increasing with real elapsed time. Replicating FNA's
literal unit-mismatch here would trade a real correctness property for a match against what reads as an
upstream implementation slip, with no compatibility benefit since no observable game behavior depends on
the absolute value. Not previously documented; added here as part of the P6 audit.

**`GestureSample` equality/`ToString` (P6-039, confirmed 2026-07-17):** neither FNA's
`GestureSample.cs` nor CNA's `GestureSample.hpp` defines `Equals`/`GetHashCode`/`ToString`/`operator==`
— it is a bare value-carrier struct in both engines (no `ValueType`-default override needed, unlike
`MouseState`/`GamePadState`/`KeyboardState`). Nothing to test; confirmed absent by design in both.

**Gesture queue has no overflow/eviction policy (P6-035, confirmed 2026-07-17):** FNA's
`gestures` is a plain `Queue<GestureSample>` (`TouchPanel.cs:80`) and CNA's `gestures_` is a plain
`std::queue<GestureSample>` (`TouchPanel.hpp:217`) — both grow unbounded if a game never calls
`ReadGesture()`. This is accepted upstream XNA/FNA behavior (a "the game is expected to drain its
queue" API contract), not a CNA-specific gap to fix.

---

## TextInputEXT / TextEditing

`TextInputEXT` (unlike most `EXT`-suffixed CNA types) is not a CNA invention — FNA itself already
ships a `Microsoft.Xna.Framework.Input.TextInputEXT` static class (`FNA/src/Input/TextInputEXT.cs`)
as its own beyond-XNA-4.0 extension, so it is ported as a strict FNA-parity type in the
`Microsoft::Xna` namespace, not tagged `CNAEXT` itself. FNA's baseline surface is exactly:
`TextInput`/`TextEditing` events, `WindowHandle` property, `IsTextInputActive()`,
`IsScreenKeyboardShown()`/`IsScreenKeyboardShown(window)`, `StartTextInput()`, `StopTextInput()`,
`SetInputRectangle()` — every one of these is a faithful 1:1 port (P2-030). CNA layers three
`CNAEXT`-tagged members with no FNA analog on top of that baseline: `TextEditingCandidatesEXT` (IME
candidate-list event; FNA's `TextInputEXT` has no candidates support at all),
`StartTextInputWithTypeEXT(TextInputTypeEXT)` and the `CNA::Input::TextInputTypeEXT` enum itself
(a 9-value mobile/on-screen-keyboard hint mirroring SDL3's `SDL_TextInputType` one-to-one — verified
against `SDL_keyboard.h`, P2-051). Those three are the only genuine extension surface; everything
else in this section is FNA-required behavior, not CNA scope creep.

| Aspect | Status |
|---|---|
| `StartTextInput`/`StopTextInput`/`SetInputRectangle`/active-window | **Faithful ports** (+ null-window guards). |
| UTF-8 → UTF-16 decode (BMP + astral surrogate pairs) | **Matches FNA** (`Encoding.UTF8.GetChars` equivalent). Exhaustively tested. |
| `TextInput` code-unit type | `charcs`/UTF-16 code unit, matching FNA's `Action<char>` (Phase I9 task 806). |
| Control-char synthesis (Home/End/Back/Tab/Enter/Delete/Ctrl+V) | **Matches FNA exactly**: `(char)2/3/8/9/13/127/22` via the same `TextInputBindings`/`TextInputCharacters` table as `FNAPlatform.cs:261-278` (P2-039..045). |

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
- **`Game.IsActive` on desktop focus change (INP-AUD-002, fixed 2026-07-16):** the "gate on
  `Game.IsActive`" mitigation described just above only works if `IsActive` is actually correct.
  Until this fix, `Game::PollEvents()` handled the mobile-style
  `SDL_EVENT_WILL_ENTER_BACKGROUND`/`SDL_EVENT_DID_ENTER_FOREGROUND` pair but had no case for the
  desktop `SDL_EVENT_WINDOW_FOCUS_LOST`/`SDL_EVENT_WINDOW_FOCUS_GAINED` pair, so an ordinary desktop
  Alt-Tab left `Game::IsActive` `true` forever and never raised `Activated`/`Deactivated`. Both
  events now route through `setIsActiveProperty`, matching FNA
  (`SDL3_FNAPlatform.cs:1006-1037`). **Known scope gap (intentional, not part of this fix):** FNA's
  handler for these two events also toggles the X11 "fullscreen desktop" window flag and
  enables/disables the SDL screensaver; CNA does not replicate either. Neither affects input
  semantics (the subject of this audit); revisit only if X11 fullscreen-on-Alt-Tab behavior or
  screensaver suppression is reported as a real-world gap.
- **Coordinate consistency (INPUT-TOUCH-024, was task 952 — verified):** both touch paths target the same
  **logical (virtual back-buffer) coordinate space.** `GraphicsDevice` sets `TouchPanel::DisplayWidth/Height`
  to `virtualWidth/Height`. The **gesture** path scales the normalized SDL coord linearly by
  `DisplayWidth/Height` (`round(x·W, y·H)` in `TouchPanel::INTERNAL_onTouchEvent`) — identical to FNA, which
  also scales normalized touch by the back-buffer size. The **touch-state** path (`to_touch_pixel_position`
  → `to_logical_position`) maps into that same logical space. For a **uniform** presentation (no letterbox
  bars — e.g. EasyGL's `FixedHeightDynamicWidth` default, or any matched-aspect `SDL_Renderer`) the two
  coincide exactly; pinned by `GestureAndTouchStateShareTheLogicalCoordinateBasis` (gesturePos ÷ metric ==
  the normalized state position). **Known edge nuance:** under a true letterbox (logical aspect ≠ window
  aspect, centering bars) the gesture path stays linear (FNA-matching) while the touch-state path is
  letterbox-aware, so the two can differ *within the bar regions* — where a touch does not land on game
  content anyway. This is accepted (gesture side matches FNA; the divergence is confined to the bars).
  `displayOrientation_` is stored but not applied to coordinates (matches FNA).

---

## Definition of Done for Input (task 958)

Input is "done" when **all** of these hold — not before:

1. Full configure succeeds in a **complete** checkout (submodules + sibling repos present).
2. All input unit tests pass.
3. Platform-contract gamepad tests pass (**satisfied — PLAT-82**: SDL-edge translation plus canned
   public-surface tests). Real-hardware *actuation* remains manual-only (see above).
4. Docs updated (this file + `input-build-and-test.md`).
5. Intentional FNA deviations listed (this file).
6. No stale `Status: PARTIAL` comments unless still true.

Coverage is **not** claimed as "100% FNA fidelity". Gamepad translation and public contract mapping
are headless-tested; real-hardware *actuation* remains manual-only. See `plan_input.md` for status.

---

## Extension APIs (FNAEXT / CNAEXT) — INP-0214

Beyond the strict XNA 4.0 surface, CNA exposes the FNA/MonoGame extensions and CNA conveniences below.
See the tier glossary in `input-public-api-frozen.md`.

- **`TextInputEXT`** (FNAEXT, whole class CNAEXT) — portable text input/IME that XNA 4.0 lacked:
  `StartTextInput`/`StopTextInput`, the `TextInput` (per UTF-16 code unit) and `TextEditing` (IME
  composition) multicast events, `SetInputRectangle`, `IsTextInputActive`, `IsScreenKeyboardShown`,
  `WindowHandle`. Backed by SDL text-input; UTF-8→UTF-16 decode + control-char/Ctrl+V synthesis.
- **`MouseCursor`** (CNAEXT, whole class) — MonoGame-style custom cursors: stock cursors, `FromTexture2D`,
  ownership/disposal. Cursor creation needs `SDL_INIT_VIDEO` (graceful null otherwise).
- **Relative mouse mode** (`Mouse::…IsRelativeMouseModeEXT`) — FPS-style pointer lock + relative-delta
  accumulation; not a stock XNA concept.
- **Scancode mode** (`Keyboard::GetKeyFromScancodeEXT` + `FNA_KEYBOARD_USE_SCANCODES`) — layout-independent
  physical-key mapping, matching FNA.
- **GamePad EXT** — `GetGUIDEXT` (device GUID), `SetLightBarEXT` (PS4/5 LED), `SetTriggerVibrationEXT`
  (adaptive-trigger haptics), `GetGyroEXT`/`GetAccelerometerEXT` (motion sensors); plus the
  `GamePadCapabilities` `Has…EXT` flags. All FNA extensions, capability-gated.
- **`Mouse::ClickedEXT`** — FNA's click callback (`MulticastAction<int>`, DEC-06).
- **`GestureSample::FingerId(2)EXT`** — per-gesture finger ids (CNAEXT convenience).

## Convention: verified fact vs intended behavior (INP-0216)

Throughout the input docs, a claim is either a **✅ verified fact** (backed by a green test or a mechanical
tool — the default for statements citing a `…Test`, the parity/coverage generators, or a compile guard) or
a **🎯 intended behavior** (implemented and FNA-faithful in code but **not** machine-verified here — e.g.
real-hardware actuation, live IME, native-Wayland cursor landing). Hardware/human-gated items are recorded
as 🎯/"not verified" in `input-manual-verification-results.md`, never asserted as ✅. Do not upgrade a 🎯 to
✅ without a green test or a dated manual entry.
