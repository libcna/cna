// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#include <climits>
#include <cstdint>
#include <vector>

#include "CNA/Internal/Renderers/Metal/MetalTextureTransfer.hpp"

using namespace CNA::Internal::Renderers::Metal;

TEST(MetalTextureTransfer, MipCountsIncludeEveryRelevantDimension)
{
    EXPECT_EQ(MetalMipLevelCount(1, 1, false), 1);
    EXPECT_EQ(MetalMipLevelCount(1, 1, true), 1);
    EXPECT_EQ(MetalMipLevelCount(8, 1, true), 4);
    EXPECT_EQ(MetalMipLevelCount(1, 8, true), 4);
    EXPECT_EQ(MetalMipLevelCount(0, 8, true), 0);

    std::vector<std::uint8_t> volumeBytes(32);
    MetalTextureTransferLayout layout{};
    EXPECT_EQ(MetalVolumeMipLevelCount(1, 1, 8, true), 4);
    EXPECT_EQ(MetalVolumeMipLevelCount(1, 2, 7, true), 3);
    EXPECT_EQ(MetalVolumeMipLevelCount(8, 1, 1, false), 1);
    EXPECT_EQ(MetalVolumeMipLevelCount(1, 1, 0, true), 0);

    const int levelsForOneByOneByEight = MetalVolumeMipLevelCount(1, 1, 8, true);
    EXPECT_TRUE(TryPrepareMetalTextureTransfer(
        1, 1, 8, levelsForOneByOneByEight, 0, 0, 0, 0, 1, 1, 8,
        volumeBytes.data(), 32, MetalTransferLengthRule::AtLeastTightBytes, 1, layout));
    EXPECT_TRUE(TryPrepareMetalTextureTransfer(
        1, 1, 8, levelsForOneByOneByEight, 3, 0, 0, 0, 1, 1, 1,
        volumeBytes.data(), 4, MetalTransferLengthRule::AtLeastTightBytes, 1, layout));
    EXPECT_FALSE(TryPrepareMetalTextureTransfer(
        1, 1, 8, levelsForOneByOneByEight, 4, 0, 0, 0, 1, 1, 1,
        volumeBytes.data(), 4, MetalTransferLengthRule::AtLeastTightBytes, 1, layout));
}

TEST(MetalTextureTransfer, RenderTargetMipDefinitionTracksFullUploadsAndGeneration)
{
    MetalMipDefinitionState state(4);
    EXPECT_FALSE(state.IsDefined(-1));
    EXPECT_FALSE(state.IsDefined(0));
    EXPECT_FALSE(state.IsDefined(3));
    EXPECT_FALSE(state.IsDefined(4));

    state.MarkUploaded(2);
    EXPECT_FALSE(state.IsDefined(0));
    EXPECT_TRUE(state.IsDefined(2));

    state.MarkGenerated();
    EXPECT_TRUE(state.IsDefined(0));
    EXPECT_TRUE(state.IsDefined(1));
    EXPECT_TRUE(state.IsDefined(2));
    EXPECT_TRUE(state.IsDefined(3));
}

TEST(MetalTextureTransfer, ComputesOnePixelAndBoundaryWidthPitches)
{
    struct Expected { int width; std::size_t tight; std::size_t aligned; };
    const Expected cases[] = {
        {1, 4, 256},
        {63, 252, 256},
        {64, 256, 256},
        {65, 260, 512},
    };
    for (const Expected& expected : cases)
    {
        MetalTextureTransferLayout layout{};
        ASSERT_TRUE(TryBuildMetalTextureTransferLayout(
            expected.width, 1, 1, MetalMacOsTextureBufferRowAlignment, layout));
        EXPECT_EQ(layout.tightRowBytes, expected.tight);
        EXPECT_EQ(layout.alignedRowBytes, expected.aligned);
        EXPECT_EQ(layout.tightTotalBytes, expected.tight);
        EXPECT_EQ(layout.stagingTotalBytes, expected.aligned);
    }
}

TEST(MetalTextureTransfer, ComputesPaddedRowsImagesAndDepth)
{
    MetalTextureTransferLayout layout{};
    ASSERT_TRUE(TryBuildMetalTextureTransferLayout(3, 2, 4, 256, layout));
    EXPECT_EQ(layout.tightRowBytes, 12u);
    EXPECT_EQ(layout.tightImageBytes, 24u);
    EXPECT_EQ(layout.tightTotalBytes, 96u);
    EXPECT_EQ(layout.alignedRowBytes, 256u);
    EXPECT_EQ(layout.alignedImageBytes, 512u);
    EXPECT_EQ(layout.stagingTotalBytes, 2048u);
}

TEST(MetalTextureTransfer, RejectsInvalidDimensionsAlignmentAndOverflow)
{
    MetalTextureTransferLayout layout{1, 2, 3, 4, 5, 6};
    EXPECT_FALSE(TryBuildMetalTextureTransferLayout(0, 1, 1, 256, layout));
    EXPECT_FALSE(TryBuildMetalTextureTransferLayout(1, -1, 1, 256, layout));
    EXPECT_FALSE(TryBuildMetalTextureTransferLayout(1, 1, 1, 0, layout));
    EXPECT_FALSE(TryBuildMetalTextureTransferLayout(INT_MAX, INT_MAX, INT_MAX, 256, layout));
    EXPECT_EQ(layout.tightRowBytes, 1u);
    EXPECT_EQ(layout.stagingTotalBytes, 6u);
}

TEST(MetalTextureTransfer, ValidatesMipRangePointerAndExactLengthBeforeTransfer)
{
    std::vector<std::uint8_t> bytes(4 * 4 * 4);
    MetalTextureTransferLayout layout{};
    EXPECT_TRUE(TryPrepareMetalTextureTransfer(
        8, 8, 1, 4, 1, 0, 0, 0, 4, 4, 1, bytes.data(), 64,
        MetalTransferLengthRule::ExactlyTightBytes, 256, layout));
    EXPECT_FALSE(TryPrepareMetalTextureTransfer(
        8, 8, 1, 4, 4, 0, 0, 0, 1, 1, 1, bytes.data(), 4,
        MetalTransferLengthRule::ExactlyTightBytes, 256, layout));
    EXPECT_FALSE(TryPrepareMetalTextureTransfer(
        8, 8, 1, 4, 1, 1, 0, 0, 4, 4, 1, bytes.data(), 64,
        MetalTransferLengthRule::ExactlyTightBytes, 256, layout));
    EXPECT_FALSE(TryPrepareMetalTextureTransfer(
        8, 8, 1, 4, 1, 0, 0, 0, 4, 4, 1, nullptr, 64,
        MetalTransferLengthRule::ExactlyTightBytes, 256, layout));
    EXPECT_FALSE(TryPrepareMetalTextureTransfer(
        8, 8, 1, 4, 1, 0, 0, 0, 4, 4, 1, bytes.data(), 63,
        MetalTransferLengthRule::ExactlyTightBytes, 256, layout));
    EXPECT_FALSE(TryPrepareMetalTextureTransfer(
        8, 8, 1, 4, 1, 0, 0, 0, 4, 4, 1, bytes.data(), 65,
        MetalTransferLengthRule::ExactlyTightBytes, 256, layout));
}

TEST(MetalTextureTransfer, UploadLengthMayExceedButNeverUndershootTightRegion)
{
    std::vector<std::uint8_t> bytes(65);
    MetalTextureTransferLayout layout{};
    EXPECT_TRUE(TryPrepareMetalTextureTransfer(
        4, 4, 1, 1, 0, 0, 0, 0, 4, 4, 1, bytes.data(), 65,
        MetalTransferLengthRule::AtLeastTightBytes, 256, layout));
    EXPECT_FALSE(TryPrepareMetalTextureTransfer(
        4, 4, 1, 1, 0, 0, 0, 0, 4, 4, 1, bytes.data(), 63,
        MetalTransferLengthRule::AtLeastTightBytes, 256, layout));
}

namespace
{
    std::vector<std::uint8_t> BuildPaddedPixels(
        const MetalTextureTransferLayout& layout, int width, int height, int depth,
        MetalTransferPixelOrder order)
    {
        std::vector<std::uint8_t> staging(layout.stagingTotalBytes, 0xEE);
        for (int image = 0; image < depth; ++image)
        {
            for (int row = 0; row < height; ++row)
            {
                for (int column = 0; column < width; ++column)
                {
                    const std::uint8_t red = static_cast<std::uint8_t>(10 + image * 30 + row * 7 + column);
                    const std::uint8_t green = static_cast<std::uint8_t>(80 + image * 11 + row * 3 + column);
                    const std::uint8_t blue = static_cast<std::uint8_t>(150 + image * 5 + row * 2 + column);
                    std::uint8_t* pixel = staging.data()
                        + static_cast<std::size_t>(image) * layout.alignedImageBytes
                        + static_cast<std::size_t>(row) * layout.alignedRowBytes
                        + static_cast<std::size_t>(column) * 4u;
                    pixel[0] = order == MetalTransferPixelOrder::Bgra ? blue : red;
                    pixel[1] = green;
                    pixel[2] = order == MetalTransferPixelOrder::Bgra ? red : blue;
                    pixel[3] = 255;
                }
            }
        }
        return staging;
    }
}

TEST(MetalTextureTransfer, DepadsOddWidthRgbaAcrossRowsAndSlices)
{
    MetalTextureTransferLayout layout{};
    ASSERT_TRUE(TryBuildMetalTextureTransferLayout(3, 2, 2, 256, layout));
    const auto staging = BuildPaddedPixels(layout, 3, 2, 2, MetalTransferPixelOrder::Rgba);
    std::vector<std::uint8_t> output(layout.tightTotalBytes, 0);
    ASSERT_TRUE(CopyMetalTextureReadbackToTightRgba(
        staging.data(), layout, 3, 2, 2, MetalTransferPixelOrder::Rgba,
        output.data(), output.size()));
    EXPECT_EQ(output[0], 10);
    EXPECT_EQ(output[4 * 3], 17);
    EXPECT_EQ(output[layout.tightImageBytes], 40);
    EXPECT_EQ(output[output.size() - 1], 255);
}

TEST(MetalTextureTransfer, DepadsAndSwizzlesBgraAcrossRowsAndSlices)
{
    MetalTextureTransferLayout layout{};
    ASSERT_TRUE(TryBuildMetalTextureTransferLayout(5, 3, 2, 256, layout));
    const auto staging = BuildPaddedPixels(layout, 5, 3, 2, MetalTransferPixelOrder::Bgra);
    std::vector<std::uint8_t> output(layout.tightTotalBytes, 0);
    ASSERT_TRUE(CopyMetalTextureReadbackToTightRgba(
        staging.data(), layout, 5, 3, 2, MetalTransferPixelOrder::Bgra,
        output.data(), output.size()));
    EXPECT_EQ(output[0], 10);
    EXPECT_EQ(output[1], 80);
    EXPECT_EQ(output[2], 150);
    const std::size_t secondSlice = layout.tightImageBytes;
    EXPECT_EQ(output[secondSlice + 0], 40);
    EXPECT_EQ(output[secondSlice + 1], 91);
    EXPECT_EQ(output[secondSlice + 2], 155);
}

TEST(MetalTextureTransfer, CopyRejectsShortDestinationWithoutMutation)
{
    MetalTextureTransferLayout layout{};
    ASSERT_TRUE(TryBuildMetalTextureTransferLayout(1, 1, 1, 256, layout));
    std::vector<std::uint8_t> staging(layout.stagingTotalBytes, 1);
    std::vector<std::uint8_t> output(4, 0xAA);
    EXPECT_FALSE(CopyMetalTextureReadbackToTightRgba(
        staging.data(), layout, 1, 1, 1, MetalTransferPixelOrder::Rgba,
        output.data(), 3));
    EXPECT_EQ(output, (std::vector<std::uint8_t>{0xAA, 0xAA, 0xAA, 0xAA}));
}

TEST(MetalTextureTransfer, ConvertsTightRgbaUploadToBgraAcrossWholeRegion)
{
    MetalTextureTransferLayout layout{};
    ASSERT_TRUE(TryBuildMetalTextureTransferLayout(3, 2, 1, 1, layout));
    std::vector<std::uint8_t> rgba(layout.tightTotalBytes);
    for (std::size_t pixel = 0; pixel < rgba.size() / 4u; ++pixel)
    {
        rgba[pixel * 4u + 0] = static_cast<std::uint8_t>(10 + pixel);
        rgba[pixel * 4u + 1] = static_cast<std::uint8_t>(40 + pixel);
        rgba[pixel * 4u + 2] = static_cast<std::uint8_t>(70 + pixel);
        rgba[pixel * 4u + 3] = 255;
    }
    std::vector<std::uint8_t> bgra(layout.tightTotalBytes, 0);
    ASSERT_TRUE(CopyMetalTightRgbaToTextureBytes(
        rgba.data(), layout, MetalTransferPixelOrder::Bgra, bgra.data(), bgra.size()));
    EXPECT_EQ(bgra[0], 70);
    EXPECT_EQ(bgra[1], 40);
    EXPECT_EQ(bgra[2], 10);
    EXPECT_EQ(bgra[bgra.size() - 1], 255);
}

// plans/plan_apple_m4.md AM4-142: float, 16-bit and packed render targets transfer their own
// texel size verbatim (their Metal byte layout is XNA's).
TEST(MetalTextureTransfer, RawTransfersKeepAnyTexelSizeVerbatim)
{
    MetalTextureTransferLayout layout{};
    // A 3x2 HalfVector4 (8-byte) region with 32-byte row alignment.
    ASSERT_TRUE(TryBuildMetalTextureTransferLayout(3, 2, 1, 32, layout, 8u));
    EXPECT_EQ(layout.tightRowBytes, 24u);
    EXPECT_EQ(layout.alignedRowBytes, 32u);
    EXPECT_EQ(layout.tightTotalBytes, 48u);
    std::vector<std::uint8_t> staging(layout.stagingTotalBytes, 0xEE);
    for (std::size_t row = 0; row < 2; ++row)
        for (std::size_t b = 0; b < 24; ++b) staging[row * 32 + b] = static_cast<std::uint8_t>(row * 24 + b);
    std::vector<std::uint8_t> tight(48, 0);
    ASSERT_TRUE(CopyMetalTextureReadbackToTightRgba(staging.data(), layout, 3, 2, 1,
                                                    MetalTransferPixelOrder::Raw, tight.data(), tight.size()));
    for (std::size_t i = 0; i < 48; ++i) EXPECT_EQ(tight[i], static_cast<std::uint8_t>(i));

    std::vector<std::uint8_t> back(48, 0);
    ASSERT_TRUE(CopyMetalTightRgbaToTextureBytes(tight.data(), layout, MetalTransferPixelOrder::Raw,
                                                 back.data(), back.size()));
    EXPECT_EQ(back, tight);

    // A 2-byte (HalfSingle) texel validates through the prepare path too.
    std::vector<std::uint8_t> half(2 * 5 * 3);
    ASSERT_TRUE(TryPrepareMetalTextureTransfer(5, 3, 1, 1, 0, 0, 0, 0, 5, 3, 1, half.data(),
                                               static_cast<int>(half.size()),
                                               MetalTransferLengthRule::ExactlyTightBytes, 256, layout, 2u));
    EXPECT_EQ(layout.tightRowBytes, 10u);
    EXPECT_FALSE(TryPrepareMetalTextureTransfer(5, 3, 1, 1, 0, 0, 0, 0, 5, 3, 1, half.data(),
                                                static_cast<int>(half.size()),
                                                MetalTransferLengthRule::ExactlyTightBytes, 256, layout))
        << "the same bytes are the wrong length for 4-byte texels";
    // Swizzling orders still require 4-byte texels.
    ASSERT_TRUE(TryBuildMetalTextureTransferLayout(3, 2, 1, 32, layout, 8u));
    EXPECT_FALSE(CopyMetalTextureReadbackToTightRgba(staging.data(), layout, 3, 2, 1,
                                                     MetalTransferPixelOrder::Bgra, tight.data(), tight.size()));
}
