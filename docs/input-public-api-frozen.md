# Input public API and signature guards (INPUT-API-031)

> **Related input docs (INP-0003):** [backend](input-backend.md) · [FNA fidelity + deviations](input-fna-fidelity.md) · [member-parity matrix](input-member-parity-matrix.md) · [test coverage](input-test-coverage.md) · [build & test](input-build-and-test.md) · [platform notes](platform-input-notes.md) · [manual results](input-manual-verification-results.md) · [demo checklist](demo-input-checklist.md)

This is the human-readable native Input surface and classification reference, checked against
`modules/input/include/Microsoft/Xna/Framework/Input/` on 2026-11-01. Its compile-time companion is
`modules/input/tests/Microsoft/Xna/Framework/Input/PublicApiInputSignatureFreezeTests.cpp`.
The guard checks its listed signatures/traits; it is not an exhaustive inventory of the current
surface and does not reject arbitrary added members or overloads.

Keep documentation and signature checks together when changing a covered member. New public
members should receive an explicit classification and appropriate guard/behavior coverage.
Generated presence belongs to [xna-4-runtime-member-coverage.md](xna-4-runtime-member-coverage.md);
a successful signature build alone is not a behavior or hardware-parity result.

## Classification — canonical API-tier glossary (INP-0001)

API origin and compatibility behavior are related but separate:

- **STRICT** — part of XNA 4.0, using CNA's C++ property/type/reference conventions.
  Measured Microsoft XNA behavior wins where it differs from FNA; see
  [xna-fna-behavior-decisions.md](xna-fna-behavior-decisions.md).
- **FNA-compatible** — a behavior qualifier on an XNA-shaped member, not a disjoint API tier.
  FNA is the usual readable reference where no measured XNA disagreement exists.
- **FNAEXT / EXT** — an extension originating in FNA, with an EXT name and CNAEXT marker.
- **CNAEXT** — CNA-specific/MonoGame-style conveniences absent from XNA/FNA. Some also have an
  EXT suffix; that suffix alone does not establish FNA origin.
- **INTERNAL** — implementation plumbing, including SdlInputBridge, InputManager,
  GestureDetector and SystemInputAccess. Detail/internal public-header hooks are not a supported
  additional XNA game surface.

CNAEXT is the marker for non-XNA declarations in the Microsoft namespace, including FNA extensions.
An entirely non-XNA class can carry it at class level. Existing enum extensions use EXT names.
Explicit value default constructors, native convenience factories and non-XNA setters are marked;
copy/move overloads representing one reference member are C++ mappings, not automatically new API.

The 2026-07 campaign's tagging fixes and completed task log are retained in Git history.

## Freeze scope and known gaps

The signature TU includes public headers, takes typed member/function addresses and checks
constructibility, equality-expression result types and special-member traits. Different checks
provide different strength: constructibility permits conversions, move traits do not pin noexcept,
and an event's untyped address does not pin its full callback type. These checks do not prove
behavior or detect every possible declaration change.

The 2026-11-01 comparison found current members outside that TU:

| Present public surface | Missing signature checks |
|---|---|
| GamePad keyboard-emulation query/setter | Both methods |
| GamePadState, GamePadButtons, GamePadDPad, GamePadThumbSticks, GamePadTriggers, KeyboardState, MouseState, TouchLocation | Equals(const std::any&) overloads |
| GamePadButtons, GamePadDPad, GamePadThumbSticks, GamePadTriggers | ToString |
| TouchPanel | Mouse-touch-emulation and input-suppression query/setter pairs |
| TouchCollection | GetEnumerator and nested Enumerator API |

These are guard-coverage gaps, not evidence that the APIs or behavior are absent. They remain
visible below instead of being described as already mechanically frozen.

Deliberately excluded plumbing includes INTERNAL_* methods, ResetForTests and private data.
XmlSerializationEXT's enum metadata is a separate opt-in serialization surface, outside the
listed signature TU. Pure enum values are checked in their enum suites; this page does not repeat
their numeric lists.

## GetTypeName and header containment

PublicApiInputCompileTests includes public Input headers, checks SDL include leakage, namespace
placement and the non-Object base status of its 18 listed top-level class/struct types.
MouseCursor implements IDisposable without deriving System::Object. None of those listed types
therefore requires an Object GetTypeName override; this statement is scoped to that checked set,
not a reflection-based check of future/nested types.

The compile guard is not a general parser that rejects every possible internal type name in a
new signature. Keep public declarations platform-neutral and inspect new additions directly.

---

## Shared value overloads

GamePadState, GamePadButtons, GamePadDPad, GamePadThumbSticks, GamePadTriggers, KeyboardState,
MouseState and TouchLocation also declare `bool Equals(const std::any&) const` as the native
mapping of object equality (STRICT). GamePadButtons, GamePadDPad, GamePadThumbSticks and
GamePadTriggers additionally declare `std::string ToString() const` (STRICT). These supplement
the per-type lists below and are among the signature-guard gaps above.

## GamePad cluster (`Microsoft::Xna::Framework::Input`)

### `GamePad` — static class (XNA + EXT)
- `GamePad() = delete;` — STRICT
- `static void setKeyboardEmulationEnabledEXT(bool);` / `static bool getKeyboardEmulationEnabledEXT();` — CNAEXT; not yet in the signature guard
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
- EXT capability pairs — `bool get…EXTProperty() const` / `void set…EXTProperty(bool)` for:
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
- `TouchPanel() = delete;` — C++ static-class mapping
- `static constexpr intcs MAX_TOUCHES;` / `static constexpr intcs NO_FINGER;` — CNAEXT
- Static property pairs (STRICT getters and setters, using `intcs`/`GestureType`/`DisplayOrientation`/`std::uintptr_t`):
  `DisplayWidth`, `DisplayHeight`, `DisplayOrientation`, `EnabledGestures`, `WindowHandle`
- `static bool getIsGestureAvailableProperty();` — STRICT
- `static bool getTouchDeviceExistsProperty();` / `static void setTouchDeviceExistsProperty(bool);` — CNAEXT
- `static bool getMouseTouchEmulationEnabledEXT();` / `static void setMouseTouchEmulationEnabledEXT(bool);` — CNAEXT; not yet in the signature guard
- `static bool getInputSuppressedEXT();` / `static void setInputSuppressedEXT(bool);` — CNAEXT system-input plumbing; not yet in the signature guard
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
- `Enumerator GetEnumerator() const;` — STRICT; not yet in the signature guard

### `TouchCollection::Enumerator` — nested struct (XNA IEnumerator mapping)

- `Enumerator() = default;` — native default construction
- `const TouchLocation& Current() const;` — STRICT mapping
- `bool MoveNext();` / `void Dispose();` — STRICT mapping
- `void Reset();` — CNAEXT exposure of the explicit interface member

The collection-taking constructor is private. All of this nested surface is currently outside
the signature TU; see [enumerator reference](xna-4-enumerator-reference.md).

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

## Audit verification

On 2026-11-01, CnaInputModuleTests and the CnaTests cross-module harness build in the existing
multi-renderer tree. Both compile/link guards and all 32 keyboard-gamepad cases pass, followed by
the three Game-pump/orientation tests on private displays. This verifies the checks actually in
the guard, not the missing entries identified above. No API declarations or test guard were changed.
