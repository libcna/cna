# Frozen public Input API surface (INPUT-API-031)

This is the **golden signature snapshot** of the public `Microsoft::Xna::Framework::Input` (and
`…::Input::Touch`) surface. It is the human-readable companion to the enforced compile-time guard
`tests/Microsoft/Xna/Framework/Input/PublicApiInputSignatureFreezeTests.cpp`, which pins every entry
below through a fully-spelled function-/member-pointer type or a type trait. Any drift — a removed or
renamed member, a changed signature, a changed constructor parameter list — fails to compile.

**Keep the two in lock-step.** When you intentionally add, remove, or change a public member, update
**both** this document and the freeze test in the same commit.

## Classification

- **STRICT** — part of the XNA 4.0 API; must match XNA/FNA exactly (values, names, signatures).
- **EXT** — FNA-compatible extension (name ends in `EXT`); consumer-visible, not in stock XNA.
- **NOXNA** — CNA/MonoGame convenience that is public but has no XNA equivalent.

## Freeze scope

Frozen: all STRICT, EXT, and stable NOXNA-convenience members of every public Input type.

Deliberately **excluded** (internal plumbing exposed as public for the SDL bridge / tests, not API —
so the golden file is not coupled to internal churn):

- `TouchPanel::INTERNAL_onTouchEvent`, `TouchPanel::ResetForTests`
- `Mouse::INTERNAL_onClicked`, `Mouse::ResetForTests`
- `TextInputEXT::INTERNAL_OnTextInput`, `TextInputEXT::INTERNAL_OnTextEditing`, `TextInputEXT::ResetForTests`
- private data members (e.g. `GamePadButtons::buttons_`)

Hidden-friend equality operators (`operator==` / `operator!=` on the value structs) are frozen via an
ADL expression (`a == b` / `a != b` yielding `bool`) rather than an address-of, because they are not
visible to qualified lookup.

The pure **enums** (`Keys`, `Buttons`, `ButtonState`, `KeyState`, `GamePadType`, `GamePadDeadZone`,
`TouchLocationState`, `GestureType`) are value-frozen separately and exhaustively by INPUT-KBD-001 /
INPUT-TEST-001 under the enum-ABI guardrail **INPUT-API-034**; they are not repeated here.

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
- `static constexpr float LeftDeadZone;` — NOXNA
- `static constexpr float RightDeadZone;` — NOXNA
- `static constexpr float TriggerThreshold;` — NOXNA
- `static float ExcludeAxisDeadZone(float, float);` — NOXNA

### `GamePadState` — struct (XNA)
- `bool getIsConnectedProperty() const;` — STRICT
- `int getPacketNumberProperty() const;` — STRICT
- `void setPacketNumberProperty(int);` — NOXNA
- `const GamePadButtons& getButtonsProperty() const;` — STRICT
- `const GamePadDPad& getDPadProperty() const;` — STRICT
- `const GamePadThumbSticks& getThumbSticksProperty() const;` — STRICT
- `const GamePadTriggers& getTriggersProperty() const;` — STRICT
- `GamePadState();` — STRICT
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
- `GamePadButtons();` — NOXNA
- `explicit GamePadButtons(Buttons);` — STRICT
- `bool Equals(const GamePadButtons&) const;` — STRICT
- `int GetHashCode() const;` — STRICT
- `friend bool operator==` / `operator!=(const GamePadButtons&, const GamePadButtons&);` — STRICT
- `static GamePadButtons FromButtonArray(std::initializer_list<Buttons>);` — NOXNA

### `GamePadDPad` — struct (XNA)
- Getters (STRICT): `getDownProperty`, `getLeftProperty`, `getRightProperty`, `getUpProperty` — each `ButtonState (…)() const`
- `GamePadDPad();` — NOXNA
- `GamePadDPad(ButtonState, ButtonState, ButtonState, ButtonState);` — STRICT
- `static GamePadDPad FromButtonArray(std::initializer_list<Buttons>);` — NOXNA
- `bool Equals(const GamePadDPad&) const;` — STRICT
- `int GetHashCode() const;` — STRICT
- `friend bool operator==` / `operator!=(const GamePadDPad&, const GamePadDPad&);` — STRICT

### `GamePadThumbSticks` — struct (XNA)
- `const Vector2& getLeftProperty() const;` / `const Vector2& getRightProperty() const;` — STRICT
- `GamePadThumbSticks();` — NOXNA
- `GamePadThumbSticks(const Vector2&, const Vector2&);` — STRICT
- `bool Equals(const GamePadThumbSticks&) const;` — STRICT
- `int GetHashCode() const;` — STRICT
- `friend bool operator==` / `operator!=(const GamePadThumbSticks&, const GamePadThumbSticks&);` — STRICT

### `GamePadTriggers` — struct (XNA)
- `float getLeftProperty() const;` / `float getRightProperty() const;` — STRICT
- `GamePadTriggers();` — NOXNA
- `GamePadTriggers(float, float);` — STRICT
- `bool Equals(const GamePadTriggers&) const;` — STRICT
- `int GetHashCode() const;` — STRICT
- `friend bool operator==` / `operator!=(const GamePadTriggers&, const GamePadTriggers&);` — STRICT

### `GamePadCapabilities` — struct (XNA + EXT)
- `GamePadCapabilities() = default;` — STRICT
- `bool getIsConnectedProperty() const;` (STRICT) + `void setIsConnectedProperty(bool);` (NOXNA)
- For each capability below: a STRICT `bool get…Property() const` and a NOXNA `void set…Property(bool)`:
  `HasAButton`, `HasBackButton`, `HasBButton`, `HasDPadDownButton`, `HasDPadLeftButton`,
  `HasDPadRightButton`, `HasDPadUpButton`, `HasLeftShoulderButton`, `HasLeftStickButton`,
  `HasRightShoulderButton`, `HasRightStickButton`, `HasStartButton`, `HasXButton`, `HasYButton`,
  `HasBigButton`, `HasLeftXThumbStick`, `HasLeftYThumbStick`, `HasRightXThumbStick`,
  `HasRightYThumbStick`, `HasLeftTrigger`, `HasRightTrigger`, `HasLeftVibrationMotor`,
  `HasRightVibrationMotor`, `HasVoiceSupport`
- `GamePadType getGamePadTypeProperty() const;` (STRICT) + `void setGamePadTypeProperty(GamePadType);` (NOXNA)
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

### `KeyboardState` — struct (XNA)
- `KeyboardState();` — NOXNA
- `KeyboardState(std::initializer_list<Keys>);` — STRICT
- `explicit KeyboardState(const std::unordered_set<Keys>&);` — NOXNA
- `KeyState getItem(Keys) const;` — STRICT
- `KeyState operator[](Keys) const;` — STRICT
- `bool IsKeyDown(Keys) const;` — STRICT
- `bool IsKeyUp(Keys) const;` — STRICT
- `std::vector<Keys> GetPressedKeys() const;` — STRICT
- `bool Equals(const KeyboardState&) const;` — STRICT
- `int GetHashCode() const;` — STRICT
- `std::string ToString() const;` — STRICT
- `friend bool operator==` / `operator!=(const KeyboardState&, const KeyboardState&);` — STRICT

### `Mouse` — static class (XNA + EXT + NOXNA)
- `Mouse() = delete;` — STRICT
- `static std::uintptr_t getWindowHandleProperty();` / `static void setWindowHandleProperty(std::uintptr_t);` — STRICT
- `static MouseState GetState();` — STRICT
- `static void SetPosition(int, int);` — STRICT
- `static void SetCursor(MouseCursor&);` — NOXNA
- `static System::MulticastAction<int> ClickedEXT;` — EXT
- `static bool getIsRelativeMouseModeEXTProperty();` / `static void setIsRelativeMouseModeEXTProperty(bool);` — EXT

### `MouseState` — struct (XNA)
- Getters (STRICT): `getXProperty`, `getYProperty` → `int`; `getLeftButtonProperty`, `getRightButtonProperty`, `getMiddleButtonProperty`, `getXButton1Property`, `getXButton2Property` → `ButtonState`; `getScrollWheelValueProperty` → `int`
- `MouseState();` — NOXNA
- `MouseState(int, int, int, ButtonState, ButtonState, ButtonState, ButtonState, ButtonState);` — STRICT
- `bool Equals(const MouseState&) const;` — STRICT
- `int GetHashCode() const;` — STRICT
- `std::string ToString() const;` — STRICT
- `friend bool operator==` / `operator!=(const MouseState&, const MouseState&);` — STRICT

### `MouseCursor` — class (CNA / NOXNA; entire class is non-XNA)
- `MouseCursor();` — NOXNA
- `explicit MouseCursor(SDL_Cursor*, bool = false);` — NOXNA (SDL type is opaque/forward-declared)
- `static MouseCursor FromTexture2D(const Graphics::Texture2D&, int, int);` — NOXNA
- `MouseCursor(const MouseCursor&) = delete;` / `operator=(const MouseCursor&) = delete;` — NOXNA
- `MouseCursor(MouseCursor&&) noexcept;` / `operator=(MouseCursor&&) noexcept;` — NOXNA
- `~MouseCursor() override;` — NOXNA
- `void Dispose() override;` — NOXNA
- `SDL_Cursor* GetSDLCursor() const;` — NOXNA
- Stock-cursor singletons (NOXNA, each `static MouseCursor& get…Property()`): `Arrow`, `Crosshair`,
  `Hand`, `IBeam`, `No`, `SizeAll`, `SizeNESW`, `SizeNS`, `SizeNWSE`, `SizeWE`, `Wait`, `WaitArrow`

### `TextInputEXT` — static class (FNA EXT)
- `TextInputEXT() = delete;` — NOXNA
- `static System::MulticastAction<charcs> TextInput;` — EXT
- `static System::MulticastAction<const std::string&, int, int> TextEditing;` — EXT
- `static std::uintptr_t getWindowHandleProperty();` / `static void setWindowHandleProperty(std::uintptr_t);` — EXT
- `static bool IsTextInputActive();` — EXT
- `static bool IsScreenKeyboardShown();` — EXT
- `static bool IsScreenKeyboardShown(std::uintptr_t);` — EXT
- `static void StartTextInput();` — EXT
- `static void StopTextInput();` — EXT
- `static void SetInputRectangle(const Rectangle&);` — EXT

---

## Touch cluster (`Microsoft::Xna::Framework::Input::Touch`)

### `TouchPanel` — static class (XNA + NOXNA)
- `TouchPanel() = delete;` — NOXNA
- `static constexpr intcs MAX_TOUCHES;` / `static constexpr intcs NO_FINGER;` — NOXNA
- Property pairs (STRICT get `intcs`/`GestureType`/`DisplayOrientation`/`std::uintptr_t`; NOXNA set):
  `DisplayWidth`, `DisplayHeight`, `DisplayOrientation`, `EnabledGestures`, `WindowHandle`
- `bool getIsGestureAvailableProperty();` — STRICT
- `bool getTouchDeviceExistsProperty();` / `void setTouchDeviceExistsProperty(bool);` — NOXNA
- `static TouchPanelCapabilities GetCapabilities();` — STRICT
- `static TouchCollection GetState();` — STRICT
- `static GestureSample ReadGesture();` — STRICT
- `static void EnqueueGesture(const GestureSample&);` — NOXNA
- `static void SetFinger(intcs, intcs, const Vector2&);` — NOXNA
- `static void Update();` — NOXNA

### `TouchCollection` — struct (XNA `IList<TouchLocation>`)
- `int getCountProperty() const;` — STRICT
- `bool getIsConnectedProperty() const;` — STRICT
- `bool getIsReadOnlyProperty() const;` — STRICT
- `TouchCollection();` — NOXNA
- `explicit TouchCollection(const std::vector<TouchLocation>&);` — STRICT
- `explicit TouchCollection(std::vector<TouchLocation>&&);` — STRICT
- `TouchLocation& operator[](std::size_t);` / `const TouchLocation& operator[](std::size_t) const;` — STRICT
- `bool empty() const;` — NOXNA
- `bool Contains(const TouchLocation&) const;` — STRICT
- `bool FindById(int, TouchLocation&) const;` — STRICT
- `void CopyTo(std::vector<TouchLocation>&, int) const;` — STRICT
- `int IndexOf(const TouchLocation&) const;` — STRICT
- `void Add(const TouchLocation&);` — STRICT
- `void Clear();` — STRICT
- `bool Remove(const TouchLocation&);` — STRICT
- `void RemoveAt(int);` — STRICT
- `void Insert(int, const TouchLocation&);` — STRICT
- `begin()` / `end()` — NOXNA (mutable + const overloads → `std::vector<TouchLocation>::(const_)iterator`)

### `TouchLocation` — struct (XNA)
- `int getIdProperty() const;` — STRICT
- `TouchLocationState getStateProperty() const;` — STRICT
- `const Vector2& getPositionProperty() const;` — STRICT
- `TouchLocation();` — NOXNA
- `TouchLocation(int, TouchLocationState, const Vector2&);` — STRICT
- `TouchLocation(int, TouchLocationState, const Vector2&, TouchLocationState, const Vector2&);` — STRICT
- `bool TryGetPreviousLocation(TouchLocation&) const;` — STRICT
- `bool Equals(const TouchLocation&) const;` — STRICT
- `int GetHashCode() const;` — STRICT
- `std::string ToString() const;` — STRICT
- `friend bool operator==` / `operator!=(const TouchLocation&, const TouchLocation&);` — STRICT

### `TouchPanelCapabilities` — struct (XNA)
- `bool getIsConnectedProperty() const;` — STRICT
- `int getMaximumTouchCountProperty() const;` — STRICT
- `TouchPanelCapabilities();` — NOXNA
- `TouchPanelCapabilities(bool, int);` — NOXNA

### `GestureSample` — struct (XNA + EXT)
- `GestureType getGestureTypeProperty() const;` — STRICT
- `System::TimeSpan getTimestampProperty() const;` — STRICT
- `const Vector2& getPositionProperty() const;` / `getPosition2Property` / `getDeltaProperty` / `getDelta2Property` — STRICT
- `int getFingerIdEXTProperty() const;` / `int getFingerId2EXTProperty() const;` — EXT
- `GestureSample();` — STRICT
- `GestureSample(GestureType, System::TimeSpan, Vector2, Vector2, Vector2, Vector2);` — STRICT
- `GestureSample(GestureType, System::TimeSpan, Vector2, Vector2, Vector2, Vector2, int, int);` — EXT
