// SPDX-License-Identifier: MS-PL
//
// plans/plan_street_perf.md STREETPERF-0003: this renderer replays draws at Present(), and every
// draw used to copy its whole vertex buffer and its indices into the draw, and the replay copied
// them again into the frame's arena. A draw now binds its buffers' own VkBuffers; a SetData on a
// buffer a draw has bound moves it to a fresh VkBuffer and retires the old one on the frame fence.
// BufferRewriteWithinFrameTests holds the other half: that a rewritten buffer still draws right.

#if defined(CNA_RENDERER_VULKAN)

#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

namespace
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;
    using CNA::Internal::Renderers::Vulkan::VulkanRenderer;
}

TEST(VulkanResidentGeometry, DrawsFromStaticBuffersCopyNoGeometry)
{
    GraphicsDevice device;
    auto* renderer = dynamic_cast<VulkanRenderer*>(&device.GetRenderer());
    if (renderer == nullptr) GTEST_SKIP() << "this run did not select the Vulkan renderer";

    RenderTarget2D target(device, 8, 8);
    const Color color = Color::Red;
    const std::array<VertexPositionColor, 4> corners{
        VertexPositionColor(Vector3(-1.0f, -1.0f, 0.0f), color),
        VertexPositionColor(Vector3(-1.0f, 1.0f, 0.0f), color),
        VertexPositionColor(Vector3(1.0f, 1.0f, 0.0f), color),
        VertexPositionColor(Vector3(1.0f, -1.0f, 0.0f), color)};
    VertexBuffer vertices(device, VertexPositionColor::getVertexDeclarationStatic(), 4,
                          BufferUsage::WriteOnly);
    vertices.SetData(corners.data(), 4);
    IndexBuffer indices(device, IndexElementSize::SixteenBits, 6, BufferUsage::WriteOnly);
    const std::array<std::uint16_t, 6> quad{0, 1, 2, 0, 2, 3};
    indices.SetData(quad.data(), 6);

    BasicEffect effect(device);
    effect.setVertexColorEnabledProperty(true);
    effect.setWorldProperty(Matrix::getIdentityProperty());
    effect.setViewProperty(Matrix::getIdentityProperty());
    effect.setProjectionProperty(Matrix::getIdentityProperty());

    device.SetRenderTarget(&target);
    device.setBlendStateProperty(BlendState::Opaque);
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertices);
    device.setIndicesProperty(&indices);
    effect.Apply();
    for (int draw = 0; draw < 16; ++draw)
    {
        device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
    }
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    std::vector<Color> pixels(64);
    target.GetData(pixels.data(), 64);   // the flush that replays and uploads

    EXPECT_EQ(pixels[27], color) << "the draws did not reach the target";
    EXPECT_EQ(renderer->GetLastArenaGeometryBytesEXT(), 0u)
        << "32 draws from two unchanged buffers copied their geometry into the frame arena";
}

#endif
