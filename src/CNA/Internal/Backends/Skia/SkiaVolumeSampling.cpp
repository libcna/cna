#include "CNA/Internal/Backends/Skia/SkiaVolumeSampling.hpp"

#include <cmath>
#include <cstring>

namespace CNA::Internal::Backends::Skia
{
    VolumeAtlasLayoutEXT ComputeVolumeAtlasLayoutEXT(int width, int height, int depth) noexcept
    {
        VolumeAtlasLayoutEXT layout;
        if (width <= 0 || height <= 0 || depth <= 0)
            return layout;

        const int cols = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(depth))));
        const int rows = (depth + cols - 1) / cols;
        layout.cols = cols;
        layout.rows = rows;
        layout.tileWidth = width + 2;
        layout.tileHeight = height + 2;
        layout.atlasWidth = layout.tileWidth * cols;
        layout.atlasHeight = layout.tileHeight * rows;
        return layout;
    }

    std::vector<std::uint8_t> PackVolumeAtlasEXT(
        int width, int height, int depth, const std::uint8_t* voxels, std::size_t voxelsByteCount)
    {
        const VolumeAtlasLayoutEXT layout = ComputeVolumeAtlasLayoutEXT(width, height, depth);
        if (layout.cols == 0 || !voxels)
            return {};
        const std::size_t sliceBytes =
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
        if (voxelsByteCount < sliceBytes * static_cast<std::size_t>(depth))
            return {};

        std::vector<std::uint8_t> atlas(
            static_cast<std::size_t>(layout.atlasWidth) * static_cast<std::size_t>(layout.atlasHeight)
                * 4u,
            0u);
        const auto atlasPixel = [&](int x, int y) -> std::uint8_t* {
            return atlas.data()
                + (static_cast<std::size_t>(y) * layout.atlasWidth + x) * 4u;
        };
        const auto slicePixel = [&](int slice, int x, int y) -> const std::uint8_t* {
            return voxels + static_cast<std::size_t>(slice) * sliceBytes
                + (static_cast<std::size_t>(y) * width + x) * 4u;
        };

        for (int slice = 0; slice < depth; ++slice)
        {
            const int col = slice % layout.cols;
            const int row = slice / layout.cols;
            const int originX = col * layout.tileWidth + 1;
            const int originY = row * layout.tileHeight + 1;

            // Interior: exact copy of this slice's own w*h texels.
            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                    std::memcpy(atlasPixel(originX + x, originY + y), slicePixel(slice, x, y), 4u);
            }

            // 1-texel border replicated from this tile's own edge texels (never another slice's),
            // so a bilinear sample anywhere inside [originX, originX+width) x [originY,
            // originY+height) can never read outside this tile regardless of address mode.
            for (int x = 0; x < width; ++x)
            {
                std::memcpy(atlasPixel(originX + x, originY - 1), slicePixel(slice, x, 0), 4u);
                std::memcpy(atlasPixel(originX + x, originY + height), slicePixel(slice, x, height - 1), 4u);
            }
            for (int y = 0; y < height; ++y)
            {
                std::memcpy(atlasPixel(originX - 1, originY + y), slicePixel(slice, 0, y), 4u);
                std::memcpy(atlasPixel(originX + width, originY + y), slicePixel(slice, width - 1, y), 4u);
            }
            std::memcpy(atlasPixel(originX - 1, originY - 1), slicePixel(slice, 0, 0), 4u);
            std::memcpy(atlasPixel(originX + width, originY - 1), slicePixel(slice, width - 1, 0), 4u);
            std::memcpy(atlasPixel(originX - 1, originY + height), slicePixel(slice, 0, height - 1), 4u);
            std::memcpy(atlasPixel(originX + width, originY + height),
                       slicePixel(slice, width - 1, height - 1), 4u);
        }
        return atlas;
    }
} // namespace CNA::Internal::Backends::Skia
