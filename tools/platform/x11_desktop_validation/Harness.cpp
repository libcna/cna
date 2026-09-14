// SPDX-License-Identifier: MS-PL

#include "Harness.hpp"
#include "UInput.hpp"

#if defined(__linux__)
#  include <linux/input-event-codes.h>
#else
#  define KEY_RIGHTCTRL 97
#endif

#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformFactory.hpp"


#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <unistd.h>
#include <variant>

namespace CnaX11Validation {

    namespace {

        using CNA::Platform::X11::kCurrentTime;
        using CNA::Platform::X11::kNone;
        using CNA::Platform::X11::kXFalse;
        using CNA::Platform::X11::kXTrue;

        int g_driverErrors = 0;

        int DriverErrorHandler(::Display*, XErrorEvent*)
        {
            ++g_driverErrors;
            return 0;
        }

        void Emit(const char* tag, const std::string& check, const std::string& detail)
        {
            std::cout << tag << ' ' << check;
            if (!detail.empty())
            {
                std::cout << " -- " << detail;
            }
            std::cout << std::endl;
        }

    } // namespace

    // --- reporting ------------------------------------------------------------------------------

    Tally& Results()
    {
        static Tally tally;
        return tally;
    }

    void Pass(const std::string& check, const std::string& detail)
    {
        ++Results().passed;
        Emit("PASS", check, detail);
    }

    void Fail(const std::string& check, const std::string& detail)
    {
        ++Results().failed;
        Emit("FAIL", check, detail);
    }

    void Skip(const std::string& check, const std::string& reason)
    {
        ++Results().skipped;
        Emit("SKIP", check, reason);
    }

    void Info(const std::string& text)
    {
        std::cout << "INFO " << text << std::endl;
    }

    bool Check(const bool condition, const std::string& check, const std::string& detail)
    {
        if (condition)
        {
            Pass(check, detail);
        }
        else
        {
            Fail(check, detail);
        }
        return condition;
    }

    // --- process resources ----------------------------------------------------------------------

    ProcessSample SampleProcess()
    {
        ProcessSample sample;
        std::ifstream status("/proc/self/status");
        std::string line;
        while (std::getline(status, line))
        {
            if (line.rfind("VmRSS:", 0) == 0)
            {
                sample.residentKb = std::strtol(line.c_str() + 6, nullptr, 10);
            }
        }
        if (DIR* directory = opendir("/proc/self/fd"))
        {
            while (const dirent* entry = readdir(directory))
            {
                if (entry->d_name[0] != '.')
                {
                    ++sample.openDescriptors;
                }
            }
            closedir(directory);
            --sample.openDescriptors;  // the directory stream itself
        }
        return sample;
    }

    // --- Driver ---------------------------------------------------------------------------------

    Driver::Driver()
    {
        display_ = XOpenDisplay(nullptr);
    }

    Driver::~Driver()
    {
        if (display_ != nullptr)
        {
            XCloseDisplay(display_);
        }
    }

    Time Driver::ServerTime()
    {
        // A window manager checks a request's timestamp against the user's last interaction and
        // pings the window with it; mutter refuses CurrentTime ("tried to ping window with a bad
        // serial"). The X server's own clock is read the ICCCM way: a zero-length append to a
        // property on a window of ours, whose PropertyNotify carries the server time.
        if (timeWindow_ == 0)
        {
            XSetWindowAttributes attributes{};
            attributes.event_mask = PropertyChangeMask;
            timeWindow_ = XCreateWindow(display_, DefaultRootWindow(display_), -10, -10, 1, 1, 0,
                                        CopyFromParent, InputOnly, CopyFromParent, CWEventMask,
                                        &attributes);
        }
        const Atom property = XInternAtom(display_, "CNA_VALIDATION_TIME", kXFalse);
        XChangeProperty(display_, timeWindow_, property, XA_STRING, 8, PropModeAppend, nullptr, 0);
        XEvent event{};
        XWindowEvent(display_, timeWindow_, PropertyChangeMask, &event);
        return event.xproperty.time;
    }

    bool Driver::Activate(const ::Window window)
    {
        if (display_ == nullptr)
        {
            return false;
        }
        XEvent event{};
        event.type = ClientMessage;
        event.xclient.window = window;
        event.xclient.message_type = XInternAtom(display_, "_NET_ACTIVE_WINDOW", kXFalse);
        event.xclient.format = 32;
        event.xclient.data.l[0] = 2;  // source indication: a pager, which is honoured
        event.xclient.data.l[1] = static_cast<long>(ServerTime());
        XSendEvent(display_, DefaultRootWindow(display_), kXFalse,
                   SubstructureRedirectMask | SubstructureNotifyMask, &event);
        XSync(display_, kXFalse);
        return true;
    }

    bool Driver::CloseThroughWindowManager(const ::Window window)
    {
        if (display_ == nullptr)
        {
            return false;
        }
        XEvent event{};
        event.type = ClientMessage;
        event.xclient.window = window;
        event.xclient.message_type = XInternAtom(display_, "_NET_CLOSE_WINDOW", kXFalse);
        event.xclient.format = 32;
        event.xclient.data.l[0] = static_cast<long>(ServerTime());
        event.xclient.data.l[1] = 2;
        XSendEvent(display_, DefaultRootWindow(display_), kXFalse,
                   SubstructureRedirectMask | SubstructureNotifyMask, &event);
        XSync(display_, kXFalse);
        return true;
    }

    bool Driver::Move(const ::Window window, const int x, const int y)
    {
        if (display_ == nullptr)
        {
            return false;
        }
        // _NET_MOVERESIZE_WINDOW names the CLIENT position with StaticGravity semantics when
        // bit 8-11 carry gravity 10 -- which is what "move my window's client area to (x, y)"
        // means. A window manager that does not implement it still sees the redirected
        // XMoveWindow below.
        XEvent event{};
        event.type = ClientMessage;
        event.xclient.window = window;
        event.xclient.message_type = XInternAtom(display_, "_NET_MOVERESIZE_WINDOW", kXFalse);
        event.xclient.format = 32;
        event.xclient.data.l[0] = StaticGravity | (1L << 8) | (1L << 9) | (2L << 12);
        event.xclient.data.l[1] = x;
        event.xclient.data.l[2] = y;
        XSendEvent(display_, DefaultRootWindow(display_), kXFalse,
                   SubstructureRedirectMask | SubstructureNotifyMask, &event);
        XSync(display_, kXFalse);
        return true;
    }

    ::Window Driver::FocusedWindow() const
    {
        ::Window focus = kNone;
        int revert = 0;
        if (display_ != nullptr)
        {
            XGetInputFocus(display_, &focus, &revert);
        }
        return focus;
    }

    bool Driver::FocusIsWithin(const ::Window window) const
    {
        ::Window focus = FocusedWindow();
        while (focus != kNone && focus != PointerRoot)
        {
            if (focus == window)
            {
                return true;
            }
            ::Window root = kNone;
            ::Window parent = kNone;
            ::Window* children = nullptr;
            unsigned int count = 0;
            if (XQueryTree(display_, focus, &root, &parent, &children, &count) == 0)
            {
                return false;
            }
            if (children != nullptr)
            {
                XFree(children);
            }
            if (parent == root)
            {
                return false;
            }
            focus = parent;
        }
        return false;
    }

    int Driver::ProbePointerGrab()
    {
        if (display_ == nullptr)
        {
            return -1;
        }
        const int status = XGrabPointer(display_, DefaultRootWindow(display_), kXFalse,
                                        ButtonPressMask, GrabModeAsync, GrabModeAsync, kNone,
                                        kNone, kCurrentTime);
        if (status == GrabSuccess)
        {
            XUngrabPointer(display_, kCurrentTime);
        }
        XSync(display_, kXFalse);
        return status;
    }

    ::Window Driver::CreatePlainWindow(const std::string& title, const int x, const int y,
                                       const int width, const int height)
    {
        if (display_ == nullptr)
        {
            return kNone;
        }
        const int screen = DefaultScreen(display_);
        const ::Window window = XCreateSimpleWindow(
            display_, DefaultRootWindow(display_), x, y, static_cast<unsigned int>(width),
            static_cast<unsigned int>(height), 0, BlackPixel(display_, screen),
            WhitePixel(display_, screen));
        XStoreName(display_, window, title.c_str());
        XMapWindow(display_, window);
        XSync(display_, kXFalse);
        return window;
    }

    void Driver::DestroyWindow(const ::Window window)
    {
        if (display_ != nullptr && window != kNone)
        {
            XDestroyWindow(display_, window);
            XSync(display_, kXFalse);
        }
    }

    long Driver::ClientResources(const std::uintptr_t anyXid,
                                 std::map<std::string, long>* byType) const
    {
#if defined(CNA_X11_VALIDATION_HAVE_XRES)
        if (display_ == nullptr)
        {
            resourceProblem_ = "no driver connection";
            return -1;
        }
        int eventBase = 0;
        int errorBase = 0;
        if (XResQueryExtension(display_, &eventBase, &errorBase) == 0)
        {
            resourceProblem_ = "the X server has no X-Resource extension";
            return -1;
        }
        // A client's resources share its resource base; XResQueryClients lists every client's
        // base and mask, which is how an XID is mapped back to the connection that created it.
        int clientCount = 0;
        XResClient* clients = nullptr;
        if (XResQueryClients(display_, &clientCount, &clients) == 0)
        {
            resourceProblem_ = "XResQueryClients failed";
            return -1;
        }
        XID base = 0;
        bool found = false;
        for (int index = 0; index < clientCount; ++index)
        {
            if ((static_cast<XID>(anyXid) & ~clients[index].resource_mask) ==
                clients[index].resource_base)
            {
                base = clients[index].resource_base;
                found = true;
                break;
            }
        }
        XFree(clients);
        if (!found)
        {
            char xid[32] = {};
            std::snprintf(xid, sizeof(xid), "0x%lx", static_cast<unsigned long>(anyXid));
            resourceProblem_ = std::string("no client owns XID ") + xid;
            return -1;
        }
        int typeCount = 0;
        XResType* types = nullptr;
        if (XResQueryClientResources(display_, base, &typeCount, &types) == 0)
        {
            resourceProblem_ = "XResQueryClientResources failed";
            return -1;
        }
        long total = 0;
        for (int index = 0; index < typeCount; ++index)
        {
            total += types[index].count;
            if (byType != nullptr)
            {
                char* name = XGetAtomName(display_, types[index].resource_type);
                (*byType)[name != nullptr ? name : "?"] = types[index].count;
                if (name != nullptr) { XFree(name); }
            }
        }
        XFree(types);
        resourceProblem_.clear();
        return total;
#else
        (void) anyXid;
        (void) byType;
        resourceProblem_ = "built without X-Resource (libXRes development files)";
        return -1;
#endif
    }

    bool Driver::PointerInside(const ::Window window, int& windowX, int& windowY, int& rootX,
                               int& rootY) const
    {
        if (display_ == nullptr)
        {
            return false;
        }
        ::Window root = kNone;
        ::Window child = kNone;
        unsigned int mask = 0;
        if (XQueryPointer(display_, window, &root, &child, &rootX, &rootY, &windowX, &windowY,
                          &mask) == 0)
        {
            return false;
        }
        XWindowAttributes attributes{};
        if (XGetWindowAttributes(display_, window, &attributes) == 0)
        {
            return false;
        }
        return windowX >= 0 && windowY >= 0 && windowX < attributes.width &&
               windowY < attributes.height;
    }

    void Driver::Sync() const
    {
        if (display_ != nullptr)
        {
            XSync(display_, kXFalse);
        }
    }

    int Driver::ErrorCount() const
    {
        return g_driverErrors;
    }

    // --- Session --------------------------------------------------------------------------------

    Session::Session()
    {
        try
        {
            platform_ = PlatformFactory::Create("X11");
            platform_->AcquireSubsystem(PlatformSubsystem::Video);
            acquired_ = true;
        }
        catch (const std::exception& error)
        {
            error_ = error.what();
        }
    }

    Session::~Session()
    {
        if (platform_ != nullptr && acquired_)
        {
            platform_->ReleaseSubsystem(PlatformSubsystem::Video);
        }
    }

    std::unique_ptr<IPlatformWindow> Session::Make(const std::string& title, const int width,
                                                   const int height, const bool visible,
                                                   const WindowRenderIntent intent, const int x,
                                                   const int y)
    {
        WindowDescription description;
        description.title = title;
        description.width = width;
        description.height = height;
        description.centered = false;
        description.x = x;
        description.y = y;
        description.visible = visible;
        description.renderIntent = intent;
        if (intent == WindowRenderIntent::OpenGl)
        {
            description.openGlFramebuffer.depthBits = 24;
            description.openGlFramebuffer.stencilBits = 8;
            description.openGlFramebuffer.doubleBuffered = true;
        }
        return platform_->CreateWindow(description);
    }

    const std::vector<PlatformEvent>& Session::Poll()
    {
        platform_->PollEvents(batch_);
        seen_.insert(seen_.end(), batch_.begin(), batch_.end());
        return batch_;
    }

    bool Session::PumpUntil(const std::function<bool()>& predicate,
                            const std::chrono::milliseconds budget)
    {
        const auto deadline = std::chrono::steady_clock::now() + budget;
        while (true)
        {
            Poll();
            if (predicate())
            {
                return true;
            }
            if (std::chrono::steady_clock::now() >= deadline)
            {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    void Session::PumpFor(const std::chrono::milliseconds duration)
    {
        PumpUntil([] { return false; }, duration);
    }

    bool Session::Saw(const WindowId window, const WindowEventKind kind) const
    {
        return Count(window, kind) > 0;
    }

    int Session::Count(const WindowId window, const WindowEventKind kind) const
    {
        return static_cast<int>(
            std::count_if(seen_.begin(), seen_.end(), [window, kind](const PlatformEvent& event) {
                const auto* windowEvent = std::get_if<WindowEvent>(&event);
                return windowEvent != nullptr && windowEvent->window == window &&
                       windowEvent->kind == kind;
            }));
    }

    bool Session::SawQuit() const
    {
        return std::any_of(seen_.begin(), seen_.end(), [](const PlatformEvent& event) {
            return std::holds_alternative<QuitEvent>(event);
        });
    }

    bool Session::WaitFor(const WindowId window, const WindowEventKind kind,
                          const std::chrono::milliseconds budget)
    {
        return PumpUntil([this, window, kind] { return Saw(window, kind); }, budget);
    }

    bool Session::Focus(IPlatformWindow& window, Driver& driver,
                        const std::chrono::milliseconds budget)
    {
        if (window.HasFocus())
        {
            return true;
        }
        const auto deadline = std::chrono::steady_clock::now() + budget;
        while (std::chrono::steady_clock::now() < deadline)
        {
            driver.Activate(static_cast<::Window>(window.GetWindowHandle()));
            if (PumpUntil([&window] { return window.HasFocus(); }, std::chrono::milliseconds(300)))
            {
                return true;
            }
        }
        return window.HasFocus();
    }

    // --- pointer steering -----------------------------------------------------------------------

    bool SteerPointerInto(VirtualInput& mouse, Driver& driver, const ::Window window,
                          const int targetX, const int targetY)
    {
        ::Display* display = driver.GetDisplay();
        if (display == nullptr)
        {
            return false;
        }
        const auto pause = [] { std::this_thread::sleep_for(std::chrono::milliseconds(4)); };

        // Bottom-left corner of the whole desktop. Clamping at the edge makes the start exact
        // whatever pointer acceleration the session applies.
        for (int step = 0; step < 60; ++step)
        {
            mouse.Move(-120, 120);
            pause();
        }

        int rootX = 0;
        int rootY = 0;
        ::Window child = kNone;
        XTranslateCoordinates(display, window, DefaultRootWindow(display), 0, 0, &rootX, &rootY,
                              &child);
        XWindowAttributes attributes{};
        XGetWindowAttributes(display, window, &attributes);
        const int screenHeight = DisplayHeight(display, DefaultScreen(display));
        const double goalX = rootX + attributes.width / 2.0;
        const double goalY = rootY + attributes.height / 2.0;
        const double startX = 0.0;
        const double startY = screenHeight - 1.0;
        const double length = std::max(1.0, std::hypot(goalX - startX, goalY - startY));
        const double unitX = (goalX - startX) / length;
        const double unitY = (goalY - startY) / length;

        // Head for the window's centre in small steps until the X server reports the cursor
        // inside it. Small steps keep libinput's acceleration near its slow-speed floor, and the
        // direction is right whatever the exact gain is.
        double carryX = 0.0;
        double carryY = 0.0;
        int windowX = -1;
        int windowY = -1;
        int queryRootX = 0;
        int queryRootY = 0;
        bool inside = false;
        for (int step = 0; step < 3000 && !inside; ++step)
        {
            carryX += unitX * 3.0;
            carryY += unitY * 3.0;
            const int moveX = static_cast<int>(carryX);
            const int moveY = static_cast<int>(carryY);
            carryX -= moveX;
            carryY -= moveY;
            mouse.Move(moveX, moveY);
            pause();
            if (step % 4 == 0)
            {
                driver.Sync();
                inside = driver.PointerInside(window, windowX, windowY, queryRootX, queryRootY) &&
                         windowX > 2 && windowY > 2 && windowX < attributes.width - 2 &&
                         windowY < attributes.height - 2;
            }
        }
        if (!inside)
        {
            return false;
        }

        // Exact feedback from here on.
        for (int step = 0; step < 600; ++step)
        {
            driver.Sync();
            driver.PointerInside(window, windowX, windowY, queryRootX, queryRootY);
            const int deltaX = targetX - windowX;
            const int deltaY = targetY - windowY;
            if (std::abs(deltaX) <= 1 && std::abs(deltaY) <= 1)
            {
                return true;
            }
            mouse.Move(std::clamp(deltaX, -3, 3), std::clamp(deltaY, -3, 3));
            pause();
        }
        return false;
    }

    bool UinputReachesDisplay(Driver& driver, const std::vector<std::string>& arguments,
                              std::string& reason)
    {
        if (OptionFlag(arguments, "native-x-input"))
        {
            return true;
        }
        int count = 0;
        char** extensions = XListExtensions(driver.GetDisplay(), &count);
        bool xwayland = false;
        for (int index = 0; index < count; ++index)
        {
            xwayland = xwayland || std::strcmp(extensions[index], "XWAYLAND") == 0;
        }
        XFreeExtensionList(extensions);
        if (!xwayland)
        {
            reason = "this X server is not Xwayland, so a uinput device would feed the real "
                     "desktop instead of it; pass --native-x-input on a native Xorg session";
        }
        return xwayland;
    }

    bool ProbeKeyboardReachesWindow(VirtualInput& keyboard, Session& session,
                                    IPlatformWindow& window)
    {
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            session.Clear();
            keyboard.Tap(KEY_RIGHTCTRL, 20);
            const bool arrived = session.PumpUntil(
                [&session, &window] {
                    for (const PlatformEvent& event : session.Seen())
                    {
                        const auto* key = std::get_if<KeyEvent>(&event);
                        if (key != nullptr && key->window == window.GetId() &&
                            key->scancode == Scancode::RightControl)
                        {
                            return true;
                        }
                    }
                    return false;
                },
                std::chrono::milliseconds(700));
            if (arrived)
            {
                return true;
            }
        }
        return false;
    }

    // --- helpers --------------------------------------------------------------------------------

    WindowId EventWindow(const PlatformEvent& event)
    {
        if (const auto* window = std::get_if<WindowEvent>(&event)) { return window->window; }
        if (const auto* key = std::get_if<KeyEvent>(&event)) { return key->window; }
        if (const auto* text = std::get_if<TextInputEvent>(&event)) { return text->window; }
        if (const auto* button = std::get_if<MouseButtonEvent>(&event)) { return button->window; }
        if (const auto* motion = std::get_if<MouseMotionEvent>(&event)) { return motion->window; }
        if (const auto* wheel = std::get_if<MouseWheelEvent>(&event)) { return wheel->window; }
        return 0;
    }

    std::string Signed(const long value)
    {
        return (value >= 0 ? "+" : "") + std::to_string(value);
    }

    double NowMs()
    {
        return std::chrono::duration<double, std::milli>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    long OptionInt(const std::vector<std::string>& arguments, const std::string& name,
                   const long fallback)
    {
        const std::string prefix = "--" + name + "=";
        for (std::size_t index = 0; index < arguments.size(); ++index)
        {
            if (arguments[index].rfind(prefix, 0) == 0)
            {
                return std::strtol(arguments[index].c_str() + prefix.size(), nullptr, 10);
            }
            if (arguments[index] == "--" + name && index + 1 < arguments.size())
            {
                return std::strtol(arguments[index + 1].c_str(), nullptr, 10);
            }
        }
        return fallback;
    }

    bool OptionFlag(const std::vector<std::string>& arguments, const std::string& name)
    {
        return std::find(arguments.begin(), arguments.end(), "--" + name) != arguments.end();
    }

} // namespace CnaX11Validation

namespace CnaX11Validation::Detail {

    /** @brief Installs the driver's non-fatal error handler once per process. */
    void InstallDriverErrorHandler()
    {
        static const bool installed = [] {
            // Chained deliberately NOT: CNA installs its own policy on its own connection and
            // forwards errors for connections it does not own to the previous handler, which is
            // this one. Counting them here is what lets a scenario assert "no X errors".
            XSetErrorHandler(DriverErrorHandler);
            return true;
        }();
        (void) installed;
    }

} // namespace CnaX11Validation::Detail
