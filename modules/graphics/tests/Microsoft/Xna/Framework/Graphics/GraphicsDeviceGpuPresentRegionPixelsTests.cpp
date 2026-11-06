// SPDX-License-Identifier: MS-PL
#include "GraphicsDeviceGpuPresentFixture.hpp"
namespace CNA::Testing::Gap08c {
#if defined(CNA_GAP08_X11_CAPTURE)
TEST_F(GpuPresentPixels, SourceOnlyFillsSelectedClientWithWhiteQuadrant)
{
    DrawQuadrants();
    device->Present(Rectangle(4,4,4,4),std::nullopt,0);
    // Literal bottom-right source is white; null destination fills all 16x16 pixels.
    EXPECT_TRUE(front->WaitFor(*platform,[](const auto& rgb) { return Solid(rgb,16,16,0xFFFFFF); }));
}
TEST_F(GpuPresentPixels, SourceOnlyFillsAnAsymmetricClientWithoutLetterboxing)
{
    DrawQuadrants();
    device->Present(Rectangle(0,0,8,4),std::nullopt,0);
    // Top half is red/green; destination aspect does not introduce magenta bars.
    EXPECT_TRUE(front->WaitFor(*platform,[](const auto& rgb) {
        return rgb[1*16+2]==0xFF0000 && rgb[14*16+2]==0xFF0000 &&
               rgb[1*16+13]==0x00FF00 && rgb[14*16+13]==0x00FF00;
    }));
}
TEST_F(GpuPresentPixels, DestinationOnlyPreservesAllOutsidePixels)
{
    DrawQuadrants();
    device->Present(std::nullopt,Rectangle(4,4,8,8),0);
    // Source 8 -> destination 8; every outside pixel stays ordinary-control magenta.
    EXPECT_TRUE(front->WaitFor(*platform,[](const auto& rgb) {
        return Outside(rgb,16,16,4,4,8,8,0xFF00FF) && QuadrantCenters(rgb,16);
    }));
}
TEST_F(GpuPresentPixels, CroppedSourceScalesFourQuadrantsIntoDestination)
{
    DrawQuadrants();
    device->Present(Rectangle(2,2,4,4),Rectangle(4,4,8,8),0);
    // Source boundary 4 maps to 8 on each axis: 4+(4-2)*8/4=8.
    // Chosen centers stay inside flat-color regions even with linear filtering.
    EXPECT_TRUE(front->WaitFor(*platform,[](const auto& rgb) {
        return Outside(rgb,16,16,4,4,8,8,0xFF00FF) && QuadrantCenters(rgb,16);
    }));
}
TEST_F(GpuPresentPixels, NegativeDestinationClipsWithoutChangingTheAffineMap)
{
    DrawQuadrants();
    device->Present(Rectangle(0,0,8,8),Rectangle(-4,-4,8,8),0);
    // sx=dx+4,sy=dy+4: visible [0,4)^2 is entirely the white source quadrant.
    // This pins the shared CNA contract; native Refresh clipping qualification is separate.
    EXPECT_TRUE(front->WaitFor(*platform,[](const auto& rgb) {
        if(!Outside(rgb,16,16,0,0,4,4,0xFF00FF)) return false;
        return rgb[1*16+1]==0xFFFFFF && rgb[2*16+2]==0xFFFFFF;
    }));
}
TEST_F(GpuPresentPixels, RightBottomClippingRetainsTheOriginalScale)
{
    DrawQuadrants();
    device->Present(Rectangle(0,0,8,8),Rectangle(12,12,8,8),0);
    // Visible [12,16)^2 maps to source [0,4)^2, red; clipping then stretching is wrong.
    EXPECT_TRUE(front->WaitFor(*platform,[](const auto& rgb) {
        return Outside(rgb,16,16,12,12,4,4,0xFF00FF) && rgb[13*16+13]==0xFF0000 && rgb[14*16+14]==0xFF0000;
    }));
}
TEST_F(GpuPresentPixels, OversizedSourceUsesSharedClippedIntersection)
{
    DrawQuadrants();
    device->Present(Rectangle(6,6,8,8),Rectangle(4,4,8,8),0);
    // CNA adaptation: [6,14) intersect [0,8) is [6,8), literal white.
    EXPECT_TRUE(front->WaitFor(*platform,[](const auto& rgb) {
        if(!Outside(rgb,16,16,4,4,8,8,0xFF00FF)) return false;
        for(int y=4;y<12;++y) for(int x=4;x<12;++x) if(rgb[y*16+x]!=0xFFFFFF) return false;
        return true;
    }));
}
TEST_F(GpuPresentPixels, ConsecutivePartialPresentsRetainEarlierDestinationPixels)
{
    // More than one swapchain image: every earlier update must survive every later swap.
    const Color colors[4]={Color(255,0,0,255),Color(0,255,0,255),Color(0,0,255,255),Color(255,255,255,255)};
    const std::uint32_t rgbColors[4]={0xFF0000,0x00FF00,0x0000FF,0xFFFFFF};
    for(int frame=0;frame<4;++frame)
    {
        device->Clear(colors[frame]);
        device->Present(std::nullopt,Rectangle(frame*4,4,4,8),0);
        EXPECT_TRUE(front->WaitFor(*platform,[&](const auto& rgb) {
            for(int y=0;y<16;++y) for(int x=0;x<16;++x)
            {
                const auto expected=(y>=4 && y<12 && x/4<=frame)?rgbColors[x/4]:0xFF00FF;
                if(rgb[y*16+x]!=expected) return false;
            }
            return true;
        })) << "presentation frame " << frame;
    }
}
TEST_F(GpuPresentPixels, OrdinaryPresentAfterRegionReplacesTheWholeClient)
{
    DrawQuadrants(); device->Present(Rectangle(4,4,4,4),Rectangle(4,4,8,8),0);
    ASSERT_TRUE(front->WaitFor(*platform,[](const auto& rgb) {
        return Outside(rgb,16,16,4,4,8,8,0xFF00FF) && rgb[6*16+6]==0xFFFFFF;
    }));
    device->Clear(Color(0,0,255,255)); device->Present();
    EXPECT_TRUE(front->WaitFor(*platform,[](const auto& rgb) { return Solid(rgb,16,16,0x0000FF); }));
    device->Clear(Color(255,0,0,255));
    device->Present(std::nullopt,Rectangle(4,4,8,8),0);
    EXPECT_TRUE(front->WaitFor(*platform,[](const auto& rgb) {
        return Outside(rgb,16,16,4,4,8,8,0x0000FF) && rgb[6*16+6]==0xFF0000;
    }));
}
TEST_F(GpuPresentPixels, NativeSizeBackbufferStillHonorsSource)
{
    device->GetRenderer().SetVirtualResolution(16,16);
    device->setViewportProperty(Viewport(0,0,16,16));
    device->Clear(Color(255,0,255,255)); device->Present();
    DrawQuadrants();
    device->Present(Rectangle(4,4,4,4),std::nullopt,0);
    // Draw occupies 8x8 of 16x16; chosen source remains entirely white.
    EXPECT_TRUE(front->WaitFor(*platform,[](const auto& rgb) { return Solid(rgb,16,16,0xFFFFFF); }));
}
TEST_F(GpuPresentPixels, MultisampleResolveFeedsRegionPresentation)
{
    device.reset(); MakeDevice(4);
    if(device->getPresentationParametersProperty().getMultiSampleCountProperty()<2)
        GTEST_SKIP() << "no multisample mode on this device";
    device->Clear(Color(255,0,255,255)); device->Present();
    ASSERT_TRUE(front->WaitFor(*platform,[](const auto& rgb) { return Solid(rgb,16,16,0xFF00FF); }));
    DrawQuadrants(); // Actual draws, not a Clear that could initialize both stores independently.
    device->Present(Rectangle(2,2,4,4),Rectangle(4,4,8,8),0);
    EXPECT_TRUE(front->WaitFor(*platform,[](const auto& rgb) {
        return Outside(rgb,16,16,4,4,8,8,0xFF00FF) && QuadrantCenters(rgb,16);
    }));
}
#else
TEST(GpuPresentPixels, RequiresPrivateNativeCapture)
{
    const auto selected=GraphicsRendererSelection::GetSelected();
    if(selected==GraphicsRendererType::SdlGpu || selected==GraphicsRendererType::WebGPU)
        FAIL() << "active GPU renderer has no compiled native capture method; X11 capture headers are required";
    else GTEST_SKIP() << "GAP-08c requires SDLGPU or WEBGPU";
}
#endif
} // namespace CNA::Testing::Gap08c
