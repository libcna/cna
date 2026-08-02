#pragma once

#include <cstddef>
#include <limits>

namespace CNA::Internal::Backends::Skia
{
    /** Shared per-resource ceiling for every CNA-owned Skia CPU allocation. */
    inline constexpr std::size_t kSkiaCpuTextureStorageLimitBytes = 256u * 1024u * 1024u;

    /** Shared edge/axis ceiling, matching GraphicsDevice's existing texture policy. */
    inline constexpr int kSkiaCpuTextureMaximumAxis = 16384;

    /** Compiler/cache limits for explicitly tagged SkSL programs. */
    inline constexpr std::size_t kSkiaSkslMaxSourceBytesEXT = 64u * 1024u;
    inline constexpr std::size_t kSkiaSkslMaxUniformBytesEXT = 16u * 1024u;
    inline constexpr std::size_t kSkiaSkslMaxUniformCountEXT = 64u;
    inline constexpr int kSkiaSkslMaxTextureUnitEXT = 7;

    [[nodiscard]] inline constexpr bool IsValidSkiaResourceAxis(int value) noexcept
    {
        return value > 0 && value <= kSkiaCpuTextureMaximumAxis;
    }

    /** Returns false without changing result when the product cannot fit in size_t. */
    [[nodiscard]] inline bool CheckedSizeMultiply(std::size_t left, std::size_t right,
                                                  std::size_t& result) noexcept
    {
        if (left != 0u && right > std::numeric_limits<std::size_t>::max() / left)
            return false;
        result = left * right;
        return true;
    }

    /** Returns false without changing result when the sum cannot fit in size_t. */
    [[nodiscard]] inline bool CheckedSizeAdd(std::size_t left, std::size_t right,
                                             std::size_t& result) noexcept
    {
        if (right > std::numeric_limits<std::size_t>::max() - left)
            return false;
        result = left + right;
        return true;
    }

    /** Computes width * height * bytesPerTexel, preserving result on overflow. */
    [[nodiscard]] inline bool CheckedTexelBytes2D(std::size_t width, std::size_t height,
                                                  std::size_t bytesPerTexel,
                                                  std::size_t& result) noexcept
    {
        std::size_t texels = 0;
        std::size_t bytes = 0;
        if (!CheckedSizeMultiply(width, height, texels)
            || !CheckedSizeMultiply(texels, bytesPerTexel, bytes))
        {
            return false;
        }
        result = bytes;
        return true;
    }

    /** Computes width * height * depth * bytesPerTexel, preserving result on overflow. */
    [[nodiscard]] inline bool CheckedTexelBytes3D(std::size_t width, std::size_t height,
                                                  std::size_t depth,
                                                  std::size_t bytesPerTexel,
                                                  std::size_t& result) noexcept
    {
        std::size_t sliceBytes = 0;
        std::size_t bytes = 0;
        if (!CheckedTexelBytes2D(width, height, bytesPerTexel, sliceBytes)
            || !CheckedSizeMultiply(sliceBytes, depth, bytes))
        {
            return false;
        }
        result = bytes;
        return true;
    }

    /**
     * Adds one retained store to a resource budget. On overflow or limit violation, result is
     * unchanged so callers can finish all validation before publishing counters or allocations.
     */
    [[nodiscard]] inline bool CheckedSkiaResourceAccumulate(
        std::size_t current, std::size_t addition, std::size_t& result) noexcept
    {
        std::size_t next = 0;
        if (!CheckedSizeAdd(current, addition, next)
            || next > kSkiaCpuTextureStorageLimitBytes)
        {
            return false;
        }
        result = next;
        return true;
    }
} // namespace CNA::Internal::Backends::Skia
