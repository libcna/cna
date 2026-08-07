// SPDX-License-Identifier: MS-PL
// SKIA-93: headless feasibility spike for the fragment-like portions of CNA's stock effects.
// This deliberately stays below the public stock-effect API: AlphaTestEffect and
// DualTextureEffect also require vertex processing, primitive coverage, fog and depth semantics.

#include "CNA/Internal/Backends/Skia/SkiaSurface.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"

#include "include/core/SkCanvas.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkPaint.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkShader.h"
#include "include/core/SkTileMode.h"
#include "include/effects/SkRuntimeEffect.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using CNA::Internal::Backends::Skia::SkiaSurface;
using Microsoft::Xna::Framework::Graphics::CompareFunction;

namespace
{
    int failures = 0;

    void Check(bool condition, const char* label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
        if (!condition) ++failures;
    }

    [[nodiscard]] bool Near(std::uint8_t actual, int expected, int tolerance = 1)
    {
        return std::abs(static_cast<int>(actual) - expected) <= tolerance;
    }

    [[nodiscard]] sk_sp<SkImage> MakeImage(
        int width, int height, const std::uint8_t* rgba, std::size_t byteCount)
    {
        if (!rgba || byteCount != static_cast<std::size_t>(width) * height * 4u)
            return nullptr;
        SkiaSurface source(width, height);
        if (!source.WritePixels(0, 0, width, height, rgba, width * 4))
            return nullptr;
        return source.SnapshotImage();
    }

    struct AlphaTestUniform
    {
        float value[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    };

    [[nodiscard]] AlphaTestUniform MakeAlphaTestUniform(CompareFunction function, int referenceAlpha)
    {
        AlphaTestUniform result;
        const float reference = static_cast<float>(referenceAlpha) / 255.0f;
        constexpr float threshold = 0.5f / 255.0f;
        switch (function)
        {
            case CompareFunction::Less:
                result.value[0] = reference - threshold;
                result.value[2] = 1.0f;
                result.value[3] = -1.0f;
                break;
            case CompareFunction::LessEqual:
                result.value[0] = reference + threshold;
                result.value[2] = 1.0f;
                result.value[3] = -1.0f;
                break;
            case CompareFunction::GreaterEqual:
                result.value[0] = reference - threshold;
                result.value[2] = -1.0f;
                result.value[3] = 1.0f;
                break;
            case CompareFunction::Greater:
                result.value[0] = reference + threshold;
                result.value[2] = -1.0f;
                result.value[3] = 1.0f;
                break;
            case CompareFunction::Equal:
                result.value[0] = reference;
                result.value[1] = threshold;
                result.value[2] = 1.0f;
                result.value[3] = -1.0f;
                break;
            case CompareFunction::NotEqual:
                result.value[0] = reference;
                result.value[1] = threshold;
                result.value[2] = -1.0f;
                result.value[3] = 1.0f;
                break;
            case CompareFunction::Never:
                result.value[2] = -1.0f;
                result.value[3] = -1.0f;
                break;
            case CompareFunction::Always:
            default:
                break;
        }
        return result;
    }

    [[nodiscard]] bool AlphaPasses(CompareFunction function, int alpha, int reference)
    {
        switch (function)
        {
            case CompareFunction::Always: return true;
            case CompareFunction::Never: return false;
            case CompareFunction::Less: return alpha < reference;
            case CompareFunction::LessEqual: return alpha <= reference;
            case CompareFunction::Equal: return alpha == reference;
            case CompareFunction::NotEqual: return alpha != reference;
            case CompareFunction::GreaterEqual: return alpha >= reference;
            case CompareFunction::Greater: return alpha > reference;
        }
        return true;
    }

    [[nodiscard]] bool IsPixel(
        const std::vector<std::uint8_t>& pixels, std::size_t pixel,
        const std::array<std::uint8_t, 4>& expected)
    {
        const std::size_t offset = pixel * 4u;
        return offset <= pixels.size() && expected.size() <= pixels.size() - offset
            && std::equal(expected.begin(), expected.end(), pixels.begin() + offset);
    }
}

int main()
{
    constexpr std::array<CompareFunction, 8> functions = {
        CompareFunction::Always,
        CompareFunction::Never,
        CompareFunction::Less,
        CompareFunction::LessEqual,
        CompareFunction::Equal,
        CompareFunction::NotEqual,
        CompareFunction::GreaterEqual,
        CompareFunction::Greater,
    };

    const auto alphaMask = SkRuntimeEffect::MakeForShader(SkString(R"(
        uniform shader texture0;
        uniform float4 alphaTest;
        half4 main(float2 sourcePixel) {
            half4 color = texture0.eval(sourcePixel);
            bool selected = alphaTest.y > 0.0
                ? abs(color.a - alphaTest.x) < alphaTest.y
                : color.a < alphaTest.x;
            float weight = selected ? alphaTest.z : alphaTest.w;
            return weight < 0.0 ? half4(0.0) : half4(1.0);
        }
    )"));
    Check(alphaMask.effect != nullptr,
          "alpha-test coverage mask compiles for the pinned raster Skia");

    constexpr std::array<std::uint8_t, 12> alphaSource = {
        255, 255, 255, 127,
        255, 255, 255, 128,
        255, 255, 255, 129,
    };
    const sk_sp<SkImage> alphaImage = MakeImage(3, 1, alphaSource.data(), alphaSource.size());
    SkiaSurface alphaSurface(3, static_cast<int>(functions.size()));
    constexpr std::array<std::uint8_t, 4> sentinel = {17, 34, 51, 255};
    alphaSurface.Clear(
        sentinel[0] / 255.0f, sentinel[1] / 255.0f,
        sentinel[2] / 255.0f, sentinel[3] / 255.0f);

    bool alphaInstantiationSucceeded = alphaMask.effect && alphaImage;
    for (std::size_t row = 0; alphaInstantiationSucceeded && row < functions.size(); ++row)
    {
        SkAutoCanvasRestore restore(alphaSurface.Canvas(), true);
        alphaSurface.Canvas()->clipRect(
            SkRect::MakeXYWH(0.0f, static_cast<float>(row), 3.0f, 1.0f),
            SkClipOp::kIntersect, false);

        const AlphaTestUniform uniform = MakeAlphaTestUniform(functions[row], 128);
        sk_sp<SkShader> maskChild = alphaImage->makeShader(
            SkTileMode::kClamp, SkTileMode::kClamp,
            SkSamplingOptions(SkFilterMode::kNearest));
        std::array<sk_sp<SkShader>, 1> maskChildren = {std::move(maskChild)};
        sk_sp<SkShader> mask = alphaMask.effect->makeShader(
            SkData::MakeWithCopy(&uniform, sizeof(uniform)),
            maskChildren.data(), maskChildren.size());
        if (!mask)
        {
            alphaInstantiationSucceeded = false;
            break;
        }
        alphaSurface.Canvas()->clipShader(std::move(mask));

        SkPaint content;
        content.setBlendMode(SkBlendMode::kSrc);
        content.setShader(alphaImage->makeShader(
            SkTileMode::kClamp, SkTileMode::kClamp,
            SkSamplingOptions(SkFilterMode::kNearest)));
        if (!content.getShader())
        {
            alphaInstantiationSucceeded = false;
            break;
        }
        alphaSurface.Canvas()->drawRect(
            SkRect::MakeXYWH(0.0f, static_cast<float>(row), 3.0f, 1.0f), content);
    }

    const std::vector<std::uint8_t> alphaPixels = alphaSurface.SnapshotRgba();
    bool allAlphaCasesMatch = alphaInstantiationSucceeded;
    constexpr std::array<int, 3> alphaValues = {127, 128, 129};
    for (std::size_t row = 0; row < functions.size(); ++row)
    {
        for (std::size_t column = 0; column < alphaValues.size(); ++column)
        {
            const bool pass = AlphaPasses(functions[row], alphaValues[column], 128);
            const std::array<std::uint8_t, 4> expected = pass
                ? std::array<std::uint8_t, 4>{255, 255, 255,
                    static_cast<std::uint8_t>(alphaValues[column])}
                : sentinel;
            allAlphaCasesMatch &= IsPixel(alphaPixels, row * 3u + column, expected);
        }
    }
    Check(allAlphaCasesMatch,
          "clipShader matches all 24 below/equal/above decisions for eight CompareFunction modes");

    SkiaSurface transparentControl(1, 1);
    transparentControl.Clear(
        sentinel[0] / 255.0f, sentinel[1] / 255.0f,
        sentinel[2] / 255.0f, sentinel[3] / 255.0f);
    SkPaint transparentPaint;
    transparentPaint.setColor(SK_ColorTRANSPARENT);
    transparentPaint.setBlendMode(SkBlendMode::kSrc);
    transparentControl.Canvas()->drawPaint(transparentPaint);
    Check(IsPixel(transparentControl.SnapshotRgba(), 0, {0, 0, 0, 0}),
          "transparent source is observably not discard under Opaque/kSrc blending");

    const auto dualTexture = SkRuntimeEffect::MakeForShader(SkString(R"(
        uniform shader texture0;
        uniform shader texture1;
        uniform float4 tint;
        half4 main(float2 sourcePixel) {
            half4 base = texture0.eval(sourcePixel);
            half4 overlay = texture1.eval(sourcePixel);
            base.rgb *= 2.0;
            return base * overlay * half4(tint);
        }
    )"));
    Check(dualTexture.effect != nullptr,
          "dual-texture RGB-doubling shader compiles with two independent image children");

    constexpr std::array<std::uint8_t, 8> basePixels = {
        64, 64, 64, 255,
        128, 128, 128, 255,
    };
    constexpr std::array<std::uint8_t, 8> overlayPixels = {
        64, 64, 64, 255,
        192, 192, 192, 255,
    };
    const sk_sp<SkImage> baseImage = MakeImage(2, 1, basePixels.data(), basePixels.size());
    const sk_sp<SkImage> overlayImage = MakeImage(2, 1, overlayPixels.data(), overlayPixels.size());
    SkiaSurface dualSurface(4, 1);
    dualSurface.Clear(0.0f, 0.0f, 0.0f, 1.0f);
    bool dualInstantiationSucceeded = dualTexture.effect && baseImage && overlayImage;
    if (dualInstantiationSucceeded)
    {
        std::array<sk_sp<SkShader>, 2> children = {
            baseImage->makeShader(
                SkTileMode::kRepeat, SkTileMode::kClamp,
                SkSamplingOptions(SkFilterMode::kNearest)),
            overlayImage->makeShader(
                SkTileMode::kMirror, SkTileMode::kClamp,
                SkSamplingOptions(SkFilterMode::kNearest)),
        };
        constexpr float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        SkPaint paint;
        paint.setBlendMode(SkBlendMode::kSrc);
        paint.setShader(dualTexture.effect->makeShader(
            SkData::MakeWithCopy(tint, sizeof(tint)), children.data(), children.size()));
        dualInstantiationSucceeded = paint.getShader() != nullptr;
        if (dualInstantiationSucceeded)
            dualSurface.Canvas()->drawRect(SkRect::MakeWH(4.0f, 1.0f), paint);
    }
    const std::vector<std::uint8_t> dualResult = dualSurface.SnapshotRgba();
    constexpr std::array<int, 4> dualExpected = {32, 193, 96, 64};
    bool dualMatches = dualInstantiationSucceeded && dualResult.size() == 16;
    for (std::size_t pixel = 0; dualMatches && pixel < dualExpected.size(); ++pixel)
    {
        const std::size_t offset = pixel * 4u;
        dualMatches &= Near(dualResult[offset], dualExpected[pixel])
            && Near(dualResult[offset + 1u], dualExpected[pixel])
            && Near(dualResult[offset + 2u], dualExpected[pixel])
            && dualResult[offset + 3u] == 255;
    }
    Check(dualMatches,
          "one draw matches the dual-texture *2 oracle with independent Repeat and Mirror children");

    const auto colorTransform = SkRuntimeEffect::MakeForColorFilter(SkString(R"(
        uniform float4 scale;
        uniform float4 bias;
        half4 main(half4 color) {
            return color.brga * half4(scale) + half4(bias) * color.a;
        }
    )"));
    Check(colorTransform.effect != nullptr,
          "a composable channel/scale/bias color filter compiles for the raster path");

    constexpr std::array<std::uint8_t, 8> colorInput = {
        20, 40, 60, 255,
        200, 100, 50, 255,
    };
    const sk_sp<SkImage> colorImage = MakeImage(2, 1, colorInput.data(), colorInput.size());
    SkiaSurface colorSurface(2, 1);
    colorSurface.Clear(0.0f, 0.0f, 0.0f, 1.0f);
    bool colorInstantiationSucceeded = colorTransform.effect && colorImage;
    if (colorInstantiationSucceeded)
    {
        constexpr float colorUniforms[8] = {
            0.5f, 1.0f, 0.25f, 1.0f,
            10.0f / 255.0f, 20.0f / 255.0f, 30.0f / 255.0f, 0.0f,
        };
        SkPaint paint;
        paint.setBlendMode(SkBlendMode::kSrc);
        paint.setShader(colorImage->makeShader(
            SkTileMode::kClamp, SkTileMode::kClamp,
            SkSamplingOptions(SkFilterMode::kNearest)));
        paint.setColorFilter(colorTransform.effect->makeColorFilter(
            SkData::MakeWithCopy(colorUniforms, sizeof(colorUniforms))));
        colorInstantiationSucceeded = paint.getShader() && paint.getColorFilter();
        if (colorInstantiationSucceeded)
            colorSurface.Canvas()->drawRect(SkRect::MakeWH(2.0f, 1.0f), paint);
    }
    const std::vector<std::uint8_t> colorResult = colorSurface.SnapshotRgba();
    const bool colorMatches = colorInstantiationSucceeded && colorResult.size() == 8
        && Near(colorResult[0], 40) && Near(colorResult[1], 40)
        && Near(colorResult[2], 40) && colorResult[3] == 255
        && Near(colorResult[4], 35) && Near(colorResult[5], 220)
        && Near(colorResult[6], 55) && colorResult[7] == 255;
    Check(colorMatches,
          "single-pass color filter matches both swizzle/scale/bias visual-oracle pixels");

    std::printf("=== %d/%d PASS ===\n", 8 - failures, 8);
    return failures == 0 ? 0 : 1;
}
