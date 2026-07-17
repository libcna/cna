# Input member-level parity matrix (INPUT-API-027)

> **Related input docs (INP-0003):** [plan](../plan_input.md) · [backend](input-backend.md) · [FNA fidelity + deviations](input-fna-fidelity.md) · [member-parity matrix](input-member-parity-matrix.md) · [frozen API + tier glossary](input-public-api-frozen.md) · [test coverage](input-test-coverage.md) · [build & test](input-build-and-test.md) · [platform notes](platform-input-notes.md) · [manual results](input-manual-verification-results.md) · [demo checklist](demo-input-checklist.md)

> **Generated** by `tools/input_parity/gen_input_parity_matrix.py` from the public
> `Microsoft::Xna::Framework::Input` (+ `::Touch`) headers and the FNA reference
> at `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`. Do not hand-edit — re-run the generator. This is a review aid;
> exact signatures stay pinned by `PublicApiInputSignatureFreezeTests.cpp`
> (INPUT-API-031) and enum values by INPUT-API-034.

Tags: **STRICT** = XNA 4.0 API (must match FNA) · **EXT** = FNA-compatible extension (`…EXT`) · **NOXNA** = CNA-only convenience.

The `In FNA` column: `yes` = a `public` FNA member matches by name; `internal` = matches an FNA `internal` member (expected for a CNA `NOXNA` that surfaces FNA-internal plumbing); `no` = no FNA member of that name (expected for `= delete`/`= default` C++ idioms and EXT).

## Review summary

No STRICT/EXT member is missing an FNA counterpart, and no FNA public member is
missing a CNA counterpart (by the heuristic name mapping, after accounting for the
documented collection-interface deviations below). Full per-type tables follow.

- FNA `IList<T>`/`IEnumerator`/`IDisposable` plumbing intentionally **not** mirrored by CNA's value-type collections (by design, not a gap): **4** — `TouchCollection.Current`, `TouchCollection.GetEnumerator`, `TouchCollection.MoveNext`, `TouchCollection.Dispose`

> Rows flagged above are heuristic (name-level) and may be false positives — e.g. a
> C++ `ref`/`&&` overload pair mapping one C# member, an FNA `internal` surfaced as CNA
> NOXNA, or an equality operator resolved via ADL. Review each against the .cs before acting.

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
| `NOXNA static std::string GetGUIDEXT(PlayerIndex playerIndex)` | EXT | yes | matched |
| `NOXNA static void SetLightBarEXT(PlayerIndex playerIndex, const Microsoft::Xna::Framework::Color& color)` | EXT | yes | matched |
| `NOXNA static bool SetTriggerVibrationEXT(PlayerIndex playerIndex, float leftTrigger, float rightTrigger)` | EXT | yes | matched |
| `NOXNA static bool GetGyroEXT(PlayerIndex playerIndex, Microsoft::Xna::Framework::Vector3& gyro)` | EXT | yes | matched |
| `NOXNA static bool GetAccelerometerEXT(PlayerIndex playerIndex, Microsoft::Xna::Framework::Vector3& accel)` | EXT | yes | matched |
| `NOXNA static int GetPlayerIndexEXT(PlayerIndex playerIndex)` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `NOXNA static bool SetPlayerIndexEXT(PlayerIndex playerIndex, int index)` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `NOXNA static CNA::Input::PowerStateEXT GetPowerInfoEXT(PlayerIndex playerIndex, int& percent)` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `NOXNA static CNA::Input::GamePadButtonLabelEXT GetButtonLabelEXT(PlayerIndex playerIndex, Buttons button)` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `NOXNA static std::string GetNameEXT(PlayerIndex playerIndex)` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `NOXNA static std::string GetPathEXT(PlayerIndex playerIndex)` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `NOXNA static std::string GetSerialEXT(PlayerIndex playerIndex)` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `NOXNA static std::uint16_t GetFirmwareVersionEXT(PlayerIndex playerIndex)` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `NOXNA static std::uint64_t GetSteamHandleEXT(PlayerIndex playerIndex)` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `NOXNA static CNA::Input::GamePadConnectionStateEXT GetConnectionStateEXT(PlayerIndex playerIndex)` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `NOXNA static int GetTouchpadCountEXT(PlayerIndex playerIndex)` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `NOXNA static int GetTouchpadFingerCountEXT(PlayerIndex playerIndex, int touchpad)` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `NOXNA static bool GetTouchpadFingerEXT(PlayerIndex playerIndex, int touchpad, int finger, bool& down, float& x, float& y, float& pressure)` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `NOXNA static constexpr float LeftDeadZone = 7849.0f / 32768.0f` | NOXNA | internal | maps FNA non-public |
| `NOXNA static constexpr float RightDeadZone = 8689.0f / 32768.0f` | NOXNA | internal | maps FNA non-public |
| `NOXNA static constexpr float TriggerThreshold = 30.0f / 255.0f` | NOXNA | internal | maps FNA non-public |
| `NOXNA static float ExcludeAxisDeadZone(float value, float deadZone)` | NOXNA | internal | maps FNA non-public |

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
| `NOXNA GamePadButtons()` | NOXNA | yes | maps FNA non-public |
| `explicit GamePadButtons(Buttons buttons)` | STRICT | yes | matched |
| `NOXNA static GamePadButtons FromButtonArray(std::initializer_list<Buttons> btns)` | NOXNA | internal | maps FNA non-public |
| `bool Equals(const GamePadButtons& other) const` | STRICT | yes | matched |
| `int GetHashCode() const` | STRICT | yes | matched |
| `friend bool operator==(const GamePadButtons& left, const GamePadButtons& right)` | STRICT | yes | matched |
| `friend bool operator!=(const GamePadButtons& left, const GamePadButtons& right)` | STRICT | yes | matched |

## `GamePadCapabilities` — struct (FNA `GamePadCapabilities.cs`)

| Member | Tag | In FNA | Note |
|--------|-----|--------|------|
| `GamePadCapabilities() = default` | STRICT | no | C++ special-member idiom (no XNA counterpart expected) |
| `bool getIsConnectedProperty() const` | STRICT | yes | matched |
| `NOXNA void setIsConnectedProperty(bool value)` | NOXNA | yes | maps FNA non-public |
| `bool getHasAButtonProperty() const` | STRICT | yes | matched |
| `NOXNA void setHasAButtonProperty(bool value)` | NOXNA | yes | maps FNA non-public |
| `bool getHasBackButtonProperty() const` | STRICT | yes | matched |
| `NOXNA void setHasBackButtonProperty(bool value)` | NOXNA | yes | maps FNA non-public |
| `bool getHasBButtonProperty() const` | STRICT | yes | matched |
| `NOXNA void setHasBButtonProperty(bool value)` | NOXNA | yes | maps FNA non-public |
| `bool getHasDPadDownButtonProperty() const` | STRICT | yes | matched |
| `NOXNA void setHasDPadDownButtonProperty(bool value)` | NOXNA | yes | maps FNA non-public |
| `bool getHasDPadLeftButtonProperty() const` | STRICT | yes | matched |
| `NOXNA void setHasDPadLeftButtonProperty(bool value)` | NOXNA | yes | maps FNA non-public |
| `bool getHasDPadRightButtonProperty() const` | STRICT | yes | matched |
| `NOXNA void setHasDPadRightButtonProperty(bool value)` | NOXNA | yes | maps FNA non-public |
| `bool getHasDPadUpButtonProperty() const` | STRICT | yes | matched |
| `NOXNA void setHasDPadUpButtonProperty(bool value)` | NOXNA | yes | maps FNA non-public |
| `bool getHasLeftShoulderButtonProperty() const` | STRICT | yes | matched |
| `NOXNA void setHasLeftShoulderButtonProperty(bool value)` | NOXNA | yes | maps FNA non-public |
| `bool getHasLeftStickButtonProperty() const` | STRICT | yes | matched |
| `NOXNA void setHasLeftStickButtonProperty(bool value)` | NOXNA | yes | maps FNA non-public |
| `bool getHasRightShoulderButtonProperty() const` | STRICT | yes | matched |
| `NOXNA void setHasRightShoulderButtonProperty(bool value)` | NOXNA | yes | maps FNA non-public |
| `bool getHasRightStickButtonProperty() const` | STRICT | yes | matched |
| `NOXNA void setHasRightStickButtonProperty(bool value)` | NOXNA | yes | maps FNA non-public |
| `bool getHasStartButtonProperty() const` | STRICT | yes | matched |
| `NOXNA void setHasStartButtonProperty(bool value)` | NOXNA | yes | maps FNA non-public |
| `bool getHasXButtonProperty() const` | STRICT | yes | matched |
| `NOXNA void setHasXButtonProperty(bool value)` | NOXNA | yes | maps FNA non-public |
| `bool getHasYButtonProperty() const` | STRICT | yes | matched |
| `NOXNA void setHasYButtonProperty(bool value)` | NOXNA | yes | maps FNA non-public |
| `bool getHasBigButtonProperty() const` | STRICT | yes | matched |
| `NOXNA void setHasBigButtonProperty(bool value)` | NOXNA | yes | maps FNA non-public |
| `bool getHasLeftXThumbStickProperty() const` | STRICT | yes | matched |
| `NOXNA void setHasLeftXThumbStickProperty(bool value)` | NOXNA | yes | maps FNA non-public |
| `bool getHasLeftYThumbStickProperty() const` | STRICT | yes | matched |
| `NOXNA void setHasLeftYThumbStickProperty(bool value)` | NOXNA | yes | maps FNA non-public |
| `bool getHasRightXThumbStickProperty() const` | STRICT | yes | matched |
| `NOXNA void setHasRightXThumbStickProperty(bool value)` | NOXNA | yes | maps FNA non-public |
| `bool getHasRightYThumbStickProperty() const` | STRICT | yes | matched |
| `NOXNA void setHasRightYThumbStickProperty(bool value)` | NOXNA | yes | maps FNA non-public |
| `bool getHasLeftTriggerProperty() const` | STRICT | yes | matched |
| `NOXNA void setHasLeftTriggerProperty(bool value)` | NOXNA | yes | maps FNA non-public |
| `bool getHasRightTriggerProperty() const` | STRICT | yes | matched |
| `NOXNA void setHasRightTriggerProperty(bool value)` | NOXNA | yes | maps FNA non-public |
| `bool getHasLeftVibrationMotorProperty() const` | STRICT | yes | matched |
| `NOXNA void setHasLeftVibrationMotorProperty(bool value)` | NOXNA | yes | maps FNA non-public |
| `bool getHasRightVibrationMotorProperty() const` | STRICT | yes | matched |
| `NOXNA void setHasRightVibrationMotorProperty(bool value)` | NOXNA | yes | maps FNA non-public |
| `bool getHasVoiceSupportProperty() const` | STRICT | yes | matched |
| `NOXNA void setHasVoiceSupportProperty(bool value)` | NOXNA | yes | maps FNA non-public |
| `GamePadType getGamePadTypeProperty() const` | STRICT | yes | matched |
| `NOXNA void setGamePadTypeProperty(GamePadType value)` | NOXNA | yes | maps FNA non-public |
| `NOXNA bool getHasLightBarEXTProperty() const` | NOXNA | yes | maps FNA non-public |
| `NOXNA void setHasLightBarEXTProperty(bool value)` | NOXNA | yes | maps FNA non-public |
| `NOXNA bool getHasTriggerVibrationMotorsEXTProperty() const` | NOXNA | yes | maps FNA non-public |
| `NOXNA void setHasTriggerVibrationMotorsEXTProperty(bool value)` | NOXNA | yes | maps FNA non-public |
| `NOXNA bool getHasMisc1EXTProperty() const` | NOXNA | yes | maps FNA non-public |
| `NOXNA void setHasMisc1EXTProperty(bool value)` | NOXNA | yes | maps FNA non-public |
| `NOXNA bool getHasPaddle1EXTProperty() const` | NOXNA | yes | maps FNA non-public |
| `NOXNA void setHasPaddle1EXTProperty(bool value)` | NOXNA | yes | maps FNA non-public |
| `NOXNA bool getHasPaddle2EXTProperty() const` | NOXNA | yes | maps FNA non-public |
| `NOXNA void setHasPaddle2EXTProperty(bool value)` | NOXNA | yes | maps FNA non-public |
| `NOXNA bool getHasPaddle3EXTProperty() const` | NOXNA | yes | maps FNA non-public |
| `NOXNA void setHasPaddle3EXTProperty(bool value)` | NOXNA | yes | maps FNA non-public |
| `NOXNA bool getHasPaddle4EXTProperty() const` | NOXNA | yes | maps FNA non-public |
| `NOXNA void setHasPaddle4EXTProperty(bool value)` | NOXNA | yes | maps FNA non-public |
| `NOXNA bool getHasTouchPadEXTProperty() const` | NOXNA | yes | maps FNA non-public |
| `NOXNA void setHasTouchPadEXTProperty(bool value)` | NOXNA | yes | maps FNA non-public |
| `NOXNA bool getHasGyroEXTProperty() const` | NOXNA | yes | maps FNA non-public |
| `NOXNA void setHasGyroEXTProperty(bool value)` | NOXNA | yes | maps FNA non-public |
| `NOXNA bool getHasAccelerometerEXTProperty() const` | NOXNA | yes | maps FNA non-public |
| `NOXNA void setHasAccelerometerEXTProperty(bool value)` | NOXNA | yes | maps FNA non-public |

## `GamePadDPad` — struct (FNA `GamePadDPad.cs`)

| Member | Tag | In FNA | Note |
|--------|-----|--------|------|
| `ButtonState getDownProperty() const` | STRICT | yes | matched |
| `ButtonState getLeftProperty() const` | STRICT | yes | matched |
| `ButtonState getRightProperty() const` | STRICT | yes | matched |
| `ButtonState getUpProperty() const` | STRICT | yes | matched |
| `NOXNA GamePadDPad()` | NOXNA | yes | maps FNA non-public |
| `GamePadDPad(ButtonState upValue, ButtonState downValue, ButtonState leftValue, ButtonState rightValue)` | STRICT | yes | matched |
| `NOXNA static GamePadDPad FromButtonArray(std::initializer_list<Buttons> buttons)` | NOXNA | internal | maps FNA non-public |
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
| `NOXNA void setPacketNumberProperty(int value)` | NOXNA | yes | maps FNA non-public |
| `const GamePadButtons& getButtonsProperty() const` | STRICT | yes | matched |
| `const GamePadDPad& getDPadProperty() const` | STRICT | yes | matched |
| `const GamePadThumbSticks& getThumbSticksProperty() const` | STRICT | yes | matched |
| `const GamePadTriggers& getTriggersProperty() const` | STRICT | yes | matched |
| `NOXNA GamePadState()` | NOXNA | yes | maps FNA non-public |
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
| `NOXNA GamePadThumbSticks()` | NOXNA | yes | maps FNA non-public |
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
| `NOXNA GamePadTriggers()` | NOXNA | yes | maps FNA non-public |
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
| `NOXNA int getFingerIdEXTProperty() const` | NOXNA | yes | maps FNA non-public |
| `NOXNA int getFingerId2EXTProperty() const` | NOXNA | yes | maps FNA non-public |
| `NOXNA GestureSample()` | NOXNA | yes | maps FNA non-public |
| `GestureSample(GestureType gestureType, System::TimeSpan timestamp, Microsoft::Xna::Framework::Vector2 position, Microsoft::Xna::Framework::Vector2 position2, Microsoft::Xna::Framework::Vector2 delta, Microsoft::Xna::Framework::Vector2 delta2)` | STRICT | yes | matched |
| `NOXNA GestureSample(GestureType gestureType, System::TimeSpan timestamp, Microsoft::Xna::Framework::Vector2 position, Microsoft::Xna::Framework::Vector2 position2, Microsoft::Xna::Framework::Vector2 delta, Microsoft::Xna::Framework::Vector2 delta2, int fingerId, int fingerId2)` | NOXNA | yes | maps FNA non-public |

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
| `NOXNA static Keys GetKeyFromScancodeEXT(Keys scancode)` | EXT | yes | matched |
| `NOXNA static CNA::Input::KeyModifiersEXT GetModStateEXT()` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `NOXNA static std::string GetScancodeNameEXT(Keys key)` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `NOXNA static Keys GetScancodeFromNameEXT(const std::string& name)` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `NOXNA static std::string GetKeyNameEXT(Keys key)` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `NOXNA static Keys GetKeyFromNameEXT(const std::string& name)` | EXT | no | EXT extension (no stock-XNA counterpart expected) |

## `KeyboardState` — struct (FNA `KeyboardState.cs`)

| Member | Tag | In FNA | Note |
|--------|-----|--------|------|
| `NOXNA KeyboardState()` | NOXNA | yes | maps FNA non-public |
| `KeyboardState(std::initializer_list<Keys> keys)` | STRICT | yes | matched |
| `NOXNA explicit KeyboardState(const std::unordered_set<Keys>& pressedKeys)` | NOXNA | yes | maps FNA non-public |
| `KeyState getItem(Keys key) const` | STRICT | yes | matched |
| `KeyState operator[](Keys key) const` | STRICT | yes | matched |
| `bool IsKeyDown(Keys key) const` | STRICT | yes | matched |
| `bool IsKeyUp(Keys key) const` | STRICT | yes | matched |
| `std::vector<Keys> GetPressedKeys() const` | STRICT | yes | matched |
| `bool Equals(const KeyboardState& other) const` | STRICT | yes | matched |
| `int GetHashCode() const` | STRICT | yes | matched |
| `NOXNA std::string ToString() const` | NOXNA | no | CNA-only |
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
| `NOXNA static void SetCursor(MouseCursor& cursor)` | NOXNA | no | CNA-only |
| `NOXNA static System::MulticastAction<int> ClickedEXT` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `NOXNA static bool getIsRelativeMouseModeEXTProperty()` | NOXNA | yes | maps FNA non-public |
| `NOXNA static void setIsRelativeMouseModeEXTProperty(bool value)` | NOXNA | yes | maps FNA non-public |
| `NOXNA static bool SetCaptureEXT(bool enabled)` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `NOXNA static void GetGlobalPositionEXT(int& x, int& y)` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `NOXNA static bool WarpGlobalEXT(int x, int y)` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `NOXNA static void INTERNAL_onClicked(int button)` | NOXNA | internal | maps FNA non-public |
| `NOXNA static void ResetForTests()` | NOXNA | no | CNA-only |

## `MouseCursor` — class (no FNA source (CNA-only))

| Member | Tag | In FNA | Note |
|--------|-----|--------|------|
| `NOXNA MouseCursor()` | NOXNA | no | CNA-only |
| `NOXNA explicit MouseCursor(SDL_Cursor* sdlCursor, bool owning = false)` | NOXNA | no | CNA-only |
| `NOXNA static MouseCursor FromTexture2D(const Graphics::Texture2D& texture, int originX, int originY)` | NOXNA | no | CNA-only |
| `MouseCursor(const MouseCursor&) = delete` | NOXNA | no | CNA-only |
| `MouseCursor& operator=(const MouseCursor&) = delete` | NOXNA | no | CNA-only |
| `MouseCursor(MouseCursor&& other) noexcept` | NOXNA | no | CNA-only |
| `~MouseCursor() override` | NOXNA | no | CNA-only |
| `NOXNA void Dispose() override` | NOXNA | no | CNA-only |
| `NOXNA SDL_Cursor* GetSDLCursor() const` | NOXNA | no | CNA-only |
| `NOXNA static MouseCursor& getArrowProperty()` | NOXNA | no | CNA-only |
| `NOXNA static MouseCursor& getCrosshairProperty()` | NOXNA | no | CNA-only |
| `NOXNA static MouseCursor& getHandProperty()` | NOXNA | no | CNA-only |
| `NOXNA static MouseCursor& getIBeamProperty()` | NOXNA | no | CNA-only |
| `NOXNA static MouseCursor& getNoProperty()` | NOXNA | no | CNA-only |
| `NOXNA static MouseCursor& getSizeAllProperty()` | NOXNA | no | CNA-only |
| `NOXNA static MouseCursor& getSizeNESWProperty()` | NOXNA | no | CNA-only |
| `NOXNA static MouseCursor& getSizeNSProperty()` | NOXNA | no | CNA-only |
| `NOXNA static MouseCursor& getSizeNWSEProperty()` | NOXNA | no | CNA-only |
| `NOXNA static MouseCursor& getSizeWEProperty()` | NOXNA | no | CNA-only |
| `NOXNA static MouseCursor& getWaitProperty()` | NOXNA | no | CNA-only |
| `NOXNA static MouseCursor& getWaitArrowProperty()` | NOXNA | no | CNA-only |

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
| `NOXNA int getHorizontalScrollWheelValueEXTProperty() const` | NOXNA | no | CNA-only |
| `NOXNA MouseState()` | NOXNA | yes | maps FNA non-public |
| `MouseState(int x, int y, int scrollWheel, ButtonState leftButton, ButtonState middleButton, ButtonState rightButton, ButtonState xButton1, ButtonState xButton2)` | STRICT | yes | matched |
| `NOXNA MouseState(int x, int y, int scrollWheel, ButtonState leftButton, ButtonState middleButton, ButtonState rightButton, ButtonState xButton1, ButtonState xButton2, int horizontalScrollWheel)` | NOXNA | yes | maps FNA non-public |
| `bool Equals(const MouseState& other) const` | STRICT | yes | matched |
| `int GetHashCode() const` | STRICT | yes | matched |
| `std::string ToString() const` | STRICT | yes | matched |
| `friend bool operator==(const MouseState& left, const MouseState& right)` | STRICT | yes | matched |
| `friend bool operator!=(const MouseState& left, const MouseState& right)` | STRICT | yes | matched |

## `TextInputEXT` — class (FNA `TextInputEXT.cs`)

| Member | Tag | In FNA | Note |
|--------|-----|--------|------|
| `TextInputEXT() = delete` | EXT | no | C++ special-member idiom (no XNA counterpart expected) |
| `NOXNA static System::MulticastAction<charcs> TextInput` | NOXNA | no | CNA-only |
| `NOXNA static System::MulticastAction<const std::string&, int, int> TextEditing` | NOXNA | no | CNA-only |
| `NOXNA static System::MulticastAction<const std::vector<std::string>&, int, bool> TextEditingCandidatesEXT` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `NOXNA static std::uintptr_t getWindowHandleProperty()` | NOXNA | yes | maps FNA non-public |
| `NOXNA static void setWindowHandleProperty(std::uintptr_t value)` | NOXNA | yes | maps FNA non-public |
| `NOXNA static bool IsTextInputActive()` | NOXNA | yes | maps FNA non-public |
| `NOXNA static bool IsScreenKeyboardShown()` | NOXNA | yes | maps FNA non-public |
| `NOXNA static bool IsScreenKeyboardShown(std::uintptr_t window)` | NOXNA | yes | maps FNA non-public |
| `NOXNA static void StartTextInput()` | NOXNA | yes | maps FNA non-public |
| `NOXNA static void StopTextInput()` | NOXNA | yes | maps FNA non-public |
| `NOXNA static void StartTextInputWithTypeEXT(CNA::Input::TextInputTypeEXT type)` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `NOXNA static void SetInputRectangle(const Microsoft::Xna::Framework::Rectangle& rectangle)` | NOXNA | yes | maps FNA non-public |
| `NOXNA static void INTERNAL_OnTextInput(charcs c)` | NOXNA | no | CNA-only |
| `NOXNA static void INTERNAL_OnTextEditing(const std::string& text, int start, int length)` | NOXNA | no | CNA-only |
| `NOXNA static void INTERNAL_OnTextEditingCandidates( const std::vector<std::string>& candidates, int selected, bool horizontal)` | NOXNA | no | CNA-only |
| `NOXNA static void ResetForTests()` | NOXNA | no | CNA-only |

## `TouchCollection` — struct (FNA `TouchCollection.cs`)

| Member | Tag | In FNA | Note |
|--------|-----|--------|------|
| `int getCountProperty() const` | STRICT | yes | matched |
| `bool getIsConnectedProperty() const` | STRICT | yes | matched |
| `bool getIsReadOnlyProperty() const` | STRICT | yes | matched |
| `NOXNA TouchCollection()` | NOXNA | yes | maps FNA non-public |
| `explicit TouchCollection(const std::vector<TouchLocation>& touches)` | STRICT | yes | matched |
| `explicit TouchCollection(std::vector<TouchLocation>&& touches)` | STRICT | yes | matched |
| `TouchLocation& operator[](std::size_t index)` | STRICT | yes | matched |
| `const TouchLocation& operator[](std::size_t index) const` | STRICT | yes | matched |
| `NOXNA bool empty() const` | NOXNA | no | CNA-only |
| `bool Contains(const TouchLocation& item) const` | STRICT | yes | matched |
| `bool FindById(int id, TouchLocation& touchLocation) const` | STRICT | yes | matched |
| `void CopyTo(std::vector<TouchLocation>& array, int arrayIndex) const` | STRICT | yes | matched |
| `int IndexOf(const TouchLocation& item) const` | STRICT | yes | matched |
| `void Add(const TouchLocation& item)` | STRICT | yes | matched |
| `void Clear()` | STRICT | yes | matched |
| `bool Remove(const TouchLocation& item)` | STRICT | yes | matched |
| `void RemoveAt(int index)` | STRICT | yes | matched |
| `void Insert(int index, const TouchLocation& item)` | STRICT | yes | matched |
| `NOXNA std::vector<TouchLocation>::iterator begin()` | NOXNA | no | CNA-only |
| `NOXNA std::vector<TouchLocation>::iterator end()` | NOXNA | no | CNA-only |
| `NOXNA std::vector<TouchLocation>::const_iterator begin() const` | NOXNA | no | CNA-only |
| `NOXNA std::vector<TouchLocation>::const_iterator end() const` | NOXNA | no | CNA-only |

## `TouchLocation` — struct (FNA `TouchLocation.cs`)

| Member | Tag | In FNA | Note |
|--------|-----|--------|------|
| `int getIdProperty() const` | STRICT | yes | matched |
| `TouchLocationState getStateProperty() const` | STRICT | yes | matched |
| `const Microsoft::Xna::Framework::Vector2& getPositionProperty() const` | STRICT | yes | matched |
| `NOXNA float getPressureEXT() const` | EXT | no | EXT extension (no stock-XNA counterpart expected) |
| `NOXNA TouchLocation()` | NOXNA | yes | maps FNA non-public |
| `TouchLocation(int id, TouchLocationState state, const Microsoft::Xna::Framework::Vector2& position)` | STRICT | yes | matched |
| `TouchLocation(int id, TouchLocationState state, const Microsoft::Xna::Framework::Vector2& position, TouchLocationState previousState, const Microsoft::Xna::Framework::Vector2& previousPosition)` | STRICT | yes | matched |
| `NOXNA TouchLocation(int id, TouchLocationState state, const Microsoft::Xna::Framework::Vector2& position, float pressure)` | NOXNA | yes | maps FNA non-public |
| `NOXNA TouchLocation(int id, TouchLocationState state, const Microsoft::Xna::Framework::Vector2& position, TouchLocationState previousState, const Microsoft::Xna::Framework::Vector2& previousPosition, float pressure)` | NOXNA | yes | maps FNA non-public |
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
| `NOXNA static constexpr intcs MAX_TOUCHES = 8` | NOXNA | internal | maps FNA non-public |
| `NOXNA static constexpr intcs NO_FINGER = -1` | NOXNA | internal | maps FNA non-public |
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
| `NOXNA static bool getTouchDeviceExistsProperty()` | NOXNA | no | CNA-only |
| `NOXNA static void setTouchDeviceExistsProperty(bool value)` | NOXNA | no | CNA-only |
| `static TouchPanelCapabilities GetCapabilities()` | STRICT | yes | matched |
| `static TouchCollection GetState()` | STRICT | yes | matched |
| `static GestureSample ReadGesture()` | STRICT | yes | matched |
| `NOXNA static void EnqueueGesture(const GestureSample& gesture)` | NOXNA | internal | maps FNA non-public |
| `NOXNA static void INTERNAL_onTouchEvent( intcs fingerId, TouchLocationState state, float x, float y, float dx, float dy )` | NOXNA | internal | maps FNA non-public |
| `NOXNA static void SetFinger(intcs index, intcs fingerId, const Microsoft::Xna::Framework::Vector2& fingerPos)` | NOXNA | internal | maps FNA non-public |
| `NOXNA static void Update()` | NOXNA | internal | maps FNA non-public |
| `NOXNA static void ResetForTests()` | NOXNA | no | CNA-only |

## `TouchPanelCapabilities` — struct (FNA `TouchPanelCapabilities.cs`)

| Member | Tag | In FNA | Note |
|--------|-----|--------|------|
| `bool getIsConnectedProperty() const` | STRICT | yes | matched |
| `int getMaximumTouchCountProperty() const` | STRICT | yes | matched |
| `NOXNA TouchPanelCapabilities()` | NOXNA | internal | maps FNA non-public |
| `NOXNA TouchPanelCapabilities(bool isConnected, int maximumTouchCount)` | NOXNA | internal | maps FNA non-public |

