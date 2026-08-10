// SPDX-License-Identifier: MS-PL
// Phase 63: verify WebGPURenderer::DrawPrimitivesEx()/DrawIndexedPrimitivesEx() forward the
// caller's real GpuDrawParams (via BasicEffect::FillGpuDrawParams()) into colored3d.wgsl's uniform
// buffer, rather than the DrawColoredPrimitives fallback's hardcoded diffuseColor=white/
// vertexColorEnabled=true.
//
// Check A -- a white-vertex-colored quad, BasicEffect.VertexColorEnabled=false and
//   DiffuseColor=red: must render red (the real DiffuseColor), not white (the raw vertex colour
//   the old DrawColoredPrimitives fallback would have shown regardless of DiffuseColor).
// Check B -- the indexed counterpart (DrawIndexedPrimitives): same proof, via a 4-vertex + 6-index
//   quad and DiffuseColor=blue this time.
// Check C -- VertexColorEnabled=true (the common case) with per-vertex green: must still render
//   green -- confirms the new dispatch path did not regress the vertex-colour-enabled case that
//   DrawColoredPrimitives already covered.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    bool colorNear(Color a, Color b, int tol = 16)
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

    VertexDeclaration PosColorDecl()
    {
        return VertexDeclaration(16, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,    0),
        });
    }
}

class WebGpuDrawPrimitivesExTest : public Game
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
        dev.setDepthStencilStateProperty(DepthStencilState::None);

        // Check A: VertexColorEnabled=false + DiffuseColor=red must win over white vertex colour.
        {
            dev.Clear(Color::Black);
            auto decl = PosColorDecl();
            VertexBuffer vb(dev, decl, 6, BufferUsage::None);
            const VertexPositionColor verts[6] = {
                { Vector3(-1.0f,  1.0f, 0.5f), Color::White },
                { Vector3(-1.0f, -1.0f, 0.5f), Color::White },
                { Vector3( 1.0f, -1.0f, 0.5f), Color::White },
                { Vector3(-1.0f,  1.0f, 0.5f), Color::White },
                { Vector3( 1.0f, -1.0f, 0.5f), Color::White },
                { Vector3( 1.0f,  1.0f, 0.5f), Color::White },
            };
            vb.SetData(verts, 0, 6);

            BasicEffect fx(dev);
            fx.VertexColorEnabled = false;
            fx.setDiffuseColorProperty(Vector3(1.0f, 0.0f, 0.0f));
            fx.Apply();

            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);

            check(colorNear(readCenter(dev), Color::Red),
                  "DrawPrimitives: VertexColorEnabled=false + DiffuseColor=red renders red, not white");
        }

        // Check B: indexed counterpart, DiffuseColor=blue.
        {
            dev.Clear(Color::Black);
            auto decl = PosColorDecl();
            VertexBuffer vb(dev, decl, 4, BufferUsage::None);
            const VertexPositionColor verts[4] = {
                { Vector3(-1.0f,  1.0f, 0.5f), Color::White },
                { Vector3( 1.0f,  1.0f, 0.5f), Color::White },
                { Vector3(-1.0f, -1.0f, 0.5f), Color::White },
                { Vector3( 1.0f, -1.0f, 0.5f), Color::White },
            };
            vb.SetData(verts, 0, 4);
            IndexBuffer ib(dev, IndexElementSize::SixteenBits, 6, BufferUsage::None);
            const std::uint16_t indices[6] = { 0, 1, 2, 1, 3, 2 };
            ib.SetData(indices, 0, 6);

            BasicEffect fx(dev);
            fx.VertexColorEnabled = false;
            fx.setDiffuseColorProperty(Vector3(0.0f, 0.0f, 1.0f));
            fx.Apply();

            dev.SetVertexBuffer(&vb);
            dev.SetIndexBuffer(&ib);
            dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
            dev.SetVertexBuffer(nullptr);
            dev.SetIndexBuffer(nullptr);

            check(colorNear(readCenter(dev), Color::Blue),
                  "DrawIndexedPrimitives: VertexColorEnabled=false + DiffuseColor=blue renders blue");
        }

        // Check C: VertexColorEnabled=true (common case) still works -- no regression.
        {
            dev.Clear(Color::Black);
            auto decl = PosColorDecl();
            VertexBuffer vb(dev, decl, 6, BufferUsage::None);
            const VertexPositionColor verts[6] = {
                { Vector3(-1.0f,  1.0f, 0.5f), Color::Lime },
                { Vector3(-1.0f, -1.0f, 0.5f), Color::Lime },
                { Vector3( 1.0f, -1.0f, 0.5f), Color::Lime },
                { Vector3(-1.0f,  1.0f, 0.5f), Color::Lime },
                { Vector3( 1.0f, -1.0f, 0.5f), Color::Lime },
                { Vector3( 1.0f,  1.0f, 0.5f), Color::Lime },
            };
            vb.SetData(verts, 0, 6);

            BasicEffect fx(dev);
            fx.VertexColorEnabled = true;
            fx.Apply();

            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);

            check(colorNear(readCenter(dev), Color::Lime),
                  "DrawPrimitives: VertexColorEnabled=true still renders the per-vertex colour");
        }

        std::printf("=== %d/3 PASS ===\n", passCount_);
        result_ = (passCount_ == 3) ? 0 : 1;
        Exit();
    }

public:
    WebGpuDrawPrimitivesExTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    WebGpuDrawPrimitivesExTest game;
    game.Run();
    return game.getResult();
}
