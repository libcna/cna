// SPDX-License-Identifier: MS-PL
// Second-capability pixel-oracle test for the PortableGL (rswinkle/PortableGL) graphics renderer:
// a real textured SpriteBatch draw and a real indexed 3D draw, each verified through
// GetBackBufferData() readback -- not merely "did not throw".
//
// Check A -- SpriteBatch.Begin/Draw/End with a real Texture2D produces the exact source-texture
//   color across the destination rectangle, proving PortableGLSpriteBatchRenderer's textured-quad
//   path genuinely samples the bound texture through PortableGL's own texture2D()/glDrawArrays,
//   not a CPU blit.
// Check B -- DrawIndexedPrimitives (VertexBuffer + IndexBuffer + BasicEffect, 4 vertices / 6
//   indices tiling NDC -1..1) produces the exact vertex color at the backbuffer's center pixel,
//   proving the real glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ...)/glDrawElements path.
// Check C -- a textured BasicEffect with VertexPositionTexture draws through the ordinary 3D
//   route and samples Texture0, which is the stock-effect shape cna-template's cube uses.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include "CNA/Internal/Renderers/PortableGL/PortableGLRenderer.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::PortableGL;

namespace
{
    VertexDeclaration PosColorDecl()
    {
        return VertexDeclaration(16, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,    0),
        });
    }
}

class PortableGLTexturedDrawTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> texture_;
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
        spriteBatch_ = std::make_unique<SpriteBatch>(getGraphicsDeviceProperty());
        // A single solid-blue 2x2 texture -- small enough that TextureFilter differences between
        // NEAREST/LINEAR sampling cannot change the expected color anywhere inside the quad.
        const std::vector<std::uint8_t> bluePixels = {
            0, 0, 255, 255,  0, 0, 255, 255,
            0, 0, 255, 255,  0, 0, 255, 255,
        };
        texture_ = std::make_unique<Texture2D>(
            Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 2, 2, bluePixels));
    }

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        // Check A: a real textured SpriteBatch draw, verified by readback.
        {
            dev.Clear(Color::Black);

            bool threw = false;
            try
            {
                spriteBatch_->Begin();
                spriteBatch_->Draw(*texture_, Rectangle(0, 0, 64, 64), Color::White);
                spriteBatch_->End();
            }
            catch (...) { threw = true; }
            check(!threw, "SpriteBatch Begin/Draw/End with a real Texture2D does not throw");

            const Rectangle centerRegion(30, 30, 4, 4);
            std::vector<Color> centerPixels(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&centerRegion, centerPixels.data(), 0, static_cast<int>(centerPixels.size()));
            bool allBlue = true;
            for (const Color& p : centerPixels)
            {
                if (p.getRProperty() != 0 || p.getGProperty() != 0 || p.getBProperty() != 255)
                {
                    allBlue = false;
                    break;
                }
            }
            check(allBlue,
                  "the real PortableGL textured-quad path samples the exact source texture color");
        }

        // Check B: a real indexed 3D draw, verified by readback.
        {
            dev.Clear(Color::Black);

            VertexBuffer vb(dev, PosColorDecl(), 4, BufferUsage::None);
            const VertexPositionColor verts[4] = {
                { Vector3(-1.0f, 1.0f, 0.0f),  Color(0, 255, 0, 255) }, // 0: top-left
                { Vector3(1.0f, 1.0f, 0.0f),   Color(0, 255, 0, 255) }, // 1: top-right
                { Vector3(1.0f, -1.0f, 0.0f),  Color(0, 255, 0, 255) }, // 2: bottom-right
                { Vector3(-1.0f, -1.0f, 0.0f), Color(0, 255, 0, 255) }, // 3: bottom-left
            };
            vb.SetData(verts, 0, 4);

            // Clockwise in XNA's top-left screen space, i.e. front-facing under the default
            // RasterizerState.CullCounterClockwise a preceding SpriteBatch.Begin installed.
            IndexBuffer ib(dev, IndexElementSize::SixteenBits, 6, BufferUsage::None);
            const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
            ib.SetData(indices, 0, 6);

            BasicEffect fx(dev);
            fx.VertexColorEnabled = true;
            fx.Apply();

            bool threw = false;
            try
            {
                dev.SetVertexBuffer(&vb);
                dev.SetIndexBuffer(&ib);
                dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
                dev.SetVertexBuffer(nullptr);
                dev.SetIndexBuffer(nullptr);
            }
            catch (...) { threw = true; }
            check(!threw, "DrawIndexedPrimitives (full-viewport indexed quad) does not throw");

            const Rectangle centerRegion(30, 30, 4, 4);
            std::vector<Color> centerPixels(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&centerRegion, centerPixels.data(), 0, static_cast<int>(centerPixels.size()));
            bool allGreen = true;
            for (const Color& p : centerPixels)
            {
                if (p.getRProperty() != 0 || p.getGProperty() != 255 || p.getBProperty() != 0)
                {
                    allGreen = false;
                    break;
                }
            }
            check(allGreen,
                  "the real PortableGL glDrawElements path draws the exact indexed-vertex color");
        }

        // Check C: the ordinary textured BasicEffect route used by cna-template's cube.
        {
            dev.Clear(Color::Black);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);

            const VertexPositionTexture verts[6] = {
                {Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 0.0f)},
                {Vector3( 1.0f,  1.0f, 0.0f), Vector2(1.0f, 0.0f)},
                {Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 1.0f)},
                {Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 0.0f)},
                {Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 1.0f)},
                {Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 1.0f)},
            };

            BasicEffect fx(dev);
            fx.setTextureEnabledProperty(true);
            fx.setTextureProperty(texture_.get());
            fx.VertexColorEnabled = false;
            fx.setViewProperty(Matrix::CreateLookAt(
                Vector3(0.0f, 0.0f, 3.0f), Vector3::Zero, Vector3::Up));
            fx.setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(
                0.78539816339f, 1.0f, 0.1f, 100.0f));
            fx.Apply();

            bool threw = false;
            try
            {
                dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
            }
            catch (...) { threw = true; }
            check(!threw, "textured BasicEffect accepts VertexPositionTexture");

            const Rectangle centerRegion(30, 30, 4, 4);
            std::vector<Color> centerPixels(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&centerRegion, centerPixels.data(), 0,
                                  static_cast<int>(centerPixels.size()));
            bool allBlue = true;
            for (const Color& p : centerPixels)
            {
                if (p.getRProperty() != 0 || p.getGProperty() != 0 || p.getBProperty() != 255)
                {
                    allBlue = false;
                    break;
                }
            }
            check(allBlue,
                  "perspective textured 3D samples BasicEffect.Texture at the projected center");

            const std::vector<std::uint8_t> columnPixels = {
                255, 0, 0, 255,  0, 255, 0, 255,
                255, 0, 0, 255,  0, 255, 0, 255,
            };
            Texture2D columns = Texture2D::CreateFromPixels(dev, 2, 2, columnPixels);
            VertexPositionTexture outsideVerts[6];
            for (int i = 0; i < 6; ++i)
                outsideVerts[i] = VertexPositionTexture(verts[i].Position, Vector2(1.25f, 0.5f));
            fx.setTextureProperty(&columns);
            fx.Apply();
            const auto centerPixel = [&dev]() {
                Color pixel(0, 0, 0, 0);
                const Rectangle center(32, 32, 1, 1);
                dev.GetBackBufferData(&center, &pixel, 0, 1);
                return pixel;
            };

            dev.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
            dev.Clear(Color::Black);
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, outsideVerts, 0, 2);
            check(centerPixel().getGProperty() == 255,
                  "3D SamplerStates[0]=PointClamp selects the right edge texel");

            dev.getSamplerStatesProperty()[0] = SamplerState::PointWrap;
            dev.Clear(Color::Black);
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, outsideVerts, 0, 2);
            check(centerPixel().getRProperty() == 255,
                  "3D SamplerStates[0]=PointWrap wraps the same U coordinate to the left texel");
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, 8);
        result_ = (passCount_ == 8) ? 0 : 1;
        Exit();
    }

public:
    PortableGLTexturedDrawTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    PortableGLTexturedDrawTest game;
    game.Run();
    return game.getResult();
}
