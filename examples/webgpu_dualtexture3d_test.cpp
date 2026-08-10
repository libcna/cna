// SPDX-License-Identifier: MS-PL
// Phase 58/59/63 (WEBGPU-24): verify WebGPURenderer's dual_texture3d.wgsl /
// dual_texture_colored3d shader / GetOrCreatePipelineDualTexture3D() / DrawPrimitivesEx() dispatch
// for DualTextureEffect, ported from VulkanRenderer's dual_texture3d.{vert,frag}.glsl /
// dual_texture_colored3d.vert.glsl. The first WebGPU 3D shader with a genuinely new bind-group
// shape: group 1 needs THREE bindings (one shared sampler + two textures), unlike every prior
// shader's single-texture group 1.
//
// FNA's DualTextureEffect formula: `tex1.rgb *= 2.0; result = tex1 * tex2 * tint` (both textures
// sampled at the same UV).
//
// Check A -- texture0 = solid mid-grey (128,128,128), texture1 = solid white: mid-grey doubled and
//   clamped renders as white after multiplying by a white overlay -- proves the real "*2.0 boost"
//   formula runs, not a plain unscaled multiply (which would render mid-grey instead).
// Check B -- texture0 = solid red, texture1 = solid green: red(boosted, still red after clamping)
//   times green multiplies to black on every channel -- proves BOTH textures are genuinely
//   sampled and multiplied together, not just one of them.
// Check C -- stride 24 (VertexPositionColorTexture), VertexColorEnabled=true, white*white*green
//   vertex tint: renders green -- proves per-vertex colour tint still applies alongside
//   dual-texture sampling.
// Check D -- DrawIndexedPrimitives counterpart of Check B (stride 20).
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
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
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
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

    VertexBuffer MakePosTexQuad(GraphicsDevice& dev)
    {
        VertexBuffer vb(dev, PosTexDecl(), 6, BufferUsage::None);
        const VertexPositionTexture verts[6] = {
            { Vector3(-1.0f,  1.0f, 0.5f), Vector2(0.0f, 0.0f) },
            { Vector3(-1.0f, -1.0f, 0.5f), Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.5f), Vector2(1.0f, 1.0f) },
            { Vector3(-1.0f,  1.0f, 0.5f), Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.5f), Vector2(1.0f, 1.0f) },
            { Vector3( 1.0f,  1.0f, 0.5f), Vector2(1.0f, 0.0f) },
        };
        vb.SetData(verts, 0, 6);
        return vb;
    }

    VertexDeclaration PosColorTexDecl()
    {
        return VertexDeclaration(24, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,    0),
            VertexElement(16, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
        });
    }
}

class WebGpuDualTexture3DTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    Texture2D grey128_;
    Texture2D white_;
    Texture2D red_;
    Texture2D green_;
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
        auto& dev = getGraphicsDeviceProperty();
        grey128_ = Texture2D::CreateFromPixels(dev, 1, 1, std::vector<std::uint8_t>{128, 128, 128, 255});
        white_   = Texture2D::CreateFromPixels(dev, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255});
        red_     = Texture2D::CreateFromPixels(dev, 1, 1, std::vector<std::uint8_t>{255, 0, 0, 255});
        green_   = Texture2D::CreateFromPixels(dev, 1, 1, std::vector<std::uint8_t>{0, 255, 0, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::None);

        // Check A: grey(128) doubled + clamped, times white overlay -> white.
        {
            dev.Clear(Color::Black);
            VertexBuffer vb = MakePosTexQuad(dev);
            DualTextureEffect fx(dev);
            fx.setTextureProperty(&grey128_);
            fx.setTexture2Property(&white_);
            fx.Apply();
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);
            check(colorNear(readCenter(dev), Color::White),
                  "grey texture0 doubled+clamped times white texture1 renders white");
        }

        // Check B: red times green -> black (proves both textures genuinely multiply).
        {
            dev.Clear(Color::Black);
            VertexBuffer vb = MakePosTexQuad(dev);
            DualTextureEffect fx(dev);
            fx.setTextureProperty(&red_);
            fx.setTexture2Property(&green_);
            fx.Apply();
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);
            check(colorNear(readCenter(dev), Color::Black),
                  "red texture0 times green texture1 multiplies to black on every channel");
        }

        // Check C: stride 24, VertexColorEnabled=true, white*white*green tint -> green.
        {
            dev.Clear(Color::Black);
            VertexBuffer vb(dev, PosColorTexDecl(), 6, BufferUsage::None);
            const VertexPositionColorTexture verts[6] = {
                { Vector3(-1.0f,  1.0f, 0.5f), Color::Green, Vector2(0.0f, 0.0f) },
                { Vector3(-1.0f, -1.0f, 0.5f), Color::Green, Vector2(0.0f, 1.0f) },
                { Vector3( 1.0f, -1.0f, 0.5f), Color::Green, Vector2(1.0f, 1.0f) },
                { Vector3(-1.0f,  1.0f, 0.5f), Color::Green, Vector2(0.0f, 0.0f) },
                { Vector3( 1.0f, -1.0f, 0.5f), Color::Green, Vector2(1.0f, 1.0f) },
                { Vector3( 1.0f,  1.0f, 0.5f), Color::Green, Vector2(1.0f, 0.0f) },
            };
            vb.SetData(verts, 0, 6);
            DualTextureEffect fx(dev);
            fx.setTextureProperty(&white_);
            fx.setTexture2Property(&white_);
            fx.setVertexColorEnabledProperty(true);
            fx.Apply();
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);
            check(colorNear(readCenter(dev), Color::Lime),
                  "stride-24 vertex colour tint (green) survives dual-texture sampling");
        }

        // Check D: DrawIndexedPrimitives counterpart of Check B.
        {
            dev.Clear(Color::Black);
            VertexBuffer vb(dev, PosTexDecl(), 4, BufferUsage::None);
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

            DualTextureEffect fx(dev);
            fx.setTextureProperty(&red_);
            fx.setTexture2Property(&green_);
            fx.Apply();
            dev.SetVertexBuffer(&vb);
            dev.SetIndexBuffer(&ib);
            dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
            dev.SetVertexBuffer(nullptr);
            dev.SetIndexBuffer(nullptr);
            check(colorNear(readCenter(dev), Color::Black),
                  "DrawIndexedPrimitives: red times green multiplies to black");
        }

        std::printf("=== %d/4 PASS ===\n", passCount_);
        result_ = (passCount_ == 4) ? 0 : 1;
        Exit();
    }

public:
    WebGpuDualTexture3DTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    WebGpuDualTexture3DTest game;
    game.Run();
    return game.getResult();
}
