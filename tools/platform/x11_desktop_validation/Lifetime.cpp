// SPDX-License-Identifier: MS-PL
//
// Phase 19 (repeated window + GL/Vulkan/presenter resource lifetimes), phase 31 (a long
// hardware-rendered session with input and state changes), and `info`.

#include "Harness.hpp"
#include "UInput.hpp"

#include "CNA/Platform/PlatformException.hpp"

#include "GlTypes.hpp"

#if defined(__linux__)
#  include <linux/input-event-codes.h>
#endif

#include <algorithm>
#include <cmath>
#include <random>
#include <sstream>
#include <thread>
#include <variant>

namespace CnaX11Validation {

    namespace VulkanLifetime {
        int Iteration(Session& session, bool validation, const std::string& wantedDevice,
                      std::string& error);
    }

    namespace {

        /// One GL window: context, a buffer, three cleared frames, then everything destroyed.
        bool GlIteration(Session& session, std::string& error)
        {
            IPlatformGlContext* service = session.Platform().GetGlContext();
            if (service == nullptr)
            {
                error = "no GLX";
                return false;
            }
            auto window = session.Make("CNA GL lifetime", 320, 240, true, WindowRenderIntent::OpenGl,
                                       1100, 100);
            session.WaitFor(window->GetId(), WindowEventKind::Exposed,
                            std::chrono::milliseconds(2000));
            GlContextDescription requested;
            requested.majorVersion = 3;
            requested.minorVersion = 3;
            requested.profile = GlProfile::Core;
            GlContextHandle context = service->CreateContext(window->GetId(), requested);
            service->MakeCurrent(window->GetId(), context);
            auto clearColor = reinterpret_cast<GlTypes::ClearColor>(service->GetProcAddress("glClearColor"));
            auto clear = reinterpret_cast<GlTypes::Clear>(service->GetProcAddress("glClear"));
            auto genBuffers = reinterpret_cast<PFNGLGENBUFFERSPROC>(service->GetProcAddress("glGenBuffers"));
            auto bindBuffer = reinterpret_cast<PFNGLBINDBUFFERPROC>(service->GetProcAddress("glBindBuffer"));
            auto bufferData = reinterpret_cast<PFNGLBUFFERDATAPROC>(service->GetProcAddress("glBufferData"));
            auto deleteBuffers =
                reinterpret_cast<PFNGLDELETEBUFFERSPROC>(service->GetProcAddress("glDeleteBuffers"));
            auto getError = reinterpret_cast<GlTypes::GetError>(service->GetProcAddress("glGetError"));
            GLuint buffer = 0;
            genBuffers(1, &buffer);
            bindBuffer(GL_ARRAY_BUFFER, buffer);
            std::vector<float> data(4096, 0.5f);
            bufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.size() * sizeof(float)),
                       data.data(), GL_STATIC_DRAW);
            for (int frame = 0; frame < 3; ++frame)
            {
                clearColor(0.1f * frame, 0.5f, 0.2f, 1.0f);
                clear(GL_COLOR_BUFFER_BIT);
                service->SwapBuffers(window->GetId());
                session.Poll();
            }
            const bool ok = getError() == GL_NO_ERROR;
            deleteBuffers(1, &buffer);
            service->MakeCurrent(0, nullptr);
            service->DestroyContext(context);
            window.reset();
            session.Poll();
            if (!ok) { error = "glGetError"; }
            return ok;
        }

        bool PresenterIteration(Session& session)
        {
            auto window = session.Make("CNA presenter lifetime", 320, 240, true,
                                       WindowRenderIntent::None, 1100, 450);
            session.WaitFor(window->GetId(), WindowEventKind::Exposed,
                            std::chrono::milliseconds(2000));
            auto presenter = session.Platform().CreateSurfacePresenter(*window);
            std::vector<std::uint8_t> pixels(320 * 240 * 4, 0x80);
            for (int frame = 0; frame < 3; ++frame)
            {
                presenter->Present({pixels.data(), 320, 240, 0});
                session.Poll();
            }
            presenter.reset();
            window.reset();
            session.Poll();
            return true;
        }

        void Report(const std::string& label, const ProcessSample& early, const ProcessSample& late,
                    const long earlyX, const long lateX, const int iterations)
        {
            Info(label + ": RSS " + std::to_string(early.residentKb) + " -> " +
                 std::to_string(late.residentKb) + " kB, fds " +
                 std::to_string(early.openDescriptors) + " -> " +
                 std::to_string(late.openDescriptors) + ", X resources " + std::to_string(earlyX) +
                 " -> " + std::to_string(lateX) + " over " + std::to_string(iterations) +
                 " iterations");
        }

    } // namespace

    int RunLifetime(const std::vector<std::string>& arguments)
    {
        const int iterations = static_cast<int>(OptionInt(arguments, "iterations", 300));
        const bool validation = OptionFlag(arguments, "validation");
        // Same meaning as the vulkan scenario's --device: a substring of the physical device name.
        // Without it the first GPU is used, which cannot present to an X server without DRI3.
        std::string wantedDevice;
        for (std::size_t index = 0; index + 1 < arguments.size(); ++index)
        {
            if (arguments[index] == "--device") { wantedDevice = arguments[index + 1]; }
        }
        Driver driver;
        Session session;
        if (!session.Ok() || !driver.Ok())
        {
            Fail("lifetime.session", session.Ok() ? "driver connection failed" : session.Error());
            return 1;
        }
        auto anchor = session.Make("CNA lifetime anchor", 120, 90, true, WindowRenderIntent::None,
                                   40, 700);
        session.WaitFor(anchor->GetId(), WindowEventKind::Exposed);

        struct Kind
        {
            const char* name;
            std::function<bool(std::string&)> iterate;
            int count;
        };
        const std::vector<Kind> kinds = {
            {"gl", [&session](std::string& error) { return GlIteration(session, error); }, iterations},
            {"vulkan",
             [&session, validation, &wantedDevice](std::string& error) {
                 return VulkanLifetime::Iteration(session, validation, wantedDevice, error) == 0;
             },
             std::max(1, iterations / 3)},
            {"presenter", [&session](std::string&) { return PresenterIteration(session); }, iterations},
        };
        for (const Kind& kind : kinds)
        {
            int failures = 0;
            std::string firstError;
            ProcessSample early{};
            ProcessSample late{};
            long earlyX = -1;
            long lateX = -1;
            std::map<std::string, long> earlyTypes;
            std::map<std::string, long> lateTypes;
            const int warmup = std::max(2, kind.count / 10);
            for (int index = 0; index < kind.count; ++index)
            {
                std::string error;
                bool ok = false;
                try
                {
                    ok = kind.iterate(error);
                }
                catch (const std::exception& exception)
                {
                    error = exception.what();
                }
                if (!ok && failures++ == 0) { firstError = error; }
                if (index == warmup)
                {
                    early = SampleProcess();
                    earlyX = driver.ClientResources(anchor->GetWindowHandle(), &earlyTypes);
                }
            }
            session.PumpFor(std::chrono::milliseconds(300));
            late = SampleProcess();
            lateX = driver.ClientResources(anchor->GetWindowHandle(), &lateTypes);
            Report(std::string("lifetime.") + kind.name, early, late, earlyX, lateX, kind.count);
            // Name what grew: a total alone cannot say whether it is CNA's own windows or, say, a
            // driver's graphics contexts on the connection CNA handed it.
            std::string grew;
            for (const auto& [type, count] : lateTypes)
            {
                const auto before = earlyTypes.find(type);
                const long was = before == earlyTypes.end() ? 0 : before->second;
                if (count > was)
                {
                    grew += " " + type + " " + std::to_string(was) + "->" + std::to_string(count);
                }
            }
            if (!grew.empty())
            {
                Info(std::string("lifetime.") + kind.name + ": X resource types that grew:" + grew);
            }
            Check(failures == 0, std::string("lifetime.") + kind.name + "-iterations",
                  std::to_string(kind.count) + " iterations, " + std::to_string(failures) +
                      " failed" + (firstError.empty() ? std::string() : ": " + firstError));
            // Driver heaps grow in steps and are reused; a real leak grows with the count.
            const long perIteration =
                (late.residentKb - early.residentKb) / std::max(1, kind.count - warmup);
            Check(perIteration < 64, std::string("lifetime.") + kind.name + "-rss-per-iteration",
                  std::to_string(perIteration) + " kB per iteration after warm-up");
            Check(late.openDescriptors <= early.openDescriptors + 2,
                  std::string("lifetime.") + kind.name + "-descriptors",
                  std::to_string(early.openDescriptors) + " -> " + std::to_string(late.openDescriptors));
            if (earlyX >= 0)
            {
                long earlyCounted = earlyX;
                long lateCounted = lateX;
                if (std::string(kind.name) == "vulkan")
                {
                    // Mesa's X11 Vulkan WSI (seen with 25.0.7) leaves resources on the connection
                    // it is handed: a GC per swapchain with every driver, and with lavapipe on a
                    // server with MIT-SHM also the segments its images were attached through. A
                    // program with no CNA in it leaks them the same way (plans/
                    // plan_native_platform_validation.md). CNA's Vulkan path creates neither, so
                    // they are reported here rather than counted against CNA.
                    for (const char* type : {"GC", "ShmSeg"})
                    {
                        earlyCounted -= earlyTypes[type];
                        lateCounted -= lateTypes[type];
                        Info(std::string("lifetime.vulkan: ") + type + " " +
                             std::to_string(earlyTypes[type]) + " -> " +
                             std::to_string(lateTypes[type]) +
                             " (the Vulkan driver's WSI; not counted against CNA)");
                    }
                }
                Check(lateCounted <= earlyCounted,
                      std::string("lifetime.") + kind.name + "-x-resources",
                      std::to_string(earlyCounted) + " -> " + std::to_string(lateCounted));
            }
        }
        Check(driver.ErrorCount() == 0, "lifetime.driver-no-x-errors");
        return Results().failed == 0 ? 0 : 1;
    }

    int RunSoak(const std::vector<std::string>& arguments)
    {
        const int seconds = static_cast<int>(OptionInt(arguments, "seconds", 600));
        Driver driver;
        Session session;
        if (!session.Ok() || !driver.Ok())
        {
            Fail("soak.session", session.Ok() ? "driver connection failed" : session.Error());
            return 1;
        }
        IPlatform& platform = session.Platform();
        IPlatformGlContext* service = platform.GetGlContext();
        if (service == nullptr)
        {
            Skip("soak", "no GLX");
            return 0;
        }
        auto window = session.Make("CNA soak (hardware GL, virtual input)", 900, 600, true,
                                   WindowRenderIntent::OpenGl, 200, 150);
        const WindowId id = window->GetId();
        const auto xid = static_cast<::Window>(window->GetWindowHandle());
        session.WaitFor(id, WindowEventKind::Exposed);
        GlContextDescription requested;
        requested.majorVersion = 3;
        requested.minorVersion = 3;
        requested.profile = GlProfile::Core;
        GlContextHandle context = service->CreateContext(id, requested);
        service->MakeCurrent(id, context);
        auto clearColor = reinterpret_cast<GlTypes::ClearColor>(service->GetProcAddress("glClearColor"));
        auto clear = reinterpret_cast<GlTypes::Clear>(service->GetProcAddress("glClear"));
        auto viewport = reinterpret_cast<GlTypes::Viewport>(service->GetProcAddress("glViewport"));
        auto getError = reinterpret_cast<GlTypes::GetError>(service->GetProcAddress("glGetError"));
        service->SetSwapInterval(1);

        std::string error;
        std::unique_ptr<VirtualInput> keyboard;
        std::unique_ptr<VirtualInput> mouse;
        if (UinputReachesDisplay(driver, arguments, error))
        {
            session.Focus(*window, driver);
            keyboard = VirtualInput::Create(true, false, error);
            if (keyboard != nullptr && !ProbeKeyboardReachesWindow(*keyboard, session, *window))
            {
                keyboard.reset();
                error = "the probe key did not reach the window";
            }
            mouse = VirtualInput::Create(false, true, error);
        }
        Info(std::string("virtual input for the soak: ") +
             (keyboard != nullptr && mouse != nullptr ? "yes" : "no (" + error + ")"));

        std::mt19937 random(31337u);
        std::vector<ProcessSample> samples;
        long frames = 0;
        long glErrors = 0;
        long keyEvents = 0;
        long pointerEvents = 0;
        long stateChanges = 0;
        const int errorsBefore = driver.ErrorCount();
        const double started = NowMs();
        double nextAction = started + 1000.0;
        double nextSample = started + 10000.0;
        while (NowMs() - started < seconds * 1000.0)
        {
            const WindowSize size = window->GetPixelSize();
            viewport(0, 0, size.width, size.height);
            const float t = static_cast<float>(frames % 600) / 600.0f;
            clearColor(t, 0.3f, 1.0f - t, 1.0f);
            clear(GL_COLOR_BUFFER_BIT);
            if (getError() != GL_NO_ERROR) { ++glErrors; }
            service->SwapBuffers(id);
            ++frames;
            for (const PlatformEvent& event : session.Poll())
            {
                if (std::holds_alternative<KeyEvent>(event)) { ++keyEvents; }
                if (std::holds_alternative<MouseMotionEvent>(event) ||
                    std::holds_alternative<MouseButtonEvent>(event) ||
                    std::holds_alternative<MouseWheelEvent>(event))
                {
                    ++pointerEvents;
                }
            }
            session.Clear();

            const double now = NowMs();
            if (now >= nextAction)
            {
                nextAction = now + 700.0 + static_cast<double>(random() % 1300);
                const int action = static_cast<int>(random() % 10);
                const bool focused = window->HasFocus() && driver.FocusIsWithin(xid);
                if (action < 3 && keyboard != nullptr && focused)
                {
                    const int keys[] = {KEY_W, KEY_A, KEY_S, KEY_D, KEY_SPACE, KEY_LEFTSHIFT};
                    keyboard->Tap(keys[random() % 6], 30 + static_cast<int>(random() % 200));
                }
                else if (action < 5 && mouse != nullptr)
                {
                    int wx = 0, wy = 0, rx = 0, ry = 0;
                    if (driver.PointerInside(xid, wx, wy, rx, ry) || SteerPointerInto(*mouse, driver, xid, 450, 300))
                    {
                        for (int step = 0; step < 20; ++step)
                        {
                            mouse->Move(static_cast<int>(random() % 7) - 3, static_cast<int>(random() % 7) - 3);
                        }
                        if (random() % 3 == 0) { mouse->Wheel(random() % 2 == 0 ? 1 : -1); }
                    }
                }
                else if (action == 5)
                {
                    window->SetSize(700 + static_cast<int>(random() % 500),
                                    450 + static_cast<int>(random() % 300));
                    ++stateChanges;
                }
                else if (action == 6 && platform.GetCapabilities().borderlessFullscreen)
                {
                    window->SetFullscreenMode(window->GetFullscreenMode() == WindowFullscreenMode::Windowed
                                                  ? WindowFullscreenMode::BorderlessFullscreen
                                                  : WindowFullscreenMode::Windowed);
                    ++stateChanges;
                }
                else if (action == 7)
                {
                    const ::Window other = driver.CreatePlainWindow("CNA soak: focus thief", 1300, 200, 200, 150);
                    session.PumpFor(std::chrono::milliseconds(150));
                    driver.Activate(other);
                    session.PumpFor(std::chrono::milliseconds(300));
                    driver.DestroyWindow(other);
                    session.Focus(*window, driver, std::chrono::milliseconds(1500));
                    ++stateChanges;
                }
                else if (action == 8)
                {
                    window->Minimize();
                    session.PumpFor(std::chrono::milliseconds(300));
                    window->Restore();
                    session.Focus(*window, driver, std::chrono::milliseconds(1500));
                    ++stateChanges;
                }
                else
                {
                    session.Focus(*window, driver, std::chrono::milliseconds(800));
                }
            }
            if (now >= nextSample)
            {
                nextSample = now + 30000.0;
                samples.push_back(SampleProcess());
                Info("soak @" + std::to_string(static_cast<int>((now - started) / 1000.0)) + " s: " +
                     std::to_string(frames) + " frames, RSS " +
                     std::to_string(samples.back().residentKb) + " kB, fds " +
                     std::to_string(samples.back().openDescriptors));
            }
        }
        if (window->GetFullscreenMode() != WindowFullscreenMode::Windowed)
        {
            window->SetFullscreenMode(WindowFullscreenMode::Windowed);
        }
        session.PumpFor(std::chrono::milliseconds(300));
        IPlatformKeyboard* keys = platform.GetKeyboard();
        keys->Update();
        platform.GetMouse()->Update();
        Info("soak totals: " + std::to_string(frames) + " frames, " + std::to_string(keyEvents) +
             " key events, " + std::to_string(pointerEvents) + " pointer events, " +
             std::to_string(stateChanges) + " state changes");
        Check(glErrors == 0, "soak.no-gl-errors", std::to_string(glErrors));
        Check(driver.ErrorCount() == errorsBefore, "soak.no-x-errors-seen-by-driver");
        Check(keys->GetSnapshot().pressedKeys.empty(), "soak.no-stuck-keys");
        Check((platform.GetMouse()->GetSnapshot().buttons & 0x1F) == 0, "soak.no-stuck-buttons");
        Check(!driver.PointerIsGrabbedElsewhere(), "soak.desktop-not-grabbed");
        if (samples.size() >= 3)
        {
            const long growth = samples.back().residentKb - samples[1].residentKb;
            Check(growth < 8192, "soak.rss-stable",
                  std::to_string(samples[1].residentKb) + " -> " +
                      std::to_string(samples.back().residentKb) + " kB");
        }
        service->MakeCurrent(0, nullptr);
        service->DestroyContext(context);
        return Results().failed == 0 ? 0 : 1;
    }

    int RunInfo(const std::vector<std::string>& arguments)
    {
        (void) arguments;
        Driver driver;
        Session session;
        if (!session.Ok() || !driver.Ok())
        {
            Fail("info.session", session.Ok() ? "driver connection failed" : session.Error());
            return 1;
        }
        IPlatform& platform = session.Platform();
        ::Display* display = driver.GetDisplay();
        Info(std::string("DISPLAY=") + DisplayString(display) + ", vendor \"" + ServerVendor(display) +
             "\" release " + std::to_string(VendorRelease(display)));
        int count = 0;
        char** extensions = XListExtensions(display, &count);
        bool xwayland = false;
        for (int index = 0; index < count; ++index)
        {
            xwayland = xwayland || std::string(extensions[index]) == "XWAYLAND";
        }
        XFreeExtensionList(extensions);
        Info(std::string("server kind: ") + (xwayland ? "Xwayland (extension XWAYLAND present)"
                                                      : "not Xwayland (native Xorg, Xvfb or other)"));
        const PlatformCapabilities caps = platform.GetCapabilities();
        std::ostringstream capabilities;
        capabilities << "multipleWindows=" << caps.multipleWindows << " highDpi=" << caps.highDpi
                     << " multipleDisplays=" << caps.multipleDisplays
                     << " borderlessFullscreen=" << caps.borderlessFullscreen
                     << " nativeWindowHandle=" << caps.nativeWindowHandle
                     << " surfacePresentation=" << caps.surfacePresentation
                     << " openGlContext=" << caps.openGlContext
                     << " vulkanSurface=" << caps.vulkanSurface << " clipboard=" << caps.clipboard
                     << " textInput=" << caps.textInput << " ime=" << caps.ime
                     << " exactKeyboardState=" << caps.exactKeyboardState
                     << " relativeMouse=" << caps.relativeMouse
                     << " cursorShapes=" << caps.cursorShapes
                     << " globalPointer=" << caps.globalPointer
                     << " inputDeviceEnumeration=" << caps.inputDeviceEnumeration;
        Info("capabilities: " + capabilities.str());
        return 0;
    }

} // namespace CnaX11Validation
