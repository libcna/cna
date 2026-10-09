// SPDX-License-Identifier: MS-PL
// plans/plan_samples_multirenderer.md MSR-034: the XNA Primitives sample represents each star
// with a one-pixel clockwise triangle in an orthographic, top-left-origin projection. Keep that
// real application contract distinct from larger triangles that can hide a winding or pixel-centre
// mismatch.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>

#include "CNA/GraphicsCapability.hpp"
#include "CNA/RendererTestGate.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kTargetSize = 16;

    int RenderTriangle(const std::array<VertexPositionColor, 3>& vertices,
                       const RasterizerState& rasterizerState)
    {
        GraphicsDevice device;
        RenderTarget2D target(
            device, kTargetSize, kTargetSize, false, SurfaceFormat::Color, DepthFormat::None, 0,
            RenderTargetUsage::PreserveContents);

        device.SetRenderTarget(&target);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(rasterizerState);
        device.Clear(Color::Black);

        BasicEffect effect(device);
        effect.VertexColorEnabled = true;
        effect.setLightingEnabledProperty(false);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::CreateOrthographicOffCenter(
            0.0f, static_cast<float>(kTargetSize), static_cast<float>(kTargetSize), 0.0f,
            0.0f, 1.0f));
        effect.getCurrentTechniqueProperty()->getPassesProperty()[0]->Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices.data(), 0, 1);
        device.SetRenderTarget(nullptr);

        std::array<Color, kTargetSize * kTargetSize> pixels{};
        target.GetData(pixels.data(), static_cast<int>(pixels.size()));
        return static_cast<int>(std::count_if(
            pixels.begin(), pixels.end(), [](const Color& pixel) { return pixel != Color::Black; }));
    }

    // These are pixel oracles, so they need a renderer that rasterizes 3D triangles into a target
    // it can read back. A 2D-only renderer reports no ThreeD; Headless reports ThreeD -- it
    // accepts every 3D call -- and rasterizes nothing, refusing the readback. Neither has a
    // pixel to measure (CNA plans/plan_apple_m4.md AM4-269).
    [[nodiscard]] bool RasterizesTriangles()
    {
        GraphicsDevice device;
        return device.SupportsCapability(CNA::GraphicsCapability::ThreeD) &&
               !CNA_RENDERER_IS(CNA::Testing::Renderers::Headless);
    }

    std::array<VertexPositionColor, 3> PixelTriangle(float size, bool reverseWinding = false)
    {
        std::array<VertexPositionColor, 3> vertices{
            VertexPositionColor(Vector3(4.0f, 4.0f, 0.0f), Color::White),
            VertexPositionColor(Vector3(4.0f + size, 4.0f, 0.0f), Color::White),
            VertexPositionColor(Vector3(4.0f, 4.0f + size, 0.0f), Color::White),
        };
        if (reverseWinding)
            std::swap(vertices[1], vertices[2]);
        return vertices;
    }
}

TEST(PixelTriangleRasterizationTest, OnePixelClockwiseTriangleSurvivesXnaDefaultCull)
{
    if (!RasterizesTriangles()) GTEST_SKIP() << "this renderer rasterizes no 3D triangles";
    EXPECT_EQ(RenderTriangle(PixelTriangle(1.0f), RasterizerState::CullCounterClockwise), 1)
        << "the one-pixel star geometry used by the XNA Primitives sample changed coverage";
}

TEST(PixelTriangleRasterizationTest, OnePixelTriangleSurvivesWhenCullingIsDisabled)
{
    if (!RasterizesTriangles()) GTEST_SKIP() << "this renderer rasterizes no 3D triangles";
    EXPECT_EQ(RenderTriangle(PixelTriangle(1.0f), RasterizerState::CullNone), 1)
        << "the failure is pixel coverage rather than face orientation";
}

TEST(PixelTriangleRasterizationTest, DefaultCullRejectsOnlyTheOppositeWinding)
{
    if (!RasterizesTriangles()) GTEST_SKIP() << "this renderer rasterizes no 3D triangles";
    EXPECT_GT(RenderTriangle(PixelTriangle(4.0f), RasterizerState::CullCounterClockwise), 0);
    EXPECT_EQ(RenderTriangle(PixelTriangle(4.0f, true), RasterizerState::CullCounterClockwise), 0);
}
