// SPDX-License-Identifier: MS-PL
// The ASCII effect's CPU quantizer and glyph atlas layout, checked against values worked out by
// hand from the documented contract (docs/ascii-post-process-effect.md, "Controls and grid" and
// "Color and alpha"; AsciiQuantizer.hpp; AsciiFontAtlas.hpp). Pure functions: no device, no window.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Internal/Graphics/Ascii/AsciiFontAtlas.hpp"
#include "CNA/Internal/Graphics/Ascii/AsciiQuantizer.hpp"
#include "System/Environment.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace CNA::Internal::Graphics::Ascii;
using Microsoft::Xna::Framework::Color;

struct Rgb
{
    int r;
    int g;
    int b;
};

std::vector<std::uint8_t> Image(int width, int height, const Rgb& fill)
{
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
    for (std::size_t i = 0; i < rgba.size(); i += 4)
    {
        rgba[i + 0] = static_cast<std::uint8_t>(fill.r);
        rgba[i + 1] = static_cast<std::uint8_t>(fill.g);
        rgba[i + 2] = static_cast<std::uint8_t>(fill.b);
        rgba[i + 3] = 255;
    }
    return rgba;
}

void SetPixel(std::vector<std::uint8_t>& rgba, int width, int x, int y, const Rgb& colour)
{
    const std::size_t at = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4U;
    rgba[at + 0] = static_cast<std::uint8_t>(colour.r);
    rgba[at + 1] = static_cast<std::uint8_t>(colour.g);
    rgba[at + 2] = static_cast<std::uint8_t>(colour.b);
    rgba[at + 3] = 255;
}

AsciiCell OnePixel(const Rgb& colour, AsciiQuantizeMode mode)
{
    const std::vector<std::uint8_t> rgba = Image(1, 1, colour);
    return QuantizeFrameToGrid(rgba.data(), 1, 1, 1, 1, mode).At(0, 0);
}

void ExpectColour(const Color& actual, int r, int g, int b, int a)
{
    EXPECT_EQ(actual.getRProperty(), r);
    EXPECT_EQ(actual.getGProperty(), g);
    EXPECT_EQ(actual.getBProperty(), b);
    EXPECT_EQ(actual.getAProperty(), a);
}

/// Sets or clears CNA_ASCII_MODE for one scope and restores the previous value.
class AsciiModeEnvironment
{
public:
    AsciiModeEnvironment()
    {
        if (const char* previous = std::getenv("CNA_ASCII_MODE")) previous_ = previous;
    }

    ~AsciiModeEnvironment()
    {
        if (previous_) Set(previous_->c_str());
        else Unset();
    }

    AsciiModeEnvironment(const AsciiModeEnvironment&) = delete;
    AsciiModeEnvironment& operator=(const AsciiModeEnvironment&) = delete;

    static void Set(const char* value)
    {
        System::Environment::SetEnvironmentVariable("CNA_ASCII_MODE", std::string(value));
    }

    static void Unset() { System::Environment::SetEnvironmentVariable("CNA_ASCII_MODE", std::nullopt); }

private:
    std::optional<std::string> previous_;
};

} // namespace

// ── Luminance -> glyph ───────────────────────────────────────────────────────────────────

TEST(AsciiQuantizerTest, GrayLevelsPickTheNearestOfTheTenEvenRampSteps)
{
    // A gray v has luma 0.299v + 0.587v + 0.114v = v, and the nearest of the ten evenly spaced
    // ramp steps is round(v * 9 / 255). The step boundaries are (k + 0.5) * 255 / 9 = 14.17,
    // 42.5, 70.83, 99.17, 127.5, 155.83, 184.17, 212.5, 240.83; each pair below straddles one.
    const struct
    {
        int gray;
        int glyph;
    } table[] = {
        {0, 0},   {14, 0},  {15, 1},  {42, 1},  {43, 2},  {70, 2},  {71, 3},
        {99, 3},  {100, 4}, {127, 4}, {128, 5}, {155, 5}, {156, 6}, {184, 6},
        {185, 7}, {212, 7}, {213, 8}, {240, 8}, {241, 9}, {255, 9},
    };
    for (const auto& row : table)
    {
        SCOPED_TRACE("gray " + std::to_string(row.gray));
        const Rgb gray{row.gray, row.gray, row.gray};
        EXPECT_EQ(OnePixel(gray, AsciiQuantizeMode::Color).glyphIndex, row.glyph);
        EXPECT_EQ(OnePixel(gray, AsciiQuantizeMode::BlackWhite).glyphIndex, row.glyph)
            << "the mode changes the colours, not the glyph";
    }
}

TEST(AsciiQuantizerTest, ColouredCellsWeightTheChannelsAsBt601Luma)
{
    // Luma 0.299 R + 0.587 G + 0.114 B, glyph round(luma * 9 / 255):
    //   red     76.245 -> 2.691 -> 3      green   149.685 -> 5.283 -> 5
    //   blue    29.070 -> 1.026 -> 1      yellow  225.930 -> 7.974 -> 8
    //   cyan   178.755 -> 6.309 -> 6      magenta 105.315 -> 3.717 -> 4
    // (Rec. 709 weights would give red 2 and green 6; an unweighted mean would give green 3.)
    EXPECT_EQ(OnePixel({255, 0, 0}, AsciiQuantizeMode::Color).glyphIndex, 3);
    EXPECT_EQ(OnePixel({0, 255, 0}, AsciiQuantizeMode::Color).glyphIndex, 5);
    EXPECT_EQ(OnePixel({0, 0, 255}, AsciiQuantizeMode::Color).glyphIndex, 1);
    EXPECT_EQ(OnePixel({255, 255, 0}, AsciiQuantizeMode::Color).glyphIndex, 8);
    EXPECT_EQ(OnePixel({0, 255, 255}, AsciiQuantizeMode::Color).glyphIndex, 6);
    EXPECT_EQ(OnePixel({255, 0, 255}, AsciiQuantizeMode::Color).glyphIndex, 4);
}

// ── Averaging and colours ────────────────────────────────────────────────────────────────

TEST(AsciiQuantizerTest, CellAveragesTruncateAndTheBackgroundIsAQuarterOfTheAverage)
{
    // "RGB averages use integer division": one black and one white pixel sum to 255 per
    // channel, 255 / 2 = 127 (a rounded mean would be 128). Luma 127 -> glyph 4 (128 -> 5).
    // The Color-mode background is a quarter of the average: 127 / 4 = 31.
    std::vector<std::uint8_t> pair = Image(2, 1, {0, 0, 0});
    SetPixel(pair, 2, 1, 0, {255, 255, 255});
    const AsciiCell mixed = QuantizeFrameToGrid(pair.data(), 2, 1, 2, 1, AsciiQuantizeMode::Color).At(0, 0);
    ExpectColour(mixed.foreground, 127, 127, 127, 255);
    ExpectColour(mixed.background, 31, 31, 31, 255);
    EXPECT_EQ(mixed.glyphIndex, 4);
    EXPECT_TRUE(mixed.hasBackground);

    // Per channel: (203, 102, 7) quarters to (50, 25, 1).
    const AsciiCell odd = OnePixel({203, 102, 7}, AsciiQuantizeMode::Color);
    ExpectColour(odd.foreground, 203, 102, 7, 255);
    ExpectColour(odd.background, 50, 25, 1, 255);

    // Three pixels with R = 1, 1, 2: 4 / 3 = 1.
    std::vector<std::uint8_t> three = Image(3, 1, {1, 0, 0});
    SetPixel(three, 3, 2, 0, {2, 0, 0});
    ExpectColour(QuantizeFrameToGrid(three.data(), 3, 1, 3, 1, AsciiQuantizeMode::Color).At(0, 0).foreground,
                 1, 0, 0, 255);
}

TEST(AsciiQuantizerTest, BlackWhiteCellsAreOpaqueWhiteGlyphsWithoutABackground)
{
    const AsciiCell blackWhite = OnePixel({128, 128, 128}, AsciiQuantizeMode::BlackWhite);
    EXPECT_EQ(blackWhite.glyphIndex, 5);
    ExpectColour(blackWhite.foreground, 255, 255, 255, 255);
    ExpectColour(blackWhite.background, 0, 0, 0, 0);
    EXPECT_FALSE(blackWhite.hasBackground);

    const AsciiCell colour = OnePixel({128, 128, 128}, AsciiQuantizeMode::Color);
    EXPECT_EQ(colour.glyphIndex, 5);
    ExpectColour(colour.foreground, 128, 128, 128, 255);
    ExpectColour(colour.background, 32, 32, 32, 255);
    EXPECT_TRUE(colour.hasBackground);
}

// ── Grid layout ──────────────────────────────────────────────────────────────────────────

TEST(AsciiQuantizerTest, GridDimensionsRoundUp)
{
    // columns = ceil(width / cellWidth), rows = ceil(height / cellHeight).
    const struct
    {
        int width, height, cellWidth, cellHeight, columns, rows;
    } table[] = {
        {1, 1, 8, 8, 1, 1},     {8, 8, 8, 8, 1, 1},   {9, 8, 8, 8, 2, 1},   {16, 17, 8, 8, 2, 3},
        {7, 3, 2, 2, 4, 2},     {100, 1, 7, 1, 15, 1}, {1, 100, 1, 33, 1, 4}, {64, 48, 64, 48, 1, 1},
    };
    for (const auto& row : table)
    {
        SCOPED_TRACE(std::to_string(row.width) + "x" + std::to_string(row.height) + " / "
                     + std::to_string(row.cellWidth) + "x" + std::to_string(row.cellHeight));
        const std::vector<std::uint8_t> rgba = Image(row.width, row.height, {10, 20, 30});
        const AsciiGrid grid =
            QuantizeFrameToGrid(rgba.data(), row.width, row.height, row.cellWidth, row.cellHeight,
                                AsciiQuantizeMode::Color);
        EXPECT_EQ(grid.columns, row.columns);
        EXPECT_EQ(grid.rows, row.rows);
        EXPECT_EQ(grid.cells.size(), static_cast<std::size_t>(row.columns * row.rows));
    }
}

TEST(AsciiQuantizerTest, TheGridIsRowMajorAndEachCellAveragesOnlyItsOwnBlock)
{
    // 4x4 image, 2x2 cells, a different solid colour in each quadrant.
    std::vector<std::uint8_t> rgba = Image(4, 4, {0, 0, 0});
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
        {
            const Rgb quadrant = y < 2 ? (x < 2 ? Rgb{200, 0, 0} : Rgb{0, 200, 0})
                                       : (x < 2 ? Rgb{0, 0, 200} : Rgb{40, 80, 120});
            SetPixel(rgba, 4, x, y, quadrant);
        }
    const AsciiGrid grid = QuantizeFrameToGrid(rgba.data(), 4, 4, 2, 2, AsciiQuantizeMode::Color);
    ASSERT_EQ(grid.columns, 2);
    ASSERT_EQ(grid.rows, 2);
    ASSERT_EQ(grid.cells.size(), 4U);
    ExpectColour(grid.cells[0].foreground, 200, 0, 0, 255);   // (col 0, row 0)
    ExpectColour(grid.cells[1].foreground, 0, 200, 0, 255);   // (col 1, row 0)
    ExpectColour(grid.cells[2].foreground, 0, 0, 200, 255);   // (col 0, row 1)
    ExpectColour(grid.cells[3].foreground, 40, 80, 120, 255); // (col 1, row 1)
    ExpectColour(grid.At(1, 0).foreground, 0, 200, 0, 255);
    ExpectColour(grid.At(0, 1).foreground, 0, 0, 200, 255);
}

TEST(AsciiQuantizerTest, PartialEdgeCellsAverageOnlyTheirRealPixels)
{
    // 5x3 image, 2x2 cells -> 3 columns, 2 rows. Pixel (x, y) is R = 50x, G = 100y, B = 7.
    //   cell (0,0): x 0-1, y 0-1 -> R (0+50+0+50)/4 = 25, G (0+0+100+100)/4 = 50
    //   cell (1,0): x 2-3, y 0-1 -> R 125, G 50
    //   cell (2,0): x 4,   y 0-1 -> R 200, G 50   (2 pixels)
    //   cell (0,1): x 0-1, y 2   -> R 25,  G 200  (2 pixels)
    //   cell (1,1): x 2-3, y 2   -> R 125, G 200
    //   cell (2,1): x 4,   y 2   -> R 200, G 200  (1 pixel); luma 177.998 -> glyph 6
    // A cell that read past its row would pick up the next row's x = 0 pixels (R = 0).
    std::vector<std::uint8_t> rgba = Image(5, 3, {0, 0, 0});
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 5; ++x)
            SetPixel(rgba, 5, x, y, {50 * x, 100 * y, 7});
    const AsciiGrid grid = QuantizeFrameToGrid(rgba.data(), 5, 3, 2, 2, AsciiQuantizeMode::Color);
    ASSERT_EQ(grid.columns, 3);
    ASSERT_EQ(grid.rows, 2);
    ExpectColour(grid.At(0, 0).foreground, 25, 50, 7, 255);
    ExpectColour(grid.At(1, 0).foreground, 125, 50, 7, 255);
    ExpectColour(grid.At(2, 0).foreground, 200, 50, 7, 255);
    ExpectColour(grid.At(0, 1).foreground, 25, 200, 7, 255);
    ExpectColour(grid.At(1, 1).foreground, 125, 200, 7, 255);
    ExpectColour(grid.At(2, 1).foreground, 200, 200, 7, 255);
    EXPECT_EQ(grid.At(2, 1).glyphIndex, 6);
}

// ── CNA_ASCII_MODE ──────────────────────────────────────────────────────────────────────

TEST(AsciiQuantizerTest, TheEnvironmentModeIsCaseInsensitiveAndEverythingElseIsColor)
{
    // docs/ascii-post-process-effect.md: COLOR or BLACKWHITE, case-insensitively; unset or
    // unrecognized text defaults to Color.
    const AsciiModeEnvironment restore;
    AsciiModeEnvironment::Unset();
    EXPECT_EQ(ParseAsciiModeFromEnvironment(), AsciiQuantizeMode::Color);
    for (const char* value : {"BLACKWHITE", "blackwhite", "BlackWhite", "bLaCkWhItE"})
    {
        AsciiModeEnvironment::Set(value);
        EXPECT_EQ(ParseAsciiModeFromEnvironment(), AsciiQuantizeMode::BlackWhite) << value;
    }
    for (const char* value : {"COLOR", "color", "Color", " blackwhite", "blackwhite ", "black",
                              "black-white", "blackwhite2", "colour", "1"})
    {
        AsciiModeEnvironment::Set(value);
        EXPECT_EQ(ParseAsciiModeFromEnvironment(), AsciiQuantizeMode::Color) << '"' << value << '"';
    }
}

// ── Atlas layout ────────────────────────────────────────────────────────────────────────

TEST(AsciiFontAtlasTest, TheAtlasIsOneRowOfElevenEightByEightCells)
{
    // Ten ramp glyphs plus the solid background slot, each kAsciiGlyphWidth x kAsciiGlyphHeight.
    ASSERT_EQ(std::strlen(kAsciiGlyphRamp), static_cast<std::size_t>(kAsciiGlyphRampLength));
    EXPECT_EQ(kAsciiGlyphRamp[0], ' ') << "the darkest cell must select the emptiest glyph";
    EXPECT_EQ(kAsciiSolidGlyphIndex, kAsciiGlyphRampLength);
    const CNA::Internal::Graphics::ImageData image = BuildAsciiFontAtlasImageData();
    EXPECT_EQ(image.width, 8 * 11);
    EXPECT_EQ(image.height, 8);
    EXPECT_EQ(image.pixels.size(), static_cast<std::size_t>(8 * 11 * 8 * 4));
}

TEST(AsciiFontAtlasTest, GlyphNSitsAtXEqualsEightNAndItsBitmapRowsReadMostSignificantBitLeft)
{
    const CNA::Internal::Graphics::ImageData image = BuildAsciiFontAtlasImageData();
    ASSERT_EQ(image.width, 88);
    const auto pixelAt = [&image](int x, int y) {
        const std::size_t at = (static_cast<std::size_t>(y) * 88U + static_cast<std::size_t>(x)) * 4U;
        return std::array<std::uint8_t, 4>{image.pixels[at], image.pixels[at + 1], image.pixels[at + 2],
                                           image.pixels[at + 3]};
    };
    const std::array<std::uint8_t, 4> on{255, 255, 255, 255};
    const std::array<std::uint8_t, 4> off{0, 0, 0, 0};
    for (int glyph = 0; glyph < kAsciiGlyphRampLength; ++glyph)
    {
        const auto rows = GetAsciiGlyphBitmap(glyph);
        int lit = 0;
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
            {
                const bool expected = (rows[static_cast<std::size_t>(y)] & (0x80U >> x)) != 0;
                lit += expected ? 1 : 0;
                EXPECT_EQ(pixelAt(8 * glyph + x, y), expected ? on : off)
                    << "glyph " << glyph << " ('" << kAsciiGlyphRamp[glyph] << "') pixel " << x << "," << y;
            }
        if (glyph == 0) EXPECT_EQ(lit, 0) << "the space glyph is empty";
        else EXPECT_GT(lit, 0) << "glyph " << glyph << " draws nothing";
    }
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            EXPECT_EQ(pixelAt(8 * kAsciiSolidGlyphIndex + x, y), on) << "solid slot pixel " << x << "," << y;
}

TEST(AsciiFontAtlasTest, GlyphBitmapsExistOnlyForRampIndices)
{
    EXPECT_THROW((void)GetAsciiGlyphBitmap(-1), std::out_of_range);
    EXPECT_THROW((void)GetAsciiGlyphBitmap(kAsciiGlyphRampLength), std::out_of_range);
    EXPECT_THROW((void)GetAsciiGlyphBitmap(kAsciiSolidGlyphIndex), std::out_of_range)
        << "the solid slot is not a character";
    EXPECT_NO_THROW((void)GetAsciiGlyphBitmap(0));
    EXPECT_NO_THROW((void)GetAsciiGlyphBitmap(kAsciiGlyphRampLength - 1));
}

#endif // CNA_CNAEXT
