// SPDX-License-Identifier: MS-PL
// Phase 58/59/63: verify WebGPURenderer's textured3d.wgsl / GetOrCreatePipelineTextured3D()
// / DrawPrimitivesEx() dispatch for stride-20 (VertexPositionTexture) draws -- the next WGSL
// shader variant after colored3d.wgsl, reusing the same UBO/bind-group-0 (WEBGPU-13) and adding a
// second bind group (group 1: sampler + texture, mirrors the SpriteBatch layout).
//
// Check A -- a stride-20 quad sampling a solid green 1x1 texture (TextureEnabled=true,
//   DiffuseColor left at its default white) renders green -- proves real texture sampling, not
//   just "a pipeline was created".
// Check B -- the same quad with DiffuseColor=red multiplies texture*diffuse (green*red channel-wise
//   = black, XNA's real BasicEffect semantics) -- proves DiffuseColor genuinely reaches this
//   shader too, not just the texture path.
// Check C -- DrawIndexedPrimitives (indexed) counterpart, sampling a solid blue texture.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

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

    VertexDeclaration PosTexDecl()
    {
        return VertexDeclaration(20, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
        });
    }
}

class WebGpuTextured3DTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    Texture2D greenTex_;
    Texture2D blueTex_;
    bool done_ = false;
    int passCount_ = 0;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

protected:
    void LoadContent() override
    {
        greenTex_ = Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 1, 1,
                                                 std::vector<std::uint8_t>{0, 255, 0, 255});
        blueTex_ = Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 1, 1,
                                                std::vector<std::uint8_t>{0, 0, 255, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::None);

        auto decl = PosTexDecl();

        // Check A: solid green texture, default (white) DiffuseColor -> renders green.
        {
            dev.Clear(Color::Black);
            VertexBuffer vb(dev, decl, 6, BufferUsage::None);
            const VertexPositionTexture verts[6] = {
                { Vector3(-1.0f,  1.0f, 0.5f), Vector2(0.0f, 0.0f) },
                { Vector3(-1.0f, -1.0f, 0.5f), Vector2(0.0f, 1.0f) },
                { Vector3( 1.0f, -1.0f, 0.5f), Vector2(1.0f, 1.0f) },
                { Vector3(-1.0f,  1.0f, 0.5f), Vector2(0.0f, 0.0f) },
                { Vector3( 1.0f, -1.0f, 0.5f), Vector2(1.0f, 1.0f) },
                { Vector3( 1.0f,  1.0f, 0.5f), Vector2(1.0f, 0.0f) },
            };
            vb.SetData(verts, 0, 6);

            BasicEffect fx(dev);
            fx.setTextureEnabledProperty(true);
            fx.setTextureProperty(&greenTex_);
            fx.Apply();

            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);

            check(colorNear(readCenter(dev), Color::Lime),
                  "DrawPrimitives: textured quad samples the bound green texture");
        }

        // Check B: same texture, DiffuseColor=red -> texture*diffuse = black (green*red = 0).
        {
            dev.Clear(Color::White);
            VertexBuffer vb(dev, decl, 6, BufferUsage::None);
            const VertexPositionTexture verts[6] = {
                { Vector3(-1.0f,  1.0f, 0.5f), Vector2(0.0f, 0.0f) },
                { Vector3(-1.0f, -1.0f, 0.5f), Vector2(0.0f, 1.0f) },
                { Vector3( 1.0f, -1.0f, 0.5f), Vector2(1.0f, 1.0f) },
                { Vector3(-1.0f,  1.0f, 0.5f), Vector2(0.0f, 0.0f) },
                { Vector3( 1.0f, -1.0f, 0.5f), Vector2(1.0f, 1.0f) },
                { Vector3( 1.0f,  1.0f, 0.5f), Vector2(1.0f, 0.0f) },
            };
            vb.SetData(verts, 0, 6);

            BasicEffect fx(dev);
            fx.setTextureEnabledProperty(true);
            fx.setTextureProperty(&greenTex_);
            fx.setDiffuseColorProperty(Vector3(1.0f, 0.0f, 0.0f));
            fx.Apply();

            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);

            check(colorNear(readCenter(dev), Color::Black),
                  "DrawPrimitives: texture(green) * DiffuseColor(red) multiplies to black");
        }

        // Check C: indexed counterpart, solid blue texture.
        {
            dev.Clear(Color::Black);
            VertexBuffer vb(dev, decl, 4, BufferUsage::None);
            const VertexPositionTexture verts[4] = {
                { Vector3(-1.0f,  1.0f, 0.5f), Vector2(0.0f, 0.0f) },
                { Vector3( 1.0f,  1.0f, 0.5f), Vector2(1.0f, 0.0f) },
                { Vector3(-1.0f, -1.0f, 0.5f), Vector2(0.0f, 1.0f) },
                { Vector3( 1.0f, -1.0f, 0.5f), Vector2(1.0f, 1.0f) },
            };
            vb.SetData(verts, 0, 4);
            IndexBuffer ib(dev, IndexElementSize::SixteenBits, 6, BufferUsage::None);
            const std::uint16_t indices[6] = { 0, 1, 2, 1, 3, 2 };
            ib.SetData(indices, 0, 6);

            BasicEffect fx(dev);
            fx.setTextureEnabledProperty(true);
            fx.setTextureProperty(&blueTex_);
            fx.Apply();

            dev.SetVertexBuffer(&vb);
            dev.SetIndexBuffer(&ib);
            dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
            dev.SetVertexBuffer(nullptr);
            dev.SetIndexBuffer(nullptr);

            check(colorNear(readCenter(dev), Color::Blue),
                  "DrawIndexedPrimitives: textured quad samples the bound blue texture");
        }

        std::printf("=== %d/3 PASS ===\n", passCount_);
        result_ = (passCount_ == 3) ? 0 : 1;
        Exit();
    }

public:
    WebGpuTextured3DTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    WebGpuTextured3DTest game;
    game.Run();
    return game.getResult();
}
