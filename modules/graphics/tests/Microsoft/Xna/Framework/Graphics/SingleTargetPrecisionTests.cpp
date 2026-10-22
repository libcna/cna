// SPDX-License-Identifier: MS-PL
// cna-engine UP-018: exercise the public constructor, raster storage and readback.
#include <gtest/gtest.h>
#include <array>
#include <cmath>
#include "CNA/RendererTestGate.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

TEST(SingleTargetPrecision, PublicDrawAndReadbackKeepSubByteDepthDifferences)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(CNA::GraphicsRendererType::Software,
                                   CNA::GraphicsRendererType::OpenGLES3,
                                   CNA::GraphicsRendererType::OpenGL33);
    GraphicsDevice device;
    device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    ASSERT_TRUE(device.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Single));
    RenderTarget2D target(device, 64, 64, false, SurfaceFormat::Single, DepthFormat::None);
    ASSERT_EQ(target.getFormatProperty(), SurfaceFormat::Single);
    device.setBlendStateProperty(BlendState::Opaque);
    device.setDepthStencilStateProperty(DepthStencilState::None);
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    BasicEffect effect(device);
    effect.setLightingEnabledProperty(false);
    effect.setTextureEnabledProperty(false);
    effect.setVertexColorEnabledProperty(false);
    const VertexPositionColor triangle[]{
        {Vector3(-1, -1, 0), Color::White},
        {Vector3(3, -1, 0), Color::White},
        {Vector3(-1, 3, 0), Color::White}};
    for (float value : {0.5f, 0.5f + 1.0f / 4096.0f, 1.0f})
    {
        SCOPED_TRACE(value);
        device.SetRenderTarget(&target);
        device.Clear(Color::Black);
        effect.setDiffuseColorProperty(Vector3(value));
        effect.Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, triangle, 0, 1);
        device.SetRenderTarget(nullptr);
        std::array<float, 64 * 64> pixels{};
        target.GetData(pixels.data(), static_cast<int>(pixels.size()));
        const Rectangle region(16, 32, 1, 1);
        float texel = -1;
        target.GetData(0, &region, &texel, 0, 1);
        EXPECT_FLOAT_EQ(texel, value);
        // Interior pixels avoid edge-fill differences; a float precision assertion
        // distinguishes both values from the same 8-bit red quantization bucket.
        for (int y : {16, 32, 48})
            for (int x : {16, 32, 48})
            {
                EXPECT_TRUE(std::isfinite(pixels[y * 64 + x]));
                EXPECT_FLOAT_EQ(pixels[y * 64 + x], value);
            }
    }
}
