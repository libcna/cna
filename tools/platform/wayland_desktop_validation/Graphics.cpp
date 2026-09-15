// SPDX-License-Identifier: MS-PL
//
// plans/plan_wayland.md WAYLAND-0114/0121: the wl_shm presenter and EGL/OpenGL on the real
// desktop and the real GPU.

#include "Harness.hpp"

#include "CNA/Platform/IPlatformGlContext.hpp"
#include "CNA/Platform/IPlatformSurfacePresenter.hpp"
#include "CNA/Platform/PlatformException.hpp"

#include <cmath>
#include <cstring>
#include <sstream>

namespace CnaWaylandValidation {

    int RunPresenter(const std::vector<std::string>& arguments)
    {
        const int frames = static_cast<int>(OptionInt(arguments, "frames", 240));
        Session session;
        if (!Check(session.Ok(), "presenter.connect", session.Error()))
        {
            return 1;
        }
        Wayland::WaylandPlatform& platform = session.Platform();
        if (!platform.GetCapabilities().surfacePresentation)
        {
            Skip("presenter", "the compositor offers no wl_shm XRGB8888");
            return 0;
        }
        const auto window = session.Make("CNA Wayland presenter", 800, 480);
        const auto presenter = platform.CreateSurfacePresenter(*window);

        presenter->SetVSync(true);
        PresentSolid(*presenter, *window, 0);
        double started = NowMs();
        for (int frame = 0; frame < frames; ++frame)
        {
            PresentSolid(*presenter, *window, static_cast<std::uint32_t>((frame * 3) & 0xFF) << 8);
            session.Poll();
        }
        const double vsyncSeconds = (NowMs() - started) / 1000.0;
        const double vsyncRate = frames / vsyncSeconds;
        Info("vsync: " + std::to_string(frames) + " frames in " + std::to_string(vsyncSeconds) + " s = " +
             std::to_string(vsyncRate) + " fps");
        // Paced by the compositor: at most a little above the fastest refresh any desktop has, and
        // not the 10 fps of a frame callback that never comes (the pacer's 100 ms bound).
        Check(vsyncRate < 400.0, "presenter.vsync-is-paced", std::to_string(vsyncRate) + " fps");
        if (vsyncRate < 12.0)
        {
            Info("~10 fps means the compositor sent no frame callbacks: the window is not being drawn (a locked "
                 "screen, another workspace) -- pacing then falls back to its 100 ms bound, by design");
        }

        presenter->SetVSync(false);
        started = NowMs();
        for (int frame = 0; frame < frames; ++frame)
        {
            PresentSolid(*presenter, *window, 0x303030);
            session.Poll();
        }
        const double freeRate = frames / ((NowMs() - started) / 1000.0);
        Info("no vsync: " + std::to_string(freeRate) + " fps (three buffers, each released by the compositor)");
        Check(freeRate > 0.0, "presenter.unpaced-throughput");

        for (const PresentScaleMode mode : {PresentScaleMode::Stretch, PresentScaleMode::Letterbox, PresentScaleMode::Overscan,
                                            PresentScaleMode::None, PresentScaleMode::Native})
        {
            presenter->SetScaleMode(mode, PresentFilter::Linear);
            std::vector<std::uint8_t> small(320 * 200 * 4, 0x80);
            SurfaceFrame frame;
            frame.pixels = small.data();
            frame.width = 320;
            frame.height = 200;
            frame.strideBytes = 320 * 4;
            presenter->Present(frame);
            session.Poll();
        }
        window->SetSize(1024, 640);
        PresentSolid(*presenter, *window, 0x102040);
        window->Sync();
        session.PumpFor(std::chrono::milliseconds(100));
        int width = 0;
        int height = 0;
        presenter->GetTargetSize(width, height);
        Check(width == window->GetPixelSize().width && height == window->GetPixelSize().height,
              "presenter.follows-a-resize", std::to_string(width) + "x" + std::to_string(height));
        Check(session.Alive(), "presenter.no-protocol-error", session.ConnectionError());
        return 0;
    }

    namespace {

        namespace Gl {
            using Enum = unsigned int;
            using GetString = const unsigned char* (*)(Enum);
            using ClearColor = void (*)(float, float, float, float);
            using Clear = void (*)(unsigned int);
            using ReadPixels = void (*)(int, int, int, int, Enum, Enum, void*);
            using Viewport = void (*)(int, int, int, int);
            using Finish = void (*)();
            using GetError = Enum (*)();
            constexpr Enum kVendor = 0x1F00;
            constexpr Enum kRenderer = 0x1F01;
            constexpr Enum kVersion = 0x1F02;
            constexpr unsigned int kColorBufferBit = 0x4000;
            constexpr Enum kRgba = 0x1908;
            constexpr Enum kUnsignedByte = 0x1401;
        }

        struct GlFunctions
        {
            Gl::GetString getString = nullptr;
            Gl::ClearColor clearColor = nullptr;
            Gl::Clear clear = nullptr;
            Gl::ReadPixels readPixels = nullptr;
            Gl::Viewport viewport = nullptr;
            Gl::Finish finish = nullptr;
            Gl::GetError getError = nullptr;

            explicit GlFunctions(IPlatformGlContext& gl)
            {
                getString = reinterpret_cast<Gl::GetString>(gl.GetProcAddress("glGetString"));
                clearColor = reinterpret_cast<Gl::ClearColor>(gl.GetProcAddress("glClearColor"));
                clear = reinterpret_cast<Gl::Clear>(gl.GetProcAddress("glClear"));
                readPixels = reinterpret_cast<Gl::ReadPixels>(gl.GetProcAddress("glReadPixels"));
                viewport = reinterpret_cast<Gl::Viewport>(gl.GetProcAddress("glViewport"));
                finish = reinterpret_cast<Gl::Finish>(gl.GetProcAddress("glFinish"));
                getError = reinterpret_cast<Gl::GetError>(gl.GetProcAddress("glGetError"));
            }

            [[nodiscard]] bool Ok() const { return getString && clearColor && clear && readPixels && viewport && finish; }

            /// Clears to a colour and reads the pixel at (x, y) back: 0xRRGGBB.
            std::uint32_t ClearAndRead(const int width, const int height, const float r, const float g, const float b,
                                       const int x, const int y) const
            {
                viewport(0, 0, width, height);
                clearColor(r, g, b, 1.0f);
                clear(Gl::kColorBufferBit);
                finish();
                std::uint8_t pixel[4] = {};
                readPixels(x, y, 1, 1, Gl::kRgba, Gl::kUnsignedByte, pixel);
                return (static_cast<std::uint32_t>(pixel[0]) << 16) | (static_cast<std::uint32_t>(pixel[1]) << 8) | pixel[2];
            }
        };

        std::string Hex(const std::uint32_t value)
        {
            std::ostringstream text;
            text << "0x" << std::hex << value;
            return text.str();
        }

    } // namespace

    int RunGl(const std::vector<std::string>& arguments)
    {
        const int frames = static_cast<int>(OptionInt(arguments, "frames", 240));
        const bool allowSoftware = OptionFlag(arguments, "allow-software");
        Session session;
        if (!Check(session.Ok(), "gl.connect", session.Error()))
        {
            return 1;
        }
        Wayland::WaylandPlatform& platform = session.Platform();
        IPlatformGlContext* gl = platform.GetGlContext();
        if (gl == nullptr)
        {
            Skip("gl", "no EGL with a Wayland platform on this machine");
            return 0;
        }

        // Which context versions and profiles this EGL gives a Wayland window.
        {
            WindowDescription description;
            description.title = "CNA Wayland GL versions";
            description.width = 64;
            description.height = 64;
            description.renderIntent = WindowRenderIntent::OpenGl;
            struct Wanted
            {
                int major;
                int minor;
                GlProfile profile;
                const char* name;
            };
            for (const Wanted wanted : {Wanted{4, 6, GlProfile::Core, "4.6 core"}, Wanted{3, 3, GlProfile::Core, "3.3 core"},
                                        Wanted{2, 1, GlProfile::Compatibility, "2.1 compatibility"},
                                        Wanted{3, 0, GlProfile::Es, "ES 3.0"}, Wanted{2, 0, GlProfile::Es, "ES 2.0"}})
            {
                GlContextDescription attributes;
                attributes.majorVersion = wanted.major;
                attributes.minorVersion = wanted.minor;
                attributes.profile = wanted.profile;
                // A window of its own for each: an EGL surface belongs to one client API.
                const auto probe = platform.CreateWindow(description);
                try
                {
                    GlContextHandle context = gl->CreateContext(probe->GetId(), attributes);
                    const GlContextDescription actual = gl->GetContextAttributes(context);
                    Info(std::string("context ") + wanted.name + ": got " + std::to_string(actual.majorVersion) + "." +
                         std::to_string(actual.minorVersion));
                    gl->MakeCurrent(0, nullptr);
                    gl->DestroyContext(context);
                }
                catch (const PlatformException& error)
                {
                    Info(std::string("context ") + wanted.name + ": refused (" + error.what() + ")");
                }
            }
        }

        const auto window = session.Make("CNA Wayland GL", 800, 600, true, WindowRenderIntent::OpenGl);
        const WindowId id = window->GetId();
        GlContextDescription wanted;
        GlContextHandle context = nullptr;
        try
        {
            context = gl->CreateContext(id, wanted);
        }
        catch (const PlatformException& error)
        {
            Fail("gl.context-3.3-core", error.what());
            return 1;
        }
        gl->MakeCurrent(id, context);
        GlFunctions f(*gl);
        if (!Check(f.Ok(), "gl.entry-points"))
        {
            return 1;
        }
        const std::string renderer = reinterpret_cast<const char*>(f.getString(Gl::kRenderer));
        const std::string vendor = reinterpret_cast<const char*>(f.getString(Gl::kVendor));
        const std::string version = reinterpret_cast<const char*>(f.getString(Gl::kVersion));
        Info("GL_VENDOR " + vendor + " | GL_RENDERER " + renderer + " | GL_VERSION " + version);
        const bool software = renderer.find("llvmpipe") != std::string::npos || renderer.find("softpipe") != std::string::npos ||
                              renderer.find("swrast") != std::string::npos;
        if (allowSoftware)
        {
            Info(software ? "software rasterizer (allowed)" : "hardware rasterizer");
        }
        else
        {
            Check(!software, "gl.hardware-accelerated", renderer);
        }

        const std::uint32_t centre = f.ClearAndRead(800, 600, 0.25f, 0.5f, 0.75f, 400, 300);
        Check(centre == 0x4080BF || centre == 0x4080C0, "gl.clear-reads-back", Hex(centre));
        gl->SwapBuffers(id);
        session.Poll();

        const bool intervalOne = gl->SetSwapInterval(1);
        double started = NowMs();
        for (int frame = 0; frame < frames; ++frame)
        {
            (void) f.ClearAndRead(window->GetPixelSize().width, window->GetPixelSize().height,
                                  static_cast<float>(frame % 60) / 60.0f, 0.2f, 0.4f, 0, 0);
            gl->SwapBuffers(id);
            session.Poll();
        }
        const double paced = frames / ((NowMs() - started) / 1000.0);
        Info("swap interval 1: " + std::to_string(paced) + " fps");
        Check(intervalOne && paced < 400.0, "gl.swap-interval-1-is-paced", std::to_string(paced) + " fps");
        (void) gl->SetSwapInterval(0);
        started = NowMs();
        for (int frame = 0; frame < frames; ++frame)
        {
            f.viewport(0, 0, window->GetPixelSize().width, window->GetPixelSize().height);
            f.clearColor(0.1f, 0.1f, static_cast<float>(frame % 60) / 60.0f, 1.0f);
            f.clear(Gl::kColorBufferBit);
            gl->SwapBuffers(id);
            session.Poll();
        }
        const double unpaced = frames / ((NowMs() - started) / 1000.0);
        Info("swap interval 0: " + std::to_string(unpaced) + " fps");
        Check(unpaced > 0.0, "gl.swap-interval-0");

        session.Clear();
        window->SetSize(1000, 700);
        session.WaitFor(id, WindowEventKind::PixelSizeChanged, std::chrono::milliseconds(1000));
        const std::uint32_t corner = f.ClearAndRead(1000, 700, 1.0f, 0.0f, 0.0f, 999, 699);
        Check(corner == 0xFF0000, "gl.resize-reaches-the-egl-surface", Hex(corner));
        gl->SwapBuffers(id);
        window->Sync();
        session.Poll();
        gl->MakeCurrent(0, nullptr);
        gl->DestroyContext(context);

        // A high-DPI GL window draws at the output's scale.
        const auto hidpi = session.Make("CNA Wayland GL high DPI", 800, 600, true, WindowRenderIntent::OpenGl, true);
        GlContextHandle hidpiContext = gl->CreateContext(hidpi->GetId(), wanted);
        gl->MakeCurrent(hidpi->GetId(), hidpiContext);
        for (int frame = 0; frame < 5; ++frame)
        {
            const WindowSize size = hidpi->GetPixelSize();
            (void) f.ClearAndRead(size.width, size.height, 0.0f, 1.0f, 0.0f, 0, 0);
            gl->SwapBuffers(hidpi->GetId());
            hidpi->Sync();
            session.Poll();
        }
        const WindowSize size = hidpi->GetPixelSize();
        const std::uint32_t far = f.ClearAndRead(size.width, size.height, 0.0f, 1.0f, 0.0f, size.width - 1, size.height - 1);
        Check(far == 0x00FF00, "gl.high-dpi-drawable-is-the-pixel-size",
              std::to_string(size.width) + "x" + std::to_string(size.height) + " at scale " +
                  std::to_string(hidpi->GetDisplayScale()) + ", corner " + Hex(far));
        gl->SwapBuffers(hidpi->GetId());
        gl->MakeCurrent(0, nullptr);
        gl->DestroyContext(hidpiContext);
        Check(session.Alive(), "gl.no-protocol-error", session.ConnectionError());
        return 0;
    }

} // namespace CnaWaylandValidation
