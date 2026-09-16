// SPDX-License-Identifier: MS-PL
//
// plans/plan_win32.md WIN32-0057..0059: the WGL context, the Vulkan surface and the GDI presenter.
//
// These three are how a renderer that is NOT DirectX reaches a Win32 window. Each is
// capability-gated, and the point of most of what follows is that the gate and the service agree:
// a capability reported true must produce a working service, and a false one must refuse with the
// capability named rather than return something inert.

#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformFactory.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace {

using namespace CNA::Platform;

class Win32GraphicsServices : public ::testing::Test
{
protected:
    void SetUp() override
    {
        platform_ = PlatformFactory::Create("Win32");
        ASSERT_NE(platform_, nullptr);
        platform_->AcquireSubsystem(PlatformSubsystem::Video);
        capabilities_ = platform_->GetCapabilities();
    }

    std::unique_ptr<IPlatformWindow> CreateWindow(const WindowRenderIntent intent,
                                                  const int width = 320, const int height = 240)
    {
        WindowDescription description;
        description.title = "graphics services";
        description.width = width;
        description.height = height;
        description.visible = false;
        description.renderIntent = intent;
        return platform_->CreateWindow(description);
    }

    std::unique_ptr<IPlatform> platform_;
    PlatformCapabilities capabilities_;
};

// --- OpenGL ----------------------------------------------------------------------------------

TEST_F(Win32GraphicsServices, GlContextServiceExistsBecauseTheCapabilityIsTrue)
{
    ASSERT_TRUE(capabilities_.openGlContext);
    EXPECT_NE(platform_->GetGlContext(), nullptr);
}

TEST_F(Win32GraphicsServices, GlContextRefusesAWindowThatDoesNotExist)
{
    IPlatformGlContext* const gl = platform_->GetGlContext();
    ASSERT_NE(gl, nullptr);
    EXPECT_THROW((void) gl->CreateContext(9999, GlContextDescription{}), PlatformException);
    EXPECT_THROW(gl->MakeCurrent(9999, reinterpret_cast<GlContextHandle>(1)), PlatformException);
}

TEST_F(Win32GraphicsServices, DestroyingANullOrForeignContextIsANoOp)
{
    // Cleanup legitimately runs after a partial initialization, so neither case may fault.
    IPlatformGlContext* const gl = platform_->GetGlContext();
    ASSERT_NE(gl, nullptr);
    EXPECT_NO_THROW(gl->DestroyContext(nullptr));
    EXPECT_NO_THROW(gl->DestroyContext(reinterpret_cast<GlContextHandle>(0xBADC0DE)));
}

TEST_F(Win32GraphicsServices, UnbindingWithoutACurrentContextSucceeds)
{
    IPlatformGlContext* const gl = platform_->GetGlContext();
    ASSERT_NE(gl, nullptr);
    EXPECT_NO_THROW(gl->MakeCurrent(0, nullptr));
    EXPECT_EQ(gl->GetCurrentBinding().context, nullptr);
    EXPECT_EQ(gl->GetCurrentBinding().window, 0u);
}

TEST_F(Win32GraphicsServices, TheProcAddressLoaderIsAlwaysUsable)
{
    // GL helper libraries bootstrap from a plain function pointer. A null loader would make every
    // one of them fail at a point far from this service.
    IPlatformGlContext* const gl = platform_->GetGlContext();
    ASSERT_NE(gl, nullptr);
    EXPECT_NE(gl->GetProcAddressLoader(), nullptr);
    // An entry point nothing exports must resolve to null rather than to a stray value a caller
    // would then call.
    EXPECT_EQ(gl->GetProcAddress("glThisEntryPointDoesNotExist"), nullptr);
}

TEST_F(Win32GraphicsServices, AContextEitherIsCreatedAndUsableOrFailsExplicitly)
{
    // Wine and a headless session may have no usable OpenGL driver at all, which is an
    // environment limitation rather than a defect. What the contract forbids is a null context
    // handed back as success.
    IPlatformGlContext* const gl = platform_->GetGlContext();
    ASSERT_NE(gl, nullptr);

    const std::unique_ptr<IPlatformWindow> window = CreateWindow(WindowRenderIntent::OpenGl);
    ASSERT_NE(window, nullptr);

    GlContextDescription wanted;
    wanted.majorVersion = 3;
    wanted.minorVersion = 3;
    wanted.profile = GlProfile::Core;

    GlContextHandle context = nullptr;
    try
    {
        context = gl->CreateContext(window->GetId(), wanted);
    }
    catch (const PlatformException& error)
    {
        GTEST_SKIP() << "no usable OpenGL driver here: " << error.what();
    }

    ASSERT_NE(context, nullptr) << "success must mean a real context";
    gl->MakeCurrent(window->GetId(), context);
    EXPECT_EQ(gl->GetCurrentBinding().context, context);
    EXPECT_EQ(gl->GetCurrentBinding().window, window->GetId());

    // The granted attributes are queryable rather than assumed: a driver may give more or less
    // than was asked for, and a renderer that assumes otherwise breaks on an unfamiliar one.
    const GlContextDescription granted = gl->GetContextAttributes(context);
    EXPECT_GT(granted.majorVersion, 0);

    EXPECT_NO_THROW(gl->SwapBuffers(window->GetId()));
    // SetSwapInterval reports whether it was applied rather than throwing: a driver that declines
    // vsync is an ordinary answer a caller branches on.
    EXPECT_NO_THROW((void) gl->SetSwapInterval(1));

    gl->MakeCurrent(window->GetId(), nullptr);
    gl->DestroyContext(context);
    EXPECT_EQ(gl->GetCurrentBinding().context, nullptr);
}

// plans/plan_windows_portability_closeout.md WINCLOSE-0002: the WGL lifetime audit, as a test.
//
// AContextEitherIsCreatedAndUsableOrFailsExplicitly passes alone and access-violates in a long
// run, and the question that had never been answered was which side of the boundary the fault is
// on. This is CNA's side of it: every ownership path the service has -- repeated create/destroy,
// unbind before destroy, destroy while current, two contexts on one window, a context per window,
// and a window destroyed before the context that was made current on it -- exercised in one
// process so that a leaked HGLRC, a stale current context or a released DC shows up here rather
// than as a fault in somebody else's test much later.
//
// Skips rather than fails without a usable driver, like its neighbour above: a host with no
// OpenGL is an environment limitation, and this test is about lifetime, not availability.
TEST_F(Win32GraphicsServices, ContextLifetimeSurvivesEveryOwnershipPathWithoutLeavingStaleState)
{
    IPlatformGlContext* const gl = platform_->GetGlContext();
    ASSERT_NE(gl, nullptr);

    GlContextDescription wanted;
    wanted.majorVersion = 3;
    wanted.minorVersion = 3;
    wanted.profile = GlProfile::Core;

    const auto tryCreate = [&](IPlatformWindow& window) -> GlContextHandle
    {
        try
        {
            return gl->CreateContext(window.GetId(), wanted);
        }
        catch (const PlatformException&)
        {
            return nullptr;
        }
    };

    {
        const std::unique_ptr<IPlatformWindow> probeWindow = CreateWindow(WindowRenderIntent::OpenGl);
        ASSERT_NE(probeWindow, nullptr);
        if (tryCreate(*probeWindow) == nullptr)
            GTEST_SKIP() << "no usable OpenGL driver here";
    }

    // 1. Repeated create/destroy on a fresh window each time. A context or a DC kept back by any
    //    one round shows up as a refusal in a later one.
    for (int round = 0; round < 8; ++round)
    {
        const std::unique_ptr<IPlatformWindow> window = CreateWindow(WindowRenderIntent::OpenGl);
        ASSERT_NE(window, nullptr) << "round " << round;
        const GlContextHandle context = tryCreate(*window);
        ASSERT_NE(context, nullptr) << "a context could not be created on round " << round
                                    << "; something earlier did not give one back";
        gl->MakeCurrent(window->GetId(), context);
        EXPECT_EQ(gl->GetCurrentBinding().context, context);
        gl->MakeCurrent(window->GetId(), nullptr);
        gl->DestroyContext(context);
        EXPECT_EQ(gl->GetCurrentBinding().context, nullptr) << "round " << round;
    }

    // 2. Destroyed while still current. The service has to clear the binding itself, because
    //    wglDeleteContext refuses a context that is current in the calling thread -- and a caller
    //    tearing down after an error does exactly this.
    {
        const std::unique_ptr<IPlatformWindow> window = CreateWindow(WindowRenderIntent::OpenGl);
        ASSERT_NE(window, nullptr);
        const GlContextHandle context = tryCreate(*window);
        ASSERT_NE(context, nullptr);
        gl->MakeCurrent(window->GetId(), context);
        ASSERT_EQ(gl->GetCurrentBinding().context, context);
        EXPECT_NO_THROW(gl->DestroyContext(context));
        EXPECT_EQ(gl->GetCurrentBinding().context, nullptr)
            << "a destroyed context is still reported as current";
        // Unbinding again must stay a no-op rather than acting on the handle just deleted.
        EXPECT_NO_THROW(gl->MakeCurrent(0, nullptr));
    }

    // 3. Two contexts on ONE window. The second SetPixelFormat on the same DC is the case the
    //    service documents as already-set-and-matching rather than an error.
    {
        const std::unique_ptr<IPlatformWindow> window = CreateWindow(WindowRenderIntent::OpenGl);
        ASSERT_NE(window, nullptr);
        const GlContextHandle first = tryCreate(*window);
        ASSERT_NE(first, nullptr);
        const GlContextHandle second = tryCreate(*window);
        ASSERT_NE(second, nullptr) << "a second context on the same window was refused";
        EXPECT_NE(first, second);
        gl->MakeCurrent(window->GetId(), first);
        gl->MakeCurrent(window->GetId(), second);
        EXPECT_EQ(gl->GetCurrentBinding().context, second);
        gl->MakeCurrent(window->GetId(), nullptr);
        gl->DestroyContext(second);
        gl->DestroyContext(first);
        EXPECT_EQ(gl->GetCurrentBinding().context, nullptr);
    }

    // 4. Two windows, a context each, both alive at once, and made current in turn.
    {
        const std::unique_ptr<IPlatformWindow> windowA = CreateWindow(WindowRenderIntent::OpenGl);
        const std::unique_ptr<IPlatformWindow> windowB = CreateWindow(WindowRenderIntent::OpenGl);
        ASSERT_NE(windowA, nullptr);
        ASSERT_NE(windowB, nullptr);
        const GlContextHandle contextA = tryCreate(*windowA);
        const GlContextHandle contextB = tryCreate(*windowB);
        ASSERT_NE(contextA, nullptr);
        ASSERT_NE(contextB, nullptr);
        gl->MakeCurrent(windowA->GetId(), contextA);
        EXPECT_EQ(gl->GetCurrentBinding().window, windowA->GetId());
        gl->MakeCurrent(windowB->GetId(), contextB);
        EXPECT_EQ(gl->GetCurrentBinding().window, windowB->GetId());
        gl->MakeCurrent(windowB->GetId(), nullptr);
        gl->DestroyContext(contextA);
        gl->DestroyContext(contextB);
        EXPECT_EQ(gl->GetCurrentBinding().context, nullptr);
    }

    // 5. The window destroyed FIRST, while its context is still current and still owned. This is
    //    the ordering a crashing teardown takes, and the one that leaves a current context whose
    //    device context belongs to a window that no longer exists. Nothing here may fault, and
    //    nothing may be left current for whatever runs next.
    {
        GlContextHandle context = nullptr;
        {
            const std::unique_ptr<IPlatformWindow> window =
                CreateWindow(WindowRenderIntent::OpenGl);
            ASSERT_NE(window, nullptr);
            context = tryCreate(*window);
            ASSERT_NE(context, nullptr);
            gl->MakeCurrent(window->GetId(), context);
            ASSERT_EQ(gl->GetCurrentBinding().context, context);
        }   // the window is destroyed here, with the context still current on it

        EXPECT_NO_THROW(gl->DestroyContext(context));
        EXPECT_EQ(gl->GetCurrentBinding().context, nullptr)
            << "a context outlived its window and stayed current; the next GL call in this "
               "process would be made against a device context that no longer exists";
    }

    // 6. After all of that, an ordinary context still works. If any round above kept a handle,
    //    this is where the driver says so.
    {
        const std::unique_ptr<IPlatformWindow> window = CreateWindow(WindowRenderIntent::OpenGl);
        ASSERT_NE(window, nullptr);
        const GlContextHandle context = tryCreate(*window);
        ASSERT_NE(context, nullptr) << "the service could not create a context after exercising "
                                       "every ownership path; something was not given back";
        gl->MakeCurrent(window->GetId(), context);
        EXPECT_NO_THROW(gl->SwapBuffers(window->GetId()));
        gl->MakeCurrent(window->GetId(), nullptr);
        gl->DestroyContext(context);
    }

    EXPECT_EQ(gl->GetCurrentBinding().context, nullptr);
    EXPECT_EQ(gl->GetCurrentBinding().window, 0u);
}

// --- Vulkan ----------------------------------------------------------------------------------

TEST_F(Win32GraphicsServices, VulkanServiceAndCapabilityAgree)
{
    // Decided by the host, not by this code: without a loader there is genuinely no surface to
    // create. A renderer checks the capability and then dereferences the service, so the two must
    // never disagree.
    EXPECT_EQ(platform_->GetVulkanSurface() != nullptr, capabilities_.vulkanSurface);
}

TEST_F(Win32GraphicsServices, VulkanInstanceExtensionsNameTheWin32Surface)
{
    IPlatformVulkanSurface* const vulkan = platform_->GetVulkanSurface();
    if (vulkan == nullptr)
        GTEST_SKIP() << "no Vulkan loader on this host";

    const std::vector<std::string> extensions = vulkan->GetInstanceExtensions();
    EXPECT_NE(std::find(extensions.begin(), extensions.end(), "VK_KHR_surface"), extensions.end());
    EXPECT_NE(std::find(extensions.begin(), extensions.end(), "VK_KHR_win32_surface"),
              extensions.end());
}

TEST_F(Win32GraphicsServices, VulkanSurfaceCreationRefusesAMissingInstanceOrWindow)
{
    IPlatformVulkanSurface* const vulkan = platform_->GetVulkanSurface();
    if (vulkan == nullptr)
        GTEST_SKIP() << "no Vulkan loader on this host";

    EXPECT_THROW((void) vulkan->CreateSurface(nullptr, 1), PlatformException);
    // Destroying a zero surface is documented as a no-op, so cleanup after a failed creation is
    // safe to write unconditionally.
    EXPECT_NO_THROW(vulkan->DestroySurface(nullptr, 0));
}

// --- surface presentation ------------------------------------------------------------------------

TEST_F(Win32GraphicsServices, ThePresenterTargetsTheWindowsDrawableSize)
{
    ASSERT_TRUE(capabilities_.surfacePresentation);
    const std::unique_ptr<IPlatformWindow> window = CreateWindow(WindowRenderIntent::None, 320, 240);
    ASSERT_NE(window, nullptr);
    window->Sync();

    const std::unique_ptr<IPlatformSurfacePresenter> presenter =
        platform_->CreateSurfacePresenter(*window);
    ASSERT_NE(presenter, nullptr);

    int width = 0;
    int height = 0;
    presenter->GetTargetSize(width, height);
    EXPECT_EQ(width, 320);
    EXPECT_EQ(height, 240);

    // The target tracks the window and can change between presents, which is why a caller reads
    // it per frame rather than caching it.
    window->SetSize(400, 300);
    window->Sync();
    presenter->GetTargetSize(width, height);
    EXPECT_EQ(width, 400);
    EXPECT_EQ(height, 300);
}

TEST_F(Win32GraphicsServices, VSyncIsReportedAsDeclinedRatherThanSilentlyIgnored)
{
    // GDI blits are not synchronised to vertical blank and there is no supported way to make them
    // so. Returning false is the honest answer; returning true would have a caller believe its
    // frame pacing was handled.
    const std::unique_ptr<IPlatformWindow> window = CreateWindow(WindowRenderIntent::None);
    ASSERT_NE(window, nullptr);
    const std::unique_ptr<IPlatformSurfacePresenter> presenter =
        platform_->CreateSurfacePresenter(*window);
    ASSERT_NE(presenter, nullptr);
    EXPECT_FALSE(presenter->SetVSync(true));
}

TEST_F(Win32GraphicsServices, EveryScaleModeAndFilterIsAcceptedAndPresents)
{
    const std::unique_ptr<IPlatformWindow> window = CreateWindow(WindowRenderIntent::None, 320, 240);
    ASSERT_NE(window, nullptr);
    window->Sync();
    const std::unique_ptr<IPlatformSurfacePresenter> presenter =
        platform_->CreateSurfacePresenter(*window);
    ASSERT_NE(presenter, nullptr);

    // A deliberately different aspect ratio from the window, so letterbox and overscan actually
    // have to compute something rather than coinciding with stretch.
    constexpr int kWidth = 64;
    constexpr int kHeight = 16;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(kWidth) * kHeight * 4, 0x40);

    SurfaceFrame frame;
    frame.pixels = pixels.data();
    frame.width = kWidth;
    frame.height = kHeight;

    for (const PresentScaleMode mode :
         {PresentScaleMode::Stretch, PresentScaleMode::Letterbox, PresentScaleMode::Overscan,
          PresentScaleMode::None, PresentScaleMode::Native})
    {
        for (const PresentFilter filter : {PresentFilter::Nearest, PresentFilter::Linear})
        {
            presenter->SetScaleMode(mode, filter);
            EXPECT_NO_THROW(presenter->Present(frame))
                << "mode " << static_cast<int>(mode) << " filter " << static_cast<int>(filter);
        }
    }
}

TEST_F(Win32GraphicsServices, PresentAcceptsAStridedSubRectangle)
{
    // A non-zero stride lets a rasteriser present part of a larger buffer without copying it
    // first. Ignoring the stride reads the wrong row and produces a sheared image.
    const std::unique_ptr<IPlatformWindow> window = CreateWindow(WindowRenderIntent::None);
    ASSERT_NE(window, nullptr);
    window->Sync();
    const std::unique_ptr<IPlatformSurfacePresenter> presenter =
        platform_->CreateSurfacePresenter(*window);
    ASSERT_NE(presenter, nullptr);

    constexpr int kBufferWidth = 64;
    constexpr int kHeight = 8;
    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(kBufferWidth) * kHeight * 4, 0x80);

    SurfaceFrame frame;
    frame.pixels = pixels.data();
    frame.width = 32;
    frame.height = kHeight;
    frame.strideBytes = kBufferWidth * 4;
    EXPECT_NO_THROW(presenter->Present(frame));
}

TEST_F(Win32GraphicsServices, MalformedFramesAreRefusedBeforeAnyPixelIsRead)
{
    const std::unique_ptr<IPlatformWindow> window = CreateWindow(WindowRenderIntent::None);
    ASSERT_NE(window, nullptr);
    window->Sync();
    const std::unique_ptr<IPlatformSurfacePresenter> presenter =
        platform_->CreateSurfacePresenter(*window);
    ASSERT_NE(presenter, nullptr);

    std::vector<std::uint8_t> pixels(64 * 8 * 4, 0);

    SurfaceFrame noPixels;
    noPixels.width = 8;
    noPixels.height = 8;
    EXPECT_THROW(presenter->Present(noPixels), PlatformException);

    SurfaceFrame noArea;
    noArea.pixels = pixels.data();
    noArea.width = 0;
    noArea.height = 8;
    EXPECT_THROW(presenter->Present(noArea), PlatformException);

    SurfaceFrame negativeStride;
    negativeStride.pixels = pixels.data();
    negativeStride.width = 8;
    negativeStride.height = 8;
    negativeStride.strideBytes = -32;
    EXPECT_THROW(presenter->Present(negativeStride), PlatformException);

    SurfaceFrame shortStride;
    shortStride.pixels = pixels.data();
    shortStride.width = 8;
    shortStride.height = 8;
    shortStride.strideBytes = 16; // shorter than one 8-pixel RGBA row
    EXPECT_THROW(presenter->Present(shortStride), PlatformException);
}

TEST_F(Win32GraphicsServices, RepeatedPresentsReuseTheConversionBuffer)
{
    // The conversion from the contract's RGBA to a Windows BGRA DIB is per pixel, so the buffer
    // behind it must be reused rather than reallocated -- the platform boundary is outside the
    // per-frame allocation budget.
    const std::unique_ptr<IPlatformWindow> window = CreateWindow(WindowRenderIntent::None);
    ASSERT_NE(window, nullptr);
    window->Sync();
    const std::unique_ptr<IPlatformSurfacePresenter> presenter =
        platform_->CreateSurfacePresenter(*window);
    ASSERT_NE(presenter, nullptr);

    std::vector<std::uint8_t> pixels(32 * 32 * 4, 0x20);
    SurfaceFrame frame;
    frame.pixels = pixels.data();
    frame.width = 32;
    frame.height = 32;
    for (int repeat = 0; repeat < 30; ++repeat)
        EXPECT_NO_THROW(presenter->Present(frame));
}

} // namespace
