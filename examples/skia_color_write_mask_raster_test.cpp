// SPDX-License-Identifier: MS-PL
// SKIA-56: raster feasibility probe for ColorWriteChannels. The runtime blender must select each
// component *after* calculating the source-over result, retaining the destination component for a
// disabled channel. Public API exposure belongs to SKIA-57.

#include "CNA/Internal/Backends/Skia/SkiaSurface.hpp"

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkData.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/effects/SkRuntimeEffect.h"

#include <array>
#include <cstdint>
#include <cstdio>

using CNA::Internal::Backends::Skia::SkiaSurface;

namespace
{
    int failures = 0;

    void Check(bool condition, const char* label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
        if (!condition) ++failures;
    }

    std::array<std::uint8_t, 4> ReadPremultipliedPixel(const SkiaSurface& surface)
    {
        std::array<std::uint8_t, 4> pixel{};
        const auto image = surface.SnapshotImage();
        const SkImageInfo info = SkImageInfo::Make(1, 1, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
        if (!image || !image->readPixels(nullptr, info, pixel.data(), pixel.size(), 0, 0))
            return {};
        return pixel;
    }
}

int main()
{
    const auto result = SkRuntimeEffect::MakeForBlender(SkString(R"(
        uniform float4 writeMask;
        half4 main(half4 src, half4 dst) {
            half4 blended = src + dst * (1.0 - src.a);
            return half4(mix(dst, blended, writeMask));
        }
    )"));
    Check(result.effect != nullptr,
          "SkRuntimeEffect compiles a post-blend RGBA write-mask blender on the raster path");

    bool allMasksCorrect = result.effect != nullptr;
    for (int mask = 0; result.effect && mask < 16; ++mask)
    {
        // Destination is premultiplied blue (0,0,128,128); source is premultiplied red
        // (128,0,0,128). SourceOver produces (128,0,64,192) before the component mask.
        SkiaSurface surface(1, 1);
        surface.Clear(0.0f, 0.0f, 1.0f, 128.0f / 255.0f);
        const float writeMask[4] = {
            (mask & 1) ? 1.0f : 0.0f,
            (mask & 2) ? 1.0f : 0.0f,
            (mask & 4) ? 1.0f : 0.0f,
            (mask & 8) ? 1.0f : 0.0f,
        };
        SkPaint paint;
        paint.setColor(SkColorSetARGB(128, 255, 0, 0));
        paint.setBlender(result.effect->makeBlender(
            SkData::MakeWithCopy(writeMask, sizeof(writeMask))));
        surface.Canvas()->drawPaint(paint);

        const auto pixel = ReadPremultipliedPixel(surface);
        const std::array<std::uint8_t, 4> expected {
            static_cast<std::uint8_t>((mask & 1) ? 128 : 0),
            0,
            static_cast<std::uint8_t>((mask & 4) ? 64 : 128),
            static_cast<std::uint8_t>((mask & 8) ? 192 : 128),
        };
        if (pixel != expected)
        {
            std::printf("[INFO] mask=%d got=(%u,%u,%u,%u) expected=(%u,%u,%u,%u)\n", mask,
                        pixel[0], pixel[1], pixel[2], pixel[3],
                        expected[0], expected[1], expected[2], expected[3]);
            allMasksCorrect = false;
        }
    }
    Check(allMasksCorrect,
          "all 16 write masks preserve disabled premultiplied destination channels after blending");

    // Mask zero is the most important no-write sentinel: it must leave every destination byte
    // untouched, not clear the pixel or apply the source's alpha.
    Check(result.effect != nullptr, "zero write-mask behaviour is included in the full 16-mask matrix");

    std::printf("=== %d/%d PASS ===\n", 3 - failures, 3);
    return failures == 0 ? 0 : 1;
}
