// SPDX-License-Identifier: MS-PL
// plans/plan_software.md Phase S9 (SOFTWARE-81): empirical verification of backface culling.
//
// Two triangles cover the exact same screen region but have opposite vertex winding:
//   frontCW  = (TL, TR, BL) -- clockwise as displayed (this renderer's screen space is Y-down).
//   backCCW  = (TL, BL, TR) -- the same 3 corners, counter-clockwise as displayed.
// Since World/View/Projection are identity, NDC coordinates equal the raw vertex positions, so
// winding can be reasoned about directly from the vertex list above.
//
// Real XNA/FNA's default RasterizerState is CullCounterClockwise -- it keeps clockwise-as-
// displayed triangles visible and discards counter-clockwise ones. This is not derived from first
// principles here -- it is checked empirically against the actual rendered pixel, per this
// project's own established rigor around winding/orientation-sensitive code.
//
// Check A -- default RasterizerState (CullCounterClockwise): frontCW is visible.
// Check B -- default RasterizerState: backCCW is culled (background shows through).
// Check C -- RasterizerState::CullClockwise: frontCW is now culled (opposite of Check A).
// Check D -- RasterizerState::CullClockwise: backCCW is now visible (opposite of Check B).
// Check E -- RasterizerState::CullNone: backCCW (normally culled by the default) is visible.
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

class SoftwareCullingTest : public Game
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

    // TL=(-1,1), TR=(1,1), BL=(-1,-1) map (identity WVP) to screen corners (0,0)/(64,0)/(0,64) of
    // a 64x64 backbuffer; the centroid of all 3 corners lands at screen pixel (21,21).
    const Vector3 kTL{-1.0f, 1.0f, 0.5f};
    const Vector3 kTR{1.0f, 1.0f, 0.5f};
    const Vector3 kBL{-1.0f, -1.0f, 0.5f};

    void DrawTriangle(GraphicsDevice& dev, const Vector3& a, const Vector3& b, const Vector3& c, const Color& color)
    {
        const VertexPositionColor verts[3] = { {a, color}, {b, color}, {c, color} };
        VertexBuffer vb(dev, 3);
        vb.SetData(verts, 3);
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
        dev.SetVertexBuffer(nullptr);
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        const int cx = 21, cy = 21;

        // Check A: default state (CullCounterClockwise) -- frontCW (TL,TR,BL) is visible.
        {
            dev.Clear(Color::Black, 1.0f);
            DrawTriangle(dev, kTL, kTR, kBL, Color::Red);
            const Color pixel = ReadPixel(dev, cx, cy);
            Check(Close(pixel.getRProperty(), 255, 10) && Close(pixel.getGProperty(), 0, 10),
                  "default RasterizerState (CullCounterClockwise) leaves the clockwise-as-displayed triangle visible");
        }

        // Check B: default state -- backCCW (TL,BL,TR) is culled (background stays black).
        {
            dev.Clear(Color::Black, 1.0f);
            DrawTriangle(dev, kTL, kBL, kTR, Color::Red);
            const Color pixel = ReadPixel(dev, cx, cy);
            Check(Close(pixel.getRProperty(), 0, 10) && Close(pixel.getGProperty(), 0, 10) &&
                  Close(pixel.getBProperty(), 0, 10),
                  "default RasterizerState (CullCounterClockwise) culls the counter-clockwise-as-displayed triangle");
        }

        // Check C: CullClockwise -- frontCW is now culled (the opposite of Check A).
        {
            dev.setRasterizerStateProperty(RasterizerState::CullClockwise);
            dev.Clear(Color::Black, 1.0f);
            DrawTriangle(dev, kTL, kTR, kBL, Color::Red);
            const Color pixel = ReadPixel(dev, cx, cy);
            Check(Close(pixel.getRProperty(), 0, 10) && Close(pixel.getGProperty(), 0, 10) &&
                  Close(pixel.getBProperty(), 0, 10),
                  "RasterizerState::CullClockwise culls the clockwise-as-displayed triangle");
        }

        // Check D: CullClockwise -- backCCW is now visible (the opposite of Check B).
        {
            dev.setRasterizerStateProperty(RasterizerState::CullClockwise);
            dev.Clear(Color::Black, 1.0f);
            DrawTriangle(dev, kTL, kBL, kTR, Color::Red);
            const Color pixel = ReadPixel(dev, cx, cy);
            Check(Close(pixel.getRProperty(), 255, 10) && Close(pixel.getGProperty(), 0, 10),
                  "RasterizerState::CullClockwise leaves the counter-clockwise-as-displayed triangle visible");
        }

        // Check E: CullNone -- backCCW (normally culled by the real default) is visible.
        {
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.Clear(Color::Black, 1.0f);
            DrawTriangle(dev, kTL, kBL, kTR, Color::Red);
            const Color pixel = ReadPixel(dev, cx, cy);
            Check(Close(pixel.getRProperty(), 255, 10) && Close(pixel.getGProperty(), 0, 10),
                  "RasterizerState::CullNone disables culling entirely -- both windings render");
        }

        std::printf("=== %d/%d PASS ===\n", g_passCount, 5);
        result_ = (g_passCount == 5) ? 0 : 1;
        Exit();
    }

public:
    SoftwareCullingTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    SoftwareCullingTest game;
    game.Run();
    return game.getResult();
}
