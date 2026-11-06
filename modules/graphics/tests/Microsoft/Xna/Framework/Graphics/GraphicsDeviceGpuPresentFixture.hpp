// SPDX-License-Identifier: MS-PL
#pragma once
#include <gtest/gtest.h>
#include "CNA/GraphicsRendererSelection.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Platform/PlatformFactory.hpp"
#include "CNA/Platform/PlatformTestDecorator.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "PresentedWindowCapture.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace CNA::Testing::Gap08c {
using namespace CNA::Platform;
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
#if defined(CNA_GAP08_X11_CAPTURE)
// Observations are actual native client pixels after presentation. The independent
// texture literals and affine arithmetic are the pixel oracle. Refresh Present
// IL_0014-0082 forwards rectangles/ HWND; native filtering/clipping is not in IL.
class GpuPresentPixels : public ::testing::Test
{
protected:
    void SetUp() override
    {
        selected=GraphicsRendererSelection::GetSelected();
        if(selected!=GraphicsRendererType::SdlGpu && selected!=GraphicsRendererType::WebGPU)
            GTEST_SKIP() << "GAP-08c requires SDLGPU or WEBGPU";
        platform=PlatformFactory::Create("SDL3");
        scope=std::make_unique<CNA::Platform::Testing::ScopedCurrentPlatform>(*platform);
        platform->AcquireSubsystem(PlatformSubsystem::Video);
        acquired=true;
        WindowDescription description;
        description.title="GAP-08c GPU presentation client";
        description.width=16; description.height=16;
        description.centered=false; description.x=160; description.y=32;
        description.borderless=true; description.visible=true;
        own=platform->CreateWindow(description);
        own->Sync();
        // The designated private X11 runner must execute these cases without skips.
        ASSERT_EQ(own->GetNativeHandle().system,NativeWindowSystem::X11)
            << "active GPU renderer has no native capture transport; use the private X11 runner";
        ASSERT_EQ(own->GetPixelSize().width,16);
        ASSERT_EQ(own->GetPixelSize().height,16);
        // Redirect before swapchain creation/first presentation, not after pixels become needed.
        front=std::make_unique<PresentedWindow>(own->GetNativeHandle(),16,16,false,PresentedCaptureMethod::Auto);
        MakeDevice();
        ASSERT_EQ(device->GetGraphicsRendererType(),selected) << "renderer fallback invalidates evidence";
        // Device construction applies PP dimensions to its attached window. Check AFTER that
        // operation too, so an oversized GetImage cannot masquerade as an unsupported GPU capture.
        own->Sync();
        ASSERT_EQ(own->GetPixelSize().width,16);
        ASSERT_EQ(own->GetPixelSize().height,16);
        int logicalWidth=0,logicalHeight=0;
        device->GetRenderer().GetViewportSize(logicalWidth,logicalHeight);
        ASSERT_EQ(logicalWidth,8); ASSERT_EQ(logicalHeight,8);
        // Ordinary presentation must seed history BEFORE the first region request.
        device->Clear(Color(255,0,255,255)); device->Present();
        std::string captureDiagnostic;
        ASSERT_TRUE(front->ProbeCapture(captureDiagnostic))
            << "no working native capture method for " << device->GetGraphicsRendererName()
            << ": " << captureDiagnostic;
        ASSERT_TRUE(front->WaitFor(*platform,[](const auto& rgb) { return Solid(rgb,16,16,0xFF00FF); }))
            << "ordinary Present control must reach the client: " << front->Diagnostics();
    }
    void TearDown() override
    {
        device.reset(); // claims/surfaces/history released before borrowed platform wrappers/hosts
        foreign.reset(); other.reset(); front.reset(); own.reset();
        if(acquired) platform->ReleaseSubsystem(PlatformSubsystem::Video);
        scope.reset(); platform.reset();
    }
    void MakeDevice(int samples=0)
    {
        // PP sizes the physical attached client; SetVirtualResolution below independently
        // establishes the 8x8 source. PP8 previously resized the 16x16 client to 8x8 in SetUp.
        auto p=PresentationParameters::CreateWindowedEXT(16,16);
        p.setDeviceWindowHandleProperty(own->GetWindowHandle());
        p.setPresentationIntervalProperty(PresentInterval::Immediate);
        p.setMultiSampleCountProperty(samples);
        device=std::make_unique<GraphicsDevice>(GraphicsAdapter::getDefaultAdapterProperty(),GraphicsProfile::HiDef,p);
        device->GetRenderer().SetVirtualResolution(8,8);
        device->setViewportProperty(Viewport(0,0,8,8));
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
        sprites.Begin(SpriteSortMode::Immediate,&BlendState::Opaque,&SamplerState::PointClamp,nullptr,nullptr);
        sprites.Draw(texture,Rectangle(0,0,8,8),Color::White);
        sprites.End();
    }
    static bool Solid(const std::vector<std::uint32_t>& rgb,int w,int h,std::uint32_t color)
    { return rgb==std::vector<std::uint32_t>(static_cast<std::size_t>(w)*h,color); }
    static bool Outside(const std::vector<std::uint32_t>& rgb,int w,int h,
                        int left,int top,int width,int height,std::uint32_t color)
    {
        if(rgb.size()!=static_cast<std::size_t>(w)*h) return false;
        for(int y=0;y<h;++y) for(int x=0;x<w;++x)
            if((x<left || y<top || x>=left+width || y>=top+height) && rgb[y*w+x]!=color) return false;
        return true;
    }
    static bool QuadrantCenters(const std::vector<std::uint32_t>& rgb,int w)
    {
        return rgb[5*w+5]==0xFF0000 && rgb[5*w+10]==0x00FF00 &&
               rgb[10*w+5]==0x0000FF && rgb[10*w+10]==0xFFFFFF;
    }
    void CreateOther()
    {
        WindowDescription description;
        description.title="GAP-08c second GPU client";
        description.width=24; description.height=20;
        description.centered=false; description.x=240; description.y=32;
        description.borderless=true; description.visible=true;
        other=platform->CreateWindow(description); other->Sync();
        ASSERT_EQ(other->GetPixelSize().width,24); ASSERT_EQ(other->GetPixelSize().height,20);
        foreign=std::make_unique<PresentedWindow>(other->GetNativeHandle(),24,20);
    }
    void SeedOther()
    {
        // Establish a prior native frame; a separate test covers the unseeded external host.
        device->Clear(Color(36,104,172,255));
        device->Present(std::nullopt,std::nullopt,foreign->NativeToken());
        ASSERT_TRUE(foreign->WaitFor(*platform,[](const auto& rgb) { return Solid(rgb,24,20,0x2468AC); }));
    }
    GraphicsRendererType selected=GraphicsRendererType::Headless;
    std::unique_ptr<IPlatform> platform;
    std::unique_ptr<CNA::Platform::Testing::ScopedCurrentPlatform> scope;
    std::unique_ptr<IPlatformWindow> own,other;
    std::unique_ptr<GraphicsDevice> device;
    std::unique_ptr<PresentedWindow> front,foreign;
    bool acquired=false;
};
#endif
} // namespace CNA::Testing::Gap08c
