// SPDX-License-Identifier: MS-PL
// Phase 58/59/63: verify WebGPUGraphicsBackend's colored_textured3d.wgsl /
// GetOrCreatePipelineColoredTextured3D() / DrawPrimitivesEx() dispatch for stride-24
// (VertexPositionColorTexture) draws -- combines colored3d.wgsl's vertex-colour mixing with
// textured3d.wgsl's texture sampling in one shader, reusing the exact same UBO/texture bind
// groups as textured3d.wgsl (just a different vertex layout + shader module).
//
// Check A -- a white 1x1 texture with per-vertex RED colour, VertexColorEnabled=true: must render
//   red (texture(white) * vertexColor(red) = red) -- proves vertex colour reaches this shader.
// Check B -- a solid GREEN 1x1 texture with per-vertex colour WHITE, VertexColorEnabled=true: must
//   render green (texture(green) * vertexColor(white) = green) -- proves texture sampling still
//   works in the combined shader, not just vertex colour.
// Check C -- VertexColorEnabled=false with a non-white DiffuseColor and a white texture: must use
//   DiffuseColor (not the per-vertex colour, which is deliberately set to a different colour to
//   prove it's being ignored) -- proves the vertexColorEnabled toggle still works here too.
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
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"

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

    VertexDeclaration PosColorTexDecl()
    {
        return VertexDeclaration(24, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,    0),
            VertexElement(16, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
        });
    }

    VertexBuffer MakeQuad(GraphicsDevice& dev, Color vertexColor)
    {
        auto decl = PosColorTexDecl();
        VertexBuffer vb(dev, decl, 6, BufferUsage::None);
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

class WebGpuColoredTextured3DTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    Texture2D whiteTex_;
    Texture2D greenTex_;
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
        whiteTex_ = Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 1, 1,
                                                 std::vector<std::uint8_t>{255, 255, 255, 255});
        greenTex_ = Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 1, 1,
                                                 std::vector<std::uint8_t>{0, 255, 0, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::None);

        // Check A: white texture * red vertex colour -> red.
        {
            dev.Clear(Color::Black);
            VertexBuffer vb = MakeQuad(dev, Color::Red);
            BasicEffect fx(dev);
            fx.setTextureEnabledProperty(true);
            fx.setTextureProperty(&whiteTex_);
            fx.VertexColorEnabled = true;
            fx.Apply();
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);
            check(colorNear(readCenter(dev), Color::Red),
                  "white texture * red vertex colour renders red");
        }

        // Check B: green texture * white vertex colour -> green.
        {
            dev.Clear(Color::Black);
            VertexBuffer vb = MakeQuad(dev, Color::White);
            BasicEffect fx(dev);
            fx.setTextureEnabledProperty(true);
            fx.setTextureProperty(&greenTex_);
            fx.VertexColorEnabled = true;
            fx.Apply();
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);
            check(colorNear(readCenter(dev), Color::Lime),
                  "green texture * white vertex colour renders green");
        }

        // Check C: VertexColorEnabled=false -- DiffuseColor wins over a deliberately-different
        // per-vertex colour (blue), with a white texture (texture*diffuse = diffuse).
        {
            dev.Clear(Color::Black);
            VertexBuffer vb = MakeQuad(dev, Color::Blue);
            BasicEffect fx(dev);
            fx.setTextureEnabledProperty(true);
            fx.setTextureProperty(&whiteTex_);
            fx.VertexColorEnabled = false;
            fx.setDiffuseColorProperty(Vector3(1.0f, 0.0f, 0.0f));
            fx.Apply();
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);
            check(colorNear(readCenter(dev), Color::Red),
                  "VertexColorEnabled=false uses DiffuseColor(red), ignoring vertex colour(blue)");
        }

        std::printf("=== %d/3 PASS ===\n", passCount_);
        result_ = (passCount_ == 3) ? 0 : 1;
        Exit();
    }

public:
    WebGpuColoredTextured3DTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    WebGpuColoredTextured3DTest game;
    game.Run();
    return game.getResult();
}
