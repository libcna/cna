// SPDX-License-Identifier: MS-PL
// cna-engine UP-018: exercise the public constructor, raster storage and readback.
#include <gtest/gtest.h>
#include <array>
#include <bit>
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
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

TEST(SingleTargetPrecision, DefaultReachReportsColorSubstitutionForPreferredSingle)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(CNA::GraphicsRendererType::Software,
                                   CNA::GraphicsRendererType::OpenGLES3,
                                   CNA::GraphicsRendererType::OpenGL33);
    GraphicsDevice device;
    ASSERT_EQ(device.getGraphicsProfileProperty(), GraphicsProfile::Reach);
    RenderTarget2D target(device, 64, 64, false, SurfaceFormat::Single, DepthFormat::Depth24);
    // The constructor takes a preferred format, not an exact-format requirement.
    // Reading a Color surface into float does not convert RGBA bytes to intensity.
    ASSERT_EQ(target.getFormatProperty(), SurfaceFormat::Color);
    const Rectangle texel(0, 0, 1, 1);
    for (Color clear : {Color(128, 128, 128, 255), Color::White})
    {
        SCOPED_TRACE(clear.getPackedValueProperty());
        device.SetRenderTarget(&target);
        device.Clear(clear);
        device.SetRenderTarget(nullptr);
        float raw = 0;
        target.GetData(0, &texel, &raw, 0, 1);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(raw), clear.getPackedValueProperty());
        Color color;
        target.GetData(0, &texel, &color, 0, 1);
        EXPECT_EQ(color, clear);
        if (clear == Color::White)
            EXPECT_TRUE(std::isnan(raw));
    }
}

TEST(SingleTargetPrecision, HiDefDepth24ClearAndIndexedFogDrawKeepFloatDepth)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(CNA::GraphicsRendererType::Software,
                                   CNA::GraphicsRendererType::OpenGLES3,
                                   CNA::GraphicsRendererType::OpenGL33);
    GraphicsDevice device;
    device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    RenderTarget2D target(device, 64, 64, false, SurfaceFormat::Single, DepthFormat::Depth24);
    ASSERT_EQ(target.getFormatProperty(), SurfaceFormat::Single);
    device.setBlendStateProperty(BlendState::Opaque);
    device.setDepthStencilStateProperty(DepthStencilState::Default);
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    const Rectangle texel(0, 0, 1, 1);
    for (Color clear : {Color(128, 128, 128, 255), Color::White})
    {
        device.SetRenderTarget(&target);
        device.Clear(clear);
        device.SetRenderTarget(nullptr);
        float value = 0;
        target.GetData(0, &texel, &value, 0, 1);
        EXPECT_FLOAT_EQ(value, clear.getRProperty() / 255.0f);
        std::array<float, 64 * 64> pixels{};
        target.GetData(pixels.data(), static_cast<int>(pixels.size()));
        for (float pixel : pixels)
            ASSERT_FLOAT_EQ(pixel, clear.getRProperty() / 255.0f);
    }

    BasicEffect effect(device);
    effect.setLightingEnabledProperty(false);
    effect.setTextureEnabledProperty(false);
    effect.setVertexColorEnabledProperty(false);
    effect.setDiffuseColorProperty(Vector3(0.5f));
    effect.setFogEnabledProperty(true);
    effect.setFogColorProperty(Vector3(1));
    effect.setFogStartProperty(0);
    effect.setFogEndProperty(4);
    effect.setProjectionProperty(Matrix::CreateOrthographic(2, 2, 0, 4));
    const std::array<VertexPositionColor, 3> triangle{{
        {Vector3(-1, -1, 0), Color::White},
        {Vector3(3, -1, 0), Color::White},
        {Vector3(-1, 3, 0), Color::White}}};
    VertexBuffer vertices(device, VertexPositionColor::getVertexDeclarationStatic(), 3,
                          BufferUsage::None);
    vertices.SetData(triangle.data(), static_cast<int>(triangle.size()));
    IndexBuffer indices(device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    const std::array<std::uint16_t, 3> indexData{0, 1, 2};
    indices.SetData(indexData.data(), static_cast<int>(indexData.size()));
    device.SetVertexBuffer(&vertices);
    device.SetIndexBuffer(&indices);
    for (float nearest : {1.0f, 1.0f + 1.0f / 512.0f})
    {
        SCOPED_TRACE(nearest);
        device.SetRenderTarget(&target);
        device.Clear(Color::White);
        // Far -> near -> far proves the depth buffer retains the nearest caster.
        for (float distance : {3.0f, nearest, 3.0f})
        {
            effect.setWorldProperty(Matrix::CreateTranslation(0, 0, -distance));
            effect.Apply();
            device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 3, 0, 1);
        }
        device.SetRenderTarget(nullptr);
        const float expected = 0.5f + nearest / 8.0f;
        std::array<float, 64 * 64> pixels{};
        target.GetData(pixels.data(), static_cast<int>(pixels.size()));
        for (float pixel : pixels)
            ASSERT_FLOAT_EQ(pixel, expected);
        float first = 0;
        target.GetData(0, &texel, &first, 0, 1);
        EXPECT_FLOAT_EQ(first, expected);
    }
}

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
