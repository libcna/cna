// SPDX-License-Identifier: MS-PL
// Migrated from former ASCII renderer tasks ASCII-30..33 to AsciiPostProcessEffect's own internal
// quantizer -- pure function, no GraphicsDevice/window needed at all.
//
// Check A -- BlackWhite mode: a solid-black 8x8 block picks glyph index 0 (space, darkest);
//   a solid-white 8x8 block picks the last ramp index ('@', densest). Both use a fixed white
//   foreground and no background fill.
// Check B -- Color mode: a solid-red 8x8 block's foreground is the exact averaged color
//   (255,0,0,255); its background is a quarter-brightness version of that, and hasBackground
//   is true.
// Check C -- grid dimensions round up for a source size that isn't an exact multiple of the
//   cell size, and the resulting edge cell only averages the real remaining pixels (not
//   out-of-bounds garbage).
// Check D (ASCII-3) -- CNA_ASCII_MODE env-var parsing: unset/unrecognized default to Color,
//   case-insensitive BLACKWHITE/COLOR both parse correctly.
// Check E -- source alpha is ignored: transparent, half-alpha, and opaque pixels with identical
//   RGB produce the same glyph/colors, and emitted colors remain opaque.
// Check F -- null source pixels and non-positive source/cell dimensions are rejected with
//   std::invalid_argument instead of reaching division or pointer arithmetic.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "CNA/Internal/Graphics/Ascii/AsciiQuantizer.hpp"
#include "System/Environment.hpp"
#include "CNA/Internal/Graphics/Ascii/AsciiFontAtlas.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <vector>

using namespace CNA::Internal::Graphics::Ascii;

namespace
{
    int passCount = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount;
    }

    void FillBlock(std::vector<std::uint8_t>& rgba, int imgWidth, int x0, int y0, int w, int h,
                   std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255)
    {
        for (int y = y0; y < y0 + h; ++y)
        {
            for (int x = x0; x < x0 + w; ++x)
            {
                std::uint8_t* p = rgba.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(imgWidth) + static_cast<std::size_t>(x)) * 4;
                p[0] = r; p[1] = g; p[2] = b; p[3] = a;
            }
        }
    }

    template<typename Action>
    bool ThrowsInvalidArgument(Action&& action)
    {
        try { action(); }
        catch (const std::invalid_argument&) { return true; }
        catch (...) { return false; }
        return false;
    }
}

int main()
{
    // Check A: BlackWhite mode glyph selection at the two luminance extremes.
    {
        const int w = 16, h = 8;
        std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 0);
        FillBlock(rgba, w, 0, 0, 8, 8, 0, 0, 0);
        FillBlock(rgba, w, 8, 0, 8, 8, 255, 255, 255);

        AsciiGrid grid = QuantizeFrameToGrid(rgba.data(), w, h, 8, 8, AsciiQuantizeMode::BlackWhite);
        check(grid.columns == 2 && grid.rows == 1, "BlackWhite: grid is 2x1 for a 16x8 image with 8x8 cells");
        check(grid.At(0, 0).glyphIndex == 0, "BlackWhite: solid-black cell picks the darkest glyph (index 0)");
        check(grid.At(1, 0).glyphIndex == kAsciiGlyphRampLength - 1, "BlackWhite: solid-white cell picks the densest glyph (last index)");
        check(!grid.At(0, 0).hasBackground && !grid.At(1, 0).hasBackground, "BlackWhite: no background fill on either cell");
    }

    // Check B: Color mode foreground/background for a solid-red block.
    {
        const int w = 8, h = 8;
        std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 0);
        FillBlock(rgba, w, 0, 0, 8, 8, 255, 0, 0);

        AsciiGrid grid = QuantizeFrameToGrid(rgba.data(), w, h, 8, 8, AsciiQuantizeMode::Color);
        const AsciiCell& cell = grid.At(0, 0);
        check(cell.hasBackground, "Color: background fill is enabled");
        check(cell.foreground.getRProperty() == 255 && cell.foreground.getGProperty() == 0 && cell.foreground.getBProperty() == 0,
              "Color: foreground is the exact averaged color (pure red)");
        check(cell.background.getRProperty() == 255 / 4 && cell.background.getGProperty() == 0 && cell.background.getBProperty() == 0,
              "Color: background is a quarter-brightness version of the foreground");
    }

    // Check C: non-exact-multiple source size rounds the grid up and clamps the edge cell.
    {
        const int w = 20, h = 8; // 20/8 = 2.5 -> 3 columns; last column only has 4 real pixels wide
        std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 0);
        FillBlock(rgba, w, 0, 0, 20, 8, 255, 255, 255);

        AsciiGrid grid = QuantizeFrameToGrid(rgba.data(), w, h, 8, 8, AsciiQuantizeMode::BlackWhite);
        check(grid.columns == 3 && grid.rows == 1, "Grid rounds up to 3 columns for a 20px-wide image with 8px cells");
        check(grid.At(2, 0).glyphIndex == kAsciiGlyphRampLength - 1,
              "The clamped edge cell (4 real px wide) still reads as solid white, not garbage");
    }

    // Check D (ASCII-3): CNA_ASCII_MODE env-var parsing -- unset/unrecognized default to Color,
    // case-insensitive BLACKWHITE/COLOR both parse correctly.
    {
        System::Environment::SetEnvironmentVariable("CNA_ASCII_MODE", "");
        check(ParseAsciiModeFromEnvironment() == AsciiQuantizeMode::Color, "CNA_ASCII_MODE unset defaults to Color");

        System::Environment::SetEnvironmentVariable("CNA_ASCII_MODE", "blackwhite");
        check(ParseAsciiModeFromEnvironment() == AsciiQuantizeMode::BlackWhite, "CNA_ASCII_MODE=blackwhite (lowercase) parses to BlackWhite");

        System::Environment::SetEnvironmentVariable("CNA_ASCII_MODE", "COLOR");
        check(ParseAsciiModeFromEnvironment() == AsciiQuantizeMode::Color, "CNA_ASCII_MODE=COLOR (uppercase) parses to Color");

        System::Environment::SetEnvironmentVariable("CNA_ASCII_MODE", "bogus");
        check(ParseAsciiModeFromEnvironment() == AsciiQuantizeMode::Color, "CNA_ASCII_MODE=bogus (unrecognized) defaults to Color");

        System::Environment::SetEnvironmentVariable("CNA_ASCII_MODE", "");
    }

    // Check E: alpha does not influence quantization or the emitted opaque colors.
    {
        const std::uint8_t transparent[] = {80, 120, 160, 0};
        const std::uint8_t halfAlpha[] = {80, 120, 160, 127};
        const std::uint8_t opaque[] = {80, 120, 160, 255};
        const AsciiCell transparentCell =
            QuantizeFrameToGrid(transparent, 1, 1, 1, 1, AsciiQuantizeMode::Color).At(0, 0);
        const AsciiCell halfAlphaCell =
            QuantizeFrameToGrid(halfAlpha, 1, 1, 1, 1, AsciiQuantizeMode::Color).At(0, 0);
        const AsciiCell opaqueCell =
            QuantizeFrameToGrid(opaque, 1, 1, 1, 1, AsciiQuantizeMode::Color).At(0, 0);

        check(transparentCell.glyphIndex == halfAlphaCell.glyphIndex &&
                  halfAlphaCell.glyphIndex == opaqueCell.glyphIndex,
              "Alpha contract: transparent, half-alpha, and opaque pixels select the same glyph");
        check(transparentCell.foreground == halfAlphaCell.foreground &&
                  halfAlphaCell.foreground == opaqueCell.foreground &&
                  opaqueCell.foreground.getRProperty() == 80 &&
                  opaqueCell.foreground.getGProperty() == 120 &&
                  opaqueCell.foreground.getBProperty() == 160,
              "Alpha contract: source alpha does not change the averaged foreground RGB");
        check(transparentCell.foreground.getAProperty() == 255 &&
                  transparentCell.background.getAProperty() == 255 &&
                  halfAlphaCell.foreground.getAProperty() == 255 &&
                  halfAlphaCell.background.getAProperty() == 255,
              "Alpha contract: emitted Color-mode foreground and background remain opaque");
    }

    // Check F: invalid inputs fail deterministically before quantization.
    {
        const std::uint8_t pixel[] = {0, 0, 0, 255};
        check(ThrowsInvalidArgument([&] {
                  (void)QuantizeFrameToGrid(nullptr, 1, 1, 1, 1, AsciiQuantizeMode::Color);
              }),
              "Validation: null source pixels throw std::invalid_argument");
        check(ThrowsInvalidArgument([&] {
                  (void)QuantizeFrameToGrid(pixel, 0, 1, 1, 1, AsciiQuantizeMode::Color);
              }),
              "Validation: non-positive source width throws std::invalid_argument");
        check(ThrowsInvalidArgument([&] {
                  (void)QuantizeFrameToGrid(pixel, 1, -1, 1, 1, AsciiQuantizeMode::Color);
              }),
              "Validation: non-positive source height throws std::invalid_argument");
        check(ThrowsInvalidArgument([&] {
                  (void)QuantizeFrameToGrid(pixel, 1, 1, 0, 1, AsciiQuantizeMode::Color);
              }),
              "Validation: non-positive cell width throws std::invalid_argument");
        check(ThrowsInvalidArgument([&] {
                  (void)QuantizeFrameToGrid(pixel, 1, 1, 1, -1, AsciiQuantizeMode::Color);
              }),
              "Validation: non-positive cell height throws std::invalid_argument");
    }

    std::printf("=== %d/%d PASS ===\n", passCount, 21);
    return (passCount == 21) ? 0 : 1;
}
