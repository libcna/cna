// SPDX-License-Identifier: MS-PL
// Phase 57/63 vertical slice: verify WebGPURenderer::DrawColoredPrimitives()/
// DrawIndexedColoredPrimitives() -- the first real 3D draw path this renderer supports (a
// VertexPositionColor, stride-16 quad/triangle with no texture/lighting), matching WEBGPU-93's
// own "3D coloured-quad pixel test (stride 16)" target. See plans/plan_webgpu.md's Phase 57 section for
// the investigated entry point this implements: the 128-float uniform layout mirrors
// VulkanRenderer::FillExtPushConst() byte-for-byte, and GraphicsDevice::DrawUserPrimitives()/
// DrawUserIndexedPrimitives() already route here directly for any VertexPositionColor draw.
//
// Check A -- DrawUserPrimitives (non-indexed): a red quad renders red at its centre.
// Check B -- DrawUserIndexedPrimitives: a blue quad built from 4 vertices + 6 indices (two
//   triangles) renders blue at its centre -- proves the index buffer path specifically, not just
//   the non-indexed one.
// Check C -- real depth-test proof, not just "something rendered": with DepthStencilState::Default
//   (depth test + write both enabled), draw a far red quad (z=0.9) first, then a near green quad
//   (z=0.1) covering the same screen area second -- green must win. Then swap the draw order (near
//   green first, far red second) -- green must still win, proving genuine depth-buffer
//   comparison, not "last draw wins" painter's-algorithm behaviour.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    bool colorNear(Color a, Color b, int tol = 12)
    {
        return std::abs(a.getRProperty() - b.getRProperty()) <= tol &&
               std::abs(a.getGProperty() - b.getGProperty()) <= tol &&
               std::abs(a.getBProperty() - b.getBProperty()) <= tol;
    }

    Color readCenter(GraphicsDevice& dev)
    {
        const Rectangle region(kSize / 2, kSize / 2, 1, 1);
        Color pixel(0, 0, 0, 0);
        dev.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

    void ApplyBasicEffect(GraphicsDevice& dev)
    {
        static BasicEffect* fx = nullptr;
        if (fx == nullptr) fx = new BasicEffect(dev);
        fx->setWorldProperty(Matrix::getIdentityProperty());
        fx->setViewProperty(Matrix::getIdentityProperty());
        fx->setProjectionProperty(Matrix::getIdentityProperty());
        fx->VertexColorEnabled = true;
        fx->Apply();
    }

    // Full-screen quad (clip-space -1..1) at the given Z (0=near, 1=far under XNA/WebGPU's [0,1]
    // depth range), single solid colour.
    void DrawFullScreenQuad(GraphicsDevice& dev, float z, const Color& color)
    {
        const VertexPositionColor verts[6] = {
            { Vector3(-1.0f,  1.0f, z), color },
            { Vector3(-1.0f, -1.0f, z), color },
            { Vector3( 1.0f, -1.0f, z), color },
            { Vector3(-1.0f,  1.0f, z), color },
            { Vector3( 1.0f, -1.0f, z), color },
            { Vector3( 1.0f,  1.0f, z), color },
        };
        ApplyBasicEffect(dev);
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
    }
}

class WebGpuColored3DTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
    int passCount_ = 0;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        // Check A: DrawUserPrimitives (non-indexed).
        dev.Clear(Color::Black);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        DrawFullScreenQuad(dev, 0.5f, Color::Red);
        check(colorNear(readCenter(dev), Color::Red),
              "DrawUserPrimitives (non-indexed) renders a red quad");

        // Check B: DrawUserIndexedPrimitives (indexed) -- 4 verts + 6 indices, two triangles.
        dev.Clear(Color::Black);
        {
            const VertexPositionColor verts[4] = {
                { Vector3(-1.0f,  1.0f, 0.5f), Color::Blue },
                { Vector3( 1.0f,  1.0f, 0.5f), Color::Blue },
                { Vector3(-1.0f, -1.0f, 0.5f), Color::Blue },
                { Vector3( 1.0f, -1.0f, 0.5f), Color::Blue },
            };
            const std::uint16_t indices[6] = { 0, 1, 2, 1, 3, 2 };
            ApplyBasicEffect(dev);
            dev.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, verts, 0, 4, indices, 0, 2);
        }
        check(colorNear(readCenter(dev), Color::Blue),
              "DrawUserIndexedPrimitives (indexed) renders a blue quad");

        // Check C: real depth-test proof (not painter's-algorithm draw order).
        dev.setDepthStencilStateProperty(DepthStencilState::Default);
        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, Color::Black, 1.0f, 0);
        DrawFullScreenQuad(dev, 0.9f, Color::Red);    // far, drawn first
        DrawFullScreenQuad(dev, 0.1f, Color::Lime);   // near, drawn second -- must win
        const Color farThenNear = readCenter(dev);
        check(colorNear(farThenNear, Color::Lime),
              "depth test: near quad drawn AFTER far quad still wins (far=red first, near=green second)");

        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, Color::Black, 1.0f, 0);
        DrawFullScreenQuad(dev, 0.1f, Color::Lime);   // near, drawn first -- must still win
        DrawFullScreenQuad(dev, 0.9f, Color::Red);    // far, drawn second
        const Color nearThenFar = readCenter(dev);
        check(colorNear(nearThenFar, Color::Lime),
              "depth test: near quad drawn BEFORE far quad still wins (near=green first, far=red second)");

        std::printf("=== %d/4 PASS ===\n", passCount_);
        result_ = (passCount_ == 4) ? 0 : 1;
        Exit();
    }

public:
    WebGpuColored3DTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
        gdm_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
    }

    int getResult() const { return result_; }
};

int main()
{
    WebGpuColored3DTest game;
    game.Run();
    return game.getResult();
}
