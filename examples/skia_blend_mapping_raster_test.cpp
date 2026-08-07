// SPDX-License-Identifier: MS-PL
// SKIA-51/SKIA-55/SKIA-123: table audit for optimized mappings plus the bounded generated fallback.
// It exhaustively classifies the XNA Blend/BlendFunction matrix without creating an SDL window.

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
        return FindSkiaBlendMapping(colorSource, alphaSource, colorDestination,
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
    Check(nonPremultiplied
              && nonPremultiplied->route == SkiaBlendMappingRoute::RuntimeNonPremultiplied
              && nonPremultiplied->sourceAlphaConvention == SkiaSourceAlphaConvention::Straight,
          "NonPremultiplied selects a straight-source runtime route with independent XNA alpha");
    Check(additive && additive->route == SkiaBlendMappingRoute::RuntimeAdditive
              && additive->sourceAlphaConvention == SkiaSourceAlphaConvention::Straight,
          "Additive selects a straight-source runtime route with independent XNA alpha");
    const auto* destinationColor = Find(6, 0, 1, 1);
    Check(destinationColor
              && destinationColor->route == SkiaBlendMappingRoute::RuntimeDestinationColorPrototype
              && destinationColor->sourceAlphaConvention == SkiaSourceAlphaConvention::Premultiplied,
          "the one public custom tuple selects the pixel-proven runtime blender route");

    bool blendCoverageCorrect = true;
    int checkedBlendValues = 0;
    for (int blend = 0; blend <= 12; ++blend)
    {
        // Change each of the four XNA factor positions individually from the Opaque tuple. Only
        // the original One/Zero values retain an optimized established mapping in that shape.
        blendCoverageCorrect = blendCoverageCorrect
            && (Find(blend, 0, 1, 1) != nullptr) == (blend == 0 || blend == 6);
        blendCoverageCorrect = blendCoverageCorrect && (Find(0, blend, 1, 1) != nullptr) == (blend == 0);
        blendCoverageCorrect = blendCoverageCorrect && (Find(0, 0, blend, 1) != nullptr) == (blend == 1);
        blendCoverageCorrect = blendCoverageCorrect && (Find(0, 0, 1, blend) != nullptr) == (blend == 1);
        checkedBlendValues += 4;
    }
    Check(blendCoverageCorrect && checkedBlendValues == 52,
          "all 13 Blend values are deterministically optimized or deferred in every factor position");

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
          "all 5 BlendFunction values are deterministically deferred outside optimized Add/Add");

    bool exhaustiveCoverageCorrect = true;
    int checkedBlendStateTuples = 0;
    int establishedTuples = 0;
    int generatedTuples = 0;
    int invalidTuples = 0;
    for (int colorSource = 0; colorSource <= 12; ++colorSource)
    {
        for (int alphaSource = 0; alphaSource <= 12; ++alphaSource)
        {
            for (int colorDestination = 0; colorDestination <= 12; ++colorDestination)
            {
                for (int alphaDestination = 0; alphaDestination <= 12; ++alphaDestination)
                {
                    for (int colorFunction = 0; colorFunction <= 4; ++colorFunction)
                    {
                        for (int alphaFunction = 0; alphaFunction <= 4; ++alphaFunction)
                        {
                            const bool expected = colorFunction == 0 && alphaFunction == 0
                                && ((colorSource == 0 && alphaSource == 0
                                     && colorDestination == 1 && alphaDestination == 1)
                                    || (colorSource == 0 && alphaSource == 0
                                        && colorDestination == 5 && alphaDestination == 5)
                                    || (colorSource == 4 && alphaSource == 4
                                        && colorDestination == 5 && alphaDestination == 5)
                                    || (colorSource == 4 && alphaSource == 4
                                        && colorDestination == 0 && alphaDestination == 0)
                                    || (colorSource == 6 && alphaSource == 0
                                        && colorDestination == 1 && alphaDestination == 1));
                            const SkiaBlendSelectorDisposition disposition = ClassifySkiaBlendSelectors(
                                colorSource, alphaSource, colorDestination, alphaDestination,
                                colorFunction, alphaFunction);
                            establishedTuples += disposition
                                == SkiaBlendSelectorDisposition::EstablishedMapping;
                            generatedTuples += disposition == SkiaBlendSelectorDisposition::Generated;
                            invalidTuples += disposition == SkiaBlendSelectorDisposition::Invalid;
                            exhaustiveCoverageCorrect = exhaustiveCoverageCorrect
                                && (disposition == SkiaBlendSelectorDisposition::EstablishedMapping)
                                    == expected;
                            ++checkedBlendStateTuples;
                        }
                    }
                }
            }
        }
    }
    Check(exhaustiveCoverageCorrect && checkedBlendStateTuples == 714025
              && establishedTuples == 5 && generatedTuples == 714020 && invalidTuples == 0,
          "all 714025 valid tuples classify as five established or 714020 generated routes");

    Check(ClassifySkiaBlendSelectors(13, 0, 1, 1, 0, 0)
              == SkiaBlendSelectorDisposition::Invalid
              && ClassifySkiaBlendSelectors(0, 0, 1, 1, 5, 0)
                  == SkiaBlendSelectorDisposition::Invalid,
          "out-of-range factor and function ordinals classify as invalid before construction");

    const std::string message = DescribeUnsupportedSkiaBlendState(6, 8, 1, 5, 1, 2);
    Check(message.find("DestinationColor") != std::string::npos
              && message.find("DestinationAlpha") != std::string::npos
              && message.find("InverseSourceAlpha") != std::string::npos
              && message.find("Subtract") != std::string::npos
              && message.find("ReverseSubtract") != std::string::npos,
          "unsupported mapping message names every requested factor and function");

    std::printf("=== %d/%d PASS ===\n", 10 - failures, 10);
    return failures == 0 ? 0 : 1;
}
