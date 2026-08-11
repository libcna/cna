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

#include <stdexcept>

#include "CNA/Graphics/AsciiPostProcessEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

using CNA::Graphics::AsciiPostProcessEffect;
using CNA::Graphics::AsciiQuantizeMode;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

TEST(AsciiPostProcessEffectTest, DefaultCellSizeIsEightByEight)
{
    GraphicsDevice gd;
    AsciiPostProcessEffect fx(gd);

    int width = 0, height = 0;
    fx.getCellSize(width, height);
    EXPECT_EQ(width, 8);
    EXPECT_EQ(height, 8);
}

TEST(AsciiPostProcessEffectTest, SetCellSizeRoundTrips)
{
    GraphicsDevice gd;
    AsciiPostProcessEffect fx(gd);

    fx.setCellSize(16, 4);
    int width = 0, height = 0;
    fx.getCellSize(width, height);
    EXPECT_EQ(width, 16);
    EXPECT_EQ(height, 4);
}

TEST(AsciiPostProcessEffectTest, SetCellSizeRejectsNonPositiveValues)
{
    GraphicsDevice gd;
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
    GraphicsDevice gd;
    AsciiPostProcessEffect fx(gd);

    fx.setQuantizeMode(AsciiQuantizeMode::BlackWhite);
    EXPECT_EQ(fx.getQuantizeMode(), AsciiQuantizeMode::BlackWhite);

    fx.setQuantizeMode(AsciiQuantizeMode::Color);
    EXPECT_EQ(fx.getQuantizeMode(), AsciiQuantizeMode::Color);
}

TEST(AsciiPostProcessEffectTest, GetLastGridDimensionsStartsAtZero)
{
    GraphicsDevice gd;
    AsciiPostProcessEffect fx(gd);

    int columns = -1, rows = -1;
    fx.GetLastGridDimensions(columns, rows);
    EXPECT_EQ(columns, 0);
    EXPECT_EQ(rows, 0);
}

TEST(AsciiPostProcessEffectTest, ConstructingMultipleIndependentEffectsDoesNotThrow)
{
    GraphicsDevice gd;
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

#endif // CNA_CNAEXT
