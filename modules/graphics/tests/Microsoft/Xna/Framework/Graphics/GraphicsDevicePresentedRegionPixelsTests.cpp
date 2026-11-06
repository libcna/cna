// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "CNA/GraphicsRendererSelection.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Platform/PlatformFactory.hpp"
#include "CNA/Platform/PlatformTestDecorator.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "PresentedWindowCapture.hpp"
#include <algorithm>
#include <memory>
#include <optional>
#include <vector>

namespace {
using namespace CNA::Platform;
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
#if defined(CNA_GAP08_X11_CAPTURE)
using CNA::Testing::PresentedWindow;
class Gap08PresentedPixels : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const auto selected=CNA::GraphicsRendererSelection::GetSelected();
        if (selected!=CNA::GraphicsRendererType::Software)
            GTEST_SKIP() << "SOFTWARE native-client pixels; FNA3D dependency witnesses are report-only";
        platform=PlatformFactory::Create("SDL3");
        scope=std::make_unique<CNA::Platform::Testing::ScopedCurrentPlatform>(*platform);
        platform->AcquireSubsystem(PlatformSubsystem::Video);
        acquired=true;
        auto p=PresentationParameters::CreateWindowedEXT(8,8);
        p.setPresentationIntervalProperty(PresentInterval::Immediate);
        if (selected==CNA::GraphicsRendererType::Software)
        {
            WindowDescription description;
            description.title="G08a explicit SOFTWARE presentation target";
            description.width=16; description.height=16; description.visible=true;
            // X11 may discard covered client pixels. Separate borderless clients keep the
            // unchanged-own-window assertion about CNA writes rather than exposure/backing store.
            description.centered=false; description.x=32; description.y=32;
            description.borderless=true;
            own=platform->CreateWindow(description);
            // Existing PP handle remains a legacy platform token. Only Present's override is native.
            p.setDeviceWindowHandleProperty(own->GetWindowHandle());
        }
        device=std::make_unique<GraphicsDevice>(GraphicsAdapter::getDefaultAdapterProperty(),
                                               GraphicsProfile::HiDef,p);
        if (!own) own=platform->AdoptWindowHandle(
            device->getPresentationParametersProperty().getDeviceWindowHandleProperty());
        own->SetSize(16,16); own->Sync(); own->Show();
        // Establish the 8x8 logical backbuffer independently of the resized physical client.
        device->GetRenderer().SetVirtualResolution(9,8);
        device->GetRenderer().SetVirtualResolution(8,8);
        if (own->GetNativeHandle().system!=NativeWindowSystem::X11)
            GTEST_SKIP() << "use the private x11 runner for native-window observations";
        ASSERT_EQ(own->GetPixelSize().width,16); ASSERT_EQ(own->GetPixelSize().height,16);
        front=std::make_unique<PresentedWindow>(own->GetNativeHandle(),16,16);
        device->Clear(Color(255,0,255,255));
        device->Present();
        ASSERT_TRUE(front->WaitFor(*platform,[](const auto& rgb) {
            return rgb==std::vector<std::uint32_t>(256,0xFF00FF);
        })) << "ordinary Present control must reach the native window";
    }
    void TearDown() override
    {
        device.reset(); // Renderer and cached borrowed wrappers must die before native windows.
        foreign.reset(); other.reset(); front.reset(); own.reset();
        if (acquired) platform->ReleaseSubsystem(PlatformSubsystem::Video);
        scope.reset(); platform.reset();
    }
    void DrawQuadrants()
    {
        Texture2D texture(*device,8,8);
        std::vector<Color> pixels;
        for(int y=0;y<8;++y) for(int x=0;x<8;++x)
            pixels.push_back(y<4 ? (x<4 ? Color(255,0,0,255) : Color(0,255,0,255))
                                : (x<4 ? Color(0,0,255,255) : Color(255,255,255,255)));
        texture.SetData(pixels.data(),0,64);
        SpriteBatch sprites(*device);
        sprites.Begin(SpriteSortMode::Immediate,&BlendState::Opaque,&SamplerState::PointClamp,
                      nullptr,nullptr);
        sprites.Draw(texture,Rectangle(0,0,8,8),Color::White);
        sprites.End();
    }
    static bool OutsideEquals(const std::vector<std::uint32_t>& rgb,int w,int h,
                              int left,int top,int width,int height,std::uint32_t color)
    {
        if(rgb.size()!=static_cast<std::size_t>(w*h)) return false;
        for(int y=0;y<h;++y) for(int x=0;x<w;++x)
            if((x<left || y<top || x>=left+width || y>=top+height) && rgb[y*w+x]!=color)
                return false;
        return true;
    }
    std::unique_ptr<IPlatform> platform;
    std::unique_ptr<CNA::Platform::Testing::ScopedCurrentPlatform> scope;
    std::unique_ptr<IPlatformWindow> own;
    std::unique_ptr<GraphicsDevice> device;
    std::unique_ptr<PresentedWindow> front;
    std::unique_ptr<PresentedWindow> foreign;
    std::unique_ptr<IPlatformWindow> other;
    void CreateOtherWindow()
    {
        WindowDescription description;
        description.title="G08a second platform window";
        description.width=24; description.height=20; description.visible=true;
        const auto ownBounds=own->GetClientBounds();
        description.centered=false;
        description.x=ownBounds.x+ownBounds.width+64; description.y=ownBounds.y;
        description.borderless=true;
        other=platform->CreateWindow(description);
        other->Sync();
        foreign=std::make_unique<PresentedWindow>(other->GetNativeHandle(),24,20);
    }
    bool acquired=false;
};

TEST_F(Gap08PresentedPixels, SourceOnlyPresentsTheSelectedQuadrantAcrossTheClient)
{
    DrawQuadrants();
    // Source [4,8)x[4,8) is literal white; null destination covers the selected 16x16 client.
    device->Present(Rectangle(4,4,4,4),std::nullopt,0);
    EXPECT_TRUE(front->WaitFor(*platform,[](const auto& rgb) {
        return rgb==std::vector<std::uint32_t>(256,0xFFFFFF);
    }));
}

TEST_F(Gap08PresentedPixels, OversizedSourcePresentsItsIntersectionWithTheLiveBackbuffer)
{
    DrawQuadrants();
    device->Present(Rectangle(6,6,8,8),Rectangle(4,4,8,8),0);
    // Existing CNA source-clipping adaptation: [6,14) intersect [0,8)=[6,8), all literal white.
    EXPECT_TRUE(front->WaitFor(*platform,[](const auto& rgb) {
        if(!OutsideEquals(rgb,16,16,4,4,8,8,0xFF00FF)) return false;
        for(int y=4;y<12;++y) for(int x=4;x<12;++x)
            if(rgb[y*16+x]!=0xFFFFFF) return false;
        return true;
    }));
}

TEST_F(Gap08PresentedPixels, SourceOnlyDoesNotLetterboxAnAsymmetricSource)
{
    DrawQuadrants();
    device->Present(Rectangle(0,0,8,4),std::nullopt,0);
    // Null destination fills 16x16 even for an 8x4 source; no magenta top/bottom bars.
    EXPECT_TRUE(front->WaitFor(*platform,[](const auto& rgb) {
        return rgb[1*16+2]==0xFF0000 && rgb[14*16+2]==0xFF0000 &&
               rgb[1*16+13]==0x00FF00 && rgb[14*16+13]==0x00FF00;
    }));
}

TEST_F(Gap08PresentedPixels, DestinationRetainsEveryPixelOutsideTheRequestedRegion)
{
    DrawQuadrants();
    device->Present(Rectangle(0,0,8,8),Rectangle(4,4,8,8),0);
    // 8 -> 8 is 1:1. Four literal quadrant interiors and ALL outside pixels are checked.
    EXPECT_TRUE(front->WaitFor(*platform,[](const auto& rgb) {
        return OutsideEquals(rgb,16,16,4,4,8,8,0xFF00FF) &&
               rgb[5*16+5]==0xFF0000 && rgb[5*16+10]==0x00FF00 &&
               rgb[10*16+5]==0x0000FF && rgb[10*16+10]==0xFFFFFF;
    }));
}

TEST_F(Gap08PresentedPixels, ScalingPlacesFourQuadrantsInTheDestination)
{
    DrawQuadrants();
    device->Present(Rectangle(2,2,4,4),Rectangle(4,4,8,8),0);
    // 4 -> 8 doubles both axes. Interior centers avoid a native filter's boundary footprint.
    EXPECT_TRUE(front->WaitFor(*platform,[](const auto& rgb) {
        return OutsideEquals(rgb,16,16,4,4,8,8,0xFF00FF) &&
               rgb[5*16+5]==0xFF0000 && rgb[5*16+10]==0x00FF00 &&
               rgb[10*16+5]==0x0000FF && rgb[10*16+10]==0xFFFFFF;
    }));
}

TEST_F(Gap08PresentedPixels, DestinationClippingRetainsTheOriginalScale)
{
    DrawQuadrants();
    device->Present(Rectangle(0,0,8,8),Rectangle(-4,0,8,8),0);
    // Original map is dx=sx-4: visible x=[0,4) comes from source x=[4,8), green/white.
    // This is the proposed CNA contract pending the native Refresh probe, not an IL pixel claim.
    EXPECT_TRUE(front->WaitFor(*platform,[](const auto& rgb) {
        return OutsideEquals(rgb,16,16,0,0,4,8,0xFF00FF) &&
               rgb[1*16+1]==0x00FF00 && rgb[6*16+1]==0xFFFFFF;
    }));
}

TEST_F(Gap08PresentedPixels, NativeOverrideUsesTheForeignClientsBoundsAndKeepsOwnWindowUntouched)
{
    // Consumers create windows only through IPlatform. External native import is covered by
    // the SDL3 platform tests; the Present override here is still a native XID, never an SDL token.
    CreateOtherWindow();
    foreign->Fill(0x2468AC);
    ASSERT_TRUE(foreign->WaitFor(*platform,[](const auto& rgb) {
        return rgb==std::vector<std::uint32_t>(480,0x2468AC);
    }));
    // Re-establish the literal control after the second client has been mapped, then take a
    // snapshot immediately before the override. Every own-client pixel must remain unchanged.
    device->Present();
    ASSERT_TRUE(front->WaitFor(*platform,[](const auto& rgb) {
        return rgb==std::vector<std::uint32_t>(256,0xFF00FF);
    }));
    DrawQuadrants();
    const auto ownBefore=front->Capture();
    ASSERT_EQ(ownBefore,std::vector<std::uint32_t>(256,0xFF00FF));
    // x=18 lies beyond the OWN window's width 16 but inside the foreign width 24.
    device->Present(Rectangle(4,0,4,4),Rectangle(18,2,4,4),foreign->NativeToken());
    EXPECT_TRUE(foreign->WaitFor(*platform,[](const auto& rgb) {
        return OutsideEquals(rgb,24,20,18,2,4,4,0x2468AC) && rgb[3*24+19]==0x00FF00;
    }));
    EXPECT_EQ(front->Capture(),ownBefore);
    device.reset(); // Cached renderer references must be gone before the foreign host is released.
    ASSERT_TRUE(foreign->IsAlive());
    foreign->Fill(0x13579B);
    EXPECT_TRUE(foreign->WaitFor(*platform,[](const auto& rgb) {
        return rgb==std::vector<std::uint32_t>(480,0x13579B);
    }));
}

TEST_F(Gap08PresentedPixels, NativeSizeBackbufferStillHonorsASourceRectangle)
{
    // Match logical and physical dimensions; source selection must still affect the output.
    device->GetRenderer().SetVirtualResolution(16,16);
    device->Clear(Color(255,0,255,255)); device->Present();
    DrawQuadrants();
    device->Present(Rectangle(4,4,4,4),std::nullopt,0);
    EXPECT_TRUE(front->WaitFor(*platform,[](const auto& rgb) {
        return rgb==std::vector<std::uint32_t>(256,0xFFFFFF);
    }));
}

TEST_F(Gap08PresentedPixels, NullRectanglesOverrideTheWholeForeignClient)
{
    CreateOtherWindow();
    foreign->Fill(0x2468AC);
    device->Present();
    ASSERT_TRUE(front->WaitFor(*platform,[](const auto& rgb) {
        return rgb==std::vector<std::uint32_t>(256,0xFF00FF);
    }));
    device->Clear(Color(255,0,0,255));
    const auto ownBefore=front->Capture();
    ASSERT_EQ(ownBefore,std::vector<std::uint32_t>(256,0xFF00FF));
    device->Present(std::nullopt,std::nullopt,foreign->NativeToken());
    EXPECT_TRUE(foreign->WaitFor(*platform,[](const auto& rgb) {
        return rgb==std::vector<std::uint32_t>(480,0xFF0000);
    }));
    EXPECT_EQ(front->Capture(),ownBefore);
    device.reset();
    EXPECT_TRUE(foreign->IsAlive());
}
#else
TEST(Gap08PresentedPixels, RequiresNativeCaptureSupport)
{
    GTEST_SKIP() << "private X11 front-window observation is unavailable on this build";
}
#endif
} // namespace
