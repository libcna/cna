// SPDX-License-Identifier: MS-PL
// SKIA-51: table audit for the direct, source-alpha-labelled BlendState mappings.  It exercises
// every current XNA Blend and BlendFunction ordinal without creating an SDL window.

#include "CNA/Internal/Backends/Skia/SkiaBlendMapping.hpp"

#include <cstdio>
#include <string>

using namespace CNA::Internal::Backends::Skia;

namespace
{
    int failures = 0;

    void Check(bool condition, const char* label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
        if (!condition) ++failures;
    }

    const SkiaBlendMapping* Find(int colorSource, int alphaSource, int colorDestination,
                                 int alphaDestination, int colorFunction = 0, int alphaFunction = 0)
    {
        return FindSkiaDirectBlendMapping(colorSource, alphaSource, colorDestination,
                                          alphaDestination, colorFunction, alphaFunction);
    }
}

int main()
{
    const auto* opaque = Find(0, 0, 1, 1);
    const auto* alphaBlend = Find(0, 0, 5, 5);
    const auto* nonPremultiplied = Find(4, 4, 5, 5);
    const auto* additive = Find(4, 4, 0, 0);
    Check(opaque && opaque->mode == SkBlendMode::kSrc
              && opaque->sourceAlphaConvention == SkiaSourceAlphaConvention::Premultiplied,
          "Opaque maps to kSrc with premultiplied source bytes");
    Check(alphaBlend && alphaBlend->mode == SkBlendMode::kSrcOver
              && alphaBlend->sourceAlphaConvention == SkiaSourceAlphaConvention::Premultiplied,
          "AlphaBlend maps to kSrcOver with premultiplied source bytes");
    Check(nonPremultiplied && nonPremultiplied->mode == SkBlendMode::kSrcOver
              && nonPremultiplied->sourceAlphaConvention == SkiaSourceAlphaConvention::Straight,
          "NonPremultiplied maps to kSrcOver with straight source bytes");
    Check(additive && additive->mode == SkBlendMode::kPlus
              && additive->sourceAlphaConvention == SkiaSourceAlphaConvention::Straight,
          "Additive maps to kPlus with straight source bytes");

    bool blendCoverageCorrect = true;
    int checkedBlendValues = 0;
    for (int blend = 0; blend <= 12; ++blend)
    {
        // Change each of the four XNA factor positions individually from the Opaque tuple.  Only
        // the original One/Zero values remain an exact known-convention mapping in that shape.
        blendCoverageCorrect = blendCoverageCorrect && (Find(blend, 0, 1, 1) != nullptr) == (blend == 0);
        blendCoverageCorrect = blendCoverageCorrect && (Find(0, blend, 1, 1) != nullptr) == (blend == 0);
        blendCoverageCorrect = blendCoverageCorrect && (Find(0, 0, blend, 1) != nullptr) == (blend == 1);
        blendCoverageCorrect = blendCoverageCorrect && (Find(0, 0, 1, blend) != nullptr) == (blend == 1);
        checkedBlendValues += 4;
    }
    Check(blendCoverageCorrect && checkedBlendValues == 52,
          "all 13 Blend values are deterministically accepted or rejected in every factor position");

    bool functionCoverageCorrect = true;
    int checkedFunctionPairs = 0;
    for (int colorFunction = 0; colorFunction <= 4; ++colorFunction)
    {
        for (int alphaFunction = 0; alphaFunction <= 4; ++alphaFunction)
        {
            functionCoverageCorrect = functionCoverageCorrect
                && (Find(0, 0, 1, 1, colorFunction, alphaFunction) != nullptr)
                    == (colorFunction == 0 && alphaFunction == 0);
            ++checkedFunctionPairs;
        }
    }
    Check(functionCoverageCorrect && checkedFunctionPairs == 25,
          "all 5 BlendFunction values are deterministically rejected outside Add/Add");

    const std::string message = DescribeUnsupportedSkiaBlendState(6, 8, 1, 5, 1, 2);
    Check(message.find("DestinationColor") != std::string::npos
              && message.find("DestinationAlpha") != std::string::npos
              && message.find("InverseSourceAlpha") != std::string::npos
              && message.find("Subtract") != std::string::npos
              && message.find("ReverseSubtract") != std::string::npos,
          "unsupported mapping message names every requested factor and function");

    std::printf("=== %d/%d PASS ===\n", 7 - failures, 7);
    return failures == 0 ? 0 : 1;
}
