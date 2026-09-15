// SPDX-License-Identifier: MS-PL

#include "X11Platform.hpp"

#include "CNA/Platform/PlatformException.hpp"
#include "../Freedesktop/DesktopPortal.hpp"
#include "../Posix/MonotonicClock.hpp"
#include "X11Error.hpp"
#include "X11EventMapper.hpp"
#include "X11Window.hpp"

#ifdef CNA_PLATFORM_HAVE_EVDEV
#include "../Linux/EvdevControllers.hpp"
#include "../Linux/EvdevHaptics.hpp"
#include "../Linux/LinuxSystemInfo.hpp"
#endif

#include <filesystem>
#include <unistd.h>

namespace CNA::Platform::X11 {

#ifdef CNA_PLATFORM_HAVE_EVDEV
    struct X11Platform::Controllers
    {
        Linux::EvdevControllerHub hub;
        Linux::EvdevGamepad gamepad{hub};
        Linux::EvdevJoystick joystick{hub};
        // Force feedback is the same nodes' (X11-0168); a joystick's node is found through the hub.
        Linux::EvdevHaptics haptics{[this](const DeviceId id) {
            const Linux::EvdevControllerHub::Controller* controller = hub.FindById(id);
            return controller != nullptr && controller->device != nullptr ? controller->device->GetPath()
                                                                           : std::string();
        }};
    };
#else
    struct X11Platform::Controllers
    {
    };
#endif

    void X11Platform::ControllersDeleter::operator()(Controllers* controllers) const
    {
        delete controllers;
    }

    void X11Platform::PortalDeleter::operator()(Freedesktop::DesktopPortal* portal) const
    {
#if defined(CNA_PLATFORM_HAVE_DBUS)
        delete portal;
#else
        (void) portal;  // Never made without D-Bus.
#endif
    }

    namespace {

        /// How close in time and space two clicks must be to count as a double click.
        ///
        /// X11 has no double-click concept at all: the server reports button presses and their
        /// timestamps, and every toolkit decides for itself. 500 ms and 4 pixels are the values
        /// the common toolkits converge on, and the spatial part matters as much as the temporal
        /// one -- two fast clicks in different places are two clicks, not a double click.
        constexpr unsigned long kDoubleClickMilliseconds = 500;
        constexpr int kDoubleClickSlopPixels = 4;

        /// Events a normal CNA window needs. Selected once at creation; adding to this later
        /// means an XSelectInput round trip, so the set is deliberately complete rather than
        /// minimal.
        constexpr long kWindowEventMask =
            ExposureMask | StructureNotifyMask | FocusChangeMask | KeyPressMask | KeyReleaseMask |
            ButtonPressMask | ButtonReleaseMask | PointerMotionMask | EnterWindowMask |
            LeaveWindowMask | PropertyChangeMask | VisibilityChangeMask;

    } // namespace

    /**
     * A non-owning view of a window the platform already owns.
     *
     * `AdoptWindow` must not hand back something whose destructor destroys the X window, because
     * the caller that created it still holds the owning wrapper. Forwarding to the live object
     * rather than constructing a second `X11Window` over the same XID also means the two views
     * cannot disagree about cached state such as focus or fullscreen mode.
     */
    class X11Platform::BorrowedWindow final : public IPlatformWindow
    {
    public:
        explicit BorrowedWindow(X11Window& target) : target_(target) {}

        [[nodiscard]] WindowId GetId() const override { return target_.GetId(); }
        [[nodiscard]] std::uintptr_t GetWindowHandle() const override
        {
            return target_.GetWindowHandle();
        }
        [[nodiscard]] NativeWindowHandle GetNativeHandle() const override
        {
            return target_.GetNativeHandle();
        }
        [[nodiscard]] std::string GetTitle() const override { return target_.GetTitle(); }
        void SetTitle(const std::string& title) override { target_.SetTitle(title); }
        [[nodiscard]] WindowBounds GetClientBounds() const override
        {
            return target_.GetClientBounds();
        }
        [[nodiscard]] WindowSize GetPixelSize() const override { return target_.GetPixelSize(); }
        void SetSize(int width, int height) override { target_.SetSize(width, height); }
        [[nodiscard]] float GetDisplayScale() const override { return target_.GetDisplayScale(); }
        [[nodiscard]] bool IsResizable() const override { return target_.IsResizable(); }
        void SetResizable(bool resizable) override { target_.SetResizable(resizable); }
        [[nodiscard]] bool IsBorderless() const override { return target_.IsBorderless(); }
        void SetBorderless(bool borderless) override { target_.SetBorderless(borderless); }
        void SetFullscreenMode(WindowFullscreenMode mode) override
        {
            target_.SetFullscreenMode(mode);
        }
        [[nodiscard]] WindowFullscreenMode GetFullscreenMode() const override
        {
            return target_.GetFullscreenMode();
        }
        void Show() override { target_.Show(); }
        void Hide() override { target_.Hide(); }
        void Minimize() override { target_.Minimize(); }
        void Maximize() override { target_.Maximize(); }
        void Restore() override { target_.Restore(); }
        void Sync() override { target_.Sync(); }
        [[nodiscard]] bool HasFocus() const override { return target_.HasFocus(); }
        [[nodiscard]] bool IsMinimized() const override { return target_.IsMinimized(); }
        [[nodiscard]] std::string GetDisplayName() const override
        {
            return target_.GetDisplayName();
        }

    private:
        X11Window& target_;
    };

    X11Platform::X11Platform()
    {
#if defined(CNA_PLATFORM_HAVE_DBUS)
        // The desktop portal is the session bus's, not the X server's: looked for once, without
        // starting it, whatever the display (X11-0169).
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
        try
        {
            OpenConnection();
        }
        catch (const PlatformException& error)
        {
            // Recorded, not propagated. See the constructor's documentation: a process with no
            // display must still be able to construct a platform to reach the portable services.
            connectionError_ = error.GetDetail().empty() ? std::string(error.what())
                                                         : error.GetDetail();
        }
#ifdef CNA_PLATFORM_HAVE_EVDEV
        // Nothing is opened here -- that waits for the first GetGamepad()/GetJoystick() -- but
        // whether the services exist at all is settled now, because the capability set is. A
        // system without /dev/input (a minimal container) has no controllers to offer, and saying
        // so beats a service that could never report one.
        std::error_code error;
        if (std::filesystem::is_directory("/dev/input", error))
        {
            controllers_.reset(new Controllers());
        }
#endif
        capabilities_ = ComputeCapabilities();
    }

    X11Platform::~X11Platform()
    {
        // Services before the connection, and the window registry before either: a service holds
        // raw pointers into windows the application owns, and closing the display first would
        // make every subsequent XFree* in a service destructor a use-after-free.
        //
        // A window still registered here outlived the platform, which PlatformFactory's contract
        // does not allow; detaching at least keeps its destructor from calling back into this
        // object once it is gone.
        for (const auto& [id, window] : windows_)
        {
            (void) id;
            if (window != nullptr)
            {
                window->DetachHost();
            }
        }
        windows_.clear();
        CloseConnection();
        // Closing the nodes stops any rumble still playing (EvdevDevice's destructor).
        controllers_.reset();
    }

    const std::string& X11Platform::GetName() const
    {
        static const std::string name = "X11";
        return name;
    }

    PlatformCapabilities X11Platform::GetCapabilities() const
    {
        return capabilities_;
    }

    PlatformCapabilities X11Platform::ComputeCapabilities() const
    {
        PlatformCapabilities capabilities;

        // Controllers first, because they do not depend on the X server at all: they are the
        // kernel's evdev nodes, readable by a process that has no display. Rumble is claimed for
        // the service -- SetRumble is always reachable -- and each pad then answers for itself
        // through GamepadCapabilities::rumble, which is how the SDL3 backend reports it too.
        if (controllers_ != nullptr)
        {
            capabilities.gamepad = true;
            capabilities.joystick = true;
            capabilities.gamepadRumble = true;
            // Motion sensors likewise: the service answers, and each pad says whether it has
            // them in GamepadCapabilities (X11-0166).
            capabilities.gamepadSensors = true;
            // Force feedback through the same nodes: the service lists what can play (X11-0168).
            capabilities.haptics = true;
        }
#ifdef CNA_PLATFORM_HAVE_EVDEV
        // Battery state is the kernel's power-supply class, which needs no display either
        // (X11-0163).
        capabilities.powerInfo = true;
#endif

        // Every flag below names what backs it. The contract's rule is that a service accessor is
        // non-null exactly when its presence capability is true, so a flag may only be set in the
        // same change that wires its accessor -- and several of these are conditional on an
        // extension actually being present, not merely compiled in.
        // Without a display there is no window system, so every one of these is false and each
        // corresponding service accessor returns null. That is not a degraded mode pretending to
        // work -- it is the truthful description of a platform that could not reach an X server.
        if (connection_ == nullptr)
        {
            return capabilities;
        }

        capabilities.multipleWindows = true;      // XCreateWindow has no single-window limit.
        capabilities.nativeWindowHandle = true;   // Display* + XID.
        capabilities.surfacePresentation = true;  // XPutImage.
        capabilities.textInput = true;            // Xutf8LookupString, or XLookupString.
        capabilities.exactKeyboardState = true;   // Real KeyRelease events; no synthesis.
        capabilities.pixelAccurateMouse = true;   // X reports true pixels.
        capabilities.cursorShapes = true;         // The core cursor font; Xcursor for ARGB images.
        capabilities.globalPointer = true;        // XQueryPointer/XWarpPointer on the root.
        capabilities.clipboard = true;            // Real ICCCM selection ownership with INCR.
        capabilities.dragAndDrop = true;          // XDND 5, target side: files and text.
        capabilities.primarySelection = true;     // PRIMARY, owned and read like CLIPBOARD.
        capabilities.clipboardData = true;        // Any target, named by its MIME type.

        // highDpi promises a drawable that can exceed the logical size, and on X11 it never does:
        // one coordinate space. A session's scale is the displays' content scale (X11-0156).
        capabilities.highDpi = false;
        capabilities.multipleDisplays = connection_->HasRandr();
        capabilities.borderlessFullscreen =
            connection_->SupportsEwmhHint(connection_->GetAtoms().netWmStateFullscreen);
        capabilities.openGlContext = glContext_ != nullptr && glContext_->IsAvailable();
        capabilities.vulkanSurface = vulkanSurface_ != nullptr;
        capabilities.relativeMouse = mouse_ != nullptr && mouse_->HasRawMotion();
        // Composition events exist only when the application asked to draw them and the input
        // method agreed to hand them over (X11TextInput's class comment, X11-0152).
        capabilities.ime = textInput_ != nullptr && textInput_->HasCompositionEvents();
        // Keyboards, mice and touch devices through XInput2, controllers through the hub (X11-0165).
        capabilities.inputDeviceEnumeration = inputDevices_ != nullptr;
        // Message boxes drawn with Xlib in a window of their own (X11-0167); file dialogs the
        // desktop portal's, where the session bus has one (X11-0169).
        capabilities.messageBox = dialogs_ != nullptr;
        capabilities.nativeFileDialog = dialogs_ != nullptr && portal_ != nullptr;
        // Icons in the system tray, where a tray was running when the platform was made (X11-0171).
        capabilities.tray = tray_ != nullptr;

        // Deliberately false, each for a stated reason rather than for want of effort:
        //   camera                 -- not an X11 facility.
        //   managedEntrypoint      -- an ordinary main().
        return capabilities;
    }

    void X11Platform::OpenConnection()
    {
        if (connection_ != nullptr)
        {
            return;
        }
        connection_ = std::make_unique<X11Connection>();

        // Services are created with the connection and destroyed with it, so a service pointer is
        // valid exactly while Video is held -- which is what makes "null when the capability is
        // false" honest for the services that depend on a live display.
        keyboard_ = std::make_unique<X11Keyboard>(*connection_);
        mouse_ = std::make_unique<X11Mouse>(*connection_);
        touch_ = std::make_unique<X11Touch>(*connection_, mouse_.get());
        textInput_ = std::make_unique<X11TextInput>(*connection_);
        clipboard_ = std::make_unique<X11Clipboard>(*connection_, connection_->GetAtoms().clipboard,
                                                    "CLIPBOARD");
        primarySelection_ = std::make_unique<X11Clipboard>(
            *connection_, connection_->GetAtoms().primary, "PRIMARY");
        dragAndDrop_ = std::make_unique<X11DragAndDrop>(*connection_, *clipboard_);
        dialogs_ = std::make_unique<X11Dialogs>(*connection_, portal_.get());
        // A tray is a client owning the selection; there is one or there is not, now (X11-0171).
        if (HasSystemTray(connection_->GetDisplay(), connection_->GetScreen()))
        {
            tray_ = std::make_unique<X11Tray>(connection_->GetDisplayName());
        }
        if (connection_->GetXInput2Opcode() >= 0)
        {
            inputDevices_ = std::make_unique<X11InputDevices>(
                *connection_, [this](const InputDeviceKind kind) { return ControllerDevices(kind); });
        }
        displays_ = std::make_unique<X11Displays>(*connection_);
        glContext_ = std::make_unique<X11GlContext>(*connection_);
        vulkanSurface_ = std::make_unique<X11VulkanSurface>(*connection_);
    }

    void X11Platform::CloseConnection()
    {
        // What the game copied is its own until now: X has no clipboard storage. A clipboard
        // manager, where the desktop runs one, is offered it before the owner goes away, so the
        // copy outlives the game (X11-0164).
        if (clipboard_ != nullptr)
        {
            (void) clipboard_->HandOverToClipboardManager();
        }
        // Reverse construction order. The clipboard owns a window on the connection and the text
        // input owns an XIM, so both must go before XCloseDisplay; the graphics services hold
        // GLX contexts, which must be destroyed while their display is alive.
        vulkanSurface_.reset();
        glContext_.reset();
        displays_.reset();
        tray_.reset();
        dialogs_.reset();
        inputDevices_.reset();
        dragAndDrop_.reset();
        primarySelection_.reset();
        clipboard_.reset();
        textInput_.reset();
        touch_.reset();
        mouse_.reset();
        keyboard_.reset();
        connection_.reset();
    }

    X11Connection& X11Platform::RequireConnection(const char* operation) const
    {
        if (connection_ == nullptr)
        {
            throw PlatformException(operation,
                                    "the Video subsystem has not been acquired, so there is no X "
                                    "connection; call AcquireSubsystem(PlatformSubsystem::Video)");
        }
        return *connection_;
    }

    void X11Platform::AcquireSubsystem(const PlatformSubsystem subsystem)
    {
        // Video is the only subsystem with anything behind it, and the only one that can fail.
        // The rest refcount and do nothing, exactly as the headless and terminal backends do:
        // the cross-implementation contract is that acquisition is bookkeeping, and that a
        // subsystem CNA has no facility for reports its absence through a null service and a
        // false capability rather than by refusing to be acquired. `GraphicsDevice::Dispose`
        // releases Video unconditionally, and callers balance acquisitions they never inspect.
        if (subsystem == PlatformSubsystem::Video && connection_ == nullptr)
        {
            throw PlatformException("X11Platform::AcquireSubsystem(Video)",
                                    connectionError_.empty()
                                        ? std::string("no X connection could be opened")
                                        : connectionError_);
        }
        const int count = ++refCounts_[subsystem];
#ifdef CNA_PLATFORM_HAVE_EVDEV
        if (subsystem == PlatformSubsystem::Gamepad && count == 1 && controllers_ != nullptr)
        {
            // A /dev/input that cannot be read leaves the hub with no controllers, which is an
            // answer ("nothing plugged in"), not a reason to refuse the subsystem.
            controllers_->hub.Start();
        }
#else
        (void) count;
#endif
    }

    void X11Platform::ReleaseSubsystem(const PlatformSubsystem subsystem)
    {
        const auto found = refCounts_.find(subsystem);
        if (found == refCounts_.end() || found->second == 0)
        {
            // Unpaired release is a documented no-op: cleanup code may legitimately run after a
            // partial initialization.
            return;
        }
        --found->second;
#ifdef CNA_PLATFORM_HAVE_EVDEV
        if (subsystem == PlatformSubsystem::Gamepad && found->second == 0 && controllers_ != nullptr)
        {
            // Unlike Video, releasing this changes no capability: the services stay, reporting
            // every slot empty until the subsystem is acquired again. Closing the nodes is what
            // a release is for -- it stops rumble and lets go of the devices.
            controllers_->hub.Stop();
            controllers_->gamepad.Update();
            controllers_->joystick.Update();
        }
        if (subsystem == PlatformSubsystem::Haptic && found->second == 0 && controllers_ != nullptr)
        {
            // Closing a device erases what it played: nothing is left buzzing.
            controllers_->haptics.CloseAll();
        }
#endif

        // The connection is deliberately NOT closed when the Video count reaches zero. Closing it
        // would change the capability set mid-life -- every display-dependent capability would
        // flip to false -- and the contract says a capability set is stable for the instance's
        // lifetime because callers cache it once. The connection is this platform instance's own
        // resource: opened in its constructor, closed in its destructor, and never the host
        // application's.
    }

    bool X11Platform::IsSubsystemInitialized(const PlatformSubsystem subsystem) const
    {
        const auto found = refCounts_.find(subsystem);
        return found != refCounts_.end() && found->second > 0;
    }

    void X11Platform::RegisterWindowWithServices(const WindowId id, X11Window* window)
    {
        if (mouse_ != nullptr) { mouse_->RegisterWindow(id, window); }
        if (textInput_ != nullptr) { textInput_->RegisterWindow(id, window); }
        if (glContext_ != nullptr) { glContext_->RegisterWindow(id, window); }
        if (vulkanSurface_ != nullptr) { vulkanSurface_->RegisterWindow(id, window); }
    }

    void X11Platform::ForgetWindow(const WindowId id)
    {
        windows_.erase(id);
        RegisterWindowWithServices(id, nullptr);
    }

    void X11Platform::OnWindowDestroyed(X11Window& window)
    {
        // Identity, not id: only the wrapper the registry actually holds may unregister it. A
        // window the server already destroyed was forgotten at its DestroyNotify, and finding
        // nothing here is then the correct outcome rather than an error.
        const auto found = windows_.find(window.GetId());
        if (found == windows_.end() || found->second != &window)
        {
            return;
        }
        if (dragAndDrop_ != nullptr)
        {
            dragAndDrop_->ForgetWindow(window.GetXWindow());
        }
        if (touch_ != nullptr)
        {
            touch_->ForgetWindow(window.GetId(), window.GetXWindow());
        }
        ForgetWindow(window.GetId());
    }

    X11Window* X11Platform::FindWindow(const WindowId id) const
    {
        const auto found = windows_.find(id);
        return found != windows_.end() ? found->second : nullptr;
    }

    X11Window* X11Platform::FindWindowByXid(const ::Window xid) const
    {
        for (const auto& [id, window] : windows_)
        {
            (void) id;
            if (window != nullptr && window->GetXWindow() == xid)
            {
                return window;
            }
        }
        return nullptr;
    }

    std::unique_ptr<IPlatformWindow> X11Platform::CreateWindow(const WindowDescription& description)
    {
        X11Connection& connection = RequireConnection("X11Platform::CreateWindow");
        Display* display = connection.GetDisplay();
        const int screen = connection.GetScreen();

        if (description.width <= 0 || description.height <= 0)
        {
            throw PlatformException("X11Platform::CreateWindow",
                                    "a window size must be positive; X rejects a zero extent");
        }

        // The visual has to be settled BEFORE XCreateWindow, because an X window's visual is
        // fixed for its lifetime. That is the entire reason WindowDescription carries a render
        // intent rather than offering a post-creation setter -- see plan design decision 13.
        Visual* visual = DefaultVisual(display, screen);
        int depth = DefaultDepth(display, screen);
        Colormap colormap = kNone;
        void* fbConfig = nullptr;

        if (description.renderIntent == WindowRenderIntent::OpenGl)
        {
            if (glContext_ == nullptr || !glContext_->IsAvailable())
            {
                throw PlatformNotSupportedException(
                    PlatformCapability::OpenGlContext,
                    "X11 (a window was requested with WindowRenderIntent::OpenGl, but this server "
                    "provides no GLX 1.3)");
            }
            const X11GlVisual chosen = glContext_->ChooseVisual(
                description.openGlFramebuffer.depthBits, description.openGlFramebuffer.stencilBits,
                description.openGlFramebuffer.doubleBuffered, description.openGlFramebuffer.samples);
            if (chosen.visual == nullptr)
            {
                throw PlatformException(
                    "X11Platform::CreateWindow",
                    "no GLX framebuffer configuration matches the requested framebuffer");
            }
            visual = chosen.visual;
            depth = chosen.depth;
            fbConfig = chosen.fbConfig;
        }

        // A window whose visual is not the screen default needs its own colormap: the root
        // window's colormap belongs to the default visual, and XCreateWindow with a mismatched
        // pair is a BadMatch.
        const bool needsOwnColormap = visual != DefaultVisual(display, screen);
        if (needsOwnColormap)
        {
            colormap = XCreateColormap(display, connection.GetRoot(), visual, AllocNone);
        }

        XSetWindowAttributes attributes{};
        unsigned long valueMask = CWEventMask | CWBackPixel | CWBorderPixel;
        attributes.event_mask = kWindowEventMask;
        attributes.background_pixel = BlackPixel(display, screen);
        // border_pixel must be set explicitly whenever the depth differs from the parent's, or
        // XCreateWindow raises BadMatch -- the default is CopyFromParent, which cannot be copied
        // across depths.
        attributes.border_pixel = 0;
        if (colormap != kNone)
        {
            attributes.colormap = colormap;
            valueMask |= CWColormap;
        }

        const int x = description.centered
                          ? (DisplayWidth(display, screen) - description.width) / 2
                          : description.x;
        const int y = description.centered
                          ? (DisplayHeight(display, screen) - description.height) / 2
                          : description.y;

        X11ErrorTrap trap(display);
        const ::Window xWindow = XCreateWindow(
            display, connection.GetRoot(), x, y, static_cast<unsigned int>(description.width),
            static_cast<unsigned int>(description.height), 0, depth, InputOutput, visual, valueMask,
            &attributes);
        trap.Sync();
        if (xWindow == kNone || trap.HasError())
        {
            if (colormap != kNone) { XFreeColormap(display, colormap); }
            throw PlatformException("X11Platform::CreateWindow",
                                    trap.HasError() ? trap.Describe()
                                                    : std::string("XCreateWindow failed"));
        }

        const WindowId id = nextWindowId_++;
        auto window = std::make_unique<X11Window>(connection, xWindow, id, colormap, visual, depth,
                                                   true);
        window->SetHost(this);
        window->SetGlFbConfig(fbConfig);
        window->SetResizableFlag(description.resizable);
        window->SetBorderlessFlag(description.borderless);
        window->SetCachedSize(description.width, description.height);
        window->SetCachedPosition(x, y);

        // WM_DELETE_WINDOW is what turns the window manager's close button into a message this
        // application can answer. Without it the window manager kills the connection instead,
        // which takes every other window with it.
        const X11Atoms& atoms = connection.GetAtoms();
        Atom protocols[] = {atoms.wmDeleteWindow};
        XSetWMProtocols(display, xWindow, protocols, 1);

        // _NET_WM_PID and WM_CLIENT_MACHINE together let a window manager identify and, if it
        // must, kill the owning process. A window with neither is one a stuck-application dialog
        // cannot offer to close.
        const long pid = static_cast<long>(getpid());
        XChangeProperty(display, xWindow, atoms.netWmPid, XA_CARDINAL, 32, PropModeReplace,
                        reinterpret_cast<const unsigned char*>(&pid), 1);
        char hostName[256] = {};
        if (gethostname(hostName, sizeof(hostName) - 1) == 0)
        {
            XTextProperty machine{};
            char* hostPointer = hostName;
            if (XStringListToTextProperty(&hostPointer, 1, &machine) != 0)
            {
                XSetWMClientMachine(display, xWindow, &machine);
                XFree(machine.value);
            }
        }

        // WM_CLASS is what a window manager keys its per-application rules and task-list grouping
        // off. "CNA" as the class, the window title as the instance name.
        XClassHint* classHint = XAllocClassHint();
        if (classHint != nullptr)
        {
            std::string instance = description.title.empty() ? std::string("cna")
                                                             : description.title;
            classHint->res_name = instance.data();
            char className[] = "CNA";
            classHint->res_class = className;
            XSetClassHint(display, xWindow, classHint);
            XFree(classHint);
        }

        window->SetTitle(description.title);
        window->ApplySizeConstraints(description.minimumWidth, description.minimumHeight,
                                     description.maximumWidth, description.maximumHeight);
        if (description.borderless)
        {
            window->SetBorderlessFlag(false);
            window->SetBorderless(true);
        }

        X11Window* raw = window.get();
        windows_[id] = raw;
        RegisterWindowWithServices(id, raw);
        if (textInput_ != nullptr)
        {
            textInput_->AttachWindow(*raw);
        }
        if (dragAndDrop_ != nullptr)
        {
            // Only windows CNA created: an adopted window belongs to a host that may run its own
            // drag and drop.
            dragAndDrop_->AttachWindow(*raw);
        }
        if (touch_ != nullptr)
        {
            // Likewise: taking a window's touch events takes its emulated pointer events away.
            touch_->AttachWindow(*raw);
        }
        if (displays_ != nullptr)
        {
            raw->SetContentScale(displays_->ContentScaleAt({x, y, description.width, description.height}));
        }

        if (description.visible)
        {
            window->Show();
        }
        // Fullscreen is applied after the map request, because a window manager only starts
        // managing a window at MapRequest and a _NET_WM_STATE message before that has nobody to
        // act on it. SetNetWmState knows the difference and writes the property directly when the
        // window is still unmapped.
        if (description.fullscreenMode != WindowFullscreenMode::Windowed)
        {
            window->SetFullscreenMode(description.fullscreenMode);
        }
        XFlush(display);
        return window;
    }

    std::unique_ptr<IPlatformWindow> X11Platform::AdoptWindow(const WindowId windowId)
    {
        if (windowId == 0)
        {
            throw PlatformException("X11Platform::AdoptWindow", "window id must be non-zero");
        }
        X11Window* window = FindWindow(windowId);
        if (window == nullptr)
        {
            throw PlatformException("X11Platform::AdoptWindow",
                                    "no window of this platform has that id");
        }
        return std::make_unique<BorrowedWindow>(*window);
    }

    std::unique_ptr<IPlatformWindow> X11Platform::AdoptWindowHandle(const std::uintptr_t handle)
    {
        if (handle == 0)
        {
            throw PlatformException("X11Platform::AdoptWindowHandle",
                                    "window handle must be non-zero");
        }
        const auto xid = static_cast<::Window>(handle);
        if (X11Window* existing = FindWindowByXid(xid))
        {
            return std::make_unique<BorrowedWindow>(*existing);
        }

        // An XID this platform did not create. It may still be a real window on this display --
        // another toolkit's, or one created before CNA started -- so it is validated against the
        // server rather than refused outright. The error trap is what turns the asynchronous
        // BadWindow into a synchronous answer.
        X11Connection& connection = RequireConnection("X11Platform::AdoptWindowHandle");
        Display* display = connection.GetDisplay();
        XWindowAttributes attributes{};
        X11ErrorTrap trap(display);
        const XStatus status = XGetWindowAttributes(display, xid, &attributes);
        trap.Sync();
        if (status == 0 || trap.HasError())
        {
            throw PlatformException("X11Platform::AdoptWindowHandle",
                                    "no window with that XID exists on " +
                                        connection.GetDisplayName());
        }

        const WindowId id = nextWindowId_++;
        auto window = std::make_unique<X11Window>(connection, xid, id, kNone, attributes.visual,
                                                   attributes.depth, false);
        // Deliberately NOT added to windows_: the platform does not own it, and an adopted window
        // that vanished would leave a dangling pointer in the registry the event pump walks.
        return window;
    }

    void X11Platform::PollEvents(std::vector<PlatformEvent>& destination)
    {
        destination.clear();
        if (connection_ != nullptr)
        {
            PollXEvents(destination);
        }
#ifdef CNA_PLATFORM_HAVE_EVDEV
        // Controller events after the window system's, from the same once-per-frame call. Only
        // once the controller subsystem is up: before that nothing is open, and a game that never
        // asks about controllers pays nothing for them.
        if (controllers_ != nullptr && controllers_->hub.IsStarted())
        {
            controllers_->hub.Pump();
            controllers_->hub.TakeEvents(destination);
        }
#endif
#if defined(CNA_PLATFORM_HAVE_DBUS)
        // A file dialog's answer, and its callback, from the same call; nothing when none is open.
        if (portal_ != nullptr)
        {
            portal_->Pump();
        }
#endif
        // The tray's clicks and menus, from the same call too (X11-0171).
        if (tray_ != nullptr)
        {
            tray_->Pump();
        }
    }

    void X11Platform::PollXEvents(std::vector<PlatformEvent>& destination)
    {
        Display* display = connection_->GetDisplay();

        // XPending flushes the output buffer and then reports what has already arrived, so this
        // loop is non-blocking by construction: XNextEvent can only block when the queue is
        // empty, and XPending has just said it is not.
        XEvent event;
        while (XPending(display) > 0)
        {
            XNextEvent(display, &event);

            // XFilterEvent gives the input method first refusal. An IME consumes the key presses
            // that make up a composition and produces one committed string at the end; delivering
            // those presses as key events too would type the composition twice.
            //
            // Key events are offered to it only while text input is started for their window.
            // Unfocusing the input context is not enough: Xlib forwards every key of a window with
            // a context to the input-method server, and ibus processes them focused or not -- a
            // dead key, or a whole Hangul or Japanese input mode, then swallowed keys a game was
            // using as keys (plans/plan_x11.md X11-0152). Everything else -- the input method's
            // own protocol traffic -- is always offered.
            bool offer = true;
            if ((event.type == KeyPress || event.type == KeyRelease) && textInput_ != nullptr)
            {
                const X11Window* target = FindWindowByXid(event.xkey.window);
                offer = target != nullptr && textInput_->IsActive(target->GetId());
            }
            const bool filtered = offer && XFilterEvent(&event, kNone) == True;
            if (textInput_ != nullptr)
            {
                // What the input method's composition callbacks produced while it handled that
                // event (plans/plan_x11.md X11-0152), in order with everything around it.
                textInput_->TakeEditingEvents(destination);
            }
            if (filtered)
            {
                continue;
            }
            // Both selections see every event: one requestor can be reading both at once, and
            // neither consumes what the other needs.
            bool selectionEvent = clipboard_ != nullptr && clipboard_->HandleEvent(event);
            if (primarySelection_ != nullptr && primarySelection_->HandleEvent(event))
            {
                selectionEvent = true;
            }
            if (selectionEvent)
            {
                continue;
            }
            TranslateEvent(event, destination);
            if (textInput_ != nullptr)
            {
                // A commit looked up with Xutf8LookupString can end the composition too.
                textInput_->TakeEditingEvents(destination);
            }
        }
        if (mouse_ != nullptr)
        {
            mouse_->RefreshRelativeMode();
        }
        if (touch_ != nullptr)
        {
            // Contacts of a window that went away since the last pump (X11Touch::ForgetWindow).
            touch_->TakePendingEvents(destination);
        }
        // A focus loss held back because it came right after a display-mode change is decided
        // here, once the grace period is over (X11Window::OnFocusChanged).
        for (const auto& [id, window] : windows_)
        {
            (void) id;
            if (window != nullptr)
            {
                window->CheckPendingFocusLoss();
            }
        }
    }

    void X11Platform::UpdateContentScale(X11Window& window, std::vector<PlatformEvent>& destination)
    {
        if (displays_ == nullptr)
        {
            return;
        }
        const float scale = displays_->ContentScaleAt(window.GetCachedBounds());
        if (scale == window.GetContentScale())
        {
            return;
        }
        window.SetContentScale(scale);
        WindowEvent changed;
        changed.window = window.GetId();
        changed.kind = WindowEventKind::DisplayScaleChanged;
        destination.emplace_back(changed);
    }

    void X11Platform::EmitWindowStateTransitions(X11Window& window,
                                                 std::vector<PlatformEvent>& destination)
    {
        // Reads the server's answer once and reports only what CHANGED.
        //
        // `_NET_WM_STATE` changes for many reasons a game has no interest in -- a pager moving
        // the window, a skip-taskbar toggle, a compositor marking it above others -- and the same
        // state change also arrives as MapNotify/UnmapNotify. Emitting an event per property
        // change would deliver duplicates for one user action and noise for none; deriving from
        // the difference against what this object last observed delivers exactly one event per
        // real transition.
        bool minimized = false;
        bool maximized = false;
        {
            // A window being closed is unmapped and then destroyed, so this read can race the
            // DestroyNotify already in the queue behind us. That is an ordinary race, not a
            // fault, and the trap is what keeps it from printing an alarming error line every
            // time a user closes a window.
            X11ErrorTrap trap(connection_->GetDisplay());
            window.ReadStateFlags(minimized, maximized);
            trap.Sync();
            if (trap.HasError())
            {
                return;
            }
        }

        const WindowId id = window.GetId();
        if (minimized != window.WasMinimized())
        {
            WindowEvent event;
            event.window = id;
            event.kind = minimized ? WindowEventKind::Minimized : WindowEventKind::Restored;
            destination.emplace_back(event);
        }
        if (maximized != window.WasMaximized())
        {
            WindowEvent event;
            event.window = id;
            event.kind = maximized ? WindowEventKind::Maximized : WindowEventKind::Restored;
            destination.emplace_back(event);
        }
        window.SetObservedState(minimized, maximized);
        window.SetObservedFullscreenMode(window.GetFullscreenMode());
    }

    void X11Platform::TranslateEvent(XEvent& event, std::vector<PlatformEvent>& destination)
    {
        Display* display = connection_->GetDisplay();
        const X11Atoms& atoms = connection_->GetAtoms();

        // A generic event is XInput2's; the opcode is what distinguishes it from any other
        // extension's, and its data has to be fetched before it can be read.
#if defined(CNA_X11_HAVE_XI)
        if (event.type == GenericEvent && connection_->GetXInput2Opcode() >= 0 &&
            event.xcookie.extension == connection_->GetXInput2Opcode())
        {
            if (XGetEventData(display, &event.xcookie) == True)
            {
                if (event.xcookie.evtype == XI_RawMotion && mouse_ != nullptr)
                {
                    const auto* raw = static_cast<const XIRawEvent*>(event.xcookie.data);
                    double deltaX = 0.0;
                    double deltaY = 0.0;
                    const double* values = raw->raw_values;
                    // valuators.mask is a bitmask over the device's axes: only the axes that
                    // actually moved have a value, and they are packed. Walking the mask is the
                    // only correct way to find axis 0 and 1 -- indexing raw_values directly reads
                    // another axis's value whenever the pointer moved in one direction only.
                    for (int axis = 0; axis < raw->valuators.mask_len * 8; ++axis)
                    {
                        if (XIMaskIsSet(raw->valuators.mask, axis) == 0)
                        {
                            continue;
                        }
                        if (axis == 0) { deltaX = *values; }
                        else if (axis == 1) { deltaY = *values; }
                        ++values;
                    }
                    mouse_->AccumulateRawMotion(deltaX, deltaY);
                }
                else if ((event.xcookie.evtype == XI_TouchBegin ||
                          event.xcookie.evtype == XI_TouchUpdate ||
                          event.xcookie.evtype == XI_TouchEnd) &&
                         touch_ != nullptr)
                {
                    // plans/plan_x11.md X11-0155: a touchscreen contact on one of our windows.
                    const auto* device = static_cast<const XIDeviceEvent*>(event.xcookie.data);
                    if (X11Window* target = FindWindowByXid(device->event))
                    {
                        touch_->HandleEvent(*device, *target, destination);
                    }
                }
                else if ((event.xcookie.evtype == XI_ButtonPress ||
                          event.xcookie.evtype == XI_ButtonRelease ||
                          event.xcookie.evtype == XI_Motion) &&
                         touch_ != nullptr)
                {
                    // A pen's own events: only pens' are selected. The core pointer events the
                    // pen drives -- the mouse -- arrive separately, as for any pointer.
                    const auto* device = static_cast<const XIDeviceEvent*>(event.xcookie.data);
                    if (X11Window* target = FindWindowByXid(device->event))
                    {
                        (void) touch_->HandlePenEvent(*device, *target, destination);
                    }
                }
                else if (event.xcookie.evtype == XI_HierarchyChanged)
                {
                    // A device plugged in or out: pens for touch, and every class for the
                    // enumeration's DeviceEvents (X11-0165).
                    if (touch_ != nullptr)
                    {
                        touch_->RefreshPens();
                    }
                    if (inputDevices_ != nullptr)
                    {
                        inputDevices_->HandleHierarchyChanged(destination);
                    }
                }
                XFreeEventData(display, &event.xcookie);
            }
            return;
        }
#endif

#if defined(CNA_X11_HAVE_XRANDR)
        if (connection_->HasRandr() &&
            event.type == connection_->GetRandrEventBase() + RRScreenChangeNotify)
        {
            // Xlib caches the screen configuration; without this call every later query returns
            // the pre-hotplug geometry.
            XRRUpdateConfiguration(&event);
            if (displays_ != nullptr) { displays_->InvalidateCache(); }
            for (const auto& [id, window] : windows_)
            {
                if (window == nullptr) { continue; }
                WindowEvent changed;
                changed.window = id;
                changed.kind = WindowEventKind::DisplayChanged;
                destination.emplace_back(changed);
            }
            return;
        }
#endif

        // plans/plan_x11.md X11-0156: the session's scale changed -- a new Xft.dpi, the settings
        // manager's property, a manager arriving or leaving. The event goes on to its ordinary
        // handling afterwards; nothing else here consumes these.
        if (connection_->GetContentScale().HandleEvent(event))
        {
            if (displays_ != nullptr) { displays_->InvalidateCache(); }
            for (const auto& [id, window] : windows_)
            {
                if (window != nullptr) { UpdateContentScale(*window, destination); }
            }
        }

        if (connection_->HasXkb() && connection_->GetXkbEventBase() >= 0 &&
            event.type == connection_->GetXkbEventBase())
        {
            // A layout switch, a new keymap or a new keyboard. The physical scancode table must
            // not change; the logical KeyCode table must. RefreshKeyboardMapping rebuilds both,
            // which is correct and is the simplest thing that cannot drift -- and it costs
            // several round trips, so it runs only when the layout really changed.
            const auto& xkb = reinterpret_cast<const XkbEvent&>(event);
            bool refresh = false;
            switch (xkb.any.xkb_type)
            {
                case XkbStateNotify:
                    refresh = (xkb.state.changed & XkbGroupStateMask) != 0 &&
                              (keyboard_ == nullptr || xkb.state.group != keyboard_->GetGroup());
                    break;
                case XkbMapNotify:
                    // Xlib answers keysym lookups from its own copy of the keymap, which stays
                    // stale until it is told about the new one.
                    XkbRefreshKeyboardMapping(const_cast<XkbMapNotifyEvent*>(&xkb.map));
                    refresh = true;
                    break;
                case XkbNewKeyboardNotify:
                    refresh = true;
                    break;
                default:
                    break;
            }
            if (refresh && keyboard_ != nullptr)
            {
                keyboard_->RefreshKeyboardMapping();
            }
            return;
        }

        if (event.type == MappingNotify)
        {
            // The core protocol's "the keymap changed" (xmodmap, or a server without XKB). Xlib's
            // cached core mapping has to be refreshed before anything reads it again.
            XRefreshKeyboardMapping(const_cast<XMappingEvent*>(&event.xmapping));
            if (event.xmapping.request != MappingPointer && keyboard_ != nullptr)
            {
                keyboard_->RefreshKeyboardMapping();
            }
            return;
        }

        X11Window* window = FindWindowByXid(event.xany.window);
        const WindowId windowId = window != nullptr ? window->GetId() : 0;

        switch (event.type)
        {
            case Expose:
            {
                // count > 0 means more Expose events for this same damage region are queued.
                // Reporting each would make an application repaint several times for one
                // uncovering; the last one carries count == 0.
                if (event.xexpose.count != 0 || window == nullptr)
                {
                    return;
                }
                WindowEvent mapped;
                mapped.window = windowId;
                mapped.kind = WindowEventKind::Exposed;
                destination.emplace_back(mapped);
                return;
            }
            case ConfigureNotify:
            {
                if (window == nullptr)
                {
                    return;
                }
                const WindowSize cached = window->GetCachedSize();
                const int width = event.xconfigure.width;
                const int height = event.xconfigure.height;
                if (width != cached.width || height != cached.height)
                {
                    window->SetCachedSize(width, height);
                    WindowEvent resized;
                    resized.window = windowId;
                    resized.kind = WindowEventKind::Resized;
                    resized.data1 = width;
                    resized.data2 = height;
                    destination.emplace_back(resized);

                    // X11 has no separate logical and physical window geometry, so a resize is
                    // always both. Emitting only one of them would leave a renderer's swapchain
                    // at the old size on a backend where the two never diverge.
                    WindowEvent pixels;
                    pixels.window = windowId;
                    pixels.kind = WindowEventKind::PixelSizeChanged;
                    pixels.data1 = width;
                    pixels.data2 = height;
                    destination.emplace_back(pixels);
                }

                // send_event marks a synthetic ConfigureNotify from the window manager, which is
                // the ONLY one carrying root-relative coordinates. A real one from the server is
                // relative to the decoration frame, so reporting its x/y as a move would send a
                // reparented window's position wrong by the border width every time it resized.
                if (event.xconfigure.send_event != 0)
                {
                    window->SetCachedPosition(event.xconfigure.x, event.xconfigure.y);
                    WindowEvent moved;
                    moved.window = windowId;
                    moved.kind = WindowEventKind::Moved;
                    moved.data1 = event.xconfigure.x;
                    moved.data2 = event.xconfigure.y;
                    destination.emplace_back(moved);
                }
                // Onto a monitor with a scale of its own (X11-0156). Only where the session gives
                // monitors their own scales: otherwise every monitor has the session's, and a
                // move cannot change it.
                if (connection_->GetContentScale().HasPerMonitorScales())
                {
                    UpdateContentScale(*window, destination);
                }
                return;
            }
            case MapNotify:
            {
                if (window == nullptr) { return; }
                const bool wasMapped = window->IsMapped();
                window->SetMapped(true);
                if (mouse_ != nullptr)
                {
                    mouse_->OnWindowMapped(windowId);
                }
                if (!wasMapped)
                {
                    // A window becoming visible is a return to the normal state, whether it was
                    // hidden, iconified, or is being shown for the first time.
                    WindowEvent restored;
                    restored.window = windowId;
                    restored.kind = WindowEventKind::Restored;
                    destination.emplace_back(restored);
                }
                EmitWindowStateTransitions(*window, destination);
                return;
            }
            case UnmapNotify:
            {
                if (window == nullptr) { return; }
                window->SetMapped(false);
                if (mouse_ != nullptr)
                {
                    mouse_->OnWindowUnmapped(windowId);
                }
                // An unmap is how iconification looks on the wire under ICCCM: the window manager
                // unmaps the window and sets WM_STATE to IconicState. Distinguishing that from an
                // application's own Hide() means asking the server which it was, which is exactly
                // what the transition helper does -- and it emits nothing when the answer has not
                // changed, so a Hide() produces no phantom Minimized.
                EmitWindowStateTransitions(*window, destination);
                return;
            }
            case PropertyNotify:
            {
                if (window == nullptr)
                {
                    if (event.xproperty.window == connection_->GetRoot() &&
                        (event.xproperty.atom == atoms.netSupported ||
                         event.xproperty.atom == atoms.netSupportingWmCheck))
                    {
                        // A window manager started, stopped or was replaced while the application
                        // was running. Re-reading is what keeps the graceful-degradation checks
                        // honest rather than frozen at startup.
                        connection_->RefreshWindowManagerState();
                    }
                    return;
                }
                if (event.xproperty.atom == atoms.netWmState)
                {
                    // Before the transitions are reported: a window taken out of fullscreen by
                    // someone else gives its display mode back first (X11-0153). The trap is for
                    // the same destroy race EmitWindowStateTransitions guards against.
                    X11ErrorTrap trap(display);
                    window->OnNetWmStateChanged();
                    trap.Sync();
                }
                if (event.xproperty.atom == atoms.netWmState ||
                    event.xproperty.atom == atoms.wmState)
                {
                    EmitWindowStateTransitions(*window, destination);
                }
                return;
            }
            case FocusIn:
            case FocusOut:
            {
                if (window == nullptr ||
                    !IsRealFocusChange(event.xfocus.mode, event.xfocus.detail))
                {
                    return;
                }
                const bool gained = event.type == FocusIn;
                window->SetFocused(gained);
                // An exclusive-fullscreen window gives the desktop its mode back when it loses
                // focus, and takes it again with focus (X11-0153).
                window->OnFocusChanged(gained);
                if (mouse_ != nullptr)
                {
                    // Relative mode's grab is held only while its window has focus.
                    mouse_->OnFocusChanged(windowId, gained);
                }
                if (textInput_ != nullptr)
                {
                    textInput_->SetFocusedWindow(gained ? window : nullptr);
                }
                if (!gained && keyboard_ != nullptr)
                {
                    // Every key the application believed was held is released on focus loss. The
                    // alternative is a key that sticks down forever, because the KeyRelease will
                    // be delivered to whichever window has focus now.
                    keyboard_->ReleaseAllKeys();
                }
                WindowEvent focus;
                focus.window = windowId;
                focus.kind = gained ? WindowEventKind::FocusGained : WindowEventKind::FocusLost;
                destination.emplace_back(focus);
                return;
            }
            case ClientMessage:
            {
                // A drag from another client (plans/plan_x11.md X11-0154).
                if (window != nullptr && dragAndDrop_ != nullptr &&
                    dragAndDrop_->HandleClientMessage(event.xclient, *window, destination))
                {
                    return;
                }
                if (event.xclient.message_type != atoms.wmProtocols ||
                    static_cast<Atom>(event.xclient.data.l[0]) != atoms.wmDeleteWindow)
                {
                    return;
                }
                // A request still queued for a window the application has already destroyed --
                // a second click on the close button, typically. There is no window left to ask,
                // and treating it as "the last window closing" would end an application whose
                // other windows are still open.
                if (window == nullptr)
                {
                    return;
                }
                WindowEvent close;
                close.window = windowId;
                close.kind = WindowEventKind::CloseRequested;
                destination.emplace_back(close);

                // The window is deliberately NOT destroyed here. WM_DELETE_WINDOW is a request,
                // and the contract says the application answers it -- a game may want to show a
                // "save first?" prompt. A QuitEvent follows only when the last window is being
                // asked to close, which is what makes closing a secondary window not end the
                // application.
                if (windows_.size() <= 1)
                {
                    destination.emplace_back(QuitEvent{});
                }
                return;
            }
            case DestroyNotify:
            {
                if (window == nullptr) { return; }
                // The window is gone at the server -- destroyed by the window manager, by another
                // client, or by a user closing it. Telling the wrapper first is what stops its
                // destructor calling XDestroyWindow on an XID the server has already released;
                // dropping it from the registry then stops a later event resolving to a window
                // whose XID has since been handed to somebody else.
                window->MarkDestroyedByServer();
                if (dragAndDrop_ != nullptr)
                {
                    dragAndDrop_->ForgetWindow(event.xdestroywindow.window);
                }
                if (touch_ != nullptr)
                {
                    touch_->ForgetWindow(windowId, event.xdestroywindow.window);
                }
                ForgetWindow(windowId);
                return;
            }
            case KeyPress:
            case KeyRelease:
            {
                if (keyboard_ == nullptr)
                {
                    return;
                }
                if (event.xkey.keycode == 0)
                {
                    // Keycode 0 is no key: it is how Xlib delivers an input method's commit, a
                    // press whose only content is the committed string. Text, not a key event --
                    // a game must not see a press of "no key" (plans/plan_x11.md X11-0152).
                    std::string text;
                    if (event.type == KeyPress && textInput_ != nullptr &&
                        textInput_->IsActive(windowId) && textInput_->LookupText(window, event.xkey, text))
                    {
                        // The composition the commit ended goes first (LookupText).
                        textInput_->TakeEditingEvents(destination);
                        TextInputEvent input;
                        input.window = windowId;
                        input.text = std::move(text);
                        destination.emplace_back(std::move(input));
                    }
                    return;
                }
                if (event.type == KeyRelease && !connection_->HasDetectableAutoRepeat())
                {
                    // Without detectable auto-repeat the server sends release+press pairs for a
                    // held key. Peeking one event ahead and dropping both halves is the only way
                    // to avoid telling the game the player let go several times a second.
                    if (XPending(display) > 0)
                    {
                        XEvent next;
                        XPeekEvent(display, &next);
                        if (IsAutoRepeatPair(event.xkey, next))
                        {
                            XNextEvent(display, &next);
                            KeyEvent repeat;
                            repeat.window = windowId;
                            repeat.scancode = keyboard_->GetScancode(event.xkey.keycode);
                            repeat.keycode = keyboard_->GetKeyCode(event.xkey.keycode);
                            repeat.modifiers = ModifiersFromXState(event.xkey.state,
                                                                    keyboard_->GetModeSwitchMask());
                            repeat.pressed = true;
                            repeat.repeat = true;
                            destination.emplace_back(repeat);

                            std::string text;
                            if (textInput_ != nullptr && textInput_->IsActive(windowId) &&
                                textInput_->LookupText(window, next.xkey, text))
                            {
                                textInput_->TakeEditingEvents(destination);
                                TextInputEvent input;
                                input.window = windowId;
                                input.text = std::move(text);
                                destination.emplace_back(std::move(input));
                            }
                            return;
                        }
                    }
                }

                const bool pressed = event.type == KeyPress;
                const bool repeat = keyboard_->TrackKeyState(event.xkey.keycode, pressed);

                KeyEvent key;
                key.window = windowId;
                key.scancode = keyboard_->GetScancode(event.xkey.keycode);
                key.keycode = keyboard_->GetKeyCode(event.xkey.keycode);
                key.modifiers =
                    ModifiersFromXState(event.xkey.state, keyboard_->GetModeSwitchMask());
                key.pressed = pressed;
                key.repeat = repeat;
                destination.emplace_back(key);

                if (pressed && textInput_ != nullptr && textInput_->IsActive(windowId))
                {
                    std::string text;
                    if (textInput_->LookupText(window, event.xkey, text))
                    {
                        // The composition the commit ended goes first (LookupText).
                        textInput_->TakeEditingEvents(destination);
                        TextInputEvent input;
                        input.window = windowId;
                        input.text = std::move(text);
                        destination.emplace_back(std::move(input));
                    }
                }
                return;
            }
            case ButtonPress:
            case ButtonRelease:
            {
                const WheelDirection wheel = ClassifyWheelButton(event.xbutton.button);
                if (wheel != WheelDirection::None)
                {
                    // A wheel notch is a press AND a release of the same button. Reporting both
                    // would double every scroll, so only the press is turned into wheel motion.
                    if (event.type != ButtonPress)
                    {
                        return;
                    }
                    MouseWheelEvent scroll;
                    scroll.window = windowId;
                    switch (wheel)
                    {
                        case WheelDirection::Up: scroll.y = 1.0f; break;
                        case WheelDirection::Down: scroll.y = -1.0f; break;
                        case WheelDirection::Left: scroll.x = -1.0f; break;
                        case WheelDirection::Right: scroll.x = 1.0f; break;
                        case WheelDirection::None: break;
                    }
                    if (mouse_ != nullptr)
                    {
                        mouse_->AccumulateScroll(static_cast<int>(scroll.x),
                                                 static_cast<int>(scroll.y));
                    }
                    destination.emplace_back(scroll);
                    return;
                }

                const std::uint8_t button = MapButtonNumber(event.xbutton.button);
                if (button == 0)
                {
                    return;
                }
                const bool pressed = event.type == ButtonPress;
                if (mouse_ != nullptr)
                {
                    mouse_->SetButtonState(button, pressed);
                    mouse_->SetLastPosition(windowId, event.xbutton.x, event.xbutton.y);
                }

                std::uint8_t clicks = 1;
                if (pressed)
                {
                    const bool sameButton = event.xbutton.button == lastClickButton_;
                    const bool soonEnough =
                        event.xbutton.time >= lastClickTime_ &&
                        (event.xbutton.time - lastClickTime_) <= kDoubleClickMilliseconds;
                    const bool nearEnough =
                        std::abs(event.xbutton.x - lastClickX_) <= kDoubleClickSlopPixels &&
                        std::abs(event.xbutton.y - lastClickY_) <= kDoubleClickSlopPixels;
                    clickCount_ = (sameButton && soonEnough && nearEnough)
                                      ? static_cast<std::uint8_t>(clickCount_ + 1)
                                      : static_cast<std::uint8_t>(1);
                    clicks = clickCount_;
                    lastClickTime_ = event.xbutton.time;
                    lastClickButton_ = event.xbutton.button;
                    lastClickX_ = event.xbutton.x;
                    lastClickY_ = event.xbutton.y;
                }

                MouseButtonEvent mapped;
                mapped.window = windowId;
                mapped.button = button;
                mapped.pressed = pressed;
                mapped.clicks = clicks;
                mapped.x = static_cast<float>(event.xbutton.x);
                mapped.y = static_cast<float>(event.xbutton.y);
                destination.emplace_back(mapped);
                return;
            }
            case MotionNotify:
            {
                MouseMotionEvent motion;
                motion.window = windowId;
                motion.x = static_cast<float>(event.xmotion.x);
                motion.y = static_cast<float>(event.xmotion.y);
                if (hasLastMotion_)
                {
                    motion.deltaX = static_cast<float>(event.xmotion.x - lastMotionX_);
                    motion.deltaY = static_cast<float>(event.xmotion.y - lastMotionY_);
                }
                lastMotionX_ = event.xmotion.x;
                lastMotionY_ = event.xmotion.y;
                hasLastMotion_ = true;
                if (mouse_ != nullptr)
                {
                    mouse_->SetLastPosition(windowId, event.xmotion.x, event.xmotion.y);
                }
                destination.emplace_back(motion);
                return;
            }
            case EnterNotify:
            case LeaveNotify:
            {
                // Not a CNA event -- the taxonomy has no enter/leave -- but the pointer's window
                // has changed, and the snapshot must follow it or the next Update() would query
                // the wrong window's coordinate space. A LeaveNotify additionally resets the
                // motion baseline so the first motion after re-entering is not a huge delta.
                if (mouse_ != nullptr && event.type == EnterNotify)
                {
                    mouse_->SetLastPosition(windowId, event.xcrossing.x, event.xcrossing.y);
                }
                hasLastMotion_ = false;
                return;
            }
            default:
                return;
        }
    }

    std::uint64_t X11Platform::GetPerformanceCounter() const
    {
        return Posix::MonotonicNanoseconds();
    }

    std::uint64_t X11Platform::GetPerformanceFrequency() const
    {
        return 1000000000uLL;
    }

    std::uint64_t X11Platform::GetTicksMilliseconds() const
    {
        const auto elapsed = std::chrono::steady_clock::now() - epoch_;
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
    }

    void X11Platform::Delay(const std::uint32_t milliseconds)
    {
        Posix::SleepMilliseconds(milliseconds);
    }

    // The controller subsystem is started by the first question about controllers rather than at
    // construction, as the SDL3 backend does: opening /dev/input reads every node's description,
    // and a game that never asks about controllers should not pay for that. Game::UpdateInput()
    // only pumps these services once the subsystem is initialised, so its per-frame call cannot be
    // what starts it.
    void X11Platform::EnsureControllerSubsystem()
    {
        if (controllerSubsystemEnsured_)
        {
            return;
        }
        controllerSubsystemEnsured_ = true;
        AcquireSubsystem(PlatformSubsystem::Gamepad);
#ifdef CNA_PLATFORM_HAVE_EVDEV
        // The pump has been skipped for every frame up to this one; without this the first query
        // would answer about a device list nothing has read yet.
        controllers_->gamepad.Update();
        controllers_->joystick.Update();
#endif
    }

    IPlatformGamepad* X11Platform::GetGamepad()
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

    IPlatformJoystick* X11Platform::GetJoystick()
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

    IPlatformHaptics* X11Platform::GetHaptics()
    {
#ifdef CNA_PLATFORM_HAVE_EVDEV
        return controllers_ != nullptr ? &controllers_->haptics : nullptr;
#else
        return nullptr;
#endif
    }

    IPlatformKeyboard* X11Platform::GetKeyboard() { return keyboard_.get(); }
    IPlatformMouse* X11Platform::GetMouse() { return mouse_.get(); }
    IPlatformTextInput* X11Platform::GetTextInput() { return textInput_.get(); }
    IPlatformClipboard* X11Platform::GetClipboard() { return clipboard_.get(); }
    IPlatformInputDevices* X11Platform::GetInputDevices() { return inputDevices_.get(); }

    std::vector<InputDeviceInfo> X11Platform::ControllerDevices(const InputDeviceKind kind)
    {
        std::vector<InputDeviceInfo> devices;
#ifdef CNA_PLATFORM_HAVE_EVDEV
        if (controllers_ == nullptr)
        {
            return devices;
        }
        if (kind == InputDeviceKind::Haptic)
        {
            // Force feedback, from sysfs; nothing is opened to list it (X11-0168).
            for (const HapticInfo& haptic : controllers_->haptics.GetHaptics())
            {
                devices.push_back({haptic.id, kind, haptic.name});
            }
            return devices;
        }
        // Asking what controllers are attached is asking about controllers: the hub starts as it
        // does for GetGamepad(), and is read now, since enumeration answers for this moment.
        EnsureControllerSubsystem();
        controllers_->hub.Pump();
        for (const auto& controller : controllers_->hub.GetControllers())
        {
            // Every controller is a joystick; a gamepad is also a gamepad, under the same id -- as
            // the DeviceEvents say.
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
    IPlatformClipboard* X11Platform::GetPrimarySelection() { return primarySelection_.get(); }

    IPlatformDisplays* X11Platform::GetDisplays()
    {
        // Non-null exactly when multipleDisplays is true. Without RandR this backend can only
        // report the whole screen, which is not display enumeration -- and the contract's rule is
        // that the accessor and the capability agree.
        if (connection_ == nullptr || !connection_->HasRandr())
        {
            return nullptr;
        }
        return displays_.get();
    }

    IPlatformGlContext* X11Platform::GetGlContext()
    {
        if (glContext_ == nullptr || !glContext_->IsAvailable())
        {
            return nullptr;
        }
        return glContext_.get();
    }

    IPlatformVulkanSurface* X11Platform::GetVulkanSurface() { return vulkanSurface_.get(); }

    std::unique_ptr<IPlatformSurfacePresenter> X11Platform::CreateSurfacePresenter(
        IPlatformWindow& window)
    {
        auto* x11Window = dynamic_cast<X11Window*>(&window);
        if (x11Window == nullptr)
        {
            throw PlatformException("X11Platform::CreateSurfacePresenter",
                                    "the window was not created by this platform");
        }
        return std::make_unique<X11SurfacePresenter>(*x11Window);
    }

} // namespace CNA::Platform::X11
