// SPDX-License-Identifier: MS-PL
// plans/plan_dx2.md Phase O4 (DX2-30..DX2-35): the real proof DIRECTX3's (a verbatim port of DIRECTX2's own) CPU transform/clip -> D3DTLVERTEX
// -> IDirect3DDevice2::DrawIndexedPrimitive pipeline works -- pixel-verified triangle rendering
// through the real Direct3D v2 device, run under Wine.
//
// Draws go through the normal GraphicsDevice::DrawPrimitives()/DrawIndexedPrimitives() public API
// with a BasicEffect applied (VertexColorEnabled explicitly set true), which GraphicsDevice routes
// to IGraphicsRenderer::DrawPrimitivesEx() -- DirectX3Renderer's own override, built on the same
// CPU transform/clip helpers DrawColoredPrimitives()/DrawIndexedColoredPrimitives() use. So this
// exercises the real pipeline end-to-end through the exact same public API a real game uses.
//
// Since World/View/Projection are all identity, clip.W == 1 for every vertex and NDC coordinates
// equal the raw vertex positions directly -- lets the test place vertices at exact, easy-to-reason
// screen locations without needing a real projection matrix. RasterizerState::CullNone is set
// explicitly since Phase O6 (ApplyRasterizerState) hasn't landed yet -- Phase O4 only guarantees a
// safe D3DCULL_NONE default (see DirectX3Renderer.cpp's Create3DDevice), so this test's own
// triangle winding is deliberately not relied upon either way.
//
// Check A -- a large flat-colored (solid red) triangle covering the center of a 64x64 backbuffer
//   rasterizes to the exact color at the center pixel.
// Check B -- a tri-color (red/green/blue) triangle's centroid pixel is the barycentric average of
//   all three vertex colors -- proves real Direct3D per-pixel color interpolation via D3DTLVERTEX,
//   not just "some color got written".
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

class DirectX3ColoredPrimitivesTest : public Game
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

            const int centroidX = static_cast<int>((0.0f * 0.5f + 0.5f) * 64.0f);
            const int centroidY = static_cast<int>((1.0f - (-0.3f * 0.5f + 0.5f)) * 64.0f);
            const Color centroid = ReadPixel(dev, centroidX, centroidY);
            Check(Close(centroid.getRProperty(), 85, 25) && Close(centroid.getGProperty(), 85, 25) &&
                  Close(centroid.getBProperty(), 85, 25),
                  "a tri-color triangle's centroid pixel is the barycentric average of its 3 vertex colors "
                  "(real Direct3D perspective-correct interpolation via D3DTLVERTEX)");
        }

        std::printf("=== %d/%d PASS ===\n", g_passCount, 2);
        result_ = (g_passCount == 2) ? 0 : 1;
        Exit();
    }

public:
    DirectX3ColoredPrimitivesTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    DirectX3ColoredPrimitivesTest game;
    game.Run();
    return game.getResult();
}
