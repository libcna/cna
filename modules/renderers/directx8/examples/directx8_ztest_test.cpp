// SPDX-License-Identifier: MS-PL
// plans/plan_dx2.md Phase O4 (DX2-37): depth-test correctness through the real Direct3D v2 device and
// its attached Z-buffer (mirrors DX2-0's own dx2_spike7_full.cpp test A, now through the full XNA
// public API instead of a standalone spike).
//
// Check A -- draw order A: draw a far (z=0.8) blue triangle first, then a near (z=0.2) red
//   triangle covering the same region -- the near triangle's color must win.
// Check B -- draw order B (reversed vs. Check A): draw the near (z=0.2) red triangle FIRST, then
//   the far (z=0.8) blue triangle -- red must STILL win, proving the depth test is real
//   (order-independent occlusion), not an accidental "last write wins" artifact of Check A's
//   particular draw order.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
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
}

class DirectX8ZTestTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int result_ = 1;

    Color ReadPixel(GraphicsDevice& dev, int x, int y)
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
        // NOTE: setDepthStencilStateProperty() is deliberately NOT called here -- DirectX8Renderer
        // does not wire ApplyDepthStencilState until Phase O6, so it would have no effect yet. This
        // test instead relies on Create3DDevice()'s own explicit Phase-O4 default (ZENABLE=TRUE,
        // ZFUNC=LESSEQUAL), which matches real XNA's DepthStencilState.Default exactly.

        const VertexPositionColor farBlue[3] = {
            { Vector3(-2.0f,  2.0f, 0.8f), Color::Blue },
            { Vector3(-2.0f, -2.0f, 0.8f), Color::Blue },
            { Vector3( 2.0f,  0.0f, 0.8f), Color::Blue },
        };
        const VertexPositionColor nearRed[3] = {
            { Vector3(-2.0f,  2.0f, 0.2f), Color::Red },
            { Vector3(-2.0f, -2.0f, 0.2f), Color::Red },
            { Vector3( 2.0f,  0.0f, 0.2f), Color::Red },
        };
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();

        // Check A: far-then-near -- near (red) must win.
        dev.Clear(Color::Black, 1.0f);
        {
            VertexBuffer vbFar(dev, 3); vbFar.SetData(farBlue, 3);
            dev.SetVertexBuffer(&vbFar);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);

            VertexBuffer vbNear(dev, 3); vbNear.SetData(nearRed, 3);
            dev.SetVertexBuffer(&vbNear);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
            dev.SetVertexBuffer(nullptr);
        }
        const Color afterFarThenNear = ReadPixel(dev, 32, 32);
        Check(Close(afterFarThenNear.getRProperty(), 255, 2) && Close(afterFarThenNear.getBProperty(), 0, 2),
              "depth test: drawing far(blue) then near(red) leaves near(red) visible");

        // Check B: near-then-far (draw order reversed) -- red must STILL win.
        dev.Clear(Color::Black, 1.0f);
        {
            VertexBuffer vbNear(dev, 3); vbNear.SetData(nearRed, 3);
            dev.SetVertexBuffer(&vbNear);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);

            VertexBuffer vbFar(dev, 3); vbFar.SetData(farBlue, 3);
            dev.SetVertexBuffer(&vbFar);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
            dev.SetVertexBuffer(nullptr);
        }
        const Color afterNearThenFar = ReadPixel(dev, 32, 32);
        Check(Close(afterNearThenFar.getRProperty(), 255, 2) && Close(afterNearThenFar.getBProperty(), 0, 2),
              "depth test: drawing near(red) then far(blue) STILL leaves near(red) visible (order-independent)");

        std::printf("=== %d/%d PASS ===\n", g_passCount, 2);
        result_ = (g_passCount == 2) ? 0 : 1;
        Exit();
    }

public:
    DirectX8ZTestTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    DirectX8ZTestTest game;
    game.Run();
    return game.getResult();
}
