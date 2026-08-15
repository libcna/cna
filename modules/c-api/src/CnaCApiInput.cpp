// SPDX-License-Identifier: MS-PL

#include "CNA/C/input.h"
#include "CNA/C/input_gamepad.h"
#include "CNA/C/input_keyboard.h"
#include "CNA/C/input_mouse.h"
#include "CnaCApiRuntimeDetail.hpp"

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
#include "Microsoft/Xna/Framework/Input/MouseState.hpp"
#include "System/MulticastAction.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchLocation.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchLocationState.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchPanelCapabilities.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_set>
#include <string>

namespace {

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::Fail;
using CNA::C::Detail::ValidateActiveGameHandle;
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
using Microsoft::Xna::Framework::Input::Keys;
using Microsoft::Xna::Framework::Input::Mouse;
using Microsoft::Xna::Framework::Input::MouseState;
using Microsoft::Xna::Framework::Input::Touch::TouchCollection;
using Microsoft::Xna::Framework::Input::Touch::TouchLocation;
using Microsoft::Xna::Framework::Input::Touch::TouchLocationState;
using Microsoft::Xna::Framework::Input::Touch::TouchPanel;
using Microsoft::Xna::Framework::Input::Touch::TouchPanelCapabilities;

constexpr uint32_t StructureVersion = UINT32_C(1);
constexpr CNA_Key KeyboardSlotCount = UINT32_C(256);

[[nodiscard]] CNA_Result ValidateKeyboardState(
    const CNA_KeyboardState* const state) noexcept
{
    if (state == nullptr || state->struct_size < sizeof(CNA_KeyboardState) ||
        state->struct_version != StructureVersion) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The keyboard-state snapshot structure is invalid.");
    }
    return CNA_RESULT_SUCCESS;
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

template<typename T>
[[nodiscard]] CNA_Result ValidateVersionedStructure(
    const T* const structure,
    const char* const message) noexcept
{
    if (structure == nullptr || structure->struct_size < sizeof(T) ||
        structure->struct_version != StructureVersion) {
        return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, message);
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] bool TryMapPlayerIndex(
    const CNA_PlayerIndex playerIndex,
    PlayerIndex* const outPlayerIndex) noexcept
{
    if (outPlayerIndex == nullptr || playerIndex > CNA_PLAYER_INDEX_FOUR) {
        return false;
    }
    *outPlayerIndex = static_cast<PlayerIndex>(playerIndex);
    return true;
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
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The touch-state snapshot count exceeds its fixed capacity.");
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
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The gamepad player index or dead-zone mode is invalid.");
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
        nativeState.getIsConnectedProperty() ? CNA_TRUE : CNA_FALSE,
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

} // namespace

CNA_Result cna_keyboard_get_state(
    const CNA_Handle gameHandle,
    CNA_KeyboardState* const outState)
{
    return CallWithExceptionBarrier([&]() {
        if (outState == nullptr || outState->struct_size < sizeof(CNA_KeyboardState) ||
            outState->struct_version != StructureVersion) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The keyboard-state output structure is invalid.");
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
    });
}

CNA_Result cna_keyboard_state_is_key_down(
    const CNA_KeyboardState* const state,
    const CNA_Key key,
    CNA_Bool* const outIsDown)
{
    return CallWithExceptionBarrier([&]() {
        if (outIsDown == nullptr || key >= KeyboardSlotCount) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The keyboard key-down query arguments are invalid.");
        }
        if (const CNA_Result result = ValidateKeyboardState(state);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outIsDown = IsKeyDown(*state, key) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_keyboard_state_is_key_up(
    const CNA_KeyboardState* const state,
    const CNA_Key key,
    CNA_Bool* const outIsUp)
{
    return CallWithExceptionBarrier([&]() {
        if (outIsUp == nullptr || key >= KeyboardSlotCount) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The keyboard key-up query arguments are invalid.");
        }
        if (const CNA_Result result = ValidateKeyboardState(state);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outIsUp = IsKeyDown(*state, key) ? CNA_FALSE : CNA_TRUE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_keyboard_state_get_pressed_key_count(
    const CNA_KeyboardState* const state,
    uint32_t* const outCount)
{
    return CallWithExceptionBarrier([&]() {
        if (outCount == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The pressed-key count output is null.");
        }
        if (const CNA_Result result = ValidateKeyboardState(state);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = CountPressedKeys(*state);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_keyboard_state_copy_pressed_keys(
    const CNA_KeyboardState* const state,
    CNA_Key* const destination,
    const uint64_t capacity,
    uint32_t* const outCount)
{
    return CallWithExceptionBarrier([&]() {
        if (outCount == nullptr || (destination == nullptr && capacity != 0U)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The pressed-key output buffer is invalid.");
        }
        if (const CNA_Result result = ValidateKeyboardState(state);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const uint32_t requiredCount = CountPressedKeys(*state);
        *outCount = requiredCount;
        if (capacity < requiredCount) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The pressed-key output buffer is too small.");
        }

        uint32_t destinationIndex = 0U;
        for (CNA_Key key = 0U; key < KeyboardSlotCount; ++key) {
            if (IsKeyDown(*state, key)) {
                destination[destinationIndex] = key;
                ++destinationIndex;
            }
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_mouse_get_state(
    const CNA_Handle gameHandle,
    CNA_MouseState* const outState)
{
    return CallWithExceptionBarrier([&]() {
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
    });
}

CNA_Result cna_gamepad_get_state(
    const CNA_Handle gameHandle,
    const CNA_PlayerIndex playerIndex,
    CNA_GamePadState* const outState)
{
    return CallWithExceptionBarrier([&]() {
        return CaptureGamePadState(
            gameHandle,
            playerIndex,
            CNA_GAMEPAD_DEAD_ZONE_INDEPENDENT_AXES,
            outState);
    });
}

CNA_Result cna_gamepad_get_state_with_dead_zone(
    const CNA_Handle gameHandle,
    const CNA_PlayerIndex playerIndex,
    const CNA_GamePadDeadZone deadZoneMode,
    CNA_GamePadState* const outState)
{
    return CallWithExceptionBarrier([&]() {
        return CaptureGamePadState(gameHandle, playerIndex, deadZoneMode, outState);
    });
}

CNA_Result cna_gamepad_apply_dead_zone(
    const CNA_GamePadDeadZone deadZoneMode,
    const CNA_GamePadAnalogState* const raw,
    CNA_GamePadAnalogState* const outProcessed)
{
    return CallWithExceptionBarrier([&]() {
        GamePadDeadZone nativeDeadZone = GamePadDeadZone::None;
        if (raw == nullptr || outProcessed == nullptr ||
            !TryMapDeadZone(deadZoneMode, &nativeDeadZone) || !IsFinite(*raw)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The gamepad dead-zone input is invalid.");
        }
        static_cast<void>(nativeDeadZone);
        const CNA_GamePadAnalogState processed = ApplyDeadZone(*raw, deadZoneMode);
        *outProcessed = processed;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_state_is_button_down(
    const CNA_GamePadState* const state,
    const CNA_GamePadButtonFlags buttons,
    CNA_Bool* const outIsDown)
{
    return CallWithExceptionBarrier([&]() {
        if (outIsDown == nullptr || (buttons & ~CNA_GAMEPAD_BUTTON_ALL) != 0U) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The gamepad button-down query arguments are invalid.");
        }
        if (const CNA_Result result = ValidateGamePadState(state);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outIsDown = (state->pressed_buttons & buttons) == buttons ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_state_is_button_up(
    const CNA_GamePadState* const state,
    const CNA_GamePadButtonFlags buttons,
    CNA_Bool* const outIsUp)
{
    return CallWithExceptionBarrier([&]() {
        if (outIsUp == nullptr || (buttons & ~CNA_GAMEPAD_BUTTON_ALL) != 0U) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The gamepad button-up query arguments are invalid.");
        }
        if (const CNA_Result result = ValidateGamePadState(state);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outIsUp = (state->pressed_buttons & buttons) != buttons ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_touch_get_capabilities(
    const CNA_Handle gameHandle,
    CNA_TouchCapabilities* const outCapabilities)
{
    return CallWithExceptionBarrier([&]() {
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
            return Fail(
                CNA_RESULT_INTERNAL,
                CNA_ERROR_CATEGORY_INTERNAL,
                "CNA returned a negative maximum touch count.");
        }
        const CNA_TouchCapabilities capabilities = {
            sizeof(CNA_TouchCapabilities),
            StructureVersion,
            nativeCapabilities.getIsConnectedProperty() ? CNA_TRUE : CNA_FALSE,
            {0U, 0U, 0U},
            static_cast<uint32_t>(maximumTouchCount)
        };
        *outCapabilities = capabilities;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_touch_get_state(
    const CNA_Handle gameHandle,
    CNA_TouchState* const outState)
{
    return CallWithExceptionBarrier([&]() {
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
            return Fail(
                CNA_RESULT_INTERNAL,
                CNA_ERROR_CATEGORY_INTERNAL,
                "CNA returned more touches than the fixed C snapshot can hold.");
        }
        CNA_TouchState snapshot = {};
        snapshot.struct_size = sizeof(CNA_TouchState);
        snapshot.struct_version = StructureVersion;
        snapshot.is_connected = nativeState.getIsConnectedProperty() ? CNA_TRUE : CNA_FALSE;
        snapshot.touch_count = static_cast<uint32_t>(nativeCount);
        for (uint32_t index = 0U; index < snapshot.touch_count; ++index) {
            snapshot.touches[index] = MapTouchLocation(nativeState[index]);
        }
        *outState = snapshot;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_touch_state_find_by_id(
    const CNA_TouchState* const state,
    const int32_t id,
    CNA_TouchLocation* const outLocation,
    CNA_Bool* const outFound)
{
    return CallWithExceptionBarrier([&]() {
        if (outLocation == nullptr || outFound == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The touch find-by-id output is null.");
        }
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
    });
}

CNA_Result cna_touch_location_try_get_previous(
    const CNA_TouchLocation* const location,
    CNA_TouchLocation* const outPrevious,
    CNA_Bool* const outFound)
{
    return CallWithExceptionBarrier([&]() {
        if (location == nullptr || outPrevious == nullptr || outFound == nullptr ||
            !IsValidTouchLocationState(location->state) ||
            !IsValidTouchLocationState(location->previous_state)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The touch previous-location query arguments are invalid.");
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
    });
}

namespace {

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
    return value ? CNA_TRUE : CNA_FALSE;
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

} // namespace

CNA_Result cna_gamepad_capabilities_init(CNA_GamePadCapabilities* const outCapabilities)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCapabilities == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The gamepad-capabilities output structure is null.");
        }
        *outCapabilities = SnapshotCapabilities(GamePadCapabilities{});
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_get_capabilities(
    const CNA_Handle gameHandle,
    const CNA_PlayerIndex playerIndex,
    CNA_GamePadCapabilities* const outCapabilities)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateVersionedStructure(
                outCapabilities,
                "The gamepad-capabilities output structure is invalid.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        PlayerIndex nativePlayerIndex = PlayerIndex::One;
        if (!TryMapPlayerIndex(playerIndex, &nativePlayerIndex)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The gamepad player index is invalid.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCapabilities = SnapshotCapabilities(GamePad::GetCapabilities(nativePlayerIndex));
        return CNA_RESULT_SUCCESS;
    });
}

namespace {

constexpr CNA_GamePadButtonFlags DPadMask = CNA_GAMEPAD_BUTTON_DPAD_UP |
    CNA_GAMEPAD_BUTTON_DPAD_DOWN | CNA_GAMEPAD_BUTTON_DPAD_LEFT | CNA_GAMEPAD_BUTTON_DPAD_RIGHT;

[[nodiscard]] CNA_Result InvalidInput(const char* const message)
{
    return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, message);
}

[[nodiscard]] CNA_Result ValidateMask(const CNA_GamePadButtonFlags mask)
{
    if ((mask & ~CNA_GAMEPAD_BUTTON_ALL) != 0U) {
        return InvalidInput("The gamepad button mask contains an undefined bit.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CombineButtonArray(
    const CNA_GamePadButtonFlags* const buttons,
    const uint64_t count,
    CNA_GamePadButtonFlags* const outMask)
{
    if (buttons == nullptr && count != 0U) {
        return InvalidInput("The gamepad button array is invalid.");
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

[[nodiscard]] CNA_GamePadState SnapshotState(const GamePadState& native)
{
    const Vector2& leftStick = native.getThumbSticksProperty().getLeftProperty();
    const Vector2& rightStick = native.getThumbSticksProperty().getRightProperty();
    CNA_GamePadState snapshot = {
        sizeof(CNA_GamePadState),
        StructureVersion,
        native.getIsConnectedProperty() ? CNA_TRUE : CNA_FALSE,
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

[[nodiscard]] CNA_Result ValidateState(const CNA_GamePadState* const state)
{
    return ValidateVersionedStructure(state, "The gamepad snapshot is invalid.");
}

[[nodiscard]] CNA_Result CopyStateText(
    const std::string& value,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    if (outBytes == nullptr || (destination == nullptr && capacity != 0U)) {
        return InvalidInput("The gamepad snapshot text output is invalid.");
    }
    *outBytes = value.size();
    if (capacity < value.size()) {
        return Fail(
            CNA_RESULT_BUFFER_TOO_SMALL,
            CNA_ERROR_CATEGORY_RANGE,
            "The destination capacity is smaller than the gamepad snapshot text.");
    }
    if (!value.empty()) {
        std::memcpy(destination, value.data(), value.size());
    }
    return CNA_RESULT_SUCCESS;
}

} // namespace

CNA_Result cna_gamepad_buttons_init(CNA_GamePadButtonFlags* const outButtons)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outButtons == nullptr) {
            return InvalidInput("The gamepad button-set output is null.");
        }
        *outButtons = MaskFromNamedButtons(GamePadButtons());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_buttons_init_from_mask(
    const CNA_GamePadButtonFlags buttons,
    CNA_GamePadButtonFlags* const outButtons)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outButtons == nullptr) {
            return InvalidInput("The gamepad button-set output is null.");
        }
        if (const CNA_Result result = ValidateMask(buttons); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outButtons = buttons;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_buttons_init_from_button_array(
    const CNA_GamePadButtonFlags* const buttons,
    const uint64_t count,
    CNA_GamePadButtonFlags* const outButtons)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outButtons == nullptr) {
            return InvalidInput("The gamepad button-set output is null.");
        }
        CNA_GamePadButtonFlags mask = CNA_GAMEPAD_BUTTON_NONE;
        if (const CNA_Result result = CombineButtonArray(buttons, count, &mask);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outButtons = mask;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_buttons_is_pressed(
    const CNA_GamePadButtonFlags buttons,
    const CNA_GamePadButtonFlags button,
    CNA_Bool* const outIsPressed)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIsPressed == nullptr) {
            return InvalidInput("The gamepad button-state output is null.");
        }
        if (const CNA_Result result = ValidateMask(buttons); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ValidateMask(button); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        bool handled = false;
        const bool pressed = NamedButtonIsPressed(NativeButtons(buttons), button, &handled);
        *outIsPressed = (handled ? pressed : (buttons & button) == button) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_buttons_equals(
    const CNA_GamePadButtonFlags left,
    const CNA_GamePadButtonFlags right,
    CNA_Bool* const outEquals)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEquals == nullptr) {
            return InvalidInput("The gamepad button-set comparison output is null.");
        }
        *outEquals = NativeButtons(left).Equals(NativeButtons(right)) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_buttons_not_equals(
    const CNA_GamePadButtonFlags left,
    const CNA_GamePadButtonFlags right,
    CNA_Bool* const outNotEquals)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outNotEquals == nullptr) {
            return InvalidInput("The gamepad button-set comparison output is null.");
        }
        *outNotEquals = NativeButtons(left) != NativeButtons(right) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_buttons_get_hash_code(
    const CNA_GamePadButtonFlags buttons,
    int32_t* const outHash)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHash == nullptr) {
            return InvalidInput("The gamepad button-set hash output is null.");
        }
        *outHash = static_cast<int32_t>(NativeButtons(buttons).GetHashCode());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_dpad_init(CNA_GamePadButtonFlags* const outDPad)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outDPad == nullptr) {
            return InvalidInput("The directional-pad output is null.");
        }
        *outDPad = CNA_GAMEPAD_BUTTON_NONE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_dpad_init_from_states(
    const CNA_Bool up,
    const CNA_Bool down,
    const CNA_Bool left,
    const CNA_Bool right,
    CNA_GamePadButtonFlags* const outDPad)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outDPad == nullptr) {
            return InvalidInput("The directional-pad output is null.");
        }
        CNA_GamePadButtonFlags mask = CNA_GAMEPAD_BUTTON_NONE;
        if (up != CNA_FALSE) { mask |= CNA_GAMEPAD_BUTTON_DPAD_UP; }
        if (down != CNA_FALSE) { mask |= CNA_GAMEPAD_BUTTON_DPAD_DOWN; }
        if (left != CNA_FALSE) { mask |= CNA_GAMEPAD_BUTTON_DPAD_LEFT; }
        if (right != CNA_FALSE) { mask |= CNA_GAMEPAD_BUTTON_DPAD_RIGHT; }
        *outDPad = mask;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_dpad_init_from_button_array(
    const CNA_GamePadButtonFlags* const buttons,
    const uint64_t count,
    CNA_GamePadButtonFlags* const outDPad)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outDPad == nullptr) {
            return InvalidInput("The directional-pad output is null.");
        }
        CNA_GamePadButtonFlags mask = CNA_GAMEPAD_BUTTON_NONE;
        if (const CNA_Result result = CombineButtonArray(buttons, count, &mask);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outDPad = mask & DPadMask;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_dpad_is_pressed(
    const CNA_GamePadButtonFlags dpad,
    const CNA_GamePadButtonFlags button,
    CNA_Bool* const outIsPressed)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIsPressed == nullptr) {
            return InvalidInput("The directional-pad state output is null.");
        }
        if (button != CNA_GAMEPAD_BUTTON_DPAD_UP && button != CNA_GAMEPAD_BUTTON_DPAD_DOWN &&
            button != CNA_GAMEPAD_BUTTON_DPAD_LEFT && button != CNA_GAMEPAD_BUTTON_DPAD_RIGHT) {
            return InvalidInput("The requested button is not a directional-pad direction.");
        }
        const GamePadDPad native = NativeDPad(dpad);
        ButtonState state = ButtonState::Released;
        switch (button) {
            case CNA_GAMEPAD_BUTTON_DPAD_UP: state = native.getUpProperty(); break;
            case CNA_GAMEPAD_BUTTON_DPAD_DOWN: state = native.getDownProperty(); break;
            case CNA_GAMEPAD_BUTTON_DPAD_LEFT: state = native.getLeftProperty(); break;
            default: state = native.getRightProperty(); break;
        }
        *outIsPressed = state == ButtonState::Pressed ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_dpad_equals(
    const CNA_GamePadButtonFlags left,
    const CNA_GamePadButtonFlags right,
    CNA_Bool* const outEquals)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEquals == nullptr) {
            return InvalidInput("The directional-pad comparison output is null.");
        }
        *outEquals = NativeDPad(left).Equals(NativeDPad(right)) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_dpad_not_equals(
    const CNA_GamePadButtonFlags left,
    const CNA_GamePadButtonFlags right,
    CNA_Bool* const outNotEquals)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outNotEquals == nullptr) {
            return InvalidInput("The directional-pad comparison output is null.");
        }
        *outNotEquals = NativeDPad(left) != NativeDPad(right) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_dpad_get_hash_code(
    const CNA_GamePadButtonFlags dpad,
    int32_t* const outHash)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHash == nullptr) {
            return InvalidInput("The directional-pad hash output is null.");
        }
        *outHash = static_cast<int32_t>(NativeDPad(dpad).GetHashCode());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_thumb_sticks_init(CNA_GamePadThumbSticks* const outThumbSticks)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outThumbSticks == nullptr) {
            return InvalidInput("The thumbstick output is null.");
        }
        const GamePadThumbSticks native;
        outThumbSticks->left = {
            native.getLeftProperty().X,
            native.getLeftProperty().Y
        };
        outThumbSticks->right = {
            native.getRightProperty().X,
            native.getRightProperty().Y
        };
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_thumb_sticks_init_from_positions(
    const CNA_Vector2* const left,
    const CNA_Vector2* const right,
    CNA_GamePadThumbSticks* const outThumbSticks)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (left == nullptr || right == nullptr || outThumbSticks == nullptr) {
            return InvalidInput("The thumbstick input or output is null.");
        }
        const GamePadThumbSticks native(
            Vector2(left->x, left->y),
            Vector2(right->x, right->y));
        outThumbSticks->left = {
            native.getLeftProperty().X,
            native.getLeftProperty().Y
        };
        outThumbSticks->right = {
            native.getRightProperty().X,
            native.getRightProperty().Y
        };
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_thumb_sticks_equals(
    const CNA_GamePadThumbSticks* const left,
    const CNA_GamePadThumbSticks* const right,
    CNA_Bool* const outEquals)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (left == nullptr || right == nullptr || outEquals == nullptr) {
            return InvalidInput("The thumbstick comparison input or output is null.");
        }
        *outEquals = NativeThumbSticks(*left).Equals(NativeThumbSticks(*right))
            ? CNA_TRUE
            : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_thumb_sticks_not_equals(
    const CNA_GamePadThumbSticks* const left,
    const CNA_GamePadThumbSticks* const right,
    CNA_Bool* const outNotEquals)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (left == nullptr || right == nullptr || outNotEquals == nullptr) {
            return InvalidInput("The thumbstick comparison input or output is null.");
        }
        *outNotEquals = NativeThumbSticks(*left) != NativeThumbSticks(*right)
            ? CNA_TRUE
            : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_thumb_sticks_get_hash_code(
    const CNA_GamePadThumbSticks* const thumbSticks,
    int32_t* const outHash)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (thumbSticks == nullptr || outHash == nullptr) {
            return InvalidInput("The thumbstick hash input or output is null.");
        }
        *outHash = static_cast<int32_t>(NativeThumbSticks(*thumbSticks).GetHashCode());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_triggers_init(CNA_GamePadTriggers* const outTriggers)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTriggers == nullptr) {
            return InvalidInput("The trigger output is null.");
        }
        const GamePadTriggers native;
        outTriggers->left = native.getLeftProperty();
        outTriggers->right = native.getRightProperty();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_triggers_init_from_positions(
    const float left,
    const float right,
    CNA_GamePadTriggers* const outTriggers)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTriggers == nullptr) {
            return InvalidInput("The trigger output is null.");
        }
        const GamePadTriggers native(left, right);
        outTriggers->left = native.getLeftProperty();
        outTriggers->right = native.getRightProperty();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_triggers_equals(
    const CNA_GamePadTriggers* const left,
    const CNA_GamePadTriggers* const right,
    CNA_Bool* const outEquals)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (left == nullptr || right == nullptr || outEquals == nullptr) {
            return InvalidInput("The trigger comparison input or output is null.");
        }
        *outEquals = NativeTriggers(*left).Equals(NativeTriggers(*right)) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_triggers_not_equals(
    const CNA_GamePadTriggers* const left,
    const CNA_GamePadTriggers* const right,
    CNA_Bool* const outNotEquals)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (left == nullptr || right == nullptr || outNotEquals == nullptr) {
            return InvalidInput("The trigger comparison input or output is null.");
        }
        *outNotEquals = NativeTriggers(*left) != NativeTriggers(*right) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_triggers_get_hash_code(
    const CNA_GamePadTriggers* const triggers,
    int32_t* const outHash)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (triggers == nullptr || outHash == nullptr) {
            return InvalidInput("The trigger hash input or output is null.");
        }
        *outHash = static_cast<int32_t>(NativeTriggers(*triggers).GetHashCode());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_state_init(CNA_GamePadState* const outState)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outState == nullptr) {
            return InvalidInput("The gamepad snapshot output is null.");
        }
        *outState = SnapshotState(GamePadState());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_state_init_from_components(
    const CNA_GamePadThumbSticks* const thumbSticks,
    const CNA_GamePadTriggers* const triggers,
    const CNA_GamePadButtonFlags buttons,
    const CNA_GamePadButtonFlags dpad,
    CNA_GamePadState* const outState)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (thumbSticks == nullptr || triggers == nullptr || outState == nullptr) {
            return InvalidInput("The gamepad snapshot input or output is null.");
        }
        if (const CNA_Result result = ValidateMask(buttons); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ValidateMask(dpad); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // The C snapshot carries a single button mask, and so does every state CNA itself builds:
        // the capture path derives the button set and the directional pad from one raw mask. The
        // pad argument is therefore merged into the button set rather than kept beside it, which
        // is what makes it readable again through cna_gamepad_state_get_dpad.
        *outState = SnapshotState(GamePadState(
            NativeThumbSticks(*thumbSticks),
            NativeTriggers(*triggers),
            NativeButtons(buttons | dpad),
            NativeDPad(dpad)));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_state_init_from_values(
    const CNA_Vector2* const leftThumbStick,
    const CNA_Vector2* const rightThumbStick,
    const float leftTrigger,
    const float rightTrigger,
    const CNA_GamePadButtonFlags* const buttons,
    const uint64_t count,
    CNA_GamePadState* const outState)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (leftThumbStick == nullptr || rightThumbStick == nullptr || outState == nullptr) {
            return InvalidInput("The gamepad snapshot input or output is null.");
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
    });
}

CNA_Result cna_gamepad_state_get_buttons(
    const CNA_GamePadState* const state,
    CNA_GamePadButtonFlags* const outButtons)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outButtons == nullptr) {
            return InvalidInput("The gamepad button-set output is null.");
        }
        if (const CNA_Result result = ValidateState(state); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outButtons = state->pressed_buttons;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_state_get_dpad(
    const CNA_GamePadState* const state,
    CNA_GamePadButtonFlags* const outDPad)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outDPad == nullptr) {
            return InvalidInput("The directional-pad output is null.");
        }
        if (const CNA_Result result = ValidateState(state); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outDPad = state->pressed_buttons & DPadMask;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_state_get_thumb_sticks(
    const CNA_GamePadState* const state,
    CNA_GamePadThumbSticks* const outThumbSticks)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outThumbSticks == nullptr) {
            return InvalidInput("The thumbstick output is null.");
        }
        if (const CNA_Result result = ValidateState(state); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        outThumbSticks->left = state->analog.left_thumb_stick;
        outThumbSticks->right = state->analog.right_thumb_stick;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_state_get_triggers(
    const CNA_GamePadState* const state,
    CNA_GamePadTriggers* const outTriggers)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTriggers == nullptr) {
            return InvalidInput("The trigger output is null.");
        }
        if (const CNA_Result result = ValidateState(state); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        outTriggers->left = state->analog.left_trigger;
        outTriggers->right = state->analog.right_trigger;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_state_set_packet_number_ext(
    CNA_GamePadState* const state,
    const int32_t packetNumber)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateState(state); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        state->packet_number = packetNumber;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_state_equals(
    const CNA_GamePadState* const left,
    const CNA_GamePadState* const right,
    CNA_Bool* const outEquals)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEquals == nullptr) {
            return InvalidInput("The gamepad snapshot comparison output is null.");
        }
        if (const CNA_Result result = ValidateState(left); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ValidateState(right); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outEquals = left->is_connected == right->is_connected &&
                left->packet_number == right->packet_number &&
                left->pressed_buttons == right->pressed_buttons &&
                NativeThumbSticks({left->analog.left_thumb_stick, left->analog.right_thumb_stick})
                    .Equals(NativeThumbSticks(
                        {right->analog.left_thumb_stick, right->analog.right_thumb_stick})) &&
                NativeTriggers({left->analog.left_trigger, left->analog.right_trigger})
                    .Equals(NativeTriggers(
                        {right->analog.left_trigger, right->analog.right_trigger}))
            ? CNA_TRUE
            : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_state_not_equals(
    const CNA_GamePadState* const left,
    const CNA_GamePadState* const right,
    CNA_Bool* const outNotEquals)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        CNA_Bool equals = CNA_FALSE;
        if (const CNA_Result result = cna_gamepad_state_equals(left, right, &equals);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outNotEquals == nullptr) {
            return InvalidInput("The gamepad snapshot comparison output is null.");
        }
        *outNotEquals = equals != CNA_FALSE ? CNA_FALSE : CNA_TRUE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_state_get_hash_code(
    const CNA_GamePadState* const state,
    int32_t* const outHash)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHash == nullptr) {
            return InvalidInput("The gamepad snapshot hash output is null.");
        }
        if (const CNA_Result result = ValidateState(state); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // The canonical hash mixes only the button set and the packet number.
        *outHash = static_cast<int32_t>(
            static_cast<unsigned>(NativeButtons(state->pressed_buttons).GetHashCode()) ^
            (static_cast<unsigned>(state->packet_number) * 31U));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_state_get_string_size(
    const CNA_GamePadState* const state,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The gamepad snapshot text output is null.");
        }
        if (const CNA_Result result = ValidateState(state); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = GamePadState().ToString().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_state_copy_string(
    const CNA_GamePadState* const state,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateState(state); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyStateText(GamePadState().ToString(), destination, capacity, outBytes);
    });
}

namespace {

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector3;

[[nodiscard]] CNA_Result BeginGamePadCall(
    const CNA_Handle gameHandle,
    const CNA_PlayerIndex playerIndex,
    PlayerIndex* const outPlayerIndex)
{
    if (!TryMapPlayerIndex(playerIndex, outPlayerIndex)) {
        return InvalidInput("The gamepad player index is invalid.");
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

[[nodiscard]] CNA_Result CopyDeviceText(
    const std::string& value,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    if (outBytes == nullptr || (destination == nullptr && capacity != 0U)) {
        return InvalidInput("The gamepad text output is invalid.");
    }
    *outBytes = value.size();
    if (capacity < value.size()) {
        return Fail(
            CNA_RESULT_BUFFER_TOO_SMALL,
            CNA_ERROR_CATEGORY_RANGE,
            "The destination capacity is smaller than the gamepad text.");
    }
    if (!value.empty()) {
        std::memcpy(destination, value.data(), value.size());
    }
    return CNA_RESULT_SUCCESS;
}

using DeviceTextRoute = std::string (*)(PlayerIndex);

[[nodiscard]] CNA_Result ReportDeviceTextSize(
    const DeviceTextRoute route,
    const CNA_Handle gameHandle,
    const CNA_PlayerIndex playerIndex,
    uint64_t* const outBytes)
{
    if (outBytes == nullptr) {
        return InvalidInput("The gamepad text size output is null.");
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
    return CopyDeviceText(route(nativePlayerIndex), destination, capacity, outBytes);
}

} // namespace

CNA_Result cna_gamepad_exclude_axis_dead_zone(
    const float value,
    const float deadZone,
    float* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidInput("The dead-zone output is null.");
        }
        if (!std::isfinite(value) || !std::isfinite(deadZone)) {
            return InvalidInput("The dead-zone input is not finite.");
        }
        *outValue = GamePad::ExcludeAxisDeadZone(value, deadZone);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_set_vibration(
    const CNA_Handle gameHandle,
    const CNA_PlayerIndex playerIndex,
    const float leftMotor,
    const float rightMotor,
    CNA_Bool* const outApplied)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outApplied == nullptr) {
            return InvalidInput("The vibration output is null.");
        }
        PlayerIndex nativePlayerIndex = PlayerIndex::One;
        if (const CNA_Result result =
                BeginGamePadCall(gameHandle, playerIndex, &nativePlayerIndex);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outApplied = GamePad::SetVibration(nativePlayerIndex, leftMotor, rightMotor)
            ? CNA_TRUE
            : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_set_light_bar_ext(
    const CNA_Handle gameHandle,
    const CNA_PlayerIndex playerIndex,
    const CNA_Color color)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        PlayerIndex nativePlayerIndex = PlayerIndex::One;
        if (const CNA_Result result =
                BeginGamePadCall(gameHandle, playerIndex, &nativePlayerIndex);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        GamePad::SetLightBarEXT(nativePlayerIndex, Color(color.r, color.g, color.b, color.a));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_set_trigger_vibration_ext(
    const CNA_Handle gameHandle,
    const CNA_PlayerIndex playerIndex,
    const float leftTrigger,
    const float rightTrigger,
    CNA_Bool* const outApplied)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outApplied == nullptr) {
            return InvalidInput("The trigger-vibration output is null.");
        }
        PlayerIndex nativePlayerIndex = PlayerIndex::One;
        if (const CNA_Result result =
                BeginGamePadCall(gameHandle, playerIndex, &nativePlayerIndex);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outApplied =
            GamePad::SetTriggerVibrationEXT(nativePlayerIndex, leftTrigger, rightTrigger)
            ? CNA_TRUE
            : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_get_gyro_ext(
    const CNA_Handle gameHandle,
    const CNA_PlayerIndex playerIndex,
    CNA_Vector3* const outGyro,
    CNA_Bool* const outAvailable)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outGyro == nullptr || outAvailable == nullptr) {
            return InvalidInput("The gyroscope output is null.");
        }
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
    });
}

CNA_Result cna_gamepad_get_accelerometer_ext(
    const CNA_Handle gameHandle,
    const CNA_PlayerIndex playerIndex,
    CNA_Vector3* const outAcceleration,
    CNA_Bool* const outAvailable)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outAcceleration == nullptr || outAvailable == nullptr) {
            return InvalidInput("The accelerometer output is null.");
        }
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
    });
}

CNA_Result cna_gamepad_get_player_index_ext(
    const CNA_Handle gameHandle,
    const CNA_PlayerIndex playerIndex,
    int32_t* const outIndex)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIndex == nullptr) {
            return InvalidInput("The device player-index output is null.");
        }
        PlayerIndex nativePlayerIndex = PlayerIndex::One;
        if (const CNA_Result result =
                BeginGamePadCall(gameHandle, playerIndex, &nativePlayerIndex);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outIndex = static_cast<int32_t>(GamePad::GetPlayerIndexEXT(nativePlayerIndex));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_set_player_index_ext(
    const CNA_Handle gameHandle,
    const CNA_PlayerIndex playerIndex,
    const int32_t index,
    CNA_Bool* const outApplied)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outApplied == nullptr) {
            return InvalidInput("The device player-index output is null.");
        }
        PlayerIndex nativePlayerIndex = PlayerIndex::One;
        if (const CNA_Result result =
                BeginGamePadCall(gameHandle, playerIndex, &nativePlayerIndex);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outApplied = GamePad::SetPlayerIndexEXT(nativePlayerIndex, static_cast<int>(index))
            ? CNA_TRUE
            : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_get_power_info_ext(
    const CNA_Handle gameHandle,
    const CNA_PlayerIndex playerIndex,
    CNA_PowerState* const outState,
    int32_t* const outPercent)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outState == nullptr || outPercent == nullptr) {
            return InvalidInput("The power-info output is null.");
        }
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
    });
}

CNA_Result cna_gamepad_get_button_label_ext(
    const CNA_Handle gameHandle,
    const CNA_PlayerIndex playerIndex,
    const CNA_GamePadButtonFlags button,
    CNA_GamePadButtonLabel* const outLabel)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outLabel == nullptr) {
            return InvalidInput("The button-label output is null.");
        }
        if (const CNA_Result result = ValidateMask(button); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        PlayerIndex nativePlayerIndex = PlayerIndex::One;
        if (const CNA_Result result =
                BeginGamePadCall(gameHandle, playerIndex, &nativePlayerIndex);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outLabel = MapButtonLabel(
            GamePad::GetButtonLabelEXT(nativePlayerIndex, static_cast<Buttons>(button)));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_get_guid_size_ext(
    const CNA_Handle gameHandle,
    const CNA_PlayerIndex playerIndex,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReportDeviceTextSize(&GamePad::GetGUIDEXT, gameHandle, playerIndex, outBytes);
    });
}

CNA_Result cna_gamepad_copy_guid_ext(
    const CNA_Handle gameHandle,
    const CNA_PlayerIndex playerIndex,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CopyDeviceTextValue(
            &GamePad::GetGUIDEXT,
            gameHandle,
            playerIndex,
            destination,
            capacity,
            outBytes);
    });
}

CNA_Result cna_gamepad_get_name_size_ext(
    const CNA_Handle gameHandle,
    const CNA_PlayerIndex playerIndex,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReportDeviceTextSize(&GamePad::GetNameEXT, gameHandle, playerIndex, outBytes);
    });
}

CNA_Result cna_gamepad_copy_name_ext(
    const CNA_Handle gameHandle,
    const CNA_PlayerIndex playerIndex,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CopyDeviceTextValue(
            &GamePad::GetNameEXT,
            gameHandle,
            playerIndex,
            destination,
            capacity,
            outBytes);
    });
}

CNA_Result cna_gamepad_get_path_size_ext(
    const CNA_Handle gameHandle,
    const CNA_PlayerIndex playerIndex,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReportDeviceTextSize(&GamePad::GetPathEXT, gameHandle, playerIndex, outBytes);
    });
}

CNA_Result cna_gamepad_copy_path_ext(
    const CNA_Handle gameHandle,
    const CNA_PlayerIndex playerIndex,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CopyDeviceTextValue(
            &GamePad::GetPathEXT,
            gameHandle,
            playerIndex,
            destination,
            capacity,
            outBytes);
    });
}

CNA_Result cna_gamepad_get_serial_size_ext(
    const CNA_Handle gameHandle,
    const CNA_PlayerIndex playerIndex,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReportDeviceTextSize(&GamePad::GetSerialEXT, gameHandle, playerIndex, outBytes);
    });
}

CNA_Result cna_gamepad_copy_serial_ext(
    const CNA_Handle gameHandle,
    const CNA_PlayerIndex playerIndex,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CopyDeviceTextValue(
            &GamePad::GetSerialEXT,
            gameHandle,
            playerIndex,
            destination,
            capacity,
            outBytes);
    });
}

CNA_Result cna_gamepad_get_firmware_version_ext(
    const CNA_Handle gameHandle,
    const CNA_PlayerIndex playerIndex,
    uint16_t* const outVersion)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outVersion == nullptr) {
            return InvalidInput("The firmware-version output is null.");
        }
        PlayerIndex nativePlayerIndex = PlayerIndex::One;
        if (const CNA_Result result =
                BeginGamePadCall(gameHandle, playerIndex, &nativePlayerIndex);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outVersion = GamePad::GetFirmwareVersionEXT(nativePlayerIndex);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_get_steam_handle_ext(
    const CNA_Handle gameHandle,
    const CNA_PlayerIndex playerIndex,
    uint64_t* const outHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHandle == nullptr) {
            return InvalidInput("The Steam-handle output is null.");
        }
        PlayerIndex nativePlayerIndex = PlayerIndex::One;
        if (const CNA_Result result =
                BeginGamePadCall(gameHandle, playerIndex, &nativePlayerIndex);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outHandle = GamePad::GetSteamHandleEXT(nativePlayerIndex);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_get_connection_state_ext(
    const CNA_Handle gameHandle,
    const CNA_PlayerIndex playerIndex,
    CNA_GamePadConnectionState* const outState)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outState == nullptr) {
            return InvalidInput("The connection-state output is null.");
        }
        PlayerIndex nativePlayerIndex = PlayerIndex::One;
        if (const CNA_Result result =
                BeginGamePadCall(gameHandle, playerIndex, &nativePlayerIndex);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outState = MapConnectionState(GamePad::GetConnectionStateEXT(nativePlayerIndex));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_get_touchpad_count_ext(
    const CNA_Handle gameHandle,
    const CNA_PlayerIndex playerIndex,
    int32_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr) {
            return InvalidInput("The touchpad-count output is null.");
        }
        PlayerIndex nativePlayerIndex = PlayerIndex::One;
        if (const CNA_Result result =
                BeginGamePadCall(gameHandle, playerIndex, &nativePlayerIndex);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = static_cast<int32_t>(GamePad::GetTouchpadCountEXT(nativePlayerIndex));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_get_touchpad_finger_count_ext(
    const CNA_Handle gameHandle,
    const CNA_PlayerIndex playerIndex,
    const int32_t touchpad,
    int32_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr) {
            return InvalidInput("The touchpad finger-count output is null.");
        }
        PlayerIndex nativePlayerIndex = PlayerIndex::One;
        if (const CNA_Result result =
                BeginGamePadCall(gameHandle, playerIndex, &nativePlayerIndex);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = static_cast<int32_t>(
            GamePad::GetTouchpadFingerCountEXT(nativePlayerIndex, static_cast<int>(touchpad)));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamepad_get_touchpad_finger_ext(
    const CNA_Handle gameHandle,
    const CNA_PlayerIndex playerIndex,
    const int32_t touchpad,
    const int32_t finger,
    CNA_GamePadTouchpadFinger* const outFinger,
    CNA_Bool* const outAvailable)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outFinger == nullptr || outAvailable == nullptr) {
            return InvalidInput("The touchpad finger output is null.");
        }
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
            outFinger->is_down = down ? CNA_TRUE : CNA_FALSE;
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
    });
}

namespace {

[[nodiscard]] CNA_Result ValidateKey(const CNA_Key key)
{
    if (key >= KeyboardSlotCount) {
        return InvalidInput("The key identity is outside the canonical 256-slot range.");
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
        return InvalidInput("The key text size output is null.");
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
    return CopyDeviceText(route(static_cast<Keys>(key)), destination, capacity, outBytes);
}

using KeyLookupRoute = Keys (*)(const std::string&);

[[nodiscard]] CNA_Result LookUpKey(
    const KeyLookupRoute route,
    const CNA_Handle gameHandle,
    const CNA_StringView name,
    CNA_Key* const outKey)
{
    if (outKey == nullptr) {
        return InvalidInput("The key identity output is null.");
    }
    std::string text;
    if (const CNA_Result result = CNA::C::Detail::CopyStringView(name, true, &text);
        result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            CNA::C::Detail::ErrorCategoryForResult(result),
            "The key name is invalid.");
    }
    if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outKey = static_cast<CNA_Key>(route(text));
    return CNA_RESULT_SUCCESS;
}

} // namespace

CNA_Result cna_keyboard_state_init(CNA_KeyboardState* const outState)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outState == nullptr) {
            return InvalidInput("The keyboard snapshot output is null.");
        }
        *outState = SnapshotKeyboard(KeyboardState());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_keyboard_state_init_from_keys(
    const CNA_Key* const keys,
    const uint64_t count,
    CNA_KeyboardState* const outState)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outState == nullptr) {
            return InvalidInput("The keyboard snapshot output is null.");
        }
        if (keys == nullptr && count != 0U) {
            return InvalidInput("The key array is invalid.");
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
    });
}

CNA_Result cna_keyboard_state_get_key_state(
    const CNA_KeyboardState* const state,
    const CNA_Key key,
    CNA_KeyState* const outKeyState)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outKeyState == nullptr) {
            return InvalidInput("The key-state output is null.");
        }
        if (const CNA_Result result = ValidateKey(key); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ValidateKeyboardState(state);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outKeyState = NativeKeyboardState(*state).getItem(static_cast<Keys>(key)) ==
                Microsoft::Xna::Framework::Input::KeyState::Down
            ? CNA_KEY_STATE_DOWN
            : CNA_KEY_STATE_UP;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_keyboard_state_equals(
    const CNA_KeyboardState* const left,
    const CNA_KeyboardState* const right,
    CNA_Bool* const outEquals)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEquals == nullptr) {
            return InvalidInput("The keyboard comparison output is null.");
        }
        if (const CNA_Result result = ValidateKeyboardState(left);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ValidateKeyboardState(right);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outEquals = NativeKeyboardState(*left).Equals(NativeKeyboardState(*right))
            ? CNA_TRUE
            : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_keyboard_state_not_equals(
    const CNA_KeyboardState* const left,
    const CNA_KeyboardState* const right,
    CNA_Bool* const outNotEquals)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        CNA_Bool equals = CNA_FALSE;
        if (const CNA_Result result = cna_keyboard_state_equals(left, right, &equals);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outNotEquals == nullptr) {
            return InvalidInput("The keyboard comparison output is null.");
        }
        *outNotEquals = equals != CNA_FALSE ? CNA_FALSE : CNA_TRUE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_keyboard_state_get_hash_code(
    const CNA_KeyboardState* const state,
    int32_t* const outHash)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHash == nullptr) {
            return InvalidInput("The keyboard hash output is null.");
        }
        if (const CNA_Result result = ValidateKeyboardState(state);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outHash = static_cast<int32_t>(NativeKeyboardState(*state).GetHashCode());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_keyboard_state_get_string_size(
    const CNA_KeyboardState* const state,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The keyboard text output is null.");
        }
        if (const CNA_Result result = ValidateKeyboardState(state);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = KeyboardState().ToString().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_keyboard_state_copy_string(
    const CNA_KeyboardState* const state,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateKeyboardState(state);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyDeviceText(KeyboardState().ToString(), destination, capacity, outBytes);
    });
}

CNA_Result cna_keyboard_get_state_for_player(
    const CNA_Handle gameHandle,
    const CNA_PlayerIndex playerIndex,
    CNA_KeyboardState* const outState)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateKeyboardState(outState);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        PlayerIndex nativePlayerIndex = PlayerIndex::One;
        if (const CNA_Result result =
                BeginGamePadCall(gameHandle, playerIndex, &nativePlayerIndex);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outState = SnapshotKeyboard(Keyboard::GetState(nativePlayerIndex));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_keyboard_get_key_from_scancode_ext(
    const CNA_Handle gameHandle,
    const CNA_Key scancode,
    CNA_Key* const outKey)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outKey == nullptr) {
            return InvalidInput("The key identity output is null.");
        }
        if (const CNA_Result result = ValidateKey(scancode); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outKey = static_cast<CNA_Key>(
            Keyboard::GetKeyFromScancodeEXT(static_cast<Keys>(scancode)));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_keyboard_get_mod_state_ext(
    const CNA_Handle gameHandle,
    CNA_KeyModifiers* const outModifiers)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outModifiers == nullptr) {
            return InvalidInput("The modifier-state output is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outModifiers = MapKeyModifiers(Keyboard::GetModStateEXT());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_keyboard_get_scancode_name_size_ext(
    const CNA_Handle gameHandle,
    const CNA_Key key,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReportKeyTextSize(&Keyboard::GetScancodeNameEXT, gameHandle, key, outBytes);
    });
}

CNA_Result cna_keyboard_copy_scancode_name_ext(
    const CNA_Handle gameHandle,
    const CNA_Key key,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CopyKeyTextValue(
            &Keyboard::GetScancodeNameEXT,
            gameHandle,
            key,
            destination,
            capacity,
            outBytes);
    });
}

CNA_Result cna_keyboard_get_scancode_from_name_ext(
    const CNA_Handle gameHandle,
    const CNA_StringView name,
    CNA_Key* const outKey)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return LookUpKey(&Keyboard::GetScancodeFromNameEXT, gameHandle, name, outKey);
    });
}

CNA_Result cna_keyboard_get_key_name_size_ext(
    const CNA_Handle gameHandle,
    const CNA_Key key,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return ReportKeyTextSize(&Keyboard::GetKeyNameEXT, gameHandle, key, outBytes);
    });
}

CNA_Result cna_keyboard_copy_key_name_ext(
    const CNA_Handle gameHandle,
    const CNA_Key key,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CopyKeyTextValue(
            &Keyboard::GetKeyNameEXT,
            gameHandle,
            key,
            destination,
            capacity,
            outBytes);
    });
}

CNA_Result cna_keyboard_get_key_from_name_ext(
    const CNA_Handle gameHandle,
    const CNA_StringView name,
    CNA_Key* const outKey)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return LookUpKey(&Keyboard::GetKeyFromNameEXT, gameHandle, name, outKey);
    });
}

namespace {

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
        return InvalidInput("The mouse button mask contains an undefined bit.");
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

} // namespace

CNA_Result cna_mouse_state_init(CNA_MouseState* const outState)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outState == nullptr) {
            return InvalidInput("The mouse snapshot output is null.");
        }
        *outState = SnapshotMouse(MouseState());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_mouse_state_init_from_values(
    const int32_t x,
    const int32_t y,
    const int32_t scrollWheel,
    const CNA_MouseButtonFlags pressedButtons,
    CNA_MouseState* const outState)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outState == nullptr) {
            return InvalidInput("The mouse snapshot output is null.");
        }
        if (const CNA_Result result = ValidateMouseButtons(pressedButtons);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outState = SnapshotMouse(MouseState(
            static_cast<int>(x),
            static_cast<int>(y),
            static_cast<int>(scrollWheel),
            MouseButton(pressedButtons, CNA_MOUSE_BUTTON_LEFT),
            MouseButton(pressedButtons, CNA_MOUSE_BUTTON_MIDDLE),
            MouseButton(pressedButtons, CNA_MOUSE_BUTTON_RIGHT),
            MouseButton(pressedButtons, CNA_MOUSE_BUTTON_X1),
            MouseButton(pressedButtons, CNA_MOUSE_BUTTON_X2)));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_mouse_state_init_from_values_ext(
    const int32_t x,
    const int32_t y,
    const int32_t scrollWheel,
    const int32_t horizontalScrollWheel,
    const CNA_MouseButtonFlags pressedButtons,
    CNA_MouseState* const outState)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outState == nullptr) {
            return InvalidInput("The mouse snapshot output is null.");
        }
        if (const CNA_Result result = ValidateMouseButtons(pressedButtons);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        CNA_MouseState state = {
            sizeof(CNA_MouseState), StructureVersion, x, y, scrollWheel, horizontalScrollWheel,
            pressedButtons, 0U
        };
        *outState = SnapshotMouse(NativeMouseState(state));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_mouse_state_equals(
    const CNA_MouseState* const left,
    const CNA_MouseState* const right,
    CNA_Bool* const outEquals)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEquals == nullptr) {
            return InvalidInput("The mouse comparison output is null.");
        }
        if (const CNA_Result result = ValidateMouseState(left); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ValidateMouseState(right); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outEquals = NativeMouseState(*left).Equals(NativeMouseState(*right))
            ? CNA_TRUE
            : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_mouse_state_not_equals(
    const CNA_MouseState* const left,
    const CNA_MouseState* const right,
    CNA_Bool* const outNotEquals)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        CNA_Bool equals = CNA_FALSE;
        if (const CNA_Result result = cna_mouse_state_equals(left, right, &equals);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outNotEquals == nullptr) {
            return InvalidInput("The mouse comparison output is null.");
        }
        *outNotEquals = equals != CNA_FALSE ? CNA_FALSE : CNA_TRUE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_mouse_state_get_hash_code(
    const CNA_MouseState* const state,
    int32_t* const outHash)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHash == nullptr) {
            return InvalidInput("The mouse hash output is null.");
        }
        if (const CNA_Result result = ValidateMouseState(state); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outHash = static_cast<int32_t>(NativeMouseState(*state).GetHashCode());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_mouse_state_get_string_size(
    const CNA_MouseState* const state,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The mouse text output is null.");
        }
        if (const CNA_Result result = ValidateMouseState(state); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = NativeMouseState(*state).ToString().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_mouse_state_copy_string(
    const CNA_MouseState* const state,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateMouseState(state); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyDeviceText(
            NativeMouseState(*state).ToString(),
            destination,
            capacity,
            outBytes);
    });
}

CNA_Result cna_mouse_get_window_handle(const CNA_Handle gameHandle, uint64_t* const outWindow)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outWindow == nullptr) {
            return InvalidInput("The mouse window-handle output is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outWindow = static_cast<uint64_t>(Mouse::getWindowHandleProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_mouse_set_window_handle(const CNA_Handle gameHandle, const uint64_t window)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Mouse::setWindowHandleProperty(static_cast<std::uintptr_t>(window));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_mouse_set_position(const CNA_Handle gameHandle, const int32_t x, const int32_t y)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Mouse::SetPosition(static_cast<int>(x), static_cast<int>(y));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_mouse_get_is_relative_mouse_mode_ext(
    const CNA_Handle gameHandle,
    CNA_Bool* const outEnabled)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEnabled == nullptr) {
            return InvalidInput("The relative-mode output is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outEnabled = Mouse::getIsRelativeMouseModeEXTProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_mouse_set_is_relative_mouse_mode_ext(
    const CNA_Handle gameHandle,
    const CNA_Bool enabled)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Mouse::setIsRelativeMouseModeEXTProperty(enabled != CNA_FALSE);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_mouse_set_capture_ext(
    const CNA_Handle gameHandle,
    const CNA_Bool enabled,
    CNA_Bool* const outApplied)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outApplied == nullptr) {
            return InvalidInput("The mouse capture output is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outApplied = Mouse::SetCaptureEXT(enabled != CNA_FALSE) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_mouse_get_global_position_ext(
    const CNA_Handle gameHandle,
    int32_t* const outX,
    int32_t* const outY)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outX == nullptr || outY == nullptr) {
            return InvalidInput("The global-position output is null.");
        }
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
    });
}

CNA_Result cna_mouse_warp_global_ext(
    const CNA_Handle gameHandle,
    const int32_t x,
    const int32_t y,
    CNA_Bool* const outApplied)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outApplied == nullptr) {
            return InvalidInput("The mouse warp output is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outApplied = Mouse::WarpGlobalEXT(static_cast<int>(x), static_cast<int>(y))
            ? CNA_TRUE
            : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_mouse_subscribe_clicked_ext(
    const CNA_MouseClickedCallback callback,
    void* const context,
    CNA_MouseEventRegistrationHandle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outRegistration == nullptr) {
            return InvalidInput("The mouse registration output is null.");
        }
        *outRegistration = CNA_INVALID_HANDLE;
        if (callback == nullptr) {
            return InvalidInput("The mouse clicked callback is null.");
        }
        const auto token = Mouse::ClickedEXT.Add([callback, context](const int button) {
            callback(static_cast<int32_t>(button), context);
        });
        const auto resource = std::make_shared<MouseClickedRegistration>(token);
        const CNA_Result result = CNA::C::Detail::GetRuntimeHandles().Create(
            CNA::C::Detail::ObjectKind::MouseEventRegistration,
            resource,
            outRegistration);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                CNA::C::Detail::ErrorCategoryForResult(result),
                "The mouse clicked registration could not be created.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_mouse_unsubscribe_clicked_ext(
    const CNA_MouseEventRegistrationHandle registration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<MouseClickedRegistration> resource;
        const CNA_Result getResult = CNA::C::Detail::GetRuntimeHandles().Get(
            registration,
            CNA::C::Detail::ObjectKind::MouseEventRegistration,
            &resource);
        if (getResult != CNA_RESULT_SUCCESS) {
            return Fail(
                getResult,
                CNA::C::Detail::ErrorCategoryForResult(getResult),
                "The mouse clicked registration handle is invalid for this call.");
        }
        const CNA_Result releaseResult =
            CNA::C::Detail::GetRuntimeHandles().Release(registration);
        if (releaseResult == CNA_RESULT_SUCCESS) {
            return CNA_RESULT_SUCCESS;
        }
        return Fail(
            releaseResult,
            CNA::C::Detail::ErrorCategoryForResult(releaseResult),
            "The mouse clicked registration handle could not be released.");
    });
}

CNA_Result cna_mouse_raise_clicked_ext(const CNA_Handle gameHandle, const int32_t button)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Mouse::INTERNAL_onClicked(static_cast<int>(button));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_mouse_reset_for_tests_ext(const CNA_Handle gameHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Mouse::ResetForTests();
        return CNA_RESULT_SUCCESS;
    });
}
