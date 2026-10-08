// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#include <cstdint>
#include <numeric>
#include <vector>

#include "CNA/Internal/Renderers/Metal/MetalCompressedTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

using namespace CNA::Internal::Renderers::Metal;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

// plans/plan_apple_m4.md AM4-148
TEST(MetalCompressedTexture, BlockBytesAndStorageCoverExactlyTheClassicDxtFormats)
{
    EXPECT_EQ(MetalDxtBlockBytes(static_cast<int>(SurfaceFormat::Dxt1)), 8);
    EXPECT_EQ(MetalDxtBlockBytes(static_cast<int>(SurfaceFormat::Dxt3)), 16);
    EXPECT_EQ(MetalDxtBlockBytes(static_cast<int>(SurfaceFormat::Dxt5)), 16);
    EXPECT_EQ(MetalDxtBlockBytes(static_cast<int>(SurfaceFormat::Color)), 0);
    EXPECT_EQ(MetalDxtBlockBytes(static_cast<int>(SurfaceFormat::Alpha8)), 0);

    MetalCompressedStorage storage = MetalCompressedStorage::DecodedRgba8;
    ASSERT_TRUE(MetalCompressedStorageFor(static_cast<int>(SurfaceFormat::Dxt1), true, storage));
    EXPECT_EQ(storage, MetalCompressedStorage::Bc1);
    ASSERT_TRUE(MetalCompressedStorageFor(static_cast<int>(SurfaceFormat::Dxt3), true, storage));
    EXPECT_EQ(storage, MetalCompressedStorage::Bc2);
    ASSERT_TRUE(MetalCompressedStorageFor(static_cast<int>(SurfaceFormat::Dxt5), true, storage));
    EXPECT_EQ(storage, MetalCompressedStorage::Bc3);
    // A device that does not sample BC (an iPhone GPU) holds the decoded texels instead.
    for (const SurfaceFormat dxt : {SurfaceFormat::Dxt1, SurfaceFormat::Dxt3, SurfaceFormat::Dxt5})
    {
        ASSERT_TRUE(MetalCompressedStorageFor(static_cast<int>(dxt), false, storage));
        EXPECT_EQ(storage, MetalCompressedStorage::DecodedRgba8);
    }
    storage = MetalCompressedStorage::Bc3;
    EXPECT_FALSE(MetalCompressedStorageFor(static_cast<int>(SurfaceFormat::Color), true, storage));
    EXPECT_EQ(storage, MetalCompressedStorage::Bc3);
}

TEST(MetalCompressedTexture, ByteCountsRoundPartialBlocksUp)
{
    EXPECT_EQ(MetalBlockRowBytes(4, 8), 8u);
    EXPECT_EQ(MetalBlockRowBytes(5, 8), 16u);
    EXPECT_EQ(MetalBlockRowBytes(1, 16), 16u);
    EXPECT_EQ(MetalBlockRegionBytes(8, 8, 8), 32u);
    EXPECT_EQ(MetalBlockRegionBytes(2, 2, 16), 16u);
    EXPECT_EQ(MetalBlockRegionBytes(12, 6, 16), 96u);
}

TEST(MetalCompressedTexture, RegionsAreWholeBlocksOrStopAtTheLevelEdge)
{
    EXPECT_TRUE(MetalBlockRegionIsValid(8, 8, 0, 0, 8, 8));
    EXPECT_TRUE(MetalBlockRegionIsValid(8, 8, 4, 4, 4, 4));
    EXPECT_TRUE(MetalBlockRegionIsValid(2, 2, 0, 0, 2, 2));     // a level smaller than one block
    EXPECT_TRUE(MetalBlockRegionIsValid(10, 6, 8, 4, 2, 2));    // NPOT edge blocks
    EXPECT_FALSE(MetalBlockRegionIsValid(8, 8, 2, 0, 4, 4));    // misaligned origin
    EXPECT_FALSE(MetalBlockRegionIsValid(8, 8, 0, 0, 2, 4));    // partial block inside the level
    EXPECT_FALSE(MetalBlockRegionIsValid(8, 8, 4, 4, 8, 4));    // outside the level
    EXPECT_FALSE(MetalBlockRegionIsValid(8, 8, 0, 0, 0, 4));
    EXPECT_FALSE(MetalBlockRegionIsValid(8, 8, -4, 0, 4, 4));
}

TEST(MetalCompressedTexture, RegionCopiesMoveOnlyTheAddressedBlocks)
{
    // A 12x8 Dxt1 level: three blocks per row, two rows, each block's bytes all equal its index.
    constexpr int blockBytes = 8;
    std::vector<std::uint8_t> level(MetalBlockRegionBytes(12, 8, blockBytes));
    for (std::size_t i = 0; i < level.size(); ++i)
        level[i] = static_cast<std::uint8_t>(i / blockBytes);

    std::vector<std::uint8_t> region(MetalBlockRegionBytes(8, 4, blockBytes), 0xCD);
    ASSERT_TRUE(CopyMetalBlockRegionOut(level.data(), 12, 8, blockBytes, 4, 4, 8, 4,
                                        region.data(), region.size()));
    for (std::size_t i = 0; i < region.size(); ++i)
        EXPECT_EQ(region[i], i < blockBytes ? 4 : 5) << i;   // blocks (1,1) and (2,1)

    std::vector<std::uint8_t> patch(MetalBlockRegionBytes(4, 4, blockBytes), 0xEE);
    ASSERT_TRUE(CopyMetalBlockRegionIn(level.data(), 12, 8, blockBytes, 8, 0, 4, 4,
                                       patch.data(), patch.size()));
    for (std::size_t i = 0; i < level.size(); ++i)
    {
        const std::size_t block = i / blockBytes;
        EXPECT_EQ(level[i], block == 2 ? 0xEE : block) << i;   // only block (2,0) changed
    }

    // Refusals copy nothing.
    const std::vector<std::uint8_t> before = level;
    std::vector<std::uint8_t> wrongSize(region.size() + 1, 0xCD);
    EXPECT_FALSE(CopyMetalBlockRegionOut(level.data(), 12, 8, blockBytes, 4, 4, 8, 4,
                                         wrongSize.data(), wrongSize.size()));
    EXPECT_EQ(wrongSize, std::vector<std::uint8_t>(region.size() + 1, 0xCD));
    EXPECT_FALSE(CopyMetalBlockRegionIn(level.data(), 12, 8, blockBytes, 2, 0, 4, 4,
                                        patch.data(), patch.size()));
    EXPECT_FALSE(CopyMetalBlockRegionIn(level.data(), 12, 8, blockBytes, 8, 0, 4, 4,
                                        patch.data(), patch.size() - 1));
    EXPECT_EQ(level, before);
}
