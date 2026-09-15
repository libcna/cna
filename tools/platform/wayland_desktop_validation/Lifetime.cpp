// SPDX-License-Identifier: MS-PL
//
// plans/plan_wayland.md WAYLAND-0114/0117: lifetimes, stress and soak -- the same operations
// thousands of times, with the process's resident memory and open descriptors sampled, because a
// leak of one wl_buffer, one memfd or one EGL surface per window is invisible in any single test.

#include "Harness.hpp"

#include "CNA/Platform/IPlatformGlContext.hpp"
#include "CNA/Platform/IPlatformSurfacePresenter.hpp"
#include "CNA/Platform/PlatformException.hpp"

#include <random>

namespace CnaWaylandValidation {

    namespace {

#if defined(__SANITIZE_ADDRESS__)
        /// ASan keeps freed memory in quarantine, so resident size says nothing about leaks
        /// there; LeakSanitizer, at exit, is the leak check of a sanitizer build.
        constexpr bool kResidentSizeMeansAnything = false;
#else
        constexpr bool kResidentSizeMeansAnything = true;
#endif

        void CheckMemory(const long grownKb, const long limitKb, const std::string& check, const std::string& detail)
        {
            if (kResidentSizeMeansAnything)
            {
                Check(grownKb < limitKb, check, detail);
            }
            else
            {
                Skip(check, "an AddressSanitizer build: its quarantine holds freed memory, LeakSanitizer checks at exit");
            }
        }

        std::string Delta(const ProcessSample& before, const ProcessSample& after)
        {
            return "RSS " + std::to_string(before.residentKb) + " -> " + std::to_string(after.residentKb) + " kB, fds " +
                   std::to_string(before.openDescriptors) + " -> " + std::to_string(after.openDescriptors);
        }

        /// One GL window, context, a few frames, torn down.
        bool GlIteration(Session& session, std::string& error)
        {
            IPlatformGlContext* gl = session.Platform().GetGlContext();
            if (gl == nullptr)
            {
                error = "no GL";
                return false;
            }
            auto window = session.Make("CNA GL lifetime", 320, 240, true, WindowRenderIntent::OpenGl);
            try
            {
                GlContextHandle context = gl->CreateContext(window->GetId(), GlContextDescription{});
                gl->MakeCurrent(window->GetId(), context);
                auto clear = reinterpret_cast<void (*)(unsigned int)>(gl->GetProcAddress("glClear"));
                for (int frame = 0; frame < 3; ++frame)
                {
                    clear(0x4000);
                    gl->SwapBuffers(window->GetId());
                    session.Poll();
                }
                gl->MakeCurrent(0, nullptr);
                gl->DestroyContext(context);
            }
            catch (const PlatformException& exception)
            {
                error = exception.what();
                return false;
            }
            return true;
        }

    } // namespace

    int RunLifetime(const std::vector<std::string>& arguments)
    {
        const int iterations = static_cast<int>(OptionInt(arguments, "iterations", 50));
        const bool validation = OptionFlag(arguments, "validation");
        Session session;
        if (!Check(session.Ok(), "lifetime.connect", session.Error()))
        {
            return 1;
        }
        Wayland::WaylandPlatform& platform = session.Platform();

        // A warm-up round first: libraries load, drivers allocate their caches, and none of that
        // is a leak.
        std::string error;
        {
            auto window = session.Make("CNA shm lifetime", 320, 240);
            auto presenter = platform.CreateSurfacePresenter(*window);
            PresentSolid(*presenter, *window, 0x808080);
        }
        (void) GlIteration(session, error);
        (void) VulkanLifetime::Iteration(session, validation, error);
        session.PumpFor(std::chrono::milliseconds(100));
        const ProcessSample before = SampleProcess();

        int shmFailures = 0;
        int glFailures = 0;
        int vulkanFailures = 0;
        int validationErrors = 0;
        const bool haveGl = platform.GetGlContext() != nullptr;
        const bool haveVulkan = platform.GetVulkanSurface() != nullptr;
        for (int iteration = 0; iteration < iterations; ++iteration)
        {
            {
                auto window = session.Make("CNA shm lifetime", 320 + iteration % 7, 240);
                auto presenter = platform.CreateSurfacePresenter(*window);
                for (int frame = 0; frame < 3; ++frame)
                {
                    PresentSolid(*presenter, *window, 0x404040u + static_cast<std::uint32_t>(frame));
                    session.Poll();
                }
                shmFailures += session.Alive() ? 0 : 1;
            }
            if (haveGl && !GlIteration(session, error))
            {
                ++glFailures;
            }
            if (haveVulkan)
            {
                const int errors = VulkanLifetime::Iteration(session, validation, error);
                if (errors < 0)
                {
                    ++vulkanFailures;
                }
                else
                {
                    validationErrors = errors;
                }
            }
            session.Clear();
        }
        session.PumpFor(std::chrono::milliseconds(200));
        const ProcessSample after = SampleProcess();
        Info(Delta(before, after) + " over " + std::to_string(iterations) + " iterations");
        Check(shmFailures == 0, "lifetime.shm-windows", std::to_string(iterations) + " windows");
        if (haveGl)
        {
            Check(glFailures == 0, "lifetime.gl-contexts", error);
        }
        if (haveVulkan)
        {
            Check(vulkanFailures == 0, "lifetime.vulkan-surfaces", error);
            if (validation)
            {
                Check(validationErrors == 0, "lifetime.vulkan-validation-clean", std::to_string(validationErrors) + " errors");
            }
        }
        // Descriptors are exact: every memfd, pipe and dmabuf a window used is closed with it.
        Check(after.openDescriptors <= before.openDescriptors + 2, "lifetime.no-descriptor-leak", Delta(before, after));
        // Memory is noisier (allocator pools, driver caches): a real per-iteration leak of a
        // window's buffers grows far past this.
        CheckMemory(after.residentKb - before.residentKb, 64 * 1024, "lifetime.no-memory-growth", Delta(before, after));
        Check(session.Alive(), "lifetime.no-protocol-error", session.ConnectionError());
        return 0;
    }

    int RunStress(const std::vector<std::string>& arguments)
    {
        const int operations = static_cast<int>(OptionInt(arguments, "ops", 2000));
        Session session;
        if (!Check(session.Ok(), "stress.connect", session.Error()))
        {
            return 1;
        }
        Wayland::WaylandPlatform& platform = session.Platform();
        struct Live
        {
            std::unique_ptr<IPlatformWindow> window;
            std::unique_ptr<IPlatformSurfacePresenter> presenter;
        };
        std::vector<Live> windows;
        std::mt19937 random(20260915u);
        const auto make = [&] {
            Live live;
            live.window = session.Make("CNA stress", 200 + static_cast<int>(random() % 400), 150 + static_cast<int>(random() % 300));
            live.presenter = platform.CreateSurfacePresenter(*live.window);
            live.presenter->SetVSync(false);
            PresentSolid(*live.presenter, *live.window, random() & 0xFFFFFF);
            windows.push_back(std::move(live));
        };
        make();
        const ProcessSample before = SampleProcess();
        int counts[10] = {};
        for (int operation = 0; operation < operations && session.Alive(); ++operation)
        {
            const int kind = static_cast<int>(random() % 10);
            ++counts[kind];
            if (windows.empty())
            {
                make();
                continue;
            }
            // Creating or destroying a window changes the vector, so neither keeps a reference.
            if (kind == 0)
            {
                if (windows.size() < 6) { make(); }
                session.Poll();
                continue;
            }
            if (kind == 1)
            {
                if (windows.size() > 1) { windows.erase(windows.begin() + static_cast<long>(random() % windows.size())); }
                session.Poll();
                continue;
            }
            Live& target = windows[random() % windows.size()];
            switch (kind)
            {
                case 2: target.window->SetSize(200 + static_cast<int>(random() % 600), 150 + static_cast<int>(random() % 400)); break;
                case 3: target.window->Maximize(); break;
                case 4: target.window->Restore(); break;
                case 5:
                    target.window->SetFullscreenMode(random() % 2 == 0 ? WindowFullscreenMode::BorderlessFullscreen
                                                                      : WindowFullscreenMode::Windowed);
                    break;
                case 6: target.window->SetTitle("CNA stress " + std::to_string(operation)); break;
                case 7:
                    target.window->Hide();
                    target.window->Show();
                    break;
                case 8: target.window->SetBorderless(random() % 2 == 0); break;
                default: break;
            }
            PresentSolid(*target.presenter, *target.window, random() & 0xFFFFFF);
            session.Poll();
            if (operation % 50 == 0)
            {
                target.window->Sync();
                session.Clear();
            }
        }
        for (Live& live : windows)
        {
            live.window->SetFullscreenMode(WindowFullscreenMode::Windowed);
        }
        windows.clear();
        session.PumpFor(std::chrono::milliseconds(300));
        const ProcessSample after = SampleProcess();
        Info("operations: create " + std::to_string(counts[0]) + ", destroy " + std::to_string(counts[1]) + ", resize " +
             std::to_string(counts[2]) + ", maximize " + std::to_string(counts[3]) + ", restore " + std::to_string(counts[4]) +
             ", fullscreen toggle " + std::to_string(counts[5]) + ", title " + std::to_string(counts[6]) + ", hide/show " +
             std::to_string(counts[7]) + ", borderless " + std::to_string(counts[8]));
        Check(session.Alive(), "stress.no-protocol-error", session.ConnectionError());
        Check(after.openDescriptors <= before.openDescriptors + 2, "stress.no-descriptor-leak", Delta(before, after));
        CheckMemory(after.residentKb - before.residentKb, 64 * 1024, "stress.no-memory-growth", Delta(before, after));
        return 0;
    }

    int RunSoak(const std::vector<std::string>& arguments)
    {
        const int seconds = static_cast<int>(OptionInt(arguments, "seconds", 60));
        Session session;
        if (!Check(session.Ok(), "soak.connect", session.Error()))
        {
            return 1;
        }
        Wayland::WaylandPlatform& platform = session.Platform();
        auto window = session.Make("CNA Wayland soak", 800, 600, true, WindowRenderIntent::None, true);
        auto presenter = platform.CreateSurfacePresenter(*window);
        presenter->SetVSync(true);
        PresentSolid(*presenter, *window, 0);
        session.PumpFor(std::chrono::milliseconds(200));
        const ProcessSample before = SampleProcess();
        const double started = NowMs();
        long frames = 0;
        int changes = 0;
        while (NowMs() - started < seconds * 1000.0 && session.Alive())
        {
            PresentSolid(*presenter, *window, static_cast<std::uint32_t>(frames % 256) * 0x010101u);
            session.Poll();
            session.Clear();
            ++frames;
            if (frames % 300 == 0)
            {
                ++changes;
                switch (changes % 4)
                {
                    case 0: window->SetSize(800, 600); break;
                    case 1: window->SetSize(1024, 700); break;
                    case 2: window->Maximize(); break;
                    default: window->Restore(); break;
                }
            }
        }
        window->Restore();
        window->Sync();
        const ProcessSample after = SampleProcess();
        const double elapsed = (NowMs() - started) / 1000.0;
        Info(std::to_string(frames) + " frames in " + std::to_string(elapsed) + " s (" +
             std::to_string(static_cast<int>(frames / elapsed)) + " fps), " + std::to_string(changes) + " state changes; " +
             Delta(before, after));
        Check(session.Alive(), "soak.no-protocol-error", session.ConnectionError());
        Check(after.openDescriptors <= before.openDescriptors + 2, "soak.no-descriptor-leak", Delta(before, after));
        CheckMemory(after.residentKb - before.residentKb, 32 * 1024, "soak.no-memory-growth", Delta(before, after));
        return 0;
    }

} // namespace CnaWaylandValidation
