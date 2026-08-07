// SPDX-License-Identifier: MS-PL
// SKIA-53: prove the pinned raster Skia build accepts a custom blender with independent RGB and
// alpha equations.  This is intentionally below CNA's public API: SKIA-54 owns the first public
// BlendState prototype and its per-batch state capture.

#include "CNA/Internal/Backends/Skia/SkiaSurface.hpp"

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/effects/SkRuntimeEffect.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using CNA::Internal::Backends::Skia::SkiaSurface;

namespace
{
    int failures = 0;

    void Check(bool condition, const char* label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
        if (!condition) ++failures;
    }

    bool Near(std::uint8_t actual, int expected, int tolerance = 2)
    {
        return std::abs(static_cast<int>(actual) - expected) <= tolerance;
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
    const auto independent = SkRuntimeEffect::MakeForBlender(SkString(R"(
        half4 main(half4 src, half4 dst) {
            // RGB: src * dst-alpha + dst * (1 - src-alpha)
            // A:   src-alpha - dst-alpha
            return half4(src.rgb * dst.a + dst.rgb * (1.0 - src.a), src.a - dst.a);
        }
    )"));
    Check(independent.effect != nullptr,
          "SkRuntimeEffect compiles a two-input custom blender for the pinned raster Skia");

    if (independent.effect)
    {
        SkiaSurface surface(1, 1);
        // Destination is straight blue at alpha 64/255.  Skia stores it premultiplied.
        surface.Clear(0.0f, 0.0f, 1.0f, 64.0f / 255.0f);
        SkPaint paint;
        // Source is straight red at alpha 192/255.  The expected unpremultiplied result is
        // approximately (96, 0, 32, 128), proving RGB and alpha followed different equations.
        paint.setColor(SkColorSetARGB(192, 255, 0, 0));
        paint.setBlender(independent.effect->makeBlender(nullptr));
        surface.Canvas()->drawPaint(paint);
        const std::vector<std::uint8_t> pixel = surface.SnapshotRgba();
        Check(pixel.size() == 4 && Near(pixel[0], 96) && Near(pixel[1], 0)
                  && Near(pixel[2], 32) && Near(pixel[3], 128),
              "custom blender applies independent colour and alpha equations on a raster SkSurface");
    }

    const auto invalidPremul = SkRuntimeEffect::MakeForBlender(SkString(R"(
        half4 main(half4 src, half4 dst) {
            // A legal XNA separate-alpha result may retain RGB while setting alpha to zero.
            return half4(1.0, 0.0, 0.0, 0.25);
        }
    )"));
    Check(invalidPremul.effect != nullptr,
          "SkRuntimeEffect accepts a custom blender whose result is not premultiplied");

    if (invalidPremul.effect)
    {
        SkiaSurface surface(1, 1);
        surface.Clear(0.0f, 0.0f, 0.0f, 0.0f);
        SkPaint paint;
        paint.setColor(SK_ColorWHITE);
        paint.setBlender(invalidPremul.effect->makeBlender(nullptr));
        surface.Canvas()->drawPaint(paint);
        const auto pixel = ReadPremultipliedPixel(surface);
        Check(pixel[0] == 255 && pixel[1] == 0 && pixel[2] == 0 && pixel[3] >= 63 && pixel[3] <= 64,
              "raster runtime blender preserves an intentionally non-premultiplied RGBA result");
    }

    std::printf("=== %d/%d PASS ===\n", 4 - failures, 4);
    return failures == 0 ? 0 : 1;
}
