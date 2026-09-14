// SPDX-License-Identifier: MS-PL
//
// Phase 18: the CPU-frame presenter (IPlatformSurfacePresenter) -- the path the SOFTWARE renderer
// takes. Pixels are read back off the window over the harness's own connection (XGetImage on
// another client's window), so what is checked is what the X server holds, not what CNA meant to
// send. Which transport was used -- MIT-SHM or plain XPutImage -- is visible in the X-Resource
// counts of CNA's client; run the scenario once on a server started with `-extension MIT-SHM` to
// force the fallback.

#include "Harness.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace CnaX11Validation {

    namespace {

        int Unpack(const unsigned long value, const unsigned long mask)
        {
            if (mask == 0) { return -1; }
            int shift = 0;
            unsigned long probe = mask;
            while ((probe & 1u) == 0) { probe >>= 1; ++shift; }
            int bits = 0;
            while ((probe & 1u) != 0) { probe >>= 1; ++bits; }
            const unsigned long channel = (value & mask) >> shift;
            return static_cast<int>(bits >= 8 ? (channel >> (bits - 8)) : (channel << (8 - bits)));
        }

        /// A frame whose every pixel is predictable from its coordinates.
        std::vector<std::uint8_t> Pattern(const int width, const int height, const int seed)
        {
            std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    std::uint8_t* pixel = &pixels[(static_cast<std::size_t>(y) * width + x) * 4];
                    pixel[0] = static_cast<std::uint8_t>((x * 255) / std::max(1, width - 1));
                    pixel[1] = static_cast<std::uint8_t>((y * 255) / std::max(1, height - 1));
                    pixel[2] = static_cast<std::uint8_t>((seed * 37) & 0xFF);
                    pixel[3] = 0xFF;
                }
            }
            return pixels;
        }

        /// Samples a grid of points off the window and compares them with the stretched pattern.
        bool Verify(Driver& driver, IPlatformWindow& window, const std::vector<std::uint8_t>& source,
                    const int sourceWidth, const int sourceHeight, std::string& detail)
        {
            ::Display* display = driver.GetDisplay();
            const auto xid = static_cast<::Window>(window.GetWindowHandle());
            XWindowAttributes attributes{};
            if (XGetWindowAttributes(display, xid, &attributes) == 0)
            {
                detail = "window gone";
                return false;
            }
            XImage* image = XGetImage(display, xid, 0, 0, static_cast<unsigned int>(attributes.width),
                                      static_cast<unsigned int>(attributes.height), AllPlanes,
                                      ZPixmap);
            if (image == nullptr)
            {
                detail = "XGetImage failed";
                return false;
            }
            int bad = 0;
            int samples = 0;
            for (int gy = 1; gy < 8; ++gy)
            {
                for (int gx = 1; gx < 8; ++gx)
                {
                    const int x = gx * attributes.width / 8;
                    const int y = gy * attributes.height / 8;
                    const int sourceX = std::min(sourceWidth - 1, x * sourceWidth / attributes.width);
                    const int sourceY = std::min(sourceHeight - 1, y * sourceHeight / attributes.height);
                    const std::uint8_t* expected =
                        &source[(static_cast<std::size_t>(sourceY) * sourceWidth + sourceX) * 4];
                    const unsigned long value = XGetPixel(image, x, y);
                    const int red = Unpack(value, attributes.visual->red_mask);
                    const int green = Unpack(value, attributes.visual->green_mask);
                    const int blue = Unpack(value, attributes.visual->blue_mask);
                    ++samples;
                    if (std::abs(red - expected[0]) > 8 || std::abs(green - expected[1]) > 8 ||
                        std::abs(blue - expected[2]) > 8)
                    {
                        if (bad == 0)
                        {
                            std::ostringstream text;
                            text << "first mismatch at " << x << "," << y << ": got " << red << ","
                                 << green << "," << blue << " wanted " << int(expected[0]) << ","
                                 << int(expected[1]) << "," << int(expected[2]);
                            detail = text.str();
                        }
                        ++bad;
                    }
                }
            }
            XDestroyImage(image);
            if (bad == 0)
            {
                detail = std::to_string(samples) + " samples at " + std::to_string(attributes.width) +
                         "x" + std::to_string(attributes.height);
            }
            return bad == 0;
        }

    } // namespace

    int RunPresenter(const std::vector<std::string>& arguments)
    {
        const int rapid = static_cast<int>(OptionInt(arguments, "rapid", 60));
        Driver driver;
        Session session;
        if (!session.Ok() || !driver.Ok())
        {
            Fail("presenter.session", session.Ok() ? "driver connection failed" : session.Error());
            return 1;
        }
        IPlatform& platform = session.Platform();
        Check(platform.GetCapabilities().surfacePresentation, "presenter.capability");
        int major = 0;
        int minor = 0;
        CNA::Platform::X11::XBool pixmaps = 0;
        const bool shmOnServer = XShmQueryVersion(driver.GetDisplay(), &major, &minor, &pixmaps) != 0;
        Info(std::string("MIT-SHM on this server: ") + (shmOnServer ? "yes" : "no (XPutImage path)"));

        auto window = session.Make("CNA presenter validation", 640, 480);
        const WindowId id = window->GetId();
        session.WaitFor(id, WindowEventKind::Exposed);
        std::unique_ptr<IPlatformSurfacePresenter> presenter = platform.CreateSurfacePresenter(*window);
        presenter->SetScaleMode(PresentScaleMode::Stretch, PresentFilter::Nearest);

        std::map<std::string, long> before;
        driver.ClientResources(window->GetWindowHandle(), &before);

        // Same size, then a smaller frame stretched.
        {
            const std::vector<std::uint8_t> frame = Pattern(640, 480, 1);
            presenter->Present({frame.data(), 640, 480, 0});
            driver.Sync();
            std::string detail;
            Check(Verify(driver, *window, frame, 640, 480, detail), "presenter.pixels-at-native-size",
                  detail);
            const std::vector<std::uint8_t> small = Pattern(160, 120, 2);
            presenter->Present({small.data(), 160, 120, 0});
            driver.Sync();
            Check(Verify(driver, *window, small, 160, 120, detail), "presenter.pixels-stretched",
                  detail);
        }
        std::map<std::string, long> during;
        driver.ClientResources(window->GetWindowHandle(), &during);
        std::ostringstream resources;
        for (const auto& [type, count] : during)
        {
            const long was = before.count(type) != 0 ? before.at(type) : 0;
            if (count != was) { resources << type << " " << was << "->" << count << " "; }
        }
        Info("X resources that changed when presenting: " +
             (resources.str().empty() ? std::string("none") : resources.str()));

        // Resize, then present at the new size.
        {
            session.Clear();
            window->SetSize(900, 500);
            window->Sync();
            session.WaitFor(id, WindowEventKind::Resized);
            int width = 0;
            int height = 0;
            presenter->GetTargetSize(width, height);
            Check(width == 900 && height == 500, "presenter.target-follows-resize",
                  std::to_string(width) + "x" + std::to_string(height));
            const std::vector<std::uint8_t> frame = Pattern(900, 500, 3);
            presenter->Present({frame.data(), 900, 500, 0});
            driver.Sync();
            std::string detail;
            Check(Verify(driver, *window, frame, 900, 500, detail), "presenter.pixels-after-resize",
                  detail);
        }

        // Rapid resize: every frame at a new size.
        {
            int bad = 0;
            std::string firstBad;
            for (int index = 0; index < rapid; ++index)
            {
                const int width = 200 + (index * 97) % 900;
                const int height = 150 + (index * 53) % 600;
                window->SetSize(width, height);
                window->Sync();
                session.Poll();
                const WindowSize size = window->GetPixelSize();
                const std::vector<std::uint8_t> frame = Pattern(size.width, size.height, index);
                presenter->Present({frame.data(), size.width, size.height, 0});
                driver.Sync();
                std::string detail;
                if (!Verify(driver, *window, frame, size.width, size.height, detail))
                {
                    if (bad++ == 0) { firstBad = detail; }
                }
            }
            Check(bad == 0, "presenter.rapid-resize",
                  std::to_string(rapid) + " sizes, " + std::to_string(bad) + " bad" +
                      (firstBad.empty() ? std::string() : ": " + firstBad));
        }

        // Cost per frame -- measured, not judged: nothing in the contract bounds it.
        for (const auto& [width, height] : {std::pair{800, 600}, std::pair{1920, 1080}})
        {
            window->SetSize(width, height);
            window->Sync();
            session.PumpFor(std::chrono::milliseconds(100));
            const WindowSize size = window->GetPixelSize();
            const std::vector<std::uint8_t> frame = Pattern(size.width, size.height, 9);
            const double started = NowMs();
            for (int index = 0; index < 30; ++index)
            {
                presenter->Present({frame.data(), size.width, size.height, 0});
            }
            driver.Sync();
            const double perFrame = (NowMs() - started) / 30.0;
            Info("presenter cost at " + std::to_string(size.width) + "x" +
                 std::to_string(size.height) + ": " + std::to_string(perFrame) + " ms per frame");
        }

        presenter.reset();
        window.reset();
        session.PumpFor(std::chrono::milliseconds(200));
        Check(driver.ErrorCount() == 0, "presenter.driver-no-x-errors");
        return Results().failed == 0 ? 0 : 1;
    }

} // namespace CnaX11Validation
