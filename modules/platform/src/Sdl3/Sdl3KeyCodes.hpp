// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/Input/KeyCode.hpp"

#include <SDL3/SDL.h>

namespace CNA::Platform::Sdl3 {

    /**
     * @brief Translates an SDL keycode into the contract's virtual key.
     *
     * Unlike the scancode translation next door, this one is a real table: SDL keycodes are
     * mostly Unicode code points for printable keys, while the contract's values are Windows
     * Virtual-Key codes. `A` is 97 in one and 65 in the other, so nothing here can be a cast.
     *
     * This table moved here out of `SdlInputBridge`, which is the point of the migration: the
     * knowledge that SDL numbers keys its own way now lives in the one module allowed to know
     * that, instead of in the input layer every platform will share.
     *
     * @param keycode SDL's keycode.
     * @return The equivalent virtual key, or `KeyCode::None` for a key CNA does not name.
     */
    [[nodiscard]] inline KeyCode ToKeyCode(const SDL_Keycode keycode)
    {
        switch (keycode)
        {
        case SDLK_AC_BACK: return KeyCode::Escape; // DEC-17: CNA-only Android/browser Back -> Escape (no FNA mapping)
        case SDLK_LEFT: return KeyCode::Left;
        case SDLK_RIGHT: return KeyCode::Right;
        case SDLK_UP: return KeyCode::Up;
        case SDLK_DOWN: return KeyCode::Down;
        case SDLK_SPACE: return KeyCode::Space;
        case SDLK_RETURN: return KeyCode::Enter;
        case SDLK_ESCAPE: return KeyCode::Escape;
        case SDLK_LCTRL: return KeyCode::LeftControl;
        case SDLK_RCTRL: return KeyCode::RightControl;
        case SDLK_LSHIFT: return KeyCode::LeftShift;
        case SDLK_RSHIFT: return KeyCode::RightShift;
        case SDLK_TAB: return KeyCode::Tab;
        case SDLK_A: return KeyCode::A;
        case SDLK_B: return KeyCode::B;
        case SDLK_C: return KeyCode::C;
        case SDLK_D: return KeyCode::D;
        case SDLK_E: return KeyCode::E;
        case SDLK_F: return KeyCode::F;
        case SDLK_G: return KeyCode::G;
        case SDLK_H: return KeyCode::H;
        case SDLK_I: return KeyCode::I;
        case SDLK_J: return KeyCode::J;
        case SDLK_K: return KeyCode::K;
        case SDLK_L: return KeyCode::L;
        case SDLK_M: return KeyCode::M;
        case SDLK_N: return KeyCode::N;
        case SDLK_O: return KeyCode::O;
        case SDLK_P: return KeyCode::P;
        case SDLK_Q: return KeyCode::Q;
        case SDLK_R: return KeyCode::R;
        case SDLK_S: return KeyCode::S;
        case SDLK_T: return KeyCode::T;
        case SDLK_U: return KeyCode::U;
        case SDLK_V: return KeyCode::V;
        case SDLK_W: return KeyCode::W;
        case SDLK_X: return KeyCode::X;
        case SDLK_Y: return KeyCode::Y;
        case SDLK_Z: return KeyCode::Z;
        case SDLK_0: return KeyCode::D0;
        case SDLK_1: return KeyCode::D1;
        case SDLK_2: return KeyCode::D2;
        case SDLK_3: return KeyCode::D3;
        case SDLK_4: return KeyCode::D4;
        case SDLK_5: return KeyCode::D5;
        case SDLK_6: return KeyCode::D6;
        case SDLK_7: return KeyCode::D7;
        case SDLK_8: return KeyCode::D8;
        case SDLK_9: return KeyCode::D9;
        case SDLK_BACKSPACE: return KeyCode::Back;
        case SDLK_LALT:  return KeyCode::LeftAlt;
        case SDLK_RALT:  return KeyCode::RightAlt;
        case SDLK_LGUI:  return KeyCode::LeftWindows;
        case SDLK_RGUI:  return KeyCode::RightWindows;
        case SDLK_CAPSLOCK:  return KeyCode::CapsLock;
        case SDLK_NUMLOCKCLEAR: return KeyCode::NumLock;
        case SDLK_SCROLLLOCK:   return KeyCode::Scroll;
        case SDLK_F1:  return KeyCode::F1;
        case SDLK_F2:  return KeyCode::F2;
        case SDLK_F3:  return KeyCode::F3;
        case SDLK_F4:  return KeyCode::F4;
        case SDLK_F5:  return KeyCode::F5;
        case SDLK_F6:  return KeyCode::F6;
        case SDLK_F7:  return KeyCode::F7;
        case SDLK_F8:  return KeyCode::F8;
        case SDLK_F9:  return KeyCode::F9;
        case SDLK_F10: return KeyCode::F10;
        case SDLK_F11: return KeyCode::F11;
        case SDLK_F12: return KeyCode::F12;
        case SDLK_KP_0: return KeyCode::NumPad0;
        case SDLK_KP_1: return KeyCode::NumPad1;
        case SDLK_KP_2: return KeyCode::NumPad2;
        case SDLK_KP_3: return KeyCode::NumPad3;
        case SDLK_KP_4: return KeyCode::NumPad4;
        case SDLK_KP_5: return KeyCode::NumPad5;
        case SDLK_KP_6: return KeyCode::NumPad6;
        case SDLK_KP_7: return KeyCode::NumPad7;
        case SDLK_KP_8: return KeyCode::NumPad8;
        case SDLK_KP_9: return KeyCode::NumPad9;
        case SDLK_KP_MULTIPLY: return KeyCode::Multiply;
        case SDLK_KP_PLUS:     return KeyCode::Add;
        case SDLK_KP_MINUS:    return KeyCode::Subtract;
        case SDLK_KP_DECIMAL:  return KeyCode::Decimal;
        case SDLK_KP_DIVIDE:   return KeyCode::Divide;
        case SDLK_KP_ENTER:    return KeyCode::Enter;
        case SDLK_SEMICOLON:   return KeyCode::OemSemicolon;
        case SDLK_EQUALS:      return KeyCode::OemPlus;
        case SDLK_COMMA:       return KeyCode::OemComma;
        case SDLK_MINUS:       return KeyCode::OemMinus;
        case SDLK_PERIOD:      return KeyCode::OemPeriod;
        case SDLK_SLASH:       return KeyCode::OemQuestion;
        case SDLK_GRAVE:       return KeyCode::OemTilde;
        case SDLK_LEFTBRACKET: return KeyCode::OemOpenBrackets;
        case SDLK_BACKSLASH:   return KeyCode::OemPipe;
        case SDLK_RIGHTBRACKET:return KeyCode::OemCloseBrackets;
        case SDLK_APOSTROPHE:  return KeyCode::OemQuotes;
        case SDLK_PAGEUP:   return KeyCode::PageUp;
        case SDLK_PAGEDOWN: return KeyCode::PageDown;
        case SDLK_HOME:     return KeyCode::Home;
        case SDLK_END:      return KeyCode::End;
        case SDLK_INSERT:   return KeyCode::Insert;
        case SDLK_DELETE:   return KeyCode::Delete;
        case SDLK_PRINTSCREEN: return KeyCode::PrintScreen;
        case SDLK_PAUSE:       return KeyCode::Pause;
        case SDLK_F13: return KeyCode::F13;
        case SDLK_F14: return KeyCode::F14;
        case SDLK_F15: return KeyCode::F15;
        case SDLK_F16: return KeyCode::F16;
        case SDLK_F17: return KeyCode::F17;
        case SDLK_F18: return KeyCode::F18;
        case SDLK_F19: return KeyCode::F19;
        case SDLK_F20: return KeyCode::F20;
        case SDLK_F21: return KeyCode::F21;
        case SDLK_F22: return KeyCode::F22;
        case SDLK_F23: return KeyCode::F23;
        case SDLK_F24: return KeyCode::F24;
        case SDLK_APPLICATION: return KeyCode::Apps;
        case SDLK_MENU:        return KeyCode::Apps;
        case SDLK_SLEEP:       return KeyCode::Sleep;
        case SDLK_VOLUMEUP:    return KeyCode::VolumeUp;
        case SDLK_VOLUMEDOWN:  return KeyCode::VolumeDown;
        case SDLK_KP_CLEAR:    return KeyCode::OemClear;
        case SDLK_KP_PERIOD:   return KeyCode::OemPeriod;
        // Locale keyboard-layout fallbacks: SDL reports the character these physical keys
        // produce on non-US layouts, which differs from the US-layout keycode already mapped
        // above for the same physical key.
        case 0x00B2: return KeyCode::OemTilde;     // '²' — AZERTY
        case '|':    return KeyCode::OemPipe;      // Norwegian
        case '+':    return KeyCode::OemPlus;      // Norwegian
        case 0x00F8: return KeyCode::OemSemicolon; // 'ø' — Norwegian
        case 0x00E6: return KeyCode::OemQuotes;    // 'æ' — Norwegian
        case 0x00E9: return KeyCode::None;       // 'é' — BEPO; no virtual key names it
        default: return KeyCode::None;
        }
    }

} // namespace CNA::Platform::Sdl3
