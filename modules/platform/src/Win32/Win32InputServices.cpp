// SPDX-License-Identifier: MS-PL

#include "Win32InputServices.hpp"

#include "Win32Error.hpp"
#include "Win32KeyCodes.hpp"
#include "Win32Modifiers.hpp"
#include "Win32Scancodes.hpp"
#include "Win32Utf.hpp"
#include "Win32Window.hpp"

#include "CNA/Platform/PlatformException.hpp"

#include <algorithm>
#include <array>
#include <vector>

namespace CNA::Platform::Win32 {

    namespace {

        constexpr std::size_t kVirtualKeyCount = 256;

        const wchar_t* SystemCursorName(const SystemCursor cursor)
        {
            switch (cursor)
            {
                case SystemCursor::Arrow:      return IDC_ARROW;
                case SystemCursor::IBeam:      return IDC_IBEAM;
                case SystemCursor::Wait:       return IDC_WAIT;
                case SystemCursor::Crosshair:  return IDC_CROSS;
                case SystemCursor::Move:       return IDC_SIZEALL;
                case SystemCursor::NotAllowed: return IDC_NO;
                case SystemCursor::Pointer:    return IDC_HAND;
                case SystemCursor::Progress:   return IDC_APPSTARTING;
                case SystemCursor::NwseResize: return IDC_SIZENWSE;
                case SystemCursor::NeswResize: return IDC_SIZENESW;
                case SystemCursor::EwResize:   return IDC_SIZEWE;
                case SystemCursor::NsResize:   return IDC_SIZENS;
            }
            return IDC_ARROW;
        }

        HCURSOR CreateCursorFromImage(const CursorImage& image)
        {
            BITMAPV5HEADER header{};
            header.bV5Size = sizeof(header);
            header.bV5Width = image.width;
            // Negative height selects a top-down DIB, which is the row order CursorImage
            // documents. Leaving it positive silently flips every custom cursor vertically.
            header.bV5Height = -image.height;
            header.bV5Planes = 1;
            header.bV5BitCount = 32;
            header.bV5Compression = BI_BITFIELDS;
            header.bV5RedMask = 0x00FF0000;
            header.bV5GreenMask = 0x0000FF00;
            header.bV5BlueMask = 0x000000FF;
            header.bV5AlphaMask = 0xFF000000;

            const HDC screen = GetDC(nullptr);
            if (screen == nullptr)
                return nullptr;

            void* pixels = nullptr;
            const HBITMAP colour = CreateDIBSection(
                screen, reinterpret_cast<const BITMAPINFO*>(&header), DIB_RGB_COLORS, &pixels,
                nullptr, 0);
            ReleaseDC(nullptr, screen);
            if (colour == nullptr || pixels == nullptr)
            {
                if (colour != nullptr)
                    DeleteObject(colour);
                return nullptr;
            }

            // CursorImage is 0xAABBGGRR (red in the low byte); a 32-bit Windows DIB is 0xAARRGGBB.
            // Only red and blue move.
            auto* destination = static_cast<std::uint32_t*>(pixels);
            const std::size_t count =
                static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height);
            for (std::size_t index = 0; index < count; ++index)
            {
                const std::uint32_t source = image.rgba[index];
                destination[index] = (source & 0xFF00FF00u) | ((source & 0x00FF0000u) >> 16) |
                                     ((source & 0x000000FFu) << 16);
            }

            // An all-zero AND mask leaves the 32-bit colour bitmap's alpha channel in charge,
            // which is what gives a soft-edged cursor.
            const HBITMAP mask = CreateBitmap(image.width, image.height, 1, 1, nullptr);
            if (mask == nullptr)
            {
                DeleteObject(colour);
                return nullptr;
            }

            ICONINFO info{};
            info.fIcon = FALSE;
            info.xHotspot = static_cast<DWORD>(image.hotSpotX);
            info.yHotspot = static_cast<DWORD>(image.hotSpotY);
            info.hbmMask = mask;
            info.hbmColor = colour;

            const HICON icon = CreateIconIndirect(&info);
            DeleteObject(mask);
            DeleteObject(colour);
            return reinterpret_cast<HCURSOR>(icon);
        }

        std::vector<InputDeviceInfo> EnumerateRawDevices(const DWORD type,
                                                         const InputDeviceKind kind)
        {
            std::vector<InputDeviceInfo> devices;

            UINT count = 0;
            if (GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST)) != 0 ||
                count == 0)
            {
                return devices;
            }

            std::vector<RAWINPUTDEVICELIST> list(count);
            const UINT written =
                GetRawInputDeviceList(list.data(), &count, sizeof(RAWINPUTDEVICELIST));
            if (written == static_cast<UINT>(-1))
                return devices;
            list.resize(written);

            for (const RAWINPUTDEVICELIST& entry : list)
            {
                if (entry.dwType != type)
                    continue;

                InputDeviceInfo info;
                info.kind = kind;
                info.id = static_cast<DeviceId>(reinterpret_cast<std::uintptr_t>(entry.hDevice));

                UINT nameLength = 0;
                if (GetRawInputDeviceInfoW(entry.hDevice, RIDI_DEVICENAME, nullptr, &nameLength) ==
                        0 &&
                    nameLength > 0)
                {
                    std::wstring name(nameLength, L'\0');
                    if (GetRawInputDeviceInfoW(entry.hDevice, RIDI_DEVICENAME, name.data(),
                                               &nameLength) != static_cast<UINT>(-1))
                    {
                        name.resize(wcsnlen(name.c_str(), name.size()));
                        info.name = ToUtf8(name);
                    }
                }
                devices.push_back(std::move(info));
            }
            return devices;
        }

    } // namespace

    // --- keyboard ------------------------------------------------------------------------------

    void Win32Keyboard::Update()
    {
        snapshot_.pressedKeys.clear();
        snapshot_.modifiers = CurrentModifiers();

        std::array<BYTE, kVirtualKeyCount> state{};
        if (GetKeyboardState(state.data()) == FALSE)
            return;

        for (std::size_t virtualKey = 0; virtualKey < kVirtualKeyCount; ++virtualKey)
        {
            // The high bit is "held"; the low bit is the toggle state, which for Caps Lock is on
            // for as long as the light is on and has nothing to do with the key being down.
            if ((state[virtualKey] & 0x80u) == 0)
                continue;

            const KeyCode keycode = ToKeyCode(static_cast<std::uint32_t>(virtualKey));
            if (keycode == KeyCode::None)
                continue;
            snapshot_.pressedKeys.push_back(keycode);
        }
    }

    const KeyboardSnapshot& Win32Keyboard::GetSnapshot() const
    {
        return snapshot_;
    }

    bool Win32Keyboard::HasKeyboard() const
    {
        return !EnumerateRawDevices(RIM_TYPEKEYBOARD, InputDeviceKind::Keyboard).empty();
    }

    KeyCode Win32Keyboard::GetKeyFromScancode(const Scancode scancode) const
    {
        return KeyCodeFromScancode(scancode);
    }

    std::string Win32Keyboard::GetScancodeName(const Scancode scancode) const
    {
        // CNA's own stable name, deliberately not the layout's: this one names the *position* and
        // must not change when the user switches keyboard layout.
        return ToString(scancode);
    }

    Scancode Win32Keyboard::GetScancodeFromName(const std::string& name) const
    {
        return ScancodeFromString(name);
    }

    std::string Win32Keyboard::GetKeyName(const Scancode scancode) const
    {
        return LayoutKeyName(scancode);
    }

    KeyCode Win32Keyboard::GetKeyFromName(const std::string& name) const
    {
        return KeyCodeFromLayoutName(name);
    }

    // --- mouse ---------------------------------------------------------------------------------

    Win32Mouse::Win32Mouse(Win32PlatformAccess& access)
        : access_(&access)
        , cursor_(LoadCursorW(nullptr, IDC_ARROW))
    {
    }

    Win32Mouse::~Win32Mouse()
    {
        if (relativeMode_)
        {
            if (Win32Window* window = access_->FindWindow(relativeWindow_))
                (void) window->SetRawPointerCapture(false);
            else
                ClipCursor(nullptr);
        }
        if (!cursorVisible_)
        {
            while (ShowCursor(TRUE) < 0)
            {
            }
        }
        ReleaseOwnedCursor();
    }

    void Win32Mouse::ReleaseOwnedCursor()
    {
        if (ownedCursor_ != nullptr)
        {
            DestroyCursor(ownedCursor_);
            ownedCursor_ = nullptr;
        }
    }

    void Win32Mouse::Update()
    {
        Win32Window* window = access_->GetFocusedWindow();
        if (window == nullptr)
            window = access_->GetAnyWindow();
        if (window == nullptr)
        {
            snapshot_.window = 0;
            return;
        }

        const Win32EventMapper& mapper = window->GetMapper();
        snapshot_.window = window->GetId();
        snapshot_.buttons = mapper.GetHeldButtons();
        mapper.GetWheelTotals(snapshot_.scrollX, snapshot_.scrollY);

        int x = 0;
        int y = 0;
        if (mapper.TryGetPointerPosition(x, y))
        {
            snapshot_.x = x;
            snapshot_.y = y;
            return;
        }

        // No motion has been seen for this window yet. Ask the desktop and convert, so the very
        // first Mouse::GetState() of a frame reports where the pointer actually is rather than
        // (0, 0).
        POINT desktop{};
        if (GetCursorPos(&desktop) != FALSE && ScreenToClient(window->GetHwnd(), &desktop) != FALSE)
        {
            snapshot_.x = desktop.x;
            snapshot_.y = desktop.y;
        }
    }

    const MouseSnapshot& Win32Mouse::GetSnapshot() const
    {
        return snapshot_;
    }

    MouseDelta Win32Mouse::ConsumeRelativeDelta()
    {
        const MouseDelta delta{relativeX_, relativeY_};
        relativeX_ = 0;
        relativeY_ = 0;
        return delta;
    }

    void Win32Mouse::AccumulateRawDelta(const int deltaX, const int deltaY)
    {
        relativeX_ += deltaX;
        relativeY_ += deltaY;
    }

    void Win32Mouse::SetPosition(const WindowId window, const int x, const int y)
    {
        Win32Window* target = access_->FindWindow(window);
        if (target == nullptr)
            return;

        POINT point{x, y};
        if (ClientToScreen(target->GetHwnd(), &point) == FALSE)
            return;
        SetCursorPos(point.x, point.y);
    }

    void Win32Mouse::SetCursorVisible(const bool visible)
    {
        if (visible == cursorVisible_)
            return;
        cursorVisible_ = visible;

        // ShowCursor keeps an internal counter rather than a flag, so a single call is not enough
        // to reach a known state from an unknown one.
        if (visible)
        {
            while (ShowCursor(TRUE) < 0)
            {
            }
        }
        else
        {
            while (ShowCursor(FALSE) >= 0)
            {
            }
        }
    }

    bool Win32Mouse::ApplyCursor()
    {
        if (!cursorVisible_)
        {
            ::SetCursor(nullptr);
            return true;
        }
        if (cursor_ == nullptr)
            return false;
        ::SetCursor(cursor_);
        return true;
    }

    void Win32Mouse::SetCursor(const SystemCursor cursor)
    {
        if (!access_->GetPlatformCapabilities().cursorShapes)
        {
            throw PlatformNotSupportedException(PlatformCapability::CursorShapes,
                                                access_->GetPlatformName());
        }

        const HCURSOR loaded = LoadCursorW(nullptr, SystemCursorName(cursor));
        if (loaded == nullptr)
            ThrowLastError("Win32Mouse::SetCursor");

        ReleaseOwnedCursor();
        cursor_ = loaded;
        (void) ApplyCursor();
    }

    void Win32Mouse::SetCursor(const CursorImage& cursor)
    {
        if (!access_->GetPlatformCapabilities().cursorShapes)
        {
            throw PlatformNotSupportedException(PlatformCapability::CursorShapes,
                                                access_->GetPlatformName());
        }

        if (cursor.width <= 0 || cursor.height <= 0)
            throw PlatformException("Win32Mouse::SetCursor", "the cursor image has no area");
        const auto expected =
            static_cast<std::size_t>(cursor.width) * static_cast<std::size_t>(cursor.height);
        if (cursor.rgba.size() != expected)
        {
            throw PlatformException("Win32Mouse::SetCursor",
                                    "the pixel count does not match width * height");
        }
        if (cursor.hotSpotX < 0 || cursor.hotSpotY < 0 || cursor.hotSpotX >= cursor.width ||
            cursor.hotSpotY >= cursor.height)
        {
            throw PlatformException("Win32Mouse::SetCursor",
                                    "the hot spot lies outside the image");
        }

        const HCURSOR created = CreateCursorFromImage(cursor);
        if (created == nullptr)
            throw PlatformException("Win32Mouse::SetCursor", DescribeLastError());

        ReleaseOwnedCursor();
        ownedCursor_ = created;
        cursor_ = created;
        (void) ApplyCursor();
    }

    void Win32Mouse::SetRelativeMode(const WindowId window, const bool enabled)
    {
        if (!access_->GetPlatformCapabilities().relativeMouse)
        {
            throw PlatformNotSupportedException(PlatformCapability::RelativeMouse,
                                                access_->GetPlatformName());
        }

        if (!enabled)
        {
            if (!relativeMode_)
                return;
            if (Win32Window* target = access_->FindWindow(relativeWindow_))
                (void) target->SetRawPointerCapture(false);
            else
                ClipCursor(nullptr);
            relativeMode_ = false;
            relativeWindow_ = 0;
            relativeX_ = 0;
            relativeY_ = 0;
            return;
        }

        Win32Window* target = window != 0 ? access_->FindWindow(window) : access_->GetFocusedWindow();
        if (target == nullptr)
            target = access_->GetAnyWindow();
        if (target == nullptr)
        {
            throw PlatformException("Win32Mouse::SetRelativeMode",
                                    "relative mode needs a window to confine the pointer to");
        }

        if (!target->SetRawPointerCapture(true))
        {
            // Registration failed: the mode is NOT entered, and saying so is the whole point.
            // A half-enabled state -- cursor hidden, no deltas -- is the failure a game cannot
            // diagnose.
            throw PlatformException("Win32Mouse::SetRelativeMode",
                                    "raw pointer input could not be registered: " +
                                        DescribeLastError());
        }

        relativeMode_ = true;
        relativeWindow_ = target->GetId();
        relativeX_ = 0;
        relativeY_ = 0;
    }

    bool Win32Mouse::IsRelativeMode() const
    {
        return relativeMode_;
    }

    bool Win32Mouse::SetCapture(const bool enabled)
    {
        if (!access_->GetPlatformCapabilities().globalPointer)
        {
            throw PlatformNotSupportedException(PlatformCapability::GlobalPointer,
                                                access_->GetPlatformName());
        }

        if (!enabled)
            return ReleaseCapture() != FALSE;

        Win32Window* window = access_->GetFocusedWindow();
        if (window == nullptr)
            window = access_->GetAnyWindow();
        if (window == nullptr)
            return false;
        ::SetCapture(window->GetHwnd());
        return true;
    }

    bool Win32Mouse::TryGetGlobalPosition(float& x, float& y) const
    {
        if (!access_->GetPlatformCapabilities().globalPointer)
        {
            throw PlatformNotSupportedException(PlatformCapability::GlobalPointer,
                                                access_->GetPlatformName());
        }

        POINT point{};
        if (GetCursorPos(&point) == FALSE)
            return false;
        x = static_cast<float>(point.x);
        y = static_cast<float>(point.y);
        return true;
    }

    bool Win32Mouse::SetGlobalPosition(const float x, const float y)
    {
        if (!access_->GetPlatformCapabilities().globalPointer)
        {
            throw PlatformNotSupportedException(PlatformCapability::GlobalPointer,
                                                access_->GetPlatformName());
        }
        return SetCursorPos(static_cast<int>(x), static_cast<int>(y)) != FALSE;
    }

    // --- text input ----------------------------------------------------------------------------

    Win32TextInput::Win32TextInput(Win32PlatformAccess& access)
        : access_(&access)
    {
    }

    void Win32TextInput::Start(const WindowId window, const TextInputType type)
    {
        if (!access_->GetPlatformCapabilities().textInput)
        {
            throw PlatformNotSupportedException(PlatformCapability::TextInput,
                                                access_->GetPlatformName());
        }

        Win32Window* target = access_->FindWindow(window);
        if (target == nullptr)
        {
            throw PlatformException("Win32TextInput::Start",
                                    "the window id does not name a live window");
        }

        // The type is a hint for a platform that chooses an input UI from it -- an on-screen
        // keyboard, a numeric pad. A desktop Windows window has one physical keyboard and no such
        // choice to make, so the hint is accepted and has no effect, rather than being refused for
        // a caller that is not asking for anything unavailable.
        (void) type;
        target->GetMapper().SetTextInputActive(true);
    }

    void Win32TextInput::Stop(const WindowId window)
    {
        if (Win32Window* target = access_->FindWindow(window))
            target->GetMapper().SetTextInputActive(false);
    }

    bool Win32TextInput::IsActive(const WindowId window) const
    {
        const Win32Window* target = access_->FindWindow(window);
        return target != nullptr && target->GetMapper().IsTextInputActive();
    }

    bool Win32TextInput::IsScreenKeyboardShown(const WindowId window) const
    {
        // Windows has a touch keyboard, but it is user- and shell-driven: an application cannot
        // ask for it and cannot reliably observe it. Reporting false is the honest answer, not a
        // gap -- the alternative would be a guess a caller would lay out its UI around.
        (void) window;
        return false;
    }

    void Win32TextInput::SetInputArea(const WindowId window, const TextInputArea& area)
    {
        // Recorded rather than applied: the only consumer of this rectangle is an input method's
        // candidate window, and `ime` is reported false. Keeping the value means the IME work
        // recorded in plans/plan_win32.md section 15 has its input already wired.
        if (access_->FindWindow(window) != nullptr)
            areas_[window] = area;
    }

    // --- input devices -------------------------------------------------------------------------

    std::vector<InputDeviceInfo> Win32InputDevices::GetDevices(const InputDeviceKind kind) const
    {
        switch (kind)
        {
            case InputDeviceKind::Keyboard:
                return EnumerateRawDevices(RIM_TYPEKEYBOARD, kind);
            case InputDeviceKind::Mouse:
                return EnumerateRawDevices(RIM_TYPEMOUSE, kind);
            case InputDeviceKind::Gamepad:
            case InputDeviceKind::Joystick:
            case InputDeviceKind::Touch:
            case InputDeviceKind::Haptic:
            case InputDeviceKind::Sensor:
                // Not "none attached" but "this backend cannot enumerate this class". The empty
                // list is what the contract specifies for both, and the capability set is where
                // the difference is stated: gamepad, joystick, haptics and sensors are all false.
                return {};
        }
        return {};
    }

    bool Win32InputDevices::HasDevice(const InputDeviceKind kind) const
    {
        return !GetDevices(kind).empty();
    }

} // namespace CNA::Platform::Win32
