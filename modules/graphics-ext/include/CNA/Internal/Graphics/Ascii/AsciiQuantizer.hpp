// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/AsciiQuantizeMode.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace CNA::Internal::Graphics::Ascii
{
    using CNA::Graphics::AsciiQuantizeMode;

    /// One quantized grid cell: which glyph to draw (index into kAsciiGlyphRamp, see
    /// AsciiFontAtlas.hpp) and what colors to draw it with.
    struct AsciiCell
    {
        int glyphIndex = 0;
        Microsoft::Xna::Framework::Color foreground{255, 255, 255, 255};
        Microsoft::Xna::Framework::Color background{0, 0, 0, 0};
        /// Whether background should actually be painted -- false in BlackWhite mode.
        bool hasBackground = false;
    };

    /// A quantized glyph/color grid: columns x rows AsciiCell entries, row-major.
    struct AsciiGrid
    {
        int columns = 0;
        int rows = 0;
        std::vector<AsciiCell> cells;

        [[nodiscard]] const AsciiCell& At(int col, int row) const
        {
            return cells[static_cast<std::size_t>(row) * static_cast<std::size_t>(columns) + static_cast<std::size_t>(col)];
        }
    };

    /**
     * @brief Quantizes an RGBA8 source image into a glyph/color grid.
     *
     * Each cell averages the block of source pixels it represents to get a representative color,
     * derives that color's luminance, and picks a glyph from kAsciiGlyphRamp by luminance rank
     * (darkest source region -> emptiest glyph, brightest -> densest). When @p srcWidth/@p
     * srcHeight are not exact multiples of @p cellWidth/@p cellHeight, the grid rounds up and the
     * edge cells average only the real remaining pixels, never out-of-bounds memory.
     * Source alpha is intentionally ignored: RGB alone selects the glyph and colors. Foregrounds
     * and Color-mode backgrounds are opaque; the public effect supplies opaque black behind
     * BlackWhite glyphs. It therefore stylizes the complete destination area rather than
     * alpha-compositing the source image.
     *
     * @param rgba       Source pixels, row-major, 4 bytes/pixel (R,G,B,A), srcWidth*srcHeight*4 bytes.
     * @param srcWidth   Source image width in pixels. Must be positive.
     * @param srcHeight  Source image height in pixels. Must be positive.
     * @param cellWidth  Source pixels per grid cell, horizontally. Must be positive.
     * @param cellHeight Source pixels per grid cell, vertically. Must be positive.
     * @param mode       BlackWhite or Color.
     * @return The quantized grid; columns = ceil(srcWidth/cellWidth), rows = ceil(srcHeight/cellHeight).
     * @throws std::invalid_argument if @p rgba is null or any source/cell dimension is not
     * positive.
     */
    AsciiGrid QuantizeFrameToGrid(const std::uint8_t* rgba, int srcWidth, int srcHeight,
                                  int cellWidth, int cellHeight, AsciiQuantizeMode mode);

    /**
     * @brief Parses `CNA_ASCII_MODE` (`BLACKWHITE`/`COLOR`, case-insensitive) at construction
     * time, with the behavior established by former ASCII renderer task ASCII-3. Unset or
     * unrecognized values default to `Color`.
     *
     * @return The parsed mode.
     */
    [[nodiscard]] AsciiQuantizeMode ParseAsciiModeFromEnvironment();
}

#endif // CNA_CNAEXT
