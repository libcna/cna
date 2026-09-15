// SPDX-License-Identifier: MS-PL
//
// plans/plan_wayland.md WAYLAND-0112/0113: the Wayland backend against a REAL compositor -- the
// private one tools/platform/wayland_test_server.sh starts (headless Weston, or headless
// gnome-shell), never the desktop's.
//
// The in-process compositor proves the backend follows the protocols; this proves a compositor
// someone else wrote agrees: that it maps what the backend draws, answers its state requests the
// way the backend expects, paces its frames, and -- with the GL renderer -- takes the dmabuf
// buffers EGL and Vulkan make on the real GPU. Everything skips without the launcher.

#include <gtest/gtest.h>

#include "../../../src/Wayland/WaylandPlatform.hpp"

#include "CNA/Platform/IPlatformGlContext.hpp"
#include "CNA/Platform/IPlatformSurfacePresenter.hpp"
#include "CNA/Platform/IPlatformSystemServices.hpp"
#include "CNA/Platform/IPlatformVulkanSurface.hpp"
#include "CNA/Platform/PlatformException.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace CNA::Platform;
using namespace CNA::Platform::Wayland;
using namespace std::chrono_literals;

std::string Launcher()
{
    const char* display = std::getenv("CNA_WAYLAND_TEST_DISPLAY");
    return display != nullptr ? display : "";
}

std::string CompositorName()
{
    const char* name = std::getenv("CNA_WAYLAND_TEST_COMPOSITOR");
    return name != nullptr ? name : "";
}

class WaylandLive : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (Launcher().empty())
        {
            GTEST_SKIP() << "no private compositor: run through tools/platform/wayland_test_server.sh";
        }
        platform_ = std::make_unique<WaylandPlatform>();
        ASSERT_NE(platform_->GetConnectionForTesting(), nullptr) << platform_->GetConnectionError();
        platform_->AcquireSubsystem(PlatformSubsystem::Video);
    }

    void TearDown() override
    {
        presenters_.clear();
        windows_.clear();
        if (platform_ != nullptr)
        {
            if (WaylandConnection* connection = platform_->GetConnectionForTesting())
            {
                (void) connection->Roundtrip(1s);
                EXPECT_TRUE(connection->IsAlive()) << "the compositor ended the connection: " << connection->GetError();
            }
            platform_->ReleaseSubsystem(PlatformSubsystem::Video);
        }
        platform_.reset();
    }

    IPlatformWindow& MakeWindow(WindowDescription description = {})
    {
        if (description.title.empty())
        {
            description.title = "CNA live test";
        }
        windows_.push_back(platform_->CreateWindow(description));
        return *windows_.back();
    }

    IPlatformSurfacePresenter& PresenterFor(IPlatformWindow& window)
    {
        presenters_.push_back(platform_->CreateSurfacePresenter(window));
        return *presenters_.back();
    }

    void Present(IPlatformSurfacePresenter& presenter, IPlatformWindow& window, const std::uint8_t shade)
    {
        const WindowSize size = window.GetPixelSize();
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size.width) * size.height * 4, shade);
        SurfaceFrame frame;
        frame.pixels = pixels.data();
        frame.width = size.width;
        frame.height = size.height;
        frame.strideBytes = size.width * 4;
        presenter.Present(frame);
    }

    void Pump()
    {
        batch_.clear();
        platform_->PollEvents(batch_);
        seen_.insert(seen_.end(), batch_.begin(), batch_.end());
    }

    [[nodiscard]] int CountWindowEvents(const WindowId window, const WindowEventKind kind) const
    {
        int count = 0;
        for (const PlatformEvent& event : seen_)
        {
            if (const auto* windowEvent = std::get_if<WindowEvent>(&event))
            {
                count += windowEvent->window == window && windowEvent->kind == kind ? 1 : 0;
            }
        }
        return count;
    }

    std::unique_ptr<WaylandPlatform> platform_;
    std::vector<std::unique_ptr<IPlatformWindow>> windows_;
    std::vector<std::unique_ptr<IPlatformSurfacePresenter>> presenters_;
    std::vector<PlatformEvent> batch_;
    std::vector<PlatformEvent> seen_;
};

TEST_F(WaylandLive, TheCompositorIsThePrivateOneAndOffersTheCore)
{
    const WaylandGlobals& globals = platform_->GetConnectionForTesting()->GetGlobals();
    EXPECT_NE(globals.compositor, nullptr);
    EXPECT_NE(globals.wmBase, nullptr);
    EXPECT_NE(globals.shm, nullptr);
    EXPECT_TRUE(platform_->GetCapabilities().surfacePresentation);
    ASSERT_NE(platform_->GetDisplays(), nullptr);
    EXPECT_GE(platform_->GetDisplays()->GetDisplays().size(), 1u);
    const char* display = std::getenv("WAYLAND_DISPLAY");
    ASSERT_NE(display, nullptr);
    EXPECT_NE(std::string(display), "wayland-0") << "never the desktop's compositor";
}

TEST_F(WaylandLive, AWindowIsConfiguredMappedAndRepaintedWithoutAProtocolError)
{
    IPlatformWindow& window = MakeWindow();
    IPlatformSurfacePresenter& presenter = PresenterFor(window);
    for (int frame = 0; frame < 30; ++frame)
    {
        Present(presenter, window, static_cast<std::uint8_t>(frame * 8));
        Pump();
    }
    window.Sync();
    Pump();
    EXPECT_EQ(window.GetClientBounds().width, 800);
    EXPECT_TRUE(platform_->GetConnectionForTesting()->IsAlive());
}

TEST_F(WaylandLive, TheShellHonoursMaximizeAndFullscreenAtTheOutputsSize)
{
    IPlatformWindow& window = MakeWindow();
    IPlatformSurfacePresenter& presenter = PresenterFor(window);
    Present(presenter, window, 0x40);
    window.Sync();
    Pump();
    const std::vector<DisplayInfo> displays = platform_->GetDisplays()->GetDisplays();
    ASSERT_FALSE(displays.empty());

    window.Maximize();
    window.Sync();
    Pump();
    Present(presenter, window, 0x40);
    window.Sync();
    Pump();
    EXPECT_EQ(CountWindowEvents(window.GetId(), WindowEventKind::Maximized), 1);
    // A maximized window fills the work area: the output, less whatever panels the shell keeps.
    EXPECT_LE(window.GetClientBounds().width, displays.front().width);
    EXPECT_GT(window.GetClientBounds().width, 800);

    window.SetFullscreenMode(WindowFullscreenMode::BorderlessFullscreen);
    window.Sync();
    Pump();
    Present(presenter, window, 0x40);
    window.Sync();
    Pump();
    EXPECT_EQ(window.GetFullscreenMode(), WindowFullscreenMode::BorderlessFullscreen);
    EXPECT_EQ(window.GetClientBounds().width, displays.front().width);
    EXPECT_EQ(window.GetClientBounds().height, displays.front().height);

    window.SetFullscreenMode(WindowFullscreenMode::Windowed);
    window.Restore();
    window.Sync();
    Pump();
    Present(presenter, window, 0x40);
    window.Sync();
    Pump();
    EXPECT_EQ(window.GetFullscreenMode(), WindowFullscreenMode::Windowed);
}

TEST_F(WaylandLive, VsyncPacesThePresenterToTheCompositorsFrames)
{
    IPlatformWindow& window = MakeWindow();
    IPlatformSurfacePresenter& presenter = PresenterFor(window);
    presenter.SetVSync(true);
    Present(presenter, window, 0);
    const auto start = std::chrono::steady_clock::now();
    constexpr int kFrames = 30;
    for (int frame = 0; frame < kFrames; ++frame)
    {
        Present(presenter, window, static_cast<std::uint8_t>(frame));
        Pump();
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    // Paced by frame callbacks, not free-running: 30 frames at ~60 Hz take about half a second;
    // a presenter ignoring the callbacks takes milliseconds, and one waiting for callbacks that
    // never come hits the 100 ms bound every frame (3 s).
    EXPECT_GT(seconds, 0.2) << "the presenter did not wait for the compositor";
    EXPECT_LT(seconds, 2.5) << "the compositor's frame callbacks were not seen";
}

TEST_F(WaylandLive, HighDpiWindowsFollowTheOutputsScale)
{
    const char* configured = std::getenv("CNA_WAYLAND_TEST_SCALE");
    const int scale = configured != nullptr ? std::atoi(configured) : 1;
    WindowDescription description;
    description.highDpi = true;
    IPlatformWindow& window = MakeWindow(description);
    IPlatformSurfacePresenter& presenter = PresenterFor(window);
    // A compositor tells a surface its outputs once it is on screen.
    for (int frame = 0; frame < 5; ++frame)
    {
        Present(presenter, window, 0x80);
        window.Sync();
        Pump();
    }
    EXPECT_FLOAT_EQ(window.GetDisplayScale(), static_cast<float>(scale));
    EXPECT_EQ(window.GetPixelSize().width, 800 * scale);
    EXPECT_EQ(window.GetClientBounds().width, 800);
}

// --- OpenGL through EGL (WAYLAND-0080) -------------------------------------------------------------

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

TEST_F(WaylandLive, AnOpenGlContextRendersIntoTheWindowAndReadsBack)
{
    IPlatformGlContext* gl = platform_->GetGlContext();
    if (gl == nullptr)
    {
        GTEST_SKIP() << "no EGL with a Wayland platform here";
    }
    WindowDescription description;
    description.renderIntent = WindowRenderIntent::OpenGl;
    description.width = 320;
    description.height = 240;
    IPlatformWindow& window = MakeWindow(description);
    GlContextDescription wanted;
    GlContextHandle context = nullptr;
    try
    {
        context = gl->CreateContext(window.GetId(), wanted);
    }
    catch (const PlatformException& error)
    {
        GTEST_SKIP() << "no OpenGL 3.3 core context here: " << error.what();
    }
    ASSERT_NE(context, nullptr);
    gl->MakeCurrent(window.GetId(), context);
    const GlContextDescription actual = gl->GetContextAttributes(context);
    EXPECT_GE(actual.majorVersion * 10 + actual.minorVersion, 33);

    auto getString = reinterpret_cast<Gl::GetString>(gl->GetProcAddress("glGetString"));
    auto clearColor = reinterpret_cast<Gl::ClearColor>(gl->GetProcAddress("glClearColor"));
    auto clear = reinterpret_cast<Gl::Clear>(gl->GetProcAddress("glClear"));
    auto readPixels = reinterpret_cast<Gl::ReadPixels>(gl->GetProcAddress("glReadPixels"));
    auto viewport = reinterpret_cast<Gl::Viewport>(gl->GetProcAddress("glViewport"));
    auto finish = reinterpret_cast<Gl::Finish>(gl->GetProcAddress("glFinish"));
    ASSERT_TRUE(getString && clearColor && clear && readPixels && viewport && finish);
    const char* renderer = reinterpret_cast<const char*>(getString(Gl::kRenderer));
    const char* version = reinterpret_cast<const char*>(getString(Gl::kVersion));
    RecordProperty("GL_RENDERER", renderer != nullptr ? renderer : "");
    RecordProperty("GL_VERSION", version != nullptr ? version : "");
    std::printf("[ INFO     ] GL_RENDERER %s | GL_VERSION %s\n", renderer ? renderer : "?", version ? version : "?");

    for (int frame = 0; frame < 10; ++frame)
    {
        viewport(0, 0, 320, 240);
        clearColor(0.25f, 0.5f, 0.75f, 1.0f);
        clear(Gl::kColorBufferBit);
        finish();
        std::uint8_t pixel[4] = {};
        readPixels(160, 120, 1, 1, Gl::kRgba, Gl::kUnsignedByte, pixel);
        EXPECT_NEAR(pixel[0], 64, 2);
        EXPECT_NEAR(pixel[1], 128, 2);
        EXPECT_NEAR(pixel[2], 191, 2);
        gl->SwapBuffers(window.GetId());
        Pump();
    }

    // A resize reaches the EGL surface before the next frame is drawn.
    window.SetSize(400, 300);
    Pump();
    viewport(0, 0, 400, 300);
    clearColor(1.0f, 0.0f, 0.0f, 1.0f);
    clear(Gl::kColorBufferBit);
    finish();
    std::uint8_t corner[4] = {};
    readPixels(399, 299, 1, 1, Gl::kRgba, Gl::kUnsignedByte, corner);
    EXPECT_EQ(corner[0], 255) << "the default framebuffer did not grow with the window";
    gl->SwapBuffers(window.GetId());
    window.Sync();
    Pump();

    EXPECT_TRUE(gl->SetSwapInterval(1));
    gl->MakeCurrent(0, nullptr);
    gl->DestroyContext(context);
}

// --- Vulkan (WAYLAND-0081) -----------------------------------------------------------------------

namespace Vk {
    using Instance = void*;
    using PhysicalDevice = void*;
    using Result = int;
    struct ApplicationInfo
    {
        int sType;
        const void* next;
        const char* applicationName;
        std::uint32_t applicationVersion;
        const char* engineName;
        std::uint32_t engineVersion;
        std::uint32_t apiVersion;
    };
    struct InstanceCreateInfo
    {
        int sType;
        const void* next;
        std::uint32_t flags;
        const ApplicationInfo* applicationInfo;
        std::uint32_t enabledLayerCount;
        const char* const* enabledLayerNames;
        std::uint32_t enabledExtensionCount;
        const char* const* enabledExtensionNames;
    };
    using GetInstanceProcAddr = void* (*)(Instance, const char*);
    using CreateInstance = Result (*)(const InstanceCreateInfo*, const void*, Instance*);
    using DestroyInstance = void (*)(Instance, const void*);
    using EnumeratePhysicalDevices = Result (*)(Instance, std::uint32_t*, PhysicalDevice*);
    using GetSurfaceSupport = Result (*)(PhysicalDevice, std::uint32_t, std::uint64_t, std::uint32_t*);
}

TEST_F(WaylandLive, AVulkanSurfaceIsOneAPhysicalDeviceCanPresentTo)
{
    IPlatformVulkanSurface* vulkan = platform_->GetVulkanSurface();
    if (vulkan == nullptr)
    {
        GTEST_SKIP() << "this build has no Vulkan surface service";
    }
    void* loader = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (loader == nullptr)
    {
        GTEST_SKIP() << "no Vulkan loader: " << dlerror();
    }
    auto getProc = reinterpret_cast<Vk::GetInstanceProcAddr>(dlsym(loader, "vkGetInstanceProcAddr"));
    ASSERT_NE(getProc, nullptr);
    const std::vector<std::string> extensions = vulkan->GetInstanceExtensions();
    ASSERT_EQ(extensions.size(), 2u);
    EXPECT_EQ(extensions[0], "VK_KHR_surface");
    EXPECT_EQ(extensions[1], "VK_KHR_wayland_surface");
    std::vector<const char*> names;
    for (const std::string& name : extensions)
    {
        names.push_back(name.c_str());
    }
    Vk::ApplicationInfo application{0, nullptr, "CNA Wayland live test", 1, "CNA", 1, 1u << 22};
    Vk::InstanceCreateInfo info{1, nullptr, 0, &application, 0, nullptr, static_cast<std::uint32_t>(names.size()), names.data()};
    Vk::Instance instance = nullptr;
    auto createInstance = reinterpret_cast<Vk::CreateInstance>(getProc(nullptr, "vkCreateInstance"));
    if (createInstance == nullptr || createInstance(&info, nullptr, &instance) != 0 || instance == nullptr)
    {
        dlclose(loader);
        GTEST_SKIP() << "no Vulkan driver implements VK_KHR_wayland_surface here";
    }
    IPlatformWindow& window = MakeWindow();
    const VulkanSurfaceHandle surface = vulkan->CreateSurface(instance, window.GetId());
    EXPECT_NE(surface, VulkanSurfaceHandle{});
    auto enumerate = reinterpret_cast<Vk::EnumeratePhysicalDevices>(getProc(instance, "vkEnumeratePhysicalDevices"));
    auto support = reinterpret_cast<Vk::GetSurfaceSupport>(getProc(instance, "vkGetPhysicalDeviceSurfaceSupportKHR"));
    std::uint32_t count = 0;
    enumerate(instance, &count, nullptr);
    std::vector<Vk::PhysicalDevice> devices(count);
    enumerate(instance, &count, devices.data());
    bool presentable = false;
    for (Vk::PhysicalDevice device : devices)
    {
        std::uint32_t supported = 0;
        if (support(device, 0, static_cast<std::uint64_t>(surface), &supported) == 0 && supported != 0)
        {
            presentable = true;
        }
    }
    EXPECT_TRUE(presentable || devices.empty()) << "no device can present to the window's surface";
    vulkan->DestroySurface(instance, surface);
    reinterpret_cast<Vk::DestroyInstance>(getProc(instance, "vkDestroyInstance"))(instance, nullptr);
    dlclose(loader);
}

} // namespace
