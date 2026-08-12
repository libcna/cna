// SPDX-License-Identifier: MS-PL

#include "TerminalAnsiWriter.hpp"

#include <array>

namespace CNA::Platform::Terminal {

    namespace {

        /// The sixteen ANSI colours as most terminals actually render them, with their SGR
        /// foreground parameters. Values are the widely-shared xterm defaults; a terminal with a
        /// custom scheme will differ, which is inherent to the rung and not worth pretending away.
        struct AnsiColour
        {
            std::uint8_t red, green, blue;
            int parameter;
        };

        constexpr std::array<AnsiColour, 16> kAnsi16 = {{
            {0, 0, 0, 30},       {205, 0, 0, 31},     {0, 205, 0, 32},     {205, 205, 0, 33},
            {0, 0, 238, 34},     {205, 0, 205, 35},   {0, 205, 205, 36},   {229, 229, 229, 37},
            {127, 127, 127, 90}, {255, 0, 0, 91},     {0, 255, 0, 92},     {255, 255, 0, 93},
            {92, 92, 255, 94},   {255, 0, 255, 95},   {0, 255, 255, 96},   {255, 255, 255, 97},
        }};

        int SquaredDistance(const int r1, const int g1, const int b1, const int r2, const int g2,
                            const int b2)
        {
            const int dr = r1 - r2, dg = g1 - g2, db = b1 - b2;
            return dr * dr + dg * dg + db * db;
        }

        void AppendInt(std::string& output, int value)
        {
            char digits[12];
            int length = 0;
            if (value == 0)
            {
                output += '0';
                return;
            }
            while (value > 0 && length < 12)
            {
                digits[length++] = static_cast<char>('0' + (value % 10));
                value /= 10;
            }
            while (length > 0)
            {
                output += digits[--length];
            }
        }

    } // namespace

    int NearestIndexed256(const std::uint8_t red, const std::uint8_t green, const std::uint8_t blue)
    {
        // The 6x6x6 colour cube occupies 16..231 with levels at 0, 95, 135, 175, 215, 255.
        static constexpr std::array<int, 6> kLevels = {0, 95, 135, 175, 215, 255};

        const auto nearestLevel = [](const int value) {
            int best = 0;
            int bestDistance = 1 << 30;
            for (int i = 0; i < 6; ++i)
            {
                const int distance = (value - kLevels[static_cast<std::size_t>(i)]) *
                                     (value - kLevels[static_cast<std::size_t>(i)]);
                if (distance < bestDistance)
                {
                    bestDistance = distance;
                    best = i;
                }
            }
            return best;
        };

        const int cubeRed = nearestLevel(red);
        const int cubeGreen = nearestLevel(green);
        const int cubeBlue = nearestLevel(blue);
        const int cubeIndex = 16 + 36 * cubeRed + 6 * cubeGreen + cubeBlue;
        const int cubeError =
            SquaredDistance(red, green, blue, kLevels[static_cast<std::size_t>(cubeRed)],
                            kLevels[static_cast<std::size_t>(cubeGreen)],
                            kLevels[static_cast<std::size_t>(cubeBlue)]);

        // The 24-step grey ramp at 232..255 is much finer than the cube's grey diagonal, so a
        // near-grey colour that took the cube would come out visibly tinted.
        const int greyLevel = (red * 299 + green * 587 + blue * 114) / 1000;
        int greyStep = (greyLevel - 8) / 10;
        greyStep = greyStep < 0 ? 0 : (greyStep > 23 ? 23 : greyStep);
        const int greyValue = 8 + greyStep * 10;
        const int greyError = SquaredDistance(red, green, blue, greyValue, greyValue, greyValue);

        return greyError < cubeError ? 232 + greyStep : cubeIndex;
    }

    int NearestAnsi16(const std::uint8_t red, const std::uint8_t green, const std::uint8_t blue)
    {
        int best = kAnsi16[0].parameter;
        int bestDistance = 1 << 30;
        for (const AnsiColour& candidate : kAnsi16)
        {
            const int distance =
                SquaredDistance(red, green, blue, candidate.red, candidate.green, candidate.blue);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = candidate.parameter;
            }
        }
        return best;
    }

    TerminalAnsiWriter::TerminalAnsiWriter(const TerminalColourDepth depth) : depth_(depth) {}

    void TerminalAnsiWriter::ForgetTerminalState() { hasCurrentColour_ = false; }

    void TerminalAnsiWriter::AppendColour(const std::uint8_t red, const std::uint8_t green,
                                          const std::uint8_t blue, std::string& output)
    {
        if (depth_ == TerminalColourDepth::Monochrome)
        {
            return;  // the glyph carries the whole picture; there is nothing to say about colour
        }

        if (hasCurrentColour_ && red == currentRed_ && green == currentGreen_ && blue == currentBlue_)
        {
            // The single most valuable line here. Restating an unchanged colour costs up to 19
            // bytes per cell, which is most of a frame.
            return;
        }

        switch (depth_)
        {
            case TerminalColourDepth::TrueColour:
                output += "\x1b[38;2;";
                AppendInt(output, red);
                output += ';';
                AppendInt(output, green);
                output += ';';
                AppendInt(output, blue);
                output += 'm';
                break;

            case TerminalColourDepth::Indexed256:
                output += "\x1b[38;5;";
                AppendInt(output, NearestIndexed256(red, green, blue));
                output += 'm';
                break;

            case TerminalColourDepth::Ansi16:
                output += "\x1b[";
                AppendInt(output, NearestAnsi16(red, green, blue));
                output += 'm';
                break;

            case TerminalColourDepth::Monochrome:
                break;  // handled above; listed so the switch stays exhaustive
        }

        hasCurrentColour_ = true;
        currentRed_ = red;
        currentGreen_ = green;
        currentBlue_ = blue;
    }

    int TerminalAnsiWriter::WriteChangedCells(const TerminalGrid& previous,
                                              const TerminalGrid& current, std::string& output)
    {
        if (previous.columns != current.columns || previous.rows != current.rows)
        {
            // Nothing on screen corresponds to the new grid's coordinates, so a diff would be
            // comparing unrelated cells. A resize is exactly when a full redraw is correct.
            WriteFullFrame(current, output);
            return current.columns * current.rows;
        }

        int redrawn = 0;
        for (int row = 0; row < current.rows; ++row)
        {
            int column = 0;
            while (column < current.columns)
            {
                if (current.At(column, row) == previous.At(column, row))
                {
                    ++column;
                    continue;
                }

                // Position once for the whole run. A cursor move costs about as much as six
                // glyphs, so positioning each changed cell separately would give back most of
                // what the diff saves on a frame with scattered small changes.
                output += "\x1b[";
                AppendInt(output, row + 1);
                output += ';';
                AppendInt(output, column + 1);
                output += 'H';

                // The cursor moved, so the colour the terminal is in is still known -- SGR state
                // survives cursor motion -- but the run starts wherever it starts, so the first
                // cell may well need a colour change and AppendColour decides that as usual.
                while (column < current.columns &&
                       !(current.At(column, row) == previous.At(column, row)))
                {
                    const TerminalCell& cell = current.At(column, row);
                    AppendColour(cell.red, cell.green, cell.blue, output);
                    output += cell.glyph;
                    ++redrawn;
                    ++column;
                }
            }
        }
        return redrawn;
    }

    void TerminalAnsiWriter::WriteFullFrame(const TerminalGrid& grid, std::string& output)
    {
        if (grid.columns <= 0 || grid.rows <= 0)
        {
            return;
        }

        // Home the cursor rather than clearing: a clear makes the terminal blank the screen and
        // then repaint it, which flickers visibly at any frame rate worth having.
        output += "\x1b[H";

        for (int row = 0; row < grid.rows; ++row)
        {
            if (row > 0)
            {
                // Explicit cursor positioning rather than a newline. A newline at the bottom of
                // the screen scrolls, which would walk the picture upwards one row per frame.
                output += "\x1b[";
                AppendInt(output, row + 1);
                output += ";1H";
            }
            for (int column = 0; column < grid.columns; ++column)
            {
                const TerminalCell& cell = grid.At(column, row);
                AppendColour(cell.red, cell.green, cell.blue, output);
                output += cell.glyph;
            }
        }
    }

} // namespace CNA::Platform::Terminal
