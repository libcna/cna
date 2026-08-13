// SPDX-License-Identifier: MS-PL

#include "TerminalFrameGrid.hpp"

#include "../Common/SurfaceFrameValidation.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace CNA::Platform::Terminal {

    namespace {

        /// Rec. 601 luma. The same weighting the ASCII post-process effect uses, so the two agree
        /// on which parts of a picture are "bright" rather than each having their own opinion.
        int Luminance(const int red, const int green, const int blue)
        {
            return (red * 299 + green * 587 + blue * 114) / 1000;
        }

    } // namespace

    const std::string& GlyphRamp()
    {
        static const std::string ramp = " .:-=+*#%@";
        return ramp;
    }

    void TerminalGrid::Reset(const int newColumns, const int newRows)
    {
        const std::size_t safeColumns = static_cast<std::size_t>(std::max(0, newColumns));
        const std::size_t safeRows = static_cast<std::size_t>(std::max(0, newRows));
        if (safeColumns != 0 &&
            safeRows > static_cast<std::size_t>(std::numeric_limits<int>::max()) / safeColumns)
        {
            throw PlatformException("TerminalGrid::Reset", "the cell count exceeds INT_MAX");
        }

        // Allocate first so a failed resize preserves the old, internally consistent grid.
        std::vector<TerminalCell> replacement(safeColumns * safeRows, TerminalCell{});
        cells.swap(replacement);
        columns = newColumns;
        rows = newRows;
    }

    void QuantizeInto(const SurfaceFrame& frame, const int sourceX, const int sourceY,
                      const int sourceWidth, const int sourceHeight, TerminalGrid& grid,
                      const int destinationColumn, const int destinationRow,
                      const int destinationColumns, const int destinationRows)
    {
        const int stride = Common::ValidateSurfaceFrame(frame, "TerminalPresenter::Present");
        if (sourceWidth <= 0 || sourceHeight <= 0 || destinationColumns <= 0 || destinationRows <= 0)
        {
            throw PlatformException("TerminalPresenter::Present",
                                    "a degenerate source or destination rectangle");
        }
        if (sourceX < 0 || sourceY < 0 || sourceX > frame.width - sourceWidth ||
            sourceY > frame.height - sourceHeight)
        {
            throw PlatformException("TerminalPresenter::Present",
                                    "the source rectangle lies outside the frame");
        }

        const std::string& ramp = GlyphRamp();
        const int lastGlyph = static_cast<int>(ramp.size()) - 1;

        for (int row = 0; row < destinationRows; ++row)
        {
            // Half-open source spans computed from the cell's edges rather than from a rounded
            // step, so every source pixel lands in exactly one cell and none is dropped when the
            // sizes do not divide evenly.
            const int top = sourceY + static_cast<int>(
                (static_cast<std::int64_t>(row) * sourceHeight) / destinationRows);
            const int bottom = sourceY + static_cast<int>(
                (static_cast<std::int64_t>(row + 1) * sourceHeight) / destinationRows);

            for (int column = 0; column < destinationColumns; ++column)
            {
                const int left = sourceX + static_cast<int>(
                    (static_cast<std::int64_t>(column) * sourceWidth) / destinationColumns);
                const int right = sourceX + static_cast<int>(
                    (static_cast<std::int64_t>(column + 1) * sourceWidth) / destinationColumns);

                std::uint64_t sumRed = 0, sumGreen = 0, sumBlue = 0;
                std::uint64_t counted = 0;

                for (int y = top; y < std::max(bottom, top + 1) && y < frame.height; ++y)
                {
                    if (y < 0)
                    {
                        continue;
                    }
                    const std::uint8_t* scanline = frame.pixels + static_cast<std::size_t>(y) *
                                                                      static_cast<std::size_t>(stride);
                    for (int x = left; x < std::max(right, left + 1) && x < frame.width; ++x)
                    {
                        if (x < 0)
                        {
                            continue;
                        }
                        const std::uint8_t* pixel = scanline + static_cast<std::size_t>(x) * 4u;
                        sumRed += pixel[0];
                        sumGreen += pixel[1];
                        sumBlue += pixel[2];
                        ++counted;
                    }
                }

                TerminalCell cell;
                if (counted > 0)
                {
                    cell.red = static_cast<std::uint8_t>(sumRed / counted);
                    cell.green = static_cast<std::uint8_t>(sumGreen / counted);
                    cell.blue = static_cast<std::uint8_t>(sumBlue / counted);
                    const int luminance = Luminance(cell.red, cell.green, cell.blue);
                    cell.glyph = ramp[static_cast<std::size_t>((luminance * lastGlyph) / 255)];
                }

                const int gridColumn = destinationColumn + column;
                const int gridRow = destinationRow + row;
                if (gridColumn >= 0 && gridColumn < grid.columns && gridRow >= 0 &&
                    gridRow < grid.rows)
                {
                    grid.cells[static_cast<std::size_t>(gridRow) *
                                   static_cast<std::size_t>(grid.columns) +
                               static_cast<std::size_t>(gridColumn)] = cell;
                }
            }
        }
    }

    TerminalGrid QuantizeToGrid(const SurfaceFrame& frame, const int columns, const int rows)
    {
        (void)Common::ValidateSurfaceFrame(frame, "TerminalPresenter::Present");
        if (columns <= 0 || rows <= 0)
        {
            throw PlatformException("TerminalPresenter::Present", "a non-positive grid size");
        }

        TerminalGrid grid;
        grid.Reset(columns, rows);
        QuantizeInto(frame, 0, 0, frame.width, frame.height, grid, 0, 0, columns, rows);
        return grid;
    }

} // namespace CNA::Platform::Terminal
