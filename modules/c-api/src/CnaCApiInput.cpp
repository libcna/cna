// SPDX-License-Identifier: MS-PL

#include "CNA/C/input.h"
#include "CNA/C/input_gamepad.h"
#include "CNA/C/input_keyboard.h"
#include "CNA/C/input_mouse.h"
#include "CNA/C/input_cursor.h"
#include "CNA/C/input_text.h"
#include "CNA/C/input_touch.h"
#include "CnaCApiGraphicsDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"
#include "CnaCApiOutput.hpp"
#include "CnaCApiMarshal.hpp"
#include "CnaCApiDetail.hpp"

#include "Microsoft/Xna/Framework/Input/ButtonState.hpp"
#include "Microsoft/Xna/Framework/Input/Buttons.hpp"
#include "Microsoft/Xna/Framework/Input/GamePad.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadCapabilities.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadDeadZone.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadButtons.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadDPad.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadState.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadThumbSticks.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadTriggers.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadType.hpp"
#include "CNA/Input/GamePadButtonLabel.hpp"
#include "CNA/Input/KeyModifiers.hpp"
#include "CNA/Input/GamePadConnectionState.hpp"
#include "CNA/Input/PowerState.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"
#include "Microsoft/Xna/Framework/Input/Keys.hpp"
#include "Microsoft/Xna/Framework/Input/Mouse.hpp"
#include "Microsoft/Xna/Framework/Input/MouseCursor.hpp"
#include "Microsoft/Xna/Framework/Input/MouseState.hpp"
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "CNA/Input/TextInputType.hpp"
#include "System/MulticastAction.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/GestureSample.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/GestureType.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchLocation.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchLocationState.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchPanelCapabilities.hpp"
#include "Microsoft/Xna/Framework/DisplayOrientation.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"
#include "System/TimeSpan.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_set>
#include <string>
#include <vector>

namespace {

using CNA::C::Detail::BufferTooSmall;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CopyText;
using CNA::C::Detail::CreateHandle;
using CNA::C::Detail::InternalError;
using CNA::C::Detail::InvalidArgument;
using CNA::C::Detail::IsFinite;
using CNA::C::Detail::LookupHandle;
using CNA::C::Detail::ReleaseHandle;
using CNA::C::Detail::StructureVersion;
using CNA::C::Detail::ToCBool;
using CNA::C::Detail::TryMapPlayerIndex;
using CNA::C::Detail::ValidateActiveGameHandle;
using CNA::C::Detail::ValidateCanonicalBool;
using CNA::C::Detail::ValidateVersionedStructure;
using Microsoft::Xna::Framework::PlayerIndex;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Input::ButtonState;
using Microsoft::Xna::Framework::Input::Buttons;
using Microsoft::Xna::Framework::Input::GamePad;
using Microsoft::Xna::Framework::Input::GamePadButtons;
using Microsoft::Xna::Framework::Input::GamePadCapabilities;
using Microsoft::Xna::Framework::Input::GamePadDeadZone;
using Microsoft::Xna::Framework::Input::GamePadDPad;
using Microsoft::Xna::Framework::Input::GamePadState;
using Microsoft::Xna::Framework::Input::GamePadThumbSticks;
using Microsoft::Xna::Framework::Input::GamePadTriggers;
using Microsoft::Xna::Framework::Input::GamePadType;
using Microsoft::Xna::Framework::Input::Keyboard;
using Microsoft::Xna::Framework::Input::KeyboardState;
using Microsoft::Xna::Framework::Input::KeyState;
using Microsoft::Xna::Framework::Input::Keys;
using Microsoft::Xna::Framework::Input::Mouse;
using Microsoft::Xna::Framework::Input::MouseState;
using Microsoft::Xna::Framework::Input::Touch::TouchCollection;
using Microsoft::Xna::Framework::Input::Touch::TouchLocation;
using Microsoft::Xna::Framework::Input::Touch::TouchLocationState;
using Microsoft::Xna::Framework::Input::Touch::TouchPanel;
using Microsoft::Xna::Framework::Input::Touch::TouchPanelCapabilities;

constexpr CNA_Key KeyboardSlotCount = UINT32_C(256);

[[nodiscard]] CNA_Result ValidateKeyboardState(
    const CNA_KeyboardState* const state) noexcept
{
    return ValidateVersionedStructure(state, "The keyboard-state snapshot structure is invalid.");
}

[[nodiscard]] bool IsKeyDown(
    const CNA_KeyboardState& state,
    const CNA_Key key) noexcept
{
    const uint32_t wordIndex = key >> 6U;
    const uint32_t bitIndex = key & UINT32_C(63);
    return (state.pressed_key_words[wordIndex] & (UINT64_C(1) << bitIndex)) != 0U;
}

[[nodiscard]] uint32_t CountPressedKeys(const CNA_KeyboardState& state) noexcept
{
    uint32_t count = 0U;
    for (const uint64_t word : state.pressed_key_words) {
        count += static_cast<uint32_t>(std::popcount(word));
    }
    return count;
}

[[nodiscard]] bool TryMapDeadZone(
    const CNA_GamePadDeadZone deadZone,
    GamePadDeadZone* const outDeadZone) noexcept
{
    if (outDeadZone == nullptr || deadZone > CNA_GAMEPAD_DEAD_ZONE_CIRCULAR) {
        return false;
    }
    *outDeadZone = static_cast<GamePadDeadZone>(deadZone);
    return true;
}

[[nodiscard]] CNA_TouchLocationState MapTouchLocationState(
    const TouchLocationState state) noexcept
{
    return static_cast<CNA_TouchLocationState>(state);
}

[[nodiscard]] bool IsValidTouchLocationState(
    const CNA_TouchLocationState state) noexcept
{
    return state <= CNA_TOUCH_LOCATION_MOVED;
}

[[nodiscard]] CNA_TouchLocation MakeInvalidTouchLocation() noexcept
{
    return CNA_TouchLocation{
        -1,
        CNA_TOUCH_LOCATION_INVALID,
        {0.0F, 0.0F},
        CNA_TOUCH_LOCATION_INVALID,
        {0.0F, 0.0F},
        0.0F
    };
}

[[nodiscard]] CNA_TouchLocation MapTouchLocation(const TouchLocation& location)
{
    TouchLocation previous;
    static_cast<void>(location.TryGetPreviousLocation(previous));
    const Vector2& position = location.getPositionProperty();
    const Vector2& previousPosition = previous.getPositionProperty();
    return CNA_TouchLocation{
        location.getIdProperty(),
        MapTouchLocationState(location.getStateProperty()),
        {position.X, position.Y},
        MapTouchLocationState(previous.getStateProperty()),
        {previousPosition.X, previousPosition.Y},
        location.getPressureEXT()
    };
}

[[nodiscard]] CNA_Result ValidateGamePadState(
    const CNA_GamePadState* const state) noexcept
{
    return ValidateVersionedStructure(
        state,
        "The gamepad-state snapshot structure is invalid.");
}

[[nodiscard]] CNA_Result ValidateTouchState(
    const CNA_TouchState* const state) noexcept
{
    if (const CNA_Result result = ValidateVersionedStructure(
            state,
            "The touch-state snapshot structure is invalid.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (state->touch_count > CNA_TOUCH_MAX_TOUCHES) {
        return InvalidArgument("The touch-state snapshot count exceeds its fixed capacity.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] bool IsFinite(const CNA_GamePadAnalogState& value) noexcept
{
    return std::isfinite(value.left_thumb_stick.x) &&
        std::isfinite(value.left_thumb_stick.y) &&
        std::isfinite(value.right_thumb_stick.x) &&
        std::isfinite(value.right_thumb_stick.y) &&
        std::isfinite(value.left_trigger) &&
        std::isfinite(value.right_trigger);
}

[[nodiscard]] float ExcludeAxisDeadZone(
    float value,
    const float deadZone) noexcept
{
    if (value < -deadZone) {
        value += deadZone;
    } else if (value > deadZone) {
        value -= deadZone;
    } else {
        return 0.0F;
    }
    return value / (1.0F - deadZone);
}

[[nodiscard]] CNA_Vector2 ApplyCircularDeadZone(
    const CNA_Vector2 value,
    const float deadZone) noexcept
{
    const float originalLength = std::sqrt(value.x * value.x + value.y * value.y);
    if (originalLength <= deadZone) {
        return CNA_Vector2{0.0F, 0.0F};
    }

    const float newLength = (originalLength - deadZone) / (1.0F - deadZone);
    CNA_Vector2 result = {
        value.x * (newLength / originalLength),
        value.y * (newLength / originalLength)
    };
    const float resultLengthSquared = result.x * result.x + result.y * result.y;
    if (resultLengthSquared > 1.0F) {
        const float inverseLength = 1.0F / std::sqrt(resultLengthSquared);
        result.x *= inverseLength;
        result.y *= inverseLength;
    }
    return result;
}

[[nodiscard]] CNA_GamePadAnalogState ApplyDeadZone(
    const CNA_GamePadAnalogState& raw,
    const CNA_GamePadDeadZone deadZone) noexcept
{
    CNA_GamePadAnalogState result = raw;
    if (deadZone == CNA_GAMEPAD_DEAD_ZONE_INDEPENDENT_AXES) {
        result.left_thumb_stick.x = ExcludeAxisDeadZone(
            result.left_thumb_stick.x,
            CNA_GAMEPAD_LEFT_DEAD_ZONE);
        result.left_thumb_stick.y = ExcludeAxisDeadZone(
            result.left_thumb_stick.y,
            CNA_GAMEPAD_LEFT_DEAD_ZONE);
        result.right_thumb_stick.x = ExcludeAxisDeadZone(
            result.right_thumb_stick.x,
            CNA_GAMEPAD_RIGHT_DEAD_ZONE);
        result.right_thumb_stick.y = ExcludeAxisDeadZone(
            result.right_thumb_stick.y,
            CNA_GAMEPAD_RIGHT_DEAD_ZONE);
    } else if (deadZone == CNA_GAMEPAD_DEAD_ZONE_CIRCULAR) {
        result.left_thumb_stick = ApplyCircularDeadZone(
            result.left_thumb_stick,
            CNA_GAMEPAD_LEFT_DEAD_ZONE);
        result.right_thumb_stick = ApplyCircularDeadZone(
            result.right_thumb_stick,
            CNA_GAMEPAD_RIGHT_DEAD_ZONE);
    }

    if (deadZone != CNA_GAMEPAD_DEAD_ZONE_NONE) {
        result.left_trigger = ExcludeAxisDeadZone(
            result.left_trigger,
            CNA_GAMEPAD_TRIGGER_THRESHOLD);
        result.right_trigger = ExcludeAxisDeadZone(
            result.right_trigger,
            CNA_GAMEPAD_TRIGGER_THRESHOLD);
    }
    if (deadZone != CNA_GAMEPAD_DEAD_ZONE_CIRCULAR) {
        result.left_thumb_stick.x = std::clamp(result.left_thumb_stick.x, -1.0F, 1.0F);
        result.left_thumb_stick.y = std::clamp(result.left_thumb_stick.y, -1.0F, 1.0F);
        result.right_thumb_stick.x = std::clamp(result.right_thumb_stick.x, -1.0F, 1.0F);
        result.right_thumb_stick.y = std::clamp(result.right_thumb_stick.y, -1.0F, 1.0F);
    }
    result.left_trigger = std::clamp(result.left_trigger, 0.0F, 1.0F);
    result.right_trigger = std::clamp(result.right_trigger, 0.0F, 1.0F);
    return result;
}

[[nodiscard]] CNA_GamePadButtonFlags CollectPressedButtons(
    const GamePadState& state) noexcept
{
    CNA_GamePadButtonFlags buttons = CNA_GAMEPAD_BUTTON_NONE;
    for (uint32_t bit = 0U; bit < 31U; ++bit) {
        const uint32_t mask = UINT32_C(1) << bit;
        if (state.IsButtonDown(static_cast<Buttons>(mask))) {
            buttons |= mask;
        }
    }
    return buttons;
}

[[nodiscard]] CNA_Result CaptureGamePadState(
    const CNA_Handle gameHandle,
    const CNA_PlayerIndex playerIndex,
    const CNA_GamePadDeadZone deadZone,
    CNA_GamePadState* const outState)
{
    if (const CNA_Result result = ValidateVersionedStructure(
            outState,
            "The gamepad-state output structure is invalid.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    PlayerIndex nativePlayerIndex = PlayerIndex::One;
    GamePadDeadZone nativeDeadZone = GamePadDeadZone::None;
    if (!TryMapPlayerIndex(playerIndex, &nativePlayerIndex) ||
        !TryMapDeadZone(deadZone, &nativeDeadZone)) {
        return InvalidArgument("The gamepad player index or dead-zone mode is invalid.");
    }
    if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }

    const GamePadState nativeState = GamePad::GetState(nativePlayerIndex, nativeDeadZone);
    const Vector2& leftStick = nativeState.getThumbSticksProperty().getLeftProperty();
    const Vector2& rightStick = nativeState.getThumbSticksProperty().getRightProperty();
    const CNA_GamePadState snapshot = {
        sizeof(CNA_GamePadState),
        StructureVersion,
        ToCBool(nativeState.getIsConnectedProperty()),
        {0U, 0U, 0U},
        nativeState.getPacketNumberProperty(),
        CollectPressedButtons(nativeState),
        0U,
        {
            {leftStick.X, leftStick.Y},
            {rightStick.X, rightStick.Y},
            nativeState.getTriggersProperty().getLeftProperty(),
            nativeState.getTriggersProperty().getRightProperty()
        }
    };
    *outState = snapshot;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result KeyboardGetState(
    const CNA_Handle gameHandle,
    CNA_KeyboardState* const outState)
{
    if (const CNA_Result result = ValidateVersionedStructure(
            outState, "The keyboard-state output structure is invalid.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }

    CNA_KeyboardState snapshot = {
        sizeof(CNA_KeyboardState),
        StructureVersion,
        {UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0)}
    };
    const KeyboardState nativeState = Keyboard::GetState();
    for (const Keys key : nativeState.GetPressedKeys()) {
        const int value = static_cast<int>(key);
        if (value >= 0 && value < static_cast<int>(KeyboardSlotCount)) {
            const auto keyValue = static_cast<uint32_t>(value);
            snapshot.pressed_key_words[keyValue >> 6U] |=
                UINT64_C(1) << (keyValue & UINT32_C(63));
        }
    }
    *outState = snapshot;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result KeyboardStateCopyPressedKeys(
    const CNA_KeyboardState* const state,
    CNA_Key* const destination,
    const uint64_t capacity,
    uint32_t* const outCount)
{
    if ((destination == nullptr && capacity != 0U)) {
        return InvalidArgument("The pressed-key output buffer is invalid.");
    }
    if (const CNA_Result result = ValidateKeyboardState(state);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const uint32_t requiredCount = CountPressedKeys(*state);
    *outCount = requiredCount;
    if (capacity < requiredCount) {
        return BufferTooSmall("The pressed-key output buffer is too small.");
    }

    uint32_t destinationIndex = 0U;
    for (CNA_Key key = 0U; key < KeyboardSlotCount; ++key) {
        if (IsKeyDown(*state, key)) {
            destination[destinationIndex] = key;
            ++destinationIndex;
        }
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result MouseGetState(
    const CNA_Handle gameHandle,
    CNA_MouseState* const outState)
{
    if (const CNA_Result result = ValidateVersionedStructure(
            outState,
            "The mouse-state output structure is invalid.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }

    const MouseState nativeState = Mouse::GetState();
    CNA_MouseButtonFlags pressedButtons = CNA_MOUSE_BUTTON_NONE;
    if (nativeState.getLeftButtonProperty() == ButtonState::Pressed) {
        pressedButtons |= CNA_MOUSE_BUTTON_LEFT;
    }
    if (nativeState.getMiddleButtonProperty() == ButtonState::Pressed) {
        pressedButtons |= CNA_MOUSE_BUTTON_MIDDLE;
    }
    if (nativeState.getRightButtonProperty() == ButtonState::Pressed) {
        pressedButtons |= CNA_MOUSE_BUTTON_RIGHT;
    }
    if (nativeState.getXButton1Property() == ButtonState::Pressed) {
        pressedButtons |= CNA_MOUSE_BUTTON_X1;
    }
    if (nativeState.getXButton2Property() == ButtonState::Pressed) {
        pressedButtons |= CNA_MOUSE_BUTTON_X2;
    }
    const CNA_MouseState snapshot = {
        sizeof(CNA_MouseState),
        StructureVersion,
        nativeState.getXProperty(),
        nativeState.getYProperty(),
        nativeState.getScrollWheelValueProperty(),
        nativeState.getHorizontalScrollWheelValueEXTProperty(),
        pressedButtons,
        0U
    };
    *outState = snapshot;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result TouchGetCapabilities(
    const CNA_Handle gameHandle,
    CNA_TouchCapabilities* const outCapabilities)
{
    if (const CNA_Result result = ValidateVersionedStructure(
            outCapabilities,
            "The touch-capabilities output structure is invalid.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }

    const TouchPanelCapabilities nativeCapabilities = TouchPanel::GetCapabilities();
    const int maximumTouchCount = nativeCapabilities.getMaximumTouchCountProperty();
    if (maximumTouchCount < 0) {
        return InternalError("CNA returned a negative maximum touch count.");
    }
    const CNA_TouchCapabilities capabilities = {
        sizeof(CNA_TouchCapabilities),
        StructureVersion,
        ToCBool(nativeCapabilities.getIsConnectedProperty()),
        {0U, 0U, 0U},
        static_cast<uint32_t>(maximumTouchCount)
    };
    *outCapabilities = capabilities;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result TouchGetState(
    const CNA_Handle gameHandle,
    CNA_TouchState* const outState)
{
    if (const CNA_Result result = ValidateVersionedStructure(
            outState,
            "The touch-state output structure is invalid.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }

    const TouchCollection nativeState = TouchPanel::GetState();
    const int nativeCount = nativeState.getCountProperty();
    if (nativeCount < 0 || nativeCount > static_cast<int>(CNA_TOUCH_MAX_TOUCHES)) {
        return InternalError("CNA returned more touches than the fixed C snapshot can hold.");
    }
    CNA_TouchState snapshot = {};
    snapshot.struct_size = sizeof(CNA_TouchState);
    snapshot.struct_version = StructureVersion;
    snapshot.is_connected = ToCBool(nativeState.getIsConnectedProperty());
    snapshot.touch_count = static_cast<uint32_t>(nativeCount);
    for (uint32_t index = 0U; index < snapshot.touch_count; ++index) {
        snapshot.touches[index] = MapTouchLocation(nativeState[index]);
    }
    *outState = snapshot;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result TouchStateFindById(
    const CNA_TouchState* const state,
    const int32_t id,
    CNA_TouchLocation* const outLocation,
    CNA_Bool* const outFound)
{
    if (const CNA_Result result = ValidateTouchState(state);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    CNA_TouchLocation result = MakeInvalidTouchLocation();
    CNA_Bool found = CNA_FALSE;
    for (uint32_t index = 0U; index < state->touch_count; ++index) {
        if (state->touches[index].id == id) {
            result = state->touches[index];
            found = CNA_TRUE;
            break;
        }
    }
    *outLocation = result;
    *outFound = found;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result TouchLocationTryGetPrevious(
    const CNA_TouchLocation* const location,
    CNA_TouchLocation* const outPrevious,
    CNA_Bool* const outFound)
{
    if (location == nullptr || !IsValidTouchLocationState(location->state) ||
        !IsValidTouchLocationState(location->previous_state)) {
        return InvalidArgument("The touch previous-location query arguments are invalid.");
    }
    const CNA_TouchLocation previous = {
        location->id,
        location->previous_state,
        location->previous_position,
        CNA_TOUCH_LOCATION_INVALID,
        {0.0F, 0.0F},
        0.0F
    };
    *outPrevious = previous;
    *outFound = location->previous_state == CNA_TOUCH_LOCATION_INVALID
        ? CNA_FALSE
        : CNA_TRUE;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_GamePadType MapGamePadType(const GamePadType type) noexcept
{
    switch (type) {
        case GamePadType::Unknown: return CNA_GAMEPAD_TYPE_UNKNOWN;
        case GamePadType::GamePad: return CNA_GAMEPAD_TYPE_GAMEPAD;
        case GamePadType::Wheel: return CNA_GAMEPAD_TYPE_WHEEL;
        case GamePadType::ArcadeStick: return CNA_GAMEPAD_TYPE_ARCADE_STICK;
        case GamePadType::FlightStick: return CNA_GAMEPAD_TYPE_FLIGHT_STICK;
        case GamePadType::DancePad: return CNA_GAMEPAD_TYPE_DANCE_PAD;
        case GamePadType::Guitar: return CNA_GAMEPAD_TYPE_GUITAR;
        case GamePadType::AlternateGuitar: return CNA_GAMEPAD_TYPE_ALTERNATE_GUITAR;
        case GamePadType::DrumKit: return CNA_GAMEPAD_TYPE_DRUM_KIT;
        case GamePadType::BigButtonPad: return CNA_GAMEPAD_TYPE_BIG_BUTTON_PAD;
    }
    return CNA_GAMEPAD_TYPE_UNKNOWN;
}

[[nodiscard]] CNA_Bool Flag(const bool value) noexcept
{
    return ToCBool(value);
}

[[nodiscard]] CNA_GamePadCapabilities SnapshotCapabilities(const GamePadCapabilities& value)
{
    CNA_GamePadCapabilities snapshot;
    snapshot.struct_size = sizeof(CNA_GamePadCapabilities);
    snapshot.struct_version = StructureVersion;
    snapshot.gamepad_type = MapGamePadType(value.getGamePadTypeProperty());
    snapshot.is_connected = Flag(value.getIsConnectedProperty());
    snapshot.has_a_button = Flag(value.getHasAButtonProperty());
    snapshot.has_b_button = Flag(value.getHasBButtonProperty());
    snapshot.has_x_button = Flag(value.getHasXButtonProperty());
    snapshot.has_y_button = Flag(value.getHasYButtonProperty());
    snapshot.has_back_button = Flag(value.getHasBackButtonProperty());
    snapshot.has_start_button = Flag(value.getHasStartButtonProperty());
    snapshot.has_big_button = Flag(value.getHasBigButtonProperty());
    snapshot.has_dpad_up_button = Flag(value.getHasDPadUpButtonProperty());
    snapshot.has_dpad_down_button = Flag(value.getHasDPadDownButtonProperty());
    snapshot.has_dpad_left_button = Flag(value.getHasDPadLeftButtonProperty());
    snapshot.has_dpad_right_button = Flag(value.getHasDPadRightButtonProperty());
    snapshot.has_left_shoulder_button = Flag(value.getHasLeftShoulderButtonProperty());
    snapshot.has_right_shoulder_button = Flag(value.getHasRightShoulderButtonProperty());
    snapshot.has_left_stick_button = Flag(value.getHasLeftStickButtonProperty());
    snapshot.has_right_stick_button = Flag(value.getHasRightStickButtonProperty());
    snapshot.has_left_x_thumb_stick = Flag(value.getHasLeftXThumbStickProperty());
    snapshot.has_left_y_thumb_stick = Flag(value.getHasLeftYThumbStickProperty());
    snapshot.has_right_x_thumb_stick = Flag(value.getHasRightXThumbStickProperty());
    snapshot.has_right_y_thumb_stick = Flag(value.getHasRightYThumbStickProperty());
    snapshot.has_left_trigger = Flag(value.getHasLeftTriggerProperty());
    snapshot.has_right_trigger = Flag(value.getHasRightTriggerProperty());
    snapshot.has_left_vibration_motor = Flag(value.getHasLeftVibrationMotorProperty());
    snapshot.has_right_vibration_motor = Flag(value.getHasRightVibrationMotorProperty());
    snapshot.has_voice_support = Flag(value.getHasVoiceSupportProperty());
    snapshot.has_light_bar_ext = Flag(value.getHasLightBarEXTProperty());
    snapshot.has_trigger_vibration_motors_ext =
        Flag(value.getHasTriggerVibrationMotorsEXTProperty());
    snapshot.has_misc1_ext = Flag(value.getHasMisc1EXTProperty());
    snapshot.has_paddle1_ext = Flag(value.getHasPaddle1EXTProperty());
    snapshot.has_paddle2_ext = Flag(value.getHasPaddle2EXTProperty());
    snapshot.has_paddle3_ext = Flag(value.getHasPaddle3EXTProperty());
    snapshot.has_paddle4_ext = Flag(value.getHasPaddle4EXTProperty());
    snapshot.has_touchpad_ext = Flag(value.getHasTouchPadEXTProperty());
    snapshot.has_gyro_ext = Flag(value.getHasGyroEXTProperty());
    snapshot.has_accelerometer_ext = Flag(value.getHasAccelerometerEXTProperty());
    snapshot.reserved[0] = UINT8_C(0);
    return snapshot;
}

[[nodiscard]] CNA_Result GamepadGetCapabilities(
    const CNA_Handle gameHandle,
    const CNA_PlayerIndex playerIndex,
    CNA_GamePadCapabilities* const outCapabilities)
{
    if (const CNA_Result result = ValidateVersionedStructure(
            outCapabilities,
            "The gamepad-capabilities output structure is invalid.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    PlayerIndex nativePlayerIndex = PlayerIndex::One;
    if (!TryMapPlayerIndex(playerIndex, &nativePlayerIndex)) {
        return InvalidArgument("The gamepad player index is invalid.");
    }
    if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outCapabilities = SnapshotCapabilities(GamePad::GetCapabilities(nativePlayerIndex));
    return CNA_RESULT_SUCCESS;
}

constexpr CNA_GamePadButtonFlags DPadMask = CNA_GAMEPAD_BUTTON_DPAD_UP |
    CNA_GAMEPAD_BUTTON_DPAD_DOWN | CNA_GAMEPAD_BUTTON_DPAD_LEFT | CNA_GAMEPAD_BUTTON_DPAD_RIGHT;

[[nodiscard]] CNA_Result ValidateMask(const CNA_GamePadButtonFlags mask)
{
    if ((mask & ~CNA_GAMEPAD_BUTTON_ALL) != 0U) {
        return InvalidArgument("The gamepad button mask contains an undefined bit.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CombineButtonArray(
    const CNA_GamePadButtonFlags* const buttons,
    const uint64_t count,
    CNA_GamePadButtonFlags* const outMask)
{
    if (buttons == nullptr && count != 0U) {
        return InvalidArgument("The gamepad button array is invalid.");
    }
    CNA_GamePadButtonFlags mask = CNA_GAMEPAD_BUTTON_NONE;
    for (uint64_t index = 0U; index < count; ++index) {
        if (const CNA_Result result = ValidateMask(buttons[index]);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        mask |= buttons[index];
    }
    *outMask = mask;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] GamePadButtons NativeButtons(const CNA_GamePadButtonFlags mask)
{
    return GamePadButtons(static_cast<Buttons>(mask));
}

// The canonical button set exposes eleven named getters and no way to read its raw field, so the
// projection back to a mask asks it about each of them. Used for the default constructor, whose
// emptiness is therefore observed rather than assumed.
[[nodiscard]] CNA_GamePadButtonFlags MaskFromNamedButtons(const GamePadButtons& value)
{
    CNA_GamePadButtonFlags mask = CNA_GAMEPAD_BUTTON_NONE;
    const auto add = [&mask](const ButtonState state, const CNA_GamePadButtonFlags bit) {
        if (state == ButtonState::Pressed) {
            mask |= bit;
        }
    };
    add(value.getAProperty(), CNA_GAMEPAD_BUTTON_A);
    add(value.getBProperty(), CNA_GAMEPAD_BUTTON_B);
    add(value.getXProperty(), CNA_GAMEPAD_BUTTON_X);
    add(value.getYProperty(), CNA_GAMEPAD_BUTTON_Y);
    add(value.getBackProperty(), CNA_GAMEPAD_BUTTON_BACK);
    add(value.getStartProperty(), CNA_GAMEPAD_BUTTON_START);
    add(value.getBigButtonProperty(), CNA_GAMEPAD_BUTTON_BIG_BUTTON);
    add(value.getLeftShoulderProperty(), CNA_GAMEPAD_BUTTON_LEFT_SHOULDER);
    add(value.getRightShoulderProperty(), CNA_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    add(value.getLeftStickProperty(), CNA_GAMEPAD_BUTTON_LEFT_STICK);
    add(value.getRightStickProperty(), CNA_GAMEPAD_BUTTON_RIGHT_STICK);
    return mask;
}

// A named button is answered through the canonical getter that owns it; the remaining identities
// -- the directional pad, the triggers and the virtual stick directions -- have no canonical
// getter on this type, so they fall back to the same masked test the canonical helper performs.
[[nodiscard]] bool NamedButtonIsPressed(
    const GamePadButtons& value,
    const CNA_GamePadButtonFlags button,
    bool* const outHandled)
{
    *outHandled = true;
    switch (button) {
        case CNA_GAMEPAD_BUTTON_A: return value.getAProperty() == ButtonState::Pressed;
        case CNA_GAMEPAD_BUTTON_B: return value.getBProperty() == ButtonState::Pressed;
        case CNA_GAMEPAD_BUTTON_X: return value.getXProperty() == ButtonState::Pressed;
        case CNA_GAMEPAD_BUTTON_Y: return value.getYProperty() == ButtonState::Pressed;
        case CNA_GAMEPAD_BUTTON_BACK: return value.getBackProperty() == ButtonState::Pressed;
        case CNA_GAMEPAD_BUTTON_START: return value.getStartProperty() == ButtonState::Pressed;
        case CNA_GAMEPAD_BUTTON_BIG_BUTTON:
            return value.getBigButtonProperty() == ButtonState::Pressed;
        case CNA_GAMEPAD_BUTTON_LEFT_SHOULDER:
            return value.getLeftShoulderProperty() == ButtonState::Pressed;
        case CNA_GAMEPAD_BUTTON_RIGHT_SHOULDER:
            return value.getRightShoulderProperty() == ButtonState::Pressed;
        case CNA_GAMEPAD_BUTTON_LEFT_STICK:
            return value.getLeftStickProperty() == ButtonState::Pressed;
        case CNA_GAMEPAD_BUTTON_RIGHT_STICK:
            return value.getRightStickProperty() == ButtonState::Pressed;
        default: break;
    }
    *outHandled = false;
    return false;
}

[[nodiscard]] GamePadDPad NativeDPad(const CNA_GamePadButtonFlags mask)
{
    const auto state = [mask](const CNA_GamePadButtonFlags bit) {
        return (mask & bit) == bit ? ButtonState::Pressed : ButtonState::Released;
    };
    return GamePadDPad(
        state(CNA_GAMEPAD_BUTTON_DPAD_UP),
        state(CNA_GAMEPAD_BUTTON_DPAD_DOWN),
        state(CNA_GAMEPAD_BUTTON_DPAD_LEFT),
        state(CNA_GAMEPAD_BUTTON_DPAD_RIGHT));
}

[[nodiscard]] GamePadThumbSticks NativeThumbSticks(const CNA_GamePadThumbSticks& value)
{
    return GamePadThumbSticks(
        Vector2(value.left.x, value.left.y),
        Vector2(value.right.x, value.right.y));
}

[[nodiscard]] GamePadTriggers NativeTriggers(const CNA_GamePadTriggers& value)
{
    return GamePadTriggers(value.left, value.right);
}

[[nodiscard]] CNA_GamePadThumbSticks ToCThumbSticks(const GamePadThumbSticks& value)
{
    return CNA_GamePadThumbSticks{
        {value.getLeftProperty().X, value.getLeftProperty().Y},
        {value.getRightProperty().X, value.getRightProperty().Y}};
}

[[nodiscard]] CNA_GamePadTriggers ToCTriggers(const GamePadTriggers& value)
{
    return CNA_GamePadTriggers{value.getLeftProperty(), value.getRightProperty()};
}

// The sticks and triggers compare through their canonical types, as GamePadState.Equals does.
[[nodiscard]] bool GamePadStatesEqual(
    const CNA_GamePadState& left,
    const CNA_GamePadState& right)
{
    return left.is_connected == right.is_connected &&
        left.packet_number == right.packet_number &&
        left.pressed_buttons == right.pressed_buttons &&
        NativeThumbSticks({left.analog.left_thumb_stick, left.analog.right_thumb_stick})
            .Equals(NativeThumbSticks(
                {right.analog.left_thumb_stick, right.analog.right_thumb_stick})) &&
        NativeTriggers({left.analog.left_trigger, left.analog.right_trigger})
            .Equals(NativeTriggers({right.analog.left_trigger, right.analog.right_trigger}));
}

[[nodiscard]] CNA_GamePadState SnapshotState(const GamePadState& native)
{
    const Vector2& leftStick = native.getThumbSticksProperty().getLeftProperty();
    const Vector2& rightStick = native.getThumbSticksProperty().getRightProperty();
    CNA_GamePadState snapshot = {
        sizeof(CNA_GamePadState),
        StructureVersion,
        ToCBool(native.getIsConnectedProperty()),
        {0U, 0U, 0U},
        native.getPacketNumberProperty(),
        CollectPressedButtons(native),
        0U,
        {
            {leftStick.X, leftStick.Y},
            {rightStick.X, rightStick.Y},
            native.getTriggersProperty().getLeftProperty(),
            native.getTriggersProperty().getRightProperty()
        }
    };
    return snapshot;
}

[[nodiscard]] CNA_Result GamepadButtonsInitFromButtonArray(
    const CNA_GamePadButtonFlags* const buttons,
    const uint64_t count,
    CNA_GamePadButtonFlags* const outButtons)
{
    CNA_GamePadButtonFlags mask = CNA_GAMEPAD_BUTTON_NONE;
    if (const CNA_Result result = CombineButtonArray(buttons, count, &mask);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outButtons = mask;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GamepadButtonsIsPressed(
    const CNA_GamePadButtonFlags buttons,
    const CNA_GamePadButtonFlags button,
    CNA_Bool* const outIsPressed)
{
    if (const CNA_Result result = ValidateMask(buttons); result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ValidateMask(button); result != CNA_RESULT_SUCCESS) {
        return result;
    }
    bool handled = false;
    const bool pressed = NamedButtonIsPressed(NativeButtons(buttons), button, &handled);
    *outIsPressed = ToCBool(handled ? pressed : (buttons & button) == button);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GamepadDpadInitFromStates(
    const CNA_Bool up,
    const CNA_Bool down,
    const CNA_Bool left,
    const CNA_Bool right,
    CNA_GamePadButtonFlags* const outDPad)
{
    CNA_GamePadButtonFlags mask = CNA_GAMEPAD_BUTTON_NONE;
    if (up != CNA_FALSE) { mask |= CNA_GAMEPAD_BUTTON_DPAD_UP; }
    if (down != CNA_FALSE) { mask |= CNA_GAMEPAD_BUTTON_DPAD_DOWN; }
    if (left != CNA_FALSE) { mask |= CNA_GAMEPAD_BUTTON_DPAD_LEFT; }
    if (right != CNA_FALSE) { mask |= CNA_GAMEPAD_BUTTON_DPAD_RIGHT; }
    *outDPad = mask;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GamepadDpadInitFromButtonArray(
    const CNA_GamePadButtonFlags* const buttons,
    const uint64_t count,
    CNA_GamePadButtonFlags* const outDPad)
{
    CNA_GamePadButtonFlags mask = CNA_GAMEPAD_BUTTON_NONE;
    if (const CNA_Result result = CombineButtonArray(buttons, count, &mask);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outDPad = mask & DPadMask;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GamepadDpadIsPressed(
    const CNA_GamePadButtonFlags dpad,
    const CNA_GamePadButtonFlags button,
    CNA_Bool* const outIsPressed)
{
    if (button != CNA_GAMEPAD_BUTTON_DPAD_UP && button != CNA_GAMEPAD_BUTTON_DPAD_DOWN &&
        button != CNA_GAMEPAD_BUTTON_DPAD_LEFT && button != CNA_GAMEPAD_BUTTON_DPAD_RIGHT) {
        return InvalidArgument("The requested button is not a directional-pad direction.");
    }
    const GamePadDPad native = NativeDPad(dpad);
    ButtonState state = ButtonState::Released;
    switch (button) {
        case CNA_GAMEPAD_BUTTON_DPAD_UP: state = native.getUpProperty(); break;
        case CNA_GAMEPAD_BUTTON_DPAD_DOWN: state = native.getDownProperty(); break;
        case CNA_GAMEPAD_BUTTON_DPAD_LEFT: state = native.getLeftProperty(); break;
        default: state = native.getRightProperty(); break;
    }
    *outIsPressed = ToCBool(state == ButtonState::Pressed);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GamepadStateInitFromValues(
    const CNA_Vector2* const leftThumbStick,
    const CNA_Vector2* const rightThumbStick,
    const float leftTrigger,
    const float rightTrigger,
    const CNA_GamePadButtonFlags* const buttons,
    const uint64_t count,
    CNA_GamePadState* const outState)
{
    if (leftThumbStick == nullptr || rightThumbStick == nullptr) {
        return InvalidArgument("The gamepad snapshot input or output is null.");
    }
    CNA_GamePadButtonFlags mask = CNA_GAMEPAD_BUTTON_NONE;
    if (const CNA_Result result = CombineButtonArray(buttons, count, &mask);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    // The canonical convenience constructor feeds the same array to both the button set and
    // the directional pad, so the C route does exactly that rather than taking two masks.
    *outState = SnapshotState(GamePadState(
        GamePadThumbSticks(
            Vector2(leftThumbStick->x, leftThumbStick->y),
            Vector2(rightThumbStick->x, rightThumbStick->y)),
        GamePadTriggers(leftTrigger, rightTrigger),
        NativeButtons(mask),
        NativeDPad(mask)));
    return CNA_RESULT_SUCCESS;
}

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector3;

[[nodiscard]] CNA_Result BeginGamePadCall(
    const CNA_Handle gameHandle,
    const CNA_PlayerIndex playerIndex,
    PlayerIndex* const outPlayerIndex)
{
    if (!TryMapPlayerIndex(playerIndex, outPlayerIndex)) {
        return InvalidArgument("The gamepad player index is invalid.");
    }
    return ValidateActiveGameHandle(gameHandle);
}

[[nodiscard]] CNA_GamePadButtonLabel MapButtonLabel(
    const CNA::Input::GamePadButtonLabelEXT label) noexcept
{
    switch (label) {
        case CNA::Input::GamePadButtonLabelEXT::Unknown:
            return CNA_GAMEPAD_BUTTON_LABEL_UNKNOWN;
        case CNA::Input::GamePadButtonLabelEXT::A: return CNA_GAMEPAD_BUTTON_LABEL_A;
        case CNA::Input::GamePadButtonLabelEXT::B: return CNA_GAMEPAD_BUTTON_LABEL_B;
        case CNA::Input::GamePadButtonLabelEXT::X: return CNA_GAMEPAD_BUTTON_LABEL_X;
        case CNA::Input::GamePadButtonLabelEXT::Y: return CNA_GAMEPAD_BUTTON_LABEL_Y;
        case CNA::Input::GamePadButtonLabelEXT::Cross: return CNA_GAMEPAD_BUTTON_LABEL_CROSS;
        case CNA::Input::GamePadButtonLabelEXT::Circle: return CNA_GAMEPAD_BUTTON_LABEL_CIRCLE;
        case CNA::Input::GamePadButtonLabelEXT::Square: return CNA_GAMEPAD_BUTTON_LABEL_SQUARE;
        case CNA::Input::GamePadButtonLabelEXT::Triangle:
            return CNA_GAMEPAD_BUTTON_LABEL_TRIANGLE;
    }
    return CNA_GAMEPAD_BUTTON_LABEL_UNKNOWN;
}

[[nodiscard]] CNA_GamePadConnectionState MapConnectionState(
    const CNA::Input::GamePadConnectionStateEXT state) noexcept
{
    switch (state) {
        case CNA::Input::GamePadConnectionStateEXT::Unknown:
            return CNA_GAMEPAD_CONNECTION_STATE_UNKNOWN;
        case CNA::Input::GamePadConnectionStateEXT::Wired:
            return CNA_GAMEPAD_CONNECTION_STATE_WIRED;
        case CNA::Input::GamePadConnectionStateEXT::Wireless:
            return CNA_GAMEPAD_CONNECTION_STATE_WIRELESS;
    }
    return CNA_GAMEPAD_CONNECTION_STATE_UNKNOWN;
}

[[nodiscard]] CNA_PowerState MapPowerState(const CNA::Input::PowerStateEXT state) noexcept
{
    switch (state) {
        case CNA::Input::PowerStateEXT::Error: return CNA_POWER_STATE_ERROR;
        case CNA::Input::PowerStateEXT::Unknown: return CNA_POWER_STATE_UNKNOWN;
        case CNA::Input::PowerStateEXT::OnBattery: return CNA_POWER_STATE_ON_BATTERY;
        case CNA::Input::PowerStateEXT::NoBattery: return CNA_POWER_STATE_NO_BATTERY;
        case CNA::Input::PowerStateEXT::Charging: return CNA_POWER_STATE_CHARGING;
        case CNA::Input::PowerStateEXT::Charged: return CNA_POWER_STATE_CHARGED;
    }
    return CNA_POWER_STATE_UNKNOWN;
}

using DeviceTextRoute = std::string (*)(PlayerIndex);

[[nodiscard]] CNA_Result ReportDeviceTextSize(
    const DeviceTextRoute route,
    const CNA_Handle gameHandle,
    const CNA_PlayerIndex playerIndex,
    uint64_t* const outBytes)
{
    if (outBytes == nullptr) {
        return InvalidArgument("The gamepad text size output is null.");
    }
    PlayerIndex nativePlayerIndex = PlayerIndex::One;
    if (const CNA_Result result = BeginGamePadCall(gameHandle, playerIndex, &nativePlayerIndex);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outBytes = route(nativePlayerIndex).size();
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CopyDeviceTextValue(
    const DeviceTextRoute route,
    const CNA_Handle gameHandle,
    const CNA_PlayerIndex playerIndex,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    PlayerIndex nativePlayerIndex = PlayerIndex::One;
    if (const CNA_Result result = BeginGamePadCall(gameHandle, playerIndex, &nativePlayerIndex);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return CopyText(route(nativePlayerIndex), destination, capacity, outBytes);
}

[[nodiscard]] CNA_Result GamepadGetGyroExt(
    const CNA_Handle gameHandle,
    const CNA_PlayerIndex playerIndex,
    CNA_Vector3* const outGyro,
    CNA_Bool* const outAvailable)
{
    PlayerIndex nativePlayerIndex = PlayerIndex::One;
    if (const CNA_Result result =
            BeginGamePadCall(gameHandle, playerIndex, &nativePlayerIndex);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Vector3 reading;
    if (GamePad::GetGyroEXT(nativePlayerIndex, reading)) {
        outGyro->x = reading.X;
        outGyro->y = reading.Y;
        outGyro->z = reading.Z;
        *outAvailable = CNA_TRUE;
    } else {
        *outAvailable = CNA_FALSE;
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GamepadGetAccelerometerExt(
    const CNA_Handle gameHandle,
    const CNA_PlayerIndex playerIndex,
    CNA_Vector3* const outAcceleration,
    CNA_Bool* const outAvailable)
{
    PlayerIndex nativePlayerIndex = PlayerIndex::One;
    if (const CNA_Result result =
            BeginGamePadCall(gameHandle, playerIndex, &nativePlayerIndex);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Vector3 reading;
    if (GamePad::GetAccelerometerEXT(nativePlayerIndex, reading)) {
        outAcceleration->x = reading.X;
        outAcceleration->y = reading.Y;
        outAcceleration->z = reading.Z;
        *outAvailable = CNA_TRUE;
    } else {
        *outAvailable = CNA_FALSE;
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GamepadGetPowerInfoExt(
    const CNA_Handle gameHandle,
    const CNA_PlayerIndex playerIndex,
    CNA_PowerState* const outState,
    int32_t* const outPercent)
{
    PlayerIndex nativePlayerIndex = PlayerIndex::One;
    if (const CNA_Result result =
            BeginGamePadCall(gameHandle, playerIndex, &nativePlayerIndex);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    int percent = 0;
    *outState = MapPowerState(GamePad::GetPowerInfoEXT(nativePlayerIndex, percent));
    *outPercent = static_cast<int32_t>(percent);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GamepadGetTouchpadFingerExt(
    const CNA_Handle gameHandle,
    const CNA_PlayerIndex playerIndex,
    const int32_t touchpad,
    const int32_t finger,
    CNA_GamePadTouchpadFinger* const outFinger,
    CNA_Bool* const outAvailable)
{
    PlayerIndex nativePlayerIndex = PlayerIndex::One;
    if (const CNA_Result result =
            BeginGamePadCall(gameHandle, playerIndex, &nativePlayerIndex);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    bool down = false;
    float x = 0.0F;
    float y = 0.0F;
    float pressure = 0.0F;
    if (GamePad::GetTouchpadFingerEXT(
            nativePlayerIndex,
            static_cast<int>(touchpad),
            static_cast<int>(finger),
            down,
            x,
            y,
            pressure)) {
        outFinger->is_down = ToCBool(down);
        outFinger->reserved[0] = UINT8_C(0);
        outFinger->reserved[1] = UINT8_C(0);
        outFinger->reserved[2] = UINT8_C(0);
        outFinger->x = x;
        outFinger->y = y;
        outFinger->pressure = pressure;
        *outAvailable = CNA_TRUE;
    } else {
        *outAvailable = CNA_FALSE;
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ValidateKey(const CNA_Key key)
{
    if (key >= KeyboardSlotCount) {
        return InvalidArgument("The key identity is outside the canonical 256-slot range.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] KeyboardState NativeKeyboardState(const CNA_KeyboardState& state)
{
    std::unordered_set<Keys> pressed;
    for (CNA_Key key = UINT32_C(0); key < KeyboardSlotCount; ++key) {
        if (IsKeyDown(state, key)) {
            pressed.insert(static_cast<Keys>(key));
        }
    }
    return KeyboardState(pressed);
}

[[nodiscard]] CNA_KeyboardState SnapshotKeyboard(const KeyboardState& native)
{
    CNA_KeyboardState snapshot = {
        sizeof(CNA_KeyboardState), StructureVersion, {0U, 0U, 0U, 0U}
    };
    for (const Keys key : native.GetPressedKeys()) {
        const auto value = static_cast<uint32_t>(key);
        if (value < KeyboardSlotCount) {
            snapshot.pressed_key_words[value >> 6U] |= UINT64_C(1) << (value & UINT32_C(63));
        }
    }
    return snapshot;
}

[[nodiscard]] CNA_KeyModifiers MapKeyModifiers(const CNA::Input::KeyModifiersEXT value) noexcept
{
    return static_cast<CNA_KeyModifiers>(static_cast<std::uint32_t>(value));
}

using KeyTextRoute = std::string (*)(Keys);

[[nodiscard]] CNA_Result ReportKeyTextSize(
    const KeyTextRoute route,
    const CNA_Handle gameHandle,
    const CNA_Key key,
    uint64_t* const outBytes)
{
    if (outBytes == nullptr) {
        return InvalidArgument("The key text size output is null.");
    }
    if (const CNA_Result result = ValidateKey(key); result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outBytes = route(static_cast<Keys>(key)).size();
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CopyKeyTextValue(
    const KeyTextRoute route,
    const CNA_Handle gameHandle,
    const CNA_Key key,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    if (const CNA_Result result = ValidateKey(key); result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return CopyText(route(static_cast<Keys>(key)), destination, capacity, outBytes);
}

using KeyLookupRoute = Keys (*)(const std::string&);

[[nodiscard]] CNA_Result LookUpKey(
    const KeyLookupRoute route,
    const CNA_Handle gameHandle,
    const CNA_StringView name,
    CNA_Key* const outKey)
{
    if (outKey == nullptr) {
        return InvalidArgument("The key identity output is null.");
    }
    std::string text;
    if (const CNA_Result result = CNA::C::Detail::ReadText(
            name, "The key name is invalid.", &text, CNA::C::Detail::EmbeddedNul::Refused);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outKey = static_cast<CNA_Key>(route(text));
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result KeyboardStateInitFromKeys(
    const CNA_Key* const keys,
    const uint64_t count,
    CNA_KeyboardState* const outState)
{
    if (keys == nullptr && count != 0U) {
        return InvalidArgument("The key array is invalid.");
    }
    // The canonical constructors silently drop a key outside the 256-slot bit field; C refuses
    // instead, so a caller can never lose a key without being told.
    std::unordered_set<Keys> pressed;
    for (uint64_t index = 0U; index < count; ++index) {
        if (const CNA_Result result = ValidateKey(keys[index]);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        pressed.insert(static_cast<Keys>(keys[index]));
    }
    *outState = SnapshotKeyboard(KeyboardState(pressed));
    return CNA_RESULT_SUCCESS;
}

using Microsoft::Xna::Framework::Input::ButtonState;

constexpr CNA_MouseButtonFlags MouseButtonMask = CNA_MOUSE_BUTTON_LEFT | CNA_MOUSE_BUTTON_MIDDLE |
    CNA_MOUSE_BUTTON_RIGHT | CNA_MOUSE_BUTTON_X1 | CNA_MOUSE_BUTTON_X2;

[[nodiscard]] ButtonState MouseButton(
    const CNA_MouseButtonFlags buttons,
    const CNA_MouseButtonFlags bit) noexcept
{
    return (buttons & bit) != 0U ? ButtonState::Pressed : ButtonState::Released;
}

[[nodiscard]] CNA_MouseButtonFlags CollectMouseButtons(const MouseState& state) noexcept
{
    CNA_MouseButtonFlags buttons = UINT32_C(0);
    if (state.getLeftButtonProperty() == ButtonState::Pressed) {
        buttons |= CNA_MOUSE_BUTTON_LEFT;
    }
    if (state.getMiddleButtonProperty() == ButtonState::Pressed) {
        buttons |= CNA_MOUSE_BUTTON_MIDDLE;
    }
    if (state.getRightButtonProperty() == ButtonState::Pressed) {
        buttons |= CNA_MOUSE_BUTTON_RIGHT;
    }
    if (state.getXButton1Property() == ButtonState::Pressed) {
        buttons |= CNA_MOUSE_BUTTON_X1;
    }
    if (state.getXButton2Property() == ButtonState::Pressed) {
        buttons |= CNA_MOUSE_BUTTON_X2;
    }
    return buttons;
}

[[nodiscard]] CNA_MouseState SnapshotMouse(const MouseState& native)
{
    const CNA_MouseState snapshot = {
        sizeof(CNA_MouseState),
        StructureVersion,
        native.getXProperty(),
        native.getYProperty(),
        native.getScrollWheelValueProperty(),
        native.getHorizontalScrollWheelValueEXTProperty(),
        CollectMouseButtons(native),
        0U
    };
    return snapshot;
}

// GamePadState.ToString reports the connection alone, and XNA's public constructors all make a
// connected state, so these two native states stand for every snapshot's text.
[[nodiscard]] std::string GamePadStateText(const CNA_GamePadState& state)
{
    return (state.is_connected == CNA_TRUE
                ? GamePadState(GamePadThumbSticks(), GamePadTriggers(), GamePadButtons(), GamePadDPad())
                : GamePadState())
        .ToString();
}

[[nodiscard]] MouseState NativeMouseState(const CNA_MouseState& state)
{
    return MouseState(
        static_cast<int>(state.x),
        static_cast<int>(state.y),
        static_cast<int>(state.scroll_wheel),
        MouseButton(state.pressed_buttons, CNA_MOUSE_BUTTON_LEFT),
        MouseButton(state.pressed_buttons, CNA_MOUSE_BUTTON_MIDDLE),
        MouseButton(state.pressed_buttons, CNA_MOUSE_BUTTON_RIGHT),
        MouseButton(state.pressed_buttons, CNA_MOUSE_BUTTON_X1),
        MouseButton(state.pressed_buttons, CNA_MOUSE_BUTTON_X2),
        static_cast<int>(state.horizontal_scroll_wheel));
}

[[nodiscard]] CNA_Result ValidateMouseButtons(const CNA_MouseButtonFlags buttons)
{
    if ((buttons & ~MouseButtonMask) != 0U) {
        return InvalidArgument("The mouse button mask contains an undefined bit.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ValidateMouseState(const CNA_MouseState* const state)
{
    return ValidateVersionedStructure(state, "The mouse snapshot is invalid.");
}

// The canonical clicked event is process-wide static state, so the registration owns only its
// subscription token and detaches by token. Releasing it after ResetForTests() cleared the event
// simply removes nothing.
class MouseClickedRegistration final {
public:
    explicit MouseClickedRegistration(const System::MulticastAction<int>::Token token)
        : token_(token)
    {
    }

    MouseClickedRegistration(const MouseClickedRegistration&) = delete;
    MouseClickedRegistration& operator=(const MouseClickedRegistration&) = delete;

    ~MouseClickedRegistration()
    {
        if (token_ != System::MulticastAction<int>::InvalidToken) {
            (void)Mouse::ClickedEXT.Remove(token_);
        }
    }

private:
    System::MulticastAction<int>::Token token_;
};

[[nodiscard]] CNA_Result MouseGetGlobalPositionExt(
    const CNA_Handle gameHandle,
    int32_t* const outX,
    int32_t* const outY)
{
    if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    int x = 0;
    int y = 0;
    Mouse::GetGlobalPositionEXT(x, y);
    *outX = static_cast<int32_t>(x);
    *outY = static_cast<int32_t>(y);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result MouseSubscribeClickedExt(
    const CNA_MouseClickedCallback callback,
    void* const context,
    CNA_MouseEventRegistrationHandle* const outRegistration)
{
    if (callback == nullptr) {
        return InvalidArgument("The mouse clicked callback is null.");
    }
    const auto token = Mouse::ClickedEXT.Add([callback, context](const int button) {
        callback(static_cast<int32_t>(button), context);
    });
    const auto resource = std::make_shared<MouseClickedRegistration>(token);
    return CreateHandle(CNA::C::Detail::ObjectKind::MouseEventRegistration,
        resource,
        outRegistration);
}

[[nodiscard]] CNA_Result MouseUnsubscribeClickedExt(
    const CNA_MouseEventRegistrationHandle registration)
{
    std::shared_ptr<MouseClickedRegistration> resource;
    const CNA_Result getResult = LookupHandle(
        registration, CNA::C::Detail::ObjectKind::MouseEventRegistration, &resource);
    if (getResult != CNA_RESULT_SUCCESS) {
        return getResult;
    }
    return ReleaseHandle(registration);
}

using Microsoft::Xna::Framework::Input::MouseCursor;

// A created cursor owns its native resource; a stock cursor is a process-lifetime singleton the C
// layer must never free, so the resource only points at it.
struct MouseCursorResource final {
    std::unique_ptr<MouseCursor> owned;
    MouseCursor* value = nullptr;
};

[[nodiscard]] CNA_Result CreateCursorHandle(
    std::shared_ptr<MouseCursorResource> resource,
    CNA_MouseCursorHandle* const outCursor)
{
    return CreateHandle(CNA::C::Detail::ObjectKind::MouseCursor, std::move(resource), outCursor);
}

[[nodiscard]] CNA_Result GetCursorResource(
    const CNA_MouseCursorHandle handle,
    std::shared_ptr<MouseCursorResource>* const outResource)
{
    return LookupHandle(handle, CNA::C::Detail::ObjectKind::MouseCursor, outResource);
}

[[nodiscard]] CNA_Result BorrowStockCursor(
    const CNA_MouseCursorStock stock,
    MouseCursor** const outCursor)
{
    switch (stock) {
        case CNA_MOUSE_CURSOR_STOCK_ARROW: *outCursor = &MouseCursor::getArrowProperty(); break;
        case CNA_MOUSE_CURSOR_STOCK_CROSSHAIR:
            *outCursor = &MouseCursor::getCrosshairProperty();
            break;
        case CNA_MOUSE_CURSOR_STOCK_HAND: *outCursor = &MouseCursor::getHandProperty(); break;
        case CNA_MOUSE_CURSOR_STOCK_IBEAM: *outCursor = &MouseCursor::getIBeamProperty(); break;
        case CNA_MOUSE_CURSOR_STOCK_NO: *outCursor = &MouseCursor::getNoProperty(); break;
        case CNA_MOUSE_CURSOR_STOCK_SIZE_ALL:
            *outCursor = &MouseCursor::getSizeAllProperty();
            break;
        case CNA_MOUSE_CURSOR_STOCK_SIZE_NESW:
            *outCursor = &MouseCursor::getSizeNESWProperty();
            break;
        case CNA_MOUSE_CURSOR_STOCK_SIZE_NS:
            *outCursor = &MouseCursor::getSizeNSProperty();
            break;
        case CNA_MOUSE_CURSOR_STOCK_SIZE_NWSE:
            *outCursor = &MouseCursor::getSizeNWSEProperty();
            break;
        case CNA_MOUSE_CURSOR_STOCK_SIZE_WE:
            *outCursor = &MouseCursor::getSizeWEProperty();
            break;
        case CNA_MOUSE_CURSOR_STOCK_WAIT: *outCursor = &MouseCursor::getWaitProperty(); break;
        case CNA_MOUSE_CURSOR_STOCK_WAIT_ARROW:
            *outCursor = &MouseCursor::getWaitArrowProperty();
            break;
        default:
            return InvalidArgument("The requested stock cursor is not a canonical identity.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result MouseCursorCreateExt(CNA_MouseCursorHandle* const outCursor)
{
    auto resource = std::make_shared<MouseCursorResource>();
    resource->owned = std::make_unique<MouseCursor>();
    resource->value = resource->owned.get();
    return CreateCursorHandle(std::move(resource), outCursor);
}

[[nodiscard]] CNA_Result MouseCursorGetStockExt(
    const CNA_Handle gameHandle,
    const CNA_MouseCursorStock stock,
    CNA_MouseCursorHandle* const outCursor)
{
    MouseCursor* stockCursor = nullptr;
    if (const CNA_Result result = BorrowStockCursor(stock, &stockCursor);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    auto resource = std::make_shared<MouseCursorResource>();
    resource->value = stockCursor;
    return CreateCursorHandle(std::move(resource), outCursor);
}

[[nodiscard]] CNA_Result MouseCursorCreateFromTexture2d(
    const CNA_Handle gameHandle,
    const CNA_Handle textureHandle,
    const int32_t originX,
    const int32_t originY,
    CNA_MouseCursorHandle* const outCursor)
{
    if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::shared_ptr<CNA::C::Detail::Texture2DResource> texture;
    if (const CNA_Result result =
            CNA::C::Detail::GetOwnedTexture2D(textureHandle, &texture);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    auto resource = std::make_shared<MouseCursorResource>();
    resource->owned = std::make_unique<MouseCursor>(MouseCursor::FromTexture2D(
        *texture->value,
        static_cast<int>(originX),
        static_cast<int>(originY)));
    resource->value = resource->owned.get();
    return CreateCursorHandle(std::move(resource), outCursor);
}

[[nodiscard]] CNA_Result MouseSetCursorExt(
    const CNA_Handle gameHandle,
    const CNA_MouseCursorHandle cursor)
{
    std::shared_ptr<MouseCursorResource> resource;
    if (const CNA_Result result = GetCursorResource(cursor, &resource);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    Mouse::SetCursor(*resource->value);
    return CNA_RESULT_SUCCESS;
}

using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Input::TextInputEXT;
using CNA::Input::TextInputTypeEXT;
using SharpRuntime::charcs;

// The C identities are the canonical ordinals, not a renumbering.
static_assert(static_cast<uint32_t>(TextInputTypeEXT::Text) == CNA_TEXT_INPUT_TYPE_TEXT);
static_assert(static_cast<uint32_t>(TextInputTypeEXT::TextName) == CNA_TEXT_INPUT_TYPE_TEXT_NAME);
static_assert(static_cast<uint32_t>(TextInputTypeEXT::TextEmail) == CNA_TEXT_INPUT_TYPE_TEXT_EMAIL);
static_assert(
    static_cast<uint32_t>(TextInputTypeEXT::TextUsername) == CNA_TEXT_INPUT_TYPE_TEXT_USERNAME);
static_assert(static_cast<uint32_t>(TextInputTypeEXT::TextPasswordHidden) ==
    CNA_TEXT_INPUT_TYPE_TEXT_PASSWORD_HIDDEN);
static_assert(static_cast<uint32_t>(TextInputTypeEXT::TextPasswordVisible) ==
    CNA_TEXT_INPUT_TYPE_TEXT_PASSWORD_VISIBLE);
static_assert(static_cast<uint32_t>(TextInputTypeEXT::Number) == CNA_TEXT_INPUT_TYPE_NUMBER);
static_assert(static_cast<uint32_t>(TextInputTypeEXT::NumberPasswordHidden) ==
    CNA_TEXT_INPUT_TYPE_NUMBER_PASSWORD_HIDDEN);
static_assert(static_cast<uint32_t>(TextInputTypeEXT::NumberPasswordVisible) ==
    CNA_TEXT_INPUT_TYPE_NUMBER_PASSWORD_VISIBLE);
static_assert(static_cast<uint32_t>(TextInputTypeEXT::NumberPasswordVisible) ==
    CNA_TEXT_INPUT_TYPE_MAXIMUM);

// The three canonical text-input events are process-wide static state with three different
// signatures, so one registration records which event it came from and detaches by token. Releasing
// it after ResetForTests() cleared the events simply removes nothing.
enum class TextInputEventKind : uint32_t {
    TextInput,
    TextEditing,
    TextEditingCandidates
};

class TextInputRegistration final {
public:
    TextInputRegistration(const TextInputEventKind kind, const uint64_t token)
        : kind_(kind), token_(token)
    {
    }

    TextInputRegistration(const TextInputRegistration&) = delete;
    TextInputRegistration& operator=(const TextInputRegistration&) = delete;

    ~TextInputRegistration()
    {
        switch (kind_) {
            case TextInputEventKind::TextInput:
                if (token_ != System::MulticastAction<charcs>::InvalidToken) {
                    (void)TextInputEXT::TextInput.Remove(token_);
                }
                return;
            case TextInputEventKind::TextEditing:
                if (token_ !=
                    System::MulticastAction<const std::string&, int, int>::InvalidToken) {
                    (void)TextInputEXT::TextEditing.Remove(token_);
                }
                return;
            case TextInputEventKind::TextEditingCandidates:
                if (token_ !=
                    System::MulticastAction<const std::vector<std::string>&, int, bool>
                        ::InvalidToken) {
                    (void)TextInputEXT::TextEditingCandidatesEXT.Remove(token_);
                }
                return;
        }
    }

private:
    TextInputEventKind kind_;
    uint64_t token_;
};

[[nodiscard]] CNA_Result CreateTextInputRegistration(
    const TextInputEventKind kind,
    const uint64_t token,
    CNA_TextInputRegistrationHandle* const outRegistration)
{
    const auto resource = std::make_shared<TextInputRegistration>(kind, token);
    return CreateHandle(CNA::C::Detail::ObjectKind::TextInputEventRegistration,
        resource,
        outRegistration);
}

[[nodiscard]] CNA_Result ToTextInputType(
    const CNA_TextInputType value,
    TextInputTypeEXT* const outType)
{
    // The canonical conversion silently falls back to plain text for an out-of-range value; C
    // refuses instead, so a consumer is told about a bad identity rather than getting a different
    // keyboard than it asked for.
    if (value > CNA_TEXT_INPUT_TYPE_MAXIMUM) {
        return InvalidArgument("The text input type identity is undefined.");
    }
    *outType = static_cast<TextInputTypeEXT>(value);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result TextInputSubscribeTextInputExt(
    const CNA_TextInputCallback callback,
    void* const context,
    CNA_TextInputRegistrationHandle* const outRegistration)
{
    if (callback == nullptr) {
        return InvalidArgument("The text-input callback is null.");
    }
    const auto token = TextInputEXT::TextInput.Add([callback, context](const charcs value) {
        callback(static_cast<uint16_t>(value), context);
    });
    return CreateTextInputRegistration(
        TextInputEventKind::TextInput,
        token,
        outRegistration);
}

[[nodiscard]] CNA_Result TextInputSubscribeTextEditingExt(
    const CNA_TextEditingCallback callback,
    void* const context,
    CNA_TextInputRegistrationHandle* const outRegistration)
{
    if (callback == nullptr) {
        return InvalidArgument("The text-editing callback is null.");
    }
    const auto token = TextInputEXT::TextEditing.Add(
        [callback, context](const std::string& text, const int start, const int length) {
            CNA_TextEditingEventInfo info = {
                sizeof(CNA_TextEditingEventInfo),
                StructureVersion,
                {text.data(), static_cast<uint64_t>(text.size())},
                static_cast<int32_t>(start),
                static_cast<int32_t>(length)
            };
            callback(&info, context);
        });
    return CreateTextInputRegistration(
        TextInputEventKind::TextEditing,
        token,
        outRegistration);
}

[[nodiscard]] CNA_Result TextInputSubscribeTextEditingCandidatesExt(
    const CNA_TextEditingCandidatesCallback callback,
    void* const context,
    CNA_TextInputRegistrationHandle* const outRegistration)
{
    if (callback == nullptr) {
        return InvalidArgument("The text-editing candidates callback is null.");
    }
    const auto token = TextInputEXT::TextEditingCandidatesEXT.Add(
        [callback, context](
            const std::vector<std::string>& candidates,
            const int selected,
            const bool horizontal) {
            std::vector<CNA_StringView> views;
            views.reserve(candidates.size());
            for (const std::string& candidate : candidates) {
                views.push_back(
                    CNA_StringView{candidate.data(), static_cast<uint64_t>(candidate.size())});
            }
            CNA_TextEditingCandidatesEventInfo info = {
                sizeof(CNA_TextEditingCandidatesEventInfo),
                StructureVersion,
                views.empty() ? nullptr : views.data(),
                static_cast<int32_t>(views.size()),
                static_cast<int32_t>(selected),
                ToCBool(horizontal),
                {0U, 0U, 0U}
            };
            callback(&info, context);
        });
    return CreateTextInputRegistration(
        TextInputEventKind::TextEditingCandidates,
        token,
        outRegistration);
}

[[nodiscard]] CNA_Result TextInputUnsubscribeExt(const CNA_TextInputRegistrationHandle registration)
{
    std::shared_ptr<TextInputRegistration> resource;
    const CNA_Result getResult = LookupHandle(
        registration, CNA::C::Detail::ObjectKind::TextInputEventRegistration, &resource);
    if (getResult != CNA_RESULT_SUCCESS) {
        return getResult;
    }
    return ReleaseHandle(registration);
}

[[nodiscard]] CNA_Result TextInputRaiseTextEditingCandidatesExt(
    const CNA_Handle gameHandle,
    const CNA_StringView* const candidates,
    const int32_t candidateCount,
    const int32_t selected,
    const CNA_Bool horizontal)
{
    if (candidateCount < 0) {
        return InvalidArgument("The candidate count is negative.");
    }
    if (candidates == nullptr && candidateCount != 0) {
        return InvalidArgument("The candidate array is null.");
    }
    std::vector<std::string> copied;
    copied.reserve(static_cast<std::size_t>(candidateCount));
    for (int32_t index = 0; index < candidateCount; ++index) {
        std::string candidate;
        if (const CNA_Result result = CNA::C::Detail::ReadText(
                candidates[index], "A text-editing candidate is not valid UTF-8.", &candidate);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        copied.push_back(std::move(candidate));
    }
    if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    TextInputEXT::INTERNAL_OnTextEditingCandidates(
        copied,
        static_cast<int>(selected),
        horizontal != CNA_FALSE);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result TextInputStartWithTypeExt(
    const CNA_Handle gameHandle,
    const CNA_TextInputType type)
{
    TextInputTypeEXT nativeType = TextInputTypeEXT::Text;
    if (const CNA_Result result = ToTextInputType(type, &nativeType);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    TextInputEXT::StartTextInputWithTypeEXT(nativeType);
    return CNA_RESULT_SUCCESS;
}

using Microsoft::Xna::Framework::DisplayOrientation;
using Microsoft::Xna::Framework::Input::Touch::GestureSample;
using Microsoft::Xna::Framework::Input::Touch::GestureType;

constexpr CNA_DisplayOrientation DisplayOrientationAll =
    CNA_DISPLAY_ORIENTATION_LANDSCAPE_LEFT | CNA_DISPLAY_ORIENTATION_LANDSCAPE_RIGHT |
    CNA_DISPLAY_ORIENTATION_PORTRAIT;

[[nodiscard]] CNA_Result ValidateGestureType(const CNA_GestureType gestures) noexcept
{
    if ((gestures & ~CNA_GESTURE_TYPE_ALL) != 0U) {
        return InvalidArgument("The gesture type contains an undefined bit.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ValidateTouchLocationValue(
    const CNA_TouchLocation* const location,
    const char* const message) noexcept
{
    if (location == nullptr) {
        return InvalidArgument(message);
    }
    if (!IsValidTouchLocationState(location->state) ||
        !IsValidTouchLocationState(location->previous_state)) {
        return InvalidArgument("The touch location carries an undefined state.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ValidatePressure(const float pressure) noexcept
{
    // The canonical constructors store any value; C refuses one outside the documented range so a
    // snapshot can never claim a pressure its own contract excludes. NaN fails both comparisons.
    if (!(pressure >= 0.0F && pressure <= 1.0F)) {
        return InvalidArgument("The touch pressure is outside the range zero through one.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] TouchLocation ToNativeTouchLocation(const CNA_TouchLocation& location)
{
    return TouchLocation(
        location.id,
        static_cast<TouchLocationState>(location.state),
        Vector2(location.position.x, location.position.y),
        static_cast<TouchLocationState>(location.previous_state),
        Vector2(location.previous_position.x, location.previous_position.y),
        location.pressure);
}

// The canonical comparison covers id, state, position, previous state and previous position and
// deliberately excludes the pressure extension, so C compares through the canonical type rather
// than memcmp-ing the POD, which would wrongly separate two locations differing only in pressure.
[[nodiscard]] bool TouchLocationsEqual(
    const CNA_TouchLocation& left,
    const CNA_TouchLocation& right)
{
    return ToNativeTouchLocation(left).Equals(ToNativeTouchLocation(right));
}

[[nodiscard]] CNA_GestureSample MapGestureSample(const GestureSample& sample)
{
    const Vector2& position = sample.getPositionProperty();
    const Vector2& position2 = sample.getPosition2Property();
    const Vector2& delta = sample.getDeltaProperty();
    const Vector2& delta2 = sample.getDelta2Property();
    CNA_GestureSample mapped = {};
    mapped.struct_size = sizeof(CNA_GestureSample);
    mapped.struct_version = StructureVersion;
    mapped.gesture_type = static_cast<CNA_GestureType>(sample.getGestureTypeProperty());
    mapped.finger_id_ext = sample.getFingerIdEXTProperty();
    mapped.finger_id2_ext = sample.getFingerId2EXTProperty();
    mapped.timestamp_ticks = static_cast<int64_t>(sample.getTimestampProperty().getTicksProperty());
    mapped.position = CNA_Vector2{position.X, position.Y};
    mapped.position2 = CNA_Vector2{position2.X, position2.Y};
    mapped.delta = CNA_Vector2{delta.X, delta.Y};
    mapped.delta2 = CNA_Vector2{delta2.X, delta2.Y};
    return mapped;
}

[[nodiscard]] GestureSample ToNativeGestureSample(const CNA_GestureSample& sample)
{
    return GestureSample(
        static_cast<GestureType>(sample.gesture_type),
        System::TimeSpan(static_cast<SharpRuntime::longcs>(sample.timestamp_ticks)),
        Vector2(sample.position.x, sample.position.y),
        Vector2(sample.position2.x, sample.position2.y),
        Vector2(sample.delta.x, sample.delta.y),
        Vector2(sample.delta2.x, sample.delta2.y),
        sample.finger_id_ext,
        sample.finger_id2_ext);
}

[[nodiscard]] CNA_GestureSample GestureSampleOf(
    const CNA_GestureType gestureType,
    const int64_t timestampTicks,
    const CNA_Vector2 position,
    const CNA_Vector2 position2,
    const CNA_Vector2 delta,
    const CNA_Vector2 delta2,
    const int32_t fingerId,
    const int32_t fingerId2) noexcept
{
    CNA_GestureSample sample = {};
    sample.struct_size = sizeof(CNA_GestureSample);
    sample.struct_version = StructureVersion;
    sample.gesture_type = gestureType;
    sample.finger_id_ext = fingerId;
    sample.finger_id2_ext = fingerId2;
    sample.timestamp_ticks = timestampTicks;
    sample.position = position;
    sample.position2 = position2;
    sample.delta = delta;
    sample.delta2 = delta2;
    return sample;
}

[[nodiscard]] CNA_Result ReportTouchLocationString(
    const CNA_TouchLocation* const location,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes,
    const bool copy)
{
    if (outBytes == nullptr) {
        return InvalidArgument("The touch-location text byte-count output is null.");
    }
    if (const CNA_Result result =
            ValidateTouchLocationValue(location, "The touch location is null.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const std::string text = ToNativeTouchLocation(*location).ToString();
    if (!copy) {
        *outBytes = text.size();
        return CNA_RESULT_SUCCESS;
    }
    return CopyText(text, destination, capacity, outBytes);
}

[[nodiscard]] CNA_Result TouchStateInit(CNA_TouchState* const outState)
{
    CNA_TouchState state = {};
    state.struct_size = sizeof(CNA_TouchState);
    state.struct_version = StructureVersion;
    state.is_connected = CNA_FALSE;
    state.touch_count = 0U;
    *outState = state;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result TouchStateInitFromLocations(
    const CNA_TouchLocation* const locations,
    const uint32_t count,
    CNA_TouchState* const outState)
{
    if (count > CNA_TOUCH_MAX_TOUCHES) {
        return InvalidArgument("The touch count exceeds the fixed snapshot capacity.");
    }
    if (locations == nullptr && count != 0U) {
        return InvalidArgument("The touch-location array is null.");
    }
    for (uint32_t index = 0U; index < count; ++index) {
        if (const CNA_Result result = ValidateTouchLocationValue(
                &locations[index],
                "The touch-location array is null.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
    }
    CNA_TouchState state = {};
    state.struct_size = sizeof(CNA_TouchState);
    state.struct_version = StructureVersion;
    state.is_connected = CNA_FALSE;
    state.touch_count = count;
    for (uint32_t index = 0U; index < count; ++index) {
        state.touches[index] = locations[index];
    }
    *outState = state;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result TouchStateContains(
    const CNA_TouchState* const state,
    const CNA_TouchLocation* const item,
    CNA_Bool* const outContains)
{
    if (const CNA_Result result = ValidateTouchState(state);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result =
            ValidateTouchLocationValue(item, "The touch location is null.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    CNA_Bool contains = CNA_FALSE;
    for (uint32_t index = 0U; index < state->touch_count; ++index) {
        if (TouchLocationsEqual(state->touches[index], *item)) {
            contains = CNA_TRUE;
            break;
        }
    }
    *outContains = contains;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result TouchStateIndexOf(
    const CNA_TouchState* const state,
    const CNA_TouchLocation* const item,
    int32_t* const outIndex)
{
    if (const CNA_Result result = ValidateTouchState(state);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result =
            ValidateTouchLocationValue(item, "The touch location is null.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    int32_t found = -1;
    for (uint32_t index = 0U; index < state->touch_count; ++index) {
        if (TouchLocationsEqual(state->touches[index], *item)) {
            found = static_cast<int32_t>(index);
            break;
        }
    }
    *outIndex = found;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result TouchStateCopyTo(
    const CNA_TouchState* const state,
    CNA_TouchLocation* const destination,
    const uint64_t count,
    const uint64_t capacity,
    const int32_t destinationIndex,
    uint64_t* const outCount)
{
    if (const CNA_Result result = ValidateTouchState(state);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (count > capacity) {
        return InvalidArgument("The destination element count exceeds its capacity.");
    }
    if (destination == nullptr && capacity != UINT64_C(0)) {
        return InvalidArgument("The destination array is null.");
    }
    // The canonical operation guards the index the same way and throws for a bad one.
    if (destinationIndex < 0 || static_cast<uint64_t>(destinationIndex) > count) {
        return InvalidArgument("The destination index is out of range.");
    }
    const uint64_t added = state->touch_count;
    const uint64_t required = count + added;
    *outCount = required;
    if (required > capacity) {
        return BufferTooSmall(
            "The destination array cannot hold the inserted touch locations.");
    }
    if (added == UINT64_C(0)) {
        return CNA_RESULT_SUCCESS;
    }
    // Insert rather than overwrite: the canonical destination is a growable vector whose copy
    // shifts existing elements up. Move the tail back first so the copy is not self-clobbering.
    const uint64_t start = static_cast<uint64_t>(destinationIndex);
    for (uint64_t offset = count; offset > start; --offset) {
        destination[offset - 1U + added] = destination[offset - 1U];
    }
    for (uint64_t index = 0U; index < added; ++index) {
        destination[start + index] = state->touches[index];
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result TouchStateAdd(
    CNA_TouchState* const state,
    const CNA_TouchLocation* const item)
{
    if (const CNA_Result result = ValidateTouchState(state);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result =
            ValidateTouchLocationValue(item, "The touch location is null.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (state->touch_count >= CNA_TOUCH_MAX_TOUCHES) {
        return BufferTooSmall("The touch snapshot is already at its fixed capacity.");
    }
    state->touches[state->touch_count] = *item;
    state->touch_count += 1U;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result TouchStateRemove(
    CNA_TouchState* const state,
    const CNA_TouchLocation* const item,
    CNA_Bool* const outRemoved)
{
    if (const CNA_Result result = ValidateTouchState(state);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result =
            ValidateTouchLocationValue(item, "The touch location is null.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outRemoved = CNA_FALSE;
    for (uint32_t index = 0U; index < state->touch_count; ++index) {
        if (!TouchLocationsEqual(state->touches[index], *item)) {
            continue;
        }
        for (uint32_t shift = index + 1U; shift < state->touch_count; ++shift) {
            state->touches[shift - 1U] = state->touches[shift];
        }
        state->touch_count -= 1U;
        state->touches[state->touch_count] = CNA_TouchLocation{};
        *outRemoved = CNA_TRUE;
        break;
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result TouchStateRemoveAt(CNA_TouchState* const state, const int32_t index)
{
    if (const CNA_Result result = ValidateTouchState(state);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (index < 0 || static_cast<uint32_t>(index) >= state->touch_count) {
        return InvalidArgument("The touch-location index is out of range.");
    }
    for (uint32_t shift = static_cast<uint32_t>(index) + 1U;
         shift < state->touch_count;
         ++shift) {
        state->touches[shift - 1U] = state->touches[shift];
    }
    state->touch_count -= 1U;
    state->touches[state->touch_count] = CNA_TouchLocation{};
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result TouchStateInsert(
    CNA_TouchState* const state,
    const int32_t index,
    const CNA_TouchLocation* const item)
{
    if (const CNA_Result result = ValidateTouchState(state);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result =
            ValidateTouchLocationValue(item, "The touch location is null.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    // The canonical insertion accepts an index equal to the count, which appends.
    if (index < 0 || static_cast<uint32_t>(index) > state->touch_count) {
        return InvalidArgument("The touch-location index is out of range.");
    }
    if (state->touch_count >= CNA_TOUCH_MAX_TOUCHES) {
        return BufferTooSmall("The touch snapshot is already at its fixed capacity.");
    }
    for (uint32_t shift = state->touch_count; shift > static_cast<uint32_t>(index); --shift) {
        state->touches[shift] = state->touches[shift - 1U];
    }
    state->touches[static_cast<uint32_t>(index)] = *item;
    state->touch_count += 1U;
    return CNA_RESULT_SUCCESS;
}

} // namespace

// The routes generated from modules/c-api/spec/Input.toml.
#include "generated/Input.inc"
