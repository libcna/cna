// SPDX-License-Identifier: MS-PL
// Migrated from the former ASCII graphics renderer (plans/plan_ascii.md Phase G2, ASCII-10/11): smoke
// test for AsciiPostProcessEffect's hand-authored glyph-density font atlas.
//
// Check A -- every glyph's pixel-count (popcount of its 8x8 bitmap) is strictly increasing along
//   kAsciiGlyphRamp -- Phase G4's quantizer picks a glyph by luminance rank, so a non-monotonic
//   ramp would pick visually wrong glyphs for a given brightness.
// Check B -- BuildAsciiFontAtlas() does not throw.
// Check C -- the returned SpriteFont has exactly kAsciiGlyphRampLength characters, in
//   kAsciiGlyphRamp's own order.
// Check D -- the SpriteFont's default character is space (matches kAsciiGlyphRamp[0]).
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"

#include "CNA/Internal/Graphics/Ascii/AsciiFontAtlas.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Graphics::Ascii;

namespace
{
    int Popcount8(std::uint8_t byte)
    {
        int count = 0;
        for (int i = 0; i < 8; ++i)
        {
            if (byte & (0x80 >> i)) ++count;
        }
        return count;
    }
}

class AsciiFontAtlasTest : public Game
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

        // Check A: strictly increasing pixel-count ramp.
        {
            bool monotonic = true;
            int previousCount = -1;
            for (int i = 0; i < kAsciiGlyphRampLength; ++i)
            {
                int count = 0;
                for (std::uint8_t row : GetAsciiGlyphBitmap(i)) count += Popcount8(row);
                if (count <= previousCount) { monotonic = false; break; }
                previousCount = count;
            }
            check(monotonic, "Glyph pixel-count ramp is strictly increasing along kAsciiGlyphRamp");
        }

        // Check B: BuildAsciiFontAtlas round-trips without throwing and has the right character set.
        bool threw = false;
        std::optional<SpriteFont> font;
        try
        {
            font.emplace(BuildAsciiFontAtlas(dev));
        }
        catch (const std::exception&) { threw = true; }
        check(!threw, "BuildAsciiFontAtlas does not throw");

        if (font.has_value())
        {
            const auto& chars = font->getCharactersProperty();
            bool matches = chars.size() == static_cast<std::size_t>(kAsciiGlyphRampLength);
            if (matches)
            {
                for (int i = 0; i < kAsciiGlyphRampLength; ++i)
                {
                    if (chars[static_cast<std::size_t>(i)] != static_cast<SharpRuntime::charcs>(kAsciiGlyphRamp[i]))
                    {
                        matches = false;
                        break;
                    }
                }
            }
            check(matches, "SpriteFont's character list matches kAsciiGlyphRamp exactly, in order");

            // Check C: default character is space.
            check(font->getDefaultCharacterProperty().has_value()
                      && font->getDefaultCharacterProperty().value() == static_cast<SharpRuntime::charcs>(' '),
                  "Default character is space");
        }
        else
        {
            check(false, "SpriteFont's character list matches kAsciiGlyphRamp exactly, in order");
            check(false, "Default character is space");
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, 4);
        result_ = (passCount_ == 4) ? 0 : 1;
        Exit();
    }

public:
    AsciiFontAtlasTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    AsciiFontAtlasTest game;
    game.Run();
    return game.getResult();
}
