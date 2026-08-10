// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/Fna3d/Fna3dSurfaceFormats.hpp"

#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"

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
            return ((width + 3) / 4) * unitBytes;
        }
        return width * unitBytes;
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
            return ((width + 3) / 4) * ((height + 3) / 4) * FormatUnitByteCount(surfaceFormat);
        }
        return width * height * FormatUnitByteCount(surfaceFormat);
    }
}
