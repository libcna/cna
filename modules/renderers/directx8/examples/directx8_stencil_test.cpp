// SPDX-License-Identifier: MS-PL
// plans/plan_dx8.md Phase S6: real stencil buffer write-then-test through the full XNA public API
// (GraphicsDevice.DepthStencilState), proving stencil (unchanged from DIRECTX6, ported verbatim)
// survives DIRECTX8's removal of the whole viewport object and its texture-binding simplification.
// Mirrors the DX8-0 spike's own Test E/F shape (dx8-spike/dx8_spike1_flattened_device.cpp), but
// through BasicEffect/DrawUserPrimitives/DepthStencilState instead of raw IDirect3DDevice7 calls.
//
// Check A -- stamp: draw a quad covering the LEFT half of the target with
//   StencilFunction=Always, StencilPass=Replace, ReferenceStencil=1 -- writes stencil=1 on the left
//   half only (StencilEnable is per-draw state, so the right half's stencil stays at its
//   Clear(..., stencil=0) value).
// Check B -- test: draw a FULL-target quad with StencilFunction=Equal, ReferenceStencil=1,
//   StencilPass/Fail/DepthBufferFail=Keep (so the test itself never mutates the buffer) -- the
//   left half (stencil==1) must pass and show the test color; the right half (stencil==0) must be
//   REJECTED and stay showing the Check A stamp color (background), proving the stencil test
//   itself is real, not merely "stencil write accepted, compare ignored."
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    int g_passCount = 0;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++g_passCount;
    }

    bool Close(int a, int b, int tolerance) { return std::abs(a - b) <= tolerance; }

    void DrawFullQuad(GraphicsDevice& dev, const Color& color)
    {
        const VertexPositionColor verts[6] = {
            { Vector3(-2.0f,  2.0f, 0.5f), color },
            { Vector3(-2.0f, -2.0f, 0.5f), color },
            { Vector3( 2.0f, -2.0f, 0.5f), color },
            { Vector3(-2.0f,  2.0f, 0.5f), color },
            { Vector3( 2.0f, -2.0f, 0.5f), color },
            { Vector3( 2.0f,  2.0f, 0.5f), color },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
    }

    void DrawLeftHalfQuad(GraphicsDevice& dev, const Color& color)
    {
        // NDC x in [-2, 0] maps (after clipping to [-1, 1]) onto the LEFT half of the viewport.
        const VertexPositionColor verts[6] = {
            { Vector3(-2.0f,  2.0f, 0.5f), color },
            { Vector3(-2.0f, -2.0f, 0.5f), color },
            { Vector3( 0.0f, -2.0f, 0.5f), color },
            { Vector3(-2.0f,  2.0f, 0.5f), color },
            { Vector3( 0.0f, -2.0f, 0.5f), color },
            { Vector3( 0.0f,  2.0f, 0.5f), color },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
    }
}

class DirectX8StencilTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int result_ = 1;

    static Color ReadPixel(GraphicsDevice& dev, int x, int y)
    {
        const Rectangle region(x, y, 1, 1);
        Color pixel(0, 0, 0, 0);
        dev.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        const Color kBackground(10, 10, 10, 255);
        const Color kStamp(200, 0, 0, 255);
        const Color kTest(0, 200, 0, 255);

        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                  kBackground, 1.0f, 0);

        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();

        // Check A: stamp stencil=1 on the LEFT half only, via a real Replace-on-pass write.
        DepthStencilState stamp;
        stamp.setDepthBufferEnableProperty(false);
        stamp.setStencilEnableProperty(true);
        stamp.setStencilFunctionProperty(CompareFunction::Always);
        stamp.setStencilPassProperty(StencilOperation::Replace);
        stamp.setReferenceStencilProperty(1);
        dev.setDepthStencilStateProperty(stamp);
        DrawLeftHalfQuad(dev, kStamp);

        const int width = dev.getViewportProperty().getWidthProperty();
        const int height = dev.getViewportProperty().getHeightProperty();
        const int leftX = width / 4;
        const int rightX = (3 * width) / 4;
        const int midY = height / 2;

        const Color afterStampLeft = ReadPixel(dev, leftX, midY);
        const Color afterStampRight = ReadPixel(dev, rightX, midY);
        Check(Close(afterStampLeft.getRProperty(), kStamp.getRProperty(), 2) &&
                  Close(afterStampLeft.getGProperty(), kStamp.getGProperty(), 2),
              "stencil stamp: left half shows stamp color");
        Check(Close(afterStampRight.getRProperty(), kBackground.getRProperty(), 2) &&
                  Close(afterStampRight.getGProperty(), kBackground.getGProperty(), 2),
              "stencil stamp: right half untouched (still background)");

        // Check B: test stencil==1 with a FULL-target quad -- left half (stencil==1) must PASS,
        // right half (stencil==0) must be REJECTED. StencilPass/Fail/DepthBufferFail are all Keep
        // so this draw itself never mutates the stencil buffer.
        DepthStencilState test;
        test.setDepthBufferEnableProperty(false);
        test.setStencilEnableProperty(true);
        test.setStencilFunctionProperty(CompareFunction::Equal);
        test.setReferenceStencilProperty(1);
        test.setStencilPassProperty(StencilOperation::Keep);
        test.setStencilFailProperty(StencilOperation::Keep);
        test.setStencilDepthBufferFailProperty(StencilOperation::Keep);
        dev.setDepthStencilStateProperty(test);
        DrawFullQuad(dev, kTest);

        const Color afterTestLeft = ReadPixel(dev, leftX, midY);
        const Color afterTestRight = ReadPixel(dev, rightX, midY);
        Check(Close(afterTestLeft.getRProperty(), kTest.getRProperty(), 2) &&
                  Close(afterTestLeft.getGProperty(), kTest.getGProperty(), 2),
              "stencil test: left half (stencil==1) passes and shows test color");
        Check(Close(afterTestRight.getRProperty(), kBackground.getRProperty(), 2) &&
                  Close(afterTestRight.getGProperty(), kBackground.getGProperty(), 2),
              "stencil test: right half (stencil==0) rejected, still shows background");

        std::printf("=== %d/%d PASS ===\n", g_passCount, 4);
        result_ = (g_passCount == 4) ? 0 : 1;
        Exit();
    }

public:
    DirectX8StencilTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    DirectX8StencilTest game;
    game.Run();
    return game.getResult();
}
