// SPDX-License-Identifier: MS-PL

#include "Win32EventMapper.hpp"

#include "Win32Common.hpp"
#include "Win32KeyCodes.hpp"
#include "Win32Modifiers.hpp"
#include "Win32Scancodes.hpp"

#include <algorithm>
#include <utility>

namespace CNA::Platform::Win32 {

    namespace {

        /// GET_X_LPARAM/GET_Y_LPARAM without <windowsx.h>. The cast through int16_t is the whole
        /// point: a pointer position left of or above the client area is negative, and reading
        /// the word unsigned turns -1 into 65535.
        int PointerX(const std::int64_t lParam)
        {
            return static_cast<std::int16_t>(static_cast<std::uint16_t>(lParam & 0xFFFF));
        }

        int PointerY(const std::int64_t lParam)
        {
            return static_cast<std::int16_t>(
                static_cast<std::uint16_t>((static_cast<std::uint64_t>(lParam) >> 16) & 0xFFFF));
        }

        int LowWord(const std::uint64_t value)
        {
            return static_cast<int>(value & 0xFFFF);
        }

        int HighWord(const std::uint64_t value)
        {
            return static_cast<int>((value >> 16) & 0xFFFF);
        }

        int HighWordSigned(const std::uint64_t value)
        {
            return static_cast<std::int16_t>(static_cast<std::uint16_t>((value >> 16) & 0xFFFF));
        }

        /// CNA button indices: 1 left, 2 middle, 3 right, 4 X1, 5 X2.
        constexpr std::uint8_t kButtonLeft = 1;
        constexpr std::uint8_t kButtonMiddle = 2;
        constexpr std::uint8_t kButtonRight = 3;
        constexpr std::uint8_t kButtonX1 = 4;
        constexpr std::uint8_t kButtonX2 = 5;

        std::uint8_t ButtonBit(const std::uint8_t button)
        {
            return button >= 1 && button <= 5 ? static_cast<std::uint8_t>(1u << (button - 1)) : 0u;
        }

    } // namespace

    Win32EventMapper::Win32EventMapper(const WindowId window, Win32EventSink& sink)
        : window_(window)
        , sink_(&sink)
        , modifiers_(&CurrentModifiers)
    {
    }

    void Win32EventMapper::DetachSink()
    {
        sink_ = nullptr;
    }

    void Win32EventMapper::SetTextInputActive(const bool active)
    {
        if (textInputActive_ == active)
            return;
        textInputActive_ = active;
        // A half-assembled surrogate pair belongs to the mode that was interrupted; carrying it
        // into the next one would prepend a stray character to the first thing typed.
        surrogates_.Reset();
    }

    void Win32EventMapper::SetModifierProvider(const ModifierProvider provider)
    {
        modifiers_ = provider != nullptr ? provider : &CurrentModifiers;
    }

    bool Win32EventMapper::TryGetPointerPosition(int& x, int& y) const
    {
        if (!pointerPositionKnown_)
            return false;
        x = pointerX_;
        y = pointerY_;
        return true;
    }

    void Win32EventMapper::GetWheelTotals(int& horizontal, int& vertical) const
    {
        horizontal = wheelHorizontal_;
        vertical = wheelVertical_;
    }

    void Win32EventMapper::Emit(PlatformEvent event)
    {
        if (sink_ != nullptr)
            sink_->Push(std::move(event));
    }

    void Win32EventMapper::PushWindow(const WindowEventKind kind, const int data1, const int data2)
    {
        WindowEvent event;
        event.window = window_;
        event.kind = kind;
        event.data1 = data1;
        event.data2 = data2;
        Emit(event);
    }

    void Win32EventMapper::PushKey(const KeyCode keycode, const Scancode scancode,
                                   const bool pressed, const bool repeat)
    {
        KeyEvent event;
        event.window = window_;
        event.keycode = keycode;
        event.scancode = scancode;
        event.modifiers = modifiers_();
        event.pressed = pressed;
        event.repeat = repeat;
        Emit(event);

        if (keycode == KeyCode::None)
            return;

        const auto existing = std::find(heldKeys_.begin(), heldKeys_.end(), keycode);
        if (pressed)
        {
            if (existing == heldKeys_.end())
            {
                heldKeys_.push_back(keycode);
                heldScancodes_.push_back(scancode);
            }
        }
        else if (existing != heldKeys_.end())
        {
            heldScancodes_.erase(heldScancodes_.begin() + (existing - heldKeys_.begin()));
            heldKeys_.erase(existing);
        }
    }

    void Win32EventMapper::ReleaseHeldKeys()
    {
        // Copied rather than iterated in place: PushKey erases from the same vectors.
        const std::vector<KeyCode> keys = heldKeys_;
        const std::vector<Scancode> scancodes = heldScancodes_;
        heldKeys_.clear();
        heldScancodes_.clear();
        for (std::size_t index = 0; index < keys.size(); ++index)
        {
            KeyEvent event;
            event.window = window_;
            event.keycode = keys[index];
            event.scancode = index < scancodes.size() ? scancodes[index] : Scancode::Unknown;
            event.modifiers = 0;
            event.pressed = false;
            event.repeat = false;
            Emit(event);
        }
        surrogates_.Reset();
    }

    bool Win32EventMapper::TranslateKey(const std::uint32_t message, const std::uint64_t wParam,
                                        const std::int64_t lParam)
    {
        const bool pressed = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
        const Win32PhysicalKey physical =
            PhysicalKeyFromLParam(static_cast<std::uint64_t>(lParam));
        const std::uint32_t sided =
            ResolveSidedVirtualKey(static_cast<std::uint32_t>(wParam), physical);

        // lParam bit 30 is the previous key state: set means the key was already down, which is
        // exactly auto-repeat. Meaningless on a release, where it is always set.
        const bool repeat =
            pressed && ((static_cast<std::uint64_t>(lParam) >> 30) & 0x1u) != 0;

        PushKey(ToKeyCode(sided), ToScancode(physical), pressed, repeat);

        // Deliberately NOT handled: the system messages still have to reach DefWindowProcW so
        // Alt+F4, Alt+Space and the menu mnemonics keep working. Returning true here would make
        // a CNA window impossible to close from the keyboard.
        return false;
    }

    bool Win32EventMapper::TranslateChar(const std::uint64_t wParam)
    {
        if (!textInputActive_)
            return true;

        const auto unit = static_cast<char16_t>(wParam & 0xFFFFu);

        // Control characters are key events, not text. Tab is the exception a text field
        // legitimately receives; carriage return, escape and backspace are not text and would be
        // committed into a string the caller then has to strip.
        if (unit < 0x20 && unit != u'\t')
            return true;
        if (unit == 0x7F)
            return true;

        std::string text;
        if (!surrogates_.Feed(unit, text))
            return true;

        TextInputEvent event;
        event.window = window_;
        event.text = std::move(text);
        Emit(event);
        return true;
    }

    bool Win32EventMapper::TranslateSize(const std::uint64_t wParam, const std::int64_t lParam)
    {
        // The client size is never negative, so it is read unsigned. Sign-extending it would
        // report a 40000-pixel-wide window as -25536 on a very large display.
        const int width = LowWord(static_cast<std::uint64_t>(lParam));
        const int height = HighWord(static_cast<std::uint64_t>(lParam));

        const Win32ShowState previous = showState_;
        switch (wParam)
        {
            case SIZE_MINIMIZED:
                showState_ = Win32ShowState::Minimized;
                if (previous != Win32ShowState::Minimized)
                    PushWindow(WindowEventKind::Minimized);
                // A minimised window has a zero-sized client area. Reporting that as a resize
                // would have every renderer rebuild a 0x0 swapchain once per minimise.
                return true;

            case SIZE_MAXIMIZED:
                showState_ = Win32ShowState::Maximized;
                if (previous != Win32ShowState::Maximized)
                    PushWindow(WindowEventKind::Maximized);
                break;

            case SIZE_RESTORED:
                showState_ = Win32ShowState::Normal;
                if (previous != Win32ShowState::Normal)
                    PushWindow(WindowEventKind::Restored);
                break;

            default:
                // SIZE_MAXHIDE / SIZE_MAXSHOW describe *another* window's maximisation. Nothing
                // about this window changed.
                return true;
        }

        PushWindow(WindowEventKind::Resized, width, height);
        // Kept distinct from Resized on purpose: a renderer sizes its swapchain from the drawable
        // size, and under per-monitor DPI the two can move independently.
        PushWindow(WindowEventKind::PixelSizeChanged, width, height);
        return true;
    }

    bool Win32EventMapper::TranslateMouseMove(const std::int64_t lParam)
    {
        const int x = PointerX(lParam);
        const int y = PointerY(lParam);

        MouseMotionEvent event;
        event.window = window_;
        event.x = static_cast<float>(x);
        event.y = static_cast<float>(y);
        if (pointerPositionKnown_)
        {
            event.deltaX = static_cast<float>(x - pointerX_);
            event.deltaY = static_cast<float>(y - pointerY_);
        }
        pointerPositionKnown_ = true;
        pointerX_ = x;
        pointerY_ = y;
        Emit(event);
        return true;
    }

    bool Win32EventMapper::TranslateMouseButton(const std::uint32_t message,
                                                const std::int64_t lParam)
    {
        std::uint8_t button = 0;
        bool pressed = false;
        std::uint8_t clicks = 1;

        switch (message)
        {
            case WM_LBUTTONDOWN: button = kButtonLeft;   pressed = true;  break;
            case WM_LBUTTONUP:   button = kButtonLeft;   pressed = false; break;
            case WM_LBUTTONDBLCLK: button = kButtonLeft; pressed = true; clicks = 2; break;
            case WM_MBUTTONDOWN: button = kButtonMiddle; pressed = true;  break;
            case WM_MBUTTONUP:   button = kButtonMiddle; pressed = false; break;
            case WM_MBUTTONDBLCLK: button = kButtonMiddle; pressed = true; clicks = 2; break;
            case WM_RBUTTONDOWN: button = kButtonRight;  pressed = true;  break;
            case WM_RBUTTONUP:   button = kButtonRight;  pressed = false; break;
            case WM_RBUTTONDBLCLK: button = kButtonRight; pressed = true; clicks = 2; break;
            default:
                return false;
        }

        const int x = PointerX(lParam);
        const int y = PointerY(lParam);
        pointerPositionKnown_ = true;
        pointerX_ = x;
        pointerY_ = y;

        if (pressed)
            heldButtons_ |= ButtonBit(button);
        else
            heldButtons_ = static_cast<std::uint8_t>(heldButtons_ & ~ButtonBit(button));

        MouseButtonEvent event;
        event.window = window_;
        event.button = button;
        event.pressed = pressed;
        event.clicks = clicks;
        event.x = static_cast<float>(x);
        event.y = static_cast<float>(y);
        Emit(event);
        return true;
    }

    bool Win32EventMapper::TranslateMouseWheel(const std::uint32_t message,
                                               const std::uint64_t wParam)
    {
        const int delta = HighWordSigned(wParam);
        const float notches = static_cast<float>(delta) / static_cast<float>(WHEEL_DELTA);

        MouseWheelEvent event;
        event.window = window_;
        if (message == WM_MOUSEWHEEL)
        {
            event.y = notches;
            wheelVertical_ += delta;
        }
        else
        {
            // Windows reports a tilt to the right as positive; the CNA vocabulary, like SDL's,
            // reports it as negative. Flipping here rather than in the consumer is what keeps a
            // game's horizontal scrolling identical on the Win32 and SDL3 backends.
            event.x = -notches;
            wheelHorizontal_ -= delta;
        }
        Emit(event);
        return true;
    }

    bool Win32EventMapper::Translate(const std::uint32_t message, const std::uint64_t wParam,
                                     const std::int64_t lParam)
    {
        switch (message)
        {
            case WM_CLOSE:
                // The contract's central window rule: a close is a *request*. Reporting it and
                // returning true suppresses DefWindowProcW's DestroyWindow, so the application
                // decides. A window dies when its IPlatformWindow wrapper is destroyed.
                PushWindow(WindowEventKind::CloseRequested);
                return true;

            case WM_SIZE:
                return TranslateSize(wParam, lParam);

            case WM_MOVE:
                PushWindow(WindowEventKind::Moved, PointerX(lParam), PointerY(lParam));
                return false;

            case WM_SETFOCUS:
                PushWindow(WindowEventKind::FocusGained);
                return false;

            case WM_KILLFOCUS:
                ReleaseHeldKeys();
                PushWindow(WindowEventKind::FocusLost);
                return false;

            case WM_PAINT:
                PushWindow(WindowEventKind::Exposed);
                // Deliberately not handled: the window still has to validate its update region,
                // which Win32Window does with BeginPaint/EndPaint. Swallowing WM_PAINT here
                // without validating would make Windows resend it forever.
                return false;

            case WM_SHOWWINDOW:
                if (wParam != 0)
                    PushWindow(WindowEventKind::Exposed);
                return false;

            case WM_DPICHANGED:
                PushWindow(WindowEventKind::DisplayScaleChanged, LowWord(wParam));
                PushWindow(WindowEventKind::PixelSizeChanged);
                return false;

            case WM_DISPLAYCHANGE:
                PushWindow(WindowEventKind::DisplayChanged);
                return false;

            case WM_KEYDOWN:
            case WM_KEYUP:
            case WM_SYSKEYDOWN:
            case WM_SYSKEYUP:
                return TranslateKey(message, wParam, lParam);

            case WM_CHAR:
                return TranslateChar(wParam);

            case WM_MOUSEMOVE:
                return TranslateMouseMove(lParam);

            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
            case WM_LBUTTONDBLCLK:
            case WM_MBUTTONDOWN:
            case WM_MBUTTONUP:
            case WM_MBUTTONDBLCLK:
            case WM_RBUTTONDOWN:
            case WM_RBUTTONUP:
            case WM_RBUTTONDBLCLK:
                return TranslateMouseButton(message, lParam);

            case WM_XBUTTONDOWN:
            case WM_XBUTTONUP:
            case WM_XBUTTONDBLCLK:
            {
                const int which = HighWordSigned(wParam);
                const std::uint8_t button = which == XBUTTON2 ? kButtonX2 : kButtonX1;
                const bool pressed = message != WM_XBUTTONUP;
                const int x = PointerX(lParam);
                const int y = PointerY(lParam);
                pointerPositionKnown_ = true;
                pointerX_ = x;
                pointerY_ = y;
                if (pressed)
                    heldButtons_ |= ButtonBit(button);
                else
                    heldButtons_ = static_cast<std::uint8_t>(heldButtons_ & ~ButtonBit(button));

                MouseButtonEvent event;
                event.window = window_;
                event.button = button;
                event.pressed = pressed;
                event.clicks = message == WM_XBUTTONDBLCLK ? 2 : 1;
                event.x = static_cast<float>(x);
                event.y = static_cast<float>(y);
                Emit(event);
                // The X buttons are the one pointer family Windows expects an acknowledgement
                // from: TRUE, not 0, is the documented "handled" reply.
                return true;
            }

            case WM_MOUSEWHEEL:
            case WM_MOUSEHWHEEL:
                return TranslateMouseWheel(message, wParam);

            case WM_MOUSELEAVE:
                pointerPositionKnown_ = false;
                return false;

            default:
                return false;
        }
    }

} // namespace CNA::Platform::Win32
