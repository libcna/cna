// SPDX-License-Identifier: MS-PL
// SKIA-120: one cached runtime blender covers every factor/function selector independently.

#include "CNA/Internal/Backends/Skia/SkiaGeneratedBlender.hpp"
#include "CNA/Internal/Backends/Skia/SkiaSurface.hpp"
#include "CNA/Internal/Backends/Skia/SkiaTextureBackend.hpp"

#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkSamplingOptions.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>

using namespace CNA::Internal::Backends::Skia;
using CNA::Internal::Graphics::ImageData;

namespace
{
    using Float4 = std::array<float, 4>;
    using Byte4 = std::array<std::uint8_t, 4>;

    int failures = 0;
    int checks = 0;

    void Check(bool pass, const std::string& label)
    {
        ++checks;
        std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", label.c_str());
        if (!pass) ++failures;
    }

    [[nodiscard]] Float4 ToFloat(const Byte4& value)
    {
        return {value[0] / 255.0f, value[1] / 255.0f,
                value[2] / 255.0f, value[3] / 255.0f};
    }

    [[nodiscard]] Float4 Factor(int selector, const Float4& source,
                                const Float4& destination, const Float4& constant)
    {
        Float4 result{};
        for (std::size_t channel = 0; channel < result.size(); ++channel)
        {
            switch (selector)
            {
                case 0: result[channel] = 1.0f; break;
                case 1: result[channel] = 0.0f; break;
                case 2: result[channel] = source[channel]; break;
                case 3: result[channel] = 1.0f - source[channel]; break;
                case 4: result[channel] = source[3]; break;
                case 5: result[channel] = 1.0f - source[3]; break;
                case 6: result[channel] = destination[channel]; break;
                case 7: result[channel] = 1.0f - destination[channel]; break;
                case 8: result[channel] = destination[3]; break;
                case 9: result[channel] = 1.0f - destination[3]; break;
                case 10: result[channel] = constant[channel]; break;
                case 11: result[channel] = 1.0f - constant[channel]; break;
                case 12:
                    result[channel] = channel == 3u
                        ? 1.0f
                        : std::min(source[3], 1.0f - destination[3]);
                    break;
                default: break;
            }
        }
        return result;
    }

    [[nodiscard]] Float4 Apply(int function, const Float4& source,
                               const Float4& destination,
                               const Float4& weightedSource,
                               const Float4& weightedDestination)
    {
        Float4 result{};
        for (std::size_t channel = 0; channel < result.size(); ++channel)
        {
            switch (function)
            {
                case 0: result[channel] = weightedSource[channel] + weightedDestination[channel]; break;
                case 1: result[channel] = weightedSource[channel] - weightedDestination[channel]; break;
                case 2: result[channel] = weightedDestination[channel] - weightedSource[channel]; break;
                case 3: result[channel] = std::max(source[channel], destination[channel]); break;
                case 4: result[channel] = std::min(source[channel], destination[channel]); break;
                default: break;
            }
            result[channel] = std::clamp(result[channel], 0.0f, 1.0f);
        }
        return result;
    }

    [[nodiscard]] Float4 Reference(const SkiaGeneratedBlendSelectors& selectors,
                                   const Float4& source, const Float4& destination,
                                   const Float4& constant)
    {
        const Float4 colorSourceFactor = Factor(
            selectors.colorSourceFactor, source, destination, constant);
        const Float4 colorDestinationFactor = Factor(
            selectors.colorDestinationFactor, source, destination, constant);
        const Float4 alphaSourceFactor = Factor(
            selectors.alphaSourceFactor, source, destination, constant);
        const Float4 alphaDestinationFactor = Factor(
            selectors.alphaDestinationFactor, source, destination, constant);
        Float4 weightedColorSource{};
        Float4 weightedColorDestination{};
        Float4 weightedAlphaSource{};
        Float4 weightedAlphaDestination{};
        for (std::size_t channel = 0; channel < source.size(); ++channel)
        {
            weightedColorSource[channel] = source[channel] * colorSourceFactor[channel];
            weightedColorDestination[channel] = destination[channel] * colorDestinationFactor[channel];
            weightedAlphaSource[channel] = source[channel] * alphaSourceFactor[channel];
            weightedAlphaDestination[channel] = destination[channel] * alphaDestinationFactor[channel];
        }
        const Float4 color = Apply(selectors.colorFunction, source, destination,
                                   weightedColorSource, weightedColorDestination);
        const Float4 alpha = Apply(selectors.alphaFunction, source, destination,
                                   weightedAlphaSource, weightedAlphaDestination);
        return {color[0], color[1], color[2], alpha[3]};
    }

    [[nodiscard]] Byte4 Render(const SkiaGeneratedBlendSelectors& selectors,
                               const Byte4& sourceBytes, const Byte4& destinationBytes,
                               const Float4& constant, std::string& error)
    {
        sk_sp<SkBlender> blender = TryMakeSkiaGeneratedBlender(selectors, constant, 15, error);
        if (!blender)
            return {};
        SkiaTextureBackend source(ImageData{1, 1, {
            sourceBytes[0], sourceBytes[1], sourceBytes[2], sourceBytes[3],
        }});
        SkiaSurface surface(1, 1);
        surface.Clear(destinationBytes[0] / 255.0f, destinationBytes[1] / 255.0f,
                      destinationBytes[2] / 255.0f, destinationBytes[3] / 255.0f);
        SkPaint paint;
        paint.setBlender(std::move(blender));
        surface.Canvas()->drawImage(
            source.SnapshotImage(kSkiaGeneratedBlendSourceAlphaConvention), 0.0f, 0.0f,
            SkSamplingOptions(SkFilterMode::kNearest), &paint);
        const auto pixels = surface.SnapshotRgba();
        return pixels.size() == 4u
            ? Byte4{pixels[0], pixels[1], pixels[2], pixels[3]}
            : Byte4{};
    }

    [[nodiscard]] bool Matches(const Byte4& actual, const Float4& expected,
                               bool alphaOnly = false)
    {
        const int expectedAlpha = static_cast<int>(expected[3] * 255.0f + 0.5f);
        if (std::abs(static_cast<int>(actual[3]) - expectedAlpha) > 2)
            return false;
        if (alphaOnly || expectedAlpha == 0)
            return true;
        for (std::size_t channel = 0; channel < 3u; ++channel)
        {
            const int expectedByte = static_cast<int>(expected[channel] * 255.0f + 0.5f);
            if (std::abs(static_cast<int>(actual[channel]) - expectedByte) > 3)
                return false;
        }
        return true;
    }
}

int main()
{
    const Byte4 sourceBytes{153, 102, 51, 204};
    const Byte4 destinationBytes{64, 128, 32, 128};
    const Float4 source = ToFloat(sourceBytes);
    const Float4 destination = ToFloat(destinationBytes);
    const Float4 constant{0.25f, 0.5f, 0.75f, 0.6f};
    std::string error;

    SkiaGeneratedBlendSelectors selectors;
    const auto first = Render(selectors, sourceBytes, destinationBytes, constant, error);
    Check(error.empty() && SkiaGeneratedBlenderCompileCountEXT() == 1u
              && Matches(first, Reference(selectors, source, destination, constant)),
          "the generic runtime blender compiles once and instantiates its default selector tuple");

    for (int factor = 0; factor < kSkiaBlendFactorCount; ++factor)
    {
        selectors = {};
        selectors.colorSourceFactor = factor;
        selectors.colorDestinationFactor = 1; // Zero
        selectors.alphaSourceFactor = 0;      // One
        selectors.alphaDestinationFactor = 1;
        const Byte4 actual = Render(selectors, sourceBytes, destinationBytes, constant, error);
        Check(error.empty() && Matches(
                  actual, Reference(selectors, source, destination, constant)),
              "color source factor ordinal " + std::to_string(factor)
                  + " matches the independent scalar oracle");
    }

    for (int factor = 0; factor < kSkiaBlendFactorCount; ++factor)
    {
        selectors = {};
        selectors.colorSourceFactor = 0;
        selectors.colorDestinationFactor = 1;
        selectors.alphaSourceFactor = factor;
        selectors.alphaDestinationFactor = 0; // keep output alpha nonzero for Zero source factor
        const Byte4 actual = Render(selectors, sourceBytes, destinationBytes, constant, error);
        Check(error.empty() && Matches(
                  actual, Reference(selectors, source, destination, constant), true),
              "alpha source factor ordinal " + std::to_string(factor)
                  + " matches the independent scalar oracle");
    }

    for (int function = 0; function < kSkiaBlendFunctionCount; ++function)
    {
        selectors = {};
        selectors.colorSourceFactor = 0;
        selectors.colorDestinationFactor = 0;
        selectors.alphaSourceFactor = 0;
        selectors.alphaDestinationFactor = 0;
        selectors.colorFunction = function;
        selectors.alphaFunction = 0;
        const Byte4 actual = Render(selectors, sourceBytes, destinationBytes, constant, error);
        Check(error.empty() && Matches(
                  actual, Reference(selectors, source, destination, constant)),
              "color function ordinal " + std::to_string(function)
                  + " matches EasyGL scalar semantics independently from alpha Add");
    }

    for (int function = 0; function < kSkiaBlendFunctionCount; ++function)
    {
        selectors = {};
        selectors.colorSourceFactor = 0;
        selectors.colorDestinationFactor = 1;
        selectors.alphaSourceFactor = 0;
        selectors.alphaDestinationFactor = 0;
        selectors.colorFunction = 0;
        selectors.alphaFunction = function;
        const Byte4 actual = Render(selectors, sourceBytes, destinationBytes, constant, error);
        Check(error.empty() && Matches(
                  actual, Reference(selectors, source, destination, constant), true),
              "alpha function ordinal " + std::to_string(function)
                  + " matches EasyGL scalar semantics independently from color Add");
    }

    selectors = {12, 10, 11, 6, 2, 1};
    const Byte4 independent = Render(selectors, sourceBytes, destinationBytes, constant, error);
    Check(error.empty() && Matches(
              independent, Reference(selectors, source, destination, constant)),
          "source-alpha saturation, constant/inverse constant, reverse subtract and separate alpha compose");
    Check(SkiaGeneratedBlenderCompileCountEXT() == 1u,
          "all valid selector combinations reuse the one cached SkSL compilation");

    const struct InvalidSelector
    {
        const char* expectedName;
        void (*mutate)(SkiaGeneratedBlendSelectors&);
    } invalidSelectors[] = {
        {"color source factor", [](auto& value) { value.colorSourceFactor = 13; }},
        {"alpha source factor", [](auto& value) { value.alphaSourceFactor = -1; }},
        {"color destination factor", [](auto& value) { value.colorDestinationFactor = 13; }},
        {"alpha destination factor", [](auto& value) { value.alphaDestinationFactor = -1; }},
        {"color function", [](auto& value) { value.colorFunction = 5; }},
        {"alpha function", [](auto& value) { value.alphaFunction = -1; }},
    };
    for (const auto& invalid : invalidSelectors)
    {
        selectors = {};
        invalid.mutate(selectors);
        std::string firstError;
        std::string secondError;
        const auto firstInvalid = TryMakeSkiaGeneratedBlender(selectors, constant, 15, firstError);
        const auto secondInvalid = TryMakeSkiaGeneratedBlender(selectors, constant, 15, secondError);
        Check(!firstInvalid && !secondInvalid && firstError == secondError
                  && firstError.find(invalid.expectedName) != std::string::npos,
              std::string("invalid ") + invalid.expectedName
                  + " rejects deterministically before construction");
    }

    selectors = {};
    Float4 invalidConstant = constant;
    invalidConstant[2] = std::numeric_limits<float>::quiet_NaN();
    const auto invalid = TryMakeSkiaGeneratedBlender(selectors, invalidConstant, 15, error);
    Check(!invalid && error.find("constant channel 2") != std::string::npos,
          "non-finite blend constant rejects deterministically");

    std::printf("=== %d/%d PASS ===\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
