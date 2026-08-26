// SPDX-License-Identifier: MS-PL
// WEBGPU-84: real GPU occlusion queries. A query counts the samples that pass depth/stencil for the
// draws issued between OcclusionQuery.Begin() and End(). This is the discriminating functional test:
// the SAME full-screen quad reads back zero samples when it is behind an occluder and a full frame's
// worth of samples when it is in front -- so a query that always returned 0 (the old no-op) or always
// returned a fixed number would fail.
//
// Into one depth-buffered RenderTarget2D, all in one frame:
//   1. draw an opaque OCCLUDER quad at depth 0.1 (no query) -- writes depth 0.1 everywhere;
//   2. Q1 wraps a quad at depth 0.9 -> depth test fails everywhere -> 0 samples;
//   3. Q2 wraps a quad at depth 0.05 -> passes everywhere -> a full target of samples.
// GetData() flushes the frame (resolving the queries); then the counts are read.
//
// Check A -- Q1 (occluded) IsComplete and PixelCount == 0.
// Check B -- Q2 (visible)  IsComplete and PixelCount  > 0 (near a full 64x64 target).
// Check C -- the two queries are independent (Q2 > Q1), proving per-slot results, not one shared value.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    int passCount = 0;
    int totalCount = 0;

    void check(bool ok, const char* label)
    {
        ++totalCount;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount;
    }

    // A full-viewport quad (two triangles) at NDC depth z, solid colour c.
    std::vector<VertexPositionColor> quad(float z, const Color& c)
    {
        return {
            {Vector3(-1.0f, -1.0f, z), c}, {Vector3(1.0f, -1.0f, z), c}, {Vector3(1.0f, 1.0f, z), c},
            {Vector3(-1.0f, -1.0f, z), c}, {Vector3(1.0f, 1.0f, z), c},  {Vector3(-1.0f, 1.0f, z), c},
        };
    }
}

class WebGpuOcclusionQueryTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int frame_ = 0;

    void drawQuad(GraphicsDevice& dev, BasicEffect& fx, float z, const Color& c)
    {
        const auto verts = quad(z, c);
        VertexBuffer vb(dev, VertexPositionColor::getVertexDeclarationStatic(),
                        static_cast<int>(verts.size()), BufferUsage::None);
        vb.SetData(verts.data(), static_cast<int>(verts.size()));
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

protected:
    void Draw(const GameTime&) override
    {
        if (frame_++ < 1) return;
        auto& dev = getGraphicsDeviceProperty();

        RenderTarget2D target(dev, kSize, kSize, false, SurfaceFormat::Color,
                              DepthFormat::Depth24Stencil8, 0, RenderTargetUsage::PreserveContents);

        RasterizerState rs;
        rs.setCullModeProperty(CullMode::None);
        dev.setRasterizerStateProperty(rs);
        dev.setDepthStencilStateProperty(DepthStencilState::Default);   // depth test + write on
        dev.setBlendStateProperty(BlendState::Opaque);

        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.setLightingEnabledProperty(false);
        fx.setTextureEnabledProperty(false);
        fx.setFogEnabledProperty(false);

        OcclusionQuery occluded(dev);   // Q1
        OcclusionQuery visible(dev);    // Q2

        dev.SetRenderTarget(&target);
        dev.setScissorRectangleProperty(Rectangle(0, 0, kSize, kSize));
        dev.Clear(Color(0, 0, 0, 255));   // depth clears to 1.0 (far)

        drawQuad(dev, fx, 0.1f, Color(0, 0, 255, 255));   // occluder, no query -> depth 0.1

        occluded.Begin();
        drawQuad(dev, fx, 0.9f, Color(255, 0, 0, 255));   // behind occluder -> fully occluded
        occluded.End();

        visible.Begin();
        drawQuad(dev, fx, 0.05f, Color(0, 255, 0, 255));  // in front -> fully visible
        visible.End();

        dev.SetRenderTarget(nullptr);

        // Flush the frame (resolves the queries) by reading the target back.
        std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
        const Rectangle region(0, 0, kSize, kSize);
        target.GetData(0, &region, pixels.data(), 0, static_cast<int>(pixels.size()));

        const int occludedCount = occluded.getPixelCountProperty();
        const int visibleCount = visible.getPixelCountProperty();
        std::printf("occluded PixelCount=%d (complete=%d), visible PixelCount=%d (complete=%d)\n",
                    occludedCount, occluded.getIsCompleteProperty() ? 1 : 0,
                    visibleCount, visible.getIsCompleteProperty() ? 1 : 0);

        check(occluded.getIsCompleteProperty() && occludedCount == 0,
              "Check A: the occluded quad's query completes with PixelCount == 0");
        check(visible.getIsCompleteProperty() && visibleCount > 0,
              "Check B: the visible quad's query completes with PixelCount > 0");
        check(visibleCount > occludedCount,
              "Check C: the two queries hold independent results (visible > occluded)");

        std::printf("=== %d/%d PASS ===\n", passCount, totalCount);
        Exit();
    }

public:
    WebGpuOcclusionQueryTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }
};

int main()
{
    WebGpuOcclusionQueryTest game;
    game.Run();

    std::printf("=== %d/%d PASS (total) ===\n", passCount, totalCount);
    return (passCount == totalCount) ? 0 : 1;
}
