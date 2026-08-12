// SPDX-License-Identifier: MS-PL

#include "TerminalFrameGrid.hpp"

#include "CNA/Platform/PlatformException.hpp"

#include <algorithm>

namespace CNA::Platform::Terminal {

    namespace {

        /// Rec. 601 luma. The same weighting the ASCII post-process effect uses, so the two agree
        /// on which parts of a picture are "bright" rather than each having their own opinion.
        int Luminance(const int red, const int green, const int blue)
        {
            return (red * 299 + green * 587 + blue * 114) / 1000;
        }

        void Validate(const SurfaceFrame& frame)
        {
            if (frame.pixels == nullptr)
            {
                throw PlatformException("TerminalPresenter::Present", "the frame has no pixels");
            }
            if (frame.width <= 0 || frame.height <= 0)
            {
                throw PlatformException("TerminalPresenter::Present",
                                        "the frame has a non-positive size");
            }
        }

    } // namespace

    const std::string& GlyphRamp()
    {
        static const std::string ramp = " .:-=+*#%@";
        return ramp;
    }

    void TerminalGrid::Reset(const int newColumns, const int newRows)
    {
        columns = newColumns;
        rows = newRows;
        cells.assign(static_cast<std::size_t>(std::max(0, newColumns)) *
                         static_cast<std::size_t>(std::max(0, newRows)),
                     TerminalCell{});
    }

    void QuantizeInto(const SurfaceFrame& frame, const int sourceX, const int sourceY,
                      const int sourceWidth, const int sourceHeight, TerminalGrid& grid,
                      const int destinationColumn, const int destinationRow,
                      const int destinationColumns, const int destinationRows)
    {
        Validate(frame);
        if (sourceWidth <= 0 || sourceHeight <= 0 || destinationColumns <= 0 || destinationRows <= 0)
        {
            throw PlatformException("TerminalPresenter::Present",
                                    "a degenerate source or destination rectangle");
        }

        const int stride = frame.strideBytes > 0 ? frame.strideBytes : frame.width * 4;
        const std::string& ramp = GlyphRamp();
        const int lastGlyph = static_cast<int>(ramp.size()) - 1;

        for (int row = 0; row < destinationRows; ++row)
        {
            // Half-open source spans computed from the cell's edges rather than from a rounded
            // step, so every source pixel lands in exactly one cell and none is dropped when the
            // sizes do not divide evenly.
            const int top = sourceY + (row * sourceHeight) / destinationRows;
            const int bottom = sourceY + ((row + 1) * sourceHeight) / destinationRows;

            for (int column = 0; column < destinationColumns; ++column)
            {
                const int left = sourceX + (column * sourceWidth) / destinationColumns;
                const int right = sourceX + ((column + 1) * sourceWidth) / destinationColumns;

                long long sumRed = 0, sumGreen = 0, sumBlue = 0;
                long long counted = 0;

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
        Validate(frame);
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
