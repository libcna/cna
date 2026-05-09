#include "CNA/Internal/Input/InputManager.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef __ANDROID__
#include <SDL3/SDL.h>
#endif

namespace CNA::Internal::Input {
    namespace {
        using Microsoft::Xna::Framework::Input::ButtonState;
        using Microsoft::Xna::Framework::Input::GamePadButtons;
        using Microsoft::Xna::Framework::Input::GamePadState;
        using Microsoft::Xna::Framework::PlayerIndex;
        using Microsoft::Xna::Framework::Input::Touch::TouchLocationState;

        struct InternalMouseState {
            int X = 0;
            int Y = 0;
            int ScrollWheelValue = 0;
            ButtonState LeftButton = ButtonState::Released;
            ButtonState RightButton = ButtonState::Released;
            ButtonState MiddleButton = ButtonState::Released;
        };

        struct InternalGamePadState {
            bool IsConnected = false;

            ButtonState A = ButtonState::Released;
            ButtonState B = ButtonState::Released;
            ButtonState X = ButtonState::Released;
            ButtonState Y = ButtonState::Released;
            ButtonState Back = ButtonState::Released;
            ButtonState Start = ButtonState::Released;
            ButtonState LeftShoulder = ButtonState::Released;
            ButtonState RightShoulder = ButtonState::Released;
            ButtonState LeftStick = ButtonState::Released;
            ButtonState RightStick = ButtonState::Released;
            ButtonState DPadUp = ButtonState::Released;
            ButtonState DPadDown = ButtonState::Released;
            ButtonState DPadLeft = ButtonState::Released;
            ButtonState DPadRight = ButtonState::Released;

            float LeftThumbstickX = 0.0f;
            float LeftThumbstickY = 0.0f;
            float RightThumbstickX = 0.0f;
            float RightThumbstickY = 0.0f;
            float LeftTrigger = 0.0f;
            float RightTrigger = 0.0f;
        };

        struct InternalTouchLocationState {
            int Id = 0;
            TouchLocationState State = TouchLocationState::Invalid;
            Microsoft::Xna::Framework::Vector2 Position = Microsoft::Xna::Framework::Vector2();
            bool RemoveAfterSnapshot = false;
        };

        struct InternalInputState {
            InternalMouseState Mouse;
            std::array<InternalGamePadState, 4> GamePads;
            std::unordered_set<Microsoft::Xna::Framework::Input::Keys> PressedKeys;
            std::unordered_map<int, InternalTouchLocationState> TouchLocations;
        };

        std::optional<std::size_t> try_get_player_slot(const PlayerIndex playerIndex) {
            const int index = static_cast<int>(playerIndex);
            if (index < 0 || index >= 4) {
                return std::nullopt;
            }
            return static_cast<std::size_t>(index);
        }

        float clamp_signed_unit(const float value) {
            return std::clamp(value, -1.0f, 1.0f);
        }

        float clamp_positive_unit(const float value) {
            return std::clamp(value, 0.0f, 1.0f);
        }

        InternalInputState& getInternalInputState() {
            static InternalInputState state{};
            return state;
        }
    }

    void InputManager::SetMousePosition(const int x, const int y) {
        auto& mouseState = getInternalInputState().Mouse;
        mouseState.X = x;
        mouseState.Y = y;
    }

    void InputManager::SetMouseButtonState(
        const MouseButton button,
        const Microsoft::Xna::Framework::Input::ButtonState state
    ) {
        auto& mouseState = getInternalInputState().Mouse;
        switch (button) {
            case MouseButton::Left:
                mouseState.LeftButton = state;
                break;
            case MouseButton::Right:
                mouseState.RightButton = state;
                break;
            case MouseButton::Middle:
                mouseState.MiddleButton = state;
                break;
        }
    }

    void InputManager::AddScrollWheelDelta(const int delta) {
        auto& mouseState = getInternalInputState().Mouse;
        mouseState.ScrollWheelValue += delta;
    }

    void InputManager::SetKeyState(
        const Microsoft::Xna::Framework::Input::Keys key,
        const bool pressed
    ) {
        auto& pressedKeys = getInternalInputState().PressedKeys;
        if (pressed) {
            pressedKeys.insert(key);
            return;
        }
        pressedKeys.erase(key);
    }

    void InputManager::SetTouchState(
        const int touchId,
        const TouchLocationState state,
        const Microsoft::Xna::Framework::Vector2& position
    ) {
        auto& touchLocations = getInternalInputState().TouchLocations;
        auto& touchLocation = touchLocations[touchId];
        touchLocation.Id = touchId;
        touchLocation.State = state;
        touchLocation.Position = position;
        touchLocation.RemoveAfterSnapshot = state == TouchLocationState::Released;
    }

    void InputManager::SetGamePadConnection(const PlayerIndex playerIndex, const bool isConnected) {
        const auto slot = try_get_player_slot(playerIndex);
        if (!slot.has_value()) {
            return;
        }

        auto& gamePadState = getInternalInputState().GamePads[slot.value()];
        if (isConnected) {
            gamePadState.IsConnected = true;
            return;
        }

        gamePadState = InternalGamePadState();
    }

    void InputManager::SetGamePadButtonState(
        const PlayerIndex playerIndex,
        const GamePadButton button,
        const ButtonState state
    ) {
        const auto slot = try_get_player_slot(playerIndex);
        if (!slot.has_value()) {
            return;
        }

        auto& gamePadState = getInternalInputState().GamePads[slot.value()];
        switch (button) {
            case GamePadButton::A:
                gamePadState.A = state;
                break;
            case GamePadButton::B:
                gamePadState.B = state;
                break;
            case GamePadButton::X:
                gamePadState.X = state;
                break;
            case GamePadButton::Y:
                gamePadState.Y = state;
                break;
            case GamePadButton::Back:
                gamePadState.Back = state;
                break;
            case GamePadButton::Start:
                gamePadState.Start = state;
                break;
            case GamePadButton::LeftShoulder:
                gamePadState.LeftShoulder = state;
                break;
            case GamePadButton::RightShoulder:
                gamePadState.RightShoulder = state;
                break;
            case GamePadButton::LeftStick:
                gamePadState.LeftStick = state;
                break;
            case GamePadButton::RightStick:
                gamePadState.RightStick = state;
                break;
            case GamePadButton::DPadUp:
                gamePadState.DPadUp = state;
                break;
            case GamePadButton::DPadDown:
                gamePadState.DPadDown = state;
                break;
            case GamePadButton::DPadLeft:
                gamePadState.DPadLeft = state;
                break;
            case GamePadButton::DPadRight:
                gamePadState.DPadRight = state;
                break;
        }
    }

    void InputManager::SetGamePadAxisValue(
        const PlayerIndex playerIndex,
        const GamePadAxis axis,
        const float value
    ) {
        const auto slot = try_get_player_slot(playerIndex);
        if (!slot.has_value()) {
            return;
        }

        auto& gamePadState = getInternalInputState().GamePads[slot.value()];
        switch (axis) {
            case GamePadAxis::LeftThumbstickX:
                gamePadState.LeftThumbstickX = clamp_signed_unit(value);
                break;
            case GamePadAxis::LeftThumbstickY:
                gamePadState.LeftThumbstickY = clamp_signed_unit(value);
                break;
            case GamePadAxis::RightThumbstickX:
                gamePadState.RightThumbstickX = clamp_signed_unit(value);
                break;
            case GamePadAxis::RightThumbstickY:
                gamePadState.RightThumbstickY = clamp_signed_unit(value);
                break;
            case GamePadAxis::LeftTrigger:
                gamePadState.LeftTrigger = clamp_positive_unit(value);
                break;
            case GamePadAxis::RightTrigger:
                gamePadState.RightTrigger = clamp_positive_unit(value);
                break;
        }
    }

    Microsoft::Xna::Framework::Input::MouseState InputManager::GetMouseState() {
        const auto& mouseState = getInternalInputState().Mouse;
        return Microsoft::Xna::Framework::Input::MouseState(
            mouseState.X,
            mouseState.Y,
            mouseState.LeftButton,
            mouseState.RightButton,
            mouseState.MiddleButton,
            mouseState.ScrollWheelValue
        );
    }

    Microsoft::Xna::Framework::Input::KeyboardState InputManager::GetKeyboardState() {
        const auto& pressedKeys = getInternalInputState().PressedKeys;
#ifdef __ANDROID__
        if (!pressedKeys.empty()) {
            std::string keyList;
            for (const auto k : pressedKeys) {
                keyList += std::to_string(static_cast<int>(k));
                keyList += ' ';
            }
            SDL_Log("[Keyboard] GetKeyboardState: pressed=%zu [%s]",
                    pressedKeys.size(), keyList.c_str());
        }
#endif
        return Microsoft::Xna::Framework::Input::KeyboardState(pressedKeys);
    }

    Microsoft::Xna::Framework::Input::Touch::TouchCollection InputManager::GetTouchState() {
        auto& touchLocations = getInternalInputState().TouchLocations;

        std::vector<int> sortedTouchIds;
        sortedTouchIds.reserve(touchLocations.size());
        for (const auto& [touchId, _] : touchLocations) {
            sortedTouchIds.push_back(touchId);
        }
        std::sort(sortedTouchIds.begin(), sortedTouchIds.end());

        std::vector<Microsoft::Xna::Framework::Input::Touch::TouchLocation> snapshot;
        snapshot.reserve(sortedTouchIds.size());
        std::vector<int> touchIdsToRemove;

        for (const int touchId : sortedTouchIds) {
            const auto touchLocationIterator = touchLocations.find(touchId);
            if (touchLocationIterator == touchLocations.end()) {
                continue;
            }

            auto& touchLocation = touchLocationIterator->second;
            snapshot.emplace_back(touchLocation.Id, touchLocation.State, touchLocation.Position);

            if (touchLocation.RemoveAfterSnapshot) {
                touchIdsToRemove.push_back(touchId);
                continue;
            }

            if (touchLocation.State == TouchLocationState::Pressed) {
                touchLocation.State = TouchLocationState::Moved;
            }
        }

        for (const int touchId : touchIdsToRemove) {
            touchLocations.erase(touchId);
        }

        return Microsoft::Xna::Framework::Input::Touch::TouchCollection(std::move(snapshot));
    }

    GamePadState InputManager::GetGamePadState(const PlayerIndex playerIndex) {
        const auto slot = try_get_player_slot(playerIndex);
        if (!slot.has_value()) {
            return GamePadState();
        }

        const auto& gamePadState = getInternalInputState().GamePads[slot.value()];
        if (!gamePadState.IsConnected) {
            return GamePadState();
        }

        return GamePadState(
            GamePadButtons(
                gamePadState.A,
                gamePadState.B,
                gamePadState.X,
                gamePadState.Y,
                gamePadState.Back,
                gamePadState.Start,
                gamePadState.LeftShoulder,
                gamePadState.RightShoulder,
                gamePadState.LeftStick,
                gamePadState.RightStick,
                gamePadState.DPadUp,
                gamePadState.DPadDown,
                gamePadState.DPadLeft,
                gamePadState.DPadRight
            ),
            Microsoft::Xna::Framework::Vector2(gamePadState.LeftThumbstickX, gamePadState.LeftThumbstickY),
            Microsoft::Xna::Framework::Vector2(gamePadState.RightThumbstickX, gamePadState.RightThumbstickY),
            gamePadState.LeftTrigger,
            gamePadState.RightTrigger,
            true
        );
    }
}
