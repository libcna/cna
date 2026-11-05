// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <typeinfo>
#include <vector>
#include "CNA/RendererTestGate.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    PresentationParameters Parameters(int width = 8, int height = 6)
    {
        auto p = PresentationParameters::CreateWindowedEXT(800, 480);
        p.setBackBufferWidthProperty(width);
        p.setBackBufferHeightProperty(height);
        p.setBackBufferFormatProperty(SurfaceFormat::Color);
        p.setMultiSampleCountProperty(0);
        return p;
    }
    template<class E, class F>
    void Argument(const char* parameter, const char* body, F action)
    {
        try { action(); ADD_FAILURE() << "accepted invalid call"; }
        catch (const E& e)
        {
            EXPECT_EQ(typeid(e), typeid(E));
            EXPECT_EQ(e.getParamNameProperty(), parameter);
            std::string text = e.getMessageProperty();
            auto end = text.find(" (Parameter ");
            if (end == std::string::npos) end = text.find("\nParameter name:");
            EXPECT_EQ(text.substr(0, end), body);
        }
    }
    template<class E, class F>
    void Refusal(const char* body, F action)
    {
        try { action(); ADD_FAILURE() << "accepted invalid call"; }
        catch (const E& e)
        {
            EXPECT_EQ(typeid(e), typeid(E));
            EXPECT_EQ(std::string(e.what()), body);
        }
    }
}

#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"

TEST(GraphicsDeviceBackbufferPixelsRefreshTest, AllColorOverloadsAndRawRectanglePreserveRowsAndElementWindows)
{
    // Pixel qualification is for the owner's private runner. Unsupported renderers do not supply an oracle.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(CNA::GraphicsRendererType::Software, CNA::GraphicsRendererType::OpenGL33,
        CNA::GraphicsRendererType::OpenGLES3, CNA::GraphicsRendererType::WebGPU, CNA::GraphicsRendererType::SdlGpu,
        CNA::GraphicsRendererType::Fna3d);
    auto parameters = Parameters(8,6);
    // Refresh readback IL_01da-01ee marks DiscardContents dirty after a successful read.
    // This repeated-overload comparison requests preservation explicitly.
    parameters.setRenderTargetUsageProperty(RenderTargetUsage::PreserveContents);
    GraphicsDevice d(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef, parameters);
    Texture2D white(d,1,1);
    const Color solid = Color::White;
    white.SetData(&solid,1);
    SpriteBatch sprites(d);
    d.Clear(Color(0,0,0,255));
    sprites.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &SamplerState::PointClamp,
                  &DepthStencilState::None, &RasterizerState::CullNone);
    // Hand-derived: opaque 1x1 white * tint gives exact channel bytes, no filtering or blending.
    // 8x6 image: top-left (11,22,33,255), top-right (44,55,66,255),
    // bottom-left (77,88,99,255), bottom-right (111,122,133,255).
    const std::array<Color,4> quadrants{Color(11,22,33,255),Color(44,55,66,255),
                                       Color(77,88,99,255),Color(111,122,133,255)};
    sprites.Draw(white, Rectangle(0,0,4,3), quadrants[0]);
    sprites.Draw(white, Rectangle(4,0,4,3), quadrants[1]);
    sprites.Draw(white, Rectangle(0,3,4,3), quadrants[2]);
    sprites.Draw(white, Rectangle(4,3,4,3), quadrants[3]);
    sprites.End();
    // Read the entire buffer despite a non-default viewport/scissor.
    d.setViewportProperty(Viewport(1,1,2,2));
    d.setScissorRectangleProperty(Rectangle(2,2,1,1));
    auto expected = [&](int x,int y) { return quadrants[(y>=3?2:0)+(x>=4?1:0)]; };
    std::array<Color,48> full{};
    d.GetBackBufferData(full.data(),48);
    for (int y=0;y<6;++y) for(int x=0;x<8;++x) EXPECT_EQ(full[y*8+x],expected(x,y));
    std::array<Color,52> offset{}; offset.fill(Color::Magenta);
    d.GetBackBufferData(offset.data(),2,48);
    EXPECT_EQ(offset[0],Color::Magenta); EXPECT_EQ(offset[1],Color::Magenta);
    EXPECT_EQ(offset[50],Color::Magenta); EXPECT_EQ(offset[51],Color::Magenta);
    for (int y=0;y<6;++y) for(int x=0;x<8;++x) EXPECT_EQ(offset[2+y*8+x],expected(x,y));
    // Refresh GetBackBufferData IL_016a-01c5; Texture.CopyData IL_0007-0028/0092-00ea:
    // destination uses startIndex*sizeof(T), rows advance by row bytes; Color channels are RGBA
    // after the A8R8G8B8 swap (CopyData IL_00a3-00bb).
    // (3,2,2,2) crosses both boundaries, so expected row order is Q0,Q1,Q2,Q3.
    const Rectangle cross(3,2,2,2);
    std::array<Color,8> selected{}; selected.fill(Color::Magenta);
    d.GetBackBufferData(&cross,selected.data(),2,4);
    for(int i=0;i<8;++i) EXPECT_EQ(selected[i],i>=2&&i<6?quadrants[i-2]:Color::Magenta);
    struct Rgba { std::uint8_t bytes[4]; };
    std::array<Rgba,8> raw{};
    for(auto& p:raw) for(auto& byte:p.bytes) byte=0xCD;
    d.GetBackBufferData(&cross,raw.data(),2,4);
    const std::uint8_t literal[16]={11,22,33,255,44,55,66,255,77,88,99,255,111,122,133,255};
    for(int i=0;i<8;++i) for(int b=0;b<4;++b)
        EXPECT_EQ(raw[i].bytes[b],i>=2&&i<6?literal[(i-2)*4+b]:0xCD);
    EXPECT_EQ(d.getScissorRectangleProperty(),Rectangle(2,2,1,1));
    EXPECT_EQ(d.getViewportProperty().getWidthProperty(),2);
}
