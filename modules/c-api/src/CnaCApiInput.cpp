// SPDX-License-Identifier: MS-PL

#include "CNA/C/input.h"
#include "CNA/C/input_gamepad.h"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Xna/Framework/Input/ButtonState.hpp"
#include "Microsoft/Xna/Framework/Input/Buttons.hpp"
#include "Microsoft/Xna/Framework/Input/GamePad.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadCapabilities.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadDeadZone.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadState.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadType.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"
#include "Microsoft/Xna/Framework/Input/Keys.hpp"
#include "Microsoft/Xna/Framework/Input/Mouse.hpp"
#include "Microsoft/Xna/Framework/Input/MouseState.hpp"
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

namespace {

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::Fail;
using CNA::C::Detail::ValidateActiveGameHandle;
using Microsoft::Xna::Framework::PlayerIndex;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Input::ButtonState;
using Microsoft::Xna::Framework::Input::Buttons;
using Microsoft::Xna::Framework::Input::GamePad;
using Microsoft::Xna::Framework::Input::GamePadCapabilities;
using Microsoft::Xna::Framework::Input::GamePadDeadZone;
using Microsoft::Xna::Framework::Input::GamePadState;
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
