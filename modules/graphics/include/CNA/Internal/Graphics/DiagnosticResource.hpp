// SPDX-License-Identifier: MS-PL
#pragma once

#if defined(CNA_DIAGNOSTICS_LEVEL) && CNA_DIAGNOSTICS_LEVEL >= 1

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>

#include "CNA/Diagnostics/Diagnostics.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"

namespace CNA::Internal::Graphics
{
    /**
     * @brief Multiplies two diagnostic sizes without unsigned overflow.
     * @param left Left operand.
     * @param right Right operand.
     * @return Product, saturated at the maximum 64-bit unsigned value.
     */
    [[nodiscard]] inline std::uint64_t SaturatingDiagnosticMultiply(
        std::uint64_t left, std::uint64_t right) noexcept
    {
        if (right != 0 && left > (std::numeric_limits<std::uint64_t>::max)() / right)
            return (std::numeric_limits<std::uint64_t>::max)();
        return left * right;
    }

    /**
     * @brief Estimates the declared pixel payload across texture faces and mip levels.
     * @param width Base width in texels.
     * @param height Base height in texels.
     * @param depth Base depth in texels.
     * @param mipCount Number of mip levels.
     * @param faces Number of faces.
     * @param format Declared surface format.
     * @return Saturating byte estimate, or zero when the format is unavailable.
     */
    [[nodiscard]] inline std::uint64_t EstimateTextureBytes(
        int width, int height, int depth, int mipCount, int faces,
        Microsoft::Xna::Framework::Graphics::SurfaceFormat format) noexcept
    {
        using Microsoft::Xna::Framework::Graphics::Texture;
        try
        {
            std::uint64_t total = 0;
            int levelWidth = std::max(width, 1);
            int levelHeight = std::max(height, 1);
            int levelDepth = std::max(depth, 1);
            const int blockArea = Texture::GetBlockSizeSquaredEXT(format);
            const std::uint64_t elementBytes =
                static_cast<std::uint64_t>(Texture::GetFormatSizeEXT(format));
            for (int level = 0; level < std::max(mipCount, 1); ++level)
            {
                const std::uint64_t columns = blockArea == 1
                    ? static_cast<std::uint64_t>(levelWidth)
                    : static_cast<std::uint64_t>((levelWidth + 3) / 4);
                const std::uint64_t rows = blockArea == 1
                    ? static_cast<std::uint64_t>(levelHeight)
                    : static_cast<std::uint64_t>((levelHeight + 3) / 4);
                std::uint64_t levelBytes = SaturatingDiagnosticMultiply(columns, rows);
                levelBytes = SaturatingDiagnosticMultiply(
                    levelBytes, static_cast<std::uint64_t>(levelDepth));
                levelBytes = SaturatingDiagnosticMultiply(levelBytes, elementBytes);
                levelBytes = SaturatingDiagnosticMultiply(
                    levelBytes, static_cast<std::uint64_t>(std::max(faces, 1)));
                if (total > (std::numeric_limits<std::uint64_t>::max)() - levelBytes)
                    return (std::numeric_limits<std::uint64_t>::max)();
                total += levelBytes;
                levelWidth = std::max(1, levelWidth / 2);
                levelHeight = std::max(1, levelHeight / 2);
                levelDepth = std::max(1, levelDepth / 2);
            }
            return total;
        }
        catch (...)
        {
            return 0;
        }
    }

    /**
     * @brief Creates diagnostic metadata for a texture-like resource.
     * @param kind Diagnostic resource kind.
     * @param width Base width in texels.
     * @param height Base height in texels.
     * @param depth Base depth in texels.
     * @param mipCount Number of mip levels.
     * @param faces Number of faces.
     * @param format Declared surface format.
     * @param label Stable diagnostic label.
     * @return Descriptor whose byte value is explicitly classified as estimated.
     */
    [[nodiscard]] inline CNA::Diagnostics::ResourceDescriptor MakeTextureDiagnosticDescriptor(
        CNA::Diagnostics::ResourceKind kind, int width, int height, int depth, int mipCount,
        int faces, Microsoft::Xna::Framework::Graphics::SurfaceFormat format,
        std::string_view label) noexcept
    {
        // The payload math is exact for CNA's declared format and mip extents. Native APIs may
        // add row alignment, auxiliary images, resolves, or driver-private allocation, so the
        // GPU allocation claim remains explicitly Estimated.
        thread_local std::string formatName;
        try
        {
            formatName = std::to_string(static_cast<int>(format));
        }
        catch (...)
        {
            formatName.clear();
        }
        return CNA::Diagnostics::ResourceDescriptor{
            .kind = kind,
            .label = label,
            .format = formatName,
            .width = static_cast<std::uint32_t>(std::max(width, 0)),
            .height = static_cast<std::uint32_t>(std::max(height, 0)),
            .depth = static_cast<std::uint32_t>(std::max(depth, 0)),
            .mipCount = static_cast<std::uint32_t>(std::max(mipCount, 0)),
            .estimatedBytes = EstimateTextureBytes(
                width, height, depth, mipCount, faces, format),
            .byteAccuracy = CNA::Diagnostics::Accuracy::Estimated};
    }

    /**
     * @brief Creates diagnostic metadata for a buffer-like resource.
     * @param kind Diagnostic resource kind.
     * @param byteCount Logical buffer capacity in bytes.
     * @param label Stable diagnostic label.
     * @return Descriptor whose byte value is exact at CNA's abstraction layer.
     */
    [[nodiscard]] inline CNA::Diagnostics::ResourceDescriptor MakeBufferDiagnosticDescriptor(
        CNA::Diagnostics::ResourceKind kind, std::uint64_t byteCount,
        std::string_view label) noexcept
    {
        return CNA::Diagnostics::ResourceDescriptor{
            .kind = kind,
            .label = label,
            .estimatedBytes = byteCount,
            .byteAccuracy = CNA::Diagnostics::Accuracy::Exact};
    }
}

#endif
