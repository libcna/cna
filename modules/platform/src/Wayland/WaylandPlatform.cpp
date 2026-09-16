// SPDX-License-Identifier: MS-PL

#include "WaylandPlatform.hpp"

#include "CNA/Platform/PlatformException.hpp"
#include "../Common/StandardSystemInfo.hpp"
#include "../Freedesktop/DesktopPortal.hpp"
#include "../Freedesktop/ScreenSaverBus.hpp"
#include "../Posix/MonotonicClock.hpp"
#include "WaylandDataDevice.hpp"
#include "WaylandDesktop.hpp"
#include "WaylandFrame.hpp"
#include "WaylandGraphicsServices.hpp"
#include "WaylandKeyboard.hpp"
#include "WaylandMouse.hpp"
#include "WaylandOutputs.hpp"
#include "WaylandSeat.hpp"
#include "WaylandTextInput.hpp"
#include "WaylandTablet.hpp"
#include "WaylandTouch.hpp"

#ifdef CNA_PLATFORM_HAVE_EVDEV
#include "../Linux/EvdevControllers.hpp"
#include "../Linux/EvdevHaptics.hpp"
#include "../Linux/LinuxSystemInfo.hpp"
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace CNA::Platform::Wayland {

    namespace {

        /// How long the startup exchange with the compositor may take (D-3). A compositor that
        /// needs longer than this to list its globals is stuck.
        constexpr std::chrono::milliseconds kStartupTimeout{5000};

    } // namespace

#ifdef CNA_PLATFORM_HAVE_EVDEV
    struct WaylandPlatform::Controllers
    {
        Linux::EvdevControllerHub hub;
        Linux::EvdevGamepad gamepad{hub};
        Linux::EvdevJoystick joystick{hub};
        Linux::EvdevHaptics haptics{[this](const DeviceId id) {
            const Linux::EvdevControllerHub::Controller* controller = hub.FindById(id);
            return controller != nullptr && controller->device != nullptr ? controller->device->GetPath()
                                                                           : std::string();
        }};
    };
#else
    struct WaylandPlatform::Controllers
    {
    };
#endif

    void WaylandPlatform::ControllersDeleter::operator()(Controllers* controllers) const
    {
        delete controllers;
    }

    void WaylandPlatform::PortalDeleter::operator()(Freedesktop::DesktopPortal* portal) const
    {
#if defined(CNA_PLATFORM_HAVE_DBUS)
        delete portal;
#else
        (void) portal;
#endif
    }

    /// A non-owning view of a window the platform owns, for AdoptWindow: forwarding to the live
    /// object means the two views cannot disagree, and the view's destructor destroys nothing.
    class WaylandPlatform::BorrowedWindow final : public IPlatformWindow
    {
    public:
        explicit BorrowedWindow(WaylandWindow& target) : target_(target) {}

        [[nodiscard]] WindowId GetId() const override { return target_.GetId(); }
        [[nodiscard]] NativeWindowHandle GetNativeHandle() const override { return target_.GetNativeHandle(); }
        [[nodiscard]] std::string GetTitle() const override { return target_.GetTitle(); }
        void SetTitle(const std::string& title) override { target_.SetTitle(title); }
        [[nodiscard]] WindowBounds GetClientBounds() const override { return target_.GetClientBounds(); }
        [[nodiscard]] WindowSize GetPixelSize() const override { return target_.GetPixelSize(); }
        void SetSize(const int width, const int height) override { target_.SetSize(width, height); }
        [[nodiscard]] float GetDisplayScale() const override { return target_.GetDisplayScale(); }
        [[nodiscard]] bool IsResizable() const override { return target_.IsResizable(); }
        void SetResizable(const bool resizable) override { target_.SetResizable(resizable); }
        [[nodiscard]] bool IsBorderless() const override { return target_.IsBorderless(); }
        void SetBorderless(const bool borderless) override { target_.SetBorderless(borderless); }
        void SetFullscreenMode(const WindowFullscreenMode mode) override { target_.SetFullscreenMode(mode); }
        [[nodiscard]] WindowFullscreenMode GetFullscreenMode() const override { return target_.GetFullscreenMode(); }
        void Show() override { target_.Show(); }
        void Hide() override { target_.Hide(); }
        void Minimize() override { target_.Minimize(); }
        void Maximize() override { target_.Maximize(); }
        void Restore() override { target_.Restore(); }
        void Sync() override { target_.Sync(); }
        [[nodiscard]] bool HasFocus() const override { return target_.HasFocus(); }
        [[nodiscard]] bool IsMinimized() const override { return target_.IsMinimized(); }
        [[nodiscard]] std::string GetDisplayName() const override { return target_.GetDisplayName(); }

    private:
        WaylandWindow& target_;
    };

    WaylandPlatform::WaylandPlatform()
    {
#if defined(CNA_PLATFORM_HAVE_DBUS)
        // The desktop portal is the session bus's, not the compositor's: looked for once, without
        // starting it (shared with X11, src/Freedesktop/).
        portal_.reset(Freedesktop::DesktopPortal::Connect().release());
#endif
#ifdef CNA_PLATFORM_HAVE_EVDEV
        systemInfo_ = std::make_unique<Linux::LinuxSystemInfo>([this](const std::string& url) {
#if defined(CNA_PLATFORM_HAVE_DBUS)
            return portal_ != nullptr && portal_->OpenUri(url, std::string());
#else
            (void) url;
            return false;
#endif
        });
#else
        systemInfo_ = std::make_unique<Common::StandardSystemInfo>();
#endif

        // The input services exist before the connection: the compositor announces its seats,
        // and their devices, during the connection's own startup exchange.
        CreateServices();
        try
        {
            connection_ = std::make_unique<WaylandConnection>(static_cast<WaylandRegistryObserver&>(*this), kStartupTimeout);
        }
        catch (const PlatformException& error)
        {
            // Recorded, not propagated (D-3): a process with no compositor still needs a platform
            // for its filesystem and host services.
            connectionError_ = error.GetDetail().empty() ? std::string(error.what()) : error.GetDetail();
        }
        if (connection_ != nullptr)
        {
            const WaylandGlobals& globals = connection_->GetGlobals();
            for (const auto& seat : seats_)
            {
                AttachSeatServices(*seat);
            }
#if defined(CNA_WAYLAND_HAVE_XDG_OUTPUT)
            for (const auto& output : outputs_)
            {
                output->AttachXdgOutput(globals.xdgOutputManager);
            }
#endif
            (void) globals;
            displays_ = std::make_unique<WaylandDisplays>(WaylandDisplays::Source{
                [this] { return GetOutputs(); },
                [this](const IPlatformWindow& window) -> const WaylandOutput* {
                    const WaylandWindow* target = FindWindow(window.GetId());
                    return target != nullptr ? target->GetCurrentOutput() : nullptr;
                },
                [this] { return screenSaverEnabled_; },
                [this](const bool enabled) { SetScreenSaverEnabled(enabled); },
            });
            glContext_ = std::make_unique<WaylandGlContext>(*connection_);
#if defined(CNA_WAYLAND_HAVE_VULKAN_HEADERS)
            vulkanSurface_ = std::make_unique<WaylandVulkanSurface>(*connection_);
#endif
            dialogs_ = std::make_unique<WaylandDialogs>(*connection_, portal_.get(),
                                                        [this](const IPlatformWindow* window) -> WaylandWindow* {
                                                            return window != nullptr ? FindWindow(window->GetId()) : nullptr;
                                                        });
            if (!dialogs_->IsUseful())
            {
                dialogs_.reset();
            }
            idleInhibitor_ = std::make_unique<WaylandIdleInhibitor>(*connection_);
            // xdg-output's logical geometry for the outputs above, before anyone asks.
            (void) connection_->Roundtrip(kStartupTimeout);
        }
#ifdef CNA_PLATFORM_HAVE_EVDEV
        // Settled now because the capability set is; nothing is opened until the first question
        // about controllers (as X11 does).
        std::error_code error;
        if (std::filesystem::is_directory("/dev/input", error))
        {
            controllers_.reset(new Controllers());
        }
#endif
        inputDevices_ = std::make_unique<WaylandInputDevices>(
            [this] {
                std::vector<const WaylandSeat*> seats;
                for (const auto& seat : seats_) { seats.push_back(seat.get()); }
                return seats;
            },
            [this](const InputDeviceKind kind) { return ControllerDevices(kind); },
            [this] { return tablet_ != nullptr ? tablet_->GetToolNames() : std::vector<std::string>(); });
        capabilities_ = ComputeCapabilities();
    }

    WaylandPlatform::~WaylandPlatform()
    {
        // Windows the application still holds outlived the platform, which the contract does not
        // allow; their proxies are destroyed now, while the display exists, and the wrappers left
        // inert.
        for (const auto& [id, window] : std::map<WindowId, WaylandWindow*>(windows_))
        {
            (void) id;
            if (window != nullptr)
            {
                OnWindowDestroyed(*window);
                window->Abandon();
            }
        }
        windows_.clear();
        screenSaverBus_.reset();
        // The connection's destructor calls OnDisconnecting, which destroys every proxy the
        // services made, while the services still exist to be asked.
        connection_.reset();
        dialogs_.reset();
        inputDevices_.reset();
        textInput_.reset();
        tablet_.reset();
        touch_.reset();
        mouse_.reset();
        keyboard_.reset();
        // Closing the nodes stops any rumble still playing.
        controllers_.reset();
    }

    const std::string& WaylandPlatform::GetName() const
    {
        static const std::string name = "Wayland";
        return name;
    }

    void WaylandPlatform::CreateServices()
    {
        keyboard_ = std::make_unique<WaylandKeyboard>(WaylandKeyboard::Host{
            [this](PlatformEvent event) { PostEvent(std::move(event)); },
            [this](const WindowId window) { return textInput_ != nullptr && textInput_->IsActive(window); },
            [this](wl_seat* seat, const std::uint32_t serial) { RecordSerial(seat, serial); },
            [this](const WindowId window, const bool focused) {
                if (WaylandWindow* target = FindWindow(window))
                {
                    target->OnKeyboardFocus(focused);
                }
                if (dataDevices_ != nullptr)
                {
                    dataDevices_->OnKeyboardFocus(window, focused);
                }
            },
        });
        keyboard_->SetSurfaceResolver([this](wl_surface* surface) { return ResolveSurface(surface); });
        mouse_ = std::make_unique<WaylandMouse>(WaylandMouse::Host{
            [this](PlatformEvent event) { PostEvent(std::move(event)); },
            [this](wl_surface* surface) { return ResolveSurface(surface); },
            [this](const WindowId window) -> wl_surface* {
                const WaylandWindow* target = FindWindow(window);
                return target != nullptr ? target->GetSurface() : nullptr;
            },
            [this](wl_seat* seat, const std::uint32_t serial) { RecordSerial(seat, serial); },
            [this](const WindowId window) {
                if (WaylandWindow* target = FindWindow(window)) { target->CommitState(); }
            },
            [this]() -> WaylandConnection& { return *connection_; },
            [this](wl_surface* surface, const int kind, const double x, const double y, const std::uint32_t button,
                   const bool pressed, const std::uint32_t serial, wl_pointer* pointer) {
                const auto found = frameSurfaces_.find(surface);
                if (found == frameSurfaces_.end())
                {
                    return false;
                }
                found->second->OnPointer(surface, kind, x, y, button, pressed, serial, pointer);
                return true;
            },
        });
        touch_ = std::make_unique<WaylandTouch>(WaylandTouch::Host{
            [this](PlatformEvent event) { PostEvent(std::move(event)); },
            [this](wl_surface* surface) { return ResolveSurface(surface); },
            [this](const WindowId window, int& width, int& height) {
                if (const WaylandWindow* target = FindWindow(window))
                {
                    const WindowBounds bounds = target->GetClientBounds();
                    width = bounds.width;
                    height = bounds.height;
                }
            },
            [this](wl_seat* seat, const std::uint32_t serial) { RecordSerial(seat, serial); },
        });
        // A pen is a touch (WAYLAND-0059): the tablets report through the same host the
        // touchscreens do, and a game that reads `TouchPanel` cannot tell them apart.
        tablet_ = std::make_unique<WaylandTablet>(WaylandTablet::Host{
            [this](PlatformEvent event) { PostEvent(std::move(event)); },
            [this](wl_surface* surface) { return ResolveSurface(surface); },
            [this](const WindowId window, int& width, int& height) {
                if (const WaylandWindow* target = FindWindow(window))
                {
                    const WindowBounds bounds = target->GetClientBounds();
                    width = bounds.width;
                    height = bounds.height;
                }
            },
            [this](wl_seat* seat, const std::uint32_t serial) { RecordSerial(seat, serial); },
        });
        textInput_ = std::make_unique<WaylandTextInput>(WaylandTextInput::Host{
            [this](PlatformEvent event) { PostEvent(std::move(event)); },
            [this](wl_surface* surface) { return ResolveSurface(surface); },
            [this](const WindowId window) -> wl_surface* {
                const WaylandWindow* target = FindWindow(window);
                return target != nullptr ? target->GetSurface() : nullptr;
            },
            [this](const WindowId window) { return FindWindow(window) != nullptr; },
            [this] {
                if (connection_ != nullptr) { connection_->Flush(); }
            },
        });
    }

    void WaylandPlatform::OnSeatAnnounced(wl_registry* registry, const std::uint32_t name, const std::uint32_t version)
    {
        WaylandSeat::Devices devices;
        devices.keyboardAdded = [this](wl_keyboard* keyboard, wl_seat* seat) { keyboard_->Attach(keyboard, seat); };
        devices.keyboardRemoved = [this](wl_keyboard* keyboard) { keyboard_->Detach(keyboard); };
        devices.pointerAdded = [this](wl_pointer* pointer, wl_seat* seat) { mouse_->Attach(pointer, seat); };
        devices.pointerRemoved = [this](wl_pointer* pointer) { mouse_->Detach(pointer); };
        devices.touchAdded = [this, name](wl_touch* touch, wl_seat* seat) { touch_->Attach(touch, seat, name); };
        devices.touchRemoved = [this](wl_touch* touch) { touch_->Detach(touch); };
        devices.seatReady = [this](WaylandSeat& seat) {
            // A seat announced after startup gets its per-seat objects now; the ones present at
            // startup get them once the connection exists (see the constructor).
            if (connection_ != nullptr)
            {
                AttachSeatServices(seat);
            }
        };
        seats_.push_back(std::make_unique<WaylandSeat>(registry, name, version, std::move(devices)));
    }

    void WaylandPlatform::AttachSeatServices(WaylandSeat& seat)
    {
        const WaylandGlobals& globals = connection_->GetGlobals();
#if defined(CNA_WAYLAND_HAVE_TEXT_INPUT_V3)
        textInput_->AttachSeat(globals.textInputManager, seat.GetProxy());
#endif
#if defined(CNA_WAYLAND_HAVE_TABLET)
        tablet_->AttachSeat(globals.tabletManager, seat.GetProxy(), seat.GetGlobalName());
#endif
        if (dataDevices_ == nullptr && globals.dataDeviceManager != nullptr)
        {
            dataDevices_ = std::make_unique<WaylandDataDevices>(
                *connection_, WaylandDataDevices::Host{
                                  [this](PlatformEvent event) { PostEvent(std::move(event)); },
                                  [this](wl_surface* surface) { return ResolveSurface(surface); },
                                  [this](wl_seat*& seat) { return GetLatestInputSerial(seat); },
                              });
        }
        if (dataDevices_ != nullptr)
        {
            dataDevices_->AttachSeat(seat.GetProxy());
        }
        (void) globals;
    }

    void WaylandPlatform::OnOutputAnnounced(wl_registry* registry, const std::uint32_t name, const std::uint32_t version)
    {
        auto output = std::make_unique<WaylandOutput>(registry, name, version, [this] {
            // An output's scale or geometry changed: every window on it may need a new scale (the
            // integer fallback reads outputs), and the display list is different.
            for (const auto& [id, window] : windows_)
            {
                (void) id;
                if (window != nullptr)
                {
                    window->RefreshOutputs();
                }
            }
        });
#if defined(CNA_WAYLAND_HAVE_XDG_OUTPUT)
        if (connection_ != nullptr)
        {
            output->AttachXdgOutput(connection_->GetGlobals().xdgOutputManager);
        }
#endif
        outputs_.push_back(std::move(output));
    }

    void WaylandPlatform::OnGlobalRemoved(const std::uint32_t name)
    {
        for (auto it = outputs_.begin(); it != outputs_.end(); ++it)
        {
            if ((*it)->GetGlobalName() != name)
            {
                continue;
            }
            // Every window forgets the output before its proxy goes: a later wl_surface.enter can
            // never name it again, but the windows still hold the pointer from the last one.
            for (const auto& [id, window] : windows_)
            {
                (void) id;
                if (window != nullptr)
                {
                    window->ForgetOutput((*it)->GetProxy());
                }
            }
            outputs_.erase(it);
            return;
        }
        for (auto it = seats_.begin(); it != seats_.end(); ++it)
        {
            if ((*it)->GetGlobalName() != name)
            {
                continue;
            }
            wl_seat* seat = (*it)->GetProxy();
            textInput_->DetachSeat(seat);
            if (tablet_ != nullptr)
            {
                tablet_->DetachSeat(seat);
            }
            if (dataDevices_ != nullptr)
            {
                dataDevices_->DetachSeat(seat);
            }
            if (serialSeat_ == seat)
            {
                serialSeat_ = nullptr;
                serial_ = 0;
            }
            seats_.erase(it);
            return;
        }
    }

    void WaylandPlatform::OnDisconnecting()
    {
        // Everything made from the connection, before it closes -- the GL service's EGL display
        // included, since EGL's Wayland platform destroys proxies of its own on it.
        idleInhibitor_.reset();
        dialogs_.reset();
        glContext_.reset();
        vulkanSurface_.reset();
        dataDevices_.reset();
        if (textInput_ != nullptr)
        {
            for (const auto& seat : seats_)
            {
                textInput_->DetachSeat(seat->GetProxy());
            }
        }
        if (tablet_ != nullptr)
        {
            for (const auto& seat : seats_)
            {
                tablet_->DetachSeat(seat->GetProxy());
            }
        }
        // A seat's destructor hands its keyboard, pointer and touch back to the services, which
        // release them.
        seats_.clear();
        if (mouse_ != nullptr)
        {
            mouse_->ReleaseConnectionObjects();
        }
        outputs_.clear();
        displays_.reset();
        serialSeat_ = nullptr;
        serial_ = 0;
    }

    WaylandConnection& WaylandPlatform::RequireConnection(const char* operation) const
    {
        if (connection_ == nullptr)
        {
            throw PlatformException(operation, connectionError_.empty()
                                                   ? std::string("no Wayland compositor connection")
                                                   : connectionError_);
        }
        if (!connection_->IsAlive())
        {
            throw PlatformException(operation, connection_->GetError());
        }
        return *connection_;
    }

    PlatformCapabilities WaylandPlatform::ComputeCapabilities() const
    {
        PlatformCapabilities capabilities;

        // Controllers, power and the portal first: none of them is the compositor's.
        if (controllers_ != nullptr)
        {
            capabilities.gamepad = true;
            capabilities.joystick = true;
            capabilities.gamepadRumble = true;
            capabilities.gamepadSensors = true;
            capabilities.haptics = true;
        }
#ifdef CNA_PLATFORM_HAVE_EVDEV
        capabilities.powerInfo = true;
#endif
        capabilities.inputDeviceEnumeration = inputDevices_ != nullptr;

        if (connection_ == nullptr)
        {
            return capabilities;
        }
        const WaylandGlobals& globals = connection_->GetGlobals();

        // Every flag names what backs it; each accessor is non-null exactly when its flag is true.
        capabilities.multipleWindows = true;      // any number of xdg_toplevels
        capabilities.nativeWindowHandle = true;   // wl_display* + wl_surface*
        capabilities.highDpi = true;              // fractional or integer scale (D-13)
        capabilities.multipleDisplays = displays_ != nullptr;  // wl_output enumeration
        capabilities.borderlessFullscreen = true; // xdg_toplevel.set_fullscreen (D-11)
        capabilities.surfacePresentation = globals.shm != nullptr && connection_->HasXrgb8888();
        capabilities.textInput = true;            // xkbcommon committed text
        capabilities.exactKeyboardState = true;   // real releases; leave releases held keys
        capabilities.pixelAccurateMouse = true;   // surface coordinates in wl_fixed_t
        capabilities.cursorShapes = mouse_->HasSystemCursors();
        capabilities.relativeMouse = mouse_->HasRelativeSupport();
#if defined(CNA_WAYLAND_HAVE_TEXT_INPUT_V3)
        capabilities.ime = globals.textInputManager != nullptr;  // text-input-v3 (D-16)
#endif
        capabilities.openGlContext = glContext_ != nullptr && glContext_->IsAvailable();
        capabilities.vulkanSurface = vulkanSurface_ != nullptr;
        capabilities.clipboard = dataDevices_ != nullptr;
        capabilities.clipboardData = dataDevices_ != nullptr;
        capabilities.dragAndDrop = dataDevices_ != nullptr;
        capabilities.primarySelection = dataDevices_ != nullptr && dataDevices_->HasPrimarySelection();
        capabilities.messageBox = dialogs_ != nullptr && dialogs_->HasMessageBox();
        capabilities.nativeFileDialog = dialogs_ != nullptr && dialogs_->HasFileDialogs();

        // Deliberately false, each for a stated reason:
        //   globalPointer     -- Wayland gives clients no desktop coordinates and no warping (D-19)
        //   tray              -- no Wayland protocol; StatusNotifierItem is a desktop's own
        //   camera, sensors   -- not window-system facilities
        //   managedEntrypoint -- an ordinary main()
        return capabilities;
    }

    void WaylandPlatform::AcquireSubsystem(const PlatformSubsystem subsystem)
    {
        if (subsystem == PlatformSubsystem::Video)
        {
            (void) RequireConnection("WaylandPlatform::AcquireSubsystem(Video)");
        }
        const int count = ++refCounts_[subsystem];
#ifdef CNA_PLATFORM_HAVE_EVDEV
        if (subsystem == PlatformSubsystem::Gamepad && count == 1 && controllers_ != nullptr)
        {
            controllers_->hub.Start();
        }
#else
        (void) count;
#endif
    }

    void WaylandPlatform::ReleaseSubsystem(const PlatformSubsystem subsystem)
    {
        const auto found = refCounts_.find(subsystem);
        if (found == refCounts_.end() || found->second == 0)
        {
            return;
        }
        --found->second;
#ifdef CNA_PLATFORM_HAVE_EVDEV
        if (subsystem == PlatformSubsystem::Gamepad && found->second == 0 && controllers_ != nullptr)
        {
            controllers_->hub.Stop();
            controllers_->gamepad.Update();
            controllers_->joystick.Update();
        }
        if (subsystem == PlatformSubsystem::Haptic && found->second == 0 && controllers_ != nullptr)
        {
            controllers_->haptics.CloseAll();
        }
#endif
        // The connection is not closed at a zero Video count: the capability set would change
        // mid-life, and callers cache it (see the constructor).
    }

    bool WaylandPlatform::IsSubsystemInitialized(const PlatformSubsystem subsystem) const
    {
        const auto found = refCounts_.find(subsystem);
        return found != refCounts_.end() && found->second > 0;
    }

    std::unique_ptr<IPlatformWindow> WaylandPlatform::CreateWindow(const WindowDescription& description)
    {
        WaylandConnection& connection = RequireConnection("WaylandPlatform::CreateWindow");
        (void) connection;
        if (description.width <= 0 || description.height <= 0)
        {
            throw PlatformException("WaylandPlatform::CreateWindow", "a window size must be positive");
        }
        if (description.renderIntent == WindowRenderIntent::OpenGl && (glContext_ == nullptr || !glContext_->IsAvailable()))
        {
            throw PlatformNotSupportedException(
                PlatformCapability::OpenGlContext,
                "Wayland (a window was requested with WindowRenderIntent::OpenGl, but libEGL/libwayland-egl with a "
                "Wayland platform is not available)");
        }
        const WindowId id = nextWindowId_++;
        auto window = std::make_unique<WaylandWindow>(static_cast<WaylandWindowHost&>(*this), id, description);
        WaylandWindow* raw = window.get();
        windows_[id] = raw;
        if (glContext_ != nullptr) { glContext_->RegisterWindow(id, raw); }
        if (vulkanSurface_ != nullptr) { vulkanSurface_->RegisterWindow(id, raw); }
        if (idleInhibitor_ != nullptr) { idleInhibitor_->AddWindow(id, raw->GetSurface(), !screenSaverEnabled_); }
        if (description.visible)
        {
            // Shown only now, registered: showing dispatches events until the first configure, and
            // a keyboard's enter on the new surface must find the window.
            window->Show();
        }
        if (description.fullscreenMode != WindowFullscreenMode::Windowed && !description.visible)
        {
            window->SetFullscreenMode(description.fullscreenMode);
        }
        return window;
    }

    std::unique_ptr<IPlatformWindow> WaylandPlatform::AdoptWindow(const WindowId windowId)
    {
        if (windowId == 0)
        {
            throw PlatformException("WaylandPlatform::AdoptWindow", "window id must be non-zero");
        }
        WaylandWindow* window = FindWindow(windowId);
        if (window == nullptr)
        {
            throw PlatformException("WaylandPlatform::AdoptWindow", "no window of this platform has that id");
        }
        return std::make_unique<BorrowedWindow>(*window);
    }

    std::unique_ptr<IPlatformWindow> WaylandPlatform::AdoptWindowHandle(const std::uintptr_t handle)
    {
        if (handle == 0)
        {
            throw PlatformException("WaylandPlatform::AdoptWindowHandle", "window handle must be non-zero");
        }
        // The token is the window id (IPlatformWindow's default). A surface of another client -- or
        // of this one that the platform did not make -- cannot be adopted: a Wayland client has no
        // way to learn another surface's size, state or output, so a wrapper could only lie.
        if (handle <= std::numeric_limits<WindowId>::max())
        {
            if (WaylandWindow* window = FindWindow(static_cast<WindowId>(handle)))
            {
                return std::make_unique<BorrowedWindow>(*window);
            }
        }
        throw PlatformException("WaylandPlatform::AdoptWindowHandle",
                                "the token names no window of this platform; Wayland cannot adopt a surface it did "
                                "not create");
    }

    void WaylandPlatform::PostEvent(PlatformEvent event)
    {
        pending_.push_back(std::move(event));
    }

    void WaylandPlatform::PollEvents(std::vector<PlatformEvent>& destination)
    {
        destination.clear();
        if (connection_ != nullptr)
        {
            if (connection_->IsAlive())
            {
                (void) connection_->Pump();
                keyboard_->GenerateRepeats(std::chrono::steady_clock::now());
                if (dataDevices_ != nullptr)
                {
                    dataDevices_->Pump();
                }
            }
            if (!connection_->IsAlive() && !connectionLostReported_)
            {
                // The compositor ended the connection -- a protocol error, or it went away. Every
                // window is gone with it, and there is nothing to reconnect to that would bring
                // them back: the application is asked to quit, and told why once.
                connectionLostReported_ = true;
                std::fprintf(stderr, "[CNA][Wayland] %s\n", connection_->GetError().c_str());
                std::fflush(stderr);
                pending_.emplace_back(QuitEvent{});
            }
        }
        destination.insert(destination.end(), std::make_move_iterator(pending_.begin()),
                           std::make_move_iterator(pending_.end()));
        pending_.clear();
#ifdef CNA_PLATFORM_HAVE_EVDEV
        if (controllers_ != nullptr && controllers_->hub.IsStarted())
        {
            controllers_->hub.Pump();
            controllers_->hub.TakeEvents(destination);
        }
#endif
#if defined(CNA_PLATFORM_HAVE_DBUS)
        if (portal_ != nullptr)
        {
            portal_->Pump();
        }
#endif
    }

    std::uint64_t WaylandPlatform::GetPerformanceCounter() const
    {
        return Posix::MonotonicNanoseconds();
    }

    std::uint64_t WaylandPlatform::GetPerformanceFrequency() const
    {
        return 1000000000uLL;
    }

    std::uint64_t WaylandPlatform::GetTicksMilliseconds() const
    {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - epoch_).count());
    }

    void WaylandPlatform::Delay(const std::uint32_t milliseconds)
    {
        Posix::SleepMilliseconds(milliseconds);
    }

    void WaylandPlatform::OnWindowDestroyed(WaylandWindow& window)
    {
        const auto found = windows_.find(window.GetId());
        if (found == windows_.end() || found->second != &window)
        {
            return;
        }
        const WindowId id = window.GetId();
        windows_.erase(found);
        // Every service forgets the window before its surface goes: the pointer lock and the idle
        // inhibitor are objects on that surface, and the GL service's wl_egl_window is made from it.
        if (mouse_ != nullptr) { mouse_->ForgetWindow(id); }
        if (keyboard_ != nullptr) { keyboard_->ForgetWindow(id); }
        if (touch_ != nullptr) { touch_->ForgetWindow(id); }
        if (tablet_ != nullptr) { tablet_->ForgetWindow(id); }
        if (textInput_ != nullptr) { textInput_->ForgetWindow(id); }
        if (dataDevices_ != nullptr) { dataDevices_->ForgetWindow(id); }
        if (idleInhibitor_ != nullptr) { idleInhibitor_->RemoveWindow(id); }
        if (glContext_ != nullptr) { glContext_->RegisterWindow(id, nullptr); }
        if (vulkanSurface_ != nullptr) { vulkanSurface_->RegisterWindow(id, nullptr); }
        if (dialogs_ != nullptr) { dialogs_->ForgetWindow(id); }
    }

    const WaylandOutput* WaylandPlatform::FindOutput(const wl_output* output) const
    {
        for (const auto& candidate : outputs_)
        {
            if (candidate->GetProxy() == output)
            {
                return candidate.get();
            }
        }
        return nullptr;
    }

    std::vector<const WaylandOutput*> WaylandPlatform::GetOutputs() const
    {
        std::vector<const WaylandOutput*> outputs;
        for (const auto& output : outputs_)
        {
            if (output->IsDescribed())
            {
                outputs.push_back(output.get());
            }
        }
        return outputs;
    }

    WaylandWindow* WaylandPlatform::FindWindow(const WindowId id) const
    {
        const auto found = windows_.find(id);
        return found != windows_.end() ? found->second : nullptr;
    }

    void WaylandPlatform::MapSurface(wl_surface* surface, const WindowId window)
    {
        if (surface == nullptr)
        {
            return;
        }
        if (window == 0)
        {
            surfaces_.erase(surface);
            return;
        }
        surfaces_[surface] = window;
    }

    void WaylandPlatform::MapFrameSurface(wl_surface* surface, WaylandFrame* frame)
    {
        if (surface == nullptr)
        {
            return;
        }
        if (frame == nullptr)
        {
            frameSurfaces_.erase(surface);
            return;
        }
        frameSurfaces_[surface] = frame;
    }

    void WaylandPlatform::SetFrameCursor(wl_pointer* pointer, const std::uint32_t serial, const SystemCursor cursor)
    {
        if (mouse_ != nullptr)
        {
            mouse_->ShowShapeFor(pointer, serial, cursor);
        }
    }

    WindowId WaylandPlatform::ResolveSurface(wl_surface* surface) const
    {
        const auto found = surfaces_.find(surface);
        return found != surfaces_.end() ? found->second : 0;
    }

    void WaylandPlatform::RecordSerial(wl_seat* seat, const std::uint32_t serial)
    {
        serialSeat_ = seat;
        serial_ = serial;
    }

    std::uint32_t WaylandPlatform::GetLatestInputSerial(wl_seat*& seat) const
    {
        seat = serialSeat_;
        return serial_;
    }

    bool WaylandPlatform::HasKeyboard() const
    {
        return keyboard_ != nullptr && keyboard_->HasKeyboard();
    }

    void WaylandPlatform::RequestActivation(WaylandWindow& window)
    {
        if (connection_ == nullptr || !connection_->IsAlive())
        {
            return;
        }
        // The token the desktop that started this program handed it (a launcher, a file manager,
        // a terminal that supports it): spent on the first window shown and taken out of the
        // environment, as the xdg-activation specification asks, so a child process does not
        // spend it again.
        if (!launchTokenSpent_)
        {
            launchTokenSpent_ = true;
            const char* launch = std::getenv("XDG_ACTIVATION_TOKEN");
            if (launch != nullptr && *launch != '\0')
            {
                const std::string token = launch;
                ::unsetenv("XDG_ACTIVATION_TOKEN");
                ActivateWindowWithToken(*connection_, window, token);
                return;
            }
        }
        // Otherwise a token for the latest input this client received -- a window opened in
        // answer to a click or a key. Without any input there is nothing a compositor would grant
        // a token for, and the compositor's own focus policy decides.
        if (serialSeat_ != nullptr && serial_ != 0)
        {
            ActivateWindow(*connection_, window, serialSeat_, serial_);
        }
    }

    void WaylandPlatform::SetScreenSaverEnabled(const bool enabled)
    {
        screenSaverEnabled_ = enabled;
        if (idleInhibitor_ != nullptr && idleInhibitor_->IsSupported())
        {
            idleInhibitor_->SetInhibited(!enabled);
            return;
        }
        // Without the protocol, the session bus's org.freedesktop.ScreenSaver -- shared with X11.
        if (!enabled)
        {
            if (screenSaverBus_ == nullptr)
            {
                screenSaverBus_ = std::make_unique<Freedesktop::ScreenSaverBusInhibition>();
            }
            (void) screenSaverBus_->Inhibit();
        }
        else if (screenSaverBus_ != nullptr)
        {
            screenSaverBus_->Lift();
        }
    }

    void WaylandPlatform::EnsureControllerSubsystem()
    {
        if (controllerSubsystemEnsured_)
        {
            return;
        }
        controllerSubsystemEnsured_ = true;
        AcquireSubsystem(PlatformSubsystem::Gamepad);
#ifdef CNA_PLATFORM_HAVE_EVDEV
        controllers_->gamepad.Update();
        controllers_->joystick.Update();
#endif
    }

    IPlatformGamepad* WaylandPlatform::GetGamepad()
    {
#ifdef CNA_PLATFORM_HAVE_EVDEV
        if (controllers_ != nullptr)
        {
            EnsureControllerSubsystem();
            return &controllers_->gamepad;
        }
#endif
        return nullptr;
    }

    IPlatformJoystick* WaylandPlatform::GetJoystick()
    {
#ifdef CNA_PLATFORM_HAVE_EVDEV
        if (controllers_ != nullptr)
        {
            EnsureControllerSubsystem();
            return &controllers_->joystick;
        }
#endif
        return nullptr;
    }

    IPlatformHaptics* WaylandPlatform::GetHaptics()
    {
#ifdef CNA_PLATFORM_HAVE_EVDEV
        return controllers_ != nullptr ? &controllers_->haptics : nullptr;
#else
        return nullptr;
#endif
    }

    std::vector<InputDeviceInfo> WaylandPlatform::ControllerDevices(const InputDeviceKind kind)
    {
        std::vector<InputDeviceInfo> devices;
#ifdef CNA_PLATFORM_HAVE_EVDEV
        if (controllers_ == nullptr)
        {
            return devices;
        }
        if (kind == InputDeviceKind::Haptic)
        {
            for (const HapticInfo& haptic : controllers_->haptics.GetHaptics())
            {
                devices.push_back({haptic.id, kind, haptic.name});
            }
            return devices;
        }
        EnsureControllerSubsystem();
        controllers_->hub.Pump();
        for (const auto& controller : controllers_->hub.GetControllers())
        {
            if (kind == InputDeviceKind::Joystick || controller->kind == Linux::EvdevDeviceClass::Gamepad)
            {
                devices.push_back({controller->id, kind, controller->device->GetDescription().name});
            }
        }
#else
        (void) kind;
#endif
        return devices;
    }

    IPlatformKeyboard* WaylandPlatform::GetKeyboard() { return connection_ != nullptr ? keyboard_.get() : nullptr; }
    IPlatformMouse* WaylandPlatform::GetMouse() { return connection_ != nullptr ? mouse_.get() : nullptr; }
    IPlatformTextInput* WaylandPlatform::GetTextInput() { return connection_ != nullptr ? textInput_.get() : nullptr; }
    IPlatformInputDevices* WaylandPlatform::GetInputDevices() { return inputDevices_.get(); }
    IPlatformClipboard* WaylandPlatform::GetClipboard()
    {
        return dataDevices_ != nullptr ? dataDevices_->GetClipboard() : nullptr;
    }
    IPlatformClipboard* WaylandPlatform::GetPrimarySelection()
    {
        return dataDevices_ != nullptr && dataDevices_->HasPrimarySelection() ? dataDevices_->GetPrimarySelection()
                                                                             : nullptr;
    }
    IPlatformDisplays* WaylandPlatform::GetDisplays() { return displays_.get(); }
    IPlatformDialogs* WaylandPlatform::GetDialogs() { return dialogs_.get(); }

    IPlatformGlContext* WaylandPlatform::GetGlContext()
    {
        return glContext_ != nullptr && glContext_->IsAvailable() ? glContext_.get() : nullptr;
    }

    IPlatformVulkanSurface* WaylandPlatform::GetVulkanSurface() { return vulkanSurface_.get(); }

    std::unique_ptr<IPlatformSurfacePresenter> WaylandPlatform::CreateSurfacePresenter(IPlatformWindow& window)
    {
        WaylandConnection& connection = RequireConnection("WaylandPlatform::CreateSurfacePresenter");
        if (!capabilities_.surfacePresentation)
        {
            throw PlatformNotSupportedException(PlatformCapability::SurfacePresentation,
                                                "Wayland (the compositor offers no wl_shm with XRGB8888)");
        }
        // An adopted wrapper forwards to the window this platform owns under the same id.
        WaylandWindow* target = dynamic_cast<WaylandWindow*>(&window);
        if (target == nullptr)
        {
            target = FindWindow(window.GetId());
        }
        if (target == nullptr || FindWindow(target->GetId()) != target)
        {
            throw PlatformException("WaylandPlatform::CreateSurfacePresenter",
                                    "the window was not created by this platform");
        }
        return std::make_unique<WaylandSurfacePresenter>(connection, *target);
    }

} // namespace CNA::Platform::Wayland
