// SPDX-License-Identifier: MS-PL
// Task 429: Verify rotation/origin/scale with DrawString on EasyGL.
//
// EasyGL port of Task 694's SDL_Renderer "Test 2": a code path already correct BEFORE Task 694's
// own flip fix (untouched by that fix) -- this test independently confirms it also renders
// correctly through EasyGL's own draw renderer.
//
// Single glyph 'A' (White, 8x8), origin=(4,4) (centers the pivot on the unscaled glyph),
// scale=(2,2), rotation=0, position=(10,10). Per SpriteBatch::DrawString's shared formula:
//   localX = curOffset.X(0)+cCrop.X(0)-origin.X(4) = -4;  scaledX = -4*2 = -8;
//   dest.X = round(10-8) = 2;  dest.Width = round(8*2) = 16  ->  dest=(2,2,16,16)
//
// Exit code 0 = all PASS, 1 = at least one FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <memory>
#include <optional>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static bool colourMatch(Color got, Color want, int tol = 40)
{
    return std::abs((int)got.getRProperty() - (int)want.getRProperty()) <= tol
        && std::abs((int)got.getGProperty() - (int)want.getGProperty()) <= tol
        && std::abs((int)got.getBProperty() - (int)want.getBProperty()) <= tol;
}

static const Color kWhite(255, 255, 255, 255);
static const Color kBlack(  0,   0,   0, 255);

class EasyGLSpriteFontEffectsRotationScaleTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<Texture2D>             atlas_;
    std::unique_ptr<SpriteFont>            font_;

    bool done_   = false;
    int  result_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (!ok) result_ = 1;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);

        // Single-glyph font: 'A' (White), 8x8.
        atlas_ = std::make_unique<Texture2D>(dev, 8, 8);
        std::vector<Color> whitePixels(64, kWhite);
        atlas_->SetData(whitePixels.data(), 64);
        std::vector<Rectangle> glyphBounds = { Rectangle(0, 0, 8, 8) };
        std::vector<Rectangle> cropping    = { Rectangle(0, 0, 8, 8) };
        std::vector<SharpRuntime::charcs> characters = { u'A' };
        std::vector<Vector3> kerning = { Vector3(0.0f, 8.0f, 0.0f) };
        font_ = std::make_unique<SpriteFont>(*atlas_, glyphBounds, cropping, characters,
                                              /*lineSpacing=*/8, /*spacing=*/0.0f, kerning,
                                              std::optional<SharpRuntime::charcs>(std::nullopt));
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(kBlack);
        dev.setBlendStateProperty(BlendState::Opaque);

        sb_->Begin();
        sb_->DrawString(*font_, "A", Vector2(10.0f, 10.0f), Color::White, 0.0f,
                         Vector2(4.0f, 4.0f), Vector2(2.0f, 2.0f), SpriteEffects::None, 0.0f);
        sb_->End();

        // dest = (2, 2, 16, 16) -- occupies x,y in [2,18).
        auto sample = [&](int x, int y) {
            Color px(0, 0, 0, 0);
            Rectangle reg(x, y, 1, 1);
            dev.GetBackBufferData(&reg, &px, 0, 1);
            return px;
        };

        check(colourMatch(sample(10, 10), kWhite), "origin=(4,4) scale=(2,2): (10,10) inside scaled glyph -> White");
        check(colourMatch(sample(0, 0), kBlack),   "origin=(4,4) scale=(2,2): (0,0) outside -> Black bg");
        check(colourMatch(sample(20, 20), kBlack), "origin=(4,4) scale=(2,2): (20,20) outside -> Black bg");

        Exit();
    }

public:
    EasyGLSpriteFontEffectsRotationScaleTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(24);
        gdm_->setPreferredBackBufferHeightProperty(24);
    }

    int getResult() const { return result_; }
};

int main()
{
    EasyGLSpriteFontEffectsRotationScaleTest game;
    game.Run();
    return game.getResult();
}
