// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

// plans/plan_apple_m4.md AM4-148: the block arithmetic of Metal's DXT textures. A DXT texture keeps
// the exact blocks of every level on the CPU -- XNA's GetData returns them, and the framework's
// partial SetData patches them -- and the GPU holds them as BC1/2/3 where the device samples BC,
// otherwise decoded to RGBA8. These helpers are pure so the region rules can be tested without a
// device.
namespace CNA::Internal::Renderers::Metal
{
    /** @brief How a DXT texture is held on the GPU. */
    enum class MetalCompressedStorage
    {
        /** @brief MTLPixelFormatBC1_RGBA -- Dxt1. */
        Bc1,
        /** @brief MTLPixelFormatBC2_RGBA -- Dxt3. */
        Bc2,
        /** @brief MTLPixelFormatBC3_RGBA -- Dxt5. */
        Bc3,
        /** @brief RGBA8Unorm holding the decoded texels, on a device that does not sample BC. */
        DecodedRgba8
    };

    /**
     * @brief Bytes in one 4x4 block of a classic DXT format.
     *
     * @param surfaceFormat SurfaceFormat ordinal.
     * @return 8 for Dxt1, 16 for Dxt3 and Dxt5, 0 for every other format.
     */
    [[nodiscard]] constexpr int MetalDxtBlockBytes(int surfaceFormat) noexcept
    {
        using SF = Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        switch (static_cast<SF>(surfaceFormat))
        {
            case SF::Dxt1: return 8;
            case SF::Dxt3:
            case SF::Dxt5: return 16;
            default: return 0;
        }
    }

    /**
     * @brief The GPU storage of a DXT format on a device.
     *
     * @param surfaceFormat SurfaceFormat ordinal.
     * @param bcAvailable Whether the device samples BC1/2/3 (`supportsBCTextureCompression`).
     * @param storage Receives the storage when the format is a classic DXT format.
     * @return True for Dxt1, Dxt3 and Dxt5.
     */
    [[nodiscard]] constexpr bool MetalCompressedStorageFor(int surfaceFormat, bool bcAvailable,
                                                           MetalCompressedStorage& storage) noexcept
    {
        using SF = Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        MetalCompressedStorage native = MetalCompressedStorage::Bc1;
        switch (static_cast<SF>(surfaceFormat))
        {
            case SF::Dxt1: native = MetalCompressedStorage::Bc1; break;
            case SF::Dxt3: native = MetalCompressedStorage::Bc2; break;
            case SF::Dxt5: native = MetalCompressedStorage::Bc3; break;
            default: return false;
        }
        storage = bcAvailable ? native : MetalCompressedStorage::DecodedRgba8;
        return true;
    }

    /**
     * @brief Bytes in one tightly packed row of blocks covering @p width texels.
     *
     * @param width Width in texels (a partial block counts as a whole one).
     * @param blockBytes Bytes in one block.
     * @return The row's byte count.
     */
    [[nodiscard]] constexpr std::size_t MetalBlockRowBytes(int width, int blockBytes) noexcept
    {
        return static_cast<std::size_t>((width + 3) / 4) * static_cast<std::size_t>(blockBytes);
    }

    /**
     * @brief Bytes in every block covering a @p width x @p height region.
     *
     * @param width Width in texels.
     * @param height Height in texels.
     * @param blockBytes Bytes in one block.
     * @return The region's byte count.
     */
    [[nodiscard]] constexpr std::size_t MetalBlockRegionBytes(int width, int height, int blockBytes) noexcept
    {
        return MetalBlockRowBytes(width, blockBytes) * static_cast<std::size_t>((height + 3) / 4);
    }

    /**
     * @brief Whether a texel region of one mip level is a whole number of blocks.
     *
     * The region starts on a block boundary and spans whole blocks, except that its right and
     * bottom edges may stop at the level's own edge, which a level that is not a multiple of four
     * (or smaller than one block) ends inside a block.
     *
     * @param levelWidth Width of the mip level in texels.
     * @param levelHeight Height of the mip level in texels.
     * @param x Left edge in texels.
     * @param y Top edge in texels.
     * @param width Region width in texels.
     * @param height Region height in texels.
     * @return True when the region addresses whole blocks inside the level.
     */
    [[nodiscard]] constexpr bool MetalBlockRegionIsValid(int levelWidth, int levelHeight, int x, int y,
                                                         int width, int height) noexcept
    {
        if (levelWidth <= 0 || levelHeight <= 0 || x < 0 || y < 0 || width <= 0 || height <= 0)
            return false;
        if (width > levelWidth || height > levelHeight || x > levelWidth - width ||
            y > levelHeight - height)
            return false;
        if (x % 4 != 0 || y % 4 != 0)
            return false;
        return (width % 4 == 0 || x + width == levelWidth) &&
               (height % 4 == 0 || y + height == levelHeight);
    }

    /**
     * @brief Copies a region's blocks out of a level's tightly packed blocks.
     *
     * @param level The level's blocks, tightly packed by block row.
     * @param levelWidth Width of the level in texels.
     * @param levelHeight Height of the level in texels.
     * @param blockBytes Bytes in one block.
     * @param x Left edge in texels.
     * @param y Top edge in texels.
     * @param width Region width in texels.
     * @param height Region height in texels.
     * @param destination Receives the region's blocks, tightly packed by block row.
     * @param destinationLength Bytes available at @p destination.
     * @return False, copying nothing, when the region is not whole blocks inside the level or the
     *         destination is not exactly the region's size.
     */
    [[nodiscard]] inline bool CopyMetalBlockRegionOut(const std::uint8_t* level, int levelWidth,
                                                      int levelHeight, int blockBytes, int x, int y,
                                                      int width, int height,
                                                      std::uint8_t* destination,
                                                      std::size_t destinationLength) noexcept
    {
        if (!level || !destination || blockBytes <= 0 ||
            !MetalBlockRegionIsValid(levelWidth, levelHeight, x, y, width, height) ||
            destinationLength != MetalBlockRegionBytes(width, height, blockBytes))
            return false;
        const std::size_t levelRow = MetalBlockRowBytes(levelWidth, blockBytes);
        const std::size_t regionRow = MetalBlockRowBytes(width, blockBytes);
        const std::size_t firstByte = static_cast<std::size_t>(x / 4) * static_cast<std::size_t>(blockBytes);
        for (int row = 0; row < (height + 3) / 4; ++row)
            std::memcpy(destination + static_cast<std::size_t>(row) * regionRow,
                        level + static_cast<std::size_t>(y / 4 + row) * levelRow + firstByte, regionRow);
        return true;
    }

    /**
     * @brief Writes a region's blocks into a level's tightly packed blocks, leaving the rest.
     *
     * @param level The level's blocks, tightly packed by block row.
     * @param levelWidth Width of the level in texels.
     * @param levelHeight Height of the level in texels.
     * @param blockBytes Bytes in one block.
     * @param x Left edge in texels.
     * @param y Top edge in texels.
     * @param width Region width in texels.
     * @param height Region height in texels.
     * @param source The region's blocks, tightly packed by block row.
     * @param sourceLength Bytes available at @p source; trailing bytes are ignored.
     * @return False, writing nothing, when the region is not whole blocks inside the level or the
     *         source is shorter than the region.
     */
    [[nodiscard]] inline bool CopyMetalBlockRegionIn(std::uint8_t* level, int levelWidth, int levelHeight,
                                                     int blockBytes, int x, int y, int width, int height,
                                                     const std::uint8_t* source,
                                                     std::size_t sourceLength) noexcept
    {
        if (!level || !source || blockBytes <= 0 ||
            !MetalBlockRegionIsValid(levelWidth, levelHeight, x, y, width, height) ||
            sourceLength < MetalBlockRegionBytes(width, height, blockBytes))
            return false;
        const std::size_t levelRow = MetalBlockRowBytes(levelWidth, blockBytes);
        const std::size_t regionRow = MetalBlockRowBytes(width, blockBytes);
        const std::size_t firstByte = static_cast<std::size_t>(x / 4) * static_cast<std::size_t>(blockBytes);
        for (int row = 0; row < (height + 3) / 4; ++row)
            std::memcpy(level + static_cast<std::size_t>(y / 4 + row) * levelRow + firstByte,
                        source + static_cast<std::size_t>(row) * regionRow, regionRow);
        return true;
    }
}
