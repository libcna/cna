// SPDX-License-Identifier: MS-PL
//
// plans/plan_apple_m4.md AM4-155: raw integer vertex elements (Byte4, Short2, Short4) feeding float
// inputs, which Direct3D 9 converts to float. software_vertex_declaration_test covers every format
// feeding COLOR0, where saturation hides the magnitude; a position shows it.

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "CNA/RendererTestGate.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using namespace Microsoft::Xna::Framework::Graphics;

TEST(IntegerVertexElementTest, Short2PositionsArriveAsTheirIntegerValues)
{
    using namespace CNA::Testing::Renderers;   // NOLINT(google-build-using-namespace)
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Metal);

    struct ShortPositionVertex
    {
        std::int16_t x, y;
        std::uint8_t r, g, b, a;
    };
    static_assert(sizeof(ShortPositionVertex) == 8, "Short2 position + Color");
    GraphicsDevice device;
    constexpr int kSize = 8;
    RenderTarget2D target(device, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::None);
    const VertexDeclaration declaration(8, {
        VertexElement(0, VertexElementFormat::Short2, VertexElementUsage::Position, 0),
        VertexElement(4, VertexElementFormat::Color, VertexElementUsage::Color, 0)});
    const std::vector<ShortPositionVertex> quad{
        {0, 1, 255, 0, 0, 255}, {1, 1, 255, 0, 0, 255}, {0, 0, 255, 0, 0, 255},
        {1, 1, 255, 0, 0, 255}, {1, 0, 255, 0, 0, 255}, {0, 0, 255, 0, 0, 255}};
    VertexBuffer buffer(device, declaration, static_cast<int>(quad.size()), BufferUsage::None);
    buffer.SetData(quad.data(), static_cast<int>(quad.size()));

    Microsoft::Xna::Framework::Graphics::BasicEffect effect(device);
    effect.VertexColorEnabled = true;
    device.SetRenderTarget(&target);
    device.Clear(Color(0, 255, 0, 255));
    device.setBlendStateProperty(BlendState::Opaque);
    device.setDepthStencilStateProperty(DepthStencilState::None);
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    effect.Apply();
    device.SetVertexBuffer(&buffer);
    device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
    device.SetVertexBuffer(nullptr);
    device.SetRenderTarget(nullptr);

    const auto read = [&target](int x, int y) {
        Color pixel(0, 0, 0, 0);
        const Rectangle region(x, y, 1, 1);
        target.GetData(0, &region, &pixel, 0, 1);
        return pixel;
    };
    const Color inside = read(6, 1);    // top-right quarter
    const Color outside = read(1, 6);   // bottom-left quarter
    EXPECT_GT(inside.getRProperty(), inside.getGProperty() + 40) << "the quad must cover x,y in (0,1)";
    EXPECT_GT(outside.getGProperty(), outside.getRProperty() + 40) << "and nothing outside it";
}
