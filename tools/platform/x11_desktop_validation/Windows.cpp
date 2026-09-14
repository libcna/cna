// SPDX-License-Identifier: MS-PL
//
// Phase 5 (window lifecycle) and phase 6 (long-running window stress) against the running
// desktop and its window manager.

#include "Harness.hpp"

#include "CNA/Platform/PlatformException.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <set>
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

        std::string ResourceSummary(const std::map<std::string, long>& byType)
        {
            std::ostringstream out;
            for (const auto& [type, count] : byType)
            {
                out << type << "=" << count << " ";
            }
            return out.str();
        }

        /// Everything a lifecycle round does to one window, each step checked.
        void ExerciseWindow(Session& session, Driver& driver, IPlatformWindow& window,
                            const std::string& label, int& failures)
        {
            const WindowId id = window.GetId();
            const auto check = [&failures, &label](const bool ok, const std::string& what,
                                                   const std::string& detail = {}) {
                if (!ok)
                {
                    ++failures;
                    Fail(label + "." + what, detail);
                }
            };

            // hide / show
            session.Clear();
            window.Hide();
            session.PumpFor(std::chrono::milliseconds(150));
            check(!window.IsMinimized(), "hide-is-not-minimize");
            window.Show();
            check(session.WaitFor(id, WindowEventKind::Restored), "show-reports-restored");
            session.WaitFor(id, WindowEventKind::Exposed, std::chrono::milliseconds(1500));

            // move (from outside: the contract has no move call; a user or the WM moves windows)
            const WindowBounds before = window.GetClientBounds();
            const int targetX = before.x + 37;
            const int targetY = before.y + 23;
            session.Clear();
            driver.Move(static_cast<::Window>(window.GetWindowHandle()), targetX, targetY);
            const bool moved = session.PumpUntil(
                [&window, targetX, targetY] {
                    const WindowBounds now = window.GetClientBounds();
                    return std::abs(now.x - targetX) <= 2 && std::abs(now.y - targetY) <= 2;
                },
                std::chrono::milliseconds(2000));
            check(moved, "move-lands", Bounds(window.GetClientBounds()) + " wanted +" +
                                           std::to_string(targetX) + "+" + std::to_string(targetY));
            // The position can be read back (XTranslateCoordinates) before the window manager's
            // synthetic ConfigureNotify -- the only one that reports a move -- has arrived.
            check(session.WaitFor(id, WindowEventKind::Moved, std::chrono::milliseconds(1500)),
                  "move-reports-moved");

            // resize
            session.Clear();
            window.SetSize(before.width + 40, before.height + 30);
            window.Sync();
            const bool resized = session.PumpUntil(
                [&session, id] {
                    return session.Saw(id, WindowEventKind::Resized) &&
                           session.Saw(id, WindowEventKind::PixelSizeChanged);
                },
                std::chrono::milliseconds(2000));
            const WindowBounds grown = window.GetClientBounds();
            check(resized, "resize-reports-resized-and-pixel-size");
            check(grown.width == before.width + 40 && grown.height == before.height + 30,
                  "resize-lands", Bounds(grown));
            check(window.GetPixelSize().width == grown.width &&
                      window.GetPixelSize().height == grown.height,
                  "pixel-size-equals-client-size");

            // minimize / restore
            session.Clear();
            window.Minimize();
            const bool minimized =
                session.PumpUntil([&window] { return window.IsMinimized(); },
                                  std::chrono::milliseconds(3000));
            check(minimized, "minimize-lands");
            check(session.Saw(id, WindowEventKind::Minimized), "minimize-reports-minimized");
            session.Clear();
            window.Restore();
            const bool unminimized =
                session.PumpUntil([&window] { return !window.IsMinimized(); },
                                  std::chrono::milliseconds(3000));
            check(unminimized, "restore-from-minimize-lands");
            check(session.WaitFor(id, WindowEventKind::Restored, std::chrono::milliseconds(2000)),
                  "restore-from-minimize-reports-restored");

            // maximize / restore
            const WindowBounds normal = window.GetClientBounds();
            session.Clear();
            window.Maximize();
            const bool maximized = session.PumpUntil(
                [&session, id] { return session.Saw(id, WindowEventKind::Maximized); },
                std::chrono::milliseconds(3000));
            check(maximized, "maximize-reports-maximized");
            const WindowBounds big = window.GetClientBounds();
            check(big.width > normal.width && big.height > normal.height, "maximize-grows",
                  Bounds(big));
            session.Clear();
            window.Restore();
            const bool demaximized = session.PumpUntil(
                [&window, normal] {
                    const WindowBounds now = window.GetClientBounds();
                    return now.width == normal.width && now.height == normal.height;
                },
                std::chrono::milliseconds(3000));
            check(demaximized, "restore-from-maximize-size", Bounds(window.GetClientBounds()) +
                                                                  " wanted " + Bounds(normal));
            check(session.Saw(id, WindowEventKind::Restored),
                  "restore-from-maximize-reports-restored");

            // focus
            session.Clear();
            const bool focused = session.Focus(window, driver);
            check(focused, "focus-lands");
            check(driver.FocusIsWithin(static_cast<::Window>(window.GetWindowHandle())),
                  "server-agrees-on-focus");
        }

    } // namespace

    int RunLifecycle(const std::vector<std::string>& arguments)
    {
        const int rounds = static_cast<int>(OptionInt(arguments, "rounds", 3));
        const int rapid = static_cast<int>(OptionInt(arguments, "rapid", 300));
        Driver driver;
        Session session;
        if (!session.Ok() || !driver.Ok())
        {
            Fail("lifecycle.session", session.Ok() ? "driver connection failed" : session.Error());
            return 1;
        }
        IPlatform& platform = session.Platform();
        Check(platform.GetCapabilities().multipleWindows, "lifecycle.multiple-windows-capability");

        std::set<WindowId> everSeen;
        long baselineResources = -1;
        std::map<std::string, long> baselineByType;
        ProcessSample baselineProcess{};

        for (const int count : {1, 2, 4})
        {
            int failures = 0;
            for (int round = 0; round < rounds; ++round)
            {
                std::vector<std::unique_ptr<IPlatformWindow>> windows;
                for (int index = 0; index < count; ++index)
                {
                    windows.push_back(session.Make(
                        "CNA lifecycle " + std::to_string(count) + "/" + std::to_string(index), 360,
                        260, true, WindowRenderIntent::None, 80 + index * 420, 90 + index * 40));
                    const WindowId id = windows.back()->GetId();
                    if (!everSeen.insert(id).second)
                    {
                        ++failures;
                        Fail("lifecycle.window-ids-unique", "id " + std::to_string(id) + " reused");
                    }
                }
                for (auto& window : windows)
                {
                    if (!session.WaitFor(window->GetId(), WindowEventKind::Exposed))
                    {
                        ++failures;
                        Fail("lifecycle.window-becomes-viewable");
                    }
                }
                if (baselineResources < 0 && count == 1 && round == 1)
                {
                    baselineResources = driver.ClientResources(windows.front()->GetWindowHandle(),
                                                               &baselineByType);
                    baselineProcess = SampleProcess();
                }
                for (auto& window : windows)
                {
                    ExerciseWindow(session, driver, *window,
                                   "lifecycle." + std::to_string(count) + "w", failures);
                }

                // Close requests through the window manager, the way its close button does: each
                // secondary window first, then the last one.
                while (!windows.empty())
                {
                    IPlatformWindow& closing = *windows.back();
                    const WindowId id = closing.GetId();
                    const bool last = windows.size() == 1;
                    session.Clear();
                    driver.CloseThroughWindowManager(
                        static_cast<::Window>(closing.GetWindowHandle()));
                    const bool requested = session.WaitFor(id, WindowEventKind::CloseRequested);
                    if (!requested)
                    {
                        ++failures;
                        Fail("lifecycle.close-request-arrives", "window " + std::to_string(id));
                    }
                    session.PumpFor(std::chrono::milliseconds(50));
                    if (session.SawQuit() != last)
                    {
                        ++failures;
                        Fail("lifecycle.quit-only-for-last-window",
                             std::string(last ? "no QuitEvent for the last window"
                                              : "QuitEvent while other windows were open"));
                    }
                    windows.pop_back();  // the application answers the request by destroying it
                    session.Clear();
                    session.PumpFor(std::chrono::milliseconds(80));
                    for (const PlatformEvent& event : session.Seen())
                    {
                        if (EventWindow(event) == id)
                        {
                            ++failures;
                            Fail("lifecycle.no-events-after-destroy", "window " + std::to_string(id));
                            break;
                        }
                    }
                }
            }
            Check(failures == 0, "lifecycle." + std::to_string(count) + "-windows",
                  std::to_string(rounds) + " rounds, " + std::to_string(failures) + " failed steps");
        }

        // Rapid create/destroy, with events still arriving for windows that are already gone.
        {
            const ProcessSample before = SampleProcess();
            const int errorsBefore = driver.ErrorCount();
            for (int index = 0; index < rapid; ++index)
            {
                auto window = session.Make("CNA rapid " + std::to_string(index), 200, 150, true,
                                           WindowRenderIntent::None, 100 + (index % 7) * 60, 120);
                if (index % 3 == 0)
                {
                    session.Poll();
                }
                window.reset();
                if (index % 5 == 0)
                {
                    session.Poll();
                }
            }
            session.PumpFor(std::chrono::milliseconds(500));
            const ProcessSample after = SampleProcess();
            Check(true, "lifecycle.rapid-create-destroy",
                  std::to_string(rapid) + " windows; RSS " + std::to_string(before.residentKb) +
                      " -> " + std::to_string(after.residentKb) + " kB, fds " +
                      std::to_string(before.openDescriptors) + " -> " +
                      std::to_string(after.openDescriptors));
            Check(after.openDescriptors <= before.openDescriptors,
                  "lifecycle.rapid-no-descriptor-growth");
            Check(driver.ErrorCount() == errorsBefore, "lifecycle.driver-no-x-errors");
        }

        // Server-side resources back where they were.
        if (baselineResources >= 0)
        {
            auto probe = session.Make("CNA lifecycle resource probe", 120, 90);
            session.WaitFor(probe->GetId(), WindowEventKind::Exposed);
            std::map<std::string, long> byType;
            const long now = driver.ClientResources(probe->GetWindowHandle(), &byType);
            Info("X resources of CNA's client, baseline (1 window): " +
                 ResourceSummary(baselineByType));
            Info("X resources of CNA's client, after everything (1 window): " +
                 ResourceSummary(byType));
            Check(now >= 0 && now <= baselineResources, "lifecycle.no-x-resource-leak",
                  std::to_string(baselineResources) + " -> " + std::to_string(now));
            const ProcessSample end = SampleProcess();
            Info("process: RSS " + std::to_string(baselineProcess.residentKb) + " -> " +
                 std::to_string(end.residentKb) + " kB, fds " +
                 std::to_string(baselineProcess.openDescriptors) + " -> " +
                 std::to_string(end.openDescriptors));
        }
        else
        {
            Skip("lifecycle.no-x-resource-leak", driver.ResourceProblem());
        }
        return Results().failed == 0 ? 0 : 1;
    }

    int RunStress(const std::vector<std::string>& arguments)
    {
        const long operations = OptionInt(arguments, "ops", 10000);
        const long fullscreenEvery = OptionInt(arguments, "fullscreen-every", 250);
        const bool burst = OptionFlag(arguments, "burst");
        Driver driver;
        Session session;
        if (!session.Ok() || !driver.Ok())
        {
            Fail("stress.session", session.Ok() ? "driver connection failed" : session.Error());
            return 1;
        }
        IPlatform& platform = session.Platform();
        const bool fullscreen = platform.GetCapabilities().borderlessFullscreen;

        std::mt19937 random(20260914u);
        std::vector<std::unique_ptr<IPlatformWindow>> windows;
        // An anchor window that lives through the whole run, so X-Resource can always be asked
        // about CNA's client.
        auto anchor = session.Make("CNA stress anchor", 160, 120, true, WindowRenderIntent::None,
                                   40, 700);
        session.WaitFor(anchor->GetId(), WindowEventKind::Exposed);

        struct Sample
        {
            long operation;
            ProcessSample process;
            long xResources;
        };
        std::vector<Sample> samples;
        std::map<std::string, long> counts;
        const double started = NowMs();
        const int errorsBefore = driver.ErrorCount();

        for (long operation = 1; operation <= operations; ++operation)
        {
            const int kind = static_cast<int>(random() % 100);
            if (windows.empty() || (kind < 12 && windows.size() < 4))
            {
                windows.push_back(session.Make("CNA stress", 200 + static_cast<int>(random() % 200),
                                               150 + static_cast<int>(random() % 150), true,
                                               WindowRenderIntent::None,
                                               100 + static_cast<int>(random() % 1500),
                                               100 + static_cast<int>(random() % 600)));
                ++counts["create"];
            }
            else
            {
                IPlatformWindow& window = *windows[random() % windows.size()];
                if (kind < 22)
                {
                    windows.erase(windows.begin() +
                                  static_cast<long>(random() % windows.size()));
                    ++counts["destroy"];
                }
                else if (kind < 42)
                {
                    window.SetSize(150 + static_cast<int>(random() % 500),
                                   120 + static_cast<int>(random() % 400));
                    ++counts["resize"];
                }
                else if (kind < 58)
                {
                    driver.Move(static_cast<::Window>(window.GetWindowHandle()),
                                60 + static_cast<int>(random() % 3500),
                                60 + static_cast<int>(random() % 800));
                    ++counts["move"];
                }
                else if (kind < 70)
                {
                    driver.Activate(static_cast<::Window>(window.GetWindowHandle()));
                    ++counts["focus"];
                }
                else if (kind < 80)
                {
                    window.Hide();
                    ++counts["hide"];
                }
                else if (kind < 90)
                {
                    window.Show();
                    ++counts["show"];
                }
                else if (kind < 95)
                {
                    window.SetTitle("CNA stress " + std::to_string(operation));
                    ++counts["title"];
                }
                else if (fullscreen && fullscreenEvery > 0 && operation % fullscreenEvery == 0)
                {
                    const bool on = window.GetFullscreenMode() == WindowFullscreenMode::Windowed;
                    window.SetFullscreenMode(on ? WindowFullscreenMode::BorderlessFullscreen
                                                : WindowFullscreenMode::Windowed);
                    ++counts["fullscreen"];
                }
                else
                {
                    window.Restore();
                    ++counts["restore"];
                }
            }
            if (!burst || operation % 50 == 0)
            {
                session.Poll();
                session.Clear();
            }
            if (!burst && operation % 10 == 0)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            if (operation % 1000 == 0)
            {
                session.PumpFor(std::chrono::milliseconds(200));
                session.Clear();
                samples.push_back({operation, SampleProcess(),
                                   driver.ClientResources(anchor->GetWindowHandle(), nullptr)});
                const Sample& sample = samples.back();
                Info("stress @" + std::to_string(operation) + ": windows " +
                     std::to_string(windows.size()) + ", RSS " +
                     std::to_string(sample.process.residentKb) + " kB, fds " +
                     std::to_string(sample.process.openDescriptors) + ", X resources " +
                     std::to_string(sample.xResources));
            }
        }
        windows.clear();
        session.PumpFor(std::chrono::milliseconds(500));
        const double seconds = (NowMs() - started) / 1000.0;
        std::ostringstream mix;
        for (const auto& [name, count] : counts)
        {
            mix << name << "=" << count << " ";
        }
        Info("operation mix: " + mix.str());
        Pass("stress.completed", std::to_string(operations) + " operations in " +
                                     std::to_string(seconds) + " s (" +
                                     (burst ? "burst" : "paced") + ")");

        if (samples.size() >= 3)
        {
            // Growth is judged between the second sample (after warm-up: fonts, cursors, the GLX
            // and XIM one-time state) and the last. A leak of even a few bytes per operation over
            // thousands of operations is well above the noise of heap fragmentation.
            const Sample& early = samples[1];
            const Sample& late = samples.back();
            const long rssGrowth = late.process.residentKb - early.process.residentKb;
            Check(rssGrowth < 4096, "stress.rss-stable",
                  std::to_string(early.process.residentKb) + " -> " +
                      std::to_string(late.process.residentKb) + " kB over " +
                      std::to_string(late.operation - early.operation) + " operations");
            Check(late.process.openDescriptors <= early.process.openDescriptors,
                  "stress.descriptors-stable",
                  std::to_string(early.process.openDescriptors) + " -> " +
                      std::to_string(late.process.openDescriptors));
            if (early.xResources >= 0)
            {
                std::map<std::string, long> byType;
                const long final = driver.ClientResources(anchor->GetWindowHandle(), &byType);
                Info("X resources after cleanup: " + ResourceSummary(byType));
                Check(final <= early.xResources, "stress.x-resources-stable",
                      std::to_string(early.xResources) + " @" + std::to_string(early.operation) +
                          " -> " + std::to_string(final) + " after cleanup");
            }
        }
        Check(driver.ErrorCount() == errorsBefore, "stress.driver-no-x-errors");
        return Results().failed == 0 ? 0 : 1;
    }

} // namespace CnaX11Validation
