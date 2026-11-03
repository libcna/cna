// SPDX-License-Identifier: MS-PL
// AsciiPostProcessEffect is a CNAEXT CNA extension (no XNA/FNA precedent), the direct successor to
// the former public ASCII graphics-renderer identity. Like CRTEffectTests.cpp/DepthEffectTests.cpp,
// these tests exercise the structural contract (parameter round-trip, validation, deterministic
// initial state) against a default-constructed GraphicsDevice with no real renderer -- pixel-level
// quantized-output correctness is covered separately by the real-window
// AsciiPostProcessEffect_Pixel ctest (modules/graphics-ext/examples/ascii_posteffect_pixel_test.cpp)
// and the renderer-free Ascii_Quantizer ctest (ascii_quantizer_test.cpp).

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include <cstdlib>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "CNA/Graphics/AsciiPostProcessEffect.hpp"
#include "CNA/Internal/Graphics/Ascii/AsciiFontAtlas.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "RetroGraphicsTestSupport.hpp"

using CNA::Graphics::AsciiPostProcessEffect;
using CNA::Graphics::AsciiQuantizeMode;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::Texture2D;

TEST(AsciiPostProcessEffectTest, DefaultCellSizeIsEightByEight)
{
    CnaTest::RetroGraphics::HiDefDevice gd;
    AsciiPostProcessEffect fx(gd);

    int width = 0, height = 0;
    fx.getCellSize(width, height);
    EXPECT_EQ(width, 8);
    EXPECT_EQ(height, 8);
}

TEST(AsciiPostProcessEffectTest, SetCellSizeRoundTrips)
{
    CnaTest::RetroGraphics::HiDefDevice gd;
    AsciiPostProcessEffect fx(gd);

    fx.setCellSize(16, 4);
    int width = 0, height = 0;
    fx.getCellSize(width, height);
    EXPECT_EQ(width, 16);
    EXPECT_EQ(height, 4);
}

TEST(AsciiPostProcessEffectTest, SetCellSizeRejectsNonPositiveValues)
{
    CnaTest::RetroGraphics::HiDefDevice gd;
    AsciiPostProcessEffect fx(gd);

    EXPECT_THROW(fx.setCellSize(0, 8), std::invalid_argument);
    EXPECT_THROW(fx.setCellSize(8, 0), std::invalid_argument);
    EXPECT_THROW(fx.setCellSize(-1, 8), std::invalid_argument);
    EXPECT_THROW(fx.setCellSize(8, -1), std::invalid_argument);

    // A rejected setCellSize() call must not mutate the existing state.
    int width = 0, height = 0;
    fx.getCellSize(width, height);
    EXPECT_EQ(width, 8);
    EXPECT_EQ(height, 8);
}

TEST(AsciiPostProcessEffectTest, SetQuantizeModeRoundTripsForBothModes)
{
    CnaTest::RetroGraphics::HiDefDevice gd;
    AsciiPostProcessEffect fx(gd);

    fx.setQuantizeMode(AsciiQuantizeMode::BlackWhite);
    EXPECT_EQ(fx.getQuantizeMode(), AsciiQuantizeMode::BlackWhite);

    fx.setQuantizeMode(AsciiQuantizeMode::Color);
    EXPECT_EQ(fx.getQuantizeMode(), AsciiQuantizeMode::Color);
}

TEST(AsciiPostProcessEffectTest, GetLastGridDimensionsStartsAtZero)
{
    CnaTest::RetroGraphics::HiDefDevice gd;
    AsciiPostProcessEffect fx(gd);

    int columns = -1, rows = -1;
    fx.GetLastGridDimensions(columns, rows);
    EXPECT_EQ(columns, 0);
    EXPECT_EQ(rows, 0);
}

TEST(AsciiPostProcessEffectTest, ConstructingMultipleIndependentEffectsDoesNotThrow)
{
    CnaTest::RetroGraphics::HiDefDevice gd;
    auto exercise = [&gd]()
    {
        AsciiPostProcessEffect a(gd);
        AsciiPostProcessEffect b(gd);
        a.setCellSize(4, 4);
        b.setCellSize(32, 32);
        int aw = 0, ah = 0, bw = 0, bh = 0;
        a.getCellSize(aw, ah);
        b.getCellSize(bw, bh);
        EXPECT_EQ(aw, 4);
        EXPECT_EQ(bw, 32);
    };
    EXPECT_NO_THROW(exercise());
}

namespace
{
    /// Sets or clears CNA_ASCII_MODE for one scope and restores the previous value.
    class AsciiModeVariable
    {
    public:
        AsciiModeVariable()
        {
            if (const char* previous = std::getenv("CNA_ASCII_MODE")) previous_ = previous;
        }

        ~AsciiModeVariable()
        {
            if (previous_) Set(previous_->c_str());
            else Unset();
        }

        AsciiModeVariable(const AsciiModeVariable&) = delete;
        AsciiModeVariable& operator=(const AsciiModeVariable&) = delete;

        static void Set(const char* value)
        {
#if defined(_WIN32)
            _putenv_s("CNA_ASCII_MODE", value);
#else
            ::setenv("CNA_ASCII_MODE", value, 1);
#endif
        }

        static void Unset()
        {
#if defined(_WIN32)
            _putenv_s("CNA_ASCII_MODE", "");
#else
            ::unsetenv("CNA_ASCII_MODE");
#endif
        }

    private:
        std::optional<std::string> previous_;
    };

    /// Fills @p source with @p left in its left half and @p right in its right half.
    void FillSplit(Texture2D& source, const Color& left, const Color& right)
    {
        const int width = source.getWidthProperty();
        const int height = source.getHeightProperty();
        std::vector<Color> pixels(static_cast<std::size_t>(width * height), left);
        for (int y = 0; y < height; ++y)
            for (int x = width / 2; x < width; ++x)
                pixels[static_cast<std::size_t>(y * width + x)] = right;
        source.SetData(pixels.data(), static_cast<int>(pixels.size()));
    }
}

TEST(AsciiFontAtlasTest, TheSpriteFontMapsEachRampCharacterToItsOwnAtlasCell)
{
    // The atlas is one row of 8x8 cells, glyph g at x = 8g; the eleventh cell (x = 80) is the
    // solid background block, which is not a character. Each glyph advances 8 pixels.
    CnaTest::RetroGraphics::HiDefDevice gd;
    namespace Ascii = CNA::Internal::Graphics::Ascii;
    const auto font = Ascii::BuildAsciiFontAtlas(gd);
    const auto& characters = font.getCharactersProperty();
    const auto& bounds = font.getGlyphBoundsEXT();
    ASSERT_EQ(characters.size(), 10U);
    ASSERT_EQ(bounds.size(), 10U);
    for (int g = 0; g < 10; ++g)
    {
        EXPECT_EQ(characters[static_cast<std::size_t>(g)], static_cast<SharpRuntime::charcs>(Ascii::kAsciiGlyphRamp[g]));
        EXPECT_EQ(bounds[static_cast<std::size_t>(g)], Rectangle(8 * g, 0, 8, 8)) << "glyph " << g;
    }
    EXPECT_EQ(font.getLineSpacingProperty(), 8);
    EXPECT_EQ(font.getSpacingProperty(), 0.0f);
    const auto size = font.MeasureString("@@@");
    EXPECT_EQ(size.X, 24.0f);
    EXPECT_EQ(size.Y, 8.0f);
}

TEST(AsciiPostProcessEffectTest, ARejectedCellSizeKeepsBothPreviousDimensions)
{
    // docs/ascii-post-process-effect.md: nonpositive dimensions throw "without changing the
    // previous size" -- including the half of the pair that was valid.
    CnaTest::RetroGraphics::HiDefDevice gd;
    AsciiPostProcessEffect fx(gd);
    fx.setCellSize(16, 4);
    EXPECT_THROW(fx.setCellSize(12, 0), std::invalid_argument);
    EXPECT_THROW(fx.setCellSize(0, 12), std::invalid_argument);
    EXPECT_THROW(fx.setCellSize(12, -3), std::invalid_argument);
    EXPECT_THROW(fx.setCellSize(std::numeric_limits<int>::min(), 12), std::invalid_argument);
    int width = 0, height = 0;
    fx.getCellSize(width, height);
    EXPECT_EQ(width, 16);
    EXPECT_EQ(height, 4);

    fx.setCellSize(1, 1);
    fx.getCellSize(width, height);
    EXPECT_EQ(width, 1);
    EXPECT_EQ(height, 1);
}

TEST(AsciiPostProcessEffectTest, TheEnvironmentChoosesTheModeOnlyAtConstruction)
{
    // "At construction, CNA_ASCII_MODE selects COLOR or BLACKWHITE ... Later environment changes
    // do not mutate an existing effect."
    CnaTest::RetroGraphics::HiDefDevice gd;
    const AsciiModeVariable restore;
    AsciiModeVariable::Set("BlackWhite");
    AsciiPostProcessEffect blackWhite(gd);
    EXPECT_EQ(blackWhite.getQuantizeMode(), AsciiQuantizeMode::BlackWhite);

    AsciiModeVariable::Set("color");
    EXPECT_EQ(blackWhite.getQuantizeMode(), AsciiQuantizeMode::BlackWhite);
    AsciiPostProcessEffect colour(gd);
    EXPECT_EQ(colour.getQuantizeMode(), AsciiQuantizeMode::Color);

    AsciiModeVariable::Unset();
    AsciiPostProcessEffect unset(gd);
    EXPECT_EQ(unset.getQuantizeMode(), AsciiQuantizeMode::Color);
}

TEST(AsciiPostProcessEffectTest, DrawReportsTheRoundedUpGridOfItsSource)
{
    // columns = ceil(20 / cellWidth), rows = ceil(9 / cellHeight).
    CnaTest::RetroGraphics::HiDefDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);
    Texture2D source(gd, 20, 9);
    FillSplit(source, Color::Black, Color::White);
    RenderTarget2D destination(gd, 40, 18);
    AsciiPostProcessEffect fx(gd);
    const struct
    {
        int cellWidth, cellHeight, columns, rows;
    } table[] = {{8, 8, 3, 2}, {7, 3, 3, 3}, {3, 7, 7, 2}, {64, 64, 1, 1}, {1, 9, 20, 1}};
    for (const auto& row : table)
    {
        fx.setCellSize(row.cellWidth, row.cellHeight);
        gd.SetRenderTarget(&destination);
        fx.Draw(source, Rectangle(0, 0, 40, 18));
        gd.SetRenderTarget(nullptr);
        int columns = 0, rows = 0;
        fx.GetLastGridDimensions(columns, rows);
        EXPECT_EQ(columns, row.columns) << row.cellWidth << "x" << row.cellHeight;
        EXPECT_EQ(rows, row.rows) << row.cellWidth << "x" << row.cellHeight;
    }
}

TEST(AsciiPostProcessEffectTest, EachCellDrawsItsOwnGlyphIntoItsOwnDestinationBlock)
{
    // A 16x8 source, black on the left and white on the right, 8x8 cells, BlackWhite, drawn 1:1
    // into a 16x8 target that starts magenta (so an undrawn pixel shows). Left cell: glyph 0, the
    // space -- nothing but its opaque black background. Right cell: the densest glyph, white on
    // black, exactly as its 8x8 bitmap reads (one byte per row, most significant bit leftmost).
    CnaTest::RetroGraphics::HiDefDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);
    namespace Ascii = CNA::Internal::Graphics::Ascii;
    Texture2D source(gd, 16, 8);
    FillSplit(source, Color::Black, Color::White);
    RenderTarget2D destination(gd, 16, 8);
    gd.SetRenderTarget(&destination);
    gd.Clear(Color(255, 0, 255, 255));
    AsciiPostProcessEffect fx(gd);
    fx.setQuantizeMode(AsciiQuantizeMode::BlackWhite);
    fx.Draw(source, Rectangle(0, 0, 16, 8));
    gd.SetRenderTarget(nullptr);

    std::vector<Color> pixels(16 * 8, Color::Black);
    destination.GetData(pixels.data(), static_cast<int>(pixels.size()));
    const auto densest = Ascii::GetAsciiGlyphBitmap(Ascii::kAsciiGlyphRampLength - 1);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 16; ++x)
        {
            const bool lit = x >= 8 && (densest[static_cast<std::size_t>(y)] & (0x80U >> (x - 8))) != 0;
            const Color expected = lit ? Color(255, 255, 255, 255) : Color(0, 0, 0, 255);
            EXPECT_EQ(pixels[static_cast<std::size_t>(y * 16 + x)].getPackedValueProperty(),
                      expected.getPackedValueProperty())
                << "pixel " << x << "," << y;
        }
}

#endif // CNA_CNAEXT
