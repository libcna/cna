#include "CNA/Internal/Input/InputManager.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace CNA::Internal::Input {
    namespace {
        using Microsoft::Xna::Framework::Input::Touch::TouchLocationState;

        struct InternalMouseState {
            int X = 0;
            int Y = 0;
            int ScrollWheelValue = 0;
            Microsoft::Xna::Framework::Input::ButtonState LeftButton = Microsoft::Xna::Framework::Input::ButtonState::Released;
            Microsoft::Xna::Framework::Input::ButtonState RightButton = Microsoft::Xna::Framework::Input::ButtonState::Released;
            Microsoft::Xna::Framework::Input::ButtonState MiddleButton = Microsoft::Xna::Framework::Input::ButtonState::Released;
        };

        struct InternalTouchLocationState {
            int Id = 0;
            TouchLocationState State = TouchLocationState::Invalid;
            Microsoft::Xna::Framework::Vector2 Position = Microsoft::Xna::Framework::Vector2();
            bool RemoveAfterSnapshot = false;
        };

        struct InternalInputState {
            InternalMouseState Mouse;
            std::unordered_set<Microsoft::Xna::Framework::Input::Keys> PressedKeys;
            std::unordered_map<int, InternalTouchLocationState> TouchLocations;
        };

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
}
