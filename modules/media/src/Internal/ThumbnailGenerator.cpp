// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Media/ThumbnailGenerator.hpp"

#include <algorithm>
#include <cmath>
#include <exception>

#include "CNA/Internal/Graphics/ImageLoader.hpp"

namespace CNA::Internal::Media
{
    using CNA::Internal::Graphics::ImageData;

    ImageData ThumbnailGenerator::Downscale(const ImageData& source)
    {
        if (source.width <= 0 || source.height <= 0 || source.pixels.empty())
        {
            return source;
        }
        // Never upscale: a "thumbnail" bigger than its source is pointless and only adds blur.
        if (source.width <= MaxEdge && source.height <= MaxEdge)
        {
            return source;
        }

        const double scale = static_cast<double>(MaxEdge) /
                              static_cast<double>(std::max(source.width, source.height));
        const int dstW = std::max(1, static_cast<int>(std::lround(source.width * scale)));
        const int dstH = std::max(1, static_cast<int>(std::lround(source.height * scale)));

        ImageData out;
        out.width = dstW;
        out.height = dstH;
        out.mipLevels = 1;
        out.pixels.assign(static_cast<std::size_t>(dstW) * static_cast<std::size_t>(dstH) * 4u, 0u);

        // Box filter: average every source pixel mapping to each destination pixel. Cheaper and
        // less aliased than nearest-neighbour, and needs no new dependency.
        for (int y = 0; y < dstH; ++y)
        {
            const int sy0 = static_cast<int>(static_cast<double>(y)     * source.height / dstH);
            const int sy1 = std::max(sy0 + 1,
                             static_cast<int>(static_cast<double>(y + 1) * source.height / dstH));
            for (int x = 0; x < dstW; ++x)
            {
                const int sx0 = static_cast<int>(static_cast<double>(x)     * source.width / dstW);
                const int sx1 = std::max(sx0 + 1,
                                 static_cast<int>(static_cast<double>(x + 1) * source.width / dstW));

                uint32_t acc[4] = {0, 0, 0, 0};
                uint32_t n = 0;
                for (int sy = sy0; sy < std::min(sy1, source.height); ++sy)
                {
                    for (int sx = sx0; sx < std::min(sx1, source.width); ++sx)
                    {
                        const std::size_t idx =
                            (static_cast<std::size_t>(sy) * static_cast<std::size_t>(source.width) +
                             static_cast<std::size_t>(sx)) * 4u;
                        if (idx + 3 >= source.pixels.size()) continue;
                        for (int c = 0; c < 4; ++c) acc[c] += source.pixels[idx + static_cast<std::size_t>(c)];
                        ++n;
                    }
                }
                if (n == 0) continue;

                const std::size_t dst =
                    (static_cast<std::size_t>(y) * static_cast<std::size_t>(dstW) +
                     static_cast<std::size_t>(x)) * 4u;
                for (int c = 0; c < 4; ++c)
                {
                    out.pixels[dst + static_cast<std::size_t>(c)] =
                        static_cast<uint8_t>(acc[static_cast<std::size_t>(c)] / n);
                }
            }
        }
        return out;
    }

    bool ThumbnailGenerator::CreatePngThumbnail(const std::string& path,
                                                 std::vector<uint8_t>& outPng)
    {
        ImageData source;
        try
        {
            source = Graphics::ImageLoader::Load(path);
        }
        catch (const std::exception&)
        {
            return false; // unreadable/unsupported source -- caller falls back to the full image
        }
        if (source.width <= 0 || source.height <= 0 || source.pixels.empty())
        {
            return false;
        }

        // Already small enough: report "no thumbnail produced" so the caller serves the original
        // bytes. Re-encoding an in-spec image to PNG would be lossy work for no benefit -- and it
        // keeps GetThumbnail() byte-identical to GetImage() for small pictures, which is both
        // cheaper and the behavior existing callers already rely on.
        if (source.width <= MaxEdge && source.height <= MaxEdge)
        {
            return false;
        }

        const ImageData scaled = Downscale(source);

        // Encode straight into memory rather than a temp file, so GetThumbnail() has no filesystem
        // side effects.
        try
        {
            outPng = Graphics::ImageLoader::EncodePng(
                scaled.pixels.data(), scaled.width, scaled.height,
                scaled.width, scaled.height);
        }
        catch (const std::exception&)
        {
            outPng.clear();
            return false;
        }
        return !outPng.empty();
    }
}
