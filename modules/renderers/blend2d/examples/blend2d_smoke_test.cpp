// SPDX-License-Identifier: MS-PL
// plans/plan_blend2d.md: end-to-end smoke test for the Blend2D (CPU 2D vector rasterizer) graphics
// renderer. Constructs a real Game/GraphicsDeviceManager under CNA_GRAPHICS_RENDERER=BLEND2D,
// runs a few frames, and asserts genuine rasterized pixel output rather than merely "did not
// throw":
//
// Check A -- SDL's video subsystem WAS initialized and GameWindow handle returns a real
//   window -- this renderer genuinely presents to a window (unlike STUB/SOFTWARE/HEADLESS).
// Check B -- a 3D DrawPrimitives call (VertexBuffer + BasicEffect) throws -- Blend2D has no 3D
//   pipeline and truthfully rejects it rather than silently no-opping.
// Check C -- Clear() followed by GetBackBufferData() round-trips the exact clear colour.
// Check D -- a SpriteBatch draw of an opaque red 2x2 texture at a known destination rectangle
//   produces the exact expected red pixels at that rectangle and leaves the surrounding clear
//   colour untouched -- genuine Blend2D rasterization, not a recorded no-op.
// Check E -- a RenderTarget2D bound, cleared to a distinct colour, unbound, then sampled back
//   onto the backbuffer via an ordinary SpriteBatch draw reproduces that exact colour --
//   the render-target "render, unbind, sample" round trip.
// Check F -- CreateVertexBuffer()/CreateIndexBuffer16() honestly report back the vertex/index
//   count they were given.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "CNA/Internal/Renderers/Blend2D/Blend2DRenderer.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::Blend2D;

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

class Blend2DSmokeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> redTexture_;
    int frame_ = 0;
    int passCount_ = 0;
    static constexpr int kTotalChecks = 9;
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
        redTexture_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
            getGraphicsDeviceProperty(), 2, 2,
            std::vector<std::uint8_t>{
                255, 0, 0, 255,  255, 0, 0, 255,
                255, 0, 0, 255,  255, 0, 0, 255}));
    }

    void Draw(const GameTime&) override
    {
        ++frame_;
        auto& dev = getGraphicsDeviceProperty();
        auto& renderer = static_cast<Blend2DRenderer&>(dev.GetRenderer());

        if (frame_ == 1)
        {
            // Check A: a real window/video subsystem exists.
            check(SDL_WasInit(SDL_INIT_VIDEO) != 0, "SDL_INIT_VIDEO was initialized under the Blend2D renderer");
            check(reinterpret_cast<SDL_Window*>(getWindowProperty().getHandleProperty()) != nullptr, "GraphicsDevice has a real window under the Blend2D renderer");
        }

        dev.Clear(Color(0, 0, 64, 255));

        // Check B: Blend2D has no 3D pipeline -- DrawPrimitives must throw, not silently no-op.
        VertexBuffer vb(dev, PosColorDecl(), 3, BufferUsage::None);
        const VertexPositionColor verts[3] = {
            { Vector3(0.0f, 1.0f, 0.0f), Color::Red },
            { Vector3(-1.0f, -1.0f, 0.0f), Color::Green },
            { Vector3(1.0f, -1.0f, 0.0f), Color::Blue },
        };
        vb.SetData(verts, 0, 3);
        BasicEffect fx(dev);
        fx.Apply();

        bool threeDThrew = false;
        try
        {
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
            dev.SetVertexBuffer(nullptr);
        }
        catch (...) { threeDThrew = true; }
        if (frame_ == 1) check(threeDThrew, "DrawPrimitives (3D) throws -- Blend2D truthfully has no 3D pipeline");

        // Check F: the buffer handles honestly report back their counts.
        if (frame_ == 1)
        {
            IndexBuffer ib(dev, IndexElementSize::SixteenBits, 3, BufferUsage::None);
            const std::uint16_t indices[3] = {0, 1, 2};
            ib.SetData(indices, 0, 3);
            check(vb.getVertexCountProperty() == 3, "VertexBuffer reports the vertex count it was given");
            check(ib.getIndexCountProperty() == 3, "IndexBuffer reports the index count it was given");
        }

        // Re-clear to a known colour for the pixel checks below (the throwing 3D call above must
        // not have left partial raster state behind).
        dev.Clear(Color(0, 0, 64, 255));

        // Check C: Clear()/GetBackBufferData() exact round trip.
        if (frame_ == 1)
        {
            std::vector<Color> pixel(1, Color::Black);
            Rectangle originRect(0, 0, 1, 1);
            dev.GetBackBufferData(&originRect, pixel.data(), 0, 1);
            check(pixel[0].getRProperty() == 0 && pixel[0].getGProperty() == 0 &&
                  pixel[0].getBProperty() == 64 && pixel[0].getAProperty() == 255,
                  "GetBackBufferData reads back the exact Clear() colour");
        }

        // Check D: a real SpriteBatch draw rasterizes the expected pixels.
        spriteBatch_->Begin();
        spriteBatch_->Draw(*redTexture_, Rectangle(4, 4, 8, 8), Color::White);
        spriteBatch_->End();

        if (frame_ == 1)
        {
            std::vector<Color> inside(1, Color::Black);
            Rectangle insideRect(8, 8, 1, 1);
            dev.GetBackBufferData(&insideRect, inside.data(), 0, 1);
            check(inside[0].getRProperty() == 255 && inside[0].getGProperty() == 0 &&
                  inside[0].getBProperty() == 0 && inside[0].getAProperty() == 255,
                  "SpriteBatch draw rasterizes the exact expected red pixel inside the destination rect");

            std::vector<Color> outside(1, Color::Black);
            Rectangle outsideRect(20, 20, 1, 1);
            dev.GetBackBufferData(&outsideRect, outside.data(), 0, 1);
            check(outside[0].getBProperty() == 64 && outside[0].getRProperty() == 0,
                  "The area outside the destination rect keeps the Clear() colour");
        }

        // Check E: RenderTarget2D render/unbind/sample round trip.
        if (frame_ == 1)
        {
            RenderTarget2D target(dev, 4, 4);
            dev.SetRenderTarget(&target);
            dev.Clear(Color(0, 255, 0, 255));
            dev.SetRenderTarget(nullptr);

            dev.Clear(Color(0, 0, 64, 255));
            spriteBatch_->Begin();
            spriteBatch_->Draw(target, Rectangle(0, 0, 4, 4), Color::White);
            spriteBatch_->End();

            std::vector<Color> sampled(1, Color::Black);
            Rectangle sampledRect(1, 1, 1, 1);
            dev.GetBackBufferData(&sampledRect, sampled.data(), 0, 1);
            check(sampled[0].getRProperty() == 0 && sampled[0].getGProperty() == 255 &&
                  sampled[0].getBProperty() == 0,
                  "RenderTarget2D render/unbind/sample round trip reproduces its Clear() colour");
        }

        // No explicit Present() here -- Game::Tick()/EndDraw() already calls
        // GraphicsDevice::Present() automatically once Draw() returns.

        if (frame_ == 3)
        {
            std::printf("=== %d/%d PASS ===\n", passCount_, kTotalChecks);
            result_ = (passCount_ == kTotalChecks) ? 0 : 1;
            Exit();
        }
    }

public:
    Blend2DSmokeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    Blend2DSmokeTest game;
    game.Run();
    return game.getResult();
}
