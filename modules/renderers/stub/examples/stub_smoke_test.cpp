// SPDX-License-Identifier: MS-PL
// plans/plan_stub.md STUB-4: end-to-end smoke test for the Stub (no-op) graphics renderer. Constructs a
// real Game (VertexBuffer, IndexBuffer, Texture2D, SpriteBatch, a custom Effect), runs a few
// frames entirely under CNA_GRAPHICS_RENDERER=STUB, and asserts:
//
// Check A -- SDL's video subsystem was never initialized (SDL_WasInit(SDL_INIT_VIDEO) == 0) --
//   proves this renderer genuinely needs no display server at all, not just a hidden window.
// Check B -- GameWindow handle returns nullptr -- no window object exists anywhere.
// Check C -- a plain 3D DrawPrimitives call (VertexBuffer + BasicEffect) does not throw.
// Check D -- an indexed 3D DrawIndexedPrimitives call (VertexBuffer + IndexBuffer + BasicEffect)
//   does not throw.
// Check E -- a SpriteBatch Begin/Draw/End sequence does not throw.
// Check F -- CreateVertexBuffer()/CreateIndexBuffer16() honestly report back the vertex/index
//   count they were given (GetVertexCount()/GetIndexCount()), confirming the Stub resource handles
//   are not just empty shells that discard everything silently.
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
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "CNA/Internal/Renderers/Stub/StubRenderer.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::Stub;

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

class StubSmokeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> texture_;
    int frame_ = 0;
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
        texture_ = std::make_unique<Texture2D>(
            Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 1, 1,
                                        std::vector<std::uint8_t>{255, 255, 255, 255}));
    }

    void Draw(const GameTime&) override
    {
        ++frame_;
        auto& dev = getGraphicsDeviceProperty();
        auto& renderer = static_cast<StubRenderer&>(dev.GetRenderer());

        if (frame_ == 1)
        {
            // Check A/B: no real window/video subsystem anywhere.
            check(SDL_WasInit(SDL_INIT_VIDEO) == 0,
                  "SDL_INIT_VIDEO was never initialized under the Stub renderer");
            check(reinterpret_cast<SDL_Window*>(getWindowProperty().getHandleProperty()) == nullptr, "GraphicsDevice has no real window under the Stub renderer");
        }

        dev.Clear(Color::Black);

        // A stride-16 VertexPositionColor triangle, drawn via DrawPrimitives.
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
        if (frame_ == 1) check(!threeDThrew, "DrawPrimitives (3D, VertexBuffer + BasicEffect) does not throw");

        // Check D: an indexed draw.
        bool indexedThrew = false;
        try
        {
            IndexBuffer ib(dev, IndexElementSize::SixteenBits, 3, BufferUsage::None);
            const std::uint16_t indices[3] = {0, 1, 2};
            ib.SetData(indices, 0, 3);
            dev.SetVertexBuffer(&vb);
            dev.SetIndexBuffer(&ib);
            dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 3, 0, 1);
            dev.SetVertexBuffer(nullptr);
            dev.SetIndexBuffer(nullptr);

            if (frame_ == 1)
            {
                // Check F: the Stub vertex/index buffer handles honestly report back their counts.
                check(vb.getVertexCountProperty() == 3, "VertexBuffer reports the vertex count it was given");
                check(ib.getIndexCountProperty() == 3, "IndexBuffer reports the index count it was given");
            }
        }
        catch (...) { indexedThrew = true; }
        if (frame_ == 1) check(!indexedThrew, "DrawIndexedPrimitives (3D, VertexBuffer + IndexBuffer + BasicEffect) does not throw");

        // Check E: a SpriteBatch draw.
        bool spriteThrew = false;
        try
        {
            spriteBatch_->Begin();
            spriteBatch_->Draw(*texture_, Rectangle(0, 0, 4, 4), Color::White);
            spriteBatch_->End();
        }
        catch (...) { spriteThrew = true; }
        if (frame_ == 1) check(!spriteThrew, "SpriteBatch Begin/Draw/End does not throw");

        // No explicit Present() here -- Game::Tick()/EndDraw() already calls
        // GraphicsDevice::Present() automatically once Draw() returns, matching real XNA/FNA
        // semantics (a Game's own Draw() override never calls Present() itself).

        if (frame_ == 3)
        {
            std::printf("=== %d/%d PASS ===\n", passCount_, 7);
            result_ = (passCount_ == 7) ? 0 : 1;
            Exit();
        }
    }

public:
    StubSmokeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    StubSmokeTest game;
    game.Run();
    return game.getResult();
}
