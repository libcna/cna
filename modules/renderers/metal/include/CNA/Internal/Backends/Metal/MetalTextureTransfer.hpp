// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace CNA::Internal::Backends::Metal
{
    /** @brief Conservative macOS texture-to-buffer row alignment used by the supported lane. */
    inline constexpr std::size_t MetalMacOsTextureBufferRowAlignment = 256;

    /** @brief Byte order exposed by the source Metal texture's pixel format. */
    enum class MetalTransferPixelOrder
    {
        /** @brief Source bytes are already red, green, blue, alpha. */
        Rgba,
        /** @brief Source bytes are blue, green, red, alpha and require red/blue swapping. */
        Bgra
    };

    /** @brief Rule applied to the caller-provided transfer buffer length. */
    enum class MetalTransferLengthRule
    {
        /** @brief Upload buffers may contain trailing bytes after the requested region. */
        AtLeastTightBytes,
        /** @brief Readback buffers must describe exactly the requested tightly packed region. */
        ExactlyTightBytes
    };

    /** @brief Overflow-checked tight and Metal-aligned byte layout for one transfer region. */
    struct MetalTextureTransferLayout
    {
        /** @brief Caller-visible bytes in one tightly packed row. */
        std::size_t tightRowBytes = 0;
        /** @brief Caller-visible bytes in one tightly packed image/slice. */
        std::size_t tightImageBytes = 0;
        /** @brief Caller-visible bytes in the complete tightly packed region. */
        std::size_t tightTotalBytes = 0;
        /** @brief Metal staging bytes in one row after alignment. */
        std::size_t alignedRowBytes = 0;
        /** @brief Metal staging bytes in one padded image/slice. */
        std::size_t alignedImageBytes = 0;
        /** @brief Metal staging bytes for every padded image/slice. */
        std::size_t stagingTotalBytes = 0;
    };

    namespace MetalTextureTransferDetail
    {
        /**
         * @brief Multiplies two byte-count factors without wrapping size_t.
         *
         * @param a First factor.
         * @param b Second factor.
         * @param result Receives the product only on success.
         * @return True when the product is representable.
         */
        inline bool CheckedMultiply(std::size_t a, std::size_t b, std::size_t& result) noexcept
        {
            if (a != 0 && b > std::numeric_limits<std::size_t>::max() / a)
                return false;
            result = a * b;
            return true;
        }

        /**
         * @brief Rounds a byte count up to a nonzero alignment without overflow.
         *
         * @param value Byte count to align.
         * @param alignment Required positive alignment.
         * @param result Receives the aligned byte count only on success.
         * @return True when alignment is nonzero and the rounded value is representable.
         */
        inline bool CheckedAlignUp(std::size_t value, std::size_t alignment,
                                   std::size_t& result) noexcept
        {
            if (alignment == 0)
                return false;
            const std::size_t remainder = value % alignment;
            if (remainder == 0)
            {
                result = value;
                return true;
            }
            const std::size_t increment = alignment - remainder;
            if (value > std::numeric_limits<std::size_t>::max() - increment)
                return false;
            result = value + increment;
            return true;
        }

        /**
         * @brief Computes one halved mip dimension with a one-texel floor.
         *
         * @param baseDimension Positive level-zero dimension.
         * @param level Nonnegative mip level.
         * @return Dimension at @p level, clamped to one.
         */
        inline int MipDimension(int baseDimension, int level) noexcept
        {
            int result = baseDimension;
            for (int i = 0; i < level && result > 1; ++i)
                result /= 2;
            return result > 0 ? result : 1;
        }
    }

    /**
     * @brief Computes the mip count exposed by CNA Texture2D, TextureCube, and Texture3D.
     *
     * Texture3D follows XNA/FNA and derives its chain from width and height only; depth shrinks
     * within those allocated levels but never creates extra levels by itself.
     *
     * @param width Level-zero width.
     * @param height Level-zero height.
     * @param mipMap Whether a complete mip chain was requested.
     * @return One for a non-mipmapped texture, otherwise the width/height-derived level count;
     *         zero when either dimension is not positive.
     */
    [[nodiscard]] constexpr int MetalMipLevelCount(
        int width,
        int height,
        bool mipMap) noexcept
    {
        if (width <= 0 || height <= 0)
            return 0;
        int levels = 1;
        while (mipMap && (width > 1 || height > 1))
        {
            width = width > 1 ? width / 2 : 1;
            height = height > 1 ? height / 2 : 1;
            ++levels;
        }
        return levels;
    }

    /** @brief Tracks which render-target mip levels have deterministic readable contents. */
    class MetalMipDefinitionState
    {
    public:
        /**
         * @brief Creates an initially undefined state for an allocated mip chain.
         *
         * @param levelCount Number of allocated levels.
         */
        explicit MetalMipDefinitionState(int levelCount)
            : defined_(levelCount > 0 ? static_cast<std::size_t>(levelCount) : 0u, false)
        {
        }

        /**
         * @brief Reports whether one level has deterministic complete contents.
         *
         * @param level Level to inspect.
         * @return True only after a full upload or successful mip generation defined it.
         */
        [[nodiscard]] bool IsDefined(int level) const noexcept
        {
            return level >= 0 && static_cast<std::size_t>(level) < defined_.size() &&
                   defined_[static_cast<std::size_t>(level)];
        }

        /**
         * @brief Marks one fully uploaded level as defined.
         *
         * @param level Level whose complete bytes were stored.
         */
        void MarkUploaded(int level) noexcept
        {
            if (level >= 0 && static_cast<std::size_t>(level) < defined_.size())
                defined_[static_cast<std::size_t>(level)] = true;
        }

        /** @brief Marks the full mip chain as defined after generation from level zero. */
        void MarkGenerated() noexcept
        {
            for (std::size_t level = 0; level < defined_.size(); ++level)
                defined_[level] = true;
        }

    private:
        std::vector<bool> defined_;
    };

    /**
     * @brief Computes tight client and padded Metal staging pitches without integer overflow.
     *
     * @param width Region width in texels.
     * @param height Region height in texels.
     * @param depth Region depth/image count.
     * @param rowAlignment Required Metal buffer row alignment in bytes.
     * @param layout Receives the complete layout only on success.
     * @return True when every dimension is positive and every byte calculation is representable.
     */
    [[nodiscard]] inline bool TryBuildMetalTextureTransferLayout(
        int width,
        int height,
        int depth,
        std::size_t rowAlignment,
        MetalTextureTransferLayout& layout) noexcept
    {
        if (width <= 0 || height <= 0 || depth <= 0 || rowAlignment == 0)
            return false;

        MetalTextureTransferLayout candidate{};
        if (!MetalTextureTransferDetail::CheckedMultiply(
                static_cast<std::size_t>(width), 4u, candidate.tightRowBytes) ||
            !MetalTextureTransferDetail::CheckedMultiply(
                candidate.tightRowBytes, static_cast<std::size_t>(height),
                candidate.tightImageBytes) ||
            !MetalTextureTransferDetail::CheckedMultiply(
                candidate.tightImageBytes, static_cast<std::size_t>(depth),
                candidate.tightTotalBytes) ||
            !MetalTextureTransferDetail::CheckedAlignUp(
                candidate.tightRowBytes, rowAlignment, candidate.alignedRowBytes) ||
            !MetalTextureTransferDetail::CheckedMultiply(
                candidate.alignedRowBytes, static_cast<std::size_t>(height),
                candidate.alignedImageBytes) ||
            !MetalTextureTransferDetail::CheckedMultiply(
                candidate.alignedImageBytes, static_cast<std::size_t>(depth),
                candidate.stagingTotalBytes))
        {
            return false;
        }
        layout = candidate;
        return true;
    }

    /**
     * @brief Validates a mip-region and caller buffer before any native transfer is attempted.
     *
     * Bounds use subtraction after independent extent checks, avoiding signed addition overflow.
     * The returned layout is unchanged on failure.
     *
     * @param baseWidth Level-zero texture width.
     * @param baseHeight Level-zero texture height.
     * @param baseDepth Level-zero texture depth; one for 2D and cube-face transfers.
     * @param levelCount Number of allocated mip levels.
     * @param level Requested mip level.
     * @param x Left origin at the requested level.
     * @param y Top origin at the requested level.
     * @param z Front origin at the requested level.
     * @param width Requested width.
     * @param height Requested height.
     * @param depth Requested depth/image count.
     * @param data Caller source or destination pointer.
     * @param dataLength Caller buffer length in bytes.
     * @param lengthRule Whether the buffer may contain trailing bytes.
     * @param rowAlignment Required Metal staging row alignment.
     * @param layout Receives the validated byte layout only on success.
     * @return True only when the complete transfer can be represented and fits the mip region.
     */
    [[nodiscard]] inline bool TryPrepareMetalTextureTransfer(
        int baseWidth,
        int baseHeight,
        int baseDepth,
        int levelCount,
        int level,
        int x,
        int y,
        int z,
        int width,
        int height,
        int depth,
        const void* data,
        int dataLength,
        MetalTransferLengthRule lengthRule,
        std::size_t rowAlignment,
        MetalTextureTransferLayout& layout) noexcept
    {
        if (baseWidth <= 0 || baseHeight <= 0 || baseDepth <= 0 || levelCount <= 0 ||
            level < 0 || level >= levelCount || x < 0 || y < 0 || z < 0 ||
            width <= 0 || height <= 0 || depth <= 0 || data == nullptr || dataLength < 0)
        {
            return false;
        }

        const int levelWidth = MetalTextureTransferDetail::MipDimension(baseWidth, level);
        const int levelHeight = MetalTextureTransferDetail::MipDimension(baseHeight, level);
        const int levelDepth = MetalTextureTransferDetail::MipDimension(baseDepth, level);
        if (width > levelWidth || height > levelHeight || depth > levelDepth ||
            x > levelWidth - width || y > levelHeight - height || z > levelDepth - depth)
        {
            return false;
        }

        MetalTextureTransferLayout candidate{};
        if (!TryBuildMetalTextureTransferLayout(width, height, depth, rowAlignment, candidate))
            return false;
        const std::size_t supplied = static_cast<std::size_t>(dataLength);
        if (lengthRule == MetalTransferLengthRule::ExactlyTightBytes
                ? supplied != candidate.tightTotalBytes
                : supplied < candidate.tightTotalBytes)
        {
            return false;
        }
        layout = candidate;
        return true;
    }

    /**
     * @brief Removes Metal row/image padding and converts source bytes to tight RGBA.
     *
     * @param staging Padded staging bytes returned by a texture-to-buffer blit.
     * @param layout Validated layout describing @p staging and @p destination.
     * @param width Region width in texels.
     * @param height Region height in texels.
     * @param depth Region image/slice count.
     * @param sourceOrder Pixel byte order stored by the source Metal texture.
     * @param destination Caller-visible tightly packed RGBA destination.
     * @param destinationLength Size of @p destination in bytes.
     * @return True when the complete region was copied; false before writing anything otherwise.
     */
    [[nodiscard]] inline bool CopyMetalTextureReadbackToTightRgba(
        const std::uint8_t* staging,
        const MetalTextureTransferLayout& layout,
        int width,
        int height,
        int depth,
        MetalTransferPixelOrder sourceOrder,
        std::uint8_t* destination,
        std::size_t destinationLength) noexcept
    {
        MetalTextureTransferLayout expected{};
        if (staging == nullptr || destination == nullptr ||
            !TryBuildMetalTextureTransferLayout(
                width, height, depth,
                layout.alignedRowBytes == 0 ? 0 : layout.alignedRowBytes,
                expected) ||
            expected.tightRowBytes != layout.tightRowBytes ||
            expected.tightImageBytes != layout.tightImageBytes ||
            expected.tightTotalBytes != layout.tightTotalBytes ||
            expected.alignedRowBytes != layout.alignedRowBytes ||
            expected.alignedImageBytes != layout.alignedImageBytes ||
            expected.stagingTotalBytes != layout.stagingTotalBytes ||
            destinationLength < layout.tightTotalBytes)
        {
            return false;
        }

        for (int image = 0; image < depth; ++image)
        {
            for (int row = 0; row < height; ++row)
            {
                const std::uint8_t* source = staging
                    + static_cast<std::size_t>(image) * layout.alignedImageBytes
                    + static_cast<std::size_t>(row) * layout.alignedRowBytes;
                std::uint8_t* target = destination
                    + static_cast<std::size_t>(image) * layout.tightImageBytes
                    + static_cast<std::size_t>(row) * layout.tightRowBytes;
                for (int column = 0; column < width; ++column)
                {
                    const std::size_t offset = static_cast<std::size_t>(column) * 4u;
                    if (sourceOrder == MetalTransferPixelOrder::Bgra)
                    {
                        target[offset + 0] = source[offset + 2];
                        target[offset + 1] = source[offset + 1];
                        target[offset + 2] = source[offset + 0];
                        target[offset + 3] = source[offset + 3];
                    }
                    else
                    {
                        target[offset + 0] = source[offset + 0];
                        target[offset + 1] = source[offset + 1];
                        target[offset + 2] = source[offset + 2];
                        target[offset + 3] = source[offset + 3];
                    }
                }
            }
        }
        return true;
    }

    /**
     * @brief Converts tightly packed caller RGBA bytes to a texture's native byte order.
     *
     * @param source Caller-visible tightly packed RGBA source.
     * @param layout Validated layout whose tight total describes both buffers.
     * @param destinationOrder Byte order required by the destination Metal texture.
     * @param destination Tightly packed native-order destination.
     * @param destinationLength Size of @p destination in bytes.
     * @return True when every pixel was converted; false before writing anything otherwise.
     */
    [[nodiscard]] inline bool CopyMetalTightRgbaToTextureBytes(
        const std::uint8_t* source,
        const MetalTextureTransferLayout& layout,
        MetalTransferPixelOrder destinationOrder,
        std::uint8_t* destination,
        std::size_t destinationLength) noexcept
    {
        if (source == nullptr || destination == nullptr ||
            layout.tightTotalBytes == 0 || layout.tightTotalBytes % 4u != 0 ||
            destinationLength < layout.tightTotalBytes)
        {
            return false;
        }
        for (std::size_t offset = 0; offset < layout.tightTotalBytes; offset += 4u)
        {
            if (destinationOrder == MetalTransferPixelOrder::Bgra)
            {
                destination[offset + 0] = source[offset + 2];
                destination[offset + 1] = source[offset + 1];
                destination[offset + 2] = source[offset + 0];
                destination[offset + 3] = source[offset + 3];
            }
            else
            {
                destination[offset + 0] = source[offset + 0];
                destination[offset + 1] = source[offset + 1];
                destination[offset + 2] = source[offset + 2];
                destination[offset + 3] = source[offset + 3];
            }
        }
        return true;
    }
}
