// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Input/InputManager.hpp"
#include "CNA/Internal/Input/SdlInputBridge.hpp"
#include "CNA/Internal/Input/GestureDetector.hpp"
#include "Microsoft/Xna/Framework/Input/Mouse.hpp"
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef __ANDROID__
#include <SDL3/SDL.h>
#endif

namespace CNA::Internal::Input
{
    namespace
    {
        using Microsoft::Xna::Framework::Input::ButtonState;
        using Microsoft::Xna::Framework::Input::Touch::TouchLocationState;

        struct InternalMouseState
        {
            int X = 0;
            int Y = 0;
            int ScrollWheelValue = 0;
            int HorizontalScrollWheelValue = 0; // CNAEXT/EXT — SDL wheel.x, surfaced via MouseState EXT
            ButtonState LeftButton = ButtonState::Released;
            ButtonState RightButton = ButtonState::Released;
            ButtonState MiddleButton = ButtonState::Released;
            ButtonState XButton1 = ButtonState::Released;
            ButtonState XButton2 = ButtonState::Released;

            // Legacy raw-bridge compatibility accumulator. Public Mouse uses
            // IPlatformMouse::ConsumeRelativeDelta after PLAT-80, but the SDL-shaped bridge tests
            // keep this state until PLAT-90 retires that adapter.
            bool RelativeMode = false;
            float RelativeDeltaX = 0.0f;
            float RelativeDeltaY = 0.0f;
        };

        struct InternalTouchLocationState
        {
            int Id = 0;
            TouchLocationState State = TouchLocationState::Invalid;
            Microsoft::Xna::Framework::Vector2 Position = Microsoft::Xna::Framework::Vector2();
            bool RemoveAfterSnapshot = false;
            // Previous-frame location (what the last GetTouchState() reported), so a Moved/Released
            // touch exposes TryGetPreviousLocation() (task 868–870). Invalid = no previous yet.
            TouchLocationState PreviousState = TouchLocationState::Invalid;
            Microsoft::Xna::Framework::Vector2 PreviousPosition = Microsoft::Xna::Framework::Vector2();
            // CNAEXT/EXT: SDL finger pressure (0..1), surfaced via TouchLocation::getPressureEXT.
            float Pressure = 0.0f;
        };

        struct InternalInputState
        {
            InternalMouseState Mouse;
            std::unordered_set<Microsoft::Xna::Framework::Input::Keys> PressedKeys;
            std::unordered_map<int, InternalTouchLocationState> TouchLocations;
        };

        InternalInputState& getInternalInputState()
        {
            static InternalInputState state{};
            return state;
        }
    }

    void InputManager::ResetForTests()
    {
        getInternalInputState() = InternalInputState{};
    }

    void InputManager::ResetAllForTests()
    {
        // Deterministic order: clear the bridge's file-static event bookkeeping first, then the
        // accumulated input singleton, then the higher-level panels/handlers. All entries are
        // independent process-wide statics, so ordering is for reproducibility, not correctness.
        SdlInputBridge::ResetForTests();
        ResetForTests();
        Microsoft::Xna::Framework::Input::Touch::TouchPanel::ResetForTests();
        GestureDetector::ResetForTests();
        Microsoft::Xna::Framework::Input::Mouse::ResetForTests();
        Microsoft::Xna::Framework::Input::TextInputEXT::ResetForTests();
    }

    void InputManager::SetMousePosition(const int x, const int y)
    {
        auto& mouseState = getInternalInputState().Mouse;
        mouseState.X = x;
        mouseState.Y = y;
    }

    void InputManager::SetMouseButtonState(
        const MouseButton button,
        const Microsoft::Xna::Framework::Input::ButtonState state
    )
    {
        auto& mouseState = getInternalInputState().Mouse;
        switch (button)
        {
        case MouseButton::Left:
            mouseState.LeftButton = state;
            break;
        case MouseButton::Right:
            mouseState.RightButton = state;
            break;
        case MouseButton::Middle:
            mouseState.MiddleButton = state;
            break;
        case MouseButton::XButton1:
            mouseState.XButton1 = state;
            break;
        case MouseButton::XButton2:
            mouseState.XButton2 = state;
            break;
        }
    }

    void InputManager::AddScrollWheelDelta(const int delta)
    {
        auto& mouseState = getInternalInputState().Mouse;
        mouseState.ScrollWheelValue += delta;
    }

    void InputManager::AddHorizontalScrollWheelDelta(const int delta)
    {
        auto& mouseState = getInternalInputState().Mouse;
        mouseState.HorizontalScrollWheelValue += delta;
    }

    void InputManager::SetMouseRelativeMode(const bool enabled)
    {
        auto& mouseState = getInternalInputState().Mouse;
        mouseState.RelativeMode = enabled;
        // Flush stale accumulated motion on toggle, matching SDL3_FNAPlatform's
        // throwaway SDL_GetRelativeMouseState() call on enable.
        mouseState.RelativeDeltaX = 0.0f;
        mouseState.RelativeDeltaY = 0.0f;
    }

    void InputManager::AddMouseRelativeDelta(const float dx, const float dy)
    {
        auto& mouseState = getInternalInputState().Mouse;
        if (!mouseState.RelativeMode)
        {
            return;
        }
        mouseState.RelativeDeltaX += dx;
        mouseState.RelativeDeltaY += dy;
    }

    void InputManager::SetKeyState(
        const Microsoft::Xna::Framework::Input::Keys key,
        const bool pressed
    )
    {
        auto& pressedKeys = getInternalInputState().PressedKeys;
        if (pressed)
        {
            pressedKeys.insert(key);
            return;
        }
        pressedKeys.erase(key);
    }

    void InputManager::SetTouchState(
        const int touchId,
        const TouchLocationState state,
        const Microsoft::Xna::Framework::Vector2& position,
        const float pressure
    )
    {
        auto& touchLocations = getInternalInputState().TouchLocations;
        auto& touchLocation = touchLocations[touchId];
        touchLocation.Id = touchId;
        touchLocation.State = state;
        touchLocation.Position = position;
        touchLocation.Pressure = pressure;
        touchLocation.RemoveAfterSnapshot = state == TouchLocationState::Released;
    }

    Microsoft::Xna::Framework::Input::MouseState InputManager::GetMouseState()
    {
        using Microsoft::Xna::Framework::Input::ButtonState;
        auto& mouseState = getInternalInputState().Mouse;

        int x = mouseState.X;
        int y = mouseState.Y;
        if (mouseState.RelativeMode)
        {
            x = static_cast<int>(mouseState.RelativeDeltaX);
            y = static_cast<int>(mouseState.RelativeDeltaY);
            mouseState.RelativeDeltaX = 0.0f;
            mouseState.RelativeDeltaY = 0.0f;
        }

        return Microsoft::Xna::Framework::Input::MouseState(
            x,
            y,
            mouseState.ScrollWheelValue,
            mouseState.LeftButton,
            mouseState.MiddleButton,
            mouseState.RightButton,
            mouseState.XButton1,
            mouseState.XButton2,
            mouseState.HorizontalScrollWheelValue // CNAEXT/EXT 9th arg
        );
    }

    Microsoft::Xna::Framework::Input::KeyboardState InputManager::GetKeyboardState()
    {
        const auto& pressedKeys = getInternalInputState().PressedKeys;
#ifdef __ANDROID__
        if (!pressedKeys.empty())
        {
            std::string keyList;
            for (const auto k : pressedKeys)
            {
                keyList += std::to_string(static_cast<int>(k));
                keyList += ' ';
            }
            SDL_Log("[Keyboard] GetKeyboardState: pressed=%zu [%s]",
                    pressedKeys.size(), keyList.c_str());
        }
#endif
        return Microsoft::Xna::Framework::Input::KeyboardState(pressedKeys);
    }

    bool InputManager::HasAnyTouch()
    {
        return !getInternalInputState().TouchLocations.empty();
    }

    Microsoft::Xna::Framework::Input::Touch::TouchCollection InputManager::GetTouchState()
    {
        auto& touchLocations = getInternalInputState().TouchLocations;

        std::vector<int> sortedTouchIds;
        sortedTouchIds.reserve(touchLocations.size());
        for (const auto& [touchId, _] : touchLocations)
        {
            sortedTouchIds.push_back(touchId);
        }
        std::sort(sortedTouchIds.begin(), sortedTouchIds.end());

        std::vector<Microsoft::Xna::Framework::Input::Touch::TouchLocation> snapshot;
        snapshot.reserve(sortedTouchIds.size());

        for (const int touchId : sortedTouchIds)
        {
            const auto touchLocationIterator = touchLocations.find(touchId);
            if (touchLocationIterator == touchLocations.end())
            {
                continue;
            }

            const auto& touchLocation = touchLocationIterator->second;

            // Expose the previous-frame location for Moved/Released touches (Pressed/new touches
            // have no previous, matching FNA). Previous is "what AdvanceTouchFrame() last recorded".
            if (touchLocation.PreviousState != TouchLocationState::Invalid)
            {
                snapshot.emplace_back(touchLocation.Id, touchLocation.State, touchLocation.Position,
                                      touchLocation.PreviousState, touchLocation.PreviousPosition,
                                      touchLocation.Pressure);
            }
            else
            {
                snapshot.emplace_back(touchLocation.Id, touchLocation.State, touchLocation.Position,
                                      touchLocation.Pressure);
            }
        }

        return Microsoft::Xna::Framework::Input::Touch::TouchCollection(std::move(snapshot));
    }

    void InputManager::AdvanceTouchFrame()
    {
        auto& touchLocations = getInternalInputState().TouchLocations;

        std::vector<int> touchIdsToRemove;
        touchIdsToRemove.reserve(touchLocations.size());

        for (auto& [touchId, touchLocation] : touchLocations)
        {
            // Record the location just reported as "previous" for the next snapshot — done before
            // the Pressed→Moved promotion below, so a promoted touch's previous is the Pressed
            // location the game actually saw, not the promoted Moved state.
            touchLocation.PreviousState    = touchLocation.State;
            touchLocation.PreviousPosition = touchLocation.Position;

            if (touchLocation.RemoveAfterSnapshot)
            {
                touchIdsToRemove.push_back(touchId);
                continue;
            }

            if (touchLocation.State == TouchLocationState::Pressed)
            {
                touchLocation.State = TouchLocationState::Moved;
            }
        }

        for (const int touchId : touchIdsToRemove)
        {
            touchLocations.erase(touchId);
        }
    }


}
