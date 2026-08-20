// SPDX-License-Identifier: MS-PL
// Task 664: Vulkan SpriteBatch multi-Begin/End-per-frame regression test.
//
// Confirmed bug (session 2026-07-02, tracked in plans/plan_graphics.md/NEXT.md §5): on the Vulkan
// renderer, if a single SpriteBatch object runs multiple independent Begin()/Draw()/End() cycles
// within one frame, only the LAST cycle's draws ever end up on screen -- earlier cycles vanish.
//
// Root cause (see plans/plan_graphics.md Task 664's write-up for the full trace): the old
// VulkanSpriteBatchRenderer::Begin() unconditionally cleared its own vertices_/indices_/draws_
// vectors -- the SAME storage a prior End() had just finished populating -- and activeBatches_
// tracked entries by raw pointer to that single mutable object. A 2nd Begin() call destroyed the
// 1st cycle's already-completed geometry before the once-per-frame harvest in
// RecordCommandBuffer() (driven by Present()) ever got a chance to read it. Fixed by moving each
// Begin()/End() cycle's geometry into its own independent BatchSnapshot at End() time, and by
// giving the shared per-frame sprite VB/IB buffers a genuine accumulating write cursor (mirroring
// the already-correct 3D draw path's vbOff/ibOff pattern) so multiple snapshots targeting the same
// render target compose additively instead of overwriting each other at a hardcoded offset 0.
//
// Design: 2 independent Begin()/Draw()/End() cycles in one Draw(GameTime) call, each drawing an
// opaque, distinctly-colored quad to a distinct (non-overlapping) screen region.
//   Cycle 1: Red quad,  destRect=(0,  0, 100, 100) -- left half.
//   Cycle 2: Blue quad, destRect=(100,0, 100, 100) -- right half.
// Both regions must show their own quad's color; the pre-fix bug would show the left region as
// background (clear color) since only cycle 2 (the last one) ever survived to be drawn.
//
// Exit code 0 = all PASS, 1 = at least one FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static bool colourMatch(Color got, Color want, int tol = 60)
{
    return std::abs((int)got.getRProperty()  - (int)want.getRProperty())  <= tol
        && std::abs((int)got.getGProperty()  - (int)want.getGProperty())  <= tol
        && std::abs((int)got.getBProperty()  - (int)want.getBProperty())  <= tol;
}

static const Color kRed  (255,   0,   0, 255);
static const Color kBlue (  0,   0, 255, 255);

class VulkanSpriteBatchMultiBeginEndTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<Texture2D>             redTex_;
    std::unique_ptr<Texture2D>             blueTex_;

    bool done_   = false;
    int  result_ = 0;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);

        redTex_ = std::make_unique<Texture2D>(dev, 1, 1);
        std::vector<Color> redPx(1, kRed);
        redTex_->SetData(redPx.data(), 1);

        blueTex_ = std::make_unique<Texture2D>(dev, 1, 1);
        std::vector<Color> bluePx(1, kBlue);
        blueTex_->SetData(bluePx.data(), 1);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(Color(0, 0, 0, 255));
        dev.setBlendStateProperty(BlendState::Opaque);

        // Cycle 1: Red quad, left half.
        sb_->Begin(SpriteSortMode::Deferred,
                   BlendState::Opaque,
                   const_cast<SamplerState*>(&SamplerState::PointClamp),
                   nullptr, nullptr, nullptr, Matrix::getIdentityProperty());
        sb_->Draw(*redTex_, Rectangle(0, 0, 100, 100), Rectangle(0, 0, 1, 1), Color::White);
        sb_->End();

        // Cycle 2: Blue quad, right half. Independent Begin()/Draw()/End() cycle, same frame.
        sb_->Begin(SpriteSortMode::Deferred,
                   BlendState::Opaque,
                   const_cast<SamplerState*>(&SamplerState::PointClamp),
                   nullptr, nullptr, nullptr, Matrix::getIdentityProperty());
        sb_->Draw(*blueTex_, Rectangle(100, 0, 100, 100), Rectangle(0, 0, 1, 1), Color::White);
        sb_->End();

        struct Check { int x; int y; Color want; const char* label; };
        const Check checks[] = {
            { 50,  50, kRed,  "Cycle 1 (Red, left half) must render" },
            { 150, 50, kBlue, "Cycle 2 (Blue, right half) must render" },
        };

        for (const auto& c : checks)
        {
            Color px(0, 0, 0, 0);
            Rectangle reg(c.x, c.y, 1, 1);
            dev.GetBackBufferData(&reg, &px, 0, 1);
            const bool ok = colourMatch(px, c.want);
            std::printf("[%s] %s: got=(%d,%d,%d)\n",
                ok ? "PASS" : "FAIL", c.label,
                px.getRProperty(), px.getGProperty(), px.getBProperty());
            if (!ok) result_ = 1;
        }

        Exit();
    }

public:
    VulkanSpriteBatchMultiBeginEndTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(200);
        gdm_->setPreferredBackBufferHeightProperty(100);
    }

    int getResult() const { return result_; }
};

int main()
{
    VulkanSpriteBatchMultiBeginEndTest game;
    game.Run();
    return game.getResult();
}
