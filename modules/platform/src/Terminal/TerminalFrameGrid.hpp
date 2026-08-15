// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatformSurfacePresenter.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace CNA::Platform::Terminal {

    /**
     * @brief The glyph ramp, darkest first.
     *
     * Ten steps from empty to solid. The same ramp `AsciiPostProcessEffect` uses, so a frame shown
     * in a terminal and the same frame put through the post-process effect read alike.
     */
    [[nodiscard]] const std::string& GlyphRamp();

    /** @brief One character cell: which glyph, and what colour to draw it in. */
    struct TerminalCell
    {
        /** @brief The character to draw. */
        char glyph = ' ';
        /** @brief Red channel of the cell's averaged colour. */
        std::uint8_t red = 0;
        /** @brief Green channel of the cell's averaged colour. */
        std::uint8_t green = 0;
        /** @brief Blue channel of the cell's averaged colour. */
        std::uint8_t blue = 0;

        /** @brief Compares two cells. @param other The cell to compare with. @return True if identical. */
        [[nodiscard]] bool operator==(const TerminalCell& other) const
        {
            return glyph == other.glyph && red == other.red && green == other.green &&
                   blue == other.blue;
        }
    };

    /** @brief A quantised character grid, row-major. */
    struct TerminalGrid
    {
        /** @brief Grid width in character cells. */
        int columns = 0;
        /** @brief Grid height in character cells. */
        int rows = 0;
        /** @brief `columns * rows` cells, row-major. */
        std::vector<TerminalCell> cells;

        /**
         * @brief Reads one cell.
         *
         * @param column Zero-based column.
         * @param row Zero-based row.
         * @return The cell at that position.
         */
        [[nodiscard]] const TerminalCell& At(int column, int row) const
        {
            return cells[static_cast<std::size_t>(row) * static_cast<std::size_t>(columns) +
                         static_cast<std::size_t>(column)];
        }

        /**
         * @brief Resizes the grid, filling it with blank cells.
         *
         * @param newColumns The new width in cells.
         * @param newRows The new height in cells.
         */
        void Reset(int newColumns, int newRows);
    };

    /**
     * @brief Quantises a finished RGBA frame into a character grid.
     *
     * Each destination cell box-averages the source pixels that fall inside it, and the average's
     * luminance picks a glyph from the ramp: dark regions get sparse glyphs, bright regions get
     * dense ones. Edge cells average only the pixels that really exist, never past the buffer.
     *
     * ### Why this is not `QuantizeFrameToGrid()` from `modules/graphics-ext`
     *
     * `plan_platform.md` PLAT-132 proposed reusing that function, and it cannot be: it lives in
     * `cna_graphics_ext`, which reaches `cna_platform` through `cna_graphics_core` → `cna_input`,
     * so calling into it from here would close a dependency cycle. The requirements diverged as
     * well — the terminal needs a four-rung colour ladder (PLAT-134) and cell-by-cell comparison
     * for damage tracking (PLAT-133), where `AsciiQuantizeMode` offers two modes and `AsciiCell`
     * carries a background colour and an XNA `Color` this layer has no business depending on.
     *
     * ### Aspect ratio is the caller's problem, deliberately
     *
     * A character cell is roughly twice as tall as it is wide, so a grid filled edge to edge
     * shows a frame squashed vertically by half. This function maps whatever source rectangle it
     * is given onto whatever grid rectangle it is given; choosing those rectangles so the picture
     * keeps its shape is the presenter's job, where the scale mode is known.
     *
     * @param frame The finished pixels. Must have non-null pixels and a positive size.
     * @param columns The grid width in cells; must be positive.
     * @param rows The grid height in cells; must be positive.
     * @return The quantised grid.
     * @throws PlatformException If @p frame is malformed or the grid size is not positive.
     */
    [[nodiscard]] TerminalGrid QuantizeToGrid(const SurfaceFrame& frame, int columns, int rows);

    /**
     * @brief Quantises a sub-rectangle of a frame into a sub-rectangle of an existing grid.
     *
     * The form the presenter actually uses: letterboxing means writing the picture into the
     * middle of a grid whose remaining cells stay blank, and cropping means reading from the
     * middle of a frame. Both are one call rather than a copy.
     *
     * @param frame The finished pixels.
     * @param sourceX Left edge of the source rectangle, in pixels.
     * @param sourceY Top edge of the source rectangle, in pixels.
     * @param sourceWidth Width of the source rectangle, in pixels.
     * @param sourceHeight Height of the source rectangle, in pixels.
     * @param grid The grid to write into; must already be sized.
     * @param destinationColumn Left edge of the destination rectangle, in cells.
     * @param destinationRow Top edge of the destination rectangle, in cells.
     * @param destinationColumns Width of the destination rectangle, in cells.
     * @param destinationRows Height of the destination rectangle, in cells.
     * @throws PlatformException If @p frame is malformed or either rectangle is degenerate.
     */
    void QuantizeInto(const SurfaceFrame& frame, int sourceX, int sourceY, int sourceWidth,
                      int sourceHeight, TerminalGrid& grid, int destinationColumn,
                      int destinationRow, int destinationColumns, int destinationRows);

} // namespace CNA::Platform::Terminal
