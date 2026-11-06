// SPDX-License-Identifier: MS-PL
//
// PLAT-41/128 against real SDL3.
//
// The GL context needs a driver that can actually create one, which a headless container does not
// have. Those tests skip. The surface presenter,
// though, works over SDL's native framebuffer on a dummy-driver window, so the interface PLAT-3
// found was missing is genuinely exercised here rather than merely compiled.

#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformFactory.hpp"

#include <gtest/gtest.h>
#include "../../../src/Sdl3/Sdl3Window.hpp"
#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <limits>
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

TEST_F(Sdl3GraphicsServiceTest, GlServiceIsReturnedWhenAdvertised)
{
    const PlatformCapabilities capabilities = platform_->GetCapabilities();
    EXPECT_EQ(platform_->GetGlContext() != nullptr, capabilities.openGlContext);
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
            GTEST_SKIP() << "no window surface available here: " << error.what();
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

TEST_F(Sdl3PresenterTest, RepeatedFramesRemainPresentable)
{
    // Retain the original repeated-call smoke; the region and scale-mode cases below
    // independently check the resulting physical pixels.
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

    SurfaceFrame negativeStride;
    negativeStride.pixels = pixels.data();
    negativeStride.width = 4;
    negativeStride.height = 4;
    negativeStride.strideBytes = -4;
    EXPECT_THROW(presenter_->Present(negativeStride), PlatformException);

    SurfaceFrame shortStride = negativeStride;
    shortStride.strideBytes = shortStride.width * 4 - 1;
    EXPECT_THROW(presenter_->Present(shortStride), PlatformException);

    SurfaceFrame overflowingRow = negativeStride;
    overflowingRow.width = std::numeric_limits<int>::max();
    overflowingRow.strideBytes = 0;
    EXPECT_THROW(presenter_->Present(overflowingRow), PlatformException);
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


namespace {
// Backend-only observation of the actual window surface on SDL's dummy/offscreen driver.
// Native front-client preservation remains the separate private-runner qualification.
class Sdl3RegionSurfaceTest : public Sdl3GraphicsServiceTest
{
protected:
    SDL_Surface* Surface() {
        auto* window=dynamic_cast<CNA::Platform::Sdl3::Sdl3Window*>(window_.get());
        return SDL_GetWindowSurface(window->GetSdlWindow());
    }
    static std::vector<std::uint8_t> Magenta(int w,int h) {
        std::vector<std::uint8_t> bytes;
        for(int i=0;i<w*h;++i) bytes.insert(bytes.end(),{255,0,255,255});
        return bytes;
    }
    void ExpectPixel(SDL_Surface* surface,int x,int y,std::uint32_t expected) {
        Uint8 r=0,g=0,b=0,a=0;
        ASSERT_TRUE(SDL_ReadSurfacePixel(surface,x,y,&r,&g,&b,&a));
        EXPECT_EQ((static_cast<unsigned>(r)<<16)|(static_cast<unsigned>(g)<<8)|b,expected)
            << "pixel " << x << ',' << y;
        EXPECT_EQ(a,255);
    }
};
TEST_F(Sdl3RegionSurfaceTest, ClippedScaledUpdateWritesOnlyTheRequestedPixels)
{
    auto presenter=platform_->CreateSurfacePresenter(*window_);
    presenter->SetScaleMode(PresentScaleMode::Stretch,PresentFilter::Nearest);
    const auto seed=Magenta(64,48);
    presenter->Present(SurfaceFrame{seed.data(),64,48,0});
    const std::uint8_t pixels[]={255,0,0,255,0,255,0,255,0,0,255,255,255,255,255,255};
    // Hand map: 2x2 ->(-4,1,8,8), right source column covers x=[0,4); y=[1,5)G,[5,9)W.
    ASSERT_TRUE(presenter->PresentRegion(SurfaceFrame{pixels,2,2,0},SurfaceDestination{-4,1,8,8}));
    SDL_Surface* surface=Surface();
    ASSERT_NE(surface,nullptr);
    for(int y=0;y<48;++y) for(int x=0;x<64;++x) {
        const std::uint32_t expected=x<4 && y>=1 && y<9 ? (y<5?0x00FF00:0xFFFFFF) : 0xFF00FF;
        ExpectPixel(surface,x,y,expected);
    }
}
TEST_F(Sdl3RegionSurfaceTest, ResizeReacquiresThePhysicalSurface)
{
    auto presenter=platform_->CreateSurfacePresenter(*window_);
    window_->SetSize(80,60); window_->Sync();
    int w=0,h=0;
    presenter->GetTargetSize(w,h);
    EXPECT_EQ(w,80); EXPECT_EQ(h,60);
    const auto seed=Magenta(80,60);
    presenter->Present(SurfaceFrame{seed.data(),80,60,0});
    const std::uint8_t green[]={0,255,0,255};
    ASSERT_TRUE(presenter->PresentRegion(SurfaceFrame{green,1,1,0},SurfaceDestination{70,50,4,4}));
    SDL_Surface* surface=Surface();
    ASSERT_NE(surface,nullptr); EXPECT_EQ(surface->w,80); EXPECT_EQ(surface->h,60);
    ExpectPixel(surface,71,51,0x00FF00);
    ExpectPixel(surface,79,59,0xFF00FF);
}
TEST_F(Sdl3RegionSurfaceTest, MalformedRegionInputsAreRefusedBeforeWriting)
{
    auto presenter=platform_->CreateSurfacePresenter(*window_);
    const std::uint8_t red[]={255,0,0,255};
    EXPECT_THROW((void)presenter->PresentRegion(SurfaceFrame{},SurfaceDestination{0,0,1,1}),PlatformException);
    EXPECT_THROW((void)presenter->PresentRegion(SurfaceFrame{red,1,1,0},SurfaceDestination{0,0,0,1}),PlatformException);
    EXPECT_THROW((void)presenter->PresentRegion(SurfaceFrame{red,1,1,-4},SurfaceDestination{0,0,1,1}),PlatformException);
}
} // namespace


namespace {
TEST_F(Sdl3RegionSurfaceTest, OrdinaryScaleModesUsePhysicalPixelsAndClearTheBars)
{
    auto presenter=platform_->CreateSurfacePresenter(*window_);
    const std::uint8_t white[]={255,255,255,255,255,255,255,255,
                                255,255,255,255,255,255,255,255};
    struct Placement { PresentScaleMode mode; int left,top,right,bottom; };
    // Hand-derived on64x48 from2x2: stretch64x48; fit48x48 with8px side bars;
    // cover64x64 cropped8px vertically; centered/native1:1 remain2x2.
    const Placement modes[]={
        {PresentScaleMode::Stretch,0,0,64,48}, {PresentScaleMode::Letterbox,8,0,56,48},
        {PresentScaleMode::Overscan,0,0,64,48}, {PresentScaleMode::None,31,23,33,25},
        {PresentScaleMode::Native,0,0,2,2}};
    for(const auto& placement:modes) {
        SCOPED_TRACE(static_cast<int>(placement.mode));
        presenter->SetScaleMode(placement.mode,PresentFilter::Nearest);
        presenter->Present(SurfaceFrame{white,2,2,0});
        SDL_Surface* surface=Surface();
        ASSERT_NE(surface,nullptr);
        for(int y=0;y<48;++y) for(int x=0;x<64;++x) {
            const bool inside=x>=placement.left && x<placement.right &&
                              y>=placement.top && y<placement.bottom;
            ExpectPixel(surface,x,y,inside?0xFFFFFF:0);
        }
    }
}
} // namespace
