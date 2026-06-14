// SPDX-License-Identifier: MS-PL
// Task 27: SpriteFont property and MeasureString integration test.
//
// Constructs a minimal SpriteFont (3-character atlas) and tests:
//   MeasureString (empty, single char, multi-char, newline),
//   LineSpacing, Spacing, DefaultCharacter.
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <cmath>
#include <vector>
#include <optional>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static int g_failures = 0;

static void check(bool cond, const char* label)
{
    if (!cond)
    {
        std::fprintf(stderr, "FAIL: %s\n", label);
        ++g_failures;
    }
}

static bool feq(float a, float b) { return std::fabs(a - b) < 0.5f; }

class SpriteFontTest : public Game
{
protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        // 1×1 dummy atlas texture (SpriteFont stores it but MeasureString doesn't use it)
        const std::vector<uint8_t> px = {255, 255, 255, 255};
        Texture2D atlas = Texture2D::CreateFromPixels(device, 1, 1, px);

        // 3-character font: 'A' 'B' ' '
        // kerning = Vector3(leftBearing, charWidth, rightBearing)
        // 'A': kerning(0, 10, 2)  → width contribution: 12
        // 'B': kerning(1, 8, 1)   → width contribution (not first): spacing + 1 + 8 + 1
        // ' ': kerning(0, 6, 0)   → width contribution (not first): spacing + 0 + 6 + 0
        const std::vector<charcs>    chars     = {u' ', u'A', u'B'};
        const std::vector<Rectangle> bounds    = {
            Rectangle(0,0,1,1), Rectangle(0,0,1,1), Rectangle(0,0,1,1)};
        const std::vector<Rectangle> cropping  = {
            Rectangle(0,0,6,15), Rectangle(0,0,10,20), Rectangle(0,0,8,18)};
        const std::vector<Vector3>   kerning   = {
            Vector3{0.f,6.f,0.f}, Vector3{0.f,10.f,2.f}, Vector3{1.f,8.f,1.f}};
        const int   lineSpacing    = 24;
        const float spacing        = 0.0f;

        SpriteFont font(atlas, bounds, cropping, chars, lineSpacing, spacing, kerning,
                        std::optional<charcs>(u'?'));

        // Inject a '?' glyph so the default fallback works
        // (we set defaultCharacter = u'?', but '?' is not in the character list).
        // Re-make font with '?' included.
        const std::vector<charcs>    chars2    = {u' ', u'?', u'A', u'B'};
        const std::vector<Rectangle> bounds2   = {
            Rectangle(0,0,1,1), Rectangle(0,0,1,1),
            Rectangle(0,0,1,1), Rectangle(0,0,1,1)};
        const std::vector<Rectangle> cropping2 = {
            Rectangle(0,0,6,15), Rectangle(0,0,8,16),
            Rectangle(0,0,10,20), Rectangle(0,0,8,18)};
        const std::vector<Vector3>   kerning2  = {
            Vector3{0.f,6.f,0.f},  Vector3{0.f,8.f,0.f},
            Vector3{0.f,10.f,2.f}, Vector3{1.f,8.f,1.f}};

        SpriteFont f(atlas, bounds2, cropping2, chars2,
                     lineSpacing, spacing, kerning2,
                     std::optional<charcs>(u'?'));

        // --- MeasureString: empty string → (0, 0) ---
        {
            Vector2 v = f.MeasureString("");
            check(feq(v.X, 0.f) && feq(v.Y, 0.f), "MeasureString empty → (0,0)");
        }

        // --- MeasureString: single char 'A' ---
        // First char: curLineWidth = |0| + 10 + 2 = 12
        // finalLineHeight = max(lineSpacing=24, cropH=20) = 24
        // Result: (12, 24)
        {
            Vector2 v = f.MeasureString("A");
            check(feq(v.X, 12.f), "MeasureString 'A' width=12");
            check(feq(v.Y, 24.f), "MeasureString 'A' height=24");
        }

        // --- MeasureString: "AB" ---
        // 'A' (first): curLineWidth = 0 + 10 + 2 = 12
        // 'B' (next):  curLineWidth += 0(spacing) + 1 + 8 + 1 = 12 + 10 = 22
        // finalLineHeight = max(24, 20, 18) = 24
        // Result: (22, 24)
        {
            Vector2 v = f.MeasureString("AB");
            check(feq(v.X, 22.f), "MeasureString 'AB' width=22");
            check(feq(v.Y, 24.f), "MeasureString 'AB' height=24");
        }

        // --- MeasureString: newline "A\nB" ---
        // Line 1 'A': curLineWidth=12, finalLineH=24
        // '\n': result.X = max(0,12)=12, result.Y += 24 → result.Y=24, reset
        // Line 2 'B' (first in line): curLineWidth = |1|+8+1=10, finalLineH=24
        // End: result.X = max(12,10)=12, result.Y = 24+24=48
        {
            Vector2 v = f.MeasureString("A\nB");
            check(feq(v.X, 12.f), "MeasureString 'A\\nB' width=12");
            check(feq(v.Y, 48.f), "MeasureString 'A\\nB' height=48");
        }

        // --- MeasureString: unknown char uses DefaultCharacter '?' ---
        // 'Z' not in font → falls back to '?' kerning (0, 8, 0) → width = 8, height=24
        {
            Vector2 v = f.MeasureString("Z");
            check(feq(v.X, 8.f), "MeasureString unknown→default char width=8");
            check(feq(v.Y, 24.f), "MeasureString unknown→default char height=24");
        }

        // --- LineSpacing ---
        check(f.getLineSpacingProperty() == lineSpacing, "LineSpacing getter");
        f.setLineSpacingProperty(30);
        check(f.getLineSpacingProperty() == 30, "LineSpacing setter");

        // --- Spacing ---
        check(feq(f.getSpacingProperty(), 0.0f), "Spacing default 0");
        f.setSpacingProperty(2.5f);
        check(feq(f.getSpacingProperty(), 2.5f), "Spacing setter");

        // --- DefaultCharacter ---
        check(f.getDefaultCharacterProperty().has_value(), "DefaultCharacter has_value");
        check(f.getDefaultCharacterProperty().value() == u'?', "DefaultCharacter == '?'");
        f.setDefaultCharacterProperty(std::nullopt);
        check(!f.getDefaultCharacterProperty().has_value(), "DefaultCharacter nullopt");
        f.setDefaultCharacterProperty(std::optional<charcs>(u'*'));
        check(f.getDefaultCharacterProperty().value() == u'*', "DefaultCharacter set '*'");

        // --- Characters list ---
        const auto& ch = f.getCharactersProperty();
        check(ch.size() == 4, "Characters list size 4");

        Exit();
    }
};

int main(int, char**)
{
    auto* game = new SpriteFontTest();
    game->Run();
    delete game;
    if (g_failures == 0)
    {
        std::printf("SpriteFont: all checks PASS\n");
        return 0;
    }
    std::printf("SpriteFont: %d check(s) FAILED\n", g_failures);
    return 1;
}
