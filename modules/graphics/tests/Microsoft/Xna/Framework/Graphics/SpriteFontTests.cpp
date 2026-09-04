// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <stdexcept>
#include <optional>
#include <vector>

#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/ArgumentException.hpp"
#include "System/Text/StringBuilder.hpp"

using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::SpriteFont;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using SharpRuntime::charcs;
using System::Text::StringBuilder;

// -----------------------------------------------------------------------
// Test fixtures / helpers
// -----------------------------------------------------------------------

// Single-glyph font: character 'A', kern=(leftBearing=1, width=8, rightBearing=2),
// cropping height=12, lineSpacing=16, spacing=0.
//   MeasureString("A")  → X = max(1,0)+8+2 = 11,  Y = 16
//   MeasureString("AA") → X = 11 + (0+1)+8+2 = 22, Y = 16
static SpriteFont makeFontA(float spacing = 0.0f,
                             std::optional<charcs> defaultChar = std::nullopt)
{
    std::vector<Rectangle> glyphs   = { {0, 0, 8, 12} };
    std::vector<Rectangle> cropping = { {0, 0, 8, 12} };
    std::vector<charcs>    chars    = { u'A' };
    std::vector<Vector3>   kern     = { Vector3(1.0f, 8.0f, 2.0f) };
    return SpriteFont(Texture2D{}, glyphs, cropping, chars,
                      /*lineSpacing=*/16, spacing, kern, defaultChar);
}

// Two-glyph font: 'A' (kern 1,8,2) and 'B' (kern 0,6,1).
//   MeasureString("AB") with spacing=s:
//     A first:  max(1,0)+8+2 = 11
//     B second: (s+0)+6+1  = s+7
//     total X   = 11+s+7 = 18+s
static SpriteFont makeFontAB(float spacing = 0.0f)
{
    std::vector<Rectangle> glyphs   = { {0, 0, 8, 12}, {8, 0, 6, 14} };
    std::vector<Rectangle> cropping = { {0, 0, 8, 12}, {0, 0, 6, 14} };
    std::vector<charcs>    chars    = { u'A', u'B' };
    std::vector<Vector3>   kern     = { Vector3(1.0f, 8.0f, 2.0f),
                                        Vector3(0.0f, 6.0f, 1.0f) };
    return SpriteFont(Texture2D{}, glyphs, cropping, chars,
                      /*lineSpacing=*/16, spacing, kern, std::nullopt);
}

// Three glyphs from cna-go's Foundation 69 measurement, whose 'B' carries a NEGATIVE right
// side bearing -- the case where XNA and FNA disagree and every glyph above hides it.
//   '?' kern (1, 4, 2), 'A' kern (0, 5, 0), 'B' kern (-3, 6, -2), lineSpacing 10, spacing 1.
static SpriteFont makeFontOverhang()
{
    std::vector<Rectangle> glyphs   = { {0, 0, 4, 8}, {4, 0, 5, 8}, {9, 0, 6, 8} };
    std::vector<Rectangle> cropping = { {0, 0, 4, 8}, {0, 0, 5, 8}, {0, 0, 6, 8} };
    std::vector<charcs>    chars    = { u'?', u'A', u'B' };
    std::vector<Vector3>   kern     = { Vector3(1.0f, 4.0f, 2.0f),
                                        Vector3(0.0f, 5.0f, 0.0f),
                                        Vector3(-3.0f, 6.0f, -2.0f) };
    return SpriteFont(Texture2D{}, glyphs, cropping, chars,
                      /*lineSpacing=*/10, /*spacing=*/1.0f, kern, std::nullopt);
}

// -----------------------------------------------------------------------
// MeasureString -- a line ending in a negative right side bearing
//
// XNA holds each glyph's right bearing back, adds it to the NEXT glyph unclamped, and adds
// Math.Max(pending, 0) at every line break and once after the loop (IL_00da, IL_010d, IL_0054,
// IL_015c). So a trailing overhang -- a negative bearing -- occupies no width to the right of
// where the line ends. FNA adds `cKern.Y + cKern.Z` per glyph instead, which is right for every
// interior glyph and short by the overhang for the last one; CNA matched FNA until CLAUDE.md
// settled that XNA wins. The numbers here are cna-go's XNA column, measured independently by
// cna-ts on its own font.
// -----------------------------------------------------------------------

TEST(SpriteFontTest, MeasureDoesNotSubtractATrailingOverhang)
{
    SpriteFont font = makeFontOverhang();
    // 'B' alone: max(-3, 0) + 6 = 6, and its -2 overhang adds nothing. FNA answered 4.
    EXPECT_FLOAT_EQ(font.MeasureString(std::string("B")).X, 6.0f);
}

TEST(SpriteFontTest, MeasureCountsAnInteriorOverhangButNotATrailingOne)
{
    SpriteFont font = makeFontOverhang();
    // "AB": A gives max(0,0)+5 = 5 holding 0; B adds spacing 1 + (-3) + held 0 + 6 = 9,
    // and its -2 is trailing. FNA answered 7.
    EXPECT_FLOAT_EQ(font.MeasureString(std::string("AB")).X, 9.0f);
}

TEST(SpriteFontTest, MeasureClampsTheOverhangOnEveryLineNotJustTheLast)
{
    SpriteFont font = makeFontOverhang();
    const Vector2 size = font.MeasureString(std::string("AB\nA"));
    // The first line still measures 9 -- the clamp happens at the break as well as at the end --
    // and it is the wider of the two. FNA answered 7.
    EXPECT_FLOAT_EQ(size.X, 9.0f);
    EXPECT_FLOAT_EQ(size.Y, 20.0f);
}

TEST(SpriteFontTest, MeasureKeepsAPositiveTrailingBearing)
{
    SpriteFont font = makeFontOverhang();
    // '?' ends in +2, which is width rather than overhang: max(1,0) + 4 + 2 = 7. This is the
    // case where XNA and FNA agree, and it must not have moved.
    EXPECT_FLOAT_EQ(font.MeasureString(std::string("?")).X, 7.0f);
}

// -----------------------------------------------------------------------
// Constructor / property getters
// -----------------------------------------------------------------------

TEST(SpriteFontTest, ConstructorSetsLineSpacing)
{
    SpriteFont font = makeFontA();
    EXPECT_EQ(font.getLineSpacingProperty(), 16);
}

TEST(SpriteFontTest, ConstructorSetsSpacing)
{
    SpriteFont font = makeFontA(3.0f);
    EXPECT_FLOAT_EQ(font.getSpacingProperty(), 3.0f);
}

TEST(SpriteFontTest, ConstructorSetsDefaultCharacterNullopt)
{
    SpriteFont font = makeFontA();
    EXPECT_FALSE(font.getDefaultCharacterProperty().has_value());
}

TEST(SpriteFontTest, ConstructorSetsDefaultCharacterValue)
{
    SpriteFont font = makeFontA(0.0f, u'A');
    ASSERT_TRUE(font.getDefaultCharacterProperty().has_value());
    EXPECT_EQ(font.getDefaultCharacterProperty().value(), u'A');
}

TEST(SpriteFontTest, ConstructorBuildsCharacterList)
{
    SpriteFont font = makeFontAB();
    const auto& chars = font.getCharactersProperty();
    ASSERT_EQ(chars.size(), 2u);
    EXPECT_EQ(chars[0], u'A');
    EXPECT_EQ(chars[1], u'B');
}

TEST(SpriteFontTest, EmptyFontHasEmptyCharacterList)
{
    SpriteFont font(Texture2D{}, {}, {}, {}, 0, 0.0f, {}, std::nullopt);
    EXPECT_TRUE(font.getCharactersProperty().empty());
}

// -----------------------------------------------------------------------
// REMED-GFX-002: defaultCharacter must be present in characters, validated at the point the
// invariant would be violated (construction / setter), rather than left to invoke UB the first
// time MeasureString/DrawString needs the fallback.
// -----------------------------------------------------------------------

TEST(SpriteFontTest, ConstructorThrowsWhenDefaultCharacterAbsentFromCharacters)
{
    std::vector<Rectangle> glyphs   = { {0, 0, 8, 12} };
    std::vector<Rectangle> cropping = { {0, 0, 8, 12} };
    std::vector<charcs>    chars    = { u'A' };
    std::vector<Vector3>   kern     = { Vector3(1.0f, 8.0f, 2.0f) };
    EXPECT_THROW(
        SpriteFont(Texture2D{}, glyphs, cropping, chars, 16, 0.0f, kern,
                   std::optional<charcs>(u'?')),
        System::ArgumentException);
}

TEST(SpriteFontTest, ConstructorAcceptsDefaultCharacterPresentInCharacters)
{
    EXPECT_NO_THROW({ SpriteFont font = makeFontA(0.0f, u'A'); });
}

// -----------------------------------------------------------------------
// Property setters
// -----------------------------------------------------------------------

TEST(SpriteFontTest, SetLineSpacing)
{
    SpriteFont font = makeFontA();
    font.setLineSpacingProperty(24);
    EXPECT_EQ(font.getLineSpacingProperty(), 24);
}

TEST(SpriteFontTest, SetSpacing)
{
    SpriteFont font = makeFontA();
    font.setSpacingProperty(5.0f);
    EXPECT_FLOAT_EQ(font.getSpacingProperty(), 5.0f);
}

TEST(SpriteFontTest, SetDefaultCharacterToValue)
{
    SpriteFont font = makeFontA();
    font.setDefaultCharacterProperty(u'A');
    ASSERT_TRUE(font.getDefaultCharacterProperty().has_value());
    EXPECT_EQ(font.getDefaultCharacterProperty().value(), u'A');
}

TEST(SpriteFontTest, SetDefaultCharacterToNullopt)
{
    SpriteFont font = makeFontA(0.0f, u'A');
    font.setDefaultCharacterProperty(std::nullopt);
    EXPECT_FALSE(font.getDefaultCharacterProperty().has_value());
}

TEST(SpriteFontTest, SetDefaultCharacterThrowsWhenAbsentFromCharacters)
{
    SpriteFont font = makeFontA();   // only 'A' is a known character
    EXPECT_THROW(font.setDefaultCharacterProperty(u'Z'), System::ArgumentException);
    // The rejected assignment must not have taken effect.
    EXPECT_FALSE(font.getDefaultCharacterProperty().has_value());
}

// -----------------------------------------------------------------------
// MeasureString — empty string
// -----------------------------------------------------------------------

TEST(SpriteFontTest, MeasureEmptyStringReturnsZero)
{
    SpriteFont font = makeFontA();
    Vector2 size = font.MeasureString(std::string(""));
    EXPECT_FLOAT_EQ(size.X, 0.0f);
    EXPECT_FLOAT_EQ(size.Y, 0.0f);
}

// -----------------------------------------------------------------------
// MeasureString — single character
// -----------------------------------------------------------------------

TEST(SpriteFontTest, MeasureSingleCharWidth)
{
    // A: max(1,0)+8+2 = 11
    SpriteFont font = makeFontA();
    Vector2 size = font.MeasureString(std::string("A"));
    EXPECT_FLOAT_EQ(size.X, 11.0f);
}

TEST(SpriteFontTest, MeasureSingleCharHeightEqualsLineSpacing)
{
    // cropHeight=12 < lineSpacing=16 → finalLineHeight stays 16
    SpriteFont font = makeFontA();
    Vector2 size = font.MeasureString(std::string("A"));
    EXPECT_FLOAT_EQ(size.Y, 16.0f);
}

// -----------------------------------------------------------------------
// MeasureString — two characters, spacing
// -----------------------------------------------------------------------

TEST(SpriteFontTest, MeasureTwoCharsNoSpacing)
{
    // "AA": 11 + (0+1)+8+2 = 22
    SpriteFont font = makeFontA(0.0f);
    Vector2 size = font.MeasureString(std::string("AA"));
    EXPECT_FLOAT_EQ(size.X, 22.0f);
}

TEST(SpriteFontTest, MeasureTwoCharsWithSpacing)
{
    // "AA" spacing=3: 11 + (3+1)+8+2 = 25
    SpriteFont font = makeFontA(3.0f);
    Vector2 size = font.MeasureString(std::string("AA"));
    EXPECT_FLOAT_EQ(size.X, 25.0f);
}

TEST(SpriteFontTest, MeasureTwoDifferentCharsNoSpacing)
{
    // "AB": 11 + (0+0)+6+1 = 18
    SpriteFont font = makeFontAB(0.0f);
    Vector2 size = font.MeasureString(std::string("AB"));
    EXPECT_FLOAT_EQ(size.X, 18.0f);
}

TEST(SpriteFontTest, MeasureTwoDifferentCharsWithSpacing)
{
    // "AB" spacing=2: 11 + (2+0)+6+1 = 20
    SpriteFont font = makeFontAB(2.0f);
    Vector2 size = font.MeasureString(std::string("AB"));
    EXPECT_FLOAT_EQ(size.X, 20.0f);
}

// -----------------------------------------------------------------------
// MeasureString — a line's first glyph, whose left side bearing is negative
//
// Measured against a live XNA 4.0 build (samples campaign SAMPLE-031), because FNA and XNA
// disagree here and only one of them is the specification. The font is Segoe UI Mono Bold at
// 12pt as the BloomPostprocess sample builds it: 'A' and 'X' have kerning.X = -1, 'B' has +1.
// XNA's MeasureString returns 11 for "A" (= 0 + 11 + 0) and 10 for "B" (= 1 + 8 + 1), so a
// negative bearing is CLAMPED AWAY on a line's first glyph rather than reflected: FNA's
// Math.Abs would have made "A" 12. The same three single-character widths come back unchanged
// when the font is rebuilt with Spacing 3 or -2, so Spacing is not applied there either.
// -----------------------------------------------------------------------

// Single-glyph font whose 'N' has a NEGATIVE left side bearing, kern=(-1, 8, 2).
static SpriteFont makeFontNegativeBearing(float spacing = 0.0f)
{
    std::vector<Rectangle> glyphs   = { {0, 0, 8, 12} };
    std::vector<Rectangle> cropping = { {0, 0, 8, 12} };
    std::vector<charcs>    chars    = { u'N' };
    std::vector<Vector3>   kern     = { Vector3(-1.0f, 8.0f, 2.0f) };
    return SpriteFont(Texture2D{}, glyphs, cropping, chars,
                      /*lineSpacing=*/16, spacing, kern, std::nullopt);
}

TEST(SpriteFontTest, FirstGlyphOfALineClampsANegativeLeftSideBearingToZero)
{
    // max(-1,0)+8+2 = 10. Math.Abs would give 11.
    SpriteFont font = makeFontNegativeBearing();
    EXPECT_FLOAT_EQ(font.MeasureString(std::string("N")).X, 10.0f);
}

TEST(SpriteFontTest, ANegativeLeftSideBearingStillAppliesAfterTheFirstGlyph)
{
    // "NN": 10 + (0 + -1) + 8 + 2 = 19. Only the FIRST glyph of a line is clamped.
    SpriteFont font = makeFontNegativeBearing();
    EXPECT_FLOAT_EQ(font.MeasureString(std::string("NN")).X, 19.0f);
}

TEST(SpriteFontTest, SpacingDoesNotApplyToTheFirstGlyphOfALine)
{
    // The measured XNA behaviour: a single character measures the same at any Spacing.
    EXPECT_FLOAT_EQ(makeFontNegativeBearing(0.0f).MeasureString(std::string("N")).X, 10.0f);
    EXPECT_FLOAT_EQ(makeFontNegativeBearing(3.0f).MeasureString(std::string("N")).X, 10.0f);
    EXPECT_FLOAT_EQ(makeFontNegativeBearing(-2.0f).MeasureString(std::string("N")).X, 10.0f);
    // ...while the second glyph does take it: 10 + (3 + -1) + 8 + 2 = 22.
    EXPECT_FLOAT_EQ(makeFontNegativeBearing(3.0f).MeasureString(std::string("NN")).X, 22.0f);
}

TEST(SpriteFontTest, EveryLineClampsItsOwnFirstGlyph)
{
    // Both lines are 10 wide, so the widest line is 10 rather than 11.
    SpriteFont font = makeFontNegativeBearing();
    EXPECT_FLOAT_EQ(font.MeasureString(std::string("N\nN")).X, 10.0f);
}

TEST(SpriteFontTest, StringBuilderOverloadClampsTheFirstGlyphTheSameWay)
{
    SpriteFont font = makeFontNegativeBearing();
    StringBuilder sb;
    sb.Append(std::string("N"));
    EXPECT_FLOAT_EQ(font.MeasureString(sb).X, 10.0f);
}

// -----------------------------------------------------------------------
// MeasureString — height from tallest glyph crop
// -----------------------------------------------------------------------

TEST(SpriteFontTest, MeasureHeightFromTallestCropHeight)
{
    // 'B' crop height=14 > lineSpacing=16? No: 14 < 16, stays 16.
    // Make a font where crop exceeds lineSpacing.
    std::vector<Rectangle> glyphs   = { {0, 0, 8, 20} };
    std::vector<Rectangle> cropping = { {0, 0, 8, 20} };
    std::vector<charcs>    chars    = { u'A' };
    std::vector<Vector3>   kern     = { Vector3(0.0f, 8.0f, 0.0f) };
    SpriteFont font(Texture2D{}, glyphs, cropping, chars, 16, 0.0f, kern, std::nullopt);
    Vector2 size = font.MeasureString(std::string("A"));
    EXPECT_FLOAT_EQ(size.Y, 20.0f);
}

// -----------------------------------------------------------------------
// MeasureString — multi-line (\n)
// -----------------------------------------------------------------------

TEST(SpriteFontTest, MeasureMultiLineWidth)
{
    // "A\nAA": line1=11, line2=22 → X=22
    SpriteFont font = makeFontA(0.0f);
    Vector2 size = font.MeasureString(std::string("A\nAA"));
    EXPECT_FLOAT_EQ(size.X, 22.0f);
}

TEST(SpriteFontTest, MeasureMultiLineHeight)
{
    // "A\nA": Y = lineSpacing + finalLineHeight = 16 + 16 = 32
    SpriteFont font = makeFontA(0.0f);
    Vector2 size = font.MeasureString(std::string("A\nA"));
    EXPECT_FLOAT_EQ(size.Y, 32.0f);
}

TEST(SpriteFontTest, MeasureThreeLinesHeight)
{
    SpriteFont font = makeFontA(0.0f);
    Vector2 size = font.MeasureString(std::string("A\nA\nA"));
    EXPECT_FLOAT_EQ(size.Y, 48.0f);
}

// -----------------------------------------------------------------------
// MeasureString — carriage return (\r) is ignored
// -----------------------------------------------------------------------

TEST(SpriteFontTest, CarriageReturnIsIgnored)
{
    // "\r" alone: no chars rendered → same as "A" without the \r
    SpriteFont font = makeFontA(0.0f);
    Vector2 noR   = font.MeasureString(std::string("A"));
    Vector2 withR = font.MeasureString(std::string("A\rA"));
    // \r is skipped; "A\rA" measures like "AA"
    SpriteFont font2 = makeFontA(0.0f);
    Vector2 aa = font2.MeasureString(std::string("AA"));
    EXPECT_FLOAT_EQ(withR.X, aa.X);
    EXPECT_FLOAT_EQ(withR.Y, aa.Y);
}

TEST(SpriteFontTest, ConsecutiveCarriageReturnsAreAllIgnored)
{
    // "A\r\rA": both \r are skipped, measures like "AA".
    SpriteFont font = makeFontA(0.0f);
    Vector2 withRR = font.MeasureString(std::string("A\r\rA"));
    SpriteFont font2 = makeFontA(0.0f);
    Vector2 aa = font2.MeasureString(std::string("AA"));
    EXPECT_FLOAT_EQ(withRR.X, aa.X);
    EXPECT_FLOAT_EQ(withRR.Y, aa.Y);
}

// -----------------------------------------------------------------------
// MeasureString — leading/trailing newline (an empty first/last line still
// contributes its own lineSpacing height, matching FNA's per-'\n' Y += LineSpacing
// followed by the loop's own unconditional final Y += finalLineHeight)
// -----------------------------------------------------------------------

TEST(SpriteFontTest, LeadingNewlineAddsAnEmptyFirstLine)
{
    // "\nA": leading '\n' -> Y += lineSpacing for the (empty) first line, then
    // "A" is the second line -> Y += lineSpacing again. Total Y = 2*lineSpacing.
    SpriteFont font = makeFontA(0.0f);
    Vector2 size = font.MeasureString(std::string("\nA"));
    EXPECT_FLOAT_EQ(size.X, 11.0f);
    EXPECT_FLOAT_EQ(size.Y, 32.0f);
}

TEST(SpriteFontTest, TrailingNewlineAddsAnEmptyLastLine)
{
    // "A\n": '\n' after "A" -> Y += lineSpacing for the "A" line, then the loop's
    // own final Y += finalLineHeight adds a second, empty trailing line.
    // Total Y = 2*lineSpacing, not 1*lineSpacing.
    SpriteFont font = makeFontA(0.0f);
    Vector2 size = font.MeasureString(std::string("A\n"));
    EXPECT_FLOAT_EQ(size.X, 11.0f);
    EXPECT_FLOAT_EQ(size.Y, 32.0f);
}

// -----------------------------------------------------------------------
// MeasureString — unknown glyph behaviour
// -----------------------------------------------------------------------

TEST(SpriteFontTest, UnknownCharWithNoDefaultThrows)
{
    SpriteFont font = makeFontA();   // no default character
    EXPECT_THROW(
        { [[maybe_unused]] auto r = font.MeasureString(std::string("B")); },
        std::invalid_argument);
}

TEST(SpriteFontTest, UnknownCharWithDefaultFallsBackToDefault)
{
    // default='A' → 'B' is rendered as 'A'
    SpriteFont font = makeFontA(0.0f, u'A');
    Vector2 fallback = font.MeasureString(std::string("B"));
    Vector2 expected = font.MeasureString(std::string("A"));
    EXPECT_FLOAT_EQ(fallback.X, expected.X);
    EXPECT_FLOAT_EQ(fallback.Y, expected.Y);
}

// -----------------------------------------------------------------------
// MeasureString(StringBuilder) — Task 423: FNA has both a MeasureString(string)
// and a MeasureString(StringBuilder) overload; the latter was entirely missing
// from CNA until now. Forwards to MeasureString(string) via ToString(),
// mirroring SpriteBatch::DrawString's existing StringBuilder-overload
// convention. Mirrors a representative subset of the MeasureString(string)
// suite above rather than duplicating every case, since the implementation
// is a straightforward forwarding call.
// -----------------------------------------------------------------------

TEST(SpriteFontTest, MeasureStringBuilderEmptyReturnsZero)
{
    SpriteFont font = makeFontA();
    Vector2 size = font.MeasureString(StringBuilder(""));
    EXPECT_FLOAT_EQ(size.X, 0.0f);
    EXPECT_FLOAT_EQ(size.Y, 0.0f);
}

TEST(SpriteFontTest, MeasureStringBuilderMatchesStringOverloadForSingleChar)
{
    SpriteFont font = makeFontA();
    Vector2 fromString  = font.MeasureString(std::string("A"));
    Vector2 fromBuilder = font.MeasureString(StringBuilder("A"));
    EXPECT_FLOAT_EQ(fromBuilder.X, fromString.X);
    EXPECT_FLOAT_EQ(fromBuilder.Y, fromString.Y);
}

TEST(SpriteFontTest, MeasureStringBuilderMatchesStringOverloadForMultiLineAppended)
{
    // Built up via Append() calls rather than a single literal, exercising the
    // actual StringBuilder buffer-accumulation path, not just a wrapped literal.
    StringBuilder sb;
    sb.Append("A").Append('\n').Append("AA");

    SpriteFont font = makeFontA(0.0f);
    Vector2 fromBuilder = font.MeasureString(sb);
    Vector2 fromString  = font.MeasureString(std::string("A\nAA"));
    EXPECT_FLOAT_EQ(fromBuilder.X, fromString.X);
    EXPECT_FLOAT_EQ(fromBuilder.Y, fromString.Y);
}

TEST(SpriteFontTest, MeasureStringBuilderUnknownCharWithNoDefaultThrows)
{
    SpriteFont font = makeFontA();   // no default character
    EXPECT_THROW(
        { [[maybe_unused]] auto r = font.MeasureString(StringBuilder("B")); },
        std::invalid_argument);
}

TEST(SpriteFontTest, MeasureStringBuilderUnknownCharWithDefaultFallsBackToDefault)
{
    SpriteFont font = makeFontA(0.0f, u'A');
    Vector2 fallback = font.MeasureString(StringBuilder("B"));
    Vector2 expected = font.MeasureString(std::string("A"));
    EXPECT_FLOAT_EQ(fallback.X, expected.X);
    EXPECT_FLOAT_EQ(fallback.Y, expected.Y);
}

// -----------------------------------------------------------------------
// CNAEXT glyph-table accessors
//
// The four tables SpriteBatch reads are what any layer re-implementing text layout above this
// class needs too, and they were unreachable outside the friendship. These assert that what comes
// back is exactly what went in, in the same order, so a font can be read back and rebuilt.
// -----------------------------------------------------------------------

TEST(SpriteFontExtTests, GlyphTablesRoundTripTheConstructorArguments)
{
    const std::vector<Rectangle> glyphs   = { {1, 2, 8, 12}, {9, 2, 6, 12} };
    const std::vector<Rectangle> cropping = { {0, 3, 8, 12}, {1, 4, 6, 12} };
    const std::vector<charcs>    chars    = { u'A', u'B' };
    const std::vector<Vector3>   kern     = { Vector3(1.0f, 8.0f, 2.0f),
                                              Vector3(0.0f, 6.0f, 1.0f) };
    const SpriteFont font(Texture2D{}, glyphs, cropping, chars, 16, 0.0f, kern, std::nullopt);

    ASSERT_EQ(font.getGlyphBoundsEXT().size(), chars.size());
    ASSERT_EQ(font.getCroppingEXT().size(), chars.size());
    ASSERT_EQ(font.getKerningEXT().size(), chars.size());

    for (std::size_t index = 0; index < chars.size(); ++index)
    {
        EXPECT_EQ(font.getGlyphBoundsEXT()[index], glyphs[index]);
        EXPECT_EQ(font.getCroppingEXT()[index], cropping[index]);
        EXPECT_FLOAT_EQ(font.getKerningEXT()[index].X, kern[index].X);
        EXPECT_FLOAT_EQ(font.getKerningEXT()[index].Y, kern[index].Y);
        EXPECT_FLOAT_EQ(font.getKerningEXT()[index].Z, kern[index].Z);
        EXPECT_EQ(font.getCharactersProperty()[index], chars[index]);
    }
}

TEST(SpriteFontExtTests, GlyphTablesAreParallelToTheCharacterList)
{
    const SpriteFont font = makeFontA();

    EXPECT_EQ(font.getGlyphBoundsEXT().size(), font.getCharactersProperty().size());
    EXPECT_EQ(font.getCroppingEXT().size(), font.getCharactersProperty().size());
    EXPECT_EQ(font.getKerningEXT().size(), font.getCharactersProperty().size());
}

TEST(SpriteFontExtTests, TextureAccessorAnswersTheAtlasTheFontWasBuiltWith)
{
    const Texture2D atlas{};
    const SpriteFont font(Texture2D{atlas}, { Rectangle{0, 0, 8, 12} },
                          { Rectangle{0, 0, 8, 12} }, { u'A' }, 16, 0.0f,
                          { Vector3(1.0f, 8.0f, 2.0f) }, std::nullopt);

    // A default-constructed atlas has no renderer, which is the observable property that
    // distinguishes it and is enough to prove the accessor answers the stored value rather than
    // a fresh object.
    EXPECT_EQ(font.getTextureEXT().HasRenderer(), atlas.HasRenderer());
    EXPECT_EQ(font.getTextureEXT().getWidthProperty(), atlas.getWidthProperty());
    EXPECT_EQ(font.getTextureEXT().getHeightProperty(), atlas.getHeightProperty());
}
