#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace CNA::Internal::Backends::Glide
{
    /** Returns the full XNA-compatible mip count for positive two-dimensional dimensions. */
    [[nodiscard]] inline int GlideMipLevelCountForDimensions(int width, int height)
    {
        if (width <= 0 || height <= 0)
        {
            throw std::runtime_error("GLIDE mip-level count received invalid texture dimensions");
        }
        int count = 1;
        while (width > 1 || height > 1)
        {
            width = std::max(1, width / 2);
            height = std::max(1, height / 2);
            ++count;
        }
        return count;
    }

    /** Converts one canonical RGBA8 texel to Glide's native ARGB4444 storage format. */
    [[nodiscard]] inline std::uint16_t RgbaToGlideArgb4444(const std::uint8_t* rgba)
    {
        return static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(rgba[3] >> 4) << 12) |
            (static_cast<std::uint16_t>(rgba[0] >> 4) << 8) |
            (static_cast<std::uint16_t>(rgba[1] >> 4) << 4) |
            static_cast<std::uint16_t>(rgba[2] >> 4));
    }

    /** Maps an integer texel coordinate with CNA's Wrap, Clamp, or Mirror address semantics. */
    [[nodiscard]] inline int AddressGlideTextureTexel(int coordinate, int dimension, int addressMode)
    {
        if (dimension <= 0)
        {
            throw std::runtime_error("GLIDE texture address conversion received an invalid dimension");
        }
        switch (addressMode)
        {
            case 0: // TextureAddressMode::Wrap
            {
                int result = coordinate % dimension;
                return result < 0 ? result + dimension : result;
            }
            case 1: // TextureAddressMode::Clamp
                return std::clamp(coordinate, 0, dimension - 1);
            case 2: // TextureAddressMode::Mirror
            {
                const int period = dimension * 2;
                int reflected = coordinate % period;
                if (reflected < 0)
                {
                    reflected += period;
                }
                return reflected < dimension ? reflected : period - 1 - reflected;
            }
            default:
                throw std::runtime_error("GLIDE backend received an unknown TextureAddressMode value");
        }
    }

    /** Maps a finite normalized texture coordinate through CNA's continuous address semantics. */
    [[nodiscard]] inline float MapGlideTextureCoordinateToUnit(float coordinate, int addressMode)
    {
        constexpr float kMaximumAddressRepeatIntervals = 4096.0f;
        if (!std::isfinite(coordinate) || coordinate < -kMaximumAddressRepeatIntervals ||
            coordinate > kMaximumAddressRepeatIntervals)
        {
            throw std::runtime_error("GLIDE 3D texture addressing spans too many Wrap/Mirror repeat intervals");
        }
        switch (addressMode)
        {
            case 1: // TextureAddressMode::Clamp
                return std::clamp(coordinate, 0.0f, 1.0f);

            case 0: // TextureAddressMode::Wrap
                return coordinate - std::floor(coordinate);

            case 2: // TextureAddressMode::Mirror
            {
                const float interval = std::floor(coordinate);
                const float fraction = coordinate - interval;
                return static_cast<int>(interval) % 2 == 0 ? fraction : 1.0f - fraction;
            }

            default:
                throw std::runtime_error("GLIDE backend received an unknown TextureAddressMode value");
        }
    }

    /**
     * Expands a logical RGBA8 mip level to the corresponding power-of-two physical source using
     * the active address mode. This is used for both level zero and explicit lower mip levels,
     * so tiled textures retain consistent padding and gutters after a per-level upload.
     */
    [[nodiscard]] inline std::vector<std::uint16_t> BuildAddressedGlideArgb4444Mip(
        const std::uint8_t* rgba, int sourceWidth, int sourceHeight,
        int physicalWidth, int physicalHeight, int addressU, int addressV)
    {
        if (rgba == nullptr || sourceWidth <= 0 || sourceHeight <= 0 ||
            physicalWidth < sourceWidth || physicalHeight < sourceHeight)
        {
            throw std::runtime_error("GLIDE mip source dimensions are invalid");
        }
        std::vector<std::uint16_t> result(static_cast<std::size_t>(physicalWidth) * physicalHeight);
        for (int y = 0; y < physicalHeight; ++y)
        {
            const int sourceY = AddressGlideTextureTexel(y, sourceHeight, addressV);
            for (int x = 0; x < physicalWidth; ++x)
            {
                const int sourceX = AddressGlideTextureTexel(x, sourceWidth, addressU);
                result[static_cast<std::size_t>(y) * physicalWidth + x] = RgbaToGlideArgb4444(
                    rgba + (static_cast<std::size_t>(sourceY) * sourceWidth + sourceX) * 4u);
            }
        }
        return result;
    }
} // namespace CNA::Internal::Backends::Glide
