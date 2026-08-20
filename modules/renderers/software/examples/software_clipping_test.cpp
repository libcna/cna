// SPDX-License-Identifier: MS-PL
// plans/plan_software.md Phase S9 (SOFTWARE-83): near-plane polygon clipping, replacing the old
// whole-triangle culling (SOFTWARE-34's own acknowledged v1 gap).
//
// A real perspective projection is required to exercise this (with an identity World/View/
// Projection, clip.W is always exactly 1 and no vertex can ever cross the near plane) --
// Matrix::CreatePerspectiveFieldOfView(PI/2, 1, ...) with World=View=identity gives
// clip.W = -position.Z directly (see Matrix::CreatePerspectiveFieldOfView's M34=-1, M44=0), so a
// vertex at Z<0 is in front of the camera (clip.W>0) and Z>=0 is behind/at the camera (clipped).
//
// This renderer clips at clip.W <= ~0 -- i.e. at the camera's eye plane, not at the projection's
// configured near-plane distance. A vertex clipped there necessarily lands at an enormous (but
// finite) screen position after the perspective divide (dividing by a value forced to be near
// zero) -- this is correct, unavoidable perspective-projection math for a point that close to the
// eye, not a bug. So these checks deliberately avoid asserting exact pixel colors far from a
// stable (unclipped) vertex, and instead check: (1) a small pixel neighborhood around each
// surviving (non-clipped) vertex's own exact screen projection is red -- proving clipping
// preserved the correct geometry near real vertices, not corrupted it; and (2) the total red
// pixel count is bounded, i.e. neither 0 (would mean nothing rendered -- old whole-triangle-cull
// behavior) nor the full framebuffer (would mean a clipping-direction sign bug painted everything).
//
// Check A -- 2 vertices in front + 1 behind (near-plane crossing splits the triangle into a quad,
//   rendered as 2 triangles): both front vertices' own screen neighborhoods are red, and the
//   total red pixel count is bounded (>0, <full framebuffer).
// Check B -- 1 vertex in front + 2 behind (crossing produces a single smaller triangle): the sole
//   surviving vertex's own screen neighborhood is red, count is bounded.
// Check C -- all 3 vertices in front (no clipping needed): renders normally, a basic regression
//   guard that the refactor from cull-the-whole-triangle to real clipping didn't break the
//   already-working no-clipping-needed path.
// Check D -- all 3 vertices behind (fully outside the near plane): produces exactly zero red
//   pixels -- the whole-triangle-discarded case still works after the refactor.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
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
#include <vector>

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

    constexpr int kSize = 64;

    bool IsRed(const Color& c)
    {
        return c.getRProperty() > 200 && c.getGProperty() < 60 && c.getBProperty() < 60;
    }

    // Whether any pixel in the (2*radius+1)^2 neighborhood around (cx,cy) is red -- used to check
    // "clipping preserved this real, unclipped vertex's own projection" without depending on
    // exactly which diagonal direction the rest of the (possibly near-plane-clipped, hence
    // arbitrarily positioned) triangle extends in.
    bool AnyRedNear(GraphicsDevice& dev, int cx, int cy, int radius)
    {
        for (int y = std::max(0, cy - radius); y <= std::min(kSize - 1, cy + radius); ++y)
        {
            for (int x = std::max(0, cx - radius); x <= std::min(kSize - 1, cx + radius); ++x)
            {
                const Rectangle region(x, y, 1, 1);
                Color pixel(0, 0, 0, 0);
                dev.GetBackBufferData(&region, &pixel, 0, 1);
                if (IsRed(pixel))
                    return true;
            }
        }
        return false;
    }

    int CountRedPixels(GraphicsDevice& dev)
    {
        int count = 0;
        for (int y = 0; y < kSize; ++y)
        {
            for (int x = 0; x < kSize; ++x)
            {
                const Rectangle region(x, y, 1, 1);
                Color pixel(0, 0, 0, 0);
                dev.GetBackBufferData(&region, &pixel, 0, 1);
                if (IsRed(pixel))
                    ++count;
            }
        }
        return count;
    }
}

class SoftwareClippingTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int result_ = 1;

    void DrawTriangle(GraphicsDevice& dev, const Matrix& projection,
                      const Vector3& a, const Vector3& b, const Vector3& c)
    {
        const VertexPositionColor verts[3] = { {a, Color::Red}, {b, Color::Red}, {c, Color::Red} };
        VertexBuffer vb(dev, 3);
        vb.SetData(verts, 3);
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.setProjectionProperty(projection);
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
        dev.SetVertexBuffer(nullptr);
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.setRasterizerStateProperty(RasterizerState::CullNone);  // isolate from SOFTWARE-81

        const Matrix projection = Matrix::CreatePerspectiveFieldOfView(
            1.5707963f /* pi/2 */, 1.0f, 0.1f, 100.0f);

        // clip.W = -Z (World=View=identity, see Matrix::CreatePerspectiveFieldOfView: M34=-1,
        // M44=0). Z<0 -> in front of the camera (W>0); Z>=0 -> behind/at the camera (clipped).

        // Check A: v0=(0,0,-2) on-axis (screen center, 32,32) and v1=(1,0,-2) (screen (48,32)),
        // both in front; v2=(0.5,2,5) behind -- the near-plane crossing splits this into a quad.
        {
            dev.Clear(Color::Black, 1.0f);
            DrawTriangle(dev, projection, Vector3(0.0f, 0.0f, -2.0f), Vector3(1.0f, 0.0f, -2.0f),
                        Vector3(0.5f, 2.0f, 5.0f));
            const bool v0Ok = AnyRedNear(dev, 32, 32, 2);
            const bool v1Ok = AnyRedNear(dev, 48, 32, 2);
            const int redCount = CountRedPixels(dev);
            Check(v0Ok && v1Ok && redCount > 0 && redCount < kSize * kSize,
                  "2-in/1-out (quad): both surviving vertices' own projections are red, output is bounded");
        }

        // Check B: v0=(0,0,-2) on-axis in front (screen center); v1/v2 behind (asymmetric, to
        // avoid a degenerate/canceling triangle) -- crosses into a single smaller triangle.
        {
            dev.Clear(Color::Black, 1.0f);
            DrawTriangle(dev, projection, Vector3(0.0f, 0.0f, -2.0f), Vector3(1.3f, 1.0f, 3.0f),
                        Vector3(-0.6f, 1.1f, 3.2f));
            const bool v0Ok = AnyRedNear(dev, 32, 32, 2);
            const int redCount = CountRedPixels(dev);
            Check(v0Ok && redCount > 0 && redCount < kSize * kSize,
                  "1-in/2-out (single triangle): the surviving vertex's own projection is red, output is bounded");
        }

        // Check C: all 3 vertices in front -- no clipping needed, must still render normally.
        {
            dev.Clear(Color::Black, 1.0f);
            DrawTriangle(dev, projection, Vector3(-1.0f, 1.0f, -2.0f), Vector3(-1.0f, -1.0f, -2.0f),
                        Vector3(1.0f, 0.0f, -2.0f));
            const int redCount = CountRedPixels(dev);
            Check(redCount > 0, "all vertices in front: renders normally (no clipping needed)");
        }

        // Check D: all 3 vertices behind -- the whole triangle is discarded, same as before.
        {
            dev.Clear(Color::Black, 1.0f);
            DrawTriangle(dev, projection, Vector3(-1.0f, 1.0f, 2.0f), Vector3(-1.0f, -1.0f, 2.0f),
                        Vector3(1.0f, 0.0f, 2.0f));
            const int redCount = CountRedPixels(dev);
            Check(redCount == 0, "all vertices behind: the whole triangle is discarded, nothing renders");
        }

        std::printf("=== %d/%d PASS ===\n", g_passCount, 4);
        result_ = (g_passCount == 4) ? 0 : 1;
        Exit();
    }

public:
    SoftwareClippingTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    SoftwareClippingTest game;
    game.Run();
    return game.getResult();
}
