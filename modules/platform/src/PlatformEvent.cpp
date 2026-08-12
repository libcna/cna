// SPDX-License-Identifier: MS-PL

#include "CNA/Platform/PlatformEvent.hpp"

namespace CNA::Platform {

    namespace {

        template <typename... Ts>
        struct Overloaded : Ts...
        {
            using Ts::operator()...;
        };
        template <typename... Ts>
        Overloaded(Ts...) -> Overloaded<Ts...>;

    } // namespace

    const std::string& GetEventTypeName(const PlatformEvent& event)
    {
        static const std::string quit = "QuitEvent";
        static const std::string window = "WindowEvent";
        static const std::string key = "KeyEvent";
        static const std::string textInput = "TextInputEvent";
        static const std::string textEditing = "TextEditingEvent";
        static const std::string textEditingCandidates = "TextEditingCandidatesEvent";
        static const std::string mouseMotion = "MouseMotionEvent";
        static const std::string mouseButton = "MouseButtonEvent";
        static const std::string mouseWheel = "MouseWheelEvent";
        static const std::string touch = "TouchEvent";
        static const std::string device = "DeviceEvent";
        static const std::string controllerAxis = "ControllerAxisEvent";
        static const std::string controllerButton = "ControllerButtonEvent";
        static const std::string appLifecycle = "AppLifecycleEvent";

        // std::visit over every alternative: adding a PlatformEvent alternative without naming it
        // here fails to compile, the same guarantee the enum switches elsewhere provide.
        return std::visit(Overloaded{
            [&](const QuitEvent&) -> const std::string& { return quit; },
            [&](const WindowEvent&) -> const std::string& { return window; },
            [&](const KeyEvent&) -> const std::string& { return key; },
            [&](const TextInputEvent&) -> const std::string& { return textInput; },
            [&](const TextEditingEvent&) -> const std::string& { return textEditing; },
            [&](const TextEditingCandidatesEvent&) -> const std::string& { return textEditingCandidates; },
            [&](const MouseMotionEvent&) -> const std::string& { return mouseMotion; },
            [&](const MouseButtonEvent&) -> const std::string& { return mouseButton; },
            [&](const MouseWheelEvent&) -> const std::string& { return mouseWheel; },
            [&](const TouchEvent&) -> const std::string& { return touch; },
            [&](const DeviceEvent&) -> const std::string& { return device; },
            [&](const ControllerAxisEvent&) -> const std::string& { return controllerAxis; },
            [&](const ControllerButtonEvent&) -> const std::string& { return controllerButton; },
            [&](const AppLifecycleEvent&) -> const std::string& { return appLifecycle; },
        }, event);
    }

    WindowId GetEventWindow(const PlatformEvent& event)
    {
        return std::visit(Overloaded{
            [](const WindowEvent& e) { return e.window; },
            [](const KeyEvent& e) { return e.window; },
            [](const TextInputEvent& e) { return e.window; },
            [](const TextEditingEvent& e) { return e.window; },
            [](const TextEditingCandidatesEvent& e) { return e.window; },
            [](const MouseMotionEvent& e) { return e.window; },
            [](const MouseButtonEvent& e) { return e.window; },
            [](const MouseWheelEvent& e) { return e.window; },
            [](const TouchEvent& e) { return e.window; },
            // Quit, device connection and application lifecycle are process-scoped, not
            // window-scoped: there is no window to report, and inventing one would be a lie.
            [](const QuitEvent&) { return WindowId{0}; },
            [](const DeviceEvent&) { return WindowId{0}; },
            [](const ControllerAxisEvent&) { return WindowId{0}; },
            [](const ControllerButtonEvent&) { return WindowId{0}; },
            [](const AppLifecycleEvent&) { return WindowId{0}; },
        }, event);
    }

    const std::string& ToString(const WindowEventKind kind)
    {
        static const std::string resized = "Resized";
        static const std::string pixelSizeChanged = "PixelSizeChanged";
        static const std::string focusGained = "FocusGained";
        static const std::string focusLost = "FocusLost";
        static const std::string closeRequested = "CloseRequested";
        static const std::string minimized = "Minimized";
        static const std::string maximized = "Maximized";
        static const std::string restored = "Restored";
        static const std::string moved = "Moved";
        static const std::string displayScaleChanged = "DisplayScaleChanged";
        static const std::string displayChanged = "DisplayChanged";

        switch (kind)
        {
            case WindowEventKind::Resized:             return resized;
            case WindowEventKind::PixelSizeChanged:    return pixelSizeChanged;
            case WindowEventKind::FocusGained:         return focusGained;
            case WindowEventKind::FocusLost:           return focusLost;
            case WindowEventKind::CloseRequested:      return closeRequested;
            case WindowEventKind::Minimized:           return minimized;
            case WindowEventKind::Maximized:           return maximized;
            case WindowEventKind::Restored:            return restored;
            case WindowEventKind::Moved:               return moved;
            case WindowEventKind::DisplayScaleChanged: return displayScaleChanged;
            case WindowEventKind::DisplayChanged:      return displayChanged;
        }
        return resized;
    }

    const std::string& ToString(const TouchEventKind kind)
    {
        static const std::string down = "Down";
        static const std::string up = "Up";
        static const std::string motion = "Motion";
        static const std::string cancelled = "Cancelled";

        switch (kind)
        {
            case TouchEventKind::Down:      return down;
            case TouchEventKind::Up:        return up;
            case TouchEventKind::Motion:    return motion;
            case TouchEventKind::Cancelled: return cancelled;
        }
        return down;
    }

    const std::string& ToString(const InputDeviceKind kind)
    {
        static const std::string keyboard = "Keyboard";
        static const std::string mouse = "Mouse";
        static const std::string gamepad = "Gamepad";
        static const std::string joystick = "Joystick";
        static const std::string touch = "Touch";
        static const std::string haptic = "Haptic";
        static const std::string sensor = "Sensor";

        switch (kind)
        {
            case InputDeviceKind::Keyboard: return keyboard;
            case InputDeviceKind::Mouse:    return mouse;
            case InputDeviceKind::Gamepad:  return gamepad;
            case InputDeviceKind::Joystick: return joystick;
            case InputDeviceKind::Touch:    return touch;
            case InputDeviceKind::Haptic:   return haptic;
            case InputDeviceKind::Sensor:   return sensor;
        }
        return gamepad;
    }

    const std::string& ToString(const GamepadAxis axis)
    {
        static const std::string leftThumbstickX = "LeftThumbstickX";
        static const std::string leftThumbstickY = "LeftThumbstickY";
        static const std::string rightThumbstickX = "RightThumbstickX";
        static const std::string rightThumbstickY = "RightThumbstickY";
        static const std::string leftTrigger = "LeftTrigger";
        static const std::string rightTrigger = "RightTrigger";

        switch (axis)
        {
            case GamepadAxis::LeftThumbstickX:  return leftThumbstickX;
            case GamepadAxis::LeftThumbstickY:  return leftThumbstickY;
            case GamepadAxis::RightThumbstickX: return rightThumbstickX;
            case GamepadAxis::RightThumbstickY: return rightThumbstickY;
            case GamepadAxis::LeftTrigger:      return leftTrigger;
            case GamepadAxis::RightTrigger:     return rightTrigger;
        }
        return leftThumbstickX;
    }

    const std::string& ToString(const GamepadButton button)
    {
        static const std::string a = "A";
        static const std::string b = "B";
        static const std::string x = "X";
        static const std::string y = "Y";
        static const std::string back = "Back";
        static const std::string start = "Start";
        static const std::string leftShoulder = "LeftShoulder";
        static const std::string rightShoulder = "RightShoulder";
        static const std::string leftStick = "LeftStick";
        static const std::string rightStick = "RightStick";
        static const std::string dPadUp = "DPadUp";
        static const std::string dPadDown = "DPadDown";
        static const std::string dPadLeft = "DPadLeft";
        static const std::string dPadRight = "DPadRight";
        static const std::string bigButton = "BigButton";
        static const std::string misc1 = "Misc1";
        static const std::string paddle1 = "Paddle1";
        static const std::string paddle2 = "Paddle2";
        static const std::string paddle3 = "Paddle3";
        static const std::string paddle4 = "Paddle4";
        static const std::string touchPad = "TouchPad";

        switch (button)
        {
            case GamepadButton::A:             return a;
            case GamepadButton::B:             return b;
            case GamepadButton::X:             return x;
            case GamepadButton::Y:             return y;
            case GamepadButton::Back:          return back;
            case GamepadButton::Start:         return start;
            case GamepadButton::LeftShoulder:  return leftShoulder;
            case GamepadButton::RightShoulder: return rightShoulder;
            case GamepadButton::LeftStick:     return leftStick;
            case GamepadButton::RightStick:    return rightStick;
            case GamepadButton::DPadUp:        return dPadUp;
            case GamepadButton::DPadDown:      return dPadDown;
            case GamepadButton::DPadLeft:      return dPadLeft;
            case GamepadButton::DPadRight:     return dPadRight;
            case GamepadButton::BigButton:     return bigButton;
            case GamepadButton::Misc1:         return misc1;
            case GamepadButton::Paddle1:       return paddle1;
            case GamepadButton::Paddle2:       return paddle2;
            case GamepadButton::Paddle3:       return paddle3;
            case GamepadButton::Paddle4:       return paddle4;
            case GamepadButton::TouchPad:      return touchPad;
        }
        return a;
    }

    const std::string& ToString(const AppLifecycleKind kind)
    {
        static const std::string willEnterBackground = "WillEnterBackground";
        static const std::string didEnterForeground = "DidEnterForeground";
        static const std::string lowMemory = "LowMemory";

        switch (kind)
        {
            case AppLifecycleKind::WillEnterBackground: return willEnterBackground;
            case AppLifecycleKind::DidEnterForeground:  return didEnterForeground;
            case AppLifecycleKind::LowMemory:           return lowMemory;
        }
        return willEnterBackground;
    }

} // namespace CNA::Platform
