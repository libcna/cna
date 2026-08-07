// SPDX-License-Identifier: MS-PL
// Direct SKIA-30 boundary contract: alpha labels alter compositing, never public texture bytes.

#include "CNA/Internal/Backends/Skia/SkiaSurface.hpp"
#include "CNA/Internal/Backends/Skia/SkiaTextureBackend.hpp"

#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkSamplingOptions.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <vector>

using CNA::Internal::Backends::Skia::SkiaSourceAlphaConvention;
using CNA::Internal::Backends::Skia::SkiaSurface;
using CNA::Internal::Backends::Skia::SkiaTextureBackend;
using CNA::Internal::Graphics::ImageData;

namespace
{
    int failures = 0;

    void Check(bool condition, const char* label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
        if (!condition)
            ++failures;
    }

    [[nodiscard]] bool SamePixel(const std::vector<std::uint8_t>& pixels,
                                 std::array<std::uint8_t, 4> expected)
    {
        return pixels.size() == expected.size()
            && std::equal(pixels.begin(), pixels.end(), expected.begin(), expected.end());
    }

    [[nodiscard]] std::vector<std::uint8_t> Composite(SkiaTextureBackend& texture,
                                                       SkiaSourceAlphaConvention convention)
    {
        SkiaSurface surface(1, 1);
        surface.Clear(0.0f, 0.0f, 1.0f, 1.0f);
        SkPaint paint;
        surface.Canvas()->drawImage(texture.SnapshotImage(convention), 0.0f, 0.0f,
                                    SkSamplingOptions(SkFilterMode::kNearest), &paint);
        return surface.SnapshotRgba();
    }
}

int main()
{
    // Same CPU bytes, two explicitly selected XNA source-alpha interpretations.
    ImageData alphaData{1, 1, {128, 0, 0, 128}};
    SkiaTextureBackend alphaTexture(alphaData);
    Check(SamePixel(Composite(alphaTexture, SkiaSourceAlphaConvention::Premultiplied),
                    {128, 0, 127, 255}),
          "premultiplied RGBA source composites without a second premultiplication");
    Check(SamePixel(Composite(alphaTexture, SkiaSourceAlphaConvention::Straight),
                    {64, 0, 127, 255}),
          "straight RGBA source is premultiplied once by Skia before compositing");

    // UpdatePixels accepts a source pitch larger than the stored row.  Padding bytes are not
    // texture content and must neither leak into the second row nor alter GetData's exact shadow.
    ImageData strideData{1, 2, {0, 0, 0, 0, 0, 0, 0, 0}};
    SkiaTextureBackend strideTexture(strideData);
    constexpr std::array<std::uint8_t, 16> sourceWithPadding{
        12, 34, 56, 78, 0xA5, 0xA5, 0xA5, 0xA5,
        90, 87, 65, 43, 0x5A, 0x5A, 0x5A, 0x5A,
    };
    strideTexture.UpdatePixels(sourceWithPadding.data(), 8);
    std::array<std::uint8_t, 8> stored{};
    Check(strideTexture.GetData(0, 0, 0, 1, 2, stored.data(), static_cast<int>(stored.size())),
          "GetData accepts the complete level-0 region after a strided update");
    Check(stored == std::array<std::uint8_t, 8>{12, 34, 56, 78, 90, 87, 65, 43},
          "strided upload preserves each RGBA row and ignores padding bytes");

    std::printf("=== %s ===\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
