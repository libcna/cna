# Frozen public Input API surface (INPUT-API-031)

> **Related input docs (INP-0003):** [plan](../plans/plan_input.md) · [backend](input-backend.md) · [FNA fidelity + deviations](input-fna-fidelity.md) · [member-parity matrix](input-member-parity-matrix.md) · [frozen API + tier glossary](input-public-api-frozen.md) · [test coverage](input-test-coverage.md) · [build & test](input-build-and-test.md) · [platform notes](platform-input-notes.md) · [manual results](input-manual-verification-results.md) · [demo checklist](demo-input-checklist.md)

This is the **golden signature snapshot** of the public `Microsoft::Xna::Framework::Input` (and
`…::Input::Touch`) surface. It is the human-readable companion to the enforced compile-time guard
`tests/Microsoft/Xna/Framework/Input/PublicApiInputSignatureFreezeTests.cpp`, which pins every entry
below through a fully-spelled function-/member-pointer type or a type trait. Any drift — a removed or
renamed member, a changed signature, a changed constructor parameter list — fails to compile.

**Keep the two in lock-step.** When you intentionally add, remove, or change a public member, update
**both** this document and the freeze test in the same commit.

## Classification — canonical API-tier glossary (INP-0001)

This is the **single canonical definition** of CNA Input API tiers. Every input doc refers here rather
than re-defining them. A member/type belongs to exactly one tier:

- **STRICT** — part of the **XNA 4.0** public API. Must match XNA/FNA exactly: names, signatures, enum
  values, and behavior (modulo the documented C++ property convention `getXProperty`/`setXProperty` and
  the accepted-deviations list in `input-fna-fidelity.md`).
- **FNA-compatible** — a STRICT member whose *behavior* intentionally follows **FNA** (the reference SDL
  platform layer) where XNA left it platform-defined. Same API as STRICT; the FNA source is the authority.
- **FNAEXT** (`EXT` name suffix) — an **FNA extension** beyond stock XNA 4.0 that FNA/MonoGame expose to
  consumers (e.g. `Keyboard::GetKeyFromScancodeEXT`, `Mouse::…IsRelativeMouseModeEXT`, the gamepad
  `GetGUIDEXT`/`SetLightBarEXT`/`SetTriggerVibrationEXT`/`GetGyroEXT`/`GetAccelerometerEXT`, `TextInputEXT`,
  `GestureSample::FingerId(2)EXT`). Consumer-visible; carries the `EXT` suffix and (for non-enum members)
  the `CNAEXT` marker.
- **CNAEXT** — a **CNA/MonoGame convenience** that is public but has **no** XNA/FNA equivalent (e.g.
  `MouseCursor` as a whole, `KeyboardState::ToString`, value-struct default constructors, `FromButtonArray`).
  Tagged with the `CNAEXT` marker macro.
- **INTERNAL** — `CNA::Internal::*` implementation (`SdlInputBridge`, `InputManager`, `GestureDetector`,
  `ISdlHapticBackend`, …). **Not public API**; must never appear in a public XNA header or signature
  (enforced by the `PublicApiInputCompileTests` SDL/Internal-leak guard).

## Freeze scope

Frozen: all STRICT, EXT, and stable CNAEXT-convenience members of every public Input type.

Deliberately **excluded** (internal plumbing exposed as public for the SDL bridge / tests, not API —
so the golden file is not coupled to internal churn):

- `TouchPanel::INTERNAL_onTouchEvent`, `TouchPanel::ResetForTests`
- `Mouse::INTERNAL_onClicked`, `Mouse::ResetForTests`
- `TextInputEXT::INTERNAL_OnTextInput`, `TextInputEXT::INTERNAL_OnTextEditing`, `TextInputEXT::INTERNAL_OnTextEditingCandidates`, `TextInputEXT::ResetForTests`
- private data members (e.g. `GamePadButtons::buttons_`)

Hidden-friend equality operators (`operator==` / `operator!=` on the value structs) are frozen via an
ADL expression (`a == b` / `a != b` yielding `bool`) rather than an address-of, because they are not
visible to qualified lookup.

The pure **enums** (`Keys`, `Buttons`, `ButtonState`, `KeyState`, `GamePadType`, `GamePadDeadZone`,
`TouchLocationState`, `GestureType`) are value-frozen separately and exhaustively by INPUT-KBD-001 /
INPUT-TEST-001 under the enum-ABI guardrail **INPUT-API-034**; they are not repeated here.

## EXT / CNAEXT tagging convention (INPUT-API-032)

Every member with **no XNA 4.0 equivalent** is tagged so it cannot be mistaken for stock XNA:

- A **non-XNA enum value** carries the `EXT` **name suffix** (e.g. `Buttons::Misc1EXT`). Enum members
  do not take the `CNAEXT` macro.
- A **non-XNA non-enum member** carries the `CNAEXT` marker. If it is an FNA-compatible extension it
  **also** carries the `EXT` name suffix (e.g. `CNAEXT static ... GetGUIDEXT(...)`). A CNA-only
  convenience carries `CNAEXT` alone.
- An **entire non-XNA class** (`MouseCursor`, `TextInputEXT`) is marked `CNAEXT` at the class; its
  individual members inherit that and need no per-member marker.
- An **explicitly-declared default constructor of a value struct is `CNAEXT`.** C# structs have an
  *implicit* parameterless constructor, so FNA declares none; CNA's explicit `Type()` is a C++
  addition with no FNA counterpart. (The `= default` on `GamePadCapabilities` is the implicit-
  equivalent and stays untagged.)
- A **`ref`/`&&` overload that is the C++ rendering of one XNA member** (e.g. the two
  `TouchCollection` vector constructors together map FNA's single `TouchCollection(TouchLocation[])`)
  is treated as that XNA member, not as separate non-XNA API.
- **`private` members that map FNA `internal`** (e.g. the dead-zone-applying `GamePadThumbSticks` /
  `GamePadTriggers` constructors) are not public API and take no tag.

### Audit result (2026-07-05)

Every public Input member was scanned; each untagged public member was verified to be genuine XNA/FNA
API (`Mouse.WindowHandle`, `TouchPanel.WindowHandle`, `TouchCollection.FindById`, the indexer
`getItem`, etc. are all public in FNA). **Two defects were found and fixed:** `GamePadState()` and
`GestureSample()` default constructors were untagged while their 9 value-struct siblings were `CNAEXT`;
both are now `CNAEXT`, so all 11 explicit value-struct default constructors are uniformly tagged. This
member-by-member table (STRICT / EXT / CNAEXT per member) is the recorded audit; the compile guards
INPUT-API-030 (header hygiene) and INPUT-API-031 (signature freeze) keep it honest, and the enum-value
guard INPUT-API-034 covers the enum layer.

## `GetTypeName()` exemption (INPUT-API-029)

CLAUDE.md requires every **concrete `System::Object` subclass** to override `CNAEXT GetTypeName()`.
Audit result (2026-07-06): **no** public Input type inherits `System::Object`, directly or transitively.
The value structs, static classes, and enums are all non-`Object`; the single base-class relationship,
`MouseCursor : System::IDisposable`, inherits `IDisposable`, which is **not** an `Object` subclass. So
`GetTypeName()` applies to none of the Input types — **all are exempt**. This exemption is pinned
mechanically by a `static_assert(!std::is_base_of_v<System::Object, T>)` block over the 18 public
class/struct Input types in `tests/Microsoft/Xna/Framework/Input/PublicApiInputCompileTests.cpp`; if a
type ever gains an `Object` base, that TU stops compiling — the point at which the `CNAEXT GetTypeName()`
override must be added.

---

## GamePad cluster (`Microsoft::Xna::Framework::Input`)

### `GamePad` — static class (XNA + EXT)
- `GamePad() = delete;` — STRICT
- `static GamePadCapabilities GetCapabilities(PlayerIndex);` — STRICT
- `static GamePadState GetState(PlayerIndex);` — STRICT
- `static GamePadState GetState(PlayerIndex, GamePadDeadZone);` — STRICT
- `static bool SetVibration(PlayerIndex, float, float);` — STRICT
- `static std::string GetGUIDEXT(PlayerIndex);` — EXT
- `static void SetLightBarEXT(PlayerIndex, const Color&);` — EXT
- `static bool SetTriggerVibrationEXT(PlayerIndex, float, float);` — EXT
- `static bool GetGyroEXT(PlayerIndex, Vector3&);` — EXT
- `static bool GetAccelerometerEXT(PlayerIndex, Vector3&);` — EXT
- `static int GetPlayerIndexEXT(PlayerIndex);` — CNAEXT/EXT (SDL device player-index / LED; -1 if disconnected)
- `static bool SetPlayerIndexEXT(PlayerIndex, int);` — CNAEXT/EXT
- `static CNA::Input::PowerStateEXT GetPowerInfoEXT(PlayerIndex, int& percent);` — CNAEXT/EXT (battery/charge; percent 0-100 or -1; Error if disconnected)
- `static CNA::Input::GamePadButtonLabelEXT GetButtonLabelEXT(PlayerIndex, Buttons);` — CNAEXT/EXT (face-button glyph; Unknown if disconnected/unlabeled)
- `static std::string GetNameEXT(PlayerIndex);` — CNAEXT/EXT (controller name; "" if disconnected)
- `static std::string GetPathEXT(PlayerIndex);` — CNAEXT/EXT (OS device path; "" if disconnected)
- `static std::string GetSerialEXT(PlayerIndex);` — CNAEXT/EXT (hardware serial; "" if unavailable)
- `static std::uint16_t GetFirmwareVersionEXT(PlayerIndex);` — CNAEXT/EXT (firmware version; 0 if unavailable)
- `static std::uint64_t GetSteamHandleEXT(PlayerIndex);` — CNAEXT/EXT (Steam Input handle; 0 if not a Steam controller)
- `static CNA::Input::GamePadConnectionStateEXT GetConnectionStateEXT(PlayerIndex);` — CNAEXT/EXT (wired/wireless; Unknown if disconnected)
- `static int GetTouchpadCountEXT(PlayerIndex);` — CNAEXT/EXT (touchpad count; 0 if disconnected/none)
- `static int GetTouchpadFingerCountEXT(PlayerIndex, int touchpad);` — CNAEXT/EXT (finger capacity; 0 if disconnected/out of range)
- `static bool GetTouchpadFingerEXT(PlayerIndex, int touchpad, int finger, bool& down, float& x, float& y, float& pressure);` — CNAEXT/EXT (finger contact/pos; false if unavailable)
- `static constexpr float LeftDeadZone;` — CNAEXT
- `static constexpr float RightDeadZone;` — CNAEXT
- `static constexpr float TriggerThreshold;` — CNAEXT
- `static float ExcludeAxisDeadZone(float, float);` — CNAEXT

### `GamePadState` — struct (XNA)
- `bool getIsConnectedProperty() const;` — STRICT
- `int getPacketNumberProperty() const;` — STRICT
- `void setPacketNumberProperty(int);` — CNAEXT
- `const GamePadButtons& getButtonsProperty() const;` — STRICT
- `const GamePadDPad& getDPadProperty() const;` — STRICT
- `const GamePadThumbSticks& getThumbSticksProperty() const;` — STRICT
- `const GamePadTriggers& getTriggersProperty() const;` — STRICT
- `GamePadState();` — CNAEXT
- `GamePadState(const GamePadThumbSticks&, const GamePadTriggers&, const GamePadButtons&, const GamePadDPad&);` — STRICT
- `GamePadState(const Vector2&, const Vector2&, float, float, std::initializer_list<Buttons>);` — STRICT
- `bool IsButtonDown(Buttons) const;` — STRICT
- `bool IsButtonUp(Buttons) const;` — STRICT
- `bool Equals(const GamePadState&) const;` — STRICT
- `int GetHashCode() const;` — STRICT
- `std::string ToString() const;` — STRICT
- `friend bool operator==(const GamePadState&, const GamePadState&);` — STRICT
- `friend bool operator!=(const GamePadState&, const GamePadState&);` — STRICT

### `GamePadButtons` — struct (XNA)
- Getters (STRICT): `getAProperty`, `getBProperty`, `getBackProperty`, `getXProperty`, `getYProperty`, `getStartProperty`, `getLeftShoulderProperty`, `getLeftStickProperty`, `getRightShoulderProperty`, `getRightStickProperty`, `getBigButtonProperty` — each `ButtonState (…)() const`
- `GamePadButtons();` — CNAEXT
- `explicit GamePadButtons(Buttons);` — STRICT
- `bool Equals(const GamePadButtons&) const;` — STRICT
- `int GetHashCode() const;` — STRICT
- `friend bool operator==` / `operator!=(const GamePadButtons&, const GamePadButtons&);` — STRICT
- `static GamePadButtons FromButtonArray(std::initializer_list<Buttons>);` — CNAEXT

### `GamePadDPad` — struct (XNA)
- Getters (STRICT): `getDownProperty`, `getLeftProperty`, `getRightProperty`, `getUpProperty` — each `ButtonState (…)() const`
- `GamePadDPad();` — CNAEXT
- `GamePadDPad(ButtonState, ButtonState, ButtonState, ButtonState);` — STRICT
- `static GamePadDPad FromButtonArray(std::initializer_list<Buttons>);` — CNAEXT
- `bool Equals(const GamePadDPad&) const;` — STRICT
- `int GetHashCode() const;` — STRICT
- `friend bool operator==` / `operator!=(const GamePadDPad&, const GamePadDPad&);` — STRICT

### `GamePadThumbSticks` — struct (XNA)
- `const Vector2& getLeftProperty() const;` / `const Vector2& getRightProperty() const;` — STRICT
- `GamePadThumbSticks();` — CNAEXT
- `GamePadThumbSticks(const Vector2&, const Vector2&);` — STRICT
- `bool Equals(const GamePadThumbSticks&) const;` — STRICT
- `int GetHashCode() const;` — STRICT
- `friend bool operator==` / `operator!=(const GamePadThumbSticks&, const GamePadThumbSticks&);` — STRICT

### `GamePadTriggers` — struct (XNA)
- `float getLeftProperty() const;` / `float getRightProperty() const;` — STRICT
- `GamePadTriggers();` — CNAEXT
- `GamePadTriggers(float, float);` — STRICT
- `bool Equals(const GamePadTriggers&) const;` — STRICT
- `int GetHashCode() const;` — STRICT
- `friend bool operator==` / `operator!=(const GamePadTriggers&, const GamePadTriggers&);` — STRICT

### `GamePadCapabilities` — struct (XNA + EXT)
- `GamePadCapabilities() = default;` — STRICT
- `bool getIsConnectedProperty() const;` (STRICT) + `void setIsConnectedProperty(bool);` (CNAEXT)
- For each capability below: a STRICT `bool get…Property() const` and a CNAEXT `void set…Property(bool)`:
  `HasAButton`, `HasBackButton`, `HasBButton`, `HasDPadDownButton`, `HasDPadLeftButton`,
  `HasDPadRightButton`, `HasDPadUpButton`, `HasLeftShoulderButton`, `HasLeftStickButton`,
  `HasRightShoulderButton`, `HasRightStickButton`, `HasStartButton`, `HasXButton`, `HasYButton`,
  `HasBigButton`, `HasLeftXThumbStick`, `HasLeftYThumbStick`, `HasRightXThumbStick`,
  `HasRightYThumbStick`, `HasLeftTrigger`, `HasRightTrigger`, `HasLeftVibrationMotor`,
  `HasRightVibrationMotor`, `HasVoiceSupport`
- `GamePadType getGamePadTypeProperty() const;` (STRICT) + `void setGamePadTypeProperty(GamePadType);` (CNAEXT)
- EXT capability pairs — STRICT-shaped `bool get…EXTProperty() const` / EXT `void set…EXTProperty(bool)` for:
  `HasLightBarEXT`, `HasTriggerVibrationMotorsEXT`, `HasMisc1EXT`, `HasPaddle1EXT`, `HasPaddle2EXT`,
  `HasPaddle3EXT`, `HasPaddle4EXT`, `HasTouchPadEXT`, `HasGyroEXT`, `HasAccelerometerEXT` — EXT

---

## Keyboard / Mouse cluster (`Microsoft::Xna::Framework::Input`)

### `Keyboard` — static class (XNA + EXT)
- `Keyboard() = delete;` — STRICT
- `static KeyboardState GetState();` — STRICT
- `static KeyboardState GetState(PlayerIndex);` — STRICT
- `static Keys GetKeyFromScancodeEXT(Keys);` — EXT
- `static CNA::Input::KeyModifiersEXT GetModStateEXT();` — CNAEXT/EXT (active Shift/Ctrl/Alt/Gui + Caps/Num/Scroll/Mode lock flags)
- `static std::string GetScancodeNameEXT(Keys);` — CNAEXT/EXT (physical key name; "" if none)
- `static Keys GetScancodeFromNameEXT(const std::string&);` — CNAEXT/EXT (inverse; Keys::None if unrecognized)
- `static std::string GetKeyNameEXT(Keys);` — CNAEXT/EXT (layout-dependent key name; "" if none)
- `static Keys GetKeyFromNameEXT(const std::string&);` — CNAEXT/EXT (inverse; Keys::None if unrecognized)

### `KeyboardState` — struct (XNA)
- `KeyboardState();` — CNAEXT
- `KeyboardState(std::initializer_list<Keys>);` — STRICT
- `explicit KeyboardState(const std::unordered_set<Keys>&);` — CNAEXT
- `KeyState getItem(Keys) const;` — STRICT
- `KeyState operator[](Keys) const;` — STRICT
- `bool IsKeyDown(Keys) const;` — STRICT
- `bool IsKeyUp(Keys) const;` — STRICT
- `std::vector<Keys> GetPressedKeys() const;` — STRICT
- `bool Equals(const KeyboardState&) const;` — STRICT
- `int GetHashCode() const;` — STRICT
- `std::string ToString() const;` — CNAEXT (FNA `KeyboardState` has no `ToString`; CNA convenience — INPUT-API-027)
- `friend bool operator==` / `operator!=(const KeyboardState&, const KeyboardState&);` — STRICT

### `Mouse` — static class (XNA + EXT + CNAEXT)
- `Mouse() = delete;` — STRICT
- `static std::uintptr_t getWindowHandleProperty();` / `static void setWindowHandleProperty(std::uintptr_t);` — STRICT
- `static MouseState GetState();` — STRICT
- `static void SetPosition(int, int);` — STRICT
- `static void SetCursor(MouseCursor&);` — CNAEXT
- `static System::MulticastAction<int> ClickedEXT;` — EXT
- `static bool getIsRelativeMouseModeEXTProperty();` / `static void setIsRelativeMouseModeEXTProperty(bool);` — EXT
- `static bool SetCaptureEXT(bool);` — CNAEXT/EXT (capture mouse outside window)
- `static void GetGlobalPositionEXT(int& x, int& y);` — CNAEXT/EXT (desktop-global cursor position)
- `static bool WarpGlobalEXT(int x, int y);` — CNAEXT/EXT (warp cursor in desktop-global coords)

### `MouseState` — struct (XNA)
- Getters (STRICT): `getXProperty`, `getYProperty` → `int`; `getLeftButtonProperty`, `getRightButtonProperty`, `getMiddleButtonProperty`, `getXButton1Property`, `getXButton2Property` → `ButtonState`; `getScrollWheelValueProperty` → `int`
- `getHorizontalScrollWheelValueEXTProperty() -> int;` — CNAEXT/EXT (SDL `wheel.x`; excluded from Equals/GetHashCode)
- `MouseState();` — CNAEXT
- `MouseState(int, int, int, ButtonState, ButtonState, ButtonState, ButtonState, ButtonState);` — STRICT
- `MouseState(int, int, int, ButtonState, ButtonState, ButtonState, ButtonState, ButtonState, int horizontalScrollWheel);` — CNAEXT/EXT
- `bool Equals(const MouseState&) const;` — STRICT
- `int GetHashCode() const;` — STRICT
- `std::string ToString() const;` — STRICT
- `friend bool operator==` / `operator!=(const MouseState&, const MouseState&);` — STRICT

### `MouseCursor` — class (CNA / CNAEXT; entire class is non-XNA)
- `MouseCursor();` — CNAEXT
- `static MouseCursor FromTexture2D(const Graphics::Texture2D&, int, int);` — CNAEXT
- `MouseCursor(const MouseCursor&) = delete;` / `operator=(const MouseCursor&) = delete;` — CNAEXT
- `MouseCursor(MouseCursor&&) noexcept;` / `operator=(MouseCursor&&) noexcept;` — CNAEXT
- `~MouseCursor() override;` — CNAEXT
- `void Dispose() override;` — CNAEXT
- Stock-cursor singletons (CNAEXT, each `static MouseCursor& get…Property()`): `Arrow`, `Crosshair`,
  `Hand`, `IBeam`, `No`, `SizeAll`, `SizeNESW`, `SizeNS`, `SizeNWSE`, `SizeWE`, `Wait`, `WaitArrow`

### `TextInputEXT` — static class (FNA EXT)
- `TextInputEXT() = delete;` — CNAEXT
- `static System::MulticastAction<charcs> TextInput;` — EXT
- `static System::MulticastAction<const std::string&, int, int> TextEditing;` — EXT
- `static System::MulticastAction<const std::vector<std::string>&, int, bool> TextEditingCandidatesEXT;` — CNAEXT/EXT (IME candidate list)
- `static std::uintptr_t getWindowHandleProperty();` / `static void setWindowHandleProperty(std::uintptr_t);` — EXT
- `static bool IsTextInputActive();` — EXT
- `static bool IsScreenKeyboardShown();` — EXT
- `static bool IsScreenKeyboardShown(std::uintptr_t);` — EXT
- `static void StartTextInput();` — EXT
- `static void StopTextInput();` — EXT
- `static void SetInputRectangle(const Rectangle&);` — EXT
- `static void StartTextInputWithTypeEXT(CNA::Input::TextInputTypeEXT);` — CNAEXT/EXT (input-type hint for on-screen keyboard / IME)

---

## Touch cluster (`Microsoft::Xna::Framework::Input::Touch`)

### `TouchPanel` — static class (XNA + CNAEXT)
- `TouchPanel() = delete;` — CNAEXT
- `static constexpr intcs MAX_TOUCHES;` / `static constexpr intcs NO_FINGER;` — CNAEXT
- Property pairs (STRICT get `intcs`/`GestureType`/`DisplayOrientation`/`std::uintptr_t`; CNAEXT set):
  `DisplayWidth`, `DisplayHeight`, `DisplayOrientation`, `EnabledGestures`, `WindowHandle`
- `bool getIsGestureAvailableProperty();` — STRICT
- `bool getTouchDeviceExistsProperty();` / `void setTouchDeviceExistsProperty(bool);` — CNAEXT
- `static TouchPanelCapabilities GetCapabilities();` — STRICT
- `static TouchCollection GetState();` — STRICT
- `static GestureSample ReadGesture();` — STRICT
- `static void EnqueueGesture(const GestureSample&);` — CNAEXT
- `static void SetFinger(intcs, intcs, const Vector2&);` — CNAEXT
- `static void Update();` — CNAEXT

### `TouchCollection` — struct (XNA `IList<TouchLocation>`)
- `int getCountProperty() const;` — STRICT
- `bool getIsConnectedProperty() const;` — STRICT
- `bool getIsReadOnlyProperty() const;` — STRICT
- `TouchCollection();` — CNAEXT
- `explicit TouchCollection(const std::vector<TouchLocation>&);` — STRICT
- `explicit TouchCollection(std::vector<TouchLocation>&&);` — STRICT
- `TouchLocation& operator[](std::size_t);` / `const TouchLocation& operator[](std::size_t) const;` — STRICT
- `bool empty() const;` — CNAEXT
- `bool Contains(const TouchLocation&) const;` — STRICT
- `bool FindById(int, TouchLocation&) const;` — STRICT
- `void CopyTo(std::vector<TouchLocation>&, int) const;` — STRICT
- `int IndexOf(const TouchLocation&) const;` — STRICT
- `void Add(const TouchLocation&);` — STRICT
- `void Clear();` — STRICT
- `bool Remove(const TouchLocation&);` — STRICT
- `void RemoveAt(int);` — STRICT
- `void Insert(int, const TouchLocation&);` — STRICT
- `begin()` / `end()` — CNAEXT (mutable + const overloads → `std::vector<TouchLocation>::(const_)iterator`)

### `TouchLocation` — struct (XNA)
- `int getIdProperty() const;` — STRICT
- `TouchLocationState getStateProperty() const;` — STRICT
- `const Vector2& getPositionProperty() const;` — STRICT
- `float getPressureEXT() const;` — CNAEXT/EXT (SDL finger pressure 0..1; excluded from Equals/GetHashCode/ToString)
- `TouchLocation();` — CNAEXT
- `TouchLocation(int, TouchLocationState, const Vector2&);` — STRICT
- `TouchLocation(int, TouchLocationState, const Vector2&, TouchLocationState, const Vector2&);` — STRICT
- `TouchLocation(int, TouchLocationState, const Vector2&, float);` — CNAEXT/EXT (pressure)
- `TouchLocation(int, TouchLocationState, const Vector2&, TouchLocationState, const Vector2&, float);` — CNAEXT/EXT (pressure)
- `bool TryGetPreviousLocation(TouchLocation&) const;` — STRICT
- `bool Equals(const TouchLocation&) const;` — STRICT
- `int GetHashCode() const;` — STRICT
- `std::string ToString() const;` — STRICT
- `friend bool operator==` / `operator!=(const TouchLocation&, const TouchLocation&);` — STRICT

### `TouchPanelCapabilities` — struct (XNA)
- `bool getIsConnectedProperty() const;` — STRICT
- `int getMaximumTouchCountProperty() const;` — STRICT
- `TouchPanelCapabilities();` — CNAEXT
- `TouchPanelCapabilities(bool, int);` — CNAEXT

### `GestureSample` — struct (XNA + EXT)
- `GestureType getGestureTypeProperty() const;` — STRICT
- `System::TimeSpan getTimestampProperty() const;` — STRICT
- `const Vector2& getPositionProperty() const;` / `getPosition2Property` / `getDeltaProperty` / `getDelta2Property` — STRICT
- `int getFingerIdEXTProperty() const;` / `int getFingerId2EXTProperty() const;` — EXT
- `GestureSample();` — CNAEXT
- `GestureSample(GestureType, System::TimeSpan, Vector2, Vector2, Vector2, Vector2);` — STRICT
- `GestureSample(GestureType, System::TimeSpan, Vector2, Vector2, Vector2, Vector2, int, int);` — EXT
