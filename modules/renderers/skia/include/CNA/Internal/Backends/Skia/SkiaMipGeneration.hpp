#pragma once

#include "CNA/Internal/Backends/Skia/SkiaMipChain2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>

namespace CNA::Internal::Backends::Skia
{
    // Shared little-endian primitives (SKIA-142): used by both SkiaTextureBackend's transfer
    // paths and the mip-generation dispatcher below, so both stay byte-for-byte consistent.
    [[nodiscard]] std::uint16_t ReadU16Le(const std::uint8_t* bytes) noexcept;
    [[nodiscard]] std::uint32_t ReadU32Le(const std::uint8_t* bytes) noexcept;
    void WriteU16Le(std::uint8_t* bytes, std::uint16_t value) noexcept;
    void WriteU32Le(std::uint8_t* bytes, std::uint32_t value) noexcept;

    [[nodiscard]] int DecodeSignedByte(std::uint8_t value) noexcept;
    [[nodiscard]] int AverageSnormComponent(std::int64_t sum, std::uint32_t sampleCount) noexcept;
    [[nodiscard]] bool IsSnormTextureFormat(
        Microsoft::Xna::Framework::Graphics::SurfaceFormat format) noexcept;
    [[nodiscard]] int SnormChannelCount(
        Microsoft::Xna::Framework::Graphics::SurfaceFormat format) noexcept;

    [[nodiscard]] double SrgbByteToLinear(std::uint8_t value) noexcept;
    [[nodiscard]] std::uint8_t LinearToSrgbByte(double value) noexcept;
    /** Area-box mip reduction for a straight-RGBA8 level pair, averaging RGB in linear light. */
    void GenerateSrgbMipLevel(SkiaMipChain2D& chain, int level);

    [[nodiscard]] bool IsFloatTextureFormat(
        Microsoft::Xna::Framework::Graphics::SurfaceFormat format) noexcept;
    [[nodiscard]] bool IsHalfTextureFormat(
        Microsoft::Xna::Framework::Graphics::SurfaceFormat format) noexcept;
    [[nodiscard]] int FloatChannelCount(
        Microsoft::Xna::Framework::Graphics::SurfaceFormat format) noexcept;
    [[nodiscard]] float ReadFloatChannel(Microsoft::Xna::Framework::Graphics::SurfaceFormat format,
                                        const std::uint8_t* texel, int channel) noexcept;
    void WriteFloatChannel(Microsoft::Xna::Framework::Graphics::SurfaceFormat format,
                           std::uint8_t* texel, int channel, float value) noexcept;

    /** Deterministic finite/NaN/infinity-aware accumulator used by generated float/half mips. */
    struct FloatMipAccumulator final
    {
        double finiteSum = 0.0;
        bool hasFinite = false;
        bool hasNaN = false;
        bool hasPositiveInfinity = false;
        bool hasNegativeInfinity = false;

        void Add(float value) noexcept;
        [[nodiscard]] float Average(std::uint32_t sampleCount) const noexcept;
    };

    /**
     * Generates mip level @p level of @p chain from level-1 by deterministic area-box reduction,
     * dispatching on @p format's exact storage layout (SNORM, float/half, sRGB straight-RGBA8, or
     * generic packed UNORM). Shared by SkiaTextureBackend and SkiaRenderTargetBackend (SKIA-142)
     * so a render target's generated mips use exactly the same algorithm as a texture's.
     */
    void GenerateFormattedSkiaMipLevel(
        Microsoft::Xna::Framework::Graphics::SurfaceFormat format, std::size_t bytesPerTexel,
        SkiaMipChain2D& chain, int level);
} // namespace CNA::Internal::Backends::Skia
