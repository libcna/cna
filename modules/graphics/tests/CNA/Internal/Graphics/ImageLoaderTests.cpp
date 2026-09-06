// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/Graphics/ImageLoader.hpp"

#include "CNA/Internal/Graphics/DibBitmap.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {

using CNA::Internal::Graphics::ImageLoader;
using CNA::Internal::Graphics::IsDeviceIndependentBitmap;
using CNA::Internal::Graphics::WithBitmapFileHeader;

TEST(ImageLoaderTests, BilinearResizeSamplesPixelCentres)
{
    // The 1x1 destination samples the exact centre of this 2x2 image, so every channel is the
    // rounded average of all four source texels. This pins the platform-neutral scaler rather
    // than merely checking dimensions or a solid colour, both of which let nearest-neighbour and
    // broken row-stride implementations pass unnoticed.
    constexpr std::array<std::uint8_t, 16> pixels{
          0,   0,  20,   0, 100,   0,  40, 100,
          0, 100,  60, 200, 100, 100,  80, 255,
    };

    const auto resized = ImageLoader::ResizeRgba(pixels.data(), 2, 2, 1, 1, false);

    EXPECT_EQ(resized.width, 1);
    EXPECT_EQ(resized.height, 1);
    EXPECT_EQ(resized.pixels, (std::vector<std::uint8_t>{50, 50, 50, 139}));
}

TEST(ImageLoaderTests, RejectsMalformedBuffersAndDimensions)
{
    constexpr std::array<std::uint8_t, 4> pixel{1, 2, 3, 4};

    EXPECT_THROW((void)ImageLoader::LoadFromMemory(nullptr, 1), std::invalid_argument);
    EXPECT_THROW((void)ImageLoader::LoadFromMemory(pixel.data(), 0), std::invalid_argument);
    EXPECT_THROW(
        (void)ImageLoader::ResizeRgba(nullptr, 1, 1, 1, 1, false), std::invalid_argument);
    EXPECT_THROW(
        (void)ImageLoader::ResizeRgba(pixel.data(), 1, 1, 0, 1, false),
        std::invalid_argument);
}

// plans/plan_xnapipeline_parity.md XNAPP-021: a `.dib` is a bitmap that lost its file header, and
// the shared decoder is where it is put back -- so a content build and the runtime answer the same
// pixels for the same bytes.
TEST(ImageLoaderTests, DecodesADeviceIndependentBitmapThatHasNoFileHeader)
{
    // BITMAPINFOHEADER, 2x2, 32bpp. A DIB stores its rows bottom-up and its channels BGRA, so
    // these four texels are the bottom row (red, white) followed by the top row (blue, green).
    constexpr std::array<std::uint8_t, 56> dib{
        40, 0, 0, 0,  2, 0, 0, 0,  2, 0, 0, 0,  1, 0, 32, 0,
         0, 0, 0, 0, 16, 0, 0, 0,  0, 0, 0, 0,  0, 0,  0, 0,
         0, 0, 0, 0,  0, 0, 0, 0,
        0x00, 0x00, 0xFF, 0xFF,   0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0x00, 0x00, 0xFF,   0x00, 0xFF, 0x00, 0xFF,
    };

    ASSERT_TRUE(IsDeviceIndependentBitmap(dib));

    const auto image = ImageLoader::LoadFromMemory(dib.data(), dib.size());

    EXPECT_EQ(image.width, 2);
    EXPECT_EQ(image.height, 2);
    // Rows come back top-down and the channels as RGBA: blue, green, then red, white.
    EXPECT_EQ(image.pixels, (std::vector<std::uint8_t>{
                                0x00, 0x00, 0xFF, 0xFF, 0x00, 0xFF, 0x00, 0xFF,
                                0xFF, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}));
}

TEST(ImageLoaderTests, TheDibRecogniserClaimsNothingItShouldNot)
{
    // Already a bitmap file, too short to hold a header, and a leading dword that is not one of
    // the three header sizes Windows defines.
    constexpr std::array<std::uint8_t, 40> bitmapFile{'B', 'M'};
    constexpr std::array<std::uint8_t, 8> tooShort{40, 0, 0, 0, 2, 0, 0, 0};
    std::array<std::uint8_t, 40> png{0x89, 'P', 'N', 'G'};

    EXPECT_FALSE(IsDeviceIndependentBitmap(bitmapFile));
    EXPECT_FALSE(IsDeviceIndependentBitmap(tooShort));
    EXPECT_FALSE(IsDeviceIndependentBitmap(png));
    EXPECT_THROW((void)WithBitmapFileHeader(png), std::invalid_argument);
}

TEST(ImageLoaderTests, TheSynthesisedFileHeaderStepsOverAPaletteAndOverBitfieldMasks)
{
    // Eight bits per pixel with a full 256-entry colour table: pixels start 14 + 40 + 1024 in.
    std::vector<std::uint8_t> paletted(40u + 1024u + 4u, 0u);
    paletted[0] = 40u;
    paletted[14] = 8u;
    EXPECT_EQ(WithBitmapFileHeader(paletted)[10], static_cast<std::uint8_t>((14u + 40u + 1024u) & 0xFFu));
    EXPECT_EQ(WithBitmapFileHeader(paletted)[11], static_cast<std::uint8_t>((14u + 40u + 1024u) >> 8));

    // BI_BITFIELDS on a v3 header puts three masks between the header and the pixels.
    std::vector<std::uint8_t> masked(40u + 12u + 16u, 0u);
    masked[0] = 40u;
    masked[14] = 32u;
    masked[16] = 3u;
    EXPECT_EQ(WithBitmapFileHeader(masked)[10], 14u + 40u + 12u);

    // A plain 32-bit v3 bitmap has neither.
    std::vector<std::uint8_t> plain(40u + 16u, 0u);
    plain[0] = 40u;
    plain[14] = 32u;
    EXPECT_EQ(WithBitmapFileHeader(plain)[10], 14u + 40u);
}

} // namespace
