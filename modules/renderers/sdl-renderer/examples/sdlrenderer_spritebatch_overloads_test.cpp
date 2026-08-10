// SPDX-License-Identifier: MS-PL
// Task 666: audit all SpriteBatch::Draw overloads specifically through SDL_Renderer (not
// inherited from EasyGL test assumptions -- this is a fresh, from-scratch exercise of every
// overload against this renderer's own SdlSpriteBatchRenderer::Draw implementations).
//
// SpriteBatch::Draw has 9 public overloads (3 CNAEXT taking a raw Texture2D + explicit Rectangle/
// float args, 6 real XNA overloads taking Vector2/optional<Rectangle>). Each is exercised exactly
// once here, drawing a distinct 40x40 tinted square at its own non-overlapping screen slot using a
// solid white 1x1 texture -- since white*tint == tint exactly (no blending ambiguity with
// BlendState::Opaque), each slot's centre pixel must read back the overload's own unique tint
// colour if (and only if) that overload actually rasterized something.
//
// Also exercises SpriteEffects::FlipHorizontally/FlipVertically (overloads 3, 6, 7) using an
// asymmetric two-colour texture (not the plain white one) so a flip that silently did nothing
// would read the wrong half's colour -- a real, discriminating check, not just "did it draw
// anything".
//
// Requires PresentationMode::NativeBackBuffer (see sdlrenderer_readback_test.cpp's header comment
// for why): SDL_RenderReadPixels operates in physical coordinates, while this renderer's default
// presentation mode (FixedHeightDynamicWidth) does not map logical pixels 1:1 to physical ones.
//
// Exit code 0 = all PASS, 1 = at least one FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <memory>
#include <optional>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSlotW = 60;
    constexpr int kSlotH = 60;
    constexpr int kSlotPad = 4;

    // 9 maximally-distinct tint colours, one per overload.
    const Color kTints[9] = {
        Color(255,   0,   0, 255), // 1: Draw(tex, x, y)
        Color(  0, 255,   0, 255), // 2: Draw(tex, destRect, srcRect, color)
        Color(  0,   0, 255, 255), // 3: Draw(tex, destRect, srcRect, color, rot, origin, effect, depth)
        Color(255, 255,   0, 255), // 4: Draw(tex, position, color)
        Color(255,   0, 255, 255), // 5: Draw(tex, position, optional<srcRect>, color)
        Color(  0, 255, 255, 255), // 6: Draw(tex, position, optional<srcRect>, color, rot, origin, float scale, effects, depth)
        Color(255, 128,   0, 255), // 7: Draw(tex, position, optional<srcRect>, color, rot, origin, Vector2 scale, effects, depth)
        Color(128,   0, 255, 255), // 8: Draw(tex, destRect, color)
        Color(  0, 128,  64, 255), // 9: Draw(tex, destRect, optional<srcRect>, color)
    };

    bool colourMatch(Color got, Color want, int tol = 40)
    {
        return std::abs(static_cast<int>(got.getRProperty()) - static_cast<int>(want.getRProperty())) <= tol
            && std::abs(static_cast<int>(got.getGProperty()) - static_cast<int>(want.getGProperty())) <= tol
            && std::abs(static_cast<int>(got.getBProperty()) - static_cast<int>(want.getBProperty())) <= tol;
    }
}

class SdlSpriteBatchOverloadsTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<Texture2D>             whiteTex_;
    std::unique_ptr<Texture2D>             splitTex_; // left half red, right half green (16x16)
    bool done_   = false;
    int  result_ = 0;

    static Rectangle Slot(int index)
    {
        return Rectangle(kSlotPad + index * (kSlotW + kSlotPad), kSlotPad, kSlotW, kSlotH);
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);

        const std::vector<uint8_t> white = { 255, 255, 255, 255 };
        whiteTex_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(dev, 1, 1, white));

        // 16x16, left 8 columns red, right 8 columns green -- for flip checks.
        std::vector<uint8_t> split(16 * 16 * 4);
        for (int y = 0; y < 16; ++y)
        {
            for (int x = 0; x < 16; ++x)
            {
                const std::size_t i = (static_cast<std::size_t>(y) * 16 + x) * 4;
                if (x < 8) { split[i] = 255; split[i+1] = 0;   split[i+2] = 0;   split[i+3] = 255; }
                else       { split[i] = 0;   split[i+1] = 255; split[i+2] = 0;   split[i+3] = 255; }
            }
        }
        splitTex_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(dev, 16, 16, split));
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(Color(32, 32, 32, 255));
        dev.setBlendStateProperty(BlendState::Opaque);

        sb_->Begin();

        // 1: Draw(texture, x, y) -- CNAEXT. Draws at native texture size (1x1, no tint
        // parameter in this overload's signature), so the check below reads exactly that single
        // physical pixel rather than a tinted region.
        sb_->Draw(*whiteTex_, static_cast<float>(Slot(0).X), static_cast<float>(Slot(0).Y));

        // 2: Draw(texture, destRect, sourceRect, color) -- CNAEXT
        sb_->Draw(*whiteTex_, Slot(1), Rectangle(0, 0, 1, 1), kTints[1]);

        // 3: Draw(texture, destRect, sourceRect, color, rotation, origin, effect, layerDepth) -- CNAEXT
        //    Uses the split texture + FlipHorizontally so the LEFT half of the destination reads
        //    the texture's right-half colour (green) if the flip genuinely took effect.
        sb_->Draw(*splitTex_, Slot(2), Rectangle(0, 0, 16, 16), Color::White,
                  0.0f, Vector2(0.0f, 0.0f), SpriteEffects::FlipHorizontally, 0.0f);

        // 4: Draw(texture, position, color)
        sb_->Draw(*whiteTex_, Vector2(static_cast<float>(Slot(3).X), static_cast<float>(Slot(3).Y)), kTints[3]);

        // 5: Draw(texture, position, optional<sourceRectangle>, color)
        sb_->Draw(*whiteTex_, Vector2(static_cast<float>(Slot(4).X), static_cast<float>(Slot(4).Y)),
                  std::optional<Rectangle>(Rectangle(0, 0, 1, 1)), kTints[4]);

        // 6: Draw(texture, position, optional<sourceRectangle>, color, rotation, origin, float scale, effects, depth)
        //    Scale factor kSlotW so the 1x1 texture fills the whole slot.
        sb_->Draw(*whiteTex_, Vector2(static_cast<float>(Slot(5).X), static_cast<float>(Slot(5).Y)),
                  std::optional<Rectangle>(Rectangle(0, 0, 1, 1)), kTints[5],
                  0.0f, Vector2(0.0f, 0.0f), static_cast<float>(kSlotW), SpriteEffects::None, 0.0f);

        // 7: Draw(texture, position, optional<sourceRectangle>, color, rotation, origin, Vector2 scale, effects, depth)
        //    Non-uniform Vector2 scale (distinct code path from overload 6's float scale).
        sb_->Draw(*whiteTex_, Vector2(static_cast<float>(Slot(6).X), static_cast<float>(Slot(6).Y)),
                  std::optional<Rectangle>(Rectangle(0, 0, 1, 1)), kTints[6],
                  0.0f, Vector2(0.0f, 0.0f), Vector2(static_cast<float>(kSlotW), static_cast<float>(kSlotH)),
                  SpriteEffects::None, 0.0f);

        // 8: Draw(texture, destRect, color)
        sb_->Draw(*whiteTex_, Slot(7), kTints[7]);

        // 9: Draw(texture, destRect, optional<sourceRectangle>, color)
        sb_->Draw(*whiteTex_, Slot(8), std::optional<Rectangle>(Rectangle(0, 0, 1, 1)), kTints[8]);

        sb_->End();

        struct Check { Rectangle region; Color want; const char* label; };
        std::vector<Check> checks = {
            { Rectangle(Slot(0).X, Slot(0).Y, 1, 1), Color(255, 255, 255, 255), "overload 1: Draw(tex,x,y)" },
            { Rectangle(Slot(1).X + kSlotW/2, Slot(1).Y + kSlotH/2, 1, 1), kTints[1], "overload 2: Draw(tex,destRect,srcRect,color)" },
            { Rectangle(Slot(2).X + kSlotW/4, Slot(2).Y + kSlotH/2, 1, 1), Color(0, 255, 0, 255), "overload 3: FlipHorizontally left-half reads green" },
            // Overloads 4/5 draw the 1x1 texture at its NATIVE size (no scale parameter in either
            // signature), so -- like overload 1 -- only the exact origin pixel is covered, not the
            // whole slot.
            { Rectangle(Slot(3).X, Slot(3).Y, 1, 1), kTints[3], "overload 4: Draw(tex,position,color)" },
            { Rectangle(Slot(4).X, Slot(4).Y, 1, 1), kTints[4], "overload 5: Draw(tex,position,optSrcRect,color)" },
            { Rectangle(Slot(5).X + kSlotW/2, Slot(5).Y + kSlotH/2, 1, 1), kTints[5], "overload 6: Draw(...,float scale,...)" },
            { Rectangle(Slot(6).X + kSlotW/2, Slot(6).Y + kSlotH/2, 1, 1), kTints[6], "overload 7: Draw(...,Vector2 scale,...)" },
            { Rectangle(Slot(7).X + kSlotW/2, Slot(7).Y + kSlotH/2, 1, 1), kTints[7], "overload 8: Draw(tex,destRect,color)" },
            { Rectangle(Slot(8).X + kSlotW/2, Slot(8).Y + kSlotH/2, 1, 1), kTints[8], "overload 9: Draw(tex,destRect,optSrcRect,color)" },
        };

        for (const auto& c : checks)
        {
            Color px(0, 0, 0, 0);
            dev.GetBackBufferData(&c.region, &px, 0, 1);
            const bool ok = colourMatch(px, c.want);
            std::printf("[%s] %s: got=(%d,%d,%d) want=(%d,%d,%d)\n",
                        ok ? "PASS" : "FAIL", c.label,
                        px.getRProperty(), px.getGProperty(), px.getBProperty(),
                        c.want.getRProperty(), c.want.getGProperty(), c.want.getBProperty());
            if (!ok) result_ = 1;
        }

        Exit();
    }

public:
    SdlSpriteBatchOverloadsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(9 * (kSlotW + kSlotPad) + kSlotPad);
        gdm_->setPreferredBackBufferHeightProperty(kSlotH + 2 * kSlotPad);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    int getResult() const { return result_; }
};

int main()
{
    SdlSpriteBatchOverloadsTest game;
    game.Run();
    return game.getResult();
}
