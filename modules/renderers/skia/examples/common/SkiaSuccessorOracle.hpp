#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace CnaTest::Skia
{
    enum class ByteOracle
    {
        Exact,
        BilinearRgbOne,
        CoverageEdgeOne,
    };

    struct ByteTolerance final
    {
        std::uint8_t rgb = 0;
        std::uint8_t alpha = 0;
        bool localized = false;
    };

    [[nodiscard]] inline constexpr ByteTolerance Tolerance(ByteOracle oracle) noexcept
    {
        switch (oracle)
        {
            case ByteOracle::Exact: return {0, 0, false};
            case ByteOracle::BilinearRgbOne: return {1, 0, true};
            case ByteOracle::CoverageEdgeOne: return {1, 1, true};
        }
        return {};
    }

    [[nodiscard]] inline bool MatchesBytePixel(
        const std::array<std::uint8_t, 4>& actual,
        const std::array<std::uint8_t, 4>& expected,
        ByteOracle oracle, bool insideDeclaredVarianceRegion = false) noexcept
    {
        ByteTolerance tolerance = Tolerance(oracle);
        if (tolerance.localized && !insideDeclaredVarianceRegion)
            tolerance = Tolerance(ByteOracle::Exact);
        for (std::size_t channel = 0; channel < actual.size(); ++channel)
        {
            const int delta = std::abs(static_cast<int>(actual[channel])
                                     - static_cast<int>(expected[channel]));
            const int allowed = channel == 3u ? tolerance.alpha : tolerance.rgb;
            if (delta > allowed)
                return false;
        }
        return true;
    }

    enum class FloatOracle
    {
        TransferBits,
        ShaderArithmetic,
    };

    inline constexpr float kShaderAbsoluteTolerance = 1.0e-6f;
    inline constexpr float kShaderRelativeTolerance = 1.0e-5f;

    [[nodiscard]] inline bool MatchesFloat(float actual, float expected,
                                           FloatOracle oracle) noexcept
    {
        if (oracle == FloatOracle::TransferBits)
            return std::memcmp(&actual, &expected, sizeof(float)) == 0;
        if (!std::isfinite(actual) || !std::isfinite(expected))
            return false;
        const float allowed = std::max(kShaderAbsoluteTolerance,
                                       std::abs(expected) * kShaderRelativeTolerance);
        return std::abs(actual - expected) <= allowed;
    }

    enum SuccessorFeature : std::uint32_t
    {
        Mipmap = 1u << 0u,
        SurfaceFormat = 1u << 1u,
        Blend = 1u << 2u,
        Effect = 1u << 3u,
        CubeSampling = 1u << 4u,
        VolumeSampling = 1u << 5u,
        RenderTarget = 1u << 6u,
        Ganesh = 1u << 7u,
        Msaa = 1u << 8u,
        Anisotropy = 1u << 9u,
        Mrt = 1u << 10u,
    };

    inline constexpr std::uint32_t kAllSuccessorFeatures =
        Mipmap | SurfaceFormat | Blend | Effect | CubeSampling | VolumeSampling | RenderTarget
        | Ganesh | Msaa | Anisotropy | Mrt;

    struct CrossFeatureScene final
    {
        const char* id;
        std::uint32_t features;
        ByteOracle byteOracle;
        FloatOracle floatOracle;
    };

    /** Minimal scenes that successor feature tests extend rather than replacing with broad diffs. */
    inline constexpr std::array<CrossFeatureScene, 8> kCrossFeatureScenes{{
        {"odd-mip-format-filter", Mipmap | SurfaceFormat | RenderTarget,
         ByteOracle::BilinearRgbOne, FloatOracle::TransferBits},
        {"translucent-blend-target-format", Blend | RenderTarget | SurfaceFormat,
         ByteOracle::Exact, FloatOracle::TransferBits},
        {"effect-mip-format", Effect | Mipmap | SurfaceFormat,
         ByteOracle::BilinearRgbOne, FloatOracle::ShaderArithmetic},
        {"cube-seam-format-mip", CubeSampling | SurfaceFormat | Mipmap,
         ByteOracle::BilinearRgbOne, FloatOracle::TransferBits},
        {"volume-slice-format-mip", VolumeSampling | SurfaceFormat | Mipmap,
         ByteOracle::BilinearRgbOne, FloatOracle::TransferBits},
        {"ganesh-msaa-target-parity", Ganesh | Msaa | RenderTarget,
         ByteOracle::CoverageEdgeOne, FloatOracle::ShaderArithmetic},
        {"ganesh-anisotropy-mips", Ganesh | Anisotropy | Mipmap,
         ByteOracle::BilinearRgbOne, FloatOracle::ShaderArithmetic},
        {"mrt-effect-blend-target", Mrt | Effect | Blend | RenderTarget,
         ByteOracle::Exact, FloatOracle::ShaderArithmetic},
    }};
} // namespace CnaTest::Skia
