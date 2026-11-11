// SPDX-License-Identifier: MS-PL
// FNA3D-8: what the FNA3D renderer forwards to the pinned fork's FNA3D_SwapBuffers, observed as
// native client pixels through the private X11 runner, and what it refuses by name.
#include "GraphicsDeviceGpuPresentFixture.hpp"
#include "System/NotSupportedException.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
namespace CNA::Testing::Gap08c {
#if defined(CNA_GAP08_X11_CAPTURE)
class Fna3dPresentPixels : public GpuPresentPixels
{
protected:
    bool AcceptsSelected(GraphicsRendererType type) const override { return type==GraphicsRendererType::Fna3d; }
    static std::string Image(const std::vector<std::uint32_t>& rgb,int w,int h)
    {
        std::ostringstream out;
        if(rgb.size()!=static_cast<std::size_t>(w)*h) return "capture has the wrong size";
        for(int y=0;y<h;++y)
        {
            out<<'\n';
            for(int x=0;x<w;++x)
            {
                char cell[8];
                std::snprintf(cell,sizeof(cell),"%06x ",static_cast<unsigned>(rgb[static_cast<std::size_t>(y)*w+x]));
                out<<cell;
            }
        }
        return out.str();
    }
    // FNA3D scales with its back-buffer filter, linear unless FNA3D_BACKBUFFER_SCALE_NEAREST is set.
    // At a source edge inside the back buffer that filter reads the neighbouring texel, so with scale
    // 4 the two outer client pixels on such an edge are blends; every check below stays outside them.
    static bool Inside(const std::vector<std::uint32_t>& rgb,int w,int left,int top,int right,int bottom,
                       std::uint32_t color)
    {
        for(int y=top;y<bottom;++y) for(int x=left;x<right;++x)
            if(rgb[static_cast<std::size_t>(y)*w+x]!=color) return false;
        return true;
    }
    static bool NoMagenta(const std::vector<std::uint32_t>& rgb)
    { return std::find(rgb.begin(),rgb.end(),0xFF00FFu)==rgb.end(); }
    // The configured driver, as the private runner's environment selects it.
    static std::string ExpectedDriverName()
    {
        const char* driver=std::getenv("FNA3D_FORCE_DRIVER");
        if(driver==nullptr) return {};
        const std::string name(driver);
        return name=="OpenGL" ? "OpenGL driver" : (name=="SDLGPU" || name=="Vulkan" ? "SDL GPU driver" : "");
    }
    void ExpectRefused(const std::optional<Rectangle>& source,const std::optional<Rectangle>& destination,
                       std::uintptr_t window,const char* subject)
    {
        try
        {
            device->Present(source,destination,window);
            ADD_FAILURE() << "FNA3D must refuse " << subject;
        }
        catch(const System::NotSupportedException& error)
        {
            const std::string message=error.what();
            EXPECT_NE(message.find("FNA3D-8"),std::string::npos) << message;
            EXPECT_NE(message.find(subject),std::string::npos) << message;
            const std::string driver=ExpectedDriverName();
            if(window==0 && !driver.empty())
                EXPECT_NE(message.find(driver),std::string::npos) << message;
        }
    }
};
TEST_F(Fna3dPresentPixels, SourceOnlyFillsTheClientWithTheSelectedQuadrant)
{
    DrawQuadrants();
    device->Present(Rectangle(4,4,4,4),std::nullopt,0);
    // Literal white source quadrant, null destination = all 16x16 client pixels.
    EXPECT_TRUE(front->WaitFor(*platform,[](const auto& rgb) {
        return NoMagenta(rgb) && Inside(rgb,16,2,2,16,16,0xFFFFFF);
    })) << Image(front->Capture(),16,16);
}
TEST_F(Fna3dPresentPixels, SourceOnlyFillsAnAsymmetricClientWithoutLetterboxing)
{
    DrawQuadrants();
    device->Present(Rectangle(0,0,8,4),std::nullopt,0);
    // Top half red/green stretched 2x4 over the whole client: no magenta bars.
    EXPECT_TRUE(front->WaitFor(*platform,[](const auto& rgb) {
        return NoMagenta(rgb) && rgb[1*16+2]==0xFF0000 && rgb[13*16+2]==0xFF0000 &&
               rgb[1*16+13]==0x00FF00 && rgb[13*16+13]==0x00FF00;
    })) << Image(front->Capture(),16,16);
}
TEST_F(Fna3dPresentPixels, WholeClientDestinationScalesTheCroppedSource)
{
    DrawQuadrants();
    device->Present(Rectangle(2,2,4,4),Rectangle(0,0,16,16),0);
    // Source boundary 4 maps to 0+(4-2)*16/4=8; the chosen centres sample flat texel pairs.
    EXPECT_TRUE(front->WaitFor(*platform,[](const auto& rgb) {
        return NoMagenta(rgb) && rgb[4*16+4]==0xFF0000 && rgb[4*16+12]==0x00FF00 &&
               rgb[12*16+4]==0x0000FF && rgb[12*16+12]==0xFFFFFF;
    })) << Image(front->Capture(),16,16);
}
TEST_F(Fna3dPresentPixels, NativeSizeBackbufferStillHonorsSource)
{
    device->GetRenderer().SetVirtualResolution(16,16);
    device->setViewportProperty(Viewport(0,0,16,16));
    device->Clear(Color(255,0,255,255)); device->Present();
    DrawQuadrants();
    device->Present(Rectangle(4,4,4,4),std::nullopt,0);
    // The 8x8 draw covers a quarter of the 16x16 back buffer; its white quadrant fills the client.
    EXPECT_TRUE(front->WaitFor(*platform,[](const auto& rgb) {
        return Inside(rgb,16,2,2,14,14,0xFFFFFF);
    })) << Image(front->Capture(),16,16);
}
TEST_F(Fna3dPresentPixels, MultisampleResolveFeedsSourcePresentation)
{
    device.reset(); MakeDevice(4);
    if(device->getPresentationParametersProperty().getMultiSampleCountProperty()<2)
        GTEST_SKIP() << "no multisample mode on this device";
    device->Clear(Color(255,0,255,255)); device->Present();
    ASSERT_TRUE(front->WaitFor(*platform,[](const auto& rgb) { return Solid(rgb,16,16,0xFF00FF); }));
    DrawQuadrants();
    device->Present(Rectangle(2,2,4,4),std::nullopt,0);
    EXPECT_TRUE(front->WaitFor(*platform,[](const auto& rgb) {
        return NoMagenta(rgb) && rgb[4*16+4]==0xFF0000 && rgb[4*16+12]==0x00FF00 &&
               rgb[12*16+4]==0x0000FF && rgb[12*16+12]==0xFFFFFF;
    })) << Image(front->Capture(),16,16);
}
TEST_F(Fna3dPresentPixels, PartialDestinationIsRefusedByNameAndPresentsNothing)
{
    // Neither pinned driver preserves the client outside a destination (the OpenGL driver leaves
    // it to the window's back buffer, the SDL GPU driver clears it), so none of these is submitted.
    const Rectangle destinations[]={Rectangle(4,4,8,8),Rectangle(-4,-4,8,8),Rectangle(12,12,8,8),
                                    Rectangle(0,0,16,15),Rectangle(-4,-4,24,24)};
    device->Clear(Color(255,0,0,255));
    for(const auto& destination:destinations)
    {
        SCOPED_TRACE(destination.ToString());
        ExpectRefused(std::nullopt,destination,0,"destination rectangle");
        ExpectRefused(Rectangle(0,0,8,8),destination,0,"destination rectangle");
    }
    platform->Delay(50);
    EXPECT_EQ(front->Capture(),std::vector<std::uint32_t>(256,0xFF00FF)) << Image(front->Capture(),16,16);
    device->Present(); // The refused requests left the device presenting normally.
    EXPECT_TRUE(front->WaitFor(*platform,[](const auto& rgb) { return Solid(rgb,16,16,0xFF0000); }))
        << Image(front->Capture(),16,16);
}
TEST_F(Fna3dPresentPixels, OverrideWindowIsRefusedByNameAndPresentsNothing)
{
    CreateOther(); ASSERT_FALSE(HasFatalFailure());
    foreign->Fill(0x2468AC);
    ASSERT_TRUE(foreign->WaitFor(*platform,[](const auto& rgb) { return Solid(rgb,24,20,0x2468AC); }));
    device->Clear(Color(255,0,0,255));
    ExpectRefused(std::nullopt,std::nullopt,foreign->NativeToken(),"override window");
    ExpectRefused(Rectangle(0,0,4,4),Rectangle(18,2,4,4),foreign->NativeToken(),"override window");
    platform->Delay(50);
    EXPECT_EQ(foreign->Capture(),std::vector<std::uint32_t>(480,0x2468AC));
    EXPECT_EQ(front->Capture(),std::vector<std::uint32_t>(256,0xFF00FF));
}
#else
TEST(Fna3dPresentPixels, RequiresPrivateNativeCapture)
{
    GTEST_SKIP() << "FNA3D presented-pixel witnesses need the X11 capture headers and the private runner";
}
#endif
} // namespace CNA::Testing::Gap08c
