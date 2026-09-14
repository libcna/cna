// SPDX-License-Identifier: MS-PL

#include "Win32Platform.hpp"

#include "Win32Error.hpp"

#include "CNA/Platform/PlatformException.hpp"

#include <algorithm>
#include <utility>

namespace CNA::Platform::Win32 {

    namespace {

        const std::string kName = "Win32";

        std::uint64_t ReadPerformanceCounter()
        {
            LARGE_INTEGER counter{};
            QueryPerformanceCounter(&counter);
            return static_cast<std::uint64_t>(counter.QuadPart);
        }

        std::uint64_t ReadPerformanceFrequency()
        {
            LARGE_INTEGER frequency{};
            if (QueryPerformanceFrequency(&frequency) == FALSE || frequency.QuadPart <= 0)
            {
                // Documented to always succeed on Windows XP and later, but the contract says
                // "never zero" and a division by it is a crash rather than a slow frame.
                return 1;
            }
            return static_cast<std::uint64_t>(frequency.QuadPart);
        }

    } // namespace

    Win32Platform::Win32Platform()
        : displays_(*this)
        , dialogs_(*this)
        , mouse_(*this)
        , textInput_(*this)
        , glContext_(*this)
        , vulkanSurface_(*this)
    {
        counterFrequency_ = ReadPerformanceFrequency();
        createdAtCounter_ = ReadPerformanceCounter();
    }

    Win32Platform::~Win32Platform()
    {
        // Windows the application still owns hold a back-pointer to this platform. Clearing it is
        // what keeps a window that outlives its platform from pushing into a destroyed queue --
        // the contract permits that order, and the wrapper simply stops producing events.
        for (auto& entry : windows_)
        {
            if (entry.second != nullptr)
                entry.second->DetachHost();
        }
        for (Win32Window* const borrowed : adopted_)
        {
            if (borrowed != nullptr)
                borrowed->DetachHost();
        }
        windows_.clear();
        adopted_.clear();
    }

    const std::string& Win32Platform::GetName() const
    {
        return kName;
    }

    const std::string& Win32Platform::GetPlatformName() const
    {
        return kName;
    }

    PlatformCapabilities Win32Platform::GetCapabilities() const
    {
        PlatformCapabilities capabilities;
        capabilities.multipleWindows = true;
        capabilities.highDpi = true;
        capabilities.multipleDisplays = true;
        capabilities.borderlessFullscreen = true;
        capabilities.nativeWindowHandle = true;
        capabilities.surfacePresentation = true;
        capabilities.openGlContext = true;
        // The one capability decided by the host rather than by this code: without a Vulkan
        // loader there is genuinely no surface to create, and saying so is what lets a renderer
        // refuse before it tries.
        capabilities.vulkanSurface = vulkanSurface_.IsAvailable();
        capabilities.clipboard = true;
        capabilities.textInput = true;
        capabilities.exactKeyboardState = true;
        capabilities.pixelAccurateMouse = true;
        capabilities.relativeMouse = true;
        capabilities.cursorShapes = true;
        capabilities.globalPointer = true;
        capabilities.inputDeviceEnumeration = true;
        capabilities.powerInfo = true;
        capabilities.messageBox = true;
        capabilities.nativeFileDialog = true;

        // Deliberately false, each with its remaining work recorded in plans/plan_win32.md
        // section 15: ime, gamepad, joystick, gamepadRumble, gamepadSensors, haptics, sensors,
        // tray, camera. A capability reported true and backed by a stub is worse than an honest
        // false, because a caller branches on it.
        return capabilities;
    }

    PlatformCapabilities Win32Platform::GetPlatformCapabilities() const
    {
        return GetCapabilities();
    }

    // --- subsystems --------------------------------------------------------------------------

    void Win32Platform::AcquireSubsystem(const PlatformSubsystem subsystem)
    {
        switch (subsystem)
        {
            case PlatformSubsystem::Video:
            case PlatformSubsystem::Audio:
                // Neither needs initialising on Win32: a process can create a window as soon as
                // it has a message queue, and audio is a separate CNA axis entirely
                // (CNA_AUDIO_PLATFORM). The count is still kept, because the contract's
                // refcounting semantics are what callers balance against.
                break;

            case PlatformSubsystem::Gamepad:
            case PlatformSubsystem::Haptic:
            case PlatformSubsystem::Sensor:
                // Acquisition is not refused: the contract distinguishes "this platform has no
                // such subsystem at all" from "nothing is attached", and a future XInput backend
                // fits behind exactly this call without changing what a caller does today. The
                // matching capabilities are false, so nothing believes a device exists.
                break;
        }
        ++ownedRefCounts_[subsystem];
    }

    void Win32Platform::ReleaseSubsystem(const PlatformSubsystem subsystem)
    {
        const auto found = ownedRefCounts_.find(subsystem);
        if (found == ownedRefCounts_.end() || found->second <= 0)
        {
            // Tolerated by design: GraphicsDevice::Dispose releases video unconditionally, and
            // cleanup after a partial initialization legitimately runs without a matching
            // acquisition.
            return;
        }
        --found->second;
    }

    bool Win32Platform::IsSubsystemInitialized(const PlatformSubsystem subsystem) const
    {
        const auto found = ownedRefCounts_.find(subsystem);
        return found != ownedRefCounts_.end() && found->second > 0;
    }

    // --- windows -----------------------------------------------------------------------------

    std::unique_ptr<IPlatformWindow> Win32Platform::CreateWindow(
        const WindowDescription& description)
    {
        // No Video-subsystem precondition, deliberately. On Win32 a process can create a window
        // as soon as it has a message queue, so demanding an acquisition first would invent a
        // failure mode the platform does not have -- and neither SDL3 nor Headless imposes one
        // either, so a game written against them would start failing here for no reason.
        const WindowId id = nextWindowId_++;
        Win32WindowHost& host = *this;
        auto window = std::make_unique<Win32Window>(description, id, host);
        windows_[id] = window.get();
        return window;
    }

    std::unique_ptr<IPlatformWindow> Win32Platform::AdoptWindow(const WindowId windowId)
    {
        const auto found = windows_.find(windowId);
        if (found == windows_.end() || found->second == nullptr)
        {
            throw PlatformException("Win32Platform::AdoptWindow",
                                    "the id does not name a live window of this platform");
        }
        // Kept out of the id registry -- that maps an id to the one wrapper that OWNS the window,
        // and a second entry under the same id would make destroying the borrowed wrapper
        // unregister the real one. Tracked separately all the same, so a platform destroyed
        // before its borrowed wrappers can clear their back-pointers.
        Win32WindowHost& host = *this;
        auto borrowed = std::make_unique<Win32Window>(found->second->GetHwnd(), windowId, host,
                                                      Win32Window::AdoptTag{});
        adopted_.push_back(borrowed.get());
        return borrowed;
    }

    std::unique_ptr<IPlatformWindow> Win32Platform::AdoptWindowHandle(const std::uintptr_t handle)
    {
        if (handle == 0)
            throw PlatformException("Win32Platform::AdoptWindowHandle", "the token is zero");

        const auto hwnd = reinterpret_cast<HWND>(handle);
        if (IsWindow(hwnd) == FALSE)
        {
            throw PlatformException("Win32Platform::AdoptWindowHandle",
                                    "the token does not name a live window");
        }

        // A window this platform already owns keeps its established id, so events the real
        // wrapper produces and the borrowed one's GetId() agree.
        WindowId id = 0;
        for (const auto& [existingId, window] : windows_)
        {
            if (window != nullptr && window->GetHwnd() == hwnd)
            {
                id = existingId;
                break;
            }
        }
        if (id == 0)
            id = nextWindowId_++;

        Win32WindowHost& host = *this;
        auto borrowed = std::make_unique<Win32Window>(hwnd, id, host, Win32Window::AdoptTag{});
        adopted_.push_back(borrowed.get());
        return borrowed;
    }

    void Win32Platform::OnWindowDestroyed(Win32Window& window)
    {
        std::erase(adopted_, &window);

        // Matched on identity, not on id. An adopted wrapper shares the id of the window it
        // borrows, so erasing by id would unregister the owner the moment a borrowed wrapper went
        // out of scope -- after which every service resolving that id would find nothing.
        const auto found = windows_.find(window.GetId());
        if (found != windows_.end() && found->second == &window)
            windows_.erase(found);
    }

    // --- events ------------------------------------------------------------------------------

    void Win32Platform::Push(PlatformEvent event)
    {
        pending_.push_back(std::move(event));
    }

    void Win32Platform::OnRawPointerDelta(const int deltaX, const int deltaY)
    {
        mouse_.AccumulateRawDelta(deltaX, deltaY);
    }

    void Win32Platform::OnQuitRequested()
    {
        pending_.push_back(QuitEvent{});
    }

    void Win32Platform::PollEvents(std::vector<PlatformEvent>& destination)
    {
        destination.clear();

        // PeekMessageW with a null window drains this thread's whole queue, including the
        // messages that belong to windows other CNA components created. DispatchMessageW calls
        // each window procedure synchronously, which is where the translation happens and what
        // fills pending_.
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE)
        {
            if (message.message == WM_QUIT)
            {
                // Genuinely process-scoped: something posted WM_QUIT to this thread. A window
                // being closed does not produce it -- see Win32Window's WM_DESTROY handling.
                pending_.push_back(QuitEvent{});
                continue;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        // Moved rather than copied, and into the caller's existing capacity: the batch belongs to
        // the caller precisely so a steady-state frame allocates nothing.
        destination.reserve(pending_.size());
        for (PlatformEvent& event : pending_)
            destination.push_back(std::move(event));
        pending_.clear();
    }

    // --- timing ------------------------------------------------------------------------------

    std::uint64_t Win32Platform::GetPerformanceCounter() const
    {
        return ReadPerformanceCounter();
    }

    std::uint64_t Win32Platform::GetPerformanceFrequency() const
    {
        return counterFrequency_;
    }

    std::uint64_t Win32Platform::GetTicksMilliseconds() const
    {
        // Derived from the same counter as GetPerformanceCounter() rather than from GetTickCount64,
        // so the two can never disagree about how much time passed.
        //
        // Split into whole seconds plus a remainder before multiplying: the direct form
        // (elapsed * 1000 / frequency) overflows 64 bits after about 21 days at a 10 MHz counter,
        // and a game left running over a weekend would see time jump backwards.
        const std::uint64_t elapsed = ReadPerformanceCounter() - createdAtCounter_;
        const std::uint64_t seconds = elapsed / counterFrequency_;
        const std::uint64_t remainder = elapsed % counterFrequency_;
        return seconds * 1000ull + (remainder * 1000ull) / counterFrequency_;
    }

    void Win32Platform::Delay(const std::uint32_t milliseconds)
    {
        // ::Sleep and deliberately not timeBeginPeriod: the timer resolution is process-global
        // state the host owns, and raising it here would change the scheduling of every thread in
        // the application for as long as CNA is loaded.
        ::Sleep(milliseconds);
    }

    // --- services ----------------------------------------------------------------------------

    IPlatformKeyboard* Win32Platform::GetKeyboard() { return &keyboard_; }
    IPlatformMouse* Win32Platform::GetMouse() { return &mouse_; }
    IPlatformGamepad* Win32Platform::GetGamepad() { return nullptr; }
    IPlatformJoystick* Win32Platform::GetJoystick() { return nullptr; }
    IPlatformTextInput* Win32Platform::GetTextInput() { return &textInput_; }
    IPlatformSensors* Win32Platform::GetSensors() { return nullptr; }
    IPlatformHaptics* Win32Platform::GetHaptics() { return nullptr; }
    IPlatformInputDevices* Win32Platform::GetInputDevices() { return &inputDevices_; }
    IPlatformClipboard* Win32Platform::GetClipboard() { return &clipboard_; }
    IPlatformDisplays* Win32Platform::GetDisplays() { return &displays_; }
    IPlatformDialogs* Win32Platform::GetDialogs() { return &dialogs_; }
    IPlatformTray* Win32Platform::GetTray() { return nullptr; }
    IPlatformCameraProvider* Win32Platform::GetCamera() { return nullptr; }
    IPlatformFileSystem* Win32Platform::GetFileSystem() { return &fileSystem_; }
    IPlatformSystemInfo* Win32Platform::GetSystemInfo() { return &systemInfo_; }
    IPlatformGlContext* Win32Platform::GetGlContext() { return &glContext_; }

    IPlatformVulkanSurface* Win32Platform::GetVulkanSurface()
    {
        // Null exactly when the capability is false, which is the contract's central rule.
        return vulkanSurface_.IsAvailable() ? &vulkanSurface_ : nullptr;
    }

    std::unique_ptr<IPlatformSurfacePresenter> Win32Platform::CreateSurfacePresenter(
        IPlatformWindow& window)
    {
        auto* native = dynamic_cast<Win32Window*>(&window);
        if (native == nullptr)
        {
            throw PlatformException("Win32Platform::CreateSurfacePresenter",
                                    "the window was not created by this platform");
        }
        return std::make_unique<Win32SurfacePresenter>(*native);
    }

    // --- platform access ---------------------------------------------------------------------

    Win32Window* Win32Platform::GetFocusedWindow()
    {
        const HWND focused = GetForegroundWindow();
        if (focused == nullptr)
            return nullptr;
        for (const auto& [id, window] : windows_)
        {
            if (window != nullptr && window->GetHwnd() == focused)
                return window;
        }
        return nullptr;
    }

    Win32Window* Win32Platform::FindWindow(const WindowId window)
    {
        const auto found = windows_.find(window);
        return found != windows_.end() ? found->second : nullptr;
    }

    Win32Window* Win32Platform::GetAnyWindow()
    {
        for (const auto& [id, window] : windows_)
        {
            if (window != nullptr)
                return window;
        }
        return nullptr;
    }

} // namespace CNA::Platform::Win32
