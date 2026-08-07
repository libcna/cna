// SPDX-License-Identifier: MS-PL
// SKIA-118: display-free proof of shared successor limits, checked arithmetic and oracle policy.

#include "CNA/Internal/Backends/Skia/SkiaResourcePolicy.hpp"
#include "common/SkiaSuccessorOracle.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string_view>

using namespace CNA::Internal::Backends::Skia;
using namespace CnaTest::Skia;

namespace
{
    int failures = 0;

    void Check(bool pass, const char* label)
    {
        std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", label);
        if (!pass) ++failures;
    }

    int SetBitCount(std::uint32_t value)
    {
        int count = 0;
        while (value != 0u)
        {
            count += static_cast<int>(value & 1u);
            value >>= 1u;
        }
        return count;
    }
}

int main()
{
    Check(kSkiaCpuTextureMaximumAxis == 16384
              && kSkiaCpuTextureStorageLimitBytes == 256u * 1024u * 1024u,
          "all successor CPU resources share the 16384-axis and 256-MiB ceilings");
    Check(kSkiaSkslMaxSourceBytesEXT == 64u * 1024u
              && kSkiaSkslMaxUniformBytesEXT == 16u * 1024u
              && kSkiaSkslMaxUniformCountEXT == 64u
              && kSkiaSkslMaxTextureUnitEXT == 7,
          "SkSL source, uniform-byte, uniform-count and child limits have one policy source");
    Check(IsValidSkiaResourceAxis(1) && IsValidSkiaResourceAxis(16384)
              && !IsValidSkiaResourceAxis(0) && !IsValidSkiaResourceAxis(16385),
          "axis boundary accepts exactly 1..16384");

    std::size_t result = 0;
    Check(CheckedTexelBytes2D(7, 5, 4, result) && result == 140,
          "2D byte accounting preserves odd dimensions exactly");
    Check(CheckedTexelBytes3D(7, 5, 3, 8, result) && result == 840,
          "3D byte accounting includes depth and format width exactly");
    const std::size_t sentinel = 0xA5A5u;
    result = sentinel;
    Check(!CheckedSizeMultiply(std::numeric_limits<std::size_t>::max(), 2u, result)
              && result == sentinel,
          "multiply overflow rejects without publishing a partial result");
    result = sentinel;
    Check(!CheckedSizeAdd(std::numeric_limits<std::size_t>::max(), 1u, result)
              && result == sentinel,
          "addition overflow rejects without publishing a partial result");
    result = sentinel;
    Check(!CheckedTexelBytes3D(std::numeric_limits<std::size_t>::max(), 2u, 2u, 16u,
                               result)
              && result == sentinel,
          "multi-axis overflow rejects without publishing a partial result");
    result = sentinel;
    Check(!CheckedSkiaResourceAccumulate(kSkiaCpuTextureStorageLimitBytes, 1u, result)
              && result == sentinel,
          "over-budget accumulation is atomic");
    Check(CheckedSkiaResourceAccumulate(kSkiaCpuTextureStorageLimitBytes - 1u, 1u, result)
              && result == kSkiaCpuTextureStorageLimitBytes,
          "the exact per-resource byte ceiling remains accepted");

    const std::array<std::uint8_t, 4> reference{100, 120, 140, 200};
    Check(MatchesBytePixel(reference, reference, ByteOracle::Exact)
              && !MatchesBytePixel({101, 120, 140, 200}, reference, ByteOracle::Exact),
          "transfer, readback and point-sampling pixels are byte exact");
    Check(MatchesBytePixel({101, 119, 141, 200}, reference,
                           ByteOracle::BilinearRgbOne, true)
              && !MatchesBytePixel({101, 119, 141, 201}, reference,
                                   ByteOracle::BilinearRgbOne, true)
              && !MatchesBytePixel({101, 120, 140, 200}, reference,
                                   ByteOracle::BilinearRgbOne, false),
          "bilinear variance is RGB-only, at most one, and localized");
    Check(MatchesBytePixel({99, 121, 139, 201}, reference,
                           ByteOracle::CoverageEdgeOne, true)
              && !MatchesBytePixel({99, 121, 139, 201}, reference,
                                   ByteOracle::CoverageEdgeOne, false),
          "coverage variance is one byte and restricted to enumerated edge pixels");

    const float one = 1.0f;
    const float adjacent = std::nextafter(one, 2.0f);
    Check(MatchesFloat(one, one, FloatOracle::TransferBits)
              && !MatchesFloat(adjacent, one, FloatOracle::TransferBits),
          "float and half transfer oracles require exact stored bits");
    Check(MatchesFloat(100.0009f, 100.0f, FloatOracle::ShaderArithmetic)
              && !MatchesFloat(100.01f, 100.0f, FloatOracle::ShaderArithmetic)
              && !MatchesFloat(std::numeric_limits<float>::quiet_NaN(), 0.0f,
                               FloatOracle::ShaderArithmetic),
          "finite shader arithmetic uses the bounded absolute/relative tolerance only");

    bool uniqueScenes = true;
    bool everySceneCrossesFeatures = true;
    std::uint32_t coveredFeatures = 0u;
    for (std::size_t left = 0; left < kCrossFeatureScenes.size(); ++left)
    {
        coveredFeatures |= kCrossFeatureScenes[left].features;
        everySceneCrossesFeatures = everySceneCrossesFeatures
            && SetBitCount(kCrossFeatureScenes[left].features) >= 2;
        for (std::size_t right = left + 1u; right < kCrossFeatureScenes.size(); ++right)
        {
            if (std::string_view(kCrossFeatureScenes[left].id)
                == std::string_view(kCrossFeatureScenes[right].id))
            {
                uniqueScenes = false;
            }
        }
    }
    Check(uniqueScenes && everySceneCrossesFeatures
              && coveredFeatures == kAllSuccessorFeatures,
          "eight unique cross-feature scenes cover every successor family without single-feature gaps");

    return failures == 0 ? 0 : 1;
}
