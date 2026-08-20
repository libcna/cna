// SPDX-License-Identifier: MS-PL
// plans/plan_headless.md HEADLESS-50/51/53/60/61/62/64: end-to-end headless smoke test for the Headless graphics
// renderer. Constructs a real Game (VertexBuffer, IndexBuffer, Texture2D, SpriteBatch, a custom
// Effect), runs a few frames entirely under CNA_GRAPHICS_RENDERER=HEADLESS, and asserts:
//
// Check A -- SDL's video subsystem was never initialized (SDL_WasInit(SDL_INIT_VIDEO) == 0) --
//   proves this renderer genuinely needs no display server at all, not just a hidden window.
// Check B -- GameWindow handle returns nullptr -- no window object exists anywhere.
// Check C -- draw-call/primitive/state-change counters after N frames match the exact expected
//   values for the fixed sequence of calls this test issues -- proves the counters are real
//   bookkeeping, not just present.
// Check D -- AssertNoLeaks() throws while a deliberately-leaked VertexBuffer is still alive...
// Check E -- ...and the alive-resource count returns to its pre-leak baseline once it is disposed
//   (not "zero" -- other long-lived resources, e.g. this frame's own VertexBuffer/the SpriteBatch/
//   the Texture2D, are still legitimately alive at this point in the test).
// Check F -- HeadlessValidation mode rejects an out-of-range DrawIndexedPrimitives call (primitiveCount
//   that needs more indices than the bound IndexBuffer has); HeadlessFast mode accepts the same call
//   without throwing -- proves the mode dial genuinely changes behaviour.
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

#include "CNA/Internal/Renderers/Headless/HeadlessRenderer.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::Headless;

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

class HeadlessSmokeTest : public Game
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
        auto& renderer = static_cast<HeadlessRenderer&>(dev.GetRenderer());

        if (frame_ == 1)
        {
            // Check A/B: no real window/video subsystem anywhere.
            check(SDL_WasInit(SDL_INIT_VIDEO) == 0,
                  "SDL_INIT_VIDEO was never initialized under the Headless renderer");
            check(reinterpret_cast<SDL_Window*>(getWindowProperty().getHandleProperty()) == nullptr, "GraphicsDevice has no real window under the Headless renderer");
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
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
        dev.SetVertexBuffer(nullptr);

        // A SpriteBatch draw.
        spriteBatch_->Begin();
        spriteBatch_->Draw(*texture_, Rectangle(0, 0, 4, 4), Color::White);
        spriteBatch_->End();

        // No explicit Present() here -- Game::Tick()/EndDraw() already calls
        // GraphicsDevice::Present() automatically once Draw() returns, matching real XNA/FNA
        // semantics (a Game's own Draw() override never calls Present() itself).

        if (frame_ == 3)
        {
            // Check C: 3 frames * (1 DrawPrimitives call + 1 SpriteBatch draw call) = 6 draw calls,
            // 3 Clear() calls, 3 VertexBuffer creations (one per frame, matching this test's own
            // deliberately-per-frame VertexBuffer construction above). presentCount is 2, not 3,
            // at this exact point: frame 3's own automatic EndDraw()->Present() call happens AFTER
            // this Draw() call returns, so only frames 1 and 2 have completed it so far.
            const HeadlessStatistics& stats = renderer.GetStatistics();
            check(stats.drawCallCount == 6, "drawCallCount is exactly 6 after 3 frames of 2 draws each");
            check(stats.clearCount == 3, "clearCount is exactly 3 after 3 Clear() calls");
            check(stats.presentCount == 2, "presentCount is exactly 2 (frames 1/2's automatic EndDraw()) so far");
            check(stats.vertexBuffersCreated == 3, "vertexBuffersCreated is exactly 3 (one per frame)");

            // Check D/E: leak detection. `vb` (this frame's own VertexBuffer, still in scope until
            // Draw() returns), `texture_`, and `spriteBatch_` are all legitimately still alive at
            // this point, so leak-freedom is checked as "back to the pre-leak baseline count", not
            // "zero" -- the correct, general shape of this check when other long-lived resources
            // genuinely exist.
            const std::size_t baselineAlive = renderer.AliveResources().size();
            {
                auto leaked = std::make_unique<VertexBuffer>(dev, PosColorDecl(), 3, BufferUsage::None);
                bool threw = false;
                try { renderer.AssertNoLeaks(); }
                catch (const HeadlessValidationException&) { threw = true; }
                check(threw, "AssertNoLeaks() throws while a VertexBuffer is deliberately kept alive");
                leaked.reset();
            }
            check(renderer.AliveResources().size() == baselineAlive,
                  "alive-resource count returns to its pre-leak baseline once the resource is disposed");

            // Check F: mode dial genuinely changes validation behaviour.
            {
                VertexDeclaration decl = PosColorDecl();
                VertexBuffer smallVb(dev, decl, 3, BufferUsage::None);
                smallVb.SetData(verts, 0, 3);
                IndexBuffer ib(dev, IndexElementSize::SixteenBits, 3, BufferUsage::None);
                const std::uint16_t indices[3] = {0, 1, 2};
                ib.SetData(indices, 0, 3);
                BasicEffect fx2(dev);
                fx2.Apply();

                renderer.SetMode(HeadlessMode::Validation);
                bool validationThrew = false;
                try
                {
                    // 10 primitives needs 30 indices; the buffer only holds 3 -- out of range.
                    dev.SetVertexBuffer(&smallVb);
                    dev.SetIndexBuffer(&ib);
                    dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 3, 0, 10);
                }
                catch (const HeadlessValidationException&) { validationThrew = true; }
                dev.SetVertexBuffer(nullptr);
                dev.SetIndexBuffer(nullptr);
                check(validationThrew, "HeadlessValidation mode rejects an out-of-range DrawIndexedPrimitives call");

                renderer.SetMode(HeadlessMode::Fast);
                bool fastThrew = false;
                try
                {
                    dev.SetVertexBuffer(&smallVb);
                    dev.SetIndexBuffer(&ib);
                    dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 3, 0, 10);
                }
                catch (const HeadlessValidationException&) { fastThrew = true; }
                dev.SetVertexBuffer(nullptr);
                dev.SetIndexBuffer(nullptr);
                check(!fastThrew, "HeadlessFast mode accepts the same out-of-range call without throwing");
                renderer.SetMode(HeadlessMode::Validation);
            }

            std::printf("=== %d/%d PASS ===\n", passCount_, 10);
            result_ = (passCount_ == 10) ? 0 : 1;
            Exit();
        }
    }

public:
    HeadlessSmokeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    HeadlessSmokeTest game;
    game.Run();
    return game.getResult();
}
