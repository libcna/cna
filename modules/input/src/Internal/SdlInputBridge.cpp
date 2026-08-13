// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Input/SdlInputBridge.hpp"
#include "CNA/Internal/Input/PlatformInputBridge.hpp"

#include "CNA/Input/InputDevices.hpp"
#include "CNA/Input/Joysticks.hpp"
#include "CNA/Internal/Input/InputManager.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/Input/IPlatformKeyboard.hpp"
#include "CNA/Platform/Input/Scancode.hpp"
#include "Microsoft/Xna/Framework/Input/Mouse.hpp"
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace
{
    using CNA::Platform::KeyCode;
    using CNA::Platform::Scancode;
    using Microsoft::Xna::Framework::Input::ButtonState;
    using Microsoft::Xna::Framework::Input::Keys;
    using Microsoft::Xna::Framework::Input::Touch::TouchLocationState;
    using SharpRuntime::charcs;

    std::optional<bool> g_scancodeModeTestOverride;
    bool g_textInputControlDown[7] = {};
    bool g_textInputSuppress = false;

    bool use_scancode_mode()
    {
        if (g_scancodeModeTestOverride.has_value())
        {
            return *g_scancodeModeTestOverride;
        }
        static const bool enabled = [] {
            const char* value = std::getenv("FNA_KEYBOARD_USE_SCANCODES");
            return value != nullptr && std::string(value) == "1";
        }();
        return enabled;
    }

    CNA::Platform::IPlatformKeyboard* current_keyboard()
    {
        return CNA::Platform::GetCurrentPlatform().GetKeyboard();
    }

    std::optional<Keys> key_for_scancode(const Scancode scancode)
    {
        const auto value = static_cast<std::uint16_t>(scancode);
        if (value >= static_cast<std::uint16_t>(Scancode::A)
            && value <= static_cast<std::uint16_t>(Scancode::Z))
        {
            return static_cast<Keys>(static_cast<int>(Keys::A) + value
                - static_cast<std::uint16_t>(Scancode::A));
        }
        if (value >= static_cast<std::uint16_t>(Scancode::D1)
            && value <= static_cast<std::uint16_t>(Scancode::D9))
        {
            return static_cast<Keys>(static_cast<int>(Keys::D1) + value
                - static_cast<std::uint16_t>(Scancode::D1));
        }
        if (value >= static_cast<std::uint16_t>(Scancode::F1)
            && value <= static_cast<std::uint16_t>(Scancode::F12))
        {
            return static_cast<Keys>(static_cast<int>(Keys::F1) + value
                - static_cast<std::uint16_t>(Scancode::F1));
        }
        if (value >= static_cast<std::uint16_t>(Scancode::F13)
            && value <= static_cast<std::uint16_t>(Scancode::F24))
        {
            return static_cast<Keys>(static_cast<int>(Keys::F13) + value
                - static_cast<std::uint16_t>(Scancode::F13));
        }
        if (value >= static_cast<std::uint16_t>(Scancode::Keypad1)
            && value <= static_cast<std::uint16_t>(Scancode::Keypad9))
        {
            return static_cast<Keys>(static_cast<int>(Keys::NumPad1) + value
                - static_cast<std::uint16_t>(Scancode::Keypad1));
        }

        switch (scancode)
        {
            case Scancode::D0: return Keys::D0;
            case Scancode::Enter: return Keys::Enter;
            case Scancode::Escape: return Keys::Escape;
            case Scancode::Backspace: return Keys::Back;
            case Scancode::Tab: return Keys::Tab;
            case Scancode::Space: return Keys::Space;
            case Scancode::Minus: return Keys::OemMinus;
            case Scancode::Equals: return Keys::OemPlus;
            case Scancode::LeftBracket: return Keys::OemOpenBrackets;
            case Scancode::RightBracket: return Keys::OemCloseBrackets;
            case Scancode::Backslash: return Keys::OemPipe;
            case Scancode::Semicolon: return Keys::OemSemicolon;
            case Scancode::Apostrophe: return Keys::OemQuotes;
            case Scancode::Grave: return Keys::OemTilde;
            case Scancode::Comma: return Keys::OemComma;
            case Scancode::Period: return Keys::OemPeriod;
            case Scancode::Slash: return Keys::OemQuestion;
            case Scancode::CapsLock: return Keys::CapsLock;
            case Scancode::PrintScreen: return Keys::PrintScreen;
            case Scancode::ScrollLock: return Keys::Scroll;
            case Scancode::Pause: return Keys::Pause;
            case Scancode::Insert: return Keys::Insert;
            case Scancode::Home: return Keys::Home;
            case Scancode::PageUp: return Keys::PageUp;
            case Scancode::Delete: return Keys::Delete;
            case Scancode::End: return Keys::End;
            case Scancode::PageDown: return Keys::PageDown;
            case Scancode::Right: return Keys::Right;
            case Scancode::Left: return Keys::Left;
            case Scancode::Down: return Keys::Down;
            case Scancode::Up: return Keys::Up;
            case Scancode::NumLock: return Keys::NumLock;
            case Scancode::KeypadDivide: return Keys::Divide;
            case Scancode::KeypadMultiply: return Keys::Multiply;
            case Scancode::KeypadMinus: return Keys::Subtract;
            case Scancode::KeypadPlus: return Keys::Add;
            case Scancode::KeypadEnter: return Keys::Enter;
            case Scancode::Keypad0: return Keys::NumPad0;
            case Scancode::KeypadPeriod: return Keys::OemPeriod;
            case Scancode::Application:
            case Scancode::Menu: return Keys::Apps;
            case Scancode::VolumeUp: return Keys::VolumeUp;
            case Scancode::VolumeDown: return Keys::VolumeDown;
            case Scancode::KeypadClear: return Keys::OemClear;
            case Scancode::KeypadDecimal: return Keys::Decimal;
            case Scancode::LeftControl: return Keys::LeftControl;
            case Scancode::LeftShift: return Keys::LeftShift;
            case Scancode::LeftAlt: return Keys::LeftAlt;
            case Scancode::LeftGui: return Keys::LeftWindows;
            case Scancode::RightControl: return Keys::RightControl;
            case Scancode::RightShift: return Keys::RightShift;
            case Scancode::RightAlt: return Keys::RightAlt;
            case Scancode::RightGui: return Keys::RightWindows;
            case Scancode::Sleep: return Keys::Sleep;
            case Scancode::Unknown:
            case Scancode::NonUsHash:
            case Scancode::NonUsBackslash: return std::nullopt;
            default: return std::nullopt;
        }
    }

    std::optional<Scancode> scancode_for_key(const Keys key)
    {
        const auto value = static_cast<int>(key);
        if (value >= static_cast<int>(Keys::A) && value <= static_cast<int>(Keys::Z))
        {
            return static_cast<Scancode>(static_cast<std::uint16_t>(Scancode::A)
                + value - static_cast<int>(Keys::A));
        }
        if (value >= static_cast<int>(Keys::D1) && value <= static_cast<int>(Keys::D9))
        {
            return static_cast<Scancode>(static_cast<std::uint16_t>(Scancode::D1)
                + value - static_cast<int>(Keys::D1));
        }
        if (value >= static_cast<int>(Keys::F1) && value <= static_cast<int>(Keys::F12))
        {
            return static_cast<Scancode>(static_cast<std::uint16_t>(Scancode::F1)
                + value - static_cast<int>(Keys::F1));
        }
        if (value >= static_cast<int>(Keys::F13) && value <= static_cast<int>(Keys::F24))
        {
            return static_cast<Scancode>(static_cast<std::uint16_t>(Scancode::F13)
                + value - static_cast<int>(Keys::F13));
        }
        if (value >= static_cast<int>(Keys::NumPad1) && value <= static_cast<int>(Keys::NumPad9))
        {
            return static_cast<Scancode>(static_cast<std::uint16_t>(Scancode::Keypad1)
                + value - static_cast<int>(Keys::NumPad1));
        }

        switch (key)
        {
            case Keys::D0: return Scancode::D0;
            case Keys::Enter: return Scancode::Enter;
            case Keys::Escape: return Scancode::Escape;
            case Keys::Back: return Scancode::Backspace;
            case Keys::Tab: return Scancode::Tab;
            case Keys::Space: return Scancode::Space;
            case Keys::OemMinus: return Scancode::Minus;
            case Keys::OemPlus: return Scancode::Equals;
            case Keys::OemOpenBrackets: return Scancode::LeftBracket;
            case Keys::OemCloseBrackets: return Scancode::RightBracket;
            case Keys::OemPipe: return Scancode::Backslash;
            case Keys::OemSemicolon: return Scancode::Semicolon;
            case Keys::OemQuotes: return Scancode::Apostrophe;
            case Keys::OemTilde: return Scancode::Grave;
            case Keys::OemComma: return Scancode::Comma;
            case Keys::OemPeriod: return Scancode::Period;
            case Keys::OemQuestion: return Scancode::Slash;
            case Keys::CapsLock: return Scancode::CapsLock;
            case Keys::PrintScreen: return Scancode::PrintScreen;
            case Keys::Scroll: return Scancode::ScrollLock;
            case Keys::Pause: return Scancode::Pause;
            case Keys::Insert: return Scancode::Insert;
            case Keys::Home: return Scancode::Home;
            case Keys::PageUp: return Scancode::PageUp;
            case Keys::Delete: return Scancode::Delete;
            case Keys::End: return Scancode::End;
            case Keys::PageDown: return Scancode::PageDown;
            case Keys::Right: return Scancode::Right;
            case Keys::Left: return Scancode::Left;
            case Keys::Down: return Scancode::Down;
            case Keys::Up: return Scancode::Up;
            case Keys::NumLock: return Scancode::NumLock;
            case Keys::Divide: return Scancode::KeypadDivide;
            case Keys::Multiply: return Scancode::KeypadMultiply;
            case Keys::Subtract: return Scancode::KeypadMinus;
            case Keys::Add: return Scancode::KeypadPlus;
            case Keys::NumPad0: return Scancode::Keypad0;
            case Keys::OemClear: return Scancode::KeypadClear;
            case Keys::Decimal: return Scancode::KeypadDecimal;
            case Keys::Apps: return Scancode::Application;
            case Keys::VolumeUp: return Scancode::VolumeUp;
            case Keys::VolumeDown: return Scancode::VolumeDown;
            case Keys::LeftControl: return Scancode::LeftControl;
            case Keys::LeftShift: return Scancode::LeftShift;
            case Keys::LeftAlt: return Scancode::LeftAlt;
            case Keys::LeftWindows: return Scancode::LeftGui;
            case Keys::RightControl: return Scancode::RightControl;
            case Keys::RightShift: return Scancode::RightShift;
            case Keys::RightAlt: return Scancode::RightAlt;
            case Keys::RightWindows: return Scancode::RightGui;
            case Keys::Sleep: return Scancode::Sleep;
            default: return std::nullopt;
        }
    }

    template <typename Emit>
    void decode_utf8_to_utf16(const char* text, Emit&& emit)
    {
        const auto* bytes = reinterpret_cast<const unsigned char*>(text);
        while (*bytes != 0)
        {
            const unsigned char first = bytes[0];
            std::uint32_t codePoint = 0;
            std::uint32_t minimum = 0;
            int length = 0;
            if (first < 0x80) { codePoint = first; length = 1; }
            else if ((first & 0xE0) == 0xC0) { codePoint = first & 0x1F; length = 2; minimum = 0x80; }
            else if ((first & 0xF0) == 0xE0) { codePoint = first & 0x0F; length = 3; minimum = 0x800; }
            else if ((first & 0xF8) == 0xF0) { codePoint = first & 0x07; length = 4; minimum = 0x10000; }
            else { emit(static_cast<charcs>(0xFFFD)); ++bytes; continue; }

            int index = 1;
            for (; index < length && (bytes[index] & 0xC0) == 0x80; ++index)
            {
                codePoint = (codePoint << 6) | (bytes[index] & 0x3F);
            }
            if (index != length)
            {
                emit(static_cast<charcs>(0xFFFD));
                bytes += index;
                continue;
            }
            bytes += length;
            if (codePoint < minimum || (codePoint >= 0xD800 && codePoint <= 0xDFFF)
                || codePoint > 0x10FFFF)
            {
                emit(static_cast<charcs>(0xFFFD));
            }
            else if (codePoint <= 0xFFFF)
            {
                emit(static_cast<charcs>(codePoint));
            }
            else
            {
                codePoint -= 0x10000;
                emit(static_cast<charcs>(0xD800 + (codePoint >> 10)));
                emit(static_cast<charcs>(0xDC00 + (codePoint & 0x3FF)));
            }
        }
    }

    std::optional<int> text_input_binding_index(const Keys key)
    {
        switch (key)
        {
            case Keys::Home: return 0;
            case Keys::End: return 1;
            case Keys::Back: return 2;
            case Keys::Tab: return 3;
            case Keys::Enter: return 4;
            case Keys::Delete: return 5;
            default: return std::nullopt;
        }
    }

    bool control_key_held()
    {
        const auto keyboard = CNA::Internal::Input::InputManager::GetKeyboardState();
        return keyboard.IsKeyDown(Keys::LeftControl) || keyboard.IsKeyDown(Keys::RightControl);
    }

    void handle_text_input_key_down(const Keys key, const bool repeat)
    {
        constexpr char controls[7] = {2, 3, 8, 9, 13, 127, 22};
        using Microsoft::Xna::Framework::Input::TextInputEXT;
        if (const auto index = text_input_binding_index(key))
        {
            if (!repeat) g_textInputControlDown[*index] = true;
            TextInputEXT::INTERNAL_OnTextInput(static_cast<charcs>(controls[*index]));
        }
        else if (control_key_held() && key == Keys::V)
        {
            if (!repeat)
            {
                g_textInputControlDown[6] = true;
                g_textInputSuppress = true;
            }
            TextInputEXT::INTERNAL_OnTextInput(static_cast<charcs>(controls[6]));
        }
    }

    void handle_text_input_key_up(const Keys key)
    {
        if (const auto index = text_input_binding_index(key))
        {
            g_textInputControlDown[*index] = false;
        }
        else if ((!control_key_held() && g_textInputControlDown[6]) || key == Keys::V)
        {
            g_textInputControlDown[6] = false;
            g_textInputSuppress = false;
        }
    }

    std::unordered_set<CNA::Platform::DeviceId>& announced_joysticks()
    {
        static std::unordered_set<CNA::Platform::DeviceId> value;
        return value;
    }

    std::unordered_map<std::uint64_t, int>& touch_ids()
    {
        static std::unordered_map<std::uint64_t, int> value;
        return value;
    }

    int& next_touch_id()
    {
        static int value = 1;
        return value;
    }

    int get_or_create_touch_id(const std::uint64_t finger)
    {
        const auto found = touch_ids().find(finger);
        if (found != touch_ids().end()) return found->second;
        const int id = next_touch_id()++;
        touch_ids()[finger] = id;
        return id;
    }

    std::optional<int> find_touch_id(const std::uint64_t finger)
    {
        const auto found = touch_ids().find(finger);
        return found != touch_ids().end() ? std::optional<int>(found->second) : std::nullopt;
    }

    Microsoft::Xna::Framework::Vector2 to_logical_position(
        const CNA::Platform::WindowId window, const float x, const float y)
    {
        if (auto* renderer = CNA::Internal::Renderers::IGraphicsRenderer::GetForWindow(window))
        {
            float logicalX = x;
            float logicalY = y;
            if (renderer->TransformWindowToLogical(x, y, logicalX, logicalY))
            {
                return {logicalX, logicalY};
            }
        }
        return {x, y};
    }

    Microsoft::Xna::Framework::Vector2 to_touch_position(
        const CNA::Platform::TouchEvent& event)
    {
        return to_logical_position(
            event.window,
            event.x * static_cast<float>(std::max(event.clientWidth, 1)),
            event.y * static_cast<float>(std::max(event.clientHeight, 1)));
    }
}

namespace CNA::Internal::Input
{
    Keys SdlInputBridge::GetKeyFromScancode(const Keys key)
    {
        if (use_scancode_mode()) return key;
        const auto scancode = scancode_for_key(key);
        if (!scancode.has_value()) return Keys::None;
        if (CNA::Platform::IPlatformKeyboard* keyboard = current_keyboard())
        {
            const KeyCode resolved = keyboard->GetKeyFromScancode(*scancode);
            if (resolved != KeyCode::None) return static_cast<Keys>(resolved);
        }
        return key_for_scancode(*scancode).value_or(Keys::None);
    }

    std::string SdlInputBridge::GetScancodeName(const Keys key)
    {
        const auto scancode = scancode_for_key(key);
        if (!scancode.has_value()) return {};
        if (CNA::Platform::IPlatformKeyboard* keyboard = current_keyboard())
        {
            const std::string name = keyboard->GetScancodeName(*scancode);
            if (!name.empty()) return name;
        }
        return CNA::Platform::ToString(*scancode);
    }

    Keys SdlInputBridge::GetScancodeFromName(const std::string& name)
    {
        Scancode scancode = Scancode::Unknown;
        if (CNA::Platform::IPlatformKeyboard* keyboard = current_keyboard())
        {
            scancode = keyboard->GetScancodeFromName(name);
        }
        if (scancode == Scancode::Unknown)
        {
            scancode = CNA::Platform::ScancodeFromString(name);
        }
        return key_for_scancode(scancode).value_or(Keys::None);
    }

    std::string SdlInputBridge::GetKeyName(const Keys key)
    {
        const auto scancode = scancode_for_key(key);
        if (!scancode.has_value()) return {};
        if (CNA::Platform::IPlatformKeyboard* keyboard = current_keyboard())
        {
            const std::string name = keyboard->GetKeyName(*scancode);
            if (!name.empty()) return name;
        }
        return CNA::Platform::ToString(*scancode);
    }

    Keys SdlInputBridge::GetKeyFromName(const std::string& name)
    {
        if (CNA::Platform::IPlatformKeyboard* keyboard = current_keyboard())
        {
            const KeyCode resolved = keyboard->GetKeyFromName(name);
            if (resolved != KeyCode::None) return static_cast<Keys>(resolved);
        }
        return key_for_scancode(CNA::Platform::ScancodeFromString(name)).value_or(Keys::None);
    }

    void SdlInputBridge::SetScancodeModeForTests(const bool enabled)
    {
        g_scancodeModeTestOverride = enabled;
    }

    void SdlInputBridge::ClearScancodeModeForTests()
    {
        g_scancodeModeTestOverride.reset();
    }

    void SdlInputBridge::ResetForTests()
    {
        g_textInputSuppress = false;
        for (bool& down : g_textInputControlDown) down = false;
        touch_ids().clear();
        next_touch_id() = 1;
        g_scancodeModeTestOverride.reset();
        announced_joysticks().clear();
    }

    void PlatformInputBridge::ProcessEvent(const CNA::Platform::PlatformEvent& event)
    {
        using namespace CNA::Platform;
        std::visit([](const auto& value)
        {
            using Event = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Event, MouseMotionEvent>)
            {
                const auto position = to_logical_position(value.window, value.x, value.y);
                InputManager::SetMousePosition(static_cast<int>(position.X), static_cast<int>(position.Y));
                InputManager::AddMouseRelativeDelta(value.deltaX, value.deltaY);
            }
            else if constexpr (std::is_same_v<Event, MouseButtonEvent>)
            {
                const ButtonState state = value.pressed ? ButtonState::Pressed : ButtonState::Released;
                switch (value.button)
                {
                    case 1: InputManager::SetMouseButtonState(MouseButton::Left, state); break;
                    case 2: InputManager::SetMouseButtonState(MouseButton::Middle, state); break;
                    case 3: InputManager::SetMouseButtonState(MouseButton::Right, state); break;
                    case 4: InputManager::SetMouseButtonState(MouseButton::XButton1, state); break;
                    case 5: InputManager::SetMouseButtonState(MouseButton::XButton2, state); break;
                    default: break;
                }
                const auto position = to_logical_position(value.window, value.x, value.y);
                InputManager::SetMousePosition(static_cast<int>(position.X), static_cast<int>(position.Y));
                if (value.pressed && value.button >= 1)
                {
                    Microsoft::Xna::Framework::Input::Mouse::INTERNAL_onClicked(value.button - 1);
                }
            }
            else if constexpr (std::is_same_v<Event, MouseWheelEvent>)
            {
                InputManager::AddScrollWheelDelta(static_cast<int>(value.y) * 120);
                InputManager::AddHorizontalScrollWheelDelta(static_cast<int>(value.x) * 120);
            }
            else if constexpr (std::is_same_v<Event, DeviceEvent>)
            {
                if (value.kind == InputDeviceKind::Mouse)
                {
                    if (value.connected) CNA::Input::InputDevices::MouseConnectedEXT.Invoke(value.device);
                    else CNA::Input::InputDevices::MouseDisconnectedEXT.Invoke(value.device);
                }
                else if (value.kind == InputDeviceKind::Keyboard)
                {
                    if (value.connected) CNA::Input::InputDevices::KeyboardConnectedEXT.Invoke(value.device);
                    else CNA::Input::InputDevices::KeyboardDisconnectedEXT.Invoke(value.device);
                }
                else if (value.kind == InputDeviceKind::Joystick
                    && value.device <= std::numeric_limits<std::uint32_t>::max())
                {
                    if (value.connected)
                    {
                        IPlatformJoystick* joysticks = GetCurrentPlatform().GetJoystick();
                        if (joysticks != nullptr && joysticks->IsConnected(value.device)
                            && announced_joysticks().insert(value.device).second)
                        {
                            CNA::Input::Joysticks::ConnectedEXT.Invoke(
                                static_cast<std::uint32_t>(value.device));
                        }
                    }
                    else if (announced_joysticks().erase(value.device) != 0)
                    {
                        CNA::Input::Joysticks::DisconnectedEXT.Invoke(
                            static_cast<std::uint32_t>(value.device));
                    }
                }
            }
            else if constexpr (std::is_same_v<Event, KeyEvent>)
            {
                const std::optional<Keys> key = use_scancode_mode()
                    ? key_for_scancode(value.scancode)
                    : std::optional<Keys>(static_cast<Keys>(value.keycode));
                if (!key.has_value() || *key == Keys::None) return;
                const bool repeat = value.pressed && value.repeat;
                if (!repeat) InputManager::SetKeyState(*key, value.pressed);
                if (value.pressed) handle_text_input_key_down(*key, repeat);
                else handle_text_input_key_up(*key);
            }
            else if constexpr (std::is_same_v<Event, TextInputEvent>)
            {
                if (!g_textInputSuppress)
                {
                    decode_utf8_to_utf16(value.text.c_str(), [](const charcs unit) {
                        Microsoft::Xna::Framework::Input::TextInputEXT::INTERNAL_OnTextInput(unit);
                    });
                }
            }
            else if constexpr (std::is_same_v<Event, TextEditingEvent>)
            {
                Microsoft::Xna::Framework::Input::TextInputEXT::INTERNAL_OnTextEditing(
                    value.text, value.text.empty() ? 0 : value.cursor,
                    value.text.empty() ? 0 : value.selectionLength);
            }
            else if constexpr (std::is_same_v<Event, TextEditingCandidatesEvent>)
            {
                Microsoft::Xna::Framework::Input::TextInputEXT::INTERNAL_OnTextEditingCandidates(
                    value.candidates, value.selectedCandidate, value.horizontal);
            }
            else if constexpr (std::is_same_v<Event, TouchEvent>)
            {
                const bool released = value.kind == TouchEventKind::Up
                    || value.kind == TouchEventKind::Cancelled;
                const TouchLocationState state = value.kind == TouchEventKind::Down
                    ? TouchLocationState::Pressed
                    : released ? TouchLocationState::Released : TouchLocationState::Moved;
                if (value.kind == TouchEventKind::Down)
                {
                    Microsoft::Xna::Framework::Input::Touch::TouchPanel::setTouchDeviceExistsProperty(true);
                }
                const int id = released
                    ? find_touch_id(value.fingerId).value_or(get_or_create_touch_id(value.fingerId))
                    : get_or_create_touch_id(value.fingerId);
                Microsoft::Xna::Framework::Input::Touch::TouchPanel::INTERNAL_setTouchState(
                    id, state, to_touch_position(value), value.pressure);
                Microsoft::Xna::Framework::Input::Touch::TouchPanel::INTERNAL_onTouchEvent(
                    id, state, value.x, value.y,
                    value.kind == TouchEventKind::Motion ? value.deltaX : 0.0f,
                    value.kind == TouchEventKind::Motion ? value.deltaY : 0.0f);
                if (released) touch_ids().erase(value.fingerId);
            }
        }, event);
    }
}
