// SPDX-License-Identifier: MS-PL
// Task 417: EasyGL SpriteBatch rotation-around-origin pixel test.
//
// Verifies SpriteBatch::Draw's rotation genuinely pivots around the caller-specified `origin`
// point (in source-texture pixel units), not around the destination rectangle's top-left corner
// and not around the sprite's centre.
//
// FNA's real GenerateVertexInfo formula (SpriteBatch.cs): for each source-space corner (sx,sy),
//   cornerX = sx - originX, cornerY = sy - originY   (origin subtracted BEFORE rotation)
//   X' = destinationX + cornerX*cos(rotation) - cornerY*sin(rotation)
//   Y' = destinationY + cornerX*sin(rotation) + cornerY*cos(rotation)
// The origin point itself (cornerX=cornerY=0) always maps to exactly (destinationX,
// destinationY), invariant under rotation -- that's the defining property of a true pivot.
//
// Design: a 100x100 texture, top-left 20x20 = Red marker, rest = Blue. Drawn at
// destinationRectangle=(200,150,100,100) with origin=(100,100) -- the source's own bottom-right
// corner, diagonally opposite the Red marker -- rotated 90 degrees (PiOver2).
//
// Working the formula for the marker's centre (source point (10,10)):
//   cornerX = 10-100 = -90, cornerY = 10-100 = -90; sin=1, cos=0
//   X' = 200 + (-90)*0 - (-90)*1 = 290
//   Y' = 150 + (-90)*1 + (-90)*0 = 60
// So after rotation the marker must appear at screen (290,60) -- nowhere near where it would
// land with no rotation applied (110,60), and nowhere near the origin point itself (200,150),
// which stays fixed. The rotated sprite's screen footprint is the axis-aligned square
// x:[200,300], y:[50,150]; (250,100) sits well inside the sprite but away from the marker, so
// it must read the fill colour (Blue). A point far outside the sprite entirely (50,50) must
// read the clear colour, confirming no stray coverage.
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

class SpriteBatchRotationTest : public Game
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
        tex_ = std::make_unique<Texture2D>(dev, 100, 100);
        std::vector<Color> pixels(100 * 100, kBlue);
        for (int y = 0; y < 20; ++y)
            for (int x = 0; x < 20; ++x)
                pixels[y * 100 + x] = kRed;
        tex_->SetData(pixels.data(), static_cast<int>(pixels.size()));
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
    SpriteBatchRotationTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(400);
        gdm_->setPreferredBackBufferHeightProperty(300);
    }

    int getResult() const { return result_; }
};

int main()
{
    SpriteBatchRotationTest game;
    game.Run();
    return game.getResult();
}
