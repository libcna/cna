// SPDX-License-Identifier: MS-PL
// Texture and SpriteBatch test for the TinyGL renderer. Proves the real texture path
// (glGenTextures/glTexImage2D + TinyGL's own texel fetch) and the real 2D quad path, including the
// two texture behaviours that are specific to this renderer and must not regress silently:
//
// Check A -- Texture2D reports the size the game asked for, even though TinyGL resamples every
//   texture to 256x256 internally (TGL_FEATURE_TEXTURE_DIM).
// Check B -- Texture2D.GetData() round-trips the exact RGBA the game uploaded, because the
//   renderer keeps an untouched CPU shadow rather than reading back TinyGL's lossy RGB storage.
// Check C -- SpriteBatch draws a real textured quad whose pixels carry the texture's colour.
// Check D -- the tint colour modulates the texel, which is what TinyGL's lit-texture path does.
// Check E -- a fully transparent texel is NOT drawn: the renderer folds alpha below its cutout
//   threshold into TinyGL's own TGL_NO_DRAW_COLOR key, and TinyGL's rasterizer discards it. This
//   is the renderer's entire transparency model and the reason BlendState::AlphaBlend is accepted.
// Check F -- an opaque texel that happens to BE the key colour is still drawn (the +1 green nudge).
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "CNA/Internal/Renderers/TinyGL/TinyGLRenderer.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::TinyGL;

namespace
{
    constexpr int kChecks = 7;

    bool NearColor(const Color& actual, int r, int g, int b, int tolerance = 3)
    {
        return std::abs(static_cast<int>(actual.getRProperty()) - r) <= tolerance &&
               std::abs(static_cast<int>(actual.getGProperty()) - g) <= tolerance &&
               std::abs(static_cast<int>(actual.getBProperty()) - b) <= tolerance;
    }

    Color ReadPixel(GraphicsDevice& dev, int x, int y)
    {
        const Rectangle region(x, y, 1, 1);
        Color pixel(0, 0, 0, 0);
        dev.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }
}

class TinyGLTextureSpriteTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int passCount_ = 0;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        // A 4x4 texture: fully opaque green everywhere except texel (0,0), which is fully
        // transparent, and texel (1,0), which is opaque magenta -- the exact colour TinyGL uses as
        // its discard key.
        Texture2D texture(dev, 4, 4);
        std::vector<Color> source(16, Color(0, 255, 0, 255));
        source[0] = Color(255, 255, 255, 0);      // transparent
        source[1] = Color(255, 0, 255, 255);      // opaque, and identical to TGL_NO_DRAW_COLOR
        texture.SetData(source.data(), 0, static_cast<int>(source.size()));

        // Check A: the reported size is the requested size, not TinyGL's internal 256x256.
        check(texture.getWidthProperty() == 4 && texture.getHeightProperty() == 4,
              "Texture2D reports the requested size, not TinyGL's internal 256x256 storage");

        // Check B: GetData() is exact, including the alpha TinyGL itself cannot store.
        {
            std::vector<Color> readback(16, Color(0, 0, 0, 0));
            texture.GetData(readback.data(), 0, static_cast<int>(readback.size()));
            bool exact = readback.size() == source.size();
            for (std::size_t i = 0; exact && i < source.size(); ++i)
            {
                exact = readback[i].getRProperty() == source[i].getRProperty() &&
                        readback[i].getGProperty() == source[i].getGProperty() &&
                        readback[i].getBProperty() == source[i].getBProperty() &&
                        readback[i].getAProperty() == source[i].getAProperty();
            }
            check(exact, "Texture2D.GetData() round-trips the exact uploaded RGBA, alpha included");
        }

        // Check C/D: a real SpriteBatch quad, drawn opaque-tinted then half-tinted.
        {
            dev.Clear(Color(0, 0, 40, 255));
            SpriteBatch batch(dev);
            bool threw = false;
            try
            {
                batch.Begin();
                // 4x scale so each source texel covers a 4x4 block of the 64x64 backbuffer,
                // starting at (16,16). Texel (0,0) lands at (16..19, 16..19).
                batch.Draw(texture, Rectangle(16, 16, 16, 16), Rectangle(0, 0, 4, 4),
                           Color(255, 255, 255, 255));
                batch.End();
            }
            catch (...) { threw = true; }
            check(!threw, "SpriteBatch.Draw of a TinyGL texture does not throw");

            // A green texel in the middle of the quad.
            check(NearColor(ReadPixel(dev, 26, 26), 0, 255, 0),
                  "SpriteBatch renders the texture's colour through TinyGL's texel fetch");

            // Check E: the transparent texel was discarded by TinyGL's own colour key, so the
            // clear colour is still there.
            check(NearColor(ReadPixel(dev, 18, 18), 0, 0, 40),
                  "a texel below the alpha cutout threshold is discarded by TinyGL, not drawn");

            // Check F: the opaque key-coloured texel is still visible (nudged, not dropped).
            const Color keyTexel = ReadPixel(dev, 22, 18);
            check(!NearColor(keyTexel, 0, 0, 40) && keyTexel.getRProperty() > 200 &&
                      keyTexel.getBProperty() > 200,
                  "an opaque texel identical to TinyGL's discard key is still drawn");
        }

        // Check D (tint): half-intensity tint modulates the texel.
        {
            dev.Clear(Color(0, 0, 40, 255));
            SpriteBatch batch(dev);
            batch.Begin();
            batch.Draw(texture, Rectangle(16, 16, 16, 16), Rectangle(1, 1, 3, 3),
                       Color(128, 128, 128, 255));
            batch.End();
            const Color tinted = ReadPixel(dev, 26, 26);
            check(tinted.getGProperty() > 100 && tinted.getGProperty() < 160 &&
                      tinted.getRProperty() < 40,
                  "the SpriteBatch tint modulates the texel colour");
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, kChecks);
        result_ = (passCount_ == kChecks) ? 0 : 1;
        Exit();
    }

public:
    TinyGLTextureSpriteTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    [[nodiscard]] int getResult() const { return result_; }
};

int main()
{
    TinyGLTextureSpriteTest game;
    game.Run();
    return game.getResult();
}
