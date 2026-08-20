// SPDX-License-Identifier: MS-PL
// plans/plan_software.md Phase S4 (SOFTWARE-30..34): the actual proof this renderer's rasterizer core
// works -- real, pixel-verified triangle rendering with no GPU, no window, no display server.
//
// Draws go through the normal GraphicsDevice::DrawPrimitives()/DrawIndexedPrimitives() public API
// with a BasicEffect applied (VertexColorEnabled explicitly set true -- it defaults to false in
// real XNA/FNA, so a plain BasicEffect ignores vertex colors entirely unless a game opts in).
// GraphicsDevice routes these to IGraphicsRenderer::DrawPrimitivesEx()/DrawIndexedPrimitivesEx(),
// which SoftwareRenderer overrides directly (Phase S6) using the same rasterizer core
// Phase S4's DrawColoredPrimitives()/DrawIndexedColoredPrimitives() proved correct. So this is
// exercising the real rasterizer end-to-end through the exact same public API a real game uses,
// not a renderer-internal-only code path.
//
// Since World/View/Projection are all identity, clip.W == 1 for every vertex and NDC coordinates
// equal the raw vertex positions directly -- this lets the test place vertices at exact,
// easy-to-reason-about screen locations without needing a real projection matrix.
//
// Check A -- a large flat-colored (solid red) triangle covering the center of a 64x64 backbuffer
//   rasterizes to the exact color at the center pixel.
// Check B -- a tri-color (red/green/blue) triangle's centroid pixel is the barycentric average of
//   all three vertex colors -- proves per-pixel color interpolation is real, not just "some
//   color got written".
// Check C -- depth test, order A: draw a far (z=0.8) blue triangle first, then a near (z=0.2) red
//   triangle covering the same region -- the near triangle's color must win.
// Check D -- depth test, order B (draw order reversed vs. Check C): draw the near (z=0.2) red
//   triangle FIRST, then the far (z=0.8) blue triangle -- red must STILL win, proving the depth
//   test is real (order-independent occlusion), not an accidental "last write wins" artifact of
//   Check C's particular draw order.
// Check E -- DrawIndexedPrimitives produces the identical pixel result as the equivalent
//   DrawPrimitives call for the same triangle.
// Checks F-J -- effect-aware point-list, line-list and line-strip draws cover both indexed and
//   non-indexed dispatch, including the exact topologies emitted by the glTF importer.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstdint>
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

class SoftwareRasterizerTest : public Game
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
        // These checks' triangles were authored for pixel-correctness, not to match XNA's winding
        // convention, and are back-facing under the real default (CullCounterClockwise) --
        // disable culling (SOFTWARE-81) so this file keeps testing what it was designed to test.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        // Check A: a large solid-red triangle covering the screen center.
        {
            dev.Clear(Color::Black, 1.0f);
            const VertexPositionColor verts[3] = {
                { Vector3(-2.0f,  2.0f, 0.5f), Color::Red },
                { Vector3(-2.0f, -2.0f, 0.5f), Color::Red },
                { Vector3( 2.0f,  0.0f, 0.5f), Color::Red },
            };
            VertexBuffer vb(dev, 3);
            vb.SetData(verts, 3);
            BasicEffect fx(dev);
            fx.VertexColorEnabled = true;
            fx.Apply();
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
            dev.SetVertexBuffer(nullptr);

            const Color center = ReadPixel(dev, 32, 32);
            Check(Close(center.getRProperty(), 255, 2) && Close(center.getGProperty(), 0, 2) &&
                  Close(center.getBProperty(), 0, 2),
                  "a solid-red triangle rasterizes the exact color at the center pixel");
        }

        // Check B: tri-color triangle, centroid pixel is the barycentric average.
        {
            dev.Clear(Color::Black, 1.0f);
            // Deliberately NOT Color::Green -- XNA's Color::Green is the standard/CSS "Green"
            // (0,128,0), not pure (0,255,0) "Lime". Using an explicit pure-green Color here keeps
            // the "each channel ~= 255/3" expectation below simple and symmetric.
            const Color pureGreen(0, 255, 0, 255);
            const VertexPositionColor verts[3] = {
                { Vector3( 0.0f,  0.9f, 0.5f), Color::Red },
                { Vector3(-0.9f, -0.9f, 0.5f), pureGreen },
                { Vector3( 0.9f, -0.9f, 0.5f), Color::Blue },
            };
            VertexBuffer vb(dev, 3);
            vb.SetData(verts, 3);
            BasicEffect fx(dev);
            fx.VertexColorEnabled = true;
            fx.Apply();
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
            dev.SetVertexBuffer(nullptr);

            // Centroid of the 3 NDC positions above is (0, -0.3); map to screen space the same
            // way the rasterizer does: screenX=(ndcX*0.5+0.5)*64, screenY=(1-(ndcY*0.5+0.5))*64.
            const int centroidX = static_cast<int>((0.0f * 0.5f + 0.5f) * 64.0f);
            const int centroidY = static_cast<int>((1.0f - (-0.3f * 0.5f + 0.5f)) * 64.0f);
            const Color centroid = ReadPixel(dev, centroidX, centroidY);
            // Equal barycentric weight on Red/Green/Blue -> each channel ~= 255/3 = 85.
            Check(Close(centroid.getRProperty(), 85, 25) && Close(centroid.getGProperty(), 85, 25) &&
                  Close(centroid.getBProperty(), 85, 25),
                  "a tri-color triangle's centroid pixel is the barycentric average of its 3 vertex colors");
        }

        // Checks C/D: depth test correctness, both draw orders.
        {
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

            // Check C: far-then-near -- near (red) must win.
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

            // Check D: near-then-far (draw order reversed) -- red must STILL win.
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
        }

        // Check E: DrawIndexedPrimitives matches the equivalent DrawPrimitives result.
        {
            dev.Clear(Color::Black, 1.0f);
            const VertexPositionColor verts[3] = {
                { Vector3(-2.0f,  2.0f, 0.5f), Color::Red },
                { Vector3(-2.0f, -2.0f, 0.5f), Color::Red },
                { Vector3( 2.0f,  0.0f, 0.5f), Color::Red },
            };
            VertexBuffer vb(dev, 3);
            vb.SetData(verts, 3);
            IndexBuffer ib(dev, IndexElementSize::SixteenBits, 3, BufferUsage::None);
            const std::uint16_t indices[3] = {0, 1, 2};
            ib.SetData(indices, 0, 3);
            BasicEffect fx(dev);
            fx.VertexColorEnabled = true;
            fx.Apply();

            dev.SetVertexBuffer(&vb);
            dev.SetIndexBuffer(&ib);
            dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 3, 0, 1);
            dev.SetVertexBuffer(nullptr);
            dev.SetIndexBuffer(nullptr);

            const Color indexedCenter = ReadPixel(dev, 32, 32);
            Check(Close(indexedCenter.getRProperty(), 255, 2) && Close(indexedCenter.getGProperty(), 0, 2),
                  "DrawIndexedPrimitives produces the same pixel result as the equivalent DrawPrimitives call");
        }

        // Check F: non-indexed point-list writes exactly the transformed point pixel.
        {
            dev.Clear(Color::Black, 1.0f);
            const VertexPositionColor verts[1] = {
                { Vector3(0.0f, 0.0f, 0.5f), Color::Red },
            };
            VertexBuffer vb(dev, 1);
            vb.SetData(verts, 1);
            BasicEffect fx(dev);
            fx.VertexColorEnabled = true;
            fx.Apply();
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::PointListEXT, 0, 1);
            dev.SetVertexBuffer(nullptr);

            const Color center = ReadPixel(dev, 32, 32);
            Check(Close(center.getRProperty(), 255, 2),
                  "DrawPrimitives PointListEXT writes the transformed point pixel");
        }

        // Check G: indexed point-list honors the index rather than drawing vertex zero.
        {
            dev.Clear(Color::Black, 1.0f);
            const Color pureGreen(0, 255, 0, 255);
            const VertexPositionColor verts[2] = {
                { Vector3(-0.75f, -0.75f, 0.5f), Color::Red },
                { Vector3(0.0f, 0.0f, 0.5f), pureGreen },
            };
            const std::uint16_t indices[1] = {1};
            VertexBuffer vb(dev, 2);
            vb.SetData(verts, 2);
            IndexBuffer ib(dev, IndexElementSize::SixteenBits, 1, BufferUsage::None);
            ib.SetData(indices, 0, 1);
            BasicEffect fx(dev);
            fx.VertexColorEnabled = true;
            fx.Apply();
            dev.SetVertexBuffer(&vb);
            dev.SetIndexBuffer(&ib);
            dev.DrawIndexedPrimitives(PrimitiveType::PointListEXT, 0, 0, 2, 0, 1);
            dev.SetVertexBuffer(nullptr);
            dev.SetIndexBuffer(nullptr);

            const Color center = ReadPixel(dev, 32, 32);
            Check(Close(center.getGProperty(), 255, 2) && Close(center.getRProperty(), 0, 2),
                  "DrawIndexedPrimitives PointListEXT honors the decoded index");
        }

        // Check H: a non-indexed line list walks every pixel between its two endpoints.
        {
            dev.Clear(Color::Black, 1.0f);
            const VertexPositionColor verts[2] = {
                { Vector3(-0.8f, 0.0f, 0.5f), Color::Blue },
                { Vector3( 0.8f, 0.0f, 0.5f), Color::Blue },
            };
            VertexBuffer vb(dev, 2);
            vb.SetData(verts, 2);
            BasicEffect fx(dev);
            fx.VertexColorEnabled = true;
            fx.Apply();
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::LineList, 0, 1);
            dev.SetVertexBuffer(nullptr);

            const Color center = ReadPixel(dev, 32, 32);
            Check(Close(center.getBProperty(), 255, 2),
                  "DrawPrimitives LineList rasterizes the segment midpoint");
        }

        // Check I: indexed line-list uses each independent decoded pair.
        {
            dev.Clear(Color::Black, 1.0f);
            const VertexPositionColor verts[3] = {
                { Vector3(-0.8f, 0.0f, 0.5f), Color::Red },
                { Vector3( 0.8f, 0.0f, 0.5f), Color::Red },
                { Vector3( 0.8f, 0.8f, 0.5f), Color::Blue },
            };
            const std::uint16_t indices[2] = {0, 1};
            VertexBuffer vb(dev, 3);
            vb.SetData(verts, 3);
            IndexBuffer ib(dev, IndexElementSize::SixteenBits, 2, BufferUsage::None);
            ib.SetData(indices, 0, 2);
            BasicEffect fx(dev);
            fx.VertexColorEnabled = true;
            fx.Apply();
            dev.SetVertexBuffer(&vb);
            dev.SetIndexBuffer(&ib);
            dev.DrawIndexedPrimitives(PrimitiveType::LineList, 0, 0, 3, 0, 1);
            dev.SetVertexBuffer(nullptr);
            dev.SetIndexBuffer(nullptr);

            const Color center = ReadPixel(dev, 32, 32);
            Check(Close(center.getRProperty(), 255, 2) && Close(center.getBProperty(), 0, 2),
                  "DrawIndexedPrimitives LineList honors the decoded endpoint pair");
        }

        // Check J: indexed line-strip shares the middle vertex and emits both segments.
        {
            dev.Clear(Color::Black, 1.0f);
            const Color pureGreen(0, 255, 0, 255);
            const VertexPositionColor verts[3] = {
                { Vector3(-0.8f, 0.0f, 0.5f), pureGreen },
                { Vector3( 0.0f, 0.0f, 0.5f), pureGreen },
                { Vector3( 0.0f, 0.8f, 0.5f), pureGreen },
            };
            const std::uint16_t indices[3] = {0, 1, 2};
            VertexBuffer vb(dev, 3);
            vb.SetData(verts, 3);
            IndexBuffer ib(dev, IndexElementSize::SixteenBits, 3, BufferUsage::None);
            ib.SetData(indices, 0, 3);
            BasicEffect fx(dev);
            fx.VertexColorEnabled = true;
            fx.Apply();
            dev.SetVertexBuffer(&vb);
            dev.SetIndexBuffer(&ib);
            dev.DrawIndexedPrimitives(PrimitiveType::LineStrip, 0, 0, 3, 0, 2);
            dev.SetVertexBuffer(nullptr);
            dev.SetIndexBuffer(nullptr);

            const Color horizontal = ReadPixel(dev, 20, 32);
            const Color vertical = ReadPixel(dev, 32, 20);
            Check(Close(horizontal.getGProperty(), 255, 2) &&
                      Close(vertical.getGProperty(), 255, 2),
                  "DrawIndexedPrimitives LineStrip emits both connected segments");
        }

        std::printf("=== %d/%d PASS ===\n", g_passCount, 10);
        result_ = (g_passCount == 10) ? 0 : 1;
        Exit();
    }

public:
    SoftwareRasterizerTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    SoftwareRasterizerTest game;
    game.Run();
    return game.getResult();
}
