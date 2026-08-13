// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Input/InputManager.hpp"
#include "CNA/Internal/Input/SdlInputBridge.hpp"
#include "CNA/Internal/Input/GestureDetector.hpp"
#include "CNA/Logger.hpp"
#include "Microsoft/Xna/Framework/Input/Mouse.hpp"
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp"

#include <string>
#include <unordered_set>

namespace CNA::Internal::Input
{
    namespace
    {
        using Microsoft::Xna::Framework::Input::ButtonState;

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

        struct InternalInputState
        {
            InternalMouseState Mouse;
            std::unordered_set<Microsoft::Xna::Framework::Input::Keys> PressedKeys;
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
        // platform backend's throwaway relative-motion read on enable.
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
            CNA::Logger::Debug(
                "[Keyboard] GetKeyboardState: pressed="
                + std::to_string(pressedKeys.size()) + " [" + keyList + "]");
        }
#endif
        return Microsoft::Xna::Framework::Input::KeyboardState(pressedKeys);
    }

}
