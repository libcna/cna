// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "CNA/GraphicsRendererSelection.hpp"
#include "CNA/Platform/PlatformFactory.hpp"
#include "CNA/Platform/PlatformTestDecorator.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "System/InvalidOperationException.hpp"
#include "PresentedWindowCapture.hpp"
#include "PresentedWindowCaptureWin32.hpp"
#include <algorithm>
#include <memory>
#include <optional>
#include <vector>

namespace {
using namespace CNA::Platform;
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using XnaRectangle=Microsoft::Xna::Framework::Rectangle;
#if defined(CNA_GAP08_X11_CAPTURE) || defined(_WIN32)
#if defined(_WIN32)
using NativeCapture=CNA::Testing::PresentedWin32Window;
#else
using NativeCapture=CNA::Testing::PresentedWindow;
#endif
std::unique_ptr<NativeCapture> CaptureClient(const NativeWindowHandle& handle,int width,int height)
{
#if defined(_WIN32)
    return std::make_unique<NativeCapture>(handle,width,height);
#else
    return std::make_unique<NativeCapture>(handle,width,height,false,CNA::Testing::PresentedCaptureMethod::Auto);
#endif
}
class Gap08bPresentedPixels : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const auto selected=CNA::GraphicsRendererSelection::GetSelected();
        if (selected!=CNA::GraphicsRendererType::OpenGL33 &&
            selected!=CNA::GraphicsRendererType::OpenGLES3 &&
            selected!=CNA::GraphicsRendererType::DirectX9)
            GTEST_SKIP() << "requires selected OPENGL33, OPENGLES3 or DIRECTX9";
        gl=selected!=CNA::GraphicsRendererType::DirectX9;
        platform=PlatformFactory::Create("SDL3");
        scope=std::make_unique<CNA::Platform::Testing::ScopedCurrentPlatform>(*platform);
        platform->AcquireSubsystem(PlatformSubsystem::Video); acquired=true;
        WindowDescription description;
        description.title="G08b primary native pixel witness";
        description.width=64; description.height=48;
        description.centered=false; description.x=16; description.y=32;
        description.borderless=true;
        if (gl)
        {
            description.renderIntent=WindowRenderIntent::OpenGl;
            description.openGlFramebuffer.doubleBuffered=true;
            description.openGlFramebuffer.depthBits=24;
            description.openGlFramebuffer.stencilBits=8;
        }
        own=platform->CreateWindow(description);
        auto p=PresentationParameters::CreateWindowedEXT(64,48);
        p.setDeviceWindowHandleProperty(own->GetWindowHandle()); // PP uses the legacy token.
        p.setPresentationIntervalProperty(PresentInterval::Immediate);
        device=std::make_unique<GraphicsDevice>(GraphicsAdapter::getDefaultAdapterProperty(),
                                               GraphicsProfile::HiDef,p);
        own->Sync(); own->Show();
        ASSERT_EQ(own->GetPixelSize().width,64); ASSERT_EQ(own->GetPixelSize().height,48);
        // Required qualification runs cannot silently substitute a different native system.
#if defined(_WIN32)
        ASSERT_EQ(own->GetNativeHandle().system,NativeWindowSystem::Win32);
#else
        ASSERT_EQ(own->GetNativeHandle().system,NativeWindowSystem::X11);
#endif
        front=CaptureClient(own->GetNativeHandle(),64,48);
        device->Clear(Color(255,0,255,255)); device->Present();
        ASSERT_TRUE(front->WaitFor(*platform,[](const auto& rgb) { return All(rgb,64,48,0xFF00FF); }))
            << "ordinary Present must reach the captured client before region assertions";
    }
    void TearDown() override
    {
        device.reset(); foreign.reset(); other.reset(); front.reset(); own.reset();
        if (acquired) platform->ReleaseSubsystem(PlatformSubsystem::Video);
        scope.reset(); platform.reset();
    }
    static bool All(const std::vector<std::uint32_t>& rgb,int w,int h,std::uint32_t color)
    {
        return rgb==std::vector<std::uint32_t>(static_cast<std::size_t>(w)*h,color);
    }
    static bool Outside(const std::vector<std::uint32_t>& rgb,int w,int h,
                        int left,int top,int width,int height,std::uint32_t color)
    {
        if (rgb.size()!=static_cast<std::size_t>(w)*h) return false;
        for(int y=0;y<h;++y) for(int x=0;x<w;++x)
            if ((x<left || y<top || x>=left+width || y>=top+height) && rgb[y*w+x]!=color)
                return false;
        return true;
    }
    void DrawQuadrants()
    {
        // Authored RGBA8 endpoints, no CNA readback supplies an expectation.
        Texture2D texture(*device,64,48);
        std::vector<Color> pixels;
        for(int y=0;y<48;++y) for(int x=0;x<64;++x)
            pixels.push_back(y<24 ? (x<32 ? Color(255,0,0,255) : Color(0,255,0,255))
                                 : (x<32 ? Color(0,0,255,255) : Color(255,255,255,255)));
        texture.SetData(pixels.data(),0,64*48);
        SpriteBatch batch(*device);
        batch.Begin(SpriteSortMode::Immediate,&BlendState::Opaque,&SamplerState::PointClamp,
                    nullptr,nullptr);
        batch.Draw(texture,XnaRectangle(0,0,64,48),Color::White); batch.End();
    }
    void CreateOther()
    {
        WindowDescription d;
        d.title="G08b foreign native target"; d.width=96; d.height=80;
        d.centered=false; d.x=160; d.y=32; d.borderless=true;
        // Owner GAP-08b/c decision: borrowed targets receive finished readback pixels through
        // the native region presenter. They need no GL context or GL-compatible visual.
        other=platform->CreateWindow(d); other->Sync(); other->Show();
        ASSERT_EQ(other->GetPixelSize().width,96); ASSERT_EQ(other->GetPixelSize().height,80);
        foreign=CaptureClient(other->GetNativeHandle(),96,80);
        foreign->Fill(0x2468AC);
        ASSERT_TRUE(foreign->WaitFor(*platform,[](const auto& rgb) { return All(rgb,96,80,0x2468AC); }));
    }
    std::unique_ptr<IPlatform> platform;
    std::unique_ptr<CNA::Platform::Testing::ScopedCurrentPlatform> scope;
    std::unique_ptr<IPlatformWindow> own,other;
    std::unique_ptr<GraphicsDevice> device;
    std::unique_ptr<NativeCapture> front,foreign;
    bool acquired=false,gl=false;
};

TEST_F(Gap08bPresentedPixels, SourceOnlySelectsBottomRightAndFillsTheClient)
{
    DrawQuadrants();
    // Refresh public Present IL_0014-0045 preserves source; [32,64)x[24,48) is W.
    device->Present(XnaRectangle(32,24,32,24),std::nullopt,0);
    EXPECT_TRUE(front->WaitFor(*platform,[](const auto& rgb) { return All(rgb,64,48,0xFFFFFF); }));
}
TEST_F(Gap08bPresentedPixels, DestinationOnlyPreservesEveryOutsidePixel)
{
    DrawQuadrants();
    // Original RECT IL_0046-0077; hand map 64x48 -> (16,8,32,24), scale 1/2.
    device->Present(std::nullopt,XnaRectangle(16,8,32,24),0);
    EXPECT_TRUE(front->WaitFor(*platform,[](const auto& rgb) {
        return Outside(rgb,64,48,16,8,32,24,0xFF00FF) &&
               rgb[12*64+20]==0xFF0000 && rgb[12*64+44]==0x00FF00 &&
               rgb[28*64+20]==0x0000FF && rgb[28*64+44]==0xFFFFFF;
    }));
}
TEST_F(Gap08bPresentedPixels, CroppedSourceScalesAroundItsOwnQuadrantBoundary)
{
    DrawQuadrants();
    device->Present(XnaRectangle(24,18,24,18),XnaRectangle(16,8,48,36),0);
    // Scale2: source boundary (32,24) maps to (32,20), not destination midpoint(40,26).
    // Probe(36,22) is W with the crop and R if the full source was silently substituted.
    EXPECT_TRUE(front->WaitFor(*platform,[](const auto& rgb) {
        return Outside(rgb,64,48,16,8,48,36,0xFF00FF) &&
               rgb[12*64+20]==0xFF0000 && rgb[12*64+60]==0x00FF00 &&
               rgb[40*64+20]==0x0000FF && rgb[40*64+60]==0xFFFFFF && rgb[22*64+36]==0xFFFFFF;
    }));
}
TEST_F(Gap08bPresentedPixels, NegativeDestinationKeepsTheOriginalAffineMap)
{
    DrawQuadrants();
    device->Present(XnaRectangle(0,0,64,48),XnaRectangle(-32,-24,64,48),0);
    // Approved G08a mapping: dx=sx-32,dy=sy-24; visible 32x24 is ONLY W.
    // Native Refresh clipping parity still requires G08a's probe, not inferred from IL.
    EXPECT_TRUE(front->WaitFor(*platform,[](const auto& rgb) {
        if (!Outside(rgb,64,48,0,0,32,24,0xFF00FF)) return false;
        for(int y=0;y<24;++y) for(int x=0;x<32;++x) if(rgb[y*64+x]!=0xFFFFFF) return false;
        return true;
    }));
}
TEST_F(Gap08bPresentedPixels, SourceClippingUsesTheLiveSurfaceIntersection)
{
    DrawQuadrants();
    device->Present(XnaRectangle(60,44,16,12),XnaRectangle(16,8,32,24),0);
    // CNA's existing independent clip: [60,76)x[44,56) intersect64x48 = 4x4 W.
    EXPECT_TRUE(front->WaitFor(*platform,[](const auto& rgb) {
        if (!Outside(rgb,64,48,16,8,32,24,0xFF00FF)) return false;
        for(int y=8;y<32;++y) for(int x=16;x<48;++x) if(rgb[y*64+x]!=0xFFFFFF) return false;
        return true;
    }));
}
TEST_F(Gap08bPresentedPixels, NativeOverrideUsesForeignBoundsAndLeavesPrimaryUntouched)
{
    CreateOther(); ASSERT_NE(foreign,nullptr); DrawQuadrants();
    const auto original=device->getPresentationParametersProperty().getDeviceWindowHandleProperty();
    // Native HWND/XID (Refresh IL_0078-0082); x72 exceeds own width64, fits foreign96.
    device->Present(XnaRectangle(32,0,32,24),XnaRectangle(72,8,16,12),foreign->NativeToken());
    EXPECT_TRUE(foreign->WaitFor(*platform,[](const auto& rgb) {
        if (!Outside(rgb,96,80,72,8,16,12,0x2468AC)) return false;
        for(int y=8;y<20;++y) for(int x=72;x<88;++x) if(rgb[y*96+x]!=0x00FF00) return false;
        return true;
    }));
    EXPECT_EQ(front->Capture(),std::vector<std::uint32_t>(64*48,0xFF00FF));
    EXPECT_EQ(device->getPresentationParametersProperty().getDeviceWindowHandleProperty(),original);
    device.reset();
    ASSERT_TRUE(foreign->IsAlive()); // Borrowed host survives renderer/cache teardown.
    foreign->Fill(0x13579B);
    EXPECT_TRUE(foreign->WaitFor(*platform,[](const auto& rgb) { return All(rgb,96,80,0x13579B); }));
}
TEST_F(Gap08bPresentedPixels, NullRectanglesFillTheForeignClientAndOrdinaryPresentReturnsHome)
{
    CreateOther(); ASSERT_NE(foreign,nullptr);
    device->Clear(Color(255,0,0,255));
    device->Present(std::nullopt,std::nullopt,foreign->NativeToken());
    ASSERT_TRUE(foreign->WaitFor(*platform,[](const auto& rgb) { return All(rgb,96,80,0xFF0000); }));
    EXPECT_EQ(front->Capture(),std::vector<std::uint32_t>(64*48,0xFF00FF));
    device->Clear(Color(0,0,255,255)); device->Present();
    EXPECT_TRUE(front->WaitFor(*platform,[](const auto& rgb) { return All(rgb,64,48,0x0000FF); }));
    EXPECT_EQ(foreign->Capture(),std::vector<std::uint32_t>(96*80,0xFF0000));
}
TEST_F(Gap08bPresentedPixels, ConsecutivePartialPresentsRetainEarlierDestinationUpdates)
{
    device->Clear(Color(255,0,0,255));
    device->Present(std::nullopt,XnaRectangle(0,0,16,12),0);
    ASSERT_TRUE(front->WaitFor(*platform,[](const auto& rgb) {
        return Outside(rgb,64,48,0,0,16,12,0xFF00FF) && rgb[5*64+5]==0xFF0000;
    }));
    device->Clear(Color(0,255,0,255));
    device->Present(std::nullopt,XnaRectangle(48,36,16,12),0);
    EXPECT_TRUE(front->WaitFor(*platform,[](const auto& rgb) {
        if (rgb.size()!=64*48) return false;
        for(int y=0;y<48;++y) for(int x=0;x<64;++x) {
            const auto expected=x<16&&y<12 ? 0xFF0000u : x>=48&&y>=36 ? 0x00FF00u : 0xFF00FFu;
            if(rgb[y*64+x]!=expected) return false;
        }
        return true;
    }));
}
TEST_F(Gap08bPresentedPixels, RegionBypassesGameScissorAndRestoresNativeDrawingState)
{
    DrawQuadrants();
    RasterizerState raster; raster.setScissorTestEnableProperty(true);
    raster.setCullModeProperty(CullMode::None);
    device->setRasterizerStateProperty(raster);
    device->setScissorRectangleProperty(XnaRectangle(0,0,2,2));
    device->setViewportProperty(Viewport(4,3,8,6));
    device->Present(XnaRectangle(32,24,32,24),XnaRectangle(16,8,32,24),0);
    ASSERT_TRUE(front->WaitFor(*platform,[](const auto& rgb) {
        return Outside(rgb,64,48,16,8,32,24,0xFF00FF) && rgb[20*64+32]==0xFFFFFF;
    }));
    EXPECT_EQ(device->getScissorRectangleProperty(),XnaRectangle(0,0,2,2));
    EXPECT_EQ(device->getViewportProperty().getWidthProperty(),8);
    // Same cached state assignments: the next real draw must still honor a tiny scissor.
    // Clear ignores scissor by XNA contract; this checks leaked native state via drawing.
    device->Clear(Color(255,0,255,255));
    device->setViewportProperty(Viewport(0,0,64,48));
    Texture2D white(*device,1,1); Color w(255,255,255,255); white.SetData(&w,0,1);
    SpriteBatch batch(*device);
    batch.Begin(SpriteSortMode::Immediate,&BlendState::Opaque,&SamplerState::PointClamp,
                nullptr,&raster);
    batch.Draw(white,XnaRectangle(0,0,64,48),Color(255,0,0,255)); batch.End();
    device->Present();
    EXPECT_TRUE(front->WaitFor(*platform,[](const auto& rgb) {
        return Outside(rgb,64,48,0,0,2,2,0xFF00FF) && rgb[0]==0xFF0000;
    }));
}
TEST_F(Gap08bPresentedPixels, ActiveTargetRefusalDoesNotChangeEitherPresentedClient)
{
    CreateOther(); ASSERT_NE(foreign,nullptr);
    RenderTarget2D target(*device,8,8); device->SetRenderTarget(&target);
    // Refresh core IL_0013-0026 guards before native Present.
    EXPECT_THROW(device->Present(XnaRectangle(0,0,4,4),XnaRectangle(0,0,8,8),foreign->NativeToken()),
                 System::InvalidOperationException);
    EXPECT_EQ(front->Capture(),std::vector<std::uint32_t>(64*48,0xFF00FF));
    EXPECT_EQ(foreign->Capture(),std::vector<std::uint32_t>(96*80,0x2468AC));
    device->SetRenderTarget(nullptr);
}
TEST_F(Gap08bPresentedPixels, ForeignResizeRefreshesNullDestinationPhysicalExtent)
{
    CreateOther(); ASSERT_NE(foreign,nullptr);
    device->Clear(Color(255,0,0,255));
    device->Present(std::nullopt,std::nullopt,foreign->NativeToken());
    ASSERT_TRUE(foreign->WaitFor(*platform,[](const auto& rgb) { return All(rgb,96,80,0xFF0000); }));
    other->SetSize(112,88); other->Sync();
    ASSERT_EQ(other->GetPixelSize().width,112); ASSERT_EQ(other->GetPixelSize().height,88);
    foreign=CaptureClient(other->GetNativeHandle(),112,88);
    device->Clear(Color(0,255,0,255));
    device->Present(std::nullopt,std::nullopt,foreign->NativeToken());
    EXPECT_TRUE(foreign->WaitFor(*platform,[](const auto& rgb) { return All(rgb,112,88,0x00FF00); }));
    EXPECT_EQ(front->Capture(),std::vector<std::uint32_t>(64*48,0xFF00FF));
}
TEST_F(Gap08bPresentedPixels, MultisampledRegionResolvesBeforeScaling)
{
    if (!gl) GTEST_SKIP() << "DIRECTX9 currently exposes no multisampled backbuffer";
    const int applied=device->GetRenderer().ApplyMultiSampleCount(4);
    if (applied<=1) GTEST_SKIP() << "driver granted only one sample; not MSAA qualification";
    DrawQuadrants();
    // Equal saturated samples resolve to their literal endpoints, then scale2 independently.
    device->Present(XnaRectangle(24,18,24,18),XnaRectangle(16,8,48,36),0);
    EXPECT_TRUE(front->WaitFor(*platform,[](const auto& rgb) {
        return Outside(rgb,64,48,16,8,48,36,0xFF00FF) &&
               rgb[12*64+20]==0xFF0000 && rgb[12*64+60]==0x00FF00 &&
               rgb[40*64+20]==0x0000FF && rgb[22*64+36]==0xFFFFFF;
    }));
}
TEST_F(Gap08bPresentedPixels, VirtualSourceUsesLogicalTexelsWithinThePhysicalBackbuffer)
{
    DrawQuadrants();
    device->GetRenderer().SetVirtualResolution(32,24);
    // CNAEXT: logical32x24 maps to physical64x48. Logical bottom-right16x12 is physical
    // [32,64)x[24,48), literal W; using the raw logical rectangle on physical storage is R.
    device->Present(XnaRectangle(16,12,16,12),std::nullopt,0);
    EXPECT_TRUE(front->WaitFor(*platform,[](const auto& rgb) { return All(rgb,64,48,0xFFFFFF); }));
}
#else
TEST(Gap08bPresentedPixels, RequiresNativeCaptureSupport)
{
    GTEST_SKIP() << "requires private X11 or Win32 native client capture";
}
#endif
} // namespace
