// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Graphics/Ascii/AsciiQuantizer.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Internal/Graphics/Ascii/AsciiFontAtlas.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>

namespace CNA::Internal::Graphics::Ascii
{
    using Microsoft::Xna::Framework::Color;

    AsciiQuantizeMode ParseAsciiModeFromEnvironment()
    {
        const char* raw = std::getenv("CNA_ASCII_MODE");
        if (raw == nullptr)
            return AsciiQuantizeMode::Color;

        std::string value(raw);
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (value == "blackwhite") return AsciiQuantizeMode::BlackWhite;
        if (value == "color") return AsciiQuantizeMode::Color;
        return AsciiQuantizeMode::Color;
    }

    namespace
    {
        int PickGlyphIndex(double luminance0to255)
        {
            const double t = luminance0to255 / 255.0;
            const int index = static_cast<int>(t * (kAsciiGlyphRampLength - 1) + 0.5);
            return std::clamp(index, 0, kAsciiGlyphRampLength - 1);
        }
    }

    AsciiGrid QuantizeFrameToGrid(const std::uint8_t* rgba, int srcWidth, int srcHeight,
                                  int cellWidth, int cellHeight, AsciiQuantizeMode mode)
    {
        AsciiGrid grid;
        grid.columns = (srcWidth + cellWidth - 1) / cellWidth;
        grid.rows = (srcHeight + cellHeight - 1) / cellHeight;
        grid.cells.resize(static_cast<std::size_t>(grid.columns) * static_cast<std::size_t>(grid.rows));

        for (int row = 0; row < grid.rows; ++row)
        {
            const int y0 = row * cellHeight;
            const int y1 = std::min(y0 + cellHeight, srcHeight);
            for (int col = 0; col < grid.columns; ++col)
            {
                const int x0 = col * cellWidth;
                const int x1 = std::min(x0 + cellWidth, srcWidth);

                std::uint64_t sumR = 0, sumG = 0, sumB = 0;
                int count = 0;
                for (int y = y0; y < y1; ++y)
                {
                    const std::uint8_t* rowPtr = rgba + (static_cast<std::size_t>(y) * static_cast<std::size_t>(srcWidth) + static_cast<std::size_t>(x0)) * 4;
                    for (int x = x0; x < x1; ++x)
                    {
                        sumR += rowPtr[0];
                        sumG += rowPtr[1];
                        sumB += rowPtr[2];
                        rowPtr += 4;
                        ++count;
                    }
                }

                const auto avgR = static_cast<std::uint8_t>(count ? sumR / static_cast<std::uint64_t>(count) : 0);
                const auto avgG = static_cast<std::uint8_t>(count ? sumG / static_cast<std::uint64_t>(count) : 0);
                const auto avgB = static_cast<std::uint8_t>(count ? sumB / static_cast<std::uint64_t>(count) : 0);
                const double luminance = 0.299 * avgR + 0.587 * avgG + 0.114 * avgB;

                AsciiCell& cell = grid.cells[static_cast<std::size_t>(row) * static_cast<std::size_t>(grid.columns) + static_cast<std::size_t>(col)];
                cell.glyphIndex = PickGlyphIndex(luminance);

                if (mode == AsciiQuantizeMode::Color)
                {
                    cell.foreground = Color(avgR, avgG, avgB, static_cast<std::uint8_t>(255));
                    cell.background = Color(static_cast<std::uint8_t>(avgR / 4), static_cast<std::uint8_t>(avgG / 4),
                                            static_cast<std::uint8_t>(avgB / 4), static_cast<std::uint8_t>(255));
                    cell.hasBackground = true;
                }
                else
                {
                    cell.foreground = Color(static_cast<std::uint8_t>(255), static_cast<std::uint8_t>(255),
                                            static_cast<std::uint8_t>(255), static_cast<std::uint8_t>(255));
                    cell.background = Color(static_cast<std::uint8_t>(0), static_cast<std::uint8_t>(0),
                                            static_cast<std::uint8_t>(0), static_cast<std::uint8_t>(0));
                    cell.hasBackground = false;
                }
            }
        }

        return grid;
    }
}

#endif // CNA_CNAEXT
