// SPDX-License-Identifier: MS-PL
// WEBGPU-44: SetDataWithOptions (None / Discard / NoOverwrite) on a DynamicVertexBuffer must upload
// correct data on the WebGPU backend for every option, and -- because this renderer snapshots each
// draw's vertex bytes at queue time rather than binding the live buffer at replay -- a second
// SetData(..., Discard) issued before the frame flushes must NOT corrupt an earlier draw that was
// already queued. That "two draws, one intervening re-upload" case is exactly the hazard Discard
// exists to guard against in an immediate-mode renderer; here the snapshot makes it safe, which is
// what this test proves.
//
// Check A -- SetData(red, None) then draw fills the target red.
// Check B -- SetData(green, Discard) then draw fills the target green.
// Check C -- SetData(blue, NoOverwrite) then draw fills the target blue.
// Check D -- hazard: into target1 SetData(red, None)+draw; into target2 SetData(green, Discard)+draw;
//   THEN read both. target1 is still red (its snapshot), not green -- the Discard/second upload did
//   not reach the first, already-queued draw.
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
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 32;

    int passCount = 0;
    int totalCount = 0;

    void check(bool ok, const char* label)
    {
        ++totalCount;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount;
    }

    bool dominant(const Color& c, int r, int g, int b)
    {
        auto near = [](int v, int t) { return v >= t - 40 && v <= t + 40; };
        return near(c.getRProperty(), r) && near(c.getGProperty(), g) && near(c.getBProperty(), b);
    }

    RenderTarget2D makeTarget(GraphicsDevice& dev)
    {
        return RenderTarget2D(dev, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::PreserveContents);
    }

    // Uploads a full-viewport solid-colour triangle into vb with the given option and draws it into
    // target. Does NOT read back -- the caller decides when to flush, so the hazard case can queue
    // two draws before the first readback.
    void uploadAndDraw(GraphicsDevice& dev, DynamicVertexBuffer& vb, RenderTarget2D& target,
                       const Color& c, SetDataOptions opt)
    {
        const VertexPositionColor tri[3] = {
            {Vector3(-1.0f, -1.0f, 0.0f), c},
            {Vector3(3.0f, -1.0f, 0.0f), c},
            {Vector3(-1.0f, 3.0f, 0.0f), c},
        };
        vb.SetData(tri, 0, 3, opt);

        RasterizerState rs;
        rs.setCullModeProperty(CullMode::None);
        dev.setRasterizerStateProperty(rs);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setBlendStateProperty(BlendState::Opaque);

        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.setLightingEnabledProperty(false);
        fx.setTextureEnabledProperty(false);
        fx.setFogEnabledProperty(false);

        dev.SetRenderTarget(&target);
        dev.setScissorRectangleProperty(Rectangle(0, 0, kSize, kSize));
        dev.Clear(Color(0, 0, 0, 255));
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
        dev.SetVertexBuffer(nullptr);
        dev.SetRenderTarget(nullptr);
    }

    Color centerOf(RenderTarget2D& target)
    {
        Color px(0, 0, 0, 0);
        const Rectangle region(kSize / 2, kSize / 2, 1, 1);
        target.GetData(0, &region, &px, 0, 1);
        return px;
    }
}

class WebGpuSetDataOptionsTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int frame_ = 0;

protected:
    void Draw(const GameTime&) override
    {
        if (frame_++ < 1) return;

        auto& dev = getGraphicsDeviceProperty();
        DynamicVertexBuffer vb(dev, VertexPositionColor::getVertexDeclarationStatic(), 3,
                               BufferUsage::None);

        // Checks A/B/C: each option uploads + draws correctly (each reads back before the next).
        {
            RenderTarget2D t = makeTarget(dev);
            uploadAndDraw(dev, vb, t, Color(255, 0, 0, 255), SetDataOptions::None);
            check(dominant(centerOf(t), 255, 0, 0), "Check A: SetData(None) draws red");
        }
        {
            RenderTarget2D t = makeTarget(dev);
            uploadAndDraw(dev, vb, t, Color(0, 255, 0, 255), SetDataOptions::Discard);
            check(dominant(centerOf(t), 0, 255, 0), "Check B: SetData(Discard) draws green");
        }
        {
            RenderTarget2D t = makeTarget(dev);
            uploadAndDraw(dev, vb, t, Color(0, 0, 255, 255), SetDataOptions::NoOverwrite);
            check(dominant(centerOf(t), 0, 0, 255), "Check C: SetData(NoOverwrite) draws blue");
        }

        // Check D: hazard -- two draws queued into two targets, an intervening SetData(Discard)
        // between them, THEN read both. target1 must still be red.
        {
            RenderTarget2D t1 = makeTarget(dev);
            RenderTarget2D t2 = makeTarget(dev);
            uploadAndDraw(dev, vb, t1, Color(255, 0, 0, 255), SetDataOptions::None);
            uploadAndDraw(dev, vb, t2, Color(0, 255, 0, 255), SetDataOptions::Discard);
            const Color c1 = centerOf(t1);
            const Color c2 = centerOf(t2);
            check(dominant(c1, 255, 0, 0) && dominant(c2, 0, 255, 0),
                  "Check D: a Discard SetData between two queued draws did not corrupt the first "
                  "draw's snapshot (target1 stays red, target2 green)");
        }

        std::printf("=== %d/%d PASS ===\n", passCount, totalCount);
        Exit();
    }

public:
    WebGpuSetDataOptionsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }
};

int main()
{
    WebGpuSetDataOptionsTest game;
    game.Run();

    std::printf("=== %d/%d PASS (total) ===\n", passCount, totalCount);
    return (passCount == totalCount) ? 0 : 1;
}
