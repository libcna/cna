// SPDX-License-Identifier: MS-PL
//
// CNAEXT. A small visual demo of the HTML_DOM graphics renderer, distinct from
// examples/htmldom_smoke_test.cpp's automated PASS/FAIL checks: this one exists to be looked at.
// It draws a soft radial-gradient sprite with real alpha falloff (rotated/scaled/tinted copies
// under AlphaBlend and Additive), a multi-character SpriteFont string ("HI"), and a small
// RenderTarget2D that is drawn INTO and then sampled back out as an ordinary texture at a larger
// scale on the main scene -- the render-target-as-source-texture path the automated smoke test
// never exercised.
//
// Not registered as a CTest, same as every other HTML_DOM/CANVAS entry -- it needs a real browser.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    // 24x24 soft radial blob: warm orange core fading to magenta at the rim, with a real alpha
    // falloff (not just a hard-edged circle) -- straight (non-premultiplied) RGBA, so a wrong
    // un-premultiply/tint pass would show up as dark fringing at the translucent edge.
    std::vector<std::uint8_t> MakeBlobPixels(int size)
    {
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size) * size * 4);
        const float cx = (size - 1) * 0.5f, cy = (size - 1) * 0.5f;
        const float maxR = size * 0.5f;
        for (int y = 0; y < size; ++y)
        {
            for (int x = 0; x < size; ++x)
            {
                const float dx = x - cx, dy = y - cy;
                const float r = std::sqrt(dx * dx + dy * dy) / maxR;
                const float t = std::clamp(r, 0.0f, 1.0f);
                const auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
                const std::uint8_t red = static_cast<std::uint8_t>(lerp(255.0f, 200.0f, t));
                const std::uint8_t green = static_cast<std::uint8_t>(lerp(160.0f, 30.0f, t));
                const std::uint8_t blue = static_cast<std::uint8_t>(lerp(60.0f, 220.0f, t));
                const std::uint8_t alpha = static_cast<std::uint8_t>(
                    std::clamp((1.0f - r) * 255.0f * 1.15f, 0.0f, 255.0f));
                const std::size_t i = (static_cast<std::size_t>(y) * size + x) * 4;
                pixels[i] = red; pixels[i + 1] = green; pixels[i + 2] = blue; pixels[i + 3] = alpha;
            }
        }
        return pixels;
    }

    // Fills a WxH block of `pixels` (row-major RGBA8) with a solid colour -- used to hand-author
    // two simple block glyphs ('H' and 'I') for the demo's SpriteFont atlas.
    void FillGlyph(std::vector<std::uint8_t>& atlas, int atlasW, int originX, int originY,
                   const std::vector<std::string>& rows, Color color)
    {
        for (int y = 0; y < static_cast<int>(rows.size()); ++y)
        {
            for (int x = 0; x < static_cast<int>(rows[y].size()); ++x)
            {
                if (rows[y][x] != '#') continue;
                const std::size_t i = (static_cast<std::size_t>(originY + y) * atlasW + (originX + x)) * 4;
                atlas[i] = color.getRProperty(); atlas[i + 1] = color.getGProperty();
                atlas[i + 2] = color.getBProperty(); atlas[i + 3] = color.getAProperty();
            }
        }
    }
}

class HtmlDomVisualDemo : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> blob_;
    std::unique_ptr<Texture2D> fontAtlas_;
    std::unique_ptr<SpriteFont> font_;
    std::unique_ptr<RenderTarget2D> stamp_;
    float t_ = 0.0f;

protected:
    void LoadContent() override
    {
        spriteBatch_ = std::make_unique<SpriteBatch>(getGraphicsDeviceProperty());

        constexpr int kBlobSize = 24;
        blob_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
            getGraphicsDeviceProperty(), kBlobSize, kBlobSize, MakeBlobPixels(kBlobSize)));

        // A 24x8 atlas holding two 6x8 block glyphs ('H' at x=0, 'I' at x=8) plus a blank 6x8
        // region at x=16 used as the space glyph -- zero-initialized, so it stays fully
        // transparent without any drawing.
        std::vector<std::uint8_t> atlas(24 * 8 * 4, 0);
        FillGlyph(atlas, 24, 0, 0, {
            "#....#",
            "#....#",
            "#....#",
            "######",
            "#....#",
            "#....#",
            "#....#",
            "#....#",
        }, Color::White);
        FillGlyph(atlas, 24, 8, 0, {
            "######",
            "..##..",
            "..##..",
            "..##..",
            "..##..",
            "..##..",
            "..##..",
            "######",
        }, Color::White);
        fontAtlas_ = std::make_unique<Texture2D>(
            Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 24, 8, atlas));
        font_ = std::make_unique<SpriteFont>(
            *fontAtlas_,
            std::vector<Rectangle>{Rectangle(0, 0, 6, 8), Rectangle(8, 0, 6, 8), Rectangle(16, 0, 6, 8)},
            std::vector<Rectangle>{Rectangle(0, 0, 6, 8), Rectangle(0, 0, 6, 8), Rectangle(0, 0, 6, 8)},
            std::vector<charcs>{u'H', u'I', u' '},
            /*lineSpacing=*/10, /*spacing=*/2.0f,
            std::vector<Vector3>{Vector3(0.0f, 6.0f, 1.0f), Vector3(1.0f, 6.0f, 0.0f), Vector3(0.0f, 6.0f, 0.0f)},
            std::nullopt);

        // A 40x40 render target: drawn into once (three small tinted blobs), then sampled back as
        // an ordinary texture at 2x scale on the main scene every frame -- the render-target-as-
        // source-texture path the automated smoke test never covers.
        stamp_ = std::make_unique<RenderTarget2D>(getGraphicsDeviceProperty(), 40, 40);
        auto& dev = getGraphicsDeviceProperty();
        dev.SetRenderTarget(stamp_.get());
        dev.Clear(Color(20, 10, 40, 255));
        spriteBatch_->Begin();
        spriteBatch_->Draw(*blob_, Rectangle(2, 2, 20, 20), Rectangle(0, 0, kBlobSize, kBlobSize),
                           Color(120, 220, 255, 255));
        spriteBatch_->Draw(*blob_, Rectangle(18, 10, 20, 20), Rectangle(0, 0, kBlobSize, kBlobSize),
                           Color(255, 255, 120, 220));
        spriteBatch_->Draw(*blob_, Rectangle(10, 18, 20, 20), Rectangle(0, 0, kBlobSize, kBlobSize),
                           Color(120, 255, 160, 220));
        spriteBatch_->End();
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    }

    void Update(GameTime& gameTime) override
    {
        t_ += static_cast<float>(gameTime.getElapsedGameTimeProperty().getTotalSecondsProperty());
    }

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(Color(18, 22, 38, 255));

        spriteBatch_->Begin();

        // A row of the same blob at increasing scale and rotation, alternating tints -- exercises
        // rotation+scale+flip+tint together, and AlphaBlend's real edge compositing (no dark
        // fringing on the soft alpha falloff if the un-premultiply math is right).
        for (int i = 0; i < 6; ++i)
        {
            const float scale = 1.0f + i * 0.35f;
            const int size = static_cast<int>(24 * scale);
            const Color tint = (i % 2 == 0) ? Color(255, 210, 140, 255) : Color(150, 200, 255, 255);
            const SpriteEffects effects = (i % 3 == 0) ? SpriteEffects::FlipHorizontally : SpriteEffects::None;
            spriteBatch_->Draw(*blob_,
                               Rectangle(30 + i * 70, 60, size, size),
                               Rectangle(0, 0, 24, 24), tint,
                               t_ * (0.6f + i * 0.15f), Vector2(12, 12), effects, 0.0f);
        }
        spriteBatch_->End();

        // Two overlapping blobs under Additive blending -- the overlap should glow brighter than
        // either alone, visually proving `mix-blend-mode: plus-lighter` actually adds rather than
        // just replacing or averaging.
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Additive);
        spriteBatch_->Draw(*blob_, Rectangle(360, 170, 60, 60), Rectangle(0, 0, 24, 24),
                           Color(255, 60, 60, 255));
        spriteBatch_->Draw(*blob_, Rectangle(395, 170, 60, 60), Rectangle(0, 0, 24, 24),
                           Color(60, 120, 255, 255));
        spriteBatch_->End();

        // The render target sampled back as an ordinary texture, scaled 2x -- render-to-texture
        // actually round-tripping into a normal draw, not just read back via GetData.
        spriteBatch_->Begin();
        spriteBatch_->Draw(*stamp_, Rectangle(20, 190, 80, 80), Rectangle(0, 0, 40, 40),
                           Color::White);

        // Multi-character DrawString: kerning between two different glyphs, not just one letter.
        spriteBatch_->DrawString(*font_, "HI HI HI", Vector2(140, 210), Color(255, 255, 255, 255),
                                 0.0f, Vector2::Zero, 3.0f, SpriteEffects::None, 0.0f);
        spriteBatch_->End();
    }

public:
    HtmlDomVisualDemo()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(480);
        gdm_->setPreferredBackBufferHeightProperty(270);
    }
};

int main()
{
    HtmlDomVisualDemo game;
    game.Run();
    return 0;
}
