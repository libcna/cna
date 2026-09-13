// SPDX-License-Identifier: MS-PL

#include "Win32Window.hpp"

#include "Win32DpiSupport.hpp"
#include "Win32Error.hpp"
#include "Win32Utf.hpp"

#include "CNA/Platform/PlatformException.hpp"

#include <algorithm>
#include <array>
#include <vector>

namespace CNA::Platform::Win32 {

    namespace {

        constexpr const wchar_t* kOwnedWindowProperty = L"CnaPlatformWindow.Owned";

        LONG_PTR StyleFor(const WindowDescription& description)
        {
            LONG_PTR style = WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
            if (description.borderless)
            {
                style |= WS_POPUP;
            }
            else
            {
                style |= WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
                if (description.resizable)
                    style |= WS_THICKFRAME | WS_MAXIMIZEBOX;
            }
            return style;
        }

        LONG_PTR WithResizable(LONG_PTR style, const bool resizable)
        {
            if (resizable)
                return style | WS_THICKFRAME | WS_MAXIMIZEBOX;
            return style & ~static_cast<LONG_PTR>(WS_THICKFRAME | WS_MAXIMIZEBOX);
        }

        WINDOWPLACEMENT ReadPlacement(const HWND window)
        {
            WINDOWPLACEMENT placement{};
            placement.length = sizeof(placement);
            GetWindowPlacement(window, &placement);
            return placement;
        }

        bool MonitorRect(const HWND window, RECT& bounds)
        {
            const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
            if (monitor == nullptr)
                return false;
            MONITORINFO info{};
            info.cbSize = sizeof(info);
            if (GetMonitorInfoW(monitor, &info) == FALSE)
                return false;
            bounds = info.rcMonitor;
            return true;
        }

    } // namespace

    Win32Window::Win32Window(const WindowDescription& description, const WindowId id,
                             Win32WindowHost& host)
        : id_(id)
        , host_(&host)
        , ownsWindow_(true)
        , mapper_(id, host)
        , minimumWidth_(description.minimumWidth)
        , minimumHeight_(description.minimumHeight)
        , maximumWidth_(description.maximumWidth)
        , maximumHeight_(description.maximumHeight)
    {
        windowClass_.emplace();

        const LONG_PTR style = StyleFor(description);
        const LONG_PTR exStyle = WS_EX_APPWINDOW;
        const Win32DpiSupport& dpiSupport = Win32DpiSupport::Get();

        // The window does not exist yet, so its own DPI cannot be asked for. The system DPI is
        // the right approximation for the initial frame; a window that then lands on a
        // differently-scaled monitor gets a WM_DPICHANGED and re-sizes itself from the suggested
        // rectangle.
        const unsigned int dpi = dpiSupport.GetSystemDpi();

        // WindowDescription's size is the CLIENT area; CreateWindowExW takes the OUTER size.
        RECT frame{0, 0, std::max(description.width, 1), std::max(description.height, 1)};
        (void) dpiSupport.AdjustWindowRect(frame, static_cast<DWORD>(style),
                                           static_cast<DWORD>(exStyle), false, dpi);
        const int outerWidth = frame.right - frame.left;
        const int outerHeight = frame.bottom - frame.top;

        int x = CW_USEDEFAULT;
        int y = CW_USEDEFAULT;
        if (!description.centered)
        {
            x = description.x + frame.left;
            y = description.y + frame.top;
        }

        const std::wstring title = ToWide(description.title);
        hwnd_ = CreateWindowExW(static_cast<DWORD>(exStyle), Win32WindowClass::GetClassName(),
                                title.c_str(), static_cast<DWORD>(style), x, y, outerWidth,
                                outerHeight, nullptr, nullptr, Win32WindowClass::GetInstance(),
                                this);
        if (hwnd_ == nullptr)
        {
            const DWORD error = GetLastError();
            windowClass_.reset();
            ThrowError("Win32Platform::CreateWindow", error);
        }

        // Marks this HWND as one CNA created and owns, so AdoptWindowHandle can refuse to adopt
        // a window the platform already owns rather than producing a second wrapper for it.
        SetPropW(hwnd_, kOwnedWindowProperty, reinterpret_cast<HANDLE>(static_cast<LONG_PTR>(1)));

        // Post-creation setup can still fail -- SetFullscreenMode throws when the window's monitor
        // cannot be resolved. A throw from here means the constructor never completes, so the
        // destructor will NOT run and the HWND this function already created would leak for the
        // process lifetime. Undoing it by hand is the only way to keep "a failed CreateWindow
        // leaves the platform able to retry" true.
        try
        {
            if (description.centered)
            {
                RECT monitor{};
                if (MonitorRect(hwnd_, monitor))
                {
                    const int centreX =
                        monitor.left + ((monitor.right - monitor.left) - outerWidth) / 2;
                    const int centreY =
                        monitor.top + ((monitor.bottom - monitor.top) - outerHeight) / 2;
                    SetWindowPos(hwnd_, nullptr, centreX, centreY, 0, 0,
                                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                }
            }

            if (description.fullscreenMode != WindowFullscreenMode::Windowed)
                SetFullscreenMode(description.fullscreenMode);

            if (description.visible)
                ShowWindow(hwnd_, SW_SHOW);
        }
        catch (...)
        {
            if (fullscreenMode_ != WindowFullscreenMode::Windowed || exclusiveModeChanged_)
                LeaveFullscreen();
            RemovePropW(hwnd_, kOwnedWindowProperty);
            SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
            DestroyWindow(hwnd_);
            hwnd_ = nullptr;
            windowClass_.reset();
            throw;
        }
    }

    Win32Window::Win32Window(const HWND window, const WindowId id, Win32WindowHost& host, AdoptTag)
        : hwnd_(window)
        , id_(id)
        , host_(&host)
        , ownsWindow_(false)
        , mapper_(id, host)
    {
        if (hwnd_ == nullptr || IsWindow(hwnd_) == FALSE)
        {
            throw PlatformException("Win32Platform::AdoptWindow",
                                    "the supplied handle does not name a live window");
        }
    }

    Win32Window::~Win32Window()
    {
        destroying_ = true;

        if (hwnd_ != nullptr && IsWindow(hwnd_) != FALSE)
        {
            // Display-mode changes and pointer confinement are process-global. Undo them before
            // the window goes away, or the desktop is left in the game's resolution with the
            // cursor still clipped to a rectangle that no longer contains anything.
            if (fullscreenMode_ != WindowFullscreenMode::Windowed)
                LeaveFullscreen();
            if (rawPointerCapture_)
                SetRawPointerCapture(false);

            if (ownsWindow_)
            {
                RemovePropW(hwnd_, kOwnedWindowProperty);
                // Detach first: DestroyWindow dispatches WM_DESTROY/WM_NCDESTROY synchronously,
                // and the handler must not push events into a mapper whose owner is mid-destruction.
                SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
                DestroyWindow(hwnd_);
            }
        }

        if (host_ != nullptr)
            host_->OnWindowDestroyed(*this);
        hwnd_ = nullptr;
        windowClass_.reset();
    }

    void Win32Window::DetachHost()
    {
        host_ = nullptr;
        mapper_.DetachSink();
    }

    LRESULT CALLBACK Win32Window::StaticWindowProc(const HWND window, const UINT message,
                                                   const WPARAM wParam, const LPARAM lParam)
    {
        if (message == WM_NCCREATE)
        {
            // The first message that carries the creation parameters, and therefore the first
            // opportunity to associate the HWND with its owner. Everything before this -- and
            // Windows does send WM_GETMINMAXINFO before it -- must tolerate a null owner.
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            if (create != nullptr && create->lpCreateParams != nullptr)
            {
                auto* self = static_cast<Win32Window*>(create->lpCreateParams);
                self->hwnd_ = window;
                SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            }
            return DefWindowProcW(window, message, wParam, lParam);
        }

        auto* self =
            reinterpret_cast<Win32Window*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (self == nullptr)
            return DefWindowProcW(window, message, wParam, lParam);

        if (message == WM_NCDESTROY)
        {
            // The last message a window ever receives. Clearing the pointer here is what
            // guarantees no later message can reach a destroyed owner.
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            return DefWindowProcW(window, message, wParam, lParam);
        }

        return self->HandleMessage(message, wParam, lParam);
    }

    LRESULT Win32Window::HandleMessage(const UINT message, const WPARAM wParam, const LPARAM lParam)
    {
        switch (message)
        {
            case WM_ERASEBKGND:
                // Claimed, and deliberately not done: the renderer paints every pixel, and
                // letting the system erase first produces a visible flash on each resize.
                return 1;

            case WM_PAINT:
            {
                PAINTSTRUCT paint{};
                BeginPaint(hwnd_, &paint);
                EndPaint(hwnd_, &paint);
                mapper_.Translate(message, wParam, static_cast<std::int64_t>(lParam));
                // Already validated above, so the default handler must not run again.
                return 0;
            }

            case WM_GETMINMAXINFO:
            {
                if (minimumWidth_ <= 0 && minimumHeight_ <= 0 && maximumWidth_ <= 0 &&
                    maximumHeight_ <= 0)
                {
                    break;
                }

                auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
                if (limits == nullptr)
                    break;

                // The constraints are client-area sizes; WM_GETMINMAXINFO wants outer sizes.
                const LONG_PTR style = CurrentStyle();
                const LONG_PTR exStyle = CurrentExStyle();
                const unsigned int dpi = Win32DpiSupport::Get().GetWindowDpi(hwnd_);
                const auto toOuter = [&](const int width, const int height, POINT& point) {
                    RECT frame{0, 0, width, height};
                    (void) Win32DpiSupport::Get().AdjustWindowRect(
                        frame, static_cast<DWORD>(style), static_cast<DWORD>(exStyle), false, dpi);
                    point.x = frame.right - frame.left;
                    point.y = frame.bottom - frame.top;
                };

                if (minimumWidth_ > 0 && minimumHeight_ > 0)
                    toOuter(minimumWidth_, minimumHeight_, limits->ptMinTrackSize);
                if (maximumWidth_ > 0 && maximumHeight_ > 0)
                    toOuter(maximumWidth_, maximumHeight_, limits->ptMaxTrackSize);
                return 0;
            }

            case WM_DPICHANGED:
            {
                // Windows hands over the rectangle the window should occupy at the new scale.
                // Ignoring it leaves a window that is physically the same size and therefore
                // logically the wrong one, which is the visible symptom of "DPI is broken".
                const auto* suggested = reinterpret_cast<const RECT*>(lParam);
                if (suggested != nullptr)
                {
                    SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                                 suggested->right - suggested->left,
                                 suggested->bottom - suggested->top,
                                 SWP_NOZORDER | SWP_NOACTIVATE);
                }
                mapper_.Translate(message, wParam, static_cast<std::int64_t>(lParam));
                return 0;
            }

            case WM_INPUT:
                HandleRawInput(lParam);
                break;

            case WM_MOUSEMOVE:
                if (!trackingPointerLeave_)
                {
                    TRACKMOUSEEVENT tracking{};
                    tracking.cbSize = sizeof(tracking);
                    tracking.dwFlags = TME_LEAVE;
                    tracking.hwndTrack = hwnd_;
                    trackingPointerLeave_ = TrackMouseEvent(&tracking) != FALSE;
                }
                break;

            case WM_MOUSELEAVE:
                trackingPointerLeave_ = false;
                break;

            case WM_CLOSE:
                // Reported as a request and never acted on -- see Win32EventMapper. Returning 0
                // suppresses DefWindowProcW's DestroyWindow, which is the whole point.
                mapper_.Translate(message, wParam, static_cast<std::int64_t>(lParam));
                return 0;

            case WM_ENDSESSION:
                // The session really is ending; this is not a window's close button. wParam is
                // FALSE when a previous WM_QUERYENDSESSION was vetoed.
                if (wParam != FALSE && host_ != nullptr)
                    host_->OnQuitRequested();
                break;

            case WM_DESTROY:
                // Deliberately no PostQuitMessage: a process with several windows must not end
                // because one of them was destroyed. Process-scoped quit arrives through
                // WM_ENDSESSION or a WM_QUIT the host posted.
                break;

            default:
                break;
        }

        const bool handled =
            mapper_.Translate(message, wParam, static_cast<std::int64_t>(lParam));

        // Implicit capture keeps a drag alive once the pointer leaves the client area, which is
        // what makes a slider usable. Driven from the mapper's button bookkeeping so the
        // press/release accounting exists in exactly one place.
        if (message == WM_LBUTTONDOWN || message == WM_MBUTTONDOWN || message == WM_RBUTTONDOWN ||
            message == WM_XBUTTONDOWN || message == WM_LBUTTONUP || message == WM_MBUTTONUP ||
            message == WM_RBUTTONUP || message == WM_XBUTTONUP)
        {
            const bool wanted = mapper_.WantsPointerCapture();
            if (wanted && !pointerCaptured_ && !rawPointerCapture_)
            {
                SetCapture(hwnd_);
                pointerCaptured_ = true;
            }
            else if (!wanted && pointerCaptured_)
            {
                ReleaseCapture();
                pointerCaptured_ = false;
            }
        }

        if (handled)
        {
            // The X buttons are the one pointer family whose documented "handled" reply is TRUE
            // rather than zero.
            if (message == WM_XBUTTONDOWN || message == WM_XBUTTONUP ||
                message == WM_XBUTTONDBLCLK)
            {
                return TRUE;
            }
            return 0;
        }
        return DefWindowProcW(hwnd_, message, wParam, lParam);
    }

    void Win32Window::HandleRawInput(const LPARAM lParam)
    {
        if (!rawPointerCapture_ || host_ == nullptr)
            return;

        UINT size = 0;
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &size,
                            sizeof(RAWINPUTHEADER)) != 0 ||
            size == 0)
        {
            return;
        }

        std::vector<std::uint8_t> buffer(size);
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, buffer.data(), &size,
                            sizeof(RAWINPUTHEADER)) != size)
        {
            return;
        }

        const auto* input = reinterpret_cast<const RAWINPUT*>(buffer.data());
        if (input->header.dwType != RIM_TYPEMOUSE)
            return;

        if ((input->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) != 0)
        {
            // A tablet or remote-desktop pointer reports absolute positions in a normalised
            // space. Converting those into a displacement would need the previous absolute
            // sample, and a relative-mode consumer wants device motion, not screen motion -- so
            // the sample is dropped rather than converted into a plausible-looking lie.
            return;
        }

        if (input->data.mouse.lLastX != 0 || input->data.mouse.lLastY != 0)
            host_->OnRawPointerDelta(input->data.mouse.lLastX, input->data.mouse.lLastY);
    }

    bool Win32Window::SetRawPointerCapture(const bool enabled)
    {
        if (enabled == rawPointerCapture_)
            return true;

        RAWINPUTDEVICE device{};
        device.usUsagePage = 0x01; // Generic desktop controls
        device.usUsage = 0x02;     // Mouse
        device.dwFlags = enabled ? 0u : static_cast<DWORD>(RIDEV_REMOVE);
        device.hwndTarget = enabled ? hwnd_ : nullptr;

        if (RegisterRawInputDevices(&device, 1, sizeof(device)) == FALSE)
        {
            // Not a silent half-enabled state: the caller is told the mode was not entered, and
            // IPlatformMouse turns that into an explicit refusal.
            return false;
        }

        rawPointerCapture_ = enabled;
        if (enabled)
        {
            RECT client{};
            if (GetClientRect(hwnd_, &client) != FALSE)
            {
                POINT topLeft{client.left, client.top};
                POINT bottomRight{client.right, client.bottom};
                ClientToScreen(hwnd_, &topLeft);
                ClientToScreen(hwnd_, &bottomRight);
                const RECT screen{topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
                ClipCursor(&screen);
            }
            SetCapture(hwnd_);
            while (ShowCursor(FALSE) >= 0)
            {
            }
        }
        else
        {
            ClipCursor(nullptr);
            if (!pointerCaptured_)
                ReleaseCapture();
            while (ShowCursor(TRUE) < 0)
            {
            }
        }
        return true;
    }

    WindowId Win32Window::GetId() const
    {
        return id_;
    }

    std::uintptr_t Win32Window::GetWindowHandle() const
    {
        return reinterpret_cast<std::uintptr_t>(hwnd_);
    }

    NativeWindowHandle Win32Window::GetNativeHandle() const
    {
        NativeWindowHandle handle;
        handle.system = NativeWindowSystem::Win32;
        handle.window = static_cast<void*>(hwnd_);
        return handle;
    }

    std::string Win32Window::GetTitle() const
    {
        const int length = GetWindowTextLengthW(hwnd_);
        if (length <= 0)
            return {};
        std::wstring title(static_cast<std::size_t>(length) + 1, L'\0');
        const int written = GetWindowTextW(hwnd_, title.data(), length + 1);
        if (written <= 0)
            return {};
        title.resize(static_cast<std::size_t>(written));
        return ToUtf8(title);
    }

    void Win32Window::SetTitle(const std::string& title)
    {
        const std::wstring wide = ToWide(title);
        SetWindowTextW(hwnd_, wide.c_str());
    }

    WindowBounds Win32Window::GetClientBounds() const
    {
        WindowBounds bounds;
        RECT client{};
        if (GetClientRect(hwnd_, &client) == FALSE)
            return bounds;

        POINT origin{client.left, client.top};
        ClientToScreen(hwnd_, &origin);
        bounds.x = origin.x;
        bounds.y = origin.y;
        bounds.width = client.right - client.left;
        bounds.height = client.bottom - client.top;
        return bounds;
    }

    WindowSize Win32Window::GetPixelSize() const
    {
        // GetClientRect already answers in physical pixels for the awareness the host chose:
        // a per-monitor-aware process gets the true device pixels, and a system-aware one gets
        // the virtualised rectangle which IS the surface it draws into. Deriving the drawable
        // size by multiplying the client size by the scale would double-apply it.
        const WindowBounds bounds = GetClientBounds();
        WindowSize size;
        size.width = bounds.width;
        size.height = bounds.height;
        return size;
    }

    void Win32Window::SetSize(const int width, const int height)
    {
        ResizeClientArea(std::max(width, 1), std::max(height, 1));
    }

    void Win32Window::ResizeClientArea(const int width, const int height)
    {
        const LONG_PTR style = CurrentStyle();
        const LONG_PTR exStyle = CurrentExStyle();
        const unsigned int dpi = Win32DpiSupport::Get().GetWindowDpi(hwnd_);

        RECT frame{0, 0, width, height};
        (void) Win32DpiSupport::Get().AdjustWindowRect(frame, static_cast<DWORD>(style),
                                                       static_cast<DWORD>(exStyle), false, dpi);
        SetWindowPos(hwnd_, nullptr, 0, 0, frame.right - frame.left, frame.bottom - frame.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    float Win32Window::GetDisplayScale() const
    {
        return Win32DpiSupport::ToDisplayScale(Win32DpiSupport::Get().GetWindowDpi(hwnd_));
    }

    LONG_PTR Win32Window::CurrentStyle() const
    {
        return GetWindowLongPtrW(hwnd_, GWL_STYLE);
    }

    LONG_PTR Win32Window::CurrentExStyle() const
    {
        return GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    }

    void Win32Window::ApplyStyle(const LONG_PTR style, const LONG_PTR exStyle)
    {
        SetWindowLongPtrW(hwnd_, GWL_STYLE, style);
        SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, exStyle);
        // Without SWP_FRAMECHANGED the non-client area keeps its old size and the window is drawn
        // with a frame it no longer has.
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }

    bool Win32Window::IsResizable() const
    {
        return (CurrentStyle() & WS_THICKFRAME) != 0;
    }

    void Win32Window::SetResizable(const bool resizable)
    {
        if (fullscreenMode_ != WindowFullscreenMode::Windowed)
            return;
        ApplyStyle(WithResizable(CurrentStyle(), resizable), CurrentExStyle());
    }

    bool Win32Window::IsBorderless() const
    {
        return (CurrentStyle() & WS_CAPTION) == 0;
    }

    void Win32Window::SetBorderless(const bool borderless)
    {
        if (fullscreenMode_ != WindowFullscreenMode::Windowed)
            return;

        LONG_PTR style = CurrentStyle();
        if (borderless)
        {
            style = Win32FullscreenState::ToFullscreenStyle(style);
        }
        else
        {
            style &= ~static_cast<LONG_PTR>(WS_POPUP);
            style |= WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        }
        ApplyStyle(style, CurrentExStyle());
    }

    void Win32Window::EnterBorderlessFullscreen()
    {
        windowedState_.Capture(CurrentStyle(), CurrentExStyle(), ReadPlacement(hwnd_));

        RECT monitor{};
        if (!MonitorRect(hwnd_, monitor))
            throw PlatformException("Win32Window::SetFullscreenMode", DescribeLastError());

        SetWindowLongPtrW(hwnd_, GWL_STYLE, Win32FullscreenState::ToFullscreenStyle(CurrentStyle()));
        SetWindowLongPtrW(hwnd_, GWL_EXSTYLE,
                          Win32FullscreenState::ToFullscreenExStyle(CurrentExStyle()));
        SetWindowPos(hwnd_, HWND_TOP, monitor.left, monitor.top, monitor.right - monitor.left,
                     monitor.bottom - monitor.top, SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }

    void Win32Window::EnterExclusiveFullscreen()
    {
        EnterBorderlessFullscreen();

        // The display mode change is what separates exclusive from borderless. It is attempted
        // for the window's own monitor, and a refusal is reported rather than silently downgraded
        // into the borderless mode the caller did not ask for.
        const HMONITOR monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        if (monitor == nullptr || GetMonitorInfoW(monitor, &info) == FALSE)
            return;

        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        if (EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode) == FALSE)
            return;

        const RECT bounds = info.rcMonitor;
        mode.dmPelsWidth = static_cast<DWORD>(bounds.right - bounds.left);
        mode.dmPelsHeight = static_cast<DWORD>(bounds.bottom - bounds.top);
        mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;

        const LONG result =
            ChangeDisplaySettingsExW(info.szDevice, &mode, nullptr, CDS_FULLSCREEN, nullptr);
        exclusiveModeChanged_ = result == DISP_CHANGE_SUCCESSFUL;
    }

    void Win32Window::LeaveFullscreen()
    {
        if (exclusiveModeChanged_)
        {
            // A null DEVMODE restores the mode recorded in the registry, which is the desktop's
            // own. Leaving this out is how a crashed game leaves the desktop at 640x480.
            ChangeDisplaySettingsExW(nullptr, nullptr, nullptr, 0, nullptr);
            exclusiveModeChanged_ = false;
        }

        LONG_PTR style = 0;
        LONG_PTR exStyle = 0;
        WINDOWPLACEMENT placement{};
        if (!windowedState_.Restore(style, exStyle, placement))
            return;

        SetWindowLongPtrW(hwnd_, GWL_STYLE, style);
        SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, exStyle);
        SetWindowPlacement(hwnd_, &placement);
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }

    void Win32Window::SetFullscreenMode(const WindowFullscreenMode mode)
    {
        if (mode == fullscreenMode_)
            return;

        if (mode == WindowFullscreenMode::Windowed)
        {
            LeaveFullscreen();
            fullscreenMode_ = WindowFullscreenMode::Windowed;
            return;
        }

        // A direct borderless <-> exclusive transition goes through windowed, so the captured
        // appearance is always the genuine one and the display mode is never left changed under a
        // borderless window.
        if (fullscreenMode_ != WindowFullscreenMode::Windowed)
            LeaveFullscreen();

        if (mode == WindowFullscreenMode::BorderlessFullscreen)
            EnterBorderlessFullscreen();
        else
            EnterExclusiveFullscreen();
        fullscreenMode_ = mode;
    }

    WindowFullscreenMode Win32Window::GetFullscreenMode() const
    {
        return fullscreenMode_;
    }

    void Win32Window::Show()
    {
        ShowWindow(hwnd_, SW_SHOW);
    }

    void Win32Window::Hide()
    {
        ShowWindow(hwnd_, SW_HIDE);
    }

    void Win32Window::Minimize()
    {
        ShowWindow(hwnd_, SW_MINIMIZE);
    }

    void Win32Window::Maximize()
    {
        ShowWindow(hwnd_, SW_MAXIMIZE);
    }

    void Win32Window::Restore()
    {
        ShowWindow(hwnd_, SW_RESTORE);
    }

    void Win32Window::Sync()
    {
        // SetWindowPos and ShowWindow post messages this window has not processed yet, so a
        // GetClientBounds immediately after a SetSize can report the old size. Draining this
        // window's own queue is what makes the requested state observable -- and it is scoped to
        // this window so it cannot consume another window's input.
        MSG message;
        while (PeekMessageW(&message, hwnd_, 0, 0, PM_REMOVE) != FALSE)
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    bool Win32Window::HasFocus() const
    {
        return GetFocus() == hwnd_ || GetForegroundWindow() == hwnd_;
    }

    bool Win32Window::IsMinimized() const
    {
        return IsIconic(hwnd_) != FALSE;
    }

    std::string Win32Window::GetDisplayName() const
    {
        const HMONITOR monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
        if (monitor == nullptr)
            return {};

        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        if (GetMonitorInfoW(monitor, &info) == FALSE)
            return {};
        return ToUtf8(std::wstring(info.szDevice));
    }

} // namespace CNA::Platform::Win32
