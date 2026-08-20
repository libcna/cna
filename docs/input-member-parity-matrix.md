# Input member-level parity matrix (INPUT-API-027)

> **Related input docs (INP-0003):** [plan](../plans/plan_input.md) · [backend](input-backend.md) · [FNA fidelity + deviations](input-fna-fidelity.md) · [member-parity matrix](input-member-parity-matrix.md) · [frozen API + tier glossary](input-public-api-frozen.md) · [test coverage](input-test-coverage.md) · [build & test](input-build-and-test.md) · [platform notes](platform-input-notes.md) · [manual results](input-manual-verification-results.md) · [demo checklist](demo-input-checklist.md)

> **Generated** by `tools/input_parity/gen_input_parity_matrix.py` from the public
> `Microsoft::Xna::Framework::Input` (+ `::Touch`) headers and the FNA reference
> at `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`. Do not hand-edit — re-run the generator. This is a review aid;
> exact signatures stay pinned by `PublicApiInputSignatureFreezeTests.cpp`
> (INPUT-API-031) and enum values by INPUT-API-034.

Tags: **STRICT** = XNA 4.0 API (must match FNA) · **EXT** = FNA-compatible extension (`…EXT`) · **CNAEXT** = CNA-only convenience.

The `In FNA` column: `yes` = a `public` FNA member matches by name; `internal` = matches an FNA `internal` member (expected for a CNA `CNAEXT` that surfaces FNA-internal plumbing); `no` = no FNA member of that name (expected for `= delete`/`= default` C++ idioms and EXT).

## Review summary

No STRICT/EXT member is missing an FNA counterpart, and no FNA public member is
missing a CNA counterpart (by the heuristic name mapping, after accounting for the
documented collection-interface deviations below). Full per-type tables follow.

- FNA `IList<T>`/`IEnumerator`/`IDisposable` plumbing intentionally **not** mirrored by CNA's value-type collections (by design, not a gap): **4** — `TouchCollection.Current`, `TouchCollection.GetEnumerator`, `TouchCollection.MoveNext`, `TouchCollection.Dispose`

> Rows flagged above are heuristic (name-level) and may be false positives — e.g. a
> C++ `ref`/`&&` overload pair mapping one C# member, an FNA `internal` surfaced as CNA
> CNAEXT, or an equality operator resolved via ADL. Review each against the .cs before acting.

## `ButtonState` — enum (FNA `ButtonState.cs`)

| Member | Tag | In FNA | Note |
|--------|-----|--------|------|
| `Released` | STRICT | yes | matched |
| `Pressed` | STRICT | yes | matched |

## `Buttons` — enum (FNA `Buttons.cs`)

| Member | Tag | In FNA | Note |
|--------|-----|--------|------|
| `DPadUp` | STRICT | yes | matched |
| `DPadDown` | STRICT | yes | matched |
| `DPadLeft` | STRICT | yes | matched |
| `DPadRight` | STRICT | yes | matched |
| `Start` | STRICT | yes | matched |
| `Back` | STRICT | yes | matched |
| `LeftStick` | STRICT | yes | matched |
| `RightStick` | STRICT | yes | matched |
| `LeftShoulder` | STRICT | yes | matched |
| `RightShoulder` | STRICT | yes | matched |
| `BigButton` | STRICT | yes | matched |
| `A` | STRICT | yes | matched |
| `B` | STRICT | yes | matched |
| `X` | STRICT | yes | matched |
| `Y` | STRICT | yes | matched |
| `LeftThumbstickLeft` | STRICT | yes | matched |
| `RightTrigger` | STRICT | yes | matched |
| `LeftTrigger` | STRICT | yes | matched |
| `RightThumbstickUp` | STRICT | yes | matched |
| `RightThumbstickDown` | STRICT | yes | matched |
| `RightThumbstickRight` | STRICT | yes | matched |
| `RightThumbstickLeft` | STRICT | yes | matched |
| `LeftThumbstickUp` | STRICT | yes | matched |
| `LeftThumbstickDown` | STRICT | yes | matched |
| `LeftThumbstickRight` | STRICT | yes | matched |
| `Misc1EXT` | EXT | yes | matched |
| `Paddle1EXT` | EXT | yes | matched |
| `Paddle2EXT` | EXT | yes | matched |
| `Paddle3EXT` | EXT | yes | matched |
| `Paddle4EXT` | EXT | yes | matched |
| `TouchPadEXT` | EXT | yes | matched |

## `GamePad` — class (FNA `GamePad.cs`)

| Member | Tag | In FNA | Note |
|--------|-----|--------|------|
| `GamePad() = delete` | STRICT | no | C++ special-member idiom (no XNA counterpart expected) |
| `static GamePadCapabilities GetCapabilities(PlayerIndex playerIndex)` | STRICT | yes | matched |
| `static GamePadState GetState(PlayerIndex playerIndex)` | STRICT | yes | matched |
| `static GamePadState GetState(PlayerIndex playerIndex, GamePadDeadZone deadZoneMode)` | STRICT | yes | matched |
| `static bool SetVibration(PlayerIndex playerIndex, float leftMotor, float rightMotor)` | STRICT | yes | matched |
| `CNAEXT static std::string GetGUIDEXT(PlayerIndex playerIndex)` | EXT | yes | matched |
| `CNAEXT static void SetLightBarEXT(PlayerIndex playerIndex, const Microsoft::Xna::Framework::Color& color)` | EXT | yes | matched |
| `CNAEXT static bool SetTriggerVibrationEXT(PlayerIndex playerIndex, float leftTrigger, float rightTrigger)` | EXT | yes | matched |
| `CNAEXT static bool GetGyroEXT(PlayerIndex playerIndex, Microsoft::Xna::Framework::Vector3& gyro)` | EXT | yes | matched |
| `CNAEXT static bool GetAccelerometerEXT(PlayerIndex playerIndex, Microsoft::Xna::Framework::Vector3& accel)` | EXT | yes | matched |
| `CNAEXT static int GetPlayerIndexEXT(PlayerIndex playerIndex)` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `CNAEXT static bool SetPlayerIndexEXT(PlayerIndex playerIndex, int index)` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `CNAEXT static CNA::Input::PowerStateEXT GetPowerInfoEXT(PlayerIndex playerIndex, int& percent)` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `CNAEXT static CNA::Input::GamePadButtonLabelEXT GetButtonLabelEXT(PlayerIndex playerIndex, Buttons button)` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `CNAEXT static std::string GetNameEXT(PlayerIndex playerIndex)` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `CNAEXT static std::string GetPathEXT(PlayerIndex playerIndex)` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `CNAEXT static std::string GetSerialEXT(PlayerIndex playerIndex)` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `CNAEXT static std::uint16_t GetFirmwareVersionEXT(PlayerIndex playerIndex)` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `CNAEXT static std::uint64_t GetSteamHandleEXT(PlayerIndex playerIndex)` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `CNAEXT static CNA::Input::GamePadConnectionStateEXT GetConnectionStateEXT(PlayerIndex playerIndex)` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `CNAEXT static int GetTouchpadCountEXT(PlayerIndex playerIndex)` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `CNAEXT static int GetTouchpadFingerCountEXT(PlayerIndex playerIndex, int touchpad)` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `CNAEXT static bool GetTouchpadFingerEXT(PlayerIndex playerIndex, int touchpad, int finger, bool& down, float& x, float& y, float& pressure)` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `CNAEXT static constexpr float LeftDeadZone = 7849.0f / 32768.0f` | CNAEXT | internal | maps FNA non-public |
| `CNAEXT static constexpr float RightDeadZone = 8689.0f / 32768.0f` | CNAEXT | internal | maps FNA non-public |
| `CNAEXT static constexpr float TriggerThreshold = 30.0f / 255.0f` | CNAEXT | internal | maps FNA non-public |
| `CNAEXT static float ExcludeAxisDeadZone(float value, float deadZone)` | CNAEXT | internal | maps FNA non-public |

## `GamePadButtons` — struct (FNA `GamePadButtons.cs`)

| Member | Tag | In FNA | Note |
|--------|-----|--------|------|
| `ButtonState getAProperty() const` | STRICT | yes | matched |
| `ButtonState getBProperty() const` | STRICT | yes | matched |
| `ButtonState getBackProperty() const` | STRICT | yes | matched |
| `ButtonState getXProperty() const` | STRICT | yes | matched |
| `ButtonState getYProperty() const` | STRICT | yes | matched |
| `ButtonState getStartProperty() const` | STRICT | yes | matched |
| `ButtonState getLeftShoulderProperty() const` | STRICT | yes | matched |
| `ButtonState getLeftStickProperty() const` | STRICT | yes | matched |
| `ButtonState getRightShoulderProperty() const` | STRICT | yes | matched |
| `ButtonState getRightStickProperty() const` | STRICT | yes | matched |
| `ButtonState getBigButtonProperty() const` | STRICT | yes | matched |
| `CNAEXT GamePadButtons()` | CNAEXT | yes | maps FNA non-public |
| `explicit GamePadButtons(Buttons buttons)` | STRICT | yes | matched |
| `CNAEXT static GamePadButtons FromButtonArray(std::initializer_list<Buttons> btns)` | CNAEXT | internal | maps FNA non-public |
| `bool Equals(const GamePadButtons& other) const` | STRICT | yes | matched |
| `int GetHashCode() const` | STRICT | yes | matched |
| `friend bool operator==(const GamePadButtons& left, const GamePadButtons& right)` | STRICT | yes | matched |
| `friend bool operator!=(const GamePadButtons& left, const GamePadButtons& right)` | STRICT | yes | matched |

## `GamePadCapabilities` — struct (FNA `GamePadCapabilities.cs`)

| Member | Tag | In FNA | Note |
|--------|-----|--------|------|
| `GamePadCapabilities() = default` | STRICT | no | C++ special-member idiom (no XNA counterpart expected) |
| `bool getIsConnectedProperty() const` | STRICT | yes | matched |
| `CNAEXT void setIsConnectedProperty(bool value)` | CNAEXT | yes | maps FNA non-public |
| `bool getHasAButtonProperty() const` | STRICT | yes | matched |
| `CNAEXT void setHasAButtonProperty(bool value)` | CNAEXT | yes | maps FNA non-public |
| `bool getHasBackButtonProperty() const` | STRICT | yes | matched |
| `CNAEXT void setHasBackButtonProperty(bool value)` | CNAEXT | yes | maps FNA non-public |
| `bool getHasBButtonProperty() const` | STRICT | yes | matched |
| `CNAEXT void setHasBButtonProperty(bool value)` | CNAEXT | yes | maps FNA non-public |
| `bool getHasDPadDownButtonProperty() const` | STRICT | yes | matched |
| `CNAEXT void setHasDPadDownButtonProperty(bool value)` | CNAEXT | yes | maps FNA non-public |
| `bool getHasDPadLeftButtonProperty() const` | STRICT | yes | matched |
| `CNAEXT void setHasDPadLeftButtonProperty(bool value)` | CNAEXT | yes | maps FNA non-public |
| `bool getHasDPadRightButtonProperty() const` | STRICT | yes | matched |
| `CNAEXT void setHasDPadRightButtonProperty(bool value)` | CNAEXT | yes | maps FNA non-public |
| `bool getHasDPadUpButtonProperty() const` | STRICT | yes | matched |
| `CNAEXT void setHasDPadUpButtonProperty(bool value)` | CNAEXT | yes | maps FNA non-public |
| `bool getHasLeftShoulderButtonProperty() const` | STRICT | yes | matched |
| `CNAEXT void setHasLeftShoulderButtonProperty(bool value)` | CNAEXT | yes | maps FNA non-public |
| `bool getHasLeftStickButtonProperty() const` | STRICT | yes | matched |
| `CNAEXT void setHasLeftStickButtonProperty(bool value)` | CNAEXT | yes | maps FNA non-public |
| `bool getHasRightShoulderButtonProperty() const` | STRICT | yes | matched |
| `CNAEXT void setHasRightShoulderButtonProperty(bool value)` | CNAEXT | yes | maps FNA non-public |
| `bool getHasRightStickButtonProperty() const` | STRICT | yes | matched |
| `CNAEXT void setHasRightStickButtonProperty(bool value)` | CNAEXT | yes | maps FNA non-public |
| `bool getHasStartButtonProperty() const` | STRICT | yes | matched |
| `CNAEXT void setHasStartButtonProperty(bool value)` | CNAEXT | yes | maps FNA non-public |
| `bool getHasXButtonProperty() const` | STRICT | yes | matched |
| `CNAEXT void setHasXButtonProperty(bool value)` | CNAEXT | yes | maps FNA non-public |
| `bool getHasYButtonProperty() const` | STRICT | yes | matched |
| `CNAEXT void setHasYButtonProperty(bool value)` | CNAEXT | yes | maps FNA non-public |
| `bool getHasBigButtonProperty() const` | STRICT | yes | matched |
| `CNAEXT void setHasBigButtonProperty(bool value)` | CNAEXT | yes | maps FNA non-public |
| `bool getHasLeftXThumbStickProperty() const` | STRICT | yes | matched |
| `CNAEXT void setHasLeftXThumbStickProperty(bool value)` | CNAEXT | yes | maps FNA non-public |
| `bool getHasLeftYThumbStickProperty() const` | STRICT | yes | matched |
| `CNAEXT void setHasLeftYThumbStickProperty(bool value)` | CNAEXT | yes | maps FNA non-public |
| `bool getHasRightXThumbStickProperty() const` | STRICT | yes | matched |
| `CNAEXT void setHasRightXThumbStickProperty(bool value)` | CNAEXT | yes | maps FNA non-public |
| `bool getHasRightYThumbStickProperty() const` | STRICT | yes | matched |
| `CNAEXT void setHasRightYThumbStickProperty(bool value)` | CNAEXT | yes | maps FNA non-public |
| `bool getHasLeftTriggerProperty() const` | STRICT | yes | matched |
| `CNAEXT void setHasLeftTriggerProperty(bool value)` | CNAEXT | yes | maps FNA non-public |
| `bool getHasRightTriggerProperty() const` | STRICT | yes | matched |
| `CNAEXT void setHasRightTriggerProperty(bool value)` | CNAEXT | yes | maps FNA non-public |
| `bool getHasLeftVibrationMotorProperty() const` | STRICT | yes | matched |
| `CNAEXT void setHasLeftVibrationMotorProperty(bool value)` | CNAEXT | yes | maps FNA non-public |
| `bool getHasRightVibrationMotorProperty() const` | STRICT | yes | matched |
| `CNAEXT void setHasRightVibrationMotorProperty(bool value)` | CNAEXT | yes | maps FNA non-public |
| `bool getHasVoiceSupportProperty() const` | STRICT | yes | matched |
| `CNAEXT void setHasVoiceSupportProperty(bool value)` | CNAEXT | yes | maps FNA non-public |
| `GamePadType getGamePadTypeProperty() const` | STRICT | yes | matched |
| `CNAEXT void setGamePadTypeProperty(GamePadType value)` | CNAEXT | yes | maps FNA non-public |
| `CNAEXT bool getHasLightBarEXTProperty() const` | CNAEXT | yes | maps FNA non-public |
| `CNAEXT void setHasLightBarEXTProperty(bool value)` | CNAEXT | yes | maps FNA non-public |
| `CNAEXT bool getHasTriggerVibrationMotorsEXTProperty() const` | CNAEXT | yes | maps FNA non-public |
| `CNAEXT void setHasTriggerVibrationMotorsEXTProperty(bool value)` | CNAEXT | yes | maps FNA non-public |
| `CNAEXT bool getHasMisc1EXTProperty() const` | CNAEXT | yes | maps FNA non-public |
| `CNAEXT void setHasMisc1EXTProperty(bool value)` | CNAEXT | yes | maps FNA non-public |
| `CNAEXT bool getHasPaddle1EXTProperty() const` | CNAEXT | yes | maps FNA non-public |
| `CNAEXT void setHasPaddle1EXTProperty(bool value)` | CNAEXT | yes | maps FNA non-public |
| `CNAEXT bool getHasPaddle2EXTProperty() const` | CNAEXT | yes | maps FNA non-public |
| `CNAEXT void setHasPaddle2EXTProperty(bool value)` | CNAEXT | yes | maps FNA non-public |
| `CNAEXT bool getHasPaddle3EXTProperty() const` | CNAEXT | yes | maps FNA non-public |
| `CNAEXT void setHasPaddle3EXTProperty(bool value)` | CNAEXT | yes | maps FNA non-public |
| `CNAEXT bool getHasPaddle4EXTProperty() const` | CNAEXT | yes | maps FNA non-public |
| `CNAEXT void setHasPaddle4EXTProperty(bool value)` | CNAEXT | yes | maps FNA non-public |
| `CNAEXT bool getHasTouchPadEXTProperty() const` | CNAEXT | yes | maps FNA non-public |
| `CNAEXT void setHasTouchPadEXTProperty(bool value)` | CNAEXT | yes | maps FNA non-public |
| `CNAEXT bool getHasGyroEXTProperty() const` | CNAEXT | yes | maps FNA non-public |
| `CNAEXT void setHasGyroEXTProperty(bool value)` | CNAEXT | yes | maps FNA non-public |
| `CNAEXT bool getHasAccelerometerEXTProperty() const` | CNAEXT | yes | maps FNA non-public |
| `CNAEXT void setHasAccelerometerEXTProperty(bool value)` | CNAEXT | yes | maps FNA non-public |

## `GamePadDPad` — struct (FNA `GamePadDPad.cs`)

| Member | Tag | In FNA | Note |
|--------|-----|--------|------|
| `ButtonState getDownProperty() const` | STRICT | yes | matched |
| `ButtonState getLeftProperty() const` | STRICT | yes | matched |
| `ButtonState getRightProperty() const` | STRICT | yes | matched |
| `ButtonState getUpProperty() const` | STRICT | yes | matched |
| `CNAEXT GamePadDPad()` | CNAEXT | yes | maps FNA non-public |
| `GamePadDPad(ButtonState upValue, ButtonState downValue, ButtonState leftValue, ButtonState rightValue)` | STRICT | yes | matched |
| `CNAEXT static GamePadDPad FromButtonArray(std::initializer_list<Buttons> buttons)` | CNAEXT | internal | maps FNA non-public |
| `bool Equals(const GamePadDPad& other) const` | STRICT | yes | matched |
| `int GetHashCode() const` | STRICT | yes | matched |
| `friend bool operator==(const GamePadDPad& left, const GamePadDPad& right)` | STRICT | yes | matched |
| `friend bool operator!=(const GamePadDPad& left, const GamePadDPad& right)` | STRICT | yes | matched |

## `GamePadDeadZone` — enum (FNA `GamePadDeadZone.cs`)

| Member | Tag | In FNA | Note |
|--------|-----|--------|------|
| `None` | STRICT | yes | matched |
| `IndependentAxes` | STRICT | yes | matched |
| `Circular` | STRICT | yes | matched |

## `GamePadState` — struct (FNA `GamePadState.cs`)

| Member | Tag | In FNA | Note |
|--------|-----|--------|------|
| `bool getIsConnectedProperty() const` | STRICT | yes | matched |
| `int getPacketNumberProperty() const` | STRICT | yes | matched |
| `CNAEXT void setPacketNumberProperty(int value)` | CNAEXT | yes | maps FNA non-public |
| `const GamePadButtons& getButtonsProperty() const` | STRICT | yes | matched |
| `const GamePadDPad& getDPadProperty() const` | STRICT | yes | matched |
| `const GamePadThumbSticks& getThumbSticksProperty() const` | STRICT | yes | matched |
| `const GamePadTriggers& getTriggersProperty() const` | STRICT | yes | matched |
| `CNAEXT GamePadState()` | CNAEXT | yes | maps FNA non-public |
| `GamePadState(const GamePadThumbSticks& thumbSticks, const GamePadTriggers& triggers, const GamePadButtons& buttons, const GamePadDPad& dPad)` | STRICT | yes | matched |
| `GamePadState(const Microsoft::Xna::Framework::Vector2& leftThumbStick, const Microsoft::Xna::Framework::Vector2& rightThumbStick, float leftTrigger, float rightTrigger, std::initializer_list<Buttons> buttons)` | STRICT | yes | matched |
| `bool IsButtonDown(Buttons button) const` | STRICT | yes | matched |
| `bool IsButtonUp(Buttons button) const` | STRICT | yes | matched |
| `bool Equals(const GamePadState& other) const` | STRICT | yes | matched |
| `int GetHashCode() const` | STRICT | yes | matched |
| `std::string ToString() const` | STRICT | yes | matched |
| `friend bool operator==(const GamePadState& left, const GamePadState& right)` | STRICT | yes | matched |
| `friend bool operator!=(const GamePadState& left, const GamePadState& right)` | STRICT | yes | matched |

## `GamePadThumbSticks` — struct (FNA `GamePadThumbSticks.cs`)

| Member | Tag | In FNA | Note |
|--------|-----|--------|------|
| `const Microsoft::Xna::Framework::Vector2& getLeftProperty() const` | STRICT | yes | matched |
| `const Microsoft::Xna::Framework::Vector2& getRightProperty() const` | STRICT | yes | matched |
| `CNAEXT GamePadThumbSticks()` | CNAEXT | yes | maps FNA non-public |
| `GamePadThumbSticks(const Microsoft::Xna::Framework::Vector2& leftPosition, const Microsoft::Xna::Framework::Vector2& rightPosition)` | STRICT | yes | matched |
| `bool Equals(const GamePadThumbSticks& other) const` | STRICT | yes | matched |
| `int GetHashCode() const` | STRICT | yes | matched |
| `friend bool operator==(const GamePadThumbSticks& left, const GamePadThumbSticks& right)` | STRICT | yes | matched |
| `friend bool operator!=(const GamePadThumbSticks& left, const GamePadThumbSticks& right)` | STRICT | yes | matched |

## `GamePadTriggers` — struct (FNA `GamePadTriggers.cs`)

| Member | Tag | In FNA | Note |
|--------|-----|--------|------|
| `float getLeftProperty() const` | STRICT | yes | matched |
| `float getRightProperty() const` | STRICT | yes | matched |
| `CNAEXT GamePadTriggers()` | CNAEXT | yes | maps FNA non-public |
| `GamePadTriggers(float leftTrigger, float rightTrigger)` | STRICT | yes | matched |
| `bool Equals(const GamePadTriggers& other) const` | STRICT | yes | matched |
| `int GetHashCode() const` | STRICT | yes | matched |
| `friend bool operator==(const GamePadTriggers& left, const GamePadTriggers& right)` | STRICT | yes | matched |
| `friend bool operator!=(const GamePadTriggers& left, const GamePadTriggers& right)` | STRICT | yes | matched |

## `GamePadType` — enum (FNA `GamePadType.cs`)

| Member | Tag | In FNA | Note |
|--------|-----|--------|------|
| `Unknown` | STRICT | yes | matched |
| `GamePad` | STRICT | yes | matched |
| `Wheel` | STRICT | yes | matched |
| `ArcadeStick` | STRICT | yes | matched |
| `FlightStick` | STRICT | yes | matched |
| `DancePad` | STRICT | yes | matched |
| `Guitar` | STRICT | yes | matched |
| `AlternateGuitar` | STRICT | yes | matched |
| `DrumKit` | STRICT | yes | matched |
| `BigButtonPad` | STRICT | yes | matched |

## `GestureSample` — struct (FNA `GestureSample.cs`)

| Member | Tag | In FNA | Note |
|--------|-----|--------|------|
| `GestureType getGestureTypeProperty() const` | STRICT | yes | matched |
| `System::TimeSpan getTimestampProperty() const` | STRICT | yes | matched |
| `const Microsoft::Xna::Framework::Vector2& getPositionProperty() const` | STRICT | yes | matched |
| `const Microsoft::Xna::Framework::Vector2& getPosition2Property() const` | STRICT | yes | matched |
| `const Microsoft::Xna::Framework::Vector2& getDeltaProperty() const` | STRICT | yes | matched |
| `const Microsoft::Xna::Framework::Vector2& getDelta2Property() const` | STRICT | yes | matched |
| `CNAEXT int getFingerIdEXTProperty() const` | CNAEXT | yes | maps FNA non-public |
| `CNAEXT int getFingerId2EXTProperty() const` | CNAEXT | yes | maps FNA non-public |
| `CNAEXT GestureSample()` | CNAEXT | yes | maps FNA non-public |
| `GestureSample(GestureType gestureType, System::TimeSpan timestamp, Microsoft::Xna::Framework::Vector2 position, Microsoft::Xna::Framework::Vector2 position2, Microsoft::Xna::Framework::Vector2 delta, Microsoft::Xna::Framework::Vector2 delta2)` | STRICT | yes | matched |
| `CNAEXT GestureSample(GestureType gestureType, System::TimeSpan timestamp, Microsoft::Xna::Framework::Vector2 position, Microsoft::Xna::Framework::Vector2 position2, Microsoft::Xna::Framework::Vector2 delta, Microsoft::Xna::Framework::Vector2 delta2, int fingerId, int fingerId2)` | CNAEXT | yes | maps FNA non-public |

## `GestureType` — enum (FNA `GestureType.cs`)

| Member | Tag | In FNA | Note |
|--------|-----|--------|------|
| `None` | STRICT | yes | matched |
| `Tap` | STRICT | yes | matched |
| `DoubleTap` | STRICT | yes | matched |
| `Hold` | STRICT | yes | matched |
| `HorizontalDrag` | STRICT | yes | matched |
| `VerticalDrag` | STRICT | yes | matched |
| `FreeDrag` | STRICT | yes | matched |
| `Pinch` | STRICT | yes | matched |
| `Flick` | STRICT | yes | matched |
| `DragComplete` | STRICT | yes | matched |
| `PinchComplete` | STRICT | yes | matched |

## `KeyState` — enum (FNA `KeyState.cs`)

| Member | Tag | In FNA | Note |
|--------|-----|--------|------|
| `Up` | STRICT | yes | matched |
| `Down` | STRICT | yes | matched |

## `Keyboard` — class (FNA `Keyboard.cs`)

| Member | Tag | In FNA | Note |
|--------|-----|--------|------|
| `Keyboard() = delete` | STRICT | no | C++ special-member idiom (no XNA counterpart expected) |
| `static KeyboardState GetState()` | STRICT | yes | matched |
| `static KeyboardState GetState(Microsoft::Xna::Framework::PlayerIndex playerIndex)` | STRICT | yes | matched |
| `CNAEXT static Keys GetKeyFromScancodeEXT(Keys scancode)` | EXT | yes | matched |
| `CNAEXT static CNA::Input::KeyModifiersEXT GetModStateEXT()` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `CNAEXT static std::string GetScancodeNameEXT(Keys key)` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `CNAEXT static Keys GetScancodeFromNameEXT(const std::string& name)` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `CNAEXT static std::string GetKeyNameEXT(Keys key)` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `CNAEXT static Keys GetKeyFromNameEXT(const std::string& name)` | EXT | no | EXT extension (no stock-XNA counterpart expected) |

## `KeyboardState` — struct (FNA `KeyboardState.cs`)

| Member | Tag | In FNA | Note |
|--------|-----|--------|------|
| `CNAEXT KeyboardState()` | CNAEXT | yes | maps FNA non-public |
| `KeyboardState(std::initializer_list<Keys> keys)` | STRICT | yes | matched |
| `CNAEXT explicit KeyboardState(const std::unordered_set<Keys>& pressedKeys)` | CNAEXT | yes | maps FNA non-public |
| `KeyState getItem(Keys key) const` | STRICT | yes | matched |
| `KeyState operator[](Keys key) const` | STRICT | yes | matched |
| `bool IsKeyDown(Keys key) const` | STRICT | yes | matched |
| `bool IsKeyUp(Keys key) const` | STRICT | yes | matched |
| `std::vector<Keys> GetPressedKeys() const` | STRICT | yes | matched |
| `bool Equals(const KeyboardState& other) const` | STRICT | yes | matched |
| `int GetHashCode() const` | STRICT | yes | matched |
| `CNAEXT std::string ToString() const` | CNAEXT | no | CNA-only |
| `friend bool operator==(const KeyboardState& a, const KeyboardState& b)` | STRICT | yes | matched |
| `friend bool operator!=(const KeyboardState& a, const KeyboardState& b)` | STRICT | yes | matched |

## `Keys` — enum (FNA `Keys.cs`)

| Member | Tag | In FNA | Note |
|--------|-----|--------|------|
| `None` | STRICT | yes | matched |
| `Back` | STRICT | yes | matched |
| `Tab` | STRICT | yes | matched |
| `Enter` | STRICT | yes | matched |
| `Pause` | STRICT | yes | matched |
| `CapsLock` | STRICT | yes | matched |
| `Kana` | STRICT | yes | matched |
| `Kanji` | STRICT | yes | matched |
| `Escape` | STRICT | yes | matched |
| `ImeConvert` | STRICT | yes | matched |
| `ImeNoConvert` | STRICT | yes | matched |
| `Space` | STRICT | yes | matched |
| `PageUp` | STRICT | yes | matched |
| `PageDown` | STRICT | yes | matched |
| `End` | STRICT | yes | matched |
| `Home` | STRICT | yes | matched |
| `Left` | STRICT | yes | matched |
| `Up` | STRICT | yes | matched |
| `Right` | STRICT | yes | matched |
| `Down` | STRICT | yes | matched |
| `Select` | STRICT | yes | matched |
| `Print` | STRICT | yes | matched |
| `Execute` | STRICT | yes | matched |
| `PrintScreen` | STRICT | yes | matched |
| `Insert` | STRICT | yes | matched |
| `Delete` | STRICT | yes | matched |
| `Help` | STRICT | yes | matched |
| `D0` | STRICT | yes | matched |
| `D1` | STRICT | yes | matched |
| `D2` | STRICT | yes | matched |
| `D3` | STRICT | yes | matched |
| `D4` | STRICT | yes | matched |
| `D5` | STRICT | yes | matched |
| `D6` | STRICT | yes | matched |
| `D7` | STRICT | yes | matched |
| `D8` | STRICT | yes | matched |
| `D9` | STRICT | yes | matched |
| `A` | STRICT | yes | matched |
| `B` | STRICT | yes | matched |
| `C` | STRICT | yes | matched |
| `D` | STRICT | yes | matched |
| `E` | STRICT | yes | matched |
| `F` | STRICT | yes | matched |
| `G` | STRICT | yes | matched |
| `H` | STRICT | yes | matched |
| `I` | STRICT | yes | matched |
| `J` | STRICT | yes | matched |
| `K` | STRICT | yes | matched |
| `L` | STRICT | yes | matched |
| `M` | STRICT | yes | matched |
| `N` | STRICT | yes | matched |
| `O` | STRICT | yes | matched |
| `P` | STRICT | yes | matched |
| `Q` | STRICT | yes | matched |
| `R` | STRICT | yes | matched |
| `S` | STRICT | yes | matched |
| `T` | STRICT | yes | matched |
| `U` | STRICT | yes | matched |
| `V` | STRICT | yes | matched |
| `W` | STRICT | yes | matched |
| `X` | STRICT | yes | matched |
| `Y` | STRICT | yes | matched |
| `Z` | STRICT | yes | matched |
| `LeftWindows` | STRICT | yes | matched |
| `RightWindows` | STRICT | yes | matched |
| `Apps` | STRICT | yes | matched |
| `Sleep` | STRICT | yes | matched |
| `NumPad0` | STRICT | yes | matched |
| `NumPad1` | STRICT | yes | matched |
| `NumPad2` | STRICT | yes | matched |
| `NumPad3` | STRICT | yes | matched |
| `NumPad4` | STRICT | yes | matched |
| `NumPad5` | STRICT | yes | matched |
| `NumPad6` | STRICT | yes | matched |
| `NumPad7` | STRICT | yes | matched |
| `NumPad8` | STRICT | yes | matched |
| `NumPad9` | STRICT | yes | matched |
| `Multiply` | STRICT | yes | matched |
| `Add` | STRICT | yes | matched |
| `Separator` | STRICT | yes | matched |
| `Subtract` | STRICT | yes | matched |
| `Decimal` | STRICT | yes | matched |
| `Divide` | STRICT | yes | matched |
| `F1` | STRICT | yes | matched |
| `F2` | STRICT | yes | matched |
| `F3` | STRICT | yes | matched |
| `F4` | STRICT | yes | matched |
| `F5` | STRICT | yes | matched |
| `F6` | STRICT | yes | matched |
| `F7` | STRICT | yes | matched |
| `F8` | STRICT | yes | matched |
| `F9` | STRICT | yes | matched |
| `F10` | STRICT | yes | matched |
| `F11` | STRICT | yes | matched |
| `F12` | STRICT | yes | matched |
| `F13` | STRICT | yes | matched |
| `F14` | STRICT | yes | matched |
| `F15` | STRICT | yes | matched |
| `F16` | STRICT | yes | matched |
| `F17` | STRICT | yes | matched |
| `F18` | STRICT | yes | matched |
| `F19` | STRICT | yes | matched |
| `F20` | STRICT | yes | matched |
| `F21` | STRICT | yes | matched |
| `F22` | STRICT | yes | matched |
| `F23` | STRICT | yes | matched |
| `F24` | STRICT | yes | matched |
| `NumLock` | STRICT | yes | matched |
| `Scroll` | STRICT | yes | matched |
| `LeftShift` | STRICT | yes | matched |
| `RightShift` | STRICT | yes | matched |
| `LeftControl` | STRICT | yes | matched |
| `RightControl` | STRICT | yes | matched |
| `LeftAlt` | STRICT | yes | matched |
| `RightAlt` | STRICT | yes | matched |
| `BrowserBack` | STRICT | yes | matched |
| `BrowserForward` | STRICT | yes | matched |
| `BrowserRefresh` | STRICT | yes | matched |
| `BrowserStop` | STRICT | yes | matched |
| `BrowserSearch` | STRICT | yes | matched |
| `BrowserFavorites` | STRICT | yes | matched |
| `BrowserHome` | STRICT | yes | matched |
| `VolumeMute` | STRICT | yes | matched |
| `VolumeDown` | STRICT | yes | matched |
| `VolumeUp` | STRICT | yes | matched |
| `MediaNextTrack` | STRICT | yes | matched |
| `MediaPreviousTrack` | STRICT | yes | matched |
| `MediaStop` | STRICT | yes | matched |
| `MediaPlayPause` | STRICT | yes | matched |
| `LaunchMail` | STRICT | yes | matched |
| `SelectMedia` | STRICT | yes | matched |
| `LaunchApplication1` | STRICT | yes | matched |
| `LaunchApplication2` | STRICT | yes | matched |
| `OemSemicolon` | STRICT | yes | matched |
| `OemPlus` | STRICT | yes | matched |
| `OemComma` | STRICT | yes | matched |
| `OemMinus` | STRICT | yes | matched |
| `OemPeriod` | STRICT | yes | matched |
| `OemQuestion` | STRICT | yes | matched |
| `OemTilde` | STRICT | yes | matched |
| `ChatPadGreen` | STRICT | yes | matched |
| `ChatPadOrange` | STRICT | yes | matched |
| `OemOpenBrackets` | STRICT | yes | matched |
| `OemPipe` | STRICT | yes | matched |
| `OemCloseBrackets` | STRICT | yes | matched |
| `OemQuotes` | STRICT | yes | matched |
| `Oem8` | STRICT | yes | matched |
| `OemBackslash` | STRICT | yes | matched |
| `ProcessKey` | STRICT | yes | matched |
| `OemCopy` | STRICT | yes | matched |
| `OemAuto` | STRICT | yes | matched |
| `OemEnlW` | STRICT | yes | matched |
| `Attn` | STRICT | yes | matched |
| `Crsel` | STRICT | yes | matched |
| `Exsel` | STRICT | yes | matched |
| `EraseEof` | STRICT | yes | matched |
| `Play` | STRICT | yes | matched |
| `Zoom` | STRICT | yes | matched |
| `Pa1` | STRICT | yes | matched |
| `OemClear` | STRICT | yes | matched |

## `Mouse` — class (FNA `Mouse.cs`)

| Member | Tag | In FNA | Note |
|--------|-----|--------|------|
| `Mouse() = delete` | STRICT | no | C++ special-member idiom (no XNA counterpart expected) |
| `static std::uintptr_t getWindowHandleProperty()` | STRICT | yes | matched |
| `static void setWindowHandleProperty(std::uintptr_t value)` | STRICT | yes | matched |
| `static MouseState GetState()` | STRICT | yes | matched |
| `static void SetPosition(int x, int y)` | STRICT | yes | matched |
| `CNAEXT static void SetCursor(MouseCursor& cursor)` | CNAEXT | no | CNA-only |
| `CNAEXT static System::MulticastAction<int> ClickedEXT` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `CNAEXT static bool getIsRelativeMouseModeEXTProperty()` | CNAEXT | yes | maps FNA non-public |
| `CNAEXT static void setIsRelativeMouseModeEXTProperty(bool value)` | CNAEXT | yes | maps FNA non-public |
| `CNAEXT static bool SetCaptureEXT(bool enabled)` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `CNAEXT static void GetGlobalPositionEXT(int& x, int& y)` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `CNAEXT static bool WarpGlobalEXT(int x, int y)` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `CNAEXT static void INTERNAL_onClicked(int button)` | CNAEXT | internal | maps FNA non-public |
| `CNAEXT static void ResetForTests()` | CNAEXT | no | CNA-only |

## `MouseCursor` — class (no FNA source (CNA-only))

| Member | Tag | In FNA | Note |
|--------|-----|--------|------|
| `CNAEXT MouseCursor()` | CNAEXT | no | CNA-only |
| `CNAEXT static MouseCursor FromTexture2D(const Graphics::Texture2D& texture, int originX, int originY)` | CNAEXT | no | CNA-only |
| `MouseCursor(const MouseCursor&) = delete` | CNAEXT | no | CNA-only |
| `MouseCursor& operator=(const MouseCursor&) = delete` | CNAEXT | no | CNA-only |
| `MouseCursor(MouseCursor&& other) noexcept` | CNAEXT | no | CNA-only |
| `~MouseCursor() override` | CNAEXT | no | CNA-only |
| `CNAEXT void Dispose() override` | CNAEXT | no | CNA-only |
| `CNAEXT static MouseCursor& getArrowProperty()` | CNAEXT | no | CNA-only |
| `CNAEXT static MouseCursor& getCrosshairProperty()` | CNAEXT | no | CNA-only |
| `CNAEXT static MouseCursor& getHandProperty()` | CNAEXT | no | CNA-only |
| `CNAEXT static MouseCursor& getIBeamProperty()` | CNAEXT | no | CNA-only |
| `CNAEXT static MouseCursor& getNoProperty()` | CNAEXT | no | CNA-only |
| `CNAEXT static MouseCursor& getSizeAllProperty()` | CNAEXT | no | CNA-only |
| `CNAEXT static MouseCursor& getSizeNESWProperty()` | CNAEXT | no | CNA-only |
| `CNAEXT static MouseCursor& getSizeNSProperty()` | CNAEXT | no | CNA-only |
| `CNAEXT static MouseCursor& getSizeNWSEProperty()` | CNAEXT | no | CNA-only |
| `CNAEXT static MouseCursor& getSizeWEProperty()` | CNAEXT | no | CNA-only |
| `CNAEXT static MouseCursor& getWaitProperty()` | CNAEXT | no | CNA-only |
| `CNAEXT static MouseCursor& getWaitArrowProperty()` | CNAEXT | no | CNA-only |

## `MouseState` — struct (FNA `MouseState.cs`)

| Member | Tag | In FNA | Note |
|--------|-----|--------|------|
| `int getXProperty() const` | STRICT | yes | matched |
| `int getYProperty() const` | STRICT | yes | matched |
| `ButtonState getLeftButtonProperty() const` | STRICT | yes | matched |
| `ButtonState getRightButtonProperty() const` | STRICT | yes | matched |
| `ButtonState getMiddleButtonProperty() const` | STRICT | yes | matched |
| `ButtonState getXButton1Property() const` | STRICT | yes | matched |
| `ButtonState getXButton2Property() const` | STRICT | yes | matched |
| `int getScrollWheelValueProperty() const` | STRICT | yes | matched |
| `CNAEXT int getHorizontalScrollWheelValueEXTProperty() const` | CNAEXT | no | CNA-only |
| `CNAEXT MouseState()` | CNAEXT | yes | maps FNA non-public |
| `MouseState(int x, int y, int scrollWheel, ButtonState leftButton, ButtonState middleButton, ButtonState rightButton, ButtonState xButton1, ButtonState xButton2)` | STRICT | yes | matched |
| `CNAEXT MouseState(int x, int y, int scrollWheel, ButtonState leftButton, ButtonState middleButton, ButtonState rightButton, ButtonState xButton1, ButtonState xButton2, int horizontalScrollWheel)` | CNAEXT | yes | maps FNA non-public |
| `bool Equals(const MouseState& other) const` | STRICT | yes | matched |
| `int GetHashCode() const` | STRICT | yes | matched |
| `std::string ToString() const` | STRICT | yes | matched |
| `friend bool operator==(const MouseState& left, const MouseState& right)` | STRICT | yes | matched |
| `friend bool operator!=(const MouseState& left, const MouseState& right)` | STRICT | yes | matched |

## `TextInputEXT` — class (FNA `TextInputEXT.cs`)

| Member | Tag | In FNA | Note |
|--------|-----|--------|------|
| `TextInputEXT() = delete` | EXT | no | C++ special-member idiom (no XNA counterpart expected) |
| `CNAEXT static System::MulticastAction<charcs> TextInput` | CNAEXT | no | CNA-only |
| `CNAEXT static System::MulticastAction<const std::string&, int, int> TextEditing` | CNAEXT | no | CNA-only |
| `CNAEXT static System::MulticastAction<const std::vector<std::string>&, int, bool> TextEditingCandidatesEXT` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `CNAEXT static std::uintptr_t getWindowHandleProperty()` | CNAEXT | yes | maps FNA non-public |
| `CNAEXT static void setWindowHandleProperty(std::uintptr_t value)` | CNAEXT | yes | maps FNA non-public |
| `CNAEXT static bool IsTextInputActive()` | CNAEXT | yes | maps FNA non-public |
| `CNAEXT static bool IsScreenKeyboardShown()` | CNAEXT | yes | maps FNA non-public |
| `CNAEXT static bool IsScreenKeyboardShown(std::uintptr_t window)` | CNAEXT | yes | maps FNA non-public |
| `CNAEXT static void StartTextInput()` | CNAEXT | yes | maps FNA non-public |
| `CNAEXT static void StopTextInput()` | CNAEXT | yes | maps FNA non-public |
| `CNAEXT static void StartTextInputWithTypeEXT(CNA::Input::TextInputTypeEXT type)` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `CNAEXT static void SetInputRectangle(const Microsoft::Xna::Framework::Rectangle& rectangle)` | CNAEXT | yes | maps FNA non-public |
| `CNAEXT static void INTERNAL_OnTextInput(charcs c)` | CNAEXT | no | CNA-only |
| `CNAEXT static void INTERNAL_OnTextEditing(const std::string& text, int start, int length)` | CNAEXT | no | CNA-only |
| `CNAEXT static void INTERNAL_OnTextEditingCandidates( const std::vector<std::string>& candidates, int selected, bool horizontal)` | CNAEXT | no | CNA-only |
| `CNAEXT static void ResetForTests()` | CNAEXT | no | CNA-only |

## `TouchCollection` — struct (FNA `TouchCollection.cs`)

| Member | Tag | In FNA | Note |
|--------|-----|--------|------|
| `int getCountProperty() const` | STRICT | yes | matched |
| `bool getIsConnectedProperty() const` | STRICT | yes | matched |
| `bool getIsReadOnlyProperty() const` | STRICT | yes | matched |
| `CNAEXT TouchCollection()` | CNAEXT | yes | maps FNA non-public |
| `explicit TouchCollection(const std::vector<TouchLocation>& touches)` | STRICT | yes | matched |
| `explicit TouchCollection(std::vector<TouchLocation>&& touches)` | STRICT | yes | matched |
| `TouchLocation& operator[](std::size_t index)` | STRICT | yes | matched |
| `const TouchLocation& operator[](std::size_t index) const` | STRICT | yes | matched |
| `CNAEXT bool empty() const` | CNAEXT | no | CNA-only |
| `bool Contains(const TouchLocation& item) const` | STRICT | yes | matched |
| `bool FindById(int id, TouchLocation& touchLocation) const` | STRICT | yes | matched |
| `void CopyTo(std::vector<TouchLocation>& array, int arrayIndex) const` | STRICT | yes | matched |
| `int IndexOf(const TouchLocation& item) const` | STRICT | yes | matched |
| `void Add(const TouchLocation& item)` | STRICT | yes | matched |
| `void Clear()` | STRICT | yes | matched |
| `bool Remove(const TouchLocation& item)` | STRICT | yes | matched |
| `void RemoveAt(int index)` | STRICT | yes | matched |
| `void Insert(int index, const TouchLocation& item)` | STRICT | yes | matched |
| `CNAEXT std::vector<TouchLocation>::iterator begin()` | CNAEXT | no | CNA-only |
| `CNAEXT std::vector<TouchLocation>::iterator end()` | CNAEXT | no | CNA-only |
| `CNAEXT std::vector<TouchLocation>::const_iterator begin() const` | CNAEXT | no | CNA-only |
| `CNAEXT std::vector<TouchLocation>::const_iterator end() const` | CNAEXT | no | CNA-only |

## `TouchLocation` — struct (FNA `TouchLocation.cs`)

| Member | Tag | In FNA | Note |
|--------|-----|--------|------|
| `int getIdProperty() const` | STRICT | yes | matched |
| `TouchLocationState getStateProperty() const` | STRICT | yes | matched |
| `const Microsoft::Xna::Framework::Vector2& getPositionProperty() const` | STRICT | yes | matched |
| `CNAEXT float getPressureEXT() const` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `CNAEXT TouchLocation()` | CNAEXT | yes | maps FNA non-public |
| `TouchLocation(int id, TouchLocationState state, const Microsoft::Xna::Framework::Vector2& position)` | STRICT | yes | matched |
| `TouchLocation(int id, TouchLocationState state, const Microsoft::Xna::Framework::Vector2& position, TouchLocationState previousState, const Microsoft::Xna::Framework::Vector2& previousPosition)` | STRICT | yes | matched |
| `CNAEXT TouchLocation(int id, TouchLocationState state, const Microsoft::Xna::Framework::Vector2& position, float pressure)` | CNAEXT | yes | maps FNA non-public |
| `CNAEXT TouchLocation(int id, TouchLocationState state, const Microsoft::Xna::Framework::Vector2& position, TouchLocationState previousState, const Microsoft::Xna::Framework::Vector2& previousPosition, float pressure)` | CNAEXT | yes | maps FNA non-public |
| `bool TryGetPreviousLocation(TouchLocation& previousLocation) const` | STRICT | yes | matched |
| `bool Equals(const TouchLocation& other) const` | STRICT | yes | matched |
| `int GetHashCode() const` | STRICT | yes | matched |
| `std::string ToString() const` | STRICT | yes | matched |
| `friend bool operator==(const TouchLocation& value1, const TouchLocation& value2)` | STRICT | yes | matched |
| `friend bool operator!=(const TouchLocation& value1, const TouchLocation& value2)` | STRICT | yes | matched |

## `TouchLocationState` — enum (FNA `TouchLocationState.cs`)

| Member | Tag | In FNA | Note |
|--------|-----|--------|------|
| `Invalid` | STRICT | yes | matched |
| `Released` | STRICT | yes | matched |
| `Pressed` | STRICT | yes | matched |
| `Moved` | STRICT | yes | matched |

## `TouchPanel` — class (FNA `TouchPanel.cs`)

| Member | Tag | In FNA | Note |
|--------|-----|--------|------|
| `TouchPanel() = delete` | STRICT | no | C++ special-member idiom (no XNA counterpart expected) |
| `CNAEXT static constexpr intcs MAX_TOUCHES = 8` | CNAEXT | internal | maps FNA non-public |
| `CNAEXT static constexpr intcs NO_FINGER = -1` | CNAEXT | internal | maps FNA non-public |
| `static intcs getDisplayWidthProperty()` | STRICT | yes | matched |
| `static void setDisplayWidthProperty(intcs value)` | STRICT | yes | matched |
| `static intcs getDisplayHeightProperty()` | STRICT | yes | matched |
| `static void setDisplayHeightProperty(intcs value)` | STRICT | yes | matched |
| `static Microsoft::Xna::Framework::DisplayOrientation getDisplayOrientationProperty()` | STRICT | yes | matched |
| `static void setDisplayOrientationProperty(Microsoft::Xna::Framework::DisplayOrientation value)` | STRICT | yes | matched |
| `static GestureType getEnabledGesturesProperty()` | STRICT | yes | matched |
| `static void setEnabledGesturesProperty(GestureType value)` | STRICT | yes | matched |
| `static bool getIsGestureAvailableProperty()` | STRICT | yes | matched |
| `static std::uintptr_t getWindowHandleProperty()` | STRICT | yes | matched |
| `static void setWindowHandleProperty(std::uintptr_t value)` | STRICT | yes | matched |
| `CNAEXT static bool getTouchDeviceExistsProperty()` | CNAEXT | no | CNA-only |
| `CNAEXT static void setTouchDeviceExistsProperty(bool value)` | CNAEXT | no | CNA-only |
| `static TouchPanelCapabilities GetCapabilities()` | STRICT | yes | matched |
| `static TouchCollection GetState()` | STRICT | yes | matched |
| `static GestureSample ReadGesture()` | STRICT | yes | matched |
| `CNAEXT static void EnqueueGesture(const GestureSample& gesture)` | CNAEXT | internal | maps FNA non-public |
| `CNAEXT static void INTERNAL_onTouchEvent( intcs fingerId, TouchLocationState state, float x, float y, float dx, float dy )` | CNAEXT | internal | maps FNA non-public |
| `CNAEXT static void SetFinger(intcs index, intcs fingerId, const Microsoft::Xna::Framework::Vector2& fingerPos)` | CNAEXT | internal | maps FNA non-public |
| `CNAEXT static void Update()` | CNAEXT | internal | maps FNA non-public |
| `CNAEXT static void ResetForTests()` | CNAEXT | no | CNA-only |

## `TouchPanelCapabilities` — struct (FNA `TouchPanelCapabilities.cs`)

| Member | Tag | In FNA | Note |
|--------|-----|--------|------|
| `bool getIsConnectedProperty() const` | STRICT | yes | matched |
| `int getMaximumTouchCountProperty() const` | STRICT | yes | matched |
| `CNAEXT TouchPanelCapabilities()` | CNAEXT | internal | maps FNA non-public |
| `CNAEXT TouchPanelCapabilities(bool isConnected, int maximumTouchCount)` | CNAEXT | internal | maps FNA non-public |

