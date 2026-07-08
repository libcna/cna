// SPDX-License-Identifier: MS-PL
// Task 674: SDL_Renderer SpriteEffects::FlipHorizontally/FlipVertically pixel test.
//
// Direct port of Task 167's EasyGL test (examples/easygl_sprite_effects_test.cpp) -- same
// methodology, same geometry, same expected pixel outcomes. Genuinely backend-specific: confirmed
// by reading SdlSpriteBatchBackend::Draw() directly, SpriteEffects is mapped to an SDL_FlipMode
// and passed to SDL_RenderTextureRotated()'s own flip parameter.
//
// Requires PresentationMode::NativeBackBuffer (Task 915 finding): SDL_RenderReadPixels operates
// in physical output coordinates, while this backend's default presentation mode
// (FixedHeightDynamicWidth) does not map logical pixels 1:1 to physical ones.
//
// Design:
//   Viewport 400x100, divided into four 100x100 sections.
//   SamplerState::PointClamp (nearest-neighbour) is used so that every pixel
//   maps to exactly one texel -- no bilinear ambiguity.
//
//   Section 0 (x=0..99):   2x1 texture [Red|Blue], no flip
//                           -> left half Red, right half Blue
//   Section 1 (x=100..199): same texture, FlipHorizontally
//                           -> left half Blue, right half Red
//   Section 2 (x=200..299): 1x2 texture [Red / Blue], no flip
//                           -> top half Red, bottom half Blue
//   Section 3 (x=300..399): same texture, FlipVertically
//                           -> top half Blue, bottom half Red
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
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <memory>

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

class SdlSpriteEffectsTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;

    // 2x1: [Red | Blue]  (column 0 = Red, column 1 = Blue)
    std::unique_ptr<Texture2D> hTex_;
    // 1x2: [Red / Blue]  (row 0 = Red, row 1 = Blue)
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

        sb_->Begin(SpriteSortMode::Deferred,
                   BlendState::Opaque,
                   const_cast<SamplerState*>(&SamplerState::PointClamp),
                   nullptr, nullptr, nullptr, Matrix::getIdentityProperty());

        const Rectangle hFull(0, 0, 2, 1);   // full 2x1 source
        const Rectangle vFull(0, 0, 1, 2);   // full 1x2 source

        // Section 0: no flip -> left=Red, right=Blue
        sb_->Draw(*hTex_, Rectangle(0,   0, 100, 100), hFull,
                  Color::White, 0.0f, Vector2::Zero, SpriteEffects::None, 0.0f);

        // Section 1: FlipHorizontally -> left=Blue, right=Red
        sb_->Draw(*hTex_, Rectangle(100, 0, 100, 100), hFull,
                  Color::White, 0.0f, Vector2::Zero, SpriteEffects::FlipHorizontally, 0.0f);

        // Section 2: no flip -> top=Red, bottom=Blue
        sb_->Draw(*vTex_, Rectangle(200, 0, 100, 100), vFull,
                  Color::White, 0.0f, Vector2::Zero, SpriteEffects::None, 0.0f);

        // Section 3: FlipVertically -> top=Blue, bottom=Red
        sb_->Draw(*vTex_, Rectangle(300, 0, 100, 100), vFull,
                  Color::White, 0.0f, Vector2::Zero, SpriteEffects::FlipVertically, 0.0f);

        sb_->End();

        struct Check { int x; int y; Color want; const char* label; };
        const Check checks[] = {
            {  25, 50, kRed,  "S0-left:  NoFlip   -> Red"  },
            {  75, 50, kBlue, "S0-right: NoFlip   -> Blue" },
            { 125, 50, kBlue, "S1-left:  FlipH    -> Blue" },
            { 175, 50, kRed,  "S1-right: FlipH    -> Red"  },
            { 250, 25, kRed,  "S2-top:   NoFlip   -> Red"  },
            { 250, 75, kBlue, "S2-bot:   NoFlip   -> Blue" },
            { 350, 25, kBlue, "S3-top:   FlipV    -> Blue" },
            { 350, 75, kRed,  "S3-bot:   FlipV    -> Red"  },
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
    SdlSpriteEffectsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(400);
        gdm_->setPreferredBackBufferHeightProperty(100);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    int getResult() const { return result_; }
};

int main()
{
    SdlSpriteEffectsTest game;
    game.Run();
    return game.getResult();
}
