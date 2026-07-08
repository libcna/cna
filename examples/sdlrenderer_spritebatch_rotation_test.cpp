// SPDX-License-Identifier: MS-PL
// Task 671: SDL_Renderer SpriteBatch rotation-around-origin pixel test.
//
// Direct port of Task 417's EasyGL test (examples/easygl_spritebatch_rotation_test.cpp) -- same
// methodology, same geometry, same expected pixel outcomes. See that file's header comment for
// the full FNA-derived rotation-formula rationale.
//
// Requires PresentationMode::NativeBackBuffer (Task 915 finding): SDL_RenderReadPixels operates
// in physical output coordinates, while this backend's default presentation mode
// (FixedHeightDynamicWidth) does not map logical pixels 1:1 to physical ones.
//
// Verifies SpriteBatch::Draw's rotation genuinely pivots around the caller-specified `origin`
// point (in source-texture pixel units), not around the destination rectangle's top-left corner
// and not around the sprite's centre.
//
// Design: a 100x100 texture, top-left 20x20 = Red marker, rest = Blue. Drawn at
// destinationRectangle=(200,150,100,100) with origin=(100,100) -- the source's own bottom-right
// corner, diagonally opposite the Red marker -- rotated 90 degrees (PiOver2). The marker's centre
// (source point (10,10)) must land at screen (290,60) after rotation; the origin point itself
// stays fixed at (200,150).
//
// Exit code 0 = all PASS, 1 = at least one FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
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

static const Color kRed   (255,   0,   0, 255);
static const Color kBlue  (  0,   0, 255, 255);
static const Color kClear (  0, 255,   0, 255);

class SdlSpriteBatchRotationTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<Texture2D>             tex_;

    bool done_   = false;
    int  result_ = 0;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);

        // 100x100: top-left 20x20 = Red marker, rest = Blue.
        std::vector<uint8_t> pixels(100 * 100 * 4);
        for (int y = 0; y < 100; ++y)
        {
            for (int x = 0; x < 100; ++x)
            {
                const std::size_t i = (static_cast<std::size_t>(y) * 100 + x) * 4;
                const bool marker = (x < 20 && y < 20);
                const Color& c = marker ? kRed : kBlue;
                pixels[i] = c.getRProperty(); pixels[i+1] = c.getGProperty();
                pixels[i+2] = c.getBProperty(); pixels[i+3] = c.getAProperty();
            }
        }
        tex_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(dev, 100, 100, pixels));
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(kClear);
        dev.setBlendStateProperty(BlendState::Opaque);

        sb_->Begin(SpriteSortMode::Deferred,
                   BlendState::Opaque,
                   const_cast<SamplerState*>(&SamplerState::PointClamp),
                   nullptr, nullptr, nullptr, Matrix::getIdentityProperty());

        sb_->Draw(*tex_,
                  Rectangle(200, 150, 100, 100),   // destinationRectangle
                  Rectangle(0, 0, 100, 100),        // sourceRectangle (whole texture)
                  Color::White,
                  MathHelper::PiOver2,               // 90-degree rotation
                  Vector2(100.0f, 100.0f),           // origin: source's own bottom-right corner
                  SpriteEffects::None,
                  0.0f);

        sb_->End();

        struct Check { int x; int y; Color want; const char* label; };
        const Check checks[] = {
            { 290,  60, kRed,   "Marker rotated to (290,60) around origin" },
            { 250, 100, kBlue,  "Sprite interior, away from marker -> Blue" },
            {  50,  50, kClear, "Outside sprite entirely -> clear colour" },
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
    SdlSpriteBatchRotationTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(400);
        gdm_->setPreferredBackBufferHeightProperty(300);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    int getResult() const { return result_; }
};

int main()
{
    SdlSpriteBatchRotationTest game;
    game.Run();
    return game.getResult();
}
