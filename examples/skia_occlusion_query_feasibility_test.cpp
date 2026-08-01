// SPDX-License-Identifier: MS-PL
// SKIA-104: prove that raster framebuffer differences cannot emulate samples-passed queries.

#include "CNA/Internal/Backends/Skia/SkiaSurface.hpp"

#include "include/core/SkBlendMode.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

using CNA::Internal::Backends::Skia::SkiaSurface;

namespace
{
    constexpr int kWidth = 8;
    constexpr int kHeight = 4;
    int failures = 0;

    void Check(bool condition, const char* label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
        if (!condition) ++failures;
    }

    void ClearSentinel(SkiaSurface& surface)
    {
        surface.Clear(17.0f / 255.0f, 34.0f / 255.0f, 51.0f / 255.0f, 1.0f);
    }

    [[nodiscard]] int CountChangedPixels(
        const std::vector<std::uint8_t>& before,
        const std::vector<std::uint8_t>& after)
    {
        if (before.size() != after.size() || before.size() % 4u != 0u)
            return -1;
        int changed = 0;
        for (std::size_t offset = 0; offset < before.size(); offset += 4u)
        {
            if (before[offset] != after[offset]
                || before[offset + 1] != after[offset + 1]
                || before[offset + 2] != after[offset + 2]
                || before[offset + 3] != after[offset + 3])
            {
                ++changed;
            }
        }
        return changed;
    }

    void DrawRect(SkiaSurface& surface, const SkRect& rect, SkColor color, SkBlendMode blend)
    {
        SkPaint paint;
        paint.setAntiAlias(false);
        paint.setColor(color);
        paint.setBlendMode(blend);
        surface.Canvas()->drawRect(rect, paint);
    }
}

int main()
{
    SkiaSurface baselineSurface(kWidth, kHeight);
    ClearSentinel(baselineSurface);
    const std::vector<std::uint8_t> baseline = baselineSurface.SnapshotRgba();
    Check(baseline.size() == static_cast<std::size_t>(kWidth * kHeight * 4),
          "raster baseline exposes the complete RGBA8 framebuffer");

    // This draw covers every sample. A hardware ANY_SAMPLES_PASSED query around it is positive,
    // even though writing the already-present opaque colour cannot change the framebuffer.
    SkiaSurface sameColorSurface(kWidth, kHeight);
    ClearSentinel(sameColorSurface);
    DrawRect(sameColorSurface, SkRect::MakeWH(kWidth, kHeight),
             SkColorSetARGB(255, 17, 34, 51), SkBlendMode::kSrc);
    const std::vector<std::uint8_t> sameColor = sameColorSurface.SnapshotRgba();
    Check(CountChangedPixels(baseline, sameColor) == 0,
          "full positive coverage can leave zero changed pixels");

    // This draw is wholly outside the target and therefore has no covered sample at all.
    SkiaSurface outsideSurface(kWidth, kHeight);
    ClearSentinel(outsideSurface);
    DrawRect(outsideSurface, SkRect::MakeXYWH(20.0f, 20.0f, 4.0f, 4.0f),
             SK_ColorRED, SkBlendMode::kSrc);
    const std::vector<std::uint8_t> outside = outsideSurface.SnapshotRgba();
    Check(CountChangedPixels(baseline, outside) == 0,
          "zero coverage also leaves zero changed pixels");
    Check(sameColor == outside,
          "framebuffer snapshots cannot distinguish positive from zero coverage");

    // Samples pass before colour blending. Destination-only blending therefore gives another
    // positive-coverage operation whose final image is byte-identical to no draw.
    SkiaSurface destinationSurface(kWidth, kHeight);
    ClearSentinel(destinationSurface);
    DrawRect(destinationSurface, SkRect::MakeWH(kWidth, kHeight),
             SK_ColorRED, SkBlendMode::kDst);
    Check(destinationSurface.SnapshotRgba() == outside,
          "destination-preserving blend also hides positive sample coverage");

    // Pixel differences are unique final pixels, not the number of passing samples across draws.
    SkiaSurface oneDrawSurface(kWidth, kHeight);
    ClearSentinel(oneDrawSurface);
    DrawRect(oneDrawSurface, SkRect::MakeWH(kWidth, kHeight),
             SK_ColorRED, SkBlendMode::kSrc);
    SkiaSurface twoDrawSurface(kWidth, kHeight);
    ClearSentinel(twoDrawSurface);
    DrawRect(twoDrawSurface, SkRect::MakeWH(kWidth, kHeight),
             SK_ColorRED, SkBlendMode::kSrc);
    DrawRect(twoDrawSurface, SkRect::MakeWH(kWidth, kHeight),
             SK_ColorRED, SkBlendMode::kSrc);
    const std::vector<std::uint8_t> oneDraw = oneDrawSurface.SnapshotRgba();
    const std::vector<std::uint8_t> twoDraw = twoDrawSurface.SnapshotRgba();
    Check(CountChangedPixels(baseline, oneDraw) == kWidth * kHeight
              && CountChangedPixels(baseline, twoDraw) == kWidth * kHeight,
          "pixel differences count final pixels rather than passing samples");
    Check(oneDraw == twoDraw,
          "one and two full-coverage submissions produce the same final image");

    std::printf("=== 7 checks, %d failures ===\n", failures);
    return failures == 0 ? 0 : 1;
}
