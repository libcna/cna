// SPDX-License-Identifier: MS-PL
// plans/plan_dx2.md Phase O4 (DX2-30, DX2-39): near-plane clipping (DirectX6ClipTriangleNearPlane, ported
// verbatim from SoftwareRenderer.cpp's SOFTWARE-83) -- a triangle straddling the near plane
// renders its visible portion only, with no crash or garbage output.
//
// Uses a real perspective projection (not identity, unlike the other Phase O4 tests) so a vertex
// can genuinely be behind the near plane (w <= 0) -- verifies the clip path specifically, since
// every other Phase O4 test's identity-projection triangles never approach it.
//
// Check A -- a triangle with one vertex far behind the camera (clipped away) and two vertices in
//   front does not crash and renders a plausible visible fragment (some non-background pixel
//   appears on screen) -- proves clipping produces a valid, rasterizable result instead of
//   garbage/NaN screen coordinates from the un-clipped vertex.
// Check B -- a triangle entirely behind the near plane (all 3 vertices behind the camera) draws
//   nothing at all (the backbuffer stays exactly the clear color) -- proves full-triangle
//   rejection works, not just partial clipping.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cmath>
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
}

class DirectX6ClippingTest : public Game
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

    bool AnyNonBlackPixel(GraphicsDevice& dev)
    {
        for (int y = 0; y < 64; y += 4)
            for (int x = 0; x < 64; x += 4)
            {
                const Color p = ReadPixel(dev, x, y);
                if (p.getRProperty() > 5 || p.getGProperty() > 5 || p.getBProperty() > 5)
                    return true;
            }
        return false;
    }

    bool EveryPixelBlack(GraphicsDevice& dev)
    {
        for (int y = 0; y < 64; y += 4)
            for (int x = 0; x < 64; x += 4)
            {
                const Color p = ReadPixel(dev, x, y);
                if (p.getRProperty() > 5 || p.getGProperty() > 5 || p.getBProperty() > 5)
                    return false;
            }
        return true;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        const Matrix view = Matrix::getIdentityProperty();
        const Matrix projection = Matrix::CreatePerspectiveFieldOfView(
            0.7853981633974483f /* pi/4 */, 1.0f, 0.1f, 100.0f);

        // Check A: one vertex far behind the camera (world Z=+50, behind the near plane once
        // viewed -- this test's camera looks down -Z with an identity view, so a vertex placed at
        // Z=+50 sits behind the viewer), two vertices safely in front (Z=-5). Should clip to a
        // visible partial quad, not crash or paint garbage.
        {
            dev.Clear(Color::Black, 1.0f);
            const VertexPositionColor verts[3] = {
                { Vector3(0.0f, 2.0f, -5.0f), Color::Red },
                { Vector3(-2.0f, -2.0f, -5.0f), Color::Red },
                { Vector3(0.0f, 0.0f, 50.0f), Color::Red },  // behind the camera
            };
            VertexBuffer vbuf(dev, 3);
            vbuf.SetData(verts, 3);
            BasicEffect fx(dev);
            fx.VertexColorEnabled = true;
            fx.World = Matrix::getIdentityProperty();
            fx.View = view;
            fx.Projection = projection;
            fx.Apply();

            bool threw = false;
            try
            {
                dev.SetVertexBuffer(&vbuf);
                dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
                dev.SetVertexBuffer(nullptr);
            }
            catch (const std::exception& e)
            {
                threw = true;
                std::printf("Draw with a straddling-the-near-plane triangle threw: %s\n", e.what());
            }
            Check(!threw && AnyNonBlackPixel(dev),
                  "a triangle straddling the near plane clips and renders a visible partial fragment, no crash");
        }

        // Check B: all 3 vertices behind the camera -- must render nothing at all.
        {
            dev.Clear(Color::Black, 1.0f);
            const VertexPositionColor verts[3] = {
                { Vector3(0.0f, 2.0f, 50.0f), Color::Red },
                { Vector3(-2.0f, -2.0f, 50.0f), Color::Red },
                { Vector3(2.0f, -2.0f, 50.0f), Color::Red },
            };
            VertexBuffer vbuf(dev, 3);
            vbuf.SetData(verts, 3);
            BasicEffect fx(dev);
            fx.VertexColorEnabled = true;
            fx.World = Matrix::getIdentityProperty();
            fx.View = view;
            fx.Projection = projection;
            fx.Apply();

            dev.SetVertexBuffer(&vbuf);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
            dev.SetVertexBuffer(nullptr);

            Check(EveryPixelBlack(dev), "a triangle fully behind the near plane draws nothing at all");
        }

        std::printf("=== %d/%d PASS ===\n", g_passCount, 2);
        result_ = (g_passCount == 2) ? 0 : 1;
        Exit();
    }

public:
    DirectX6ClippingTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    DirectX6ClippingTest game;
    game.Run();
    return game.getResult();
}
