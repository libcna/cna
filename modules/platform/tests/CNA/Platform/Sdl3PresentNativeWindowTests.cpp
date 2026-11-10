// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "CNA/Platform/PlatformFactory.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include "PresentedWindowCapture.hpp"
#include <memory>
#include <vector>
namespace {
using namespace CNA::Platform;
class Gap08NativeWindowTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        platform = PlatformFactory::Create("SDL3");
        platform->AcquireSubsystem(PlatformSubsystem::Video);
        WindowDescription description;
        description.width=16; description.height=16; description.visible=true;
        original=platform->CreateWindow(description);
        original->Sync();
        if (original->GetNativeHandle().system != NativeWindowSystem::X11)
            GTEST_SKIP() << "native X11 qualification requires the private x11 runner";
    }
    void TearDown() override
    {
        original.reset();
        if (platform) platform->ReleaseSubsystem(PlatformSubsystem::Video);
    }
    std::unique_ptr<IPlatform> platform;
    std::unique_ptr<IPlatformWindow> original;
};
TEST_F(Gap08NativeWindowTest, ReusesAKnownNativeIdentityWithoutTakingOwnership)
{
#if defined(CNA_GAP08_X11_CAPTURE)
    CNA::Testing::PresentedWindow capture(original->GetNativeHandle(),16,16,false,CNA::Testing::PresentedCaptureMethod::Auto);
    const auto id=original->GetId();
    {
        auto borrowed=platform->AdoptNativeWindowHandle(capture.NativeToken());
        ASSERT_NE(borrowed,nullptr);
        EXPECT_EQ(borrowed->GetId(),id);
        EXPECT_EQ(borrowed->GetNativeHandle().windowId,capture.NativeToken());
        EXPECT_EQ(borrowed->GetPixelSize().width,16);
    }
    EXPECT_TRUE(capture.IsAlive());
    capture.Fill(0x13579B);
    EXPECT_TRUE(capture.WaitFor(*platform,[](const auto& rgb) {
        return rgb==std::vector<std::uint32_t>(256,0x13579B);
    }));
#else
    GTEST_SKIP() << "X11 observation unavailable";
#endif
}
TEST_F(Gap08NativeWindowTest, ImportsAnExternalNativeWindowButNeverDestroysIt)
{
#if defined(CNA_GAP08_X11_CAPTURE)
    CNA::Testing::PresentedWindow foreign(original->GetNativeHandle(),24,20,true,CNA::Testing::PresentedCaptureMethod::Auto);
    {
        auto borrowed=platform->AdoptNativeWindowHandle(foreign.NativeToken());
        ASSERT_NE(borrowed,nullptr);
        EXPECT_EQ(borrowed->GetNativeHandle().windowId,foreign.NativeToken());
        EXPECT_EQ(borrowed->GetPixelSize().width,24);
        EXPECT_EQ(borrowed->GetPixelSize().height,20);
        auto alias=platform->AdoptNativeWindowHandle(foreign.NativeToken());
        EXPECT_EQ(alias->GetId(),borrowed->GetId());
        borrowed.reset();
        // Two devices/platform wrappers may borrow the same import. The first borrower's
        // destruction must not invalidate the second SDL wrapper while the native host lives.
        EXPECT_NE(alias->GetId(),0u);
        EXPECT_EQ(alias->GetNativeHandle().windowId,foreign.NativeToken());
    }
    // Independent native liveness check: wrapper destruction must not close the host window.
    ASSERT_TRUE(foreign.IsAlive());
    foreign.Fill(0x2468AC);
    EXPECT_TRUE(foreign.WaitFor(*platform,[](const auto& rgb) {
        return rgb==std::vector<std::uint32_t>(480,0x2468AC);
    }));
#else
    GTEST_SKIP() << "X11 observation unavailable";
#endif
}
TEST_F(Gap08NativeWindowTest, AZeroNativeHandleIsRefused)
{
    EXPECT_THROW((void)platform->AdoptNativeWindowHandle(0),PlatformException);
}
TEST_F(Gap08NativeWindowTest, AnX11IdThatNamesNoWindowIsRefused)
{
    // SDL's X11 import ignores a failed attribute query; adopting a bogus id used to return a
    // window that crashed at its first surface presentation (CNA-DEMOS-ISSUE-158).
    EXPECT_THROW((void)platform->AdoptNativeWindowHandle(0x1234),PlatformException);
}
TEST_F(Gap08NativeWindowTest, AnX11IdWiderThanTheProtocolsTwentyNineBitsIsRefused)
{
    // The server would see it truncated to 32 bits: 0x20001234 must not be imported as another id.
    EXPECT_THROW((void)platform->AdoptNativeWindowHandle(0x20001234u),PlatformException);
}

TEST_F(Gap08NativeWindowTest, RegionPresenterScalesAndPreservesTheRestOfTheClient)
{
#if defined(CNA_GAP08_X11_CAPTURE)
    CNA::Testing::PresentedWindow capture(original->GetNativeHandle(),16,16,false,CNA::Testing::PresentedCaptureMethod::Auto);
    auto presenter=platform->CreateSurfacePresenter(*original);
    presenter->SetScaleMode(PresentScaleMode::Stretch,PresentFilter::Nearest);
    std::vector<std::uint8_t> seed;
    for(int i=0;i<256;++i) seed.insert(seed.end(),{255,0,255,255});
    SurfaceFrame background{seed.data(),16,16,0};
    presenter->Present(background);
    ASSERT_TRUE(capture.WaitFor(*platform,[](const auto& rgb) {
        return rgb==std::vector<std::uint32_t>(256,0xFF00FF);
    }));
    const std::uint8_t source[]={255,0,0,255, 0,255,0,255,
                                0,0,255,255, 255,255,255,255};
    SurfaceFrame frame{source,2,2,0};
    // Literal 2x2 -> 8x8: each source pixel occupies a 4x4 destination quadrant.
    ASSERT_TRUE(presenter->PresentRegion(frame,SurfaceDestination{4,4,8,8}));
    EXPECT_TRUE(capture.WaitFor(*platform,[](const auto& rgb) {
        for(int y=0;y<16;++y) for(int x=0;x<16;++x) {
            std::uint32_t expected=0xFF00FF;
            if(x>=4 && x<12 && y>=4 && y<12)
                expected=y<8 ? (x<8?0xFF0000:0x00FF00) : (x<8?0x0000FF:0xFFFFFF);
            if(rgb[y*16+x]!=expected) return false;
        }
        return true;
    }));
#else
    GTEST_SKIP() << "X11 observation unavailable";
#endif
}
TEST_F(Gap08NativeWindowTest, RegionClippingKeepsTheOriginalDestinationTransform)
{
#if defined(CNA_GAP08_X11_CAPTURE)
    CNA::Testing::PresentedWindow capture(original->GetNativeHandle(),16,16,false,CNA::Testing::PresentedCaptureMethod::Auto);
    auto presenter=platform->CreateSurfacePresenter(*original);
    presenter->SetScaleMode(PresentScaleMode::Stretch,PresentFilter::Nearest);
    std::vector<std::uint8_t> seed;
    for(int i=0;i<256;++i) seed.insert(seed.end(),{255,0,255,255});
    presenter->Present(SurfaceFrame{seed.data(),16,16,0});
    ASSERT_TRUE(capture.WaitFor(*platform,[](const auto& rgb) {
        return rgb==std::vector<std::uint32_t>(256,0xFF00FF);
    }));
    const std::uint8_t source[]={255,0,0,255, 0,255,0,255,
                                0,0,255,255, 255,255,255,255};
    // x=-4,width=8 maps the left source column outside the client: only green/white remain.
    ASSERT_TRUE(presenter->PresentRegion(SurfaceFrame{source,2,2,0},SurfaceDestination{-4,0,8,8}));
    EXPECT_TRUE(capture.WaitFor(*platform,[](const auto& rgb) {
        for(int y=0;y<16;++y) for(int x=0;x<16;++x) {
            const std::uint32_t expected=x<4 && y<8 ? (y<4?0x00FF00:0xFFFFFF) : 0xFF00FF;
            if(rgb[y*16+x]!=expected) return false;
        }
        return true;
    }));
#else
    GTEST_SKIP() << "X11 observation unavailable";
#endif
}
TEST_F(Gap08NativeWindowTest, ImportedNativeRegionUpdatesOnlyTheExternalClient)
{
#if defined(CNA_GAP08_X11_CAPTURE)
    CNA::Testing::PresentedWindow own(original->GetNativeHandle(),16,16,false,CNA::Testing::PresentedCaptureMethod::Auto);
    CNA::Testing::PresentedWindow external(original->GetNativeHandle(),24,20,true,CNA::Testing::PresentedCaptureMethod::Auto);
    own.Fill(0xFF00FF); external.Fill(0x2468AC);
    auto imported=platform->AdoptNativeWindowHandle(external.NativeToken());
    auto presenter=platform->CreateSurfacePresenter(*imported);
    presenter->SetScaleMode(PresentScaleMode::Stretch,PresentFilter::Nearest);
    const std::uint8_t source[]={255,0,0,255,0,255,0,255,0,0,255,255,255,255,255,255};
    ASSERT_TRUE(presenter->PresentRegion(SurfaceFrame{source,2,2,0},SurfaceDestination{18,2,4,4}));
    // 2x2 ->4x4: each literal source pixel covers2x2. All464 untouched external pixels retain S.
    EXPECT_TRUE(external.WaitFor(*platform,[](const auto& rgb) {
        for(int y=0;y<20;++y) for(int x=0;x<24;++x) {
            std::uint32_t expected=0x2468AC;
            if(x>=18 && x<22 && y>=2 && y<6)
                expected=y<4 ? (x<20?0xFF0000:0x00FF00) : (x<20?0x0000FF:0xFFFFFF);
            if(rgb[y*24+x]!=expected) return false;
        }
        return true;
    }));
    EXPECT_EQ(own.Capture(),std::vector<std::uint32_t>(256,0xFF00FF));
#else
    GTEST_SKIP() << "X11 observation unavailable";
#endif
}
TEST_F(Gap08NativeWindowTest, RegionValidationRejectsMalformedFramesAndDestinations)
{
    auto presenter=platform->CreateSurfacePresenter(*original);
    std::vector<std::uint8_t> seed(16*16*4,255);
    const SurfaceFrame valid{seed.data(),16,16,0};
    // Same documented memory-layout validation as ordinary Present; region adds positive extents.
    EXPECT_THROW((void)presenter->PresentRegion(SurfaceFrame{},SurfaceDestination{0,0,4,4}),
                 PlatformException);
    EXPECT_THROW((void)presenter->PresentRegion(valid,SurfaceDestination{0,0,0,4}),
                 PlatformException);
    EXPECT_THROW((void)presenter->PresentRegion(valid,SurfaceDestination{0,0,4,-1}),
                 PlatformException);
}
} // namespace
