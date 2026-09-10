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

// plans/plan_xna_sample_xnb_sweep.md XNASWEEP-221: a bitmap may put bytes between its header and
// its pixels, and the decoder under `ImageLoader` reads such a file from the wrong place. CNA
// closes the gap before handing the bytes over, so the image is the one the file describes.
//
// The measurement that found it is `spikes/bmp-offset-spike`, which reproduces the corruption in
// the vendored decoder alone: the same eight-by-eight image with four filler bytes decodes
// differently from the same image with none. SAMPLE-141's `riemerstexture.bmp` carries 864 bytes
// of colour table written for 256-colour displays, and was 55,886 bytes wrong against a reference
// the genuine XNA pipeline built.
TEST(ImageLoaderTests, ABitmapWhosePixelsDoNotFollowItsHeaderDecodesToTheSameImage)
{
    constexpr int kWidth = 8;
    constexpr int kHeight = 8;
    constexpr std::size_t kStride = static_cast<std::size_t>(kWidth) * 3u;   // already a multiple of 4
    const auto write32 = [](std::vector<std::uint8_t>& into, const std::size_t at,
                            const std::uint32_t value)
    {
        for (std::size_t step = 0; step < 4u; ++step)
        {
            into[at + step] = static_cast<std::uint8_t>((value >> (8u * step)) & 0xFFu);
        }
    };
    // One 24-bit bitmap, written twice: once with its pixels straight after the header and once
    // with `gap` bytes of filler in between. Nothing else about the two files differs.
    const auto bitmap = [&](const std::size_t gap)
    {
        std::vector<std::uint8_t> file(14u + 40u + gap + kStride * kHeight, 0u);
        file[0] = 'B';
        file[1] = 'M';
        write32(file, 2u, static_cast<std::uint32_t>(file.size()));
        write32(file, 10u, static_cast<std::uint32_t>(14u + 40u + gap));
        write32(file, 14u, 40u);
        write32(file, 18u, static_cast<std::uint32_t>(kWidth));
        write32(file, 22u, static_cast<std::uint32_t>(kHeight));
        file[26] = 1u;                                   // planes
        file[28] = 24u;                                  // bits per pixel
        std::fill(file.begin() + 14 + 40,
                  file.begin() + static_cast<std::ptrdiff_t>(14u + 40u + gap),
                  static_cast<std::uint8_t>(0xAAu));
        for (int y = 0; y < kHeight; ++y)
        {
            for (int x = 0; x < kWidth; ++x)
            {
                const std::size_t at = 14u + 40u + gap + static_cast<std::size_t>(y) * kStride +
                                       static_cast<std::size_t>(x) * 3u;
                file[at] = static_cast<std::uint8_t>(x * 16 + 8);        // blue
                file[at + 1u] = static_cast<std::uint8_t>(y * 16 + 16);  // green
                file[at + 2u] = static_cast<std::uint8_t>((x + y) * 8 + 24);
            }
        }
        return file;
    };

    const std::vector<std::uint8_t> straight = bitmap(0u);
    const auto truth = ImageLoader::LoadFromMemory(straight.data(), straight.size());
    ASSERT_EQ(truth.width, kWidth);
    ASSERT_EQ(truth.height, kHeight);
    // A filler run of any length, including one that is not a whole pixel and one that is exactly
    // a row: the corruption this closes is not a clean displacement and none of these may show it.
    for (const std::size_t gap : {std::size_t{4}, std::size_t{16}, std::size_t{64},
                                  std::size_t{256}, kStride, std::size_t{864}})
    {
        const std::vector<std::uint8_t> spaced = bitmap(gap);
        const auto got = ImageLoader::LoadFromMemory(spaced.data(), spaced.size());
        EXPECT_EQ(got.width, kWidth) << "gap " << gap;
        EXPECT_EQ(got.height, kHeight) << "gap " << gap;
        EXPECT_EQ(got.pixels, truth.pixels) << "gap " << gap << ": the filler moved the image";
    }
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
