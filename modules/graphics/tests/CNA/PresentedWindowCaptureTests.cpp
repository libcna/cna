// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "PresentedWindowCapture.hpp"
#include "CNA/Platform/PlatformFactory.hpp"
#include "CNA/Platform/PlatformTestDecorator.hpp"
#include <array>
#include <optional>
#include <vector>
using namespace CNA::Testing;
namespace {
using Pixels=std::optional<std::vector<std::uint32_t>>;
using namespace PresentedCaptureDetail;
}
TEST(PresentedWindowCaptureContract, GeometryRejectsTheOldSixteenPixelReadOfAnEightPixelClient)
{
    // XGetImage requires the requested rectangle to fit the drawable. Never crop a bad fixture.
    EXPECT_TRUE(GeometryProblem(16,16,16,16,2).empty());
    const auto message=GeometryProblem(16,16,8,8,2);
    EXPECT_NE(message.find("requested 16x16"),std::string::npos);
    EXPECT_NE(message.find("actual client 8x8"),std::string::npos);
    EXPECT_FALSE(GeometryProblem(16,16,32,32,2).empty());
    EXPECT_FALSE(GeometryProblem(0,16,16,16,2).empty());
}
TEST(PresentedWindowCaptureContract, UnviewableWindowsCannotMasqueradeAsCapturedPixels)
{
    // Xlib map_state: IsUnmapped=0, IsUnviewable=1, IsViewable=2.
    EXPECT_FALSE(GeometryProblem(16,16,16,16,0).empty());
    EXPECT_FALSE(GeometryProblem(16,16,16,16,1).empty());
    EXPECT_TRUE(GeometryProblem(16,16,16,16,2).empty());
}
TEST(PresentedWindowCaptureContract, SoftwareDefaultAndExplicitDiagnosticMethodsRemainDistinct)
{
    EXPECT_EQ(ParseMethod("",PresentedCaptureMethod::Window),PresentedCaptureMethod::Window);
    EXPECT_EQ(ParseMethod("",PresentedCaptureMethod::Auto),PresentedCaptureMethod::Auto);
    EXPECT_EQ(ParseMethod("window",PresentedCaptureMethod::Auto),PresentedCaptureMethod::Window);
    EXPECT_EQ(ParseMethod("composite",PresentedCaptureMethod::Window),PresentedCaptureMethod::Composite);
    EXPECT_EQ(ParseMethod("auto",PresentedCaptureMethod::Window),PresentedCaptureMethod::Auto);
    EXPECT_THROW(ParseMethod("unknown",PresentedCaptureMethod::Auto),std::invalid_argument);
}
TEST(PresentedWindowCaptureContract, DirectModeNeverRedirectsOrFallsBack)
{
    int composites=0,windows=0;
    const auto result=Read(PresentedCaptureMethod::Window,
        [&]() -> Pixels { ++composites; return std::vector<std::uint32_t>{0}; },
        [&]() -> Pixels { ++windows; return std::vector<std::uint32_t>{0xFF00FF}; });
    ASSERT_TRUE(result); EXPECT_EQ(*result,(std::vector<std::uint32_t>{0xFF00FF}));
    EXPECT_EQ(composites,0); EXPECT_EQ(windows,1);
}
TEST(PresentedWindowCaptureContract, AutoUsesCompositeWithoutChoosingTheOracleColor)
{
    int composites=0,windows=0;
    // A readable black frame MUST remain black; never switch readers to obtain expected magenta.
    const auto result=Read(PresentedCaptureMethod::Auto,
        [&]() -> Pixels { ++composites; return std::vector<std::uint32_t>{0}; },
        [&]() -> Pixels { ++windows; return std::vector<std::uint32_t>{0xFF00FF}; });
    ASSERT_TRUE(result); EXPECT_EQ(*result,(std::vector<std::uint32_t>{0}));
    EXPECT_EQ(composites,1); EXPECT_EQ(windows,0);
}
TEST(PresentedWindowCaptureContract, AutoFallsBackOnlyWhenTheCompositeTransportFails)
{
    std::vector<char> order;
    const auto result=Read(PresentedCaptureMethod::Auto,
        [&]() -> Pixels { order.push_back('C'); return std::nullopt; },
        [&]() -> Pixels { order.push_back('W'); return std::vector<std::uint32_t>{0x2468AC}; });
    ASSERT_TRUE(result); EXPECT_EQ(*result,(std::vector<std::uint32_t>{0x2468AC}));
    EXPECT_EQ(order,(std::vector<char>{'C','W'}));
}
TEST(PresentedWindowCaptureContract, ForcedCompositeAndCompleteTransportFailureStayFailures)
{
    int windows=0;
    const auto forced=Read(PresentedCaptureMethod::Composite,[]() -> Pixels { return std::nullopt; },
        [&]() -> Pixels { ++windows; return std::vector<std::uint32_t>{0xFFFFFF}; });
    EXPECT_FALSE(forced); EXPECT_EQ(windows,0);
    const auto automatic=Read(PresentedCaptureMethod::Auto,[]() -> Pixels { return std::nullopt; },
        [&]() -> Pixels { ++windows; return std::nullopt; });
    EXPECT_FALSE(automatic); EXPECT_EQ(windows,1);
}
TEST(PresentedWindowCaptureContract, WindowVisualMasksDecodeNamedPixmapPixels)
{
    // GetImage of a pixmap has no visual ID; associated window masks supply RGB meaning.
    EXPECT_EQ(Rgb(0x123456,0xFF0000,0x00FF00,0x0000FF),0x123456u);
    EXPECT_EQ(Rgb(0x563412,0x0000FF,0x00FF00,0xFF0000),0x123456u);
    EXPECT_EQ(Rgb(0xFF00FF,0xFF0000,0x00FF00,0x0000FF),0xFF00FFu);
}
TEST(PresentedWindowCaptureContract, NativeChannelWidthsHaveIndependentIntegerArithmetic)
{
    EXPECT_EQ(Rgb(0xFFFF,0xF800,0x07E0,0x001F),0xFFFFFFu);
    // 565 word8410: R=16*255/31=131,G=32*255/63=129,B=16*255/31=131.
    EXPECT_EQ(Rgb(0x8410,0xF800,0x07E0,0x001F),0x838183u);
    EXPECT_EQ(Channel(0,0),0u);
}

#if defined(CNA_GAP08_X11_CAPTURE) && defined(CNA_PLATFORM_SDL3)
namespace {
using namespace CNA::Platform;
class CaptureNativeClient : public ::testing::Test
{
protected:
    void SetUp() override
    {
        platform=PlatformFactory::Create("SDL3");
        scope=std::make_unique<CNA::Platform::Testing::ScopedCurrentPlatform>(*platform);
        platform->AcquireSubsystem(PlatformSubsystem::Video); acquired=true;
        WindowDescription d; d.width=16; d.height=16; d.visible=true; d.borderless=true;
        d.centered=false; d.x=320; d.y=32; d.title="GAP-08c capture transport CPU control";
        window=platform->CreateWindow(d); window->Sync();
        ASSERT_EQ(window->GetNativeHandle().system,NativeWindowSystem::X11)
            << "native capture control requires private X11";
    }
    void TearDown() override
    {
        window.reset();
        if(acquired) platform->ReleaseSubsystem(PlatformSubsystem::Video);
        scope.reset(); platform.reset();
    }
    std::unique_ptr<IPlatform> platform;
    std::unique_ptr<CNA::Platform::Testing::ScopedCurrentPlatform> scope;
    std::unique_ptr<IPlatformWindow> window;
    bool acquired=false;
};
}
TEST_F(CaptureNativeClient, CpuPresenterKeepsTheSoftwareCapturePathWorking)
{
    // Real native CPU output, no GraphicsDevice/GPU. Verify all pixels, not merely transport success.
    PresentedWindow capture(window->GetNativeHandle(),16,16,false,PresentedCaptureMethod::Auto);
    auto presenter=platform->CreateSurfacePresenter(*window);
    presenter->SetScaleMode(PresentScaleMode::Stretch,PresentFilter::Nearest);
    const std::array<std::uint8_t,16> pixels{255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,255,255};
    presenter->Present({pixels.data(),2,2,8});
    std::string diagnostic;
    ASSERT_TRUE(capture.ProbeCapture(diagnostic))<<diagnostic;
    EXPECT_TRUE(capture.WaitFor(*platform,[](const auto& rgb) {
        if(rgb.size()!=256) return false;
        for(int y=0;y<16;++y) for(int x=0;x<16;++x) {
            const std::uint32_t expected=y<8?(x<8?0xFF0000:0x00FF00):(x<8?0x0000FF:0xFFFFFF);
            if(rgb[y*16+x]!=expected) return false;
        }
        return true;
    }))<<capture.Diagnostics();
}
TEST_F(CaptureNativeClient, ResizeMismatchIsExplicitBeforeAnOversizedImageRequest)
{
    PresentedWindow capture(window->GetNativeHandle(),16,16,false,PresentedCaptureMethod::Auto);
    window->SetSize(8,8); window->Sync();
    std::string diagnostic;
    EXPECT_FALSE(capture.ProbeCapture(diagnostic));
    EXPECT_NE(diagnostic.find("capture geometry mismatch"),std::string::npos)<<diagnostic;
    EXPECT_NE(diagnostic.find("actual client 8x8"),std::string::npos)<<diagnostic;
}
#endif
