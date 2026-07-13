// SPDX-License-Identifier: MS-PL
// Phase 58/59/63 (WEBGPU-23/34/72): verify WebGPUGraphicsBackend's alpha_test3d.wgsl /
// alpha_test_colored3d shader / GetOrCreatePipelineAlphaTest3D() / DrawPrimitivesEx() dispatch for
// AlphaTestEffect's per-pixel alpha discard, ported from VulkanGraphicsBackend's
// alpha_test3d.{vert,frag}.glsl. Reuses the exact same UBO (coloredBindGroupLayout_) and texture
// (texturedBindGroupLayout_) bind groups as textured3d.wgsl -- AlphaTestEffect has no lighting, so
// the primary uniform block's ambient/light0 slots are repurposed for
// {alphaRef, alphaTolerance, passWeight, failWeight} instead.
//
// Each check clears to a distinct background colour, draws an alpha-tested quad, then reads the
// centre pixel: if the quad's own colour shows, the fragment was kept; if the background colour
// still shows, the fragment was discarded.
//
// Check A -- AlphaFunction=Greater, ReferenceAlpha=128, texture alpha=200 (>128): the fragment
//   passes the test and is kept -- centre reads the texture's own colour (green).
// Check B -- same AlphaFunction/ReferenceAlpha, texture alpha=50 (<128): the fragment fails and is
//   discarded -- centre still shows the background clear colour (red), proving `discard` genuinely
//   ran, not just "always draw".
// Check C -- stride 24 (VertexPositionColorTexture, VertexColorEnabled=true): a passing alpha test
//   with a blue per-vertex tint over a white texture renders blue -- proves vertex-colour mixing
//   and alpha-test discard both work together in the combined shader.
// Check D -- DrawIndexedPrimitives counterpart of a discarding case (stride 20): proves the indexed
//   dispatch path applies the same discard logic, not just the non-indexed path.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
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

    VertexBuffer MakePosColorTexQuad(GraphicsDevice& dev, Color vertexColor)
    {
        VertexBuffer vb(dev, PosColorTexDecl(), 6, BufferUsage::None);
        const VertexPositionColorTexture verts[6] = {
            { Vector3(-1.0f,  1.0f, 0.5f), vertexColor, Vector2(0.0f, 0.0f) },
            { Vector3(-1.0f, -1.0f, 0.5f), vertexColor, Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.5f), vertexColor, Vector2(1.0f, 1.0f) },
            { Vector3(-1.0f,  1.0f, 0.5f), vertexColor, Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.5f), vertexColor, Vector2(1.0f, 1.0f) },
            { Vector3( 1.0f,  1.0f, 0.5f), vertexColor, Vector2(1.0f, 0.0f) },
        };
        vb.SetData(verts, 0, 6);
        return vb;
    }
}

class WebGpuAlphaTest3DTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    Texture2D greenAlpha200_;
    Texture2D greenAlpha50_;
    Texture2D whiteAlpha200_;
    Texture2D whiteAlpha10_;
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
        greenAlpha200_ = Texture2D::CreateFromPixels(dev, 1, 1, std::vector<std::uint8_t>{0, 255, 0, 200});
        greenAlpha50_  = Texture2D::CreateFromPixels(dev, 1, 1, std::vector<std::uint8_t>{0, 255, 0, 50});
        whiteAlpha200_ = Texture2D::CreateFromPixels(dev, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 200});
        whiteAlpha10_  = Texture2D::CreateFromPixels(dev, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 10});
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::None);

        // Check A: alpha=200 > ReferenceAlpha=128 (Greater) -- passes, texture colour shows.
        {
            dev.Clear(Color::Black);
            VertexBuffer vb = MakePosTexQuad(dev);
            AlphaTestEffect fx(dev);
            fx.setTextureProperty(&greenAlpha200_);
            fx.setAlphaFunctionProperty(CompareFunction::Greater);
            fx.setReferenceAlphaProperty(128);
            fx.Apply();
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);
            check(colorNear(readCenter(dev), Color::Lime),
                  "alpha(200) > ReferenceAlpha(128) passes the test, texture colour renders");
        }

        // Check B: alpha=50 < ReferenceAlpha=128 (Greater) -- fails, fragment discarded, the red
        // background clear colour is still visible underneath.
        {
            dev.Clear(Color::Red);
            VertexBuffer vb = MakePosTexQuad(dev);
            AlphaTestEffect fx(dev);
            fx.setTextureProperty(&greenAlpha50_);
            fx.setAlphaFunctionProperty(CompareFunction::Greater);
            fx.setReferenceAlphaProperty(128);
            fx.Apply();
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);
            check(colorNear(readCenter(dev), Color::Red),
                  "alpha(50) < ReferenceAlpha(128) fails the test, fragment is discarded");
        }

        // Check C: stride 24, VertexColorEnabled=true, a passing alpha test -- vertex colour tint
        // (blue) over a white texture still renders blue.
        {
            dev.Clear(Color::Black);
            VertexBuffer vb = MakePosColorTexQuad(dev, Color::Blue);
            AlphaTestEffect fx(dev);
            fx.setTextureProperty(&whiteAlpha200_);
            fx.setVertexColorEnabledProperty(true);
            fx.setAlphaFunctionProperty(CompareFunction::Greater);
            fx.setReferenceAlphaProperty(128);
            fx.Apply();
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);
            check(colorNear(readCenter(dev), Color::Blue),
                  "stride-24 vertex colour tint (blue) survives a passing alpha test");
        }

        // Check D: DrawIndexedPrimitives counterpart of a discarding case (stride 20).
        {
            dev.Clear(Color::Lime);
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

            AlphaTestEffect fx(dev);
            fx.setTextureProperty(&whiteAlpha10_);
            fx.setAlphaFunctionProperty(CompareFunction::Greater);
            fx.setReferenceAlphaProperty(128);
            fx.Apply();
            dev.SetVertexBuffer(&vb);
            dev.SetIndexBuffer(&ib);
            dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
            dev.SetVertexBuffer(nullptr);
            dev.SetIndexBuffer(nullptr);
            check(colorNear(readCenter(dev), Color::Lime),
                  "DrawIndexedPrimitives: alpha(10) < ReferenceAlpha(128) discards, background shows");
        }

        std::printf("=== %d/4 PASS ===\n", passCount_);
        result_ = (passCount_ == 4) ? 0 : 1;
        Exit();
    }

public:
    WebGpuAlphaTest3DTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    WebGpuAlphaTest3DTest game;
    game.Run();
    return game.getResult();
}
