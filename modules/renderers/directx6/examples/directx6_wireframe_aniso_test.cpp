// SPDX-License-Identifier: MS-PL
// plans/plan_dx2.md Phase O9 (DX2-95, DX2-97): SupportsCapability(WireFrame)/AnisotropicFiltering()
// re-verification, mirroring dx2_spike10_specular_wireframe_aniso.cpp's Test D through the full
// XNA public API instead of a standalone spike.
//
// Check A -- SupportsCapability(WireFrame)==true, SupportsCapability(AnisotropicFiltering)==false.
// Check B -- a triangle covering the render target's center, drawn with RasterizerState.FillMode
//   = Solid: the center reads the triangle's own (non-background) color.
// Check C -- the SAME triangle/vertices, drawn with FillMode = WireFrame: the center reads the
//   cleared background color, not the triangle's -- proving genuinely hollow/edge-only rendering,
//   not a silent solid-fill fallback.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "CNA/GraphicsCapability.hpp"
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
using CNA::GraphicsCapability;

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

class DirectX6WireframeAnisoTest : public Game
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

        Check(dev.SupportsCapability(GraphicsCapability::WireFrame),
              "Check A1: SupportsCapability(WireFrame)==true (Phase O9, spike-confirmed real)");
        Check(!dev.SupportsCapability(GraphicsCapability::AnisotropicFiltering),
              "Check A2: SupportsCapability(AnisotropicFiltering)==false (Phase O9, empirically confirmed absent)");

        // A triangle covering the render target's center, white on a black background.
        const VertexPositionColor verts[3] = {
            { Vector3(-0.8f,  0.8f, 0.5f), Color::White },
            { Vector3( 0.8f,  0.8f, 0.5f), Color::White },
            { Vector3( 0.0f, -0.8f, 0.5f), Color::White },
        };
        VertexBuffer vb(dev, 3);
        vb.SetData(verts, 3);
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();

        RasterizerState solidState;
        solidState.setCullModeProperty(CullMode::None);
        solidState.setFillModeProperty(FillMode::Solid);
        dev.setRasterizerStateProperty(solidState);
        dev.Clear(Color::Black, 1.0f);
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
        dev.SetVertexBuffer(nullptr);
        const Color solidCenter = ReadPixel(dev, 32, 24);
        Check(Close(solidCenter.getRProperty(), 255, 4) && Close(solidCenter.getGProperty(), 255, 4) &&
              Close(solidCenter.getBProperty(), 255, 4),
              "Check B: FillMode=Solid -- the triangle's interior reads its own (white) color");

        RasterizerState wireState;
        wireState.setCullModeProperty(CullMode::None);
        wireState.setFillModeProperty(FillMode::WireFrame);
        dev.setRasterizerStateProperty(wireState);
        dev.Clear(Color::Black, 1.0f);
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
        dev.SetVertexBuffer(nullptr);
        const Color wireCenter = ReadPixel(dev, 32, 24);
        Check(Close(wireCenter.getRProperty(), 0, 4) && Close(wireCenter.getGProperty(), 0, 4) &&
              Close(wireCenter.getBProperty(), 0, 4),
              "Check C: FillMode=WireFrame -- the SAME interior point reads the cleared background "
              "color instead, proving genuinely hollow/edge-only rendering");

        std::printf("=== %d/%d PASS ===\n", g_passCount, 4);
        result_ = (g_passCount == 4) ? 0 : 1;
        Exit();
    }

public:
    DirectX6WireframeAnisoTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    DirectX6WireframeAnisoTest game;
    game.Run();
    return game.getResult();
}
