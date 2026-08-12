// SPDX-License-Identifier: MS-PL

#include "TerminalKeyboard.hpp"

#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <utility>

namespace CNA::Platform::Terminal {

    namespace {

        bool ParseNumber(const std::string_view text, std::uint32_t& value)
        {
            if (text.empty())
            {
                return false;
            }
            std::uint32_t parsed = 0;
            const auto [end, error] =
                std::from_chars(text.data(), text.data() + text.size(), parsed);
            if (error != std::errc{} || end != text.data() + text.size())
            {
                return false;
            }
            value = parsed;
            return true;
        }

        std::string_view FieldBefore(const std::string_view text, const char separator)
        {
            const std::size_t at = text.find(separator);
            return at == std::string_view::npos ? text : text.substr(0, at);
        }

        std::string_view FieldAfter(const std::string_view text, const char separator)
        {
            const std::size_t at = text.find(separator);
            return at == std::string_view::npos ? std::string_view{} : text.substr(at + 1);
        }

        KeyCode AsciiKeyCode(const std::uint32_t codepoint)
        {
            if (codepoint >= 'a' && codepoint <= 'z')
            {
                return static_cast<KeyCode>(static_cast<std::uint16_t>(KeyCode::A) +
                                            static_cast<std::uint16_t>(codepoint - 'a'));
            }
            if (codepoint >= 'A' && codepoint <= 'Z')
            {
                return static_cast<KeyCode>(static_cast<std::uint16_t>(KeyCode::A) +
                                            static_cast<std::uint16_t>(codepoint - 'A'));
            }
            if (codepoint >= '0' && codepoint <= '9')
            {
                return static_cast<KeyCode>(static_cast<std::uint16_t>(KeyCode::D0) +
                                            static_cast<std::uint16_t>(codepoint - '0'));
            }

            switch (codepoint)
            {
                case 8:
                case 127: return KeyCode::Back;
                case 9:   return KeyCode::Tab;
                case 13:  return KeyCode::Enter;
                case 27:  return KeyCode::Escape;
                case ' ': return KeyCode::Space;
                case ';': return KeyCode::OemSemicolon;
                case '=': return KeyCode::OemPlus;
                case ',': return KeyCode::OemComma;
                case '-': return KeyCode::OemMinus;
                case '.': return KeyCode::OemPeriod;
                case '/': return KeyCode::OemQuestion;
                case '`': return KeyCode::OemTilde;
                case '[': return KeyCode::OemOpenBrackets;
                case '\\': return KeyCode::OemPipe;
                case ']': return KeyCode::OemCloseBrackets;
                case '\'': return KeyCode::OemQuotes;
                default: return KeyCode::None;
            }
        }

        KeyCode FunctionalKeyCode(const std::uint32_t codepoint)
        {
            // Kitty assigns this contiguous private-use block to the traditional editing and
            // function keys. Keeping the arithmetic visible makes the wire table auditable.
            switch (codepoint)
            {
                case 57348: return KeyCode::Insert;
                case 57349: return KeyCode::Delete;
                case 57350: return KeyCode::Left;
                case 57351: return KeyCode::Right;
                case 57352: return KeyCode::Up;
                case 57353: return KeyCode::Down;
                case 57354: return KeyCode::PageUp;
                case 57355: return KeyCode::PageDown;
                case 57356: return KeyCode::Home;
                case 57357: return KeyCode::End;
                case 57358: return KeyCode::CapsLock;
                case 57359: return KeyCode::Scroll;
                case 57360: return KeyCode::NumLock;
                case 57361: return KeyCode::PrintScreen;
                case 57362: return KeyCode::Pause;
                case 57363: return KeyCode::Apps;
                case 57399: return KeyCode::NumPad0;
                case 57400: return KeyCode::NumPad1;
                case 57401: return KeyCode::NumPad2;
                case 57402: return KeyCode::NumPad3;
                case 57403: return KeyCode::NumPad4;
                case 57404: return KeyCode::NumPad5;
                case 57405: return KeyCode::NumPad6;
                case 57406: return KeyCode::NumPad7;
                case 57407: return KeyCode::NumPad8;
                case 57408: return KeyCode::NumPad9;
                case 57409: return KeyCode::Decimal;
                case 57410: return KeyCode::Divide;
                case 57411: return KeyCode::Multiply;
                case 57412: return KeyCode::Subtract;
                case 57413: return KeyCode::Add;
                case 57414: return KeyCode::Enter;
                case 57416: return KeyCode::Separator;
                case 57430: return KeyCode::MediaPlayPause;
                case 57432: return KeyCode::MediaStop;
                case 57435: return KeyCode::MediaNextTrack;
                case 57436: return KeyCode::MediaPreviousTrack;
                case 57438: return KeyCode::VolumeDown;
                case 57439: return KeyCode::VolumeUp;
                case 57440: return KeyCode::VolumeMute;
                case 57441: return KeyCode::LeftShift;
                case 57442: return KeyCode::LeftControl;
                case 57443: return KeyCode::LeftAlt;
                case 57444: return KeyCode::LeftWindows;
                case 57447: return KeyCode::RightShift;
                case 57448: return KeyCode::RightControl;
                case 57449: return KeyCode::RightAlt;
                case 57450: return KeyCode::RightWindows;
                default: break;
            }

            if (codepoint >= 57364 && codepoint <= 57387)
            {
                return static_cast<KeyCode>(static_cast<std::uint16_t>(KeyCode::F1) +
                                            static_cast<std::uint16_t>(codepoint - 57364));
            }
            return KeyCode::None;
        }

        KeyCode ToKeyCode(const std::uint32_t codepoint)
        {
            const KeyCode ascii = AsciiKeyCode(codepoint);
            return ascii != KeyCode::None ? ascii : FunctionalKeyCode(codepoint);
        }

        Scancode ToScancode(const KeyCode key)
        {
            const auto value = static_cast<std::uint16_t>(key);
            if (key >= KeyCode::A && key <= KeyCode::Z)
            {
                return static_cast<Scancode>(static_cast<std::uint16_t>(Scancode::A) +
                                             value - static_cast<std::uint16_t>(KeyCode::A));
            }
            if (key >= KeyCode::D1 && key <= KeyCode::D9)
            {
                return static_cast<Scancode>(static_cast<std::uint16_t>(Scancode::D1) +
                                             value - static_cast<std::uint16_t>(KeyCode::D1));
            }
            switch (key)
            {
                case KeyCode::D0: return Scancode::D0;
                case KeyCode::NumPad0: return Scancode::Keypad0;
                case KeyCode::NumPad1: return Scancode::Keypad1;
                case KeyCode::NumPad2: return Scancode::Keypad2;
                case KeyCode::NumPad3: return Scancode::Keypad3;
                case KeyCode::NumPad4: return Scancode::Keypad4;
                case KeyCode::NumPad5: return Scancode::Keypad5;
                case KeyCode::NumPad6: return Scancode::Keypad6;
                case KeyCode::NumPad7: return Scancode::Keypad7;
                case KeyCode::NumPad8: return Scancode::Keypad8;
                case KeyCode::NumPad9: return Scancode::Keypad9;
                case KeyCode::Decimal: return Scancode::KeypadPeriod;
                case KeyCode::Divide: return Scancode::KeypadDivide;
                case KeyCode::Multiply: return Scancode::KeypadMultiply;
                case KeyCode::Subtract: return Scancode::KeypadMinus;
                case KeyCode::Add: return Scancode::KeypadPlus;
                case KeyCode::Separator: return Scancode::KeypadDecimal;
                case KeyCode::Back: return Scancode::Backspace;
                case KeyCode::Tab: return Scancode::Tab;
                case KeyCode::Enter: return Scancode::Enter;
                case KeyCode::Escape: return Scancode::Escape;
                case KeyCode::Space: return Scancode::Space;
                case KeyCode::Left: return Scancode::Left;
                case KeyCode::Right: return Scancode::Right;
                case KeyCode::Up: return Scancode::Up;
                case KeyCode::Down: return Scancode::Down;
                case KeyCode::PageUp: return Scancode::PageUp;
                case KeyCode::PageDown: return Scancode::PageDown;
                case KeyCode::Home: return Scancode::Home;
                case KeyCode::End: return Scancode::End;
                case KeyCode::Insert: return Scancode::Insert;
                case KeyCode::Delete: return Scancode::Delete;
                case KeyCode::CapsLock: return Scancode::CapsLock;
                case KeyCode::Scroll: return Scancode::ScrollLock;
                case KeyCode::NumLock: return Scancode::NumLock;
                case KeyCode::PrintScreen: return Scancode::PrintScreen;
                case KeyCode::Pause: return Scancode::Pause;
                case KeyCode::Apps: return Scancode::Application;
                case KeyCode::LeftShift: return Scancode::LeftShift;
                case KeyCode::RightShift: return Scancode::RightShift;
                case KeyCode::LeftControl: return Scancode::LeftControl;
                case KeyCode::RightControl: return Scancode::RightControl;
                case KeyCode::LeftAlt: return Scancode::LeftAlt;
                case KeyCode::RightAlt: return Scancode::RightAlt;
                case KeyCode::LeftWindows: return Scancode::LeftGui;
                case KeyCode::RightWindows: return Scancode::RightGui;
                case KeyCode::OemSemicolon: return Scancode::Semicolon;
                case KeyCode::OemPlus: return Scancode::Equals;
                case KeyCode::OemComma: return Scancode::Comma;
                case KeyCode::OemMinus: return Scancode::Minus;
                case KeyCode::OemPeriod: return Scancode::Period;
                case KeyCode::OemQuestion: return Scancode::Slash;
                case KeyCode::OemTilde: return Scancode::Grave;
                case KeyCode::OemOpenBrackets: return Scancode::LeftBracket;
                case KeyCode::OemPipe: return Scancode::Backslash;
                case KeyCode::OemCloseBrackets: return Scancode::RightBracket;
                case KeyCode::OemQuotes: return Scancode::Apostrophe;
                default: break;
            }
            if (key >= KeyCode::F1 && key <= KeyCode::F12)
            {
                return static_cast<Scancode>(static_cast<std::uint16_t>(Scancode::F1) +
                                             value - static_cast<std::uint16_t>(KeyCode::F1));
            }
            if (key >= KeyCode::F13 && key <= KeyCode::F24)
            {
                return static_cast<Scancode>(static_cast<std::uint16_t>(Scancode::F13) +
                                             value - static_cast<std::uint16_t>(KeyCode::F13));
            }
            return Scancode::Unknown;
        }

        std::uint16_t ToModifiers(const std::uint32_t encoded)
        {
            // Kitty adds one so the default/no-modifier parameter is never zero.
            const std::uint32_t bits = encoded > 0 ? encoded - 1 : 0;
            std::uint16_t result = 0;
            if ((bits & 1u) != 0)   { result |= static_cast<std::uint16_t>(KeyModifier::Shift); }
            if ((bits & 2u) != 0)   { result |= static_cast<std::uint16_t>(KeyModifier::Alt); }
            if ((bits & 4u) != 0)   { result |= static_cast<std::uint16_t>(KeyModifier::Control); }
            if ((bits & 8u) != 0 || (bits & 16u) != 0 || (bits & 32u) != 0)
            {
                result |= static_cast<std::uint16_t>(KeyModifier::Gui);
            }
            if ((bits & 64u) != 0)  { result |= static_cast<std::uint16_t>(KeyModifier::CapsLock); }
            if ((bits & 128u) != 0) { result |= static_cast<std::uint16_t>(KeyModifier::NumLock); }
            return result;
        }

        void SetLegacyKey(KeyEvent& event, const KeyCode key)
        {
            event.keycode = key;
            event.scancode = ToScancode(key);
            event.pressed = true;
        }

        bool DecodeLegacyByte(const unsigned char byte, KeyEvent& event)
        {
            KeyEvent decoded;

            if (byte >= 1 && byte <= 26 && byte != '\b' && byte != '\t' && byte != '\r')
            {
                const KeyCode key = static_cast<KeyCode>(
                    static_cast<std::uint16_t>(KeyCode::A) + byte - 1);
                SetLegacyKey(decoded, key);
                decoded.modifiers = static_cast<std::uint16_t>(KeyModifier::Control);
                event = decoded;
                return true;
            }

            switch (byte)
            {
                case 0:
                    SetLegacyKey(decoded, KeyCode::Space);
                    decoded.modifiers = static_cast<std::uint16_t>(KeyModifier::Control);
                    break;
                case 8:
                case 127: SetLegacyKey(decoded, KeyCode::Back); break;
                case 9: SetLegacyKey(decoded, KeyCode::Tab); break;
                case 13: SetLegacyKey(decoded, KeyCode::Enter); break;
                case 27: SetLegacyKey(decoded, KeyCode::Escape); break;
                case 28:
                    SetLegacyKey(decoded, KeyCode::OemPipe);
                    decoded.modifiers = static_cast<std::uint16_t>(KeyModifier::Control);
                    break;
                case 29:
                    SetLegacyKey(decoded, KeyCode::OemCloseBrackets);
                    decoded.modifiers = static_cast<std::uint16_t>(KeyModifier::Control);
                    break;
                case 30:
                    SetLegacyKey(decoded, KeyCode::D6);
                    decoded.modifiers = static_cast<std::uint16_t>(KeyModifier::Control);
                    break;
                case 31:
                    SetLegacyKey(decoded, KeyCode::OemMinus);
                    decoded.modifiers = static_cast<std::uint16_t>(KeyModifier::Control);
                    break;
                default: break;
            }
            if (decoded.keycode != KeyCode::None)
            {
                event = decoded;
                return true;
            }

            if (byte >= 'a' && byte <= 'z')
            {
                SetLegacyKey(decoded, AsciiKeyCode(byte));
            }
            else if (byte >= 'A' && byte <= 'Z')
            {
                SetLegacyKey(decoded, AsciiKeyCode(byte));
                decoded.modifiers = static_cast<std::uint16_t>(KeyModifier::Shift);
            }
            else if (byte >= '0' && byte <= '9')
            {
                SetLegacyKey(decoded, AsciiKeyCode(byte));
            }
            else
            {
                KeyCode key = AsciiKeyCode(byte);
                bool shifted = false;
                switch (byte)
                {
                    case '!': key = KeyCode::D1; shifted = true; break;
                    case '@': key = KeyCode::D2; shifted = true; break;
                    case '#': key = KeyCode::D3; shifted = true; break;
                    case '$': key = KeyCode::D4; shifted = true; break;
                    case '%': key = KeyCode::D5; shifted = true; break;
                    case '^': key = KeyCode::D6; shifted = true; break;
                    case '&': key = KeyCode::D7; shifted = true; break;
                    case '*': key = KeyCode::D8; shifted = true; break;
                    case '(': key = KeyCode::D9; shifted = true; break;
                    case ')': key = KeyCode::D0; shifted = true; break;
                    case '_': key = KeyCode::OemMinus; shifted = true; break;
                    case '+': key = KeyCode::OemPlus; shifted = true; break;
                    case '{': key = KeyCode::OemOpenBrackets; shifted = true; break;
                    case '}': key = KeyCode::OemCloseBrackets; shifted = true; break;
                    case '|': key = KeyCode::OemPipe; shifted = true; break;
                    case ':': key = KeyCode::OemSemicolon; shifted = true; break;
                    case '"': key = KeyCode::OemQuotes; shifted = true; break;
                    case '~': key = KeyCode::OemTilde; shifted = true; break;
                    case '<': key = KeyCode::OemComma; shifted = true; break;
                    case '>': key = KeyCode::OemPeriod; shifted = true; break;
                    case '?': key = KeyCode::OemQuestion; shifted = true; break;
                    default: break;
                }
                if (key != KeyCode::None)
                {
                    SetLegacyKey(decoded, key);
                    if (shifted)
                    {
                        decoded.modifiers = static_cast<std::uint16_t>(KeyModifier::Shift);
                    }
                }
            }

            if (decoded.keycode == KeyCode::None)
            {
                return false;
            }
            event = decoded;
            return true;
        }

        KeyCode TildeKey(const std::uint32_t parameter)
        {
            switch (parameter)
            {
                case 1:
                case 7: return KeyCode::Home;
                case 2: return KeyCode::Insert;
                case 3: return KeyCode::Delete;
                case 4:
                case 8: return KeyCode::End;
                case 5: return KeyCode::PageUp;
                case 6: return KeyCode::PageDown;
                case 11: return KeyCode::F1;
                case 12: return KeyCode::F2;
                case 13: return KeyCode::F3;
                case 14: return KeyCode::F4;
                case 15: return KeyCode::F5;
                case 17: return KeyCode::F6;
                case 18: return KeyCode::F7;
                case 19: return KeyCode::F8;
                case 20: return KeyCode::F9;
                case 21: return KeyCode::F10;
                case 23: return KeyCode::F11;
                case 24: return KeyCode::F12;
                case 25: return KeyCode::F13;
                case 26: return KeyCode::F14;
                case 28: return KeyCode::F15;
                case 29: return KeyCode::F16;
                case 31: return KeyCode::F17;
                case 32: return KeyCode::F18;
                case 33: return KeyCode::F19;
                case 34: return KeyCode::F20;
                default: return KeyCode::None;
            }
        }

        bool ParseLegacyParameters(const std::string_view body, std::uint32_t& first,
                                   std::uint32_t& modifiers)
        {
            first = 1;
            modifiers = 1;
            if (body.empty())
            {
                return true;
            }
            if (!ParseNumber(FieldBefore(body, ';'), first))
            {
                return false;
            }
            const std::string_view afterFirst = FieldAfter(body, ';');
            return afterFirst.empty() || ParseNumber(FieldBefore(afterFirst, ';'), modifiers);
        }

        bool IsHeld(const std::set<KeyCode>& pressed, const KeyCode left, const KeyCode right)
        {
            return pressed.contains(left) || pressed.contains(right);
        }

    } // namespace

    bool DecodeKittyKeyEvent(const std::string_view sequence, KeyEvent& event)
    {
        if (sequence.size() < 4 || sequence[0] != '\x1b' || sequence[1] != '[' ||
            sequence.back() != 'u')
        {
            return false;
        }

        const std::string_view body = sequence.substr(2, sequence.size() - 3);
        const std::string_view keys = FieldBefore(body, ';');
        const std::string_view remainder = FieldAfter(body, ';');
        const std::string_view modifiersAndType = FieldBefore(remainder, ';');

        std::uint32_t primary = 0;
        if (!ParseNumber(FieldBefore(keys, ':'), primary))
        {
            return false;
        }

        std::uint32_t baseLayout = primary;
        const std::string_view afterPrimary = FieldAfter(keys, ':');
        const std::string_view afterShifted = FieldAfter(afterPrimary, ':');
        if (!afterShifted.empty() && !ParseNumber(FieldBefore(afterShifted, ':'), baseLayout))
        {
            return false;
        }

        std::uint32_t encodedModifiers = 1;
        std::uint32_t eventType = 1;
        if (!modifiersAndType.empty())
        {
            if (!ParseNumber(FieldBefore(modifiersAndType, ':'), encodedModifiers))
            {
                return false;
            }
            const std::string_view type = FieldAfter(modifiersAndType, ':');
            if (!type.empty() && !ParseNumber(type, eventType))
            {
                return false;
            }
        }
        if (eventType < 1 || eventType > 3)
        {
            return false;
        }

        KeyEvent decoded;
        decoded.keycode = ToKeyCode(primary);
        decoded.scancode = ToScancode(ToKeyCode(baseLayout));
        // Kitty has a distinct PUA value for keypad Enter but KeyCode intentionally follows the
        // Windows virtual-key vocabulary, where both Enter keys are VK_RETURN. Preserve the
        // physical distinction in the scancode half of the contract.
        if (baseLayout == 57414)
        {
            decoded.scancode = Scancode::KeypadEnter;
        }
        decoded.modifiers = ToModifiers(encodedModifiers);
        decoded.pressed = eventType != 3;
        decoded.repeat = eventType == 2;
        event = decoded;
        return true;
    }

    bool DecodeLegacyKeyEvent(const std::string_view sequence, KeyEvent& event)
    {
        if (sequence.empty())
        {
            return false;
        }
        if (sequence.size() == 1)
        {
            return DecodeLegacyByte(static_cast<unsigned char>(sequence.front()), event);
        }

        if (sequence.front() != '\x1b')
        {
            return false;
        }
        if (sequence.size() == 2 && sequence[1] != '[' && sequence[1] != 'O')
        {
            KeyEvent decoded;
            if (!DecodeLegacyByte(static_cast<unsigned char>(sequence[1]), decoded))
            {
                return false;
            }
            decoded.modifiers |= static_cast<std::uint16_t>(KeyModifier::Alt);
            event = decoded;
            return true;
        }
        if (sequence.size() < 3 || (sequence[1] != '[' && sequence[1] != 'O'))
        {
            return false;
        }

        const char final = sequence.back();
        const std::string_view body = sequence.substr(2, sequence.size() - 3);
        std::uint32_t first = 1;
        std::uint32_t encodedModifiers = 1;
        if (!ParseLegacyParameters(body, first, encodedModifiers))
        {
            return false;
        }

        KeyEvent decoded;
        decoded.modifiers = ToModifiers(encodedModifiers);
        KeyCode key = KeyCode::None;
        switch (final)
        {
            case 'A': key = KeyCode::Up; break;
            case 'B': key = KeyCode::Down; break;
            case 'C': key = KeyCode::Right; break;
            case 'D': key = KeyCode::Left; break;
            case 'H': key = KeyCode::Home; break;
            case 'F': key = KeyCode::End; break;
            case 'P': key = KeyCode::F1; break;
            case 'Q': key = KeyCode::F2; break;
            case 'R': key = KeyCode::F3; break;
            case 'S': key = KeyCode::F4; break;
            case 'Z':
                key = KeyCode::Tab;
                decoded.modifiers |= static_cast<std::uint16_t>(KeyModifier::Shift);
                break;
            case '~': key = TildeKey(first); break;
            default: break;
        }
        if (key == KeyCode::None)
        {
            return false;
        }

        SetLegacyKey(decoded, key);
        event = decoded;
        return true;
    }

    TerminalInputDecoder::TerminalInputDecoder(
        std::shared_ptr<TerminalSessionController> sessions)
        : sessions_(std::move(sessions)), exactKeyboardState_(sessions_->HasKittyKeyboard())
    {
    }

    void TerminalInputDecoder::Pump() { PumpAt(std::chrono::steady_clock::now()); }

    void TerminalInputDecoder::PumpAt(const std::chrono::steady_clock::time_point now)
    {
        if (!keyboardLease_)
        {
            keyboardLease_ = sessions_->Acquire(TerminalSessionUse::Keyboard);
        }

        const int descriptor = sessions_->GetInputDescriptor();
        char buffer[512];
        for (;;)
        {
            pollfd waiting{};
            waiting.fd = descriptor;
            waiting.events = POLLIN;
            if (poll(&waiting, 1, 0) <= 0)
            {
                break;
            }
            const ssize_t count = read(descriptor, buffer, sizeof(buffer));
            if (count <= 0)
            {
                break;
            }
            inputBuffer_.append(buffer, static_cast<std::size_t>(count));
            if (inputBuffer_.size() > 4096)
            {
                // A corrupt or hostile peer must not grow memory forever by starting a sequence
                // and withholding its terminator. Keep enough tail for the next complete event.
                inputBuffer_.erase(0, inputBuffer_.size() - 4096);
            }
        }
        ConsumeCompleteSequences(now);
    }

    void TerminalInputDecoder::ConsumeCompleteSequences(
        const std::chrono::steady_clock::time_point now)
    {
        if (exactKeyboardState_)
        {
            ConsumeKittySequences();
            return;
        }
        ConsumeLegacySequences(now);
        ExpireSyntheticKeys(now);
    }

    void TerminalInputDecoder::ConsumeKittySequences()
    {
        for (;;)
        {
            const std::size_t start = inputBuffer_.find("\x1b[");
            if (start == std::string::npos)
            {
                // Canonical all-keys mode produces no plain text. Retain a trailing ESC in case
                // the CSI introducer was split across reads; discard everything else.
                if (!inputBuffer_.empty() && inputBuffer_.back() == '\x1b')
                {
                    inputBuffer_.erase(0, inputBuffer_.size() - 1);
                }
                else
                {
                    inputBuffer_.clear();
                }
                return;
            }
            if (start != 0)
            {
                inputBuffer_.erase(0, start);
            }

            const std::size_t end = inputBuffer_.find('u', 2);
            const std::size_t next = inputBuffer_.find("\x1b[", 2);
            if (next != std::string::npos && (end == std::string::npos || next < end))
            {
                inputBuffer_.erase(0, next);
                continue;
            }
            if (end == std::string::npos)
            {
                return;
            }

            KeyEvent event;
            if (DecodeKittyKeyEvent(std::string_view(inputBuffer_).substr(0, end + 1), event))
            {
                Apply(event);
                events_.push_back(event);
            }
            inputBuffer_.erase(0, end + 1);
        }
    }

    void TerminalInputDecoder::ConsumeLegacySequences(
        const std::chrono::steady_clock::time_point now)
    {
        constexpr auto escapeDelay = std::chrono::milliseconds(30);

        while (!inputBuffer_.empty())
        {
            std::size_t sequenceLength = 1;
            if (inputBuffer_.front() == '\x1b')
            {
                if (inputBuffer_.size() == 1)
                {
                    if (!pendingEscapeSince_.has_value())
                    {
                        pendingEscapeSince_ = now;
                        return;
                    }
                    if (now - *pendingEscapeSince_ < escapeDelay)
                    {
                        return;
                    }
                    pendingEscapeSince_.reset();
                }
                else if (inputBuffer_[1] == '[' || inputBuffer_[1] == 'O')
                {
                    pendingEscapeSince_.reset();
                    const auto isFinal = [](const unsigned char byte) {
                        return byte >= 0x40 && byte <= 0x7e;
                    };
                    const auto end = std::find_if(inputBuffer_.begin() + 2, inputBuffer_.end(),
                                                  isFinal);
                    if (end == inputBuffer_.end())
                    {
                        return;
                    }
                    sequenceLength = static_cast<std::size_t>(end - inputBuffer_.begin()) + 1;
                }
                else
                {
                    pendingEscapeSince_.reset();
                    sequenceLength = 2;
                }
            }

            const std::string_view sequence(inputBuffer_.data(), sequenceLength);
            KeyEvent event;
            if (DecodeLegacyKeyEvent(sequence, event))
            {
                ApplySyntheticPress(event, now);
            }
            else if (DecodeKittyKeyEvent(sequence, event))
            {
                const auto identity = std::pair(event.scancode, event.keycode);
                if (event.pressed)
                {
                    ApplySyntheticPress(event, now);
                }
                else
                {
                    syntheticHeldKeys_.erase(identity);
                    Apply(event);
                    events_.push_back(event);
                }
            }
            inputBuffer_.erase(0, sequenceLength);
        }
    }

    void TerminalInputDecoder::ApplySyntheticPress(
        KeyEvent event, const std::chrono::steady_clock::time_point now)
    {
        // Deliberately a little longer than common 20--50 ms repeat intervals. A shorter window
        // creates false releases between repeats; a longer one makes taps look held for longer.
        // The first OS repeat commonly arrives after this deadline, so a continuously held key
        // can briefly stutter before repeats begin. No byte protocol can distinguish that pause
        // from a released tap -- the reason TerminalPlatform keeps ExactKeyboardState false.
        constexpr auto releaseDelay = std::chrono::milliseconds(100);
        const auto identity = std::pair(event.scancode, event.keycode);
        auto [held, inserted] = syntheticHeldKeys_.try_emplace(identity);
        held->second.expiresAt = now + releaseDelay;
        held->second.modifiers = event.modifiers;
        event.repeat = !inserted;
        Apply(event);
        events_.push_back(event);
    }

    void TerminalInputDecoder::ExpireSyntheticKeys(
        const std::chrono::steady_clock::time_point now)
    {
        for (auto held = syntheticHeldKeys_.begin(); held != syntheticHeldKeys_.end();)
        {
            if (held->second.expiresAt > now)
            {
                ++held;
                continue;
            }

            KeyEvent release;
            release.scancode = held->first.first;
            release.keycode = held->first.second;
            release.modifiers = held->second.modifiers;
            release.pressed = false;
            held = syntheticHeldKeys_.erase(held);
            Apply(release);
            events_.push_back(release);
        }
    }

    void TerminalInputDecoder::Apply(const KeyEvent& event)
    {
        if (event.keycode != KeyCode::None)
        {
            const auto identity = std::pair(event.scancode, event.keycode);
            if (event.pressed)
            {
                heldKeys_.insert(identity);
            }
            else
            {
                heldKeys_.erase(identity);
            }
        }

        const std::uint16_t lockMask = static_cast<std::uint16_t>(KeyModifier::CapsLock) |
                                       static_cast<std::uint16_t>(KeyModifier::NumLock) |
                                       static_cast<std::uint16_t>(KeyModifier::ScrollLock);
        lockModifiers_ = event.modifiers & lockMask;
        RebuildSnapshot();
    }

    void TerminalInputDecoder::RebuildSnapshot()
    {
        pressed_.clear();
        for (const auto& [scancode, keycode] : heldKeys_)
        {
            (void)scancode;
            pressed_.insert(keycode);
        }

        snapshot_.pressedKeys.clear();
        snapshot_.pressedKeys.reserve(pressed_.size());
        for (const KeyCode key : pressed_)
        {
            snapshot_.pressedKeys.push_back(static_cast<std::uint32_t>(key));
        }

        snapshot_.modifiers = lockModifiers_;
        if (!exactKeyboardState_)
        {
            const std::uint16_t momentaryMask =
                static_cast<std::uint16_t>(KeyModifier::Shift) |
                static_cast<std::uint16_t>(KeyModifier::Control) |
                static_cast<std::uint16_t>(KeyModifier::Alt) |
                static_cast<std::uint16_t>(KeyModifier::Gui) |
                static_cast<std::uint16_t>(KeyModifier::Mode);
            for (const auto& [identity, held] : syntheticHeldKeys_)
            {
                (void)identity;
                snapshot_.modifiers |= held.modifiers & momentaryMask;
            }
        }
        if (IsHeld(pressed_, KeyCode::LeftShift, KeyCode::RightShift))
        {
            snapshot_.modifiers |= static_cast<std::uint16_t>(KeyModifier::Shift);
        }
        if (IsHeld(pressed_, KeyCode::LeftControl, KeyCode::RightControl))
        {
            snapshot_.modifiers |= static_cast<std::uint16_t>(KeyModifier::Control);
        }
        if (IsHeld(pressed_, KeyCode::LeftAlt, KeyCode::RightAlt))
        {
            snapshot_.modifiers |= static_cast<std::uint16_t>(KeyModifier::Alt);
        }
        if (IsHeld(pressed_, KeyCode::LeftWindows, KeyCode::RightWindows))
        {
            snapshot_.modifiers |= static_cast<std::uint16_t>(KeyModifier::Gui);
        }
    }

    void TerminalInputDecoder::DrainEvents(std::vector<PlatformEvent>& destination,
                                           const WindowId window)
    {
        for (KeyEvent& event : events_)
        {
            event.window = window;
            destination.emplace_back(event);
        }
        events_.clear();
    }

    TerminalKeyboard::TerminalKeyboard(std::shared_ptr<TerminalInputDecoder> decoder)
        : decoder_(std::move(decoder))
    {
    }

    void TerminalKeyboard::Update() { decoder_->Pump(); }

    const KeyboardSnapshot& TerminalKeyboard::GetSnapshot() const
    {
        return decoder_->GetSnapshot();
    }

    bool TerminalKeyboard::HasKeyboard() const { return true; }

} // namespace CNA::Platform::Terminal
