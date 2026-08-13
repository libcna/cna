// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/Fna3d/Fna3dSurfaceFormats.hpp"

#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"

#include <cstdint>
#include <limits>

namespace CNA::Internal::Renderers::Fna3d
{
    namespace
    {
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        using Microsoft::Xna::Framework::Graphics::Texture;

        SurfaceFormat AsSurfaceFormat(int surfaceFormat)
        {
            return static_cast<SurfaceFormat>(surfaceFormat);
        }

        int CheckedByteCount(std::int64_t byteCount)
        {
            return byteCount > 0 && byteCount <= std::numeric_limits<int>::max()
                       ? static_cast<int>(byteCount)
                       : 0;
        }

        int BlockCountForExtent(int extent)
        {
            // `extent + 3` overflows for INT_MAX. Division and remainder express the same ceil
            // operation without ever exceeding the input range.
            return extent / 4 + (extent % 4 == 0 ? 0 : 1);
        }
    }

    bool IsBlockCompressedFormat(int surfaceFormat)
    {
        // GetBlockSizeSquaredEXT reports 16 (a 4x4 block) for every compressed format and 1 for
        // every linear one, so this needs no format list of its own.
        return Texture::GetBlockSizeSquaredEXT(AsSurfaceFormat(surfaceFormat)) != 1;
    }

    int FormatUnitByteCount(int surfaceFormat)
    {
        return Texture::GetFormatSizeEXT(AsSurfaceFormat(surfaceFormat));
    }

    int FormatRowByteCount(int surfaceFormat, int width)
    {
        if (width <= 0)
        {
            return 0;
        }
        const int unitBytes = FormatUnitByteCount(surfaceFormat);
        if (IsBlockCompressedFormat(surfaceFormat))
        {
            return CheckedByteCount(
                static_cast<std::int64_t>(BlockCountForExtent(width)) * unitBytes);
        }
        return CheckedByteCount(static_cast<std::int64_t>(width) * unitBytes);
    }

    int FormatRegionByteCount(int surfaceFormat, int width, int height)
    {
        if (width <= 0 || height <= 0)
        {
            return 0;
        }
        if (IsBlockCompressedFormat(surfaceFormat))
        {
            // Block rows are counted from the PADDED height: a 5-texel-tall level still occupies
            // two full block rows, and shorting the count by the partial tail block is exactly
            // the over-read this function exists to prevent.
            return CheckedByteCount(static_cast<std::int64_t>(BlockCountForExtent(width)) *
                                    BlockCountForExtent(height) *
                                    FormatUnitByteCount(surfaceFormat));
        }
        return CheckedByteCount(static_cast<std::int64_t>(width) * height *
                                FormatUnitByteCount(surfaceFormat));
    }

    bool IsValidTextureRegion2D(int levelWidth, int levelHeight, int x, int y, int width,
                                int height) noexcept
    {
        return levelWidth > 0 && levelHeight > 0 && x >= 0 && y >= 0 && width > 0 && height > 0 &&
               x <= levelWidth - width && y <= levelHeight - height;
    }

    bool IsValidTextureRegion2DForFormat(int surfaceFormat, int levelWidth, int levelHeight, int x,
                                         int y, int width, int height) noexcept
    {
        if (!IsValidTextureRegion2D(levelWidth, levelHeight, x, y, width, height))
        {
            return false;
        }
        try
        {
            if (!IsBlockCompressedFormat(surfaceFormat))
            {
                return true;
            }
        }
        catch (...)
        {
            return false;
        }

        const bool rightEdgeAligned = width % 4 == 0 || x == levelWidth - width;
        const bool bottomEdgeAligned = height % 4 == 0 || y == levelHeight - height;
        return x % 4 == 0 && y % 4 == 0 && rightEdgeAligned && bottomEdgeAligned;
    }

    bool IsValidTextureRegion3D(int levelWidth, int levelHeight, int levelDepth, int x, int y,
                                int z, int width, int height, int depth) noexcept
    {
        return IsValidTextureRegion2D(levelWidth, levelHeight, x, y, width, height) &&
               levelDepth > 0 && z >= 0 && depth > 0 && z <= levelDepth - depth;
    }
}
