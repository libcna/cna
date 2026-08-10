// SPDX-License-Identifier: MS-PL
// SKIA-129: display-free TextureFilter decomposition and affine LOD selection oracle.

#include "CNA/Internal/Renderers/Skia/SkiaMipSampling.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>

using namespace CNA::Internal::Renderers::Skia;

namespace
{
    int failures = 0;

    void Check(bool pass, const char* label)
    {
        std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", label);
        if (!pass) ++failures;
    }

    [[nodiscard]] bool Near(float actual, float expected, float tolerance = 1.0e-5f)
    {
        return std::abs(actual - expected) <= tolerance;
    }
}

int main()
{
    using Within = SkiaWithinLevelFilter;
    using Mip = SkiaMipLevelFilter;
    constexpr std::array<SkiaMipFilter, 9> expected {{
        SkiaMipFilter{Within::Linear, Within::Linear, Mip::Linear},
        SkiaMipFilter{Within::Point, Within::Point, Mip::Point},
        SkiaMipFilter{Within::Linear, Within::Linear, Mip::Linear},
        SkiaMipFilter{Within::Linear, Within::Linear, Mip::Point},
        SkiaMipFilter{Within::Point, Within::Point, Mip::Linear},
        SkiaMipFilter{Within::Linear, Within::Point, Mip::Linear},
        SkiaMipFilter{Within::Linear, Within::Point, Mip::Point},
        SkiaMipFilter{Within::Point, Within::Linear, Mip::Linear},
        SkiaMipFilter{Within::Point, Within::Linear, Mip::Point},
    }};
    for (int ordinal = 0; ordinal < static_cast<int>(expected.size()); ++ordinal)
    {
        const SkiaMipFilter actual = DecodeSkiaMipFilter(ordinal);
        const SkiaMipFilter want = expected[static_cast<std::size_t>(ordinal)];
        Check(actual.minification == want.minification
                  && actual.magnification == want.magnification
                  && actual.mip == want.mip,
              "TextureFilter ordinal has the authoritative min/mag/mip decomposition");
    }
    bool invalidLow = false;
    bool invalidHigh = false;
    try { (void)DecodeSkiaMipFilter(-1); } catch (const std::invalid_argument&) { invalidLow = true; }
    try { (void)DecodeSkiaMipFilter(9); } catch (const std::invalid_argument&) { invalidHigh = true; }
    Check(invalidLow && invalidHigh, "raw TextureFilter ordinals outside 0..8 reject");

    SkMatrix matrix;
    matrix.setIdentity();
    Check(Near(SkiaTexelRate(matrix), 1.0f), "identity is one source texel per device pixel");
    matrix.setScale(4.0f, 2.0f);
    Check(Near(SkiaTexelRate(matrix), 1.0f), "magnification clamps the LOD footprint to one");
    matrix.setScale(0.25f, 0.5f);
    Check(Near(SkiaTexelRate(matrix), 4.0f), "non-uniform minification selects the largest footprint");
    matrix.setAll(0.0f, -0.25f, 17.0f,
                  0.25f, 0.0f, -9.0f,
                  0.0f, 0.0f, 1.0f);
    Check(Near(SkiaTexelRate(matrix), 4.0f), "rotation and translation preserve a four-texel footprint");
    matrix.setScale(0.0f, 1.0f);
    Check(Near(SkiaTexelRate(matrix), 1.0f), "degenerate affine transforms stay on level zero");

    const SkiaMipFilter point = DecodeSkiaMipFilter(1);
    const SkiaMipFilter linear = DecodeSkiaMipFilter(0);
    const SkiaMipSelection magnified = SelectSkiaMipLevels(point, 4, 0.25f);
    Check(magnified.magnifying && magnified.lowerLevel == 0 && magnified.upperLevel == 0
              && magnified.withinLevel == Within::Point,
          "magnification selects level zero and the filter's mag component");

    for (int ordinal = 0; ordinal < static_cast<int>(expected.size()); ++ordinal)
    {
        const SkiaMipSelection exact = SelectSkiaMipLevels(
            DecodeSkiaMipFilter(ordinal), 4, 4.0f);
        Check(!exact.magnifying && exact.lowerLevel == 2 && exact.upperLevel == 2
                  && exact.upperWeight == 0.0f
                  && exact.withinLevel == expected[static_cast<std::size_t>(ordinal)].minification,
              "integer lambda selects one exact level and retains the minification component");
    }

    const float halfLevelRate = std::sqrt(2.0f);
    const SkiaMipSelection lowerTie = SelectSkiaMipLevels(point, 4, halfLevelRate);
    const SkiaMipSelection aboveTie = SelectSkiaMipLevels(
        point, 4, std::nextafter(halfLevelRate, std::numeric_limits<float>::infinity()));
    Check(lowerTie.lowerLevel == 0 && aboveTie.lowerLevel == 1,
          "nearest mip resolves an exact half-level downward and the next value upward");

    const SkiaMipSelection interpolated = SelectSkiaMipLevels(
        linear, 4, std::exp2(1.25f));
    Check(interpolated.lowerLevel == 1 && interpolated.upperLevel == 2
              && Near(interpolated.upperWeight, 0.25f),
          "linear mip selection brackets adjacent levels with the fractional lambda");
    const SkiaMipSelection clamped = SelectSkiaMipLevels(linear, 4, 65536.0f);
    Check(clamped.lowerLevel == 3 && clamped.upperLevel == 3 && clamped.upperWeight == 0.0f,
          "oversized minification clamps to the resource's final level");
    const SkiaMipSelection nonFinite = SelectSkiaMipLevels(
        linear, 4, std::numeric_limits<float>::quiet_NaN());
    Check(nonFinite.magnifying && nonFinite.lowerLevel == 0,
          "non-finite footprints deterministically select level zero");
    bool rejectedEmpty = false;
    try { (void)SelectSkiaMipLevels(linear, 0, 1.0f); }
    catch (const std::invalid_argument&) { rejectedEmpty = true; }
    Check(rejectedEmpty, "an empty mip chain rejects before selection");

    return failures == 0 ? 0 : 1;
}
