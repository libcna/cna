// SPDX-License-Identifier: MS-PL
// plans/plan_d3d10.md design decision 3/5: D3D10 genuinely supports MRT (unlike every DIRECTX1..DIRECTX8 renderer,
// where DirectDraw/early-Direct3D has exactly one active render target) -- verifies
// SetRenderTargets(count=2) does not throw AND that each bound target keeps its own genuinely
// distinct buffer (not silently aliased to the same one): two render targets are each cleared to
// their own distinct color, bound together, then a colored-primitives draw (which only writes
// SV_Target0 in this renderer's shader) covers the screen -- afterward, target 0 shows the draw's
// color but target 1 still shows its own original clear color, unchanged.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kCanvasSize = 32;

class D3D10MrtTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int passCount_ = 0;
    static constexpr int kTotal = 3;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        // RenderTargetUsage::PreserveContents on BOTH targets (real, documented XNA/FNA behavior,
        // NOT this v1's own choice): GraphicsDevice::SetRenderTargets checks ONLY the FIRST bound
        // target's own usage -- if it's the default DiscardContents, EVERY currently-bound target
        // (not just the first) gets auto-cleared on each bind, real XNA/D3D hardware historically
        // not preserving render-target content across a bind unless asked to. Without
        // PreserveContents on rt0 specifically (the first element of the {rt0, rt1} binding
        // below), binding them together would auto-clear rt1 back to black too, which would look
        // identical to a genuine MRT-aliasing bug and defeat this test's own purpose.
        RenderTarget2D rt0(dev, kCanvasSize, kCanvasSize, false, SurfaceFormat::Color, DepthFormat::None,
                           0, RenderTargetUsage::PreserveContents);
        RenderTarget2D rt1(dev, kCanvasSize, kCanvasSize, false, SurfaceFormat::Color, DepthFormat::None,
                           0, RenderTargetUsage::PreserveContents);

        std::vector<RenderTargetBinding> singleRt0{ RenderTargetBinding(&rt0) };
        dev.SetRenderTargets(singleRt0);
        dev.Clear(Color(10, 20, 30, 255));

        std::vector<RenderTargetBinding> singleRt1{ RenderTargetBinding(&rt1) };
        dev.SetRenderTargets(singleRt1);
        dev.Clear(Color(200, 100, 50, 255));

        std::vector<RenderTargetBinding> both{ RenderTargetBinding(&rt0), RenderTargetBinding(&rt1) };
        bool threw = false;
        try { dev.SetRenderTargets(both); }
        catch (const std::exception& e)
        {
            threw = true;
            std::printf("SetRenderTargets(count=2) threw: %s\n", e.what());
        }
        check(!threw, "SetRenderTargets(count=2) does not throw (real MRT support)");

        // A full-screen triangle-strip-covering quad (2 triangles), solid white -- only writes
        // SV_Target0 in this renderer's colored3d shader.
        const VertexPositionColor verts[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), Color::White },
            { Vector3( 1.0f,  1.0f, 0.0f), Color::White },
            { Vector3(-1.0f, -1.0f, 0.0f), Color::White },
            { Vector3( 1.0f,  1.0f, 0.0f), Color::White },
            { Vector3( 1.0f, -1.0f, 0.0f), Color::White },
            { Vector3(-1.0f, -1.0f, 0.0f), Color::White },
        };
        VertexBuffer vb(dev, 6);
        vb.SetData(verts, 6);
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);

        std::vector<RenderTargetBinding> none;
        dev.SetRenderTargets(none);

        const Rectangle onePixel(0, 0, 1, 1);
        Color rt0Pixel(0, 0, 0, 0);
        rt0.GetData(0, &onePixel, &rt0Pixel, 0, 1);
        check(rt0Pixel.getRProperty() > 200 && rt0Pixel.getGProperty() > 200 && rt0Pixel.getBProperty() > 200,
              "Target 0 shows the draw's own color (white) after the MRT draw");

        Color rt1Pixel(0, 0, 0, 0);
        rt1.GetData(0, &onePixel, &rt1Pixel, 0, 1);
        check(rt1Pixel.getRProperty() == 200 && rt1Pixel.getGProperty() == 100 && rt1Pixel.getBProperty() == 50,
              "Target 1 keeps its own original clear color, unchanged -- genuinely distinct MRT buffers");

        std::printf("=== %d/%d PASS ===\n", passCount_, kTotal);
        result_ = (passCount_ == kTotal) ? 0 : 1;
        Exit();
    }

public:
    D3D10MrtTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kCanvasSize);
        gdm_->setPreferredBackBufferHeightProperty(kCanvasSize);
    }
    int getResult() const { return result_; }
};

int main()
{
    D3D10MrtTest game;
    game.Run();
    return game.getResult();
}
