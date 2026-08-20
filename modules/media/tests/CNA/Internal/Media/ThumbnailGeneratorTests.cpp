// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#include "CNA/Internal/Graphics/ImageLoader.hpp"
#include "CNA/Internal/Media/ThumbnailGenerator.hpp"

using CNA::Internal::Graphics::ImageData;
using CNA::Internal::Media::ThumbnailGenerator;

namespace
{
    ImageData MakeSolid(int w, int h, uint8_t r, uint8_t g, uint8_t b)
    {
        ImageData d;
        d.width = w;
        d.height = h;
        d.pixels.reserve(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u);
        for (int i = 0; i < w * h; ++i)
        {
            d.pixels.push_back(r);
            d.pixels.push_back(g);
            d.pixels.push_back(b);
            d.pixels.push_back(255);
        }
        return d;
    }
}

// plans/plan_media.md MEDIA-209/210: Album::GetThumbnail and Picture::GetThumbnail used to just call
// GetAlbumArt()/GetImage(), returning the FULL-SIZE image -- GetThumbnail was a synonym, not a
// thumbnail. These assert real downscaling, deterministically and without any fixture.

TEST(ThumbnailGeneratorTest, DownscalesAnOversizedImageToTheMaxEdge)
{
    const ImageData source = MakeSolid(400, 300, 10, 20, 30);
    const ImageData thumb = ThumbnailGenerator::Downscale(source);

    EXPECT_EQ(std::max(thumb.width, thumb.height), ThumbnailGenerator::MaxEdge);
    EXPECT_LT(thumb.width, source.width);
    EXPECT_LT(thumb.height, source.height);
    EXPECT_EQ(thumb.pixels.size(),
              static_cast<std::size_t>(thumb.width) * static_cast<std::size_t>(thumb.height) * 4u);
}

TEST(ThumbnailGeneratorTest, PreservesAspectRatio)
{
    const ImageData source = MakeSolid(400, 200, 0, 0, 0); // 2:1
    const ImageData thumb = ThumbnailGenerator::Downscale(source);

    const double srcRatio = 400.0 / 200.0;
    const double dstRatio = static_cast<double>(thumb.width) / static_cast<double>(thumb.height);
    EXPECT_NEAR(dstRatio, srcRatio, 0.05) << thumb.width << "x" << thumb.height;
}

// A thumbnail larger than its source would be pointless and lossy, so small images pass through.
TEST(ThumbnailGeneratorTest, NeverUpscalesAnAlreadySmallImage)
{
    const ImageData source = MakeSolid(32, 24, 1, 2, 3);
    const ImageData thumb = ThumbnailGenerator::Downscale(source);

    EXPECT_EQ(thumb.width, 32);
    EXPECT_EQ(thumb.height, 24);
}

// Box filter: averaging a solid-colour image must reproduce that exact colour, proving pixels are
// genuinely averaged rather than zeroed or left uninitialised.
TEST(ThumbnailGeneratorTest, BoxFilterPreservesASolidColour)
{
    const ImageData source = MakeSolid(400, 400, 200, 100, 50);
    const ImageData thumb = ThumbnailGenerator::Downscale(source);

    ASSERT_GE(thumb.pixels.size(), 4u);
    for (std::size_t i = 0; i + 3 < thumb.pixels.size(); i += 4)
    {
        ASSERT_EQ(thumb.pixels[i + 0], 200) << "pixel " << i / 4;
        ASSERT_EQ(thumb.pixels[i + 1], 100);
        ASSERT_EQ(thumb.pixels[i + 2], 50);
        ASSERT_EQ(thumb.pixels[i + 3], 255);
    }
}

// End-to-end through the real PNG encoder, against a genuinely oversized fixture (400x300, well
// above MaxEdge, so a pass-through implementation cannot satisfy this).
TEST(ThumbnailGeneratorTest, CreatesARealPngThumbnailSmallerThanTheSource)
{
    std::vector<uint8_t> png;
    ASSERT_TRUE(ThumbnailGenerator::CreatePngThumbnail(
        "tests/assets/media/thumbnails/large_400x300.png", png));
    ASSERT_FALSE(png.empty());

    // Must be a real PNG (8-byte signature), not raw pixels.
    ASSERT_GE(png.size(), 8u);
    const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    for (int i = 0; i < 8; ++i) EXPECT_EQ(png[static_cast<std::size_t>(i)], sig[i]) << "byte " << i;

    // And decoding it back must yield the downscaled dimensions, not 400x300.
    const ImageData decoded =
        CNA::Internal::Graphics::ImageLoader::LoadFromMemory(png.data(), png.size());
    EXPECT_EQ(std::max(decoded.width, decoded.height), ThumbnailGenerator::MaxEdge);
    EXPECT_LT(decoded.width, 400);
    EXPECT_LT(decoded.height, 300);
}

TEST(ThumbnailGeneratorTest, ReturnsFalseForAnUnreadableSourceInsteadOfThrowing)
{
    std::vector<uint8_t> png;
    EXPECT_NO_THROW({
        EXPECT_FALSE(ThumbnailGenerator::CreatePngThumbnail("does/not/exist.png", png));
    });
    EXPECT_TRUE(png.empty()) << "failed generation must leave the output untouched";
}
