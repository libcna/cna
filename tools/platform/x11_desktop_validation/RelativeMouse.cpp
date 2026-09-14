// SPDX-License-Identifier: MS-PL
//
// Phase 10: XInput2 raw relative mouse on the real desktop.
//
// Motion comes from two sources, reported separately because they prove different things:
//   * XTest (`XTestFakeRelativeMotionEvent` on the driver's connection) -- the X server's own
//     input queue; available on any X server.
//   * a uinput mouse -- the whole kernel -> libinput -> compositor -> Xwayland path, which is
//     where a physical mouse's motion becomes an XI2 raw event on a Wayland desktop.
// Neither is a hand on a mouse, and the log says which one produced every number.

#include "Harness.hpp"
#include "UInput.hpp"

#include "CNA/Platform/PlatformException.hpp"

#if defined(__linux__)
#  include <linux/input-event-codes.h>
#endif

#include <csignal>
#include <cstdlib>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace CnaX11Validation {

    namespace {

        using CNA::Platform::X11::kXFalse;

        /// Pumps and drains the relative delta for a while, returning the total it reported.
        MouseDelta DrainDelta(Session& session, IPlatformMouse& mouse,
                              const std::chrono::milliseconds duration)
        {
            MouseDelta total{0, 0};
            const auto deadline = std::chrono::steady_clock::now() + duration;
            while (std::chrono::steady_clock::now() < deadline)
            {
                session.Poll();
                mouse.Update();
                const MouseDelta delta = mouse.ConsumeRelativeDelta();
                total.x += delta.x;
                total.y += delta.y;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            return total;
        }

        std::string Describe(const MouseDelta delta)
        {
            return "(" + Signed(delta.x) + ", " + Signed(delta.y) + ")";
        }

        bool EnableRelative(IPlatformMouse& mouse, const WindowId window, std::string& error)
        {
            try
            {
                mouse.SetRelativeMode(window, true);
                return true;
            }
            catch (const std::exception& exception)
            {
                error = exception.what();
                return false;
            }
        }

    } // namespace

    int RunRelative(const std::vector<std::string>& arguments)
    {
        const int cycles = static_cast<int>(OptionInt(arguments, "cycles", 200));
        Driver driver;
        Session session;
        if (!session.Ok() || !driver.Ok())
        {
            Fail("relative.session", session.Ok() ? "driver connection failed" : session.Error());
            return 1;
        }
        IPlatform& platform = session.Platform();
        IPlatformMouse* mouse = platform.GetMouse();
        if (!platform.GetCapabilities().relativeMouse || mouse == nullptr)
        {
            Skip("relative", "the backend reports no relativeMouse capability (no XInput2)");
            return 0;
        }
        {
            int opcode = 0;
            int event = 0;
            int error = 0;
            int major = 2;
            int minor = 4;
            if (XQueryExtension(driver.GetDisplay(), "XInputExtension", &opcode, &event, &error))
            {
                XIQueryVersion(driver.GetDisplay(), &major, &minor);
                Info("server XInput version " + std::to_string(major) + "." + std::to_string(minor) +
                     " (as negotiated by a client asking for 2.4)");
            }
        }

        // --- 1. the very first thing a game does: show the window and lock the pointer ---------
        {
            auto early = session.Make("CNA relative: enable immediately after Show", 640, 480);
            std::string error;
            const bool enabled = EnableRelative(*mouse, early->GetId(), error);
            Check(enabled, "relative.enable-right-after-show",
                  enabled ? "accepted" : "refused: " + error);
            if (enabled)
            {
                mouse->SetRelativeMode(early->GetId(), false);
            }
            session.PumpFor(std::chrono::milliseconds(200));
        }

        // --- 2. a focused, viewable window ------------------------------------------------------
        auto window = session.Make("CNA relative mouse", 800, 600);
        const WindowId id = window->GetId();
        if (!session.WaitFor(id, WindowEventKind::Exposed))
        {
            Fail("relative.window-viewable", "the window never received its first Expose");
            return 1;
        }
        if (!Check(session.Focus(*window, driver), "relative.window-focused",
                   "activated through _NET_ACTIVE_WINDOW like a pager"))
        {
            return 1;
        }

        std::string error;
        if (!Check(EnableRelative(*mouse, id, error), "relative.enable-on-focused-window", error))
        {
            return 1;
        }
        Check(mouse->IsRelativeMode(), "relative.reports-active");
        Check(driver.PointerIsGrabbedElsewhere(), "relative.pointer-grab-held",
              "another client's XGrabPointer is refused with AlreadyGrabbed");
        (void) DrainDelta(session, *mouse, std::chrono::milliseconds(150));

        // --- 3. motion from XTest --------------------------------------------------------------
#if defined(CNA_X11_VALIDATION_HAVE_XTEST)
        {
            for (int step = 0; step < 20; ++step)
            {
                XTestFakeRelativeMotionEvent(driver.GetDisplay(), 10, 0, 0);
            }
            driver.Sync();
            const MouseDelta delta = DrainDelta(session, *mouse, std::chrono::milliseconds(300));
            Info("XTest: injected (+200, +0) in 20 reports, backend reported " + Describe(delta));
            Check(delta.x > 0, "relative.xtest-motion-arrives", Describe(delta));
        }
#else
        Skip("relative.xtest-motion-arrives", "built without libXtst");
#endif

        // --- 4. motion from a kernel-level virtual mouse ------------------------------------------
        std::string uinputError;
        std::unique_ptr<VirtualInput> device =
            UinputReachesDisplay(driver, arguments, uinputError)
                ? VirtualInput::Create(false, true, uinputError)
                : nullptr;
        if (device == nullptr)
        {
            Skip("relative.uinput", uinputError);
        }
        else
        {
            // The device's arrival can move focus nowhere, but re-assert it: every uinput event
            // goes wherever the compositor says focus is.
            session.Focus(*window, driver);
            (void) DrainDelta(session, *mouse, std::chrono::milliseconds(100));

            for (int step = 0; step < 30; ++step)
            {
                device->Move(7, -3);
                std::this_thread::sleep_for(std::chrono::milliseconds(4));
            }
            const MouseDelta delta = DrainDelta(session, *mouse, std::chrono::milliseconds(400));
            Info("uinput: injected (+210, -90) in 30 reports, backend reported " + Describe(delta));
            Check(delta.x == 210 && delta.y == -90, "relative.uinput-raw-deltas-exact",
                  "raw (unaccelerated) motion should arrive unscaled: " + Describe(delta));

            // Sub-pixel motion: one count per report. libinput normalises devices to 1000 dpi,
            // so a real high-resolution mouse produces fractional deltas; a backend that
            // truncates each report to an integer loses slow motion entirely.
            for (int step = 0; step < 50; ++step)
            {
                device->Move(1, 0);
                std::this_thread::sleep_for(std::chrono::milliseconds(3));
            }
            const MouseDelta slow = DrainDelta(session, *mouse, std::chrono::milliseconds(300));
            Check(slow.x == 50, "relative.uinput-slow-motion-kept", Describe(slow));

            // Edge clipping: far more motion than the window is wide. The confined pointer stops
            // at the edge; raw deltas must not.
            for (int step = 0; step < 100; ++step)
            {
                device->Move(60, 0);
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            const MouseDelta edge = DrainDelta(session, *mouse, std::chrono::milliseconds(400));
            Check(edge.x == 6000, "relative.no-edge-clipping",
                  "6000 px pushed into an 800 px window: " + Describe(edge));

            // Warping the pointer is not motion.
            mouse->SetPosition(id, 400, 300);
            const MouseDelta afterWarp = DrainDelta(session, *mouse, std::chrono::milliseconds(200));
            Check(afterWarp.x == 0 && afterWarp.y == 0, "relative.warp-is-not-motion",
                  Describe(afterWarp));
        }

        // --- 5. focus loss -------------------------------------------------------------------------
        {
            const ::Window other = driver.CreatePlainWindow("CNA validation: focus thief", 900, 100,
                                                            300, 200);
            session.PumpFor(std::chrono::milliseconds(300));
            driver.Activate(other);
            const bool lost = session.PumpUntil([&window] { return !window->HasFocus(); },
                                                std::chrono::milliseconds(2000));
            Check(lost, "relative.focus-moved-away", "another window was activated");
            session.PumpFor(std::chrono::milliseconds(200));
            const bool grabbed = driver.PointerIsGrabbedElsewhere();
            Check(!grabbed, "relative.focus-loss-releases-grab",
                  grabbed ? "CNA still holds the pointer grab while another window has focus"
                          : "the pointer is free for the focused application");
            if (device != nullptr)
            {
                (void) DrainDelta(session, *mouse, std::chrono::milliseconds(100));
                for (int step = 0; step < 20; ++step)
                {
                    device->Move(5, 5);
                    std::this_thread::sleep_for(std::chrono::milliseconds(3));
                }
                const MouseDelta unfocused =
                    DrainDelta(session, *mouse, std::chrono::milliseconds(300));
                Check(unfocused.x == 0 && unfocused.y == 0, "relative.no-deltas-while-unfocused",
                      "motion made for another application: " + Describe(unfocused));
            }

            driver.DestroyWindow(other);
            const bool regained = session.Focus(*window, driver);
            Check(regained, "relative.focus-returned");
            session.PumpFor(std::chrono::milliseconds(200));
            if (mouse->IsRelativeMode())
            {
                Check(driver.PointerIsGrabbedElsewhere(), "relative.grab-restored-on-focus",
                      "relative mode is still on, so the pointer should be locked again");
            }
        }

        // --- 6. repeated enable/disable ------------------------------------------------------------
        {
            int failures = 0;
            int stuck = 0;
            for (int cycle = 0; cycle < cycles; ++cycle)
            {
                std::string cycleError;
                if (!EnableRelative(*mouse, id, cycleError))
                {
                    ++failures;
                    continue;
                }
                session.Poll();
                mouse->SetRelativeMode(id, false);
                session.Poll();
                if (cycle % 20 == 0 && driver.PointerIsGrabbedElsewhere())
                {
                    ++stuck;
                }
            }
            Check(failures == 0, "relative.repeated-enable",
                  std::to_string(cycles) + " cycles, " + std::to_string(failures) + " refused");
            Check(stuck == 0 && !driver.PointerIsGrabbedElsewhere(),
                  "relative.repeated-disable-releases-grab", std::to_string(stuck) + " stuck samples");
            Check(!mouse->IsRelativeMode(), "relative.reports-inactive-after-disable");
        }

        // --- 7. minimize and close while relative ------------------------------------------------
        {
            std::string minimizeError;
            if (EnableRelative(*mouse, id, minimizeError))
            {
                window->Minimize();
                session.PumpUntil([&window] { return window->IsMinimized(); },
                                  std::chrono::milliseconds(2000));
                session.PumpFor(std::chrono::milliseconds(200));
                Check(!driver.PointerIsGrabbedElsewhere(), "relative.minimize-releases-grab");
                window->Restore();
                session.PumpFor(std::chrono::milliseconds(300));
                mouse->SetRelativeMode(id, false);
            }

            auto closing = session.Make("CNA relative: destroyed while locked", 400, 300);
            session.WaitFor(closing->GetId(), WindowEventKind::Exposed);
            session.Focus(*closing, driver);
            std::string closeError;
            if (EnableRelative(*mouse, closing->GetId(), closeError))
            {
                closing.reset();
                session.PumpFor(std::chrono::milliseconds(200));
                Check(!driver.PointerIsGrabbedElsewhere(), "relative.destroy-releases-grab");
                Check(!mouse->IsRelativeMode(), "relative.destroy-ends-relative-mode");
            }
            else
            {
                Fail("relative.destroy-releases-grab", "could not enable: " + closeError);
            }
        }

        // --- 8. abnormal exit ------------------------------------------------------------------------
        {
            const pid_t child = fork();
            if (child == 0)
            {
                Session doomed;
                if (doomed.Ok())
                {
                    Driver childDriver;
                    auto locked = doomed.Make("CNA relative: process about to crash", 400, 300);
                    doomed.WaitFor(locked->GetId(), WindowEventKind::Exposed);
                    doomed.Focus(*locked, childDriver);
                    try
                    {
                        doomed.Platform().GetMouse()->SetRelativeMode(locked->GetId(), true);
                    }
                    catch (...)
                    {
                    }
                }
                // SIGKILL rather than abort(): no handler, no unwinding, no core file -- the
                // harshest way a process can leave, and the one only the server can clean up.
                ::kill(::getpid(), SIGKILL);
                ::_exit(1);
            }
            int status = 0;
            waitpid(child, &status, 0);
            session.PumpFor(std::chrono::milliseconds(300));
            Check(!driver.PointerIsGrabbedElsewhere(), "relative.abnormal-exit-leaves-desktop-free",
                  WIFSIGNALED(status) ? "child killed by signal " + std::to_string(WTERMSIG(status))
                                      : "child exited");
        }

        Check(driver.ErrorCount() == 0, "relative.driver-saw-no-x-errors",
              std::to_string(driver.ErrorCount()) + " errors");
        return Results().failed == 0 ? 0 : 1;
    }

} // namespace CnaX11Validation
