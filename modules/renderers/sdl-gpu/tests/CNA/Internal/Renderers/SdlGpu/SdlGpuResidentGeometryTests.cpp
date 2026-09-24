// SPDX-License-Identifier: MS-PL
//
// plans/plan_street_perf.md STREETPERF-0002: this renderer replays draws at Present(), and used to
// keep each draw's geometry by copying its buffers' whole contents into the draw and uploading
// them again at the end of the frame -- ~430 MB a frame in cna-street, whose scene holds 71 MiB.
// A draw now binds its vertex and index buffers' own GPU storage; only bytes the draw carries
// itself (per-instance streams, the neutral record, bone palettes, rewritten geometry) are staged.
// BufferRewriteWithinFrameTests holds the other half: that a rewritten buffer still draws right.

#if defined(CNA_RENDERER_SDL_GPU)

#include "CNA/Internal/Renderers/SdlGpu/SdlGpuRenderer.hpp"
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
    using CNA::Internal::Renderers::SdlGpu::SdlGpuRenderer;
}

TEST(SdlGpuResidentGeometry, DrawsFromStaticBuffersStageNoGeometry)
{
    GraphicsDevice device;
    auto* renderer = dynamic_cast<SdlGpuRenderer*>(&device.GetRenderer());
    if (renderer == nullptr) GTEST_SKIP() << "this run did not select the SDL GPU renderer";

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
    EXPECT_EQ(renderer->GetLastSceneUploadBytesEXT(), 0u)
        << "32 draws from two unchanged buffers staged their geometry again";
}

#endif
