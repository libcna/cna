// SPDX-License-Identifier: MS-PL
// SKIA-119: raw working-colour probes for every source storage and blend convention route.

#include "CNA/Internal/Backends/Skia/SkiaBlendMapping.hpp"
#include "CNA/Internal/Backends/Skia/SkiaEffectBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaRenderTargetBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaSourceAlphaPolicy.hpp"
#include "CNA/Internal/Backends/Skia/SkiaSurface.hpp"
#include "CNA/Internal/Backends/Skia/SkiaTextureBackend.hpp"

#include "include/core/SkCanvas.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkShader.h"
#include "include/effects/SkRuntimeEffect.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

using namespace CNA::Internal::Backends::Skia;
using CNA::Internal::Graphics::ImageData;
using Microsoft::Xna::Framework::Color;

namespace
{
    int failures = 0;
    int checks = 0;

    void Check(bool pass, const char* label)
    {
        ++checks;
        std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", label);
        if (!pass) ++failures;
    }

    [[nodiscard]] bool Near(std::uint8_t actual, int expected, int tolerance = 1)
    {
        return std::abs(static_cast<int>(actual) - expected) <= tolerance;
    }

    [[nodiscard]] bool Near(float actual, float expected)
    {
        return std::abs(actual - expected) <= 1.0e-6f;
    }

    [[nodiscard]] std::array<std::uint8_t, 4> ReadWorkingPixel(sk_sp<SkImage> image)
    {
        if (!image)
            return {};
        SkiaSurface surface(1, 1);
        surface.Clear(0.0f, 0.0f, 0.0f, 0.0f);
        SkPaint paint;
        paint.setBlendMode(SkBlendMode::kSrc);
        surface.Canvas()->drawImage(image, 0.0f, 0.0f,
                                    SkSamplingOptions(SkFilterMode::kNearest), &paint);

        std::array<std::uint8_t, 4> pixel{};
        const sk_sp<SkImage> result = surface.SnapshotImage();
        const SkImageInfo info = SkImageInfo::Make(
            1, 1, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
        if (!result || !result->readPixels(nullptr, info, pixel.data(), pixel.size(), 0, 0))
            return {};
        return pixel;
    }

    [[nodiscard]] std::array<std::uint8_t, 4> ReadWorkingShader(sk_sp<SkShader> shader)
    {
        if (!shader)
            return {};
        SkiaSurface surface(1, 1);
        surface.Clear(0.0f, 0.0f, 0.0f, 0.0f);
        SkPaint paint;
        paint.setBlendMode(SkBlendMode::kSrc);
        paint.setShader(std::move(shader));
        surface.Canvas()->drawRect(SkRect::MakeWH(1.0f, 1.0f), paint);

        std::array<std::uint8_t, 4> pixel{};
        const sk_sp<SkImage> result = surface.SnapshotImage();
        const SkImageInfo info = SkImageInfo::Make(
            1, 1, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
        if (!result || !result->readPixels(nullptr, info, pixel.data(), pixel.size(), 0, 0))
            return {};
        return pixel;
    }
}

int main()
{
    Check(ResolveSkiaWorkingSourceRoute(SkiaSourceStorageAlpha::CanonicalRgbaBytes,
                                        SkiaSourceAlphaConvention::Premultiplied)
              == SkiaWorkingSourceRoute::PreserveDeclaredComponents
              && ResolveSkiaWorkingSourceRoute(SkiaSourceStorageAlpha::CanonicalRgbaBytes,
                                               SkiaSourceAlphaConvention::Straight)
                  == SkiaWorkingSourceRoute::PremultiplyStraightRgbOnce
              && ResolveSkiaWorkingSourceRoute(SkiaSourceStorageAlpha::PremultipliedSurface,
                                               SkiaSourceAlphaConvention::Straight)
                  == SkiaWorkingSourceRoute::ReusePremultipliedSurface,
          "storage and requested convention resolve through one exhaustive working-colour table");

    int premultipliedMappings = 0;
    int straightMappings = 0;
    bool mappingNamesPresent = true;
    for (const SkiaBlendMapping& mapping : kSkiaBlendMappings)
    {
        mappingNamesPresent = mappingNamesPresent && mapping.name && mapping.name[0] != '\0';
        if (mapping.sourceAlphaConvention == SkiaSourceAlphaConvention::Premultiplied)
            ++premultipliedMappings;
        else
            ++straightMappings;
    }
    Check(mappingNamesPresent && premultipliedMappings == 3 && straightMappings == 2,
          "every accepted blend tuple explicitly declares one source convention");

    const ImageData data{1, 1, {96, 32, 16, 128}};
    SkiaTextureBackend texture(data);
    Check(texture.StorageAlphaEXT() == SkiaSourceStorageAlpha::CanonicalRgbaBytes,
          "Texture2D identifies its exact public RGBA byte shadow");
    Check(ReadWorkingPixel(texture.SnapshotImage(SkiaSourceAlphaConvention::Premultiplied))
              == std::array<std::uint8_t, 4>{96, 32, 16, 128},
          "premultiplied-labelled Texture2D preserves all source components in Skia working bytes");
    Check(ReadWorkingPixel(texture.SnapshotImage(SkiaSourceAlphaConvention::Straight))
              == std::array<std::uint8_t, 4>{48, 16, 8, 128},
          "straight-labelled Texture2D multiplies RGB by source alpha exactly once");

    SkiaRenderTargetBackend target(1, 1, true, {}, {});
    const std::array<std::uint8_t, 4> targetSource{96, 32, 16, 128};
    target.UpdatePixels(targetSource.data(), 4);
    const auto targetPremultiplied = ReadWorkingPixel(
        target.SnapshotImage(SkiaSourceAlphaConvention::Premultiplied));
    const auto targetStraight = ReadWorkingPixel(
        target.SnapshotImage(SkiaSourceAlphaConvention::Straight));
    Check(target.StorageAlphaEXT() == SkiaSourceStorageAlpha::PremultipliedSurface
              && targetPremultiplied == std::array<std::uint8_t, 4>{48, 16, 8, 128}
              && targetStraight == targetPremultiplied,
          "RenderTarget2D reuses one already-premultiplied snapshot for either blend convention");

    const Color tint(128, 64, 32, 128);
    const SkiaTintScale premultipliedTint = MakeSkiaTintScale(
        tint, SkiaSourceAlphaConvention::Premultiplied);
    const SkiaTintScale straightTint = MakeSkiaTintScale(
        tint, SkiaSourceAlphaConvention::Straight);
    Check(Near(premultipliedTint.red, 128.0f / 255.0f)
              && Near(premultipliedTint.green, 64.0f / 255.0f)
              && Near(premultipliedTint.blue, 32.0f / 255.0f)
              && Near(premultipliedTint.alpha, 128.0f / 255.0f),
          "premultiplied source keeps tint RGB and alpha components independent");
    Check(Near(straightTint.red, (128.0f / 255.0f) * (128.0f / 255.0f))
              && Near(straightTint.green, (64.0f / 255.0f) * (128.0f / 255.0f))
              && Near(straightTint.blue, (32.0f / 255.0f) * (128.0f / 255.0f))
              && Near(straightTint.alpha, 128.0f / 255.0f),
          "straight source folds tint alpha into working RGB exactly once");

    SkiaEffectBackend effect;
    const bool compiled = effect.CompileProgram(std::string(kSkiaSkslSpriteEffectMarkerEXT), R"(
        uniform shader cnaTexture0;
        uniform float4 cnaTint;
        half4 main(float2 p) {
            half4 source = cnaTexture0.eval(p);
            return half4(source.rgb * cnaTint.rgb, source.a * cnaTint.a);
        }
    )");
    Check(compiled
              && kSkiaSkslEffectOutputAlphaConventionEXT
                  == SkiaSourceAlphaConvention::Premultiplied,
          "tagged custom effects have one explicit premultiplied output contract");
    if (compiled)
    {
        const ImageData effectData{1, 1, {64, 32, 16, 128}};
        SkiaTextureBackend effectTexture(effectData);
        sk_sp<SkShader> primary = effectTexture
            .SnapshotImage(SkiaSourceAlphaConvention::Premultiplied)
            ->makeShader(SkTileMode::kClamp, SkTileMode::kClamp,
                         SkSamplingOptions(SkFilterMode::kNearest));
        const SkiaTintScale effectTint = MakeSkiaTintScale(
            Color(128, 255, 128, 128), SkiaSourceAlphaConvention::Premultiplied);
        const float tintValues[4] = {
            effectTint.red, effectTint.green, effectTint.blue, effectTint.alpha,
        };
        const auto pixel = ReadWorkingShader(
            effect.MakeSpriteShaderEXT(std::move(primary), tintValues));
        Check(Near(pixel[0], 32) && Near(pixel[1], 32) && Near(pixel[2], 8)
                  && Near(pixel[3], 64),
              "custom effect output preserves the declared working source/tint components");
    }

    std::printf("=== %d/%d PASS ===\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
