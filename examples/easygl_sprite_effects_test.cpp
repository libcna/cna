// SPDX-License-Identifier: MS-PL
// Task 167: EasyGL SpriteEffects pixel integration test.
//
// Verifies SpriteEffects::FlipHorizontally and FlipVertically at the UV level
// in the EasyGL 2D sprite pipeline.
//
// Design:
//   Viewport 400×100, divided into four 100×100 sections.
//   SamplerState::PointClamp (nearest-neighbour) is used so that every pixel
//   maps to exactly one texel — no bilinear ambiguity.
//
//   Section 0 (x=0..99):   2×1 texture [Red|Blue], no flip
//                           → left half Red, right half Blue
//   Section 1 (x=100..199): same texture, FlipHorizontally
//                           → left half Blue, right half Red
//   Section 2 (x=200..299): 1×2 texture [Red / Blue], no flip
//                           → top half Red, bottom half Blue
//   Section 3 (x=300..399): same texture, FlipVertically
//                           → top half Blue, bottom half Red
//
//   Readback samples taken from clearly within each half, far from the seam:
//     (25,50)  → Red   (section 0 left half, no-flip)
//     (75,50)  → Blue  (section 0 right half, no-flip)
//     (125,50) → Blue  (section 1 left half, H-flipped)
//     (175,50) → Red   (section 1 right half, H-flipped)
//     (250,25) → Red   (section 2 top half, no-flip)
//     (250,75) → Blue  (section 2 bottom half, no-flip)
//     (350,25) → Blue  (section 3 top half, V-flipped)
//     (350,75) → Red   (section 3 bottom half, V-flipped)
//
// Exit code 0 = all PASS, 1 = at least one FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

// ─── helpers ────────────────────────────────────────────────────────────────

static bool colourMatch(Color got, Color want, int tol = 60)
{
    return std::abs((int)got.getRProperty()  - (int)want.getRProperty())  <= tol
        && std::abs((int)got.getGProperty()  - (int)want.getGProperty())  <= tol
        && std::abs((int)got.getBProperty()  - (int)want.getBProperty())  <= tol;
}

static const Color kRed  (255,   0,   0, 255);
static const Color kBlue (  0,   0, 255, 255);

// ─── game ───────────────────────────────────────────────────────────────────

class SpriteEffectsTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;

    // 2×1: [Red | Blue]  (column 0 = Red, column 1 = Blue)
    std::unique_ptr<Texture2D> hTex_;
    // 1×2: [Red / Blue]  (row 0 = Red, row 1 = Blue)
    std::unique_ptr<Texture2D> vTex_;

    bool done_   = false;
    int  result_ = 0;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);

        hTex_ = std::make_unique<Texture2D>(dev, 2, 1);
        const Color hPix[2] = { kRed, kBlue };
        hTex_->SetData(hPix, 2);

        vTex_ = std::make_unique<Texture2D>(dev, 1, 2);
        const Color vPix[2] = { kRed, kBlue };
        vTex_->SetData(vPix, 2);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(Color(0, 0, 0, 255));
        dev.setBlendStateProperty(BlendState::Opaque);

        // ── SamplerState::PointClamp → nearest-neighbour, no bilinear mix ──
        sb_->Begin(SpriteSortMode::Deferred,
                   BlendState::Opaque,
                   const_cast<SamplerState*>(&SamplerState::PointClamp),
                   nullptr, nullptr, nullptr, Matrix::getIdentityProperty());

        const Rectangle hFull(0, 0, 2, 1);   // full 2×1 source
        const Rectangle vFull(0, 0, 1, 2);   // full 1×2 source

        // Section 0: no flip → left=Red, right=Blue
        sb_->Draw(*hTex_, Rectangle(0,   0, 100, 100), hFull,
                  Color::White, 0.0f, Vector2::Zero, SpriteEffects::None, 0.0f);

        // Section 1: FlipHorizontally → left=Blue, right=Red
        sb_->Draw(*hTex_, Rectangle(100, 0, 100, 100), hFull,
                  Color::White, 0.0f, Vector2::Zero, SpriteEffects::FlipHorizontally, 0.0f);

        // Section 2: no flip → top=Red, bottom=Blue
        sb_->Draw(*vTex_, Rectangle(200, 0, 100, 100), vFull,
                  Color::White, 0.0f, Vector2::Zero, SpriteEffects::None, 0.0f);

        // Section 3: FlipVertically → top=Blue, bottom=Red
        sb_->Draw(*vTex_, Rectangle(300, 0, 100, 100), vFull,
                  Color::White, 0.0f, Vector2::Zero, SpriteEffects::FlipVertically, 0.0f);

        sb_->End();

        // ── readback checks ─────────────────────────────────────────────────
        struct Check { int x; int y; Color want; const char* label; };
        const Check checks[] = {
            // Section 0 — no flip
            {  25, 50, kRed,  "S0-left:  NoFlip   → Red"  },
            {  75, 50, kBlue, "S0-right: NoFlip   → Blue" },
            // Section 1 — FlipHorizontally
            { 125, 50, kBlue, "S1-left:  FlipH    → Blue" },
            { 175, 50, kRed,  "S1-right: FlipH    → Red"  },
            // Section 2 — no flip (vertical)
            { 250, 25, kRed,  "S2-top:   NoFlip   → Red"  },
            { 250, 75, kBlue, "S2-bot:   NoFlip   → Blue" },
            // Section 3 — FlipVertically
            { 350, 25, kBlue, "S3-top:   FlipV    → Blue" },
            { 350, 75, kRed,  "S3-bot:   FlipV    → Red"  },
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
    SpriteEffectsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(400);
        gdm_->setPreferredBackBufferHeightProperty(100);
    }

    int getResult() const { return result_; }
};

int main()
{
    SpriteEffectsTest game;
    game.Run();
    return game.getResult();
}
