// SPDX-License-Identifier: MS-PL
//
// Phase 12 (the running window manager), phases 13-14 (XRandR displays and the DPI policy).

#include "Harness.hpp"

#include "CNA/Platform/PlatformException.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <thread>
#include <variant>

namespace CnaX11Validation {

    namespace {

        std::string Bounds(const WindowBounds& bounds)
        {
            std::ostringstream out;
            out << bounds.width << "x" << bounds.height << "+" << bounds.x << "+" << bounds.y;
            return out.str();
        }

        std::string WindowManagerName(Driver& driver)
        {
            ::Display* display = driver.GetDisplay();
            const Atom check = XInternAtom(display, "_NET_SUPPORTING_WM_CHECK", CNA::Platform::X11::kXFalse);
            const Atom name = XInternAtom(display, "_NET_WM_NAME", CNA::Platform::X11::kXFalse);
            const Atom utf8 = XInternAtom(display, "UTF8_STRING", CNA::Platform::X11::kXFalse);
            Atom type = CNA::Platform::X11::kNone;
            int format = 0;
            unsigned long count = 0;
            unsigned long remaining = 0;
            unsigned char* data = nullptr;
            ::Window child = CNA::Platform::X11::kNone;
            if (XGetWindowProperty(display, DefaultRootWindow(display), check, 0, 1,
                                   CNA::Platform::X11::kXFalse, XA_WINDOW, &type, &format, &count,
                                   &remaining, &data) == Success && data != nullptr && count == 1)
            {
                child = static_cast<::Window>(*reinterpret_cast<unsigned long*>(data));
            }
            if (data != nullptr) { XFree(data); }
            if (child == CNA::Platform::X11::kNone) { return "(none)"; }
            data = nullptr;
            std::string result = "(unnamed)";
            if (XGetWindowProperty(display, child, name, 0, 256, CNA::Platform::X11::kXFalse, utf8,
                                   &type, &format, &count, &remaining, &data) == Success &&
                data != nullptr)
            {
                result.assign(reinterpret_cast<const char*>(data), count);
            }
            if (data != nullptr) { XFree(data); }
            return result;
        }

    } // namespace

    int RunWindowManager(const std::vector<std::string>& arguments)
    {
        const int toggles = static_cast<int>(OptionInt(arguments, "toggles", 100));
        Driver driver;
        Session session;
        if (!session.Ok() || !driver.Ok())
        {
            Fail("wm.session", session.Ok() ? "driver connection failed" : session.Error());
            return 1;
        }
        IPlatform& platform = session.Platform();
        Info("window manager: " + WindowManagerName(driver));
        if (!platform.GetCapabilities().borderlessFullscreen)
        {
            Skip("wm", "the running window manager does not advertise _NET_WM_STATE_FULLSCREEN");
            return 0;
        }

        auto window = session.Make("CNA window-manager validation", 640, 420, true,
                                   WindowRenderIntent::None, 150, 120);
        const WindowId id = window->GetId();
        session.WaitFor(id, WindowEventKind::Exposed);
        session.Focus(*window, driver);
        session.PumpFor(std::chrono::milliseconds(300));
        const WindowBounds original = window->GetClientBounds();
        Info("windowed bounds: " + Bounds(original));

        // --- borderless fullscreen, round trip after round trip ------------------------------------
        int fullscreenFailures = 0;
        int restoreFailures = 0;
        double worstEnterMs = 0.0;
        double worstLeaveMs = 0.0;
        WindowBounds fullscreenBounds{};
        for (int round = 0; round < toggles; ++round)
        {
            double started = NowMs();
            window->SetFullscreenMode(WindowFullscreenMode::BorderlessFullscreen);
            const bool entered = session.PumpUntil(
                [&window] {
                    return window->GetFullscreenMode() == WindowFullscreenMode::BorderlessFullscreen;
                },
                std::chrono::milliseconds(3000));
            worstEnterMs = std::max(worstEnterMs, NowMs() - started);
            if (!entered)
            {
                ++fullscreenFailures;
                continue;
            }
            // The window manager resizes the window after setting the state; wait for the size.
            session.PumpUntil(
                [&window, original] {
                    const WindowBounds now = window->GetClientBounds();
                    return now.width > original.width && now.height > original.height;
                },
                std::chrono::milliseconds(2000));
            fullscreenBounds = window->GetClientBounds();

            started = NowMs();
            window->SetFullscreenMode(WindowFullscreenMode::Windowed);
            const bool left = session.PumpUntil(
                [&window, original] {
                    const WindowBounds now = window->GetClientBounds();
                    return window->GetFullscreenMode() == WindowFullscreenMode::Windowed &&
                           now.width == original.width && now.height == original.height &&
                           now.x == original.x && now.y == original.y;
                },
                std::chrono::milliseconds(3000));
            worstLeaveMs = std::max(worstLeaveMs, NowMs() - started);
            if (!left)
            {
                ++restoreFailures;
                Info("round " + std::to_string(round) + ": back in windowed mode at " +
                     Bounds(window->GetClientBounds()) + ", originally " + Bounds(original));
            }
        }
        Info("fullscreen bounds: " + Bounds(fullscreenBounds) + "; worst enter " +
             std::to_string(static_cast<int>(worstEnterMs)) + " ms, worst leave " +
             std::to_string(static_cast<int>(worstLeaveMs)) + " ms");
        Check(fullscreenFailures == 0, "wm.fullscreen-enters",
              std::to_string(toggles) + " round trips, " + std::to_string(fullscreenFailures) +
                  " did not enter");
        Check(restoreFailures == 0, "wm.fullscreen-restores-exact-geometry",
              std::to_string(restoreFailures) + " of " + std::to_string(toggles) +
                  " came back to a different size or position");

        // --- exclusive fullscreen refuses ------------------------------------------------------------
        try
        {
            window->SetFullscreenMode(WindowFullscreenMode::ExclusiveFullscreen);
            Fail("wm.exclusive-fullscreen-refuses", "it was accepted");
        }
        catch (const PlatformNotSupportedException&)
        {
            Pass("wm.exclusive-fullscreen-refuses");
        }
        Check(window->GetFullscreenMode() == WindowFullscreenMode::Windowed,
              "wm.refusal-leaves-windowed");

        // --- maximise / minimise / restore / activation ---------------------------------------------
        session.Clear();
        window->Maximize();
        const bool maximized = session.PumpUntil(
            [&session, id] { return session.Saw(id, WindowEventKind::Maximized); },
            std::chrono::milliseconds(3000));
        Check(maximized, "wm.maximize-event", Bounds(window->GetClientBounds()));
        window->Restore();
        const bool unmaximized = session.PumpUntil(
            [&window, original] {
                const WindowBounds now = window->GetClientBounds();
                return now.width == original.width && now.height == original.height;
            },
            std::chrono::milliseconds(3000));
        Check(unmaximized, "wm.unmaximize-restores-size", Bounds(window->GetClientBounds()));

        session.Clear();
        window->Minimize();
        const bool minimized = session.PumpUntil([&window] { return window->IsMinimized(); },
                                                 std::chrono::milliseconds(3000));
        Check(minimized && session.Saw(id, WindowEventKind::Minimized), "wm.minimize");
        Check(!window->HasFocus(), "wm.minimized-window-has-no-focus");
        window->Restore();
        const bool back = session.PumpUntil([&window] { return !window->IsMinimized(); },
                                            std::chrono::milliseconds(3000));
        Check(back && session.WaitFor(id, WindowEventKind::Restored, std::chrono::milliseconds(2000)),
              "wm.restore-from-minimize");

        const ::Window other = driver.CreatePlainWindow("CNA validation: other", 900, 300, 300, 200);
        session.PumpFor(std::chrono::milliseconds(300));
        driver.Activate(other);
        const bool lost = session.PumpUntil([&window] { return !window->HasFocus(); },
                                            std::chrono::milliseconds(2000));
        Check(lost && session.Saw(id, WindowEventKind::FocusLost), "wm.focus-lost-to-another-client");
        session.Clear();
        const bool regained = session.Focus(*window, driver);
        Check(regained && session.Saw(id, WindowEventKind::FocusGained), "wm.activation-returns-focus");
        driver.DestroyWindow(other);

        // --- move and resize through the window manager -------------------------------------------------
        session.Clear();
        driver.Move(static_cast<::Window>(window->GetWindowHandle()), 300, 200);
        const bool moved = session.PumpUntil(
            [&window] {
                const WindowBounds now = window->GetClientBounds();
                return std::abs(now.x - 300) <= 2 && std::abs(now.y - 200) <= 2;
            },
            std::chrono::milliseconds(2000));
        // The geometry query can see the new position before the window manager's (synthetic)
        // ConfigureNotify has been read, so the event gets its own wait rather than having to
        // have arrived by the moment the position matched.
        const bool movedEvent = session.WaitFor(id, WindowEventKind::Moved,
                                                std::chrono::milliseconds(1500));
        Check(moved && movedEvent, "wm.move",
              Bounds(window->GetClientBounds()) +
                  (moved ? "" : " (position not reached)") +
                  (movedEvent ? "" : " (no Moved event)"));
        window->SetSize(800, 500);
        window->Sync();
        Check(window->GetClientBounds().width == 800 && window->GetClientBounds().height == 500,
              "wm.resize-after-sync", Bounds(window->GetClientBounds()));
        return Results().failed == 0 ? 0 : 1;
    }

    int RunDisplays(const std::vector<std::string>& arguments)
    {
        (void) arguments;
        Driver driver;
        Session session;
        if (!session.Ok() || !driver.Ok())
        {
            Fail("displays.session", session.Ok() ? "driver connection failed" : session.Error());
            return 1;
        }
        IPlatform& platform = session.Platform();
        IPlatformDisplays* displays = platform.GetDisplays();
        if (displays == nullptr)
        {
            Skip("displays", "no XRandR 1.2+, so no display enumeration");
            return 0;
        }
        const std::vector<DisplayInfo> cna = displays->GetDisplays();

        // The server's own answer, read independently.
        ::Display* display = driver.GetDisplay();
        int count = 0;
        XRRMonitorInfo* monitors = XRRGetMonitors(display, DefaultRootWindow(display), True, &count);
        struct Monitor
        {
            std::string name;
            int x, y, width, height, widthMm, heightMm;
            bool primary;
        };
        std::vector<Monitor> server;
        for (int index = 0; index < count; ++index)
        {
            char* name = XGetAtomName(display, monitors[index].name);
            server.push_back({name != nullptr ? name : "?", monitors[index].x, monitors[index].y,
                              monitors[index].width, monitors[index].height,
                              monitors[index].mwidth, monitors[index].mheight,
                              monitors[index].primary != 0});
            if (name != nullptr) { XFree(name); }
        }
        if (monitors != nullptr) { XRRFreeMonitors(monitors); }

        for (const Monitor& monitor : server)
        {
            Info("XRandR monitor " + monitor.name + ": " + std::to_string(monitor.width) + "x" +
                 std::to_string(monitor.height) + "+" + std::to_string(monitor.x) + "+" +
                 std::to_string(monitor.y) + ", " + std::to_string(monitor.widthMm) + "x" +
                 std::to_string(monitor.heightMm) + " mm" + (monitor.primary ? ", primary" : ""));
        }
        for (const DisplayInfo& info : cna)
        {
            Info("CNA display " + std::to_string(info.id) + " \"" + info.name + "\": " +
                 std::to_string(info.width) + "x" + std::to_string(info.height) + "+" +
                 std::to_string(info.x) + "+" + std::to_string(info.y) + ", scale " +
                 std::to_string(info.contentScale) + ", refresh " +
                 std::to_string(info.desktopMode.refreshRate) + " Hz");
        }
        Check(cna.size() == server.size(), "displays.count-matches-xrandr",
              std::to_string(cna.size()) + " vs " + std::to_string(server.size()));
        int matched = 0;
        for (const Monitor& monitor : server)
        {
            for (const DisplayInfo& info : cna)
            {
                if (info.name == monitor.name && info.x == monitor.x && info.y == monitor.y &&
                    info.width == monitor.width && info.height == monitor.height)
                {
                    ++matched;
                }
            }
        }
        Check(matched == static_cast<int>(server.size()), "displays.names-and-bounds-match",
              std::to_string(matched) + " of " + std::to_string(server.size()));
        const auto primary = std::find_if(server.begin(), server.end(),
                                          [](const Monitor& monitor) { return monitor.primary; });
        if (primary != server.end() && !cna.empty())
        {
            Check(cna.front().name == primary->name, "displays.primary-is-first",
                  "first CNA display " + cna.front().name + ", primary " + primary->name);
        }

        // --- window-to-display association, and DisplayChanged, across real monitors ----------------
        auto window = session.Make("CNA display association", 500, 350, true,
                                   WindowRenderIntent::None, 200, 200);
        const WindowId id = window->GetId();
        session.WaitFor(id, WindowEventKind::Exposed);
        for (const Monitor& monitor : server)
        {
            session.Clear();
            const int targetX = monitor.x + 150;
            const int targetY = monitor.y + 150;
            driver.Move(static_cast<::Window>(window->GetWindowHandle()), targetX, targetY);
            session.PumpUntil(
                [&window, targetX, targetY] {
                    const WindowBounds now = window->GetClientBounds();
                    return std::abs(now.x - targetX) <= 2 && std::abs(now.y - targetY) <= 2;
                },
                std::chrono::milliseconds(2000));
            session.PumpFor(std::chrono::milliseconds(200));
            DisplayInfo info{};
            const bool found = displays->TryGetDisplayForWindow(*window, info);
            Check(found && info.name == monitor.name, "displays.window-on-" + monitor.name,
                  found ? "reported " + info.name + " at " + Bounds(window->GetClientBounds())
                        : std::string("no display"));
            if (server.size() > 1 && &monitor != &server.front())
            {
                if (session.Saw(id, WindowEventKind::DisplayChanged))
                {
                    Pass("displays.display-changed-event-on-move-to-" + monitor.name);
                }
                else
                {
                    Fail("displays.display-changed-event-on-move-to-" + monitor.name,
                         "the window moved to another monitor and no DisplayChanged was emitted "
                         "(the contract: \"the window moved to a different display\")");
                }
            }
        }
        if (server.size() < 2)
        {
            Skip("displays.multi-monitor", "only one monitor is connected");
        }

        // --- DPI policy --------------------------------------------------------------------------------
        char* resources = XResourceManagerString(display);
        std::string xftDpi = "(unset)";
        if (resources != nullptr)
        {
            const std::string text(resources);
            const auto at = text.find("Xft.dpi:");
            if (at != std::string::npos)
            {
                xftDpi = text.substr(at + 8, text.find('\n', at) - at - 8);
                xftDpi.erase(0, xftDpi.find_first_not_of(" \t"));
            }
        }
        const float scale = window->GetDisplayScale();
        Info("Xft.dpi " + xftDpi + "; CNA window display scale " + std::to_string(scale) +
             "; highDpi capability " + (platform.GetCapabilities().highDpi ? "true" : "false"));
        const double expected = xftDpi == "(unset)" ? 1.0 : std::atof(xftDpi.c_str()) / 96.0;
        Check(std::fabs(scale - expected) < 1e-3, "displays.scale-follows-xft-dpi",
              "scale " + std::to_string(scale) + ", Xft.dpi/96 = " + std::to_string(expected));
        Check(platform.GetCapabilities().highDpi == (scale != 1.0f),
              "displays.highdpi-capability-consistent");
        const WindowSize pixels = window->GetPixelSize();
        const WindowBounds bounds = window->GetClientBounds();
        Check(pixels.width == bounds.width && pixels.height == bounds.height,
              "displays.pixel-size-equals-client-size");
        for (const DisplayInfo& info : cna)
        {
            Check(std::fabs(info.contentScale - scale) < 1e-3,
                  "displays.content-scale-consistent-" + info.name,
                  std::to_string(info.contentScale));
        }
        for (const Monitor& monitor : server)
        {
            if (monitor.widthMm > 0)
            {
                Info(monitor.name + ": XRandR geometry implies " +
                     std::to_string(static_cast<int>(monitor.width * 25.4 / monitor.widthMm)) +
                     " logical DPI -- deliberately NOT used by CNA's policy (plan_x11 D10)");
            }
        }
        return Results().failed == 0 ? 0 : 1;
    }

} // namespace CnaX11Validation
