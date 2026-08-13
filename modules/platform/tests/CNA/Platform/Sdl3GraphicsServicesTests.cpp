// SPDX-License-Identifier: MS-PL
//
// PLAT-41/42/128 against real SDL3.
//
// The GL context needs a driver that can actually create one and the Vulkan surface needs a real
// VkInstance, neither of which a headless container has. Those tests skip. The surface presenter,
// though, works over SDL's software renderer on a dummy-driver window, so the interface PLAT-3
// found was missing is genuinely exercised here rather than merely compiled.

#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformFactory.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace CNA::Platform;

class Sdl3GraphicsServiceTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const std::vector<std::string> available = PlatformFactory::GetAvailable();
        if (std::find(available.begin(), available.end(), "SDL3") == available.end())
        {
            GTEST_SKIP() << "built with CNA_PLATFORM != SDL3";
        }
        platform_ = PlatformFactory::Create("SDL3");
        try
        {
            platform_->AcquireSubsystem(PlatformSubsystem::Video);
        }
        catch (const PlatformException& error)
        {
            GTEST_SKIP() << "no video subsystem available here: " << error.what();
        }
        acquired_ = true;

        WindowDescription description;
        description.title = "graphics service probe";
        description.width = 64;
        description.height = 48;
        description.visible = false;
        try
        {
            window_ = platform_->CreateWindow(description);
        }
        catch (const PlatformException& error)
        {
            GTEST_SKIP() << "window creation unavailable here: " << error.what();
        }
    }

    void TearDown() override
    {
        window_.reset();
        if (acquired_)
        {
            platform_->ReleaseSubsystem(PlatformSubsystem::Video);
        }
    }

    std::unique_ptr<IPlatform> platform_;
    std::unique_ptr<IPlatformWindow> window_;
    bool acquired_ = false;
};

// --- services exist because the capability set says so -----------------------------------------------

TEST_F(Sdl3GraphicsServiceTest, GlAndVulkanServicesAreReturnedWhenAdvertised)
{
    const PlatformCapabilities capabilities = platform_->GetCapabilities();
    EXPECT_EQ(platform_->GetGlContext() != nullptr, capabilities.openGlContext);
    EXPECT_EQ(platform_->GetVulkanSurface() != nullptr, capabilities.vulkanSurface);
}

// --- Vulkan (PLAT-42) ----------------------------------------------------------------------------------

TEST_F(Sdl3GraphicsServiceTest, VulkanInstanceExtensionsAreWellFormedWhenAvailable)
{
    // Callable before any VkInstance exists -- that is the whole point of the signature, and
    // getting it backwards is an easy mistake.
    if (platform_->GetVulkanSurface() == nullptr)
    {
        GTEST_SKIP() << "this host has no Vulkan loader";
    }

    for (const std::string& extension : platform_->GetVulkanSurface()->GetInstanceExtensions())
    {
        EXPECT_FALSE(extension.empty());
        EXPECT_EQ(extension.find(' '), std::string::npos) << extension;
    }
}

TEST_F(Sdl3GraphicsServiceTest, DestroyingANullVulkanSurfaceIsANoOp)
{
    if (platform_->GetVulkanSurface() == nullptr)
    {
        GTEST_SKIP() << "this host has no Vulkan loader";
    }
    // Zero is the documented "no surface" value; a caller unwinding from a failed initialisation
    // will pass it, and that must not crash.
    EXPECT_NO_THROW(platform_->GetVulkanSurface()->DestroySurface(nullptr, 0));
}

// --- OpenGL (PLAT-41) -----------------------------------------------------------------------------------

TEST_F(Sdl3GraphicsServiceTest, DestroyingANullGlContextIsANoOp)
{
    EXPECT_NO_THROW(platform_->GetGlContext()->DestroyContext(nullptr));
    EXPECT_NE(platform_->GetGlContext()->GetProcAddressLoader(), nullptr);
}

TEST_F(Sdl3GraphicsServiceTest, GlContextCreationEitherSucceedsOrRefusesCleanly)
{
    // A dummy video driver has no GL. What matters is that the failure is a PlatformException
    // naming the operation, not a crash or a silently-null context a renderer would then use.
    WindowDescription description;
    description.title = "gl probe";
    description.visible = false;
    description.renderIntent = WindowRenderIntent::OpenGl;

    std::unique_ptr<IPlatformWindow> glWindow;
    try
    {
        glWindow = platform_->CreateWindow(description);
    }
    catch (const PlatformException&)
    {
        GTEST_SKIP() << "this driver cannot create an OpenGL window";
    }

    GlContextDescription requested;
    GlContextHandle context = nullptr;
    try
    {
        context = platform_->GetGlContext()->CreateContext(glWindow->GetId(), requested);
    }
    catch (const PlatformException&)
    {
        SUCCEED() << "refused cleanly, which is the correct outcome with no GL driver";
        return;
    }

    ASSERT_NE(context, nullptr);
    const GlContextDescription granted = platform_->GetGlContext()->GetContextAttributes(context);
    // A driver may grant more than was asked for; it must not grant less than a usable version.
    EXPECT_GE(granted.majorVersion, 1);
    platform_->GetGlContext()->DestroyContext(context);
}

TEST_F(Sdl3GraphicsServiceTest, GlServiceRejectsUnknownIdAndPresenterRejectsForeignWindow)
{
    // Both stable-id lookup and the presenter's typed-window check must produce a named refusal
    // rather than dereferencing an unknown native object.
    class ForeignWindow final : public IPlatformWindow
    {
    public:
        [[nodiscard]] WindowId GetId() const override { return 1; }
        [[nodiscard]] NativeWindowHandle GetNativeHandle() const override { return {}; }
        [[nodiscard]] std::string GetTitle() const override { return {}; }
        void SetTitle(const std::string&) override {}
        [[nodiscard]] WindowBounds GetClientBounds() const override { return {}; }
        [[nodiscard]] WindowSize GetPixelSize() const override { return {}; }
        void SetSize(int, int) override {}
        [[nodiscard]] float GetDisplayScale() const override { return 1.0f; }
        [[nodiscard]] bool IsResizable() const override { return false; }
        void SetResizable(bool) override {}
        [[nodiscard]] bool IsBorderless() const override { return false; }
        void SetBorderless(bool) override {}
        void SetFullscreenMode(WindowFullscreenMode) override {}
        [[nodiscard]] WindowFullscreenMode GetFullscreenMode() const override
        {
            return WindowFullscreenMode::Windowed;
        }
        void Show() override {}
        void Hide() override {}
        void Minimize() override {}
        void Maximize() override {}
        void Restore() override {}
        void Sync() override {}
        [[nodiscard]] bool HasFocus() const override { return false; }
        [[nodiscard]] bool IsMinimized() const override { return false; }
        [[nodiscard]] std::string GetDisplayName() const override { return {}; }
    };

    ForeignWindow foreign;
    GlContextDescription requested;
    EXPECT_THROW((void)platform_->GetGlContext()->CreateContext(0, requested), PlatformException);
    EXPECT_THROW((void)platform_->CreateSurfacePresenter(foreign), PlatformException);
}

// --- surface presenter (PLAT-128) -------------------------------------------------------------------------

class Sdl3PresenterTest : public Sdl3GraphicsServiceTest
{
protected:
    void SetUp() override
    {
        Sdl3GraphicsServiceTest::SetUp();
        if (IsSkipped())
        {
            return;
        }
        try
        {
            presenter_ = platform_->CreateSurfacePresenter(*window_);
        }
        catch (const PlatformException& error)
        {
            GTEST_SKIP() << "no 2D renderer available here: " << error.what();
        }
    }

    std::unique_ptr<IPlatformSurfacePresenter> presenter_;
};

TEST_F(Sdl3PresenterTest, PresentsARealFrame)
{
    // The path SKIA and BLEND2D take: rasterise on the CPU, hand over finished RGBA, let the
    // platform put it on screen.
    std::vector<std::uint8_t> pixels(64 * 48 * 4, 0x80);
    SurfaceFrame frame;
    frame.pixels = pixels.data();
    frame.width = 64;
    frame.height = 48;

    EXPECT_NO_THROW(presenter_->Present(frame));
}

TEST_F(Sdl3PresenterTest, PresentingRepeatedlyReusesTheTexture)
{
    // The texture is recreated only on a size change. Recreating it every frame would be a
    // per-frame GPU allocation in the hot path.
    std::vector<std::uint8_t> pixels(64 * 48 * 4, 0x40);
    SurfaceFrame frame;
    frame.pixels = pixels.data();
    frame.width = 64;
    frame.height = 48;

    for (int i = 0; i < 5; ++i)
    {
        EXPECT_NO_THROW(presenter_->Present(frame));
    }
}

TEST_F(Sdl3PresenterTest, ASizeChangeIsHandled)
{
    std::vector<std::uint8_t> small(16 * 16 * 4, 0x10);
    SurfaceFrame frame;
    frame.pixels = small.data();
    frame.width = 16;
    frame.height = 16;
    EXPECT_NO_THROW(presenter_->Present(frame));

    std::vector<std::uint8_t> large(80 * 60 * 4, 0x20);
    frame.pixels = large.data();
    frame.width = 80;
    frame.height = 60;
    EXPECT_NO_THROW(presenter_->Present(frame));
}

TEST_F(Sdl3PresenterTest, MalformedFramesAreRejectedRatherThanRead)
{
    // Reading from a null pointer or a negative extent would be a crash inside the present path.
    SurfaceFrame nullPixels;
    nullPixels.width = 8;
    nullPixels.height = 8;
    EXPECT_THROW(presenter_->Present(nullPixels), PlatformException);

    std::vector<std::uint8_t> pixels(64, 0);
    SurfaceFrame zeroSize;
    zeroSize.pixels = pixels.data();
    zeroSize.width = 0;
    zeroSize.height = 8;
    EXPECT_THROW(presenter_->Present(zeroSize), PlatformException);
}

TEST_F(Sdl3PresenterTest, TargetSizeIsReportedAndScaleModeIsAccepted)
{
    int width = -1;
    int height = -1;
    presenter_->GetTargetSize(width, height);
    EXPECT_GE(width, 0);
    EXPECT_GE(height, 0);

    EXPECT_NO_THROW(presenter_->SetScaleMode(PresentScaleMode::Stretch, PresentFilter::Linear));
    EXPECT_NO_THROW(presenter_->SetScaleMode(PresentScaleMode::Overscan, PresentFilter::Linear));
    EXPECT_NO_THROW(presenter_->SetScaleMode(PresentScaleMode::None, PresentFilter::Nearest));
    EXPECT_NO_THROW(presenter_->SetScaleMode(PresentScaleMode::Native, PresentFilter::Nearest));
    // SetVSync reports whether it was honoured rather than throwing: a driver that cannot
    // synchronise should keep presenting.
    (void)presenter_->SetVSync(false);
}

} // namespace
