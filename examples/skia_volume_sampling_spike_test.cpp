// SPDX-License-Identifier: MS-PL
// SKIA-147: headless feasibility spike proving the padded grid-atlas volume-sampling design
// docs/skia-cube-volume-sampling-contract.md fixed: Point (nearest-slice, nearest-texel) sampling
// only -- SKIA-148 adds trilinear/mip/address modes on top of this same cnaSampleVolumeEXT entry
// point. Stays below the public ShaderEffect/SetTexture(Texture3D) API, matching the SKIA-93/145/
// 146 spike precedent; SKIA-149 wires the real, weak-lifetime-tracked public path.

#include "CNA/Internal/Backends/Skia/SkiaSurface.hpp"
#include "CNA/Internal/Backends/Skia/SkiaVolumeSampling.hpp"

#include "include/core/SkCanvas.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkPaint.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkShader.h"
#include "include/core/SkTileMode.h"
#include "include/effects/SkRuntimeEffect.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace CNA::Internal::Backends::Skia;

namespace
{
    int failures = 0;

    void Check(bool condition, const std::string& label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label.c_str());
        if (!condition) ++failures;
    }

    constexpr int kWidth = 4;
    constexpr int kHeight = 4;
    constexpr int kDepth = 5; // NPOT relative to ceil(sqrt(5))=3 cols x 2 rows (one unused cell).

    constexpr std::array<std::uint8_t, 4> kSlice0{255, 0, 0, 255};
    constexpr std::array<std::uint8_t, 4> kSlice1{0, 255, 0, 255};
    constexpr std::array<std::uint8_t, 4> kSlice3{0, 0, 255, 255};
    constexpr std::array<std::uint8_t, 4> kSlice4{255, 255, 0, 255};
    constexpr std::array<std::uint8_t, 4> kQuadTL{10, 20, 30, 255};
    constexpr std::array<std::uint8_t, 4> kQuadTR{40, 50, 60, 255};
    constexpr std::array<std::uint8_t, 4> kQuadBL{70, 80, 90, 255};
    constexpr std::array<std::uint8_t, 4> kQuadBR{100, 110, 120, 255};

    void FillSlice(std::uint8_t* slice, const std::array<std::uint8_t, 4>& colour)
    {
        for (int i = 0; i < kWidth * kHeight; ++i)
            std::memcpy(slice + static_cast<std::size_t>(i) * 4u, colour.data(), 4u);
    }

    // Slice 2 is a four-quadrant pattern (2x2 blocks across the 4x4 slice) proving exact
    // within-slice (u, v) addressing, not just slice selection.
    void FillQuadrantSlice(std::uint8_t* slice)
    {
        for (int y = 0; y < kHeight; ++y)
        {
            for (int x = 0; x < kWidth; ++x)
            {
                const std::array<std::uint8_t, 4>& colour =
                    (x < kWidth / 2)
                        ? (y < kHeight / 2 ? kQuadTL : kQuadBL)
                        : (y < kHeight / 2 ? kQuadTR : kQuadBR);
                std::memcpy(slice + (static_cast<std::size_t>(y) * kWidth + x) * 4u,
                           colour.data(), 4u);
            }
        }
    }

    [[nodiscard]] std::vector<std::uint8_t> SampleVolumeEXT(
        const SkRuntimeEffect& effect, const SkRuntimeEffect::Uniform& uvwUniform,
        const SkRuntimeEffect::Uniform& meta0Uniform, const SkRuntimeEffect::Uniform& meta1Uniform,
        const sk_sp<SkImage>& atlas, const VolumeAtlasLayoutEXT& layout,
        float u, float v, float w)
    {
        std::vector<std::uint8_t> uniformBytes(effect.uniformSize(), 0u);
        const float uvw[3] = {u, v, w};
        const float meta0[4] = {static_cast<float>(layout.cols), static_cast<float>(layout.rows),
                                static_cast<float>(kDepth), 0.0f};
        const float meta1[4] = {static_cast<float>(layout.tileWidth),
                                static_cast<float>(layout.tileHeight), 0.0f, 0.0f};
        std::memcpy(uniformBytes.data() + uvwUniform.offset, uvw, sizeof(uvw));
        std::memcpy(uniformBytes.data() + meta0Uniform.offset, meta0, sizeof(meta0));
        std::memcpy(uniformBytes.data() + meta1Uniform.offset, meta1, sizeof(meta1));

        sk_sp<SkShader> child = atlas->makeShader(
            SkTileMode::kClamp, SkTileMode::kClamp, SkSamplingOptions(SkFilterMode::kNearest));
        if (!child)
            return {};
        std::array<sk_sp<SkShader>, 1> children{std::move(child)};
        sk_sp<SkShader> shader = effect.makeShader(
            SkData::MakeWithCopy(uniformBytes.data(), uniformBytes.size()),
            children.data(), children.size());
        if (!shader)
            return {};

        SkiaSurface surface(1, 1);
        surface.Clear(0.0f, 0.0f, 0.0f, 0.0f);
        SkPaint paint;
        paint.setShader(std::move(shader));
        paint.setBlendMode(SkBlendMode::kSrc);
        surface.Canvas()->drawRect(SkRect::MakeXYWH(0.0f, 0.0f, 1.0f, 1.0f), paint);
        return surface.SnapshotRgba();
    }

    [[nodiscard]] bool SamePixel(const std::vector<std::uint8_t>& pixels,
                                 const std::array<std::uint8_t, 4>& expected)
    {
        return pixels.size() >= 4u && std::memcmp(pixels.data(), expected.data(), 4u) == 0;
    }
}

int main()
{
    const VolumeAtlasLayoutEXT layout = ComputeVolumeAtlasLayoutEXT(kWidth, kHeight, kDepth);
    Check(layout.cols == 3 && layout.rows == 2 && layout.tileWidth == kWidth + 2
              && layout.tileHeight == kHeight + 2 && layout.atlasWidth == 3 * (kWidth + 2)
              && layout.atlasHeight == 2 * (kHeight + 2),
          "5-slice NPOT volume packs into a 3x2 grid (one unused cell), matching ceil(sqrt(5))");

    std::vector<std::uint8_t> voxels(
        static_cast<std::size_t>(kWidth) * kHeight * kDepth * 4u, 0u);
    const std::size_t sliceBytes = static_cast<std::size_t>(kWidth) * kHeight * 4u;
    FillSlice(voxels.data() + 0 * sliceBytes, kSlice0);
    FillSlice(voxels.data() + 1 * sliceBytes, kSlice1);
    FillQuadrantSlice(voxels.data() + 2 * sliceBytes);
    FillSlice(voxels.data() + 3 * sliceBytes, kSlice3);
    FillSlice(voxels.data() + 4 * sliceBytes, kSlice4);

    const std::vector<std::uint8_t> atlasBytes =
        PackVolumeAtlasEXT(kWidth, kHeight, kDepth, voxels.data(), voxels.size());
    Check(static_cast<int>(atlasBytes.size())
              == layout.atlasWidth * layout.atlasHeight * 4,
          "packed atlas has the exact expected byte size");

    // Padding-bleed check (SKIA-147's explicit "atlas padding cannot bleed between slices"),
    // verified directly against the packed bytes: slice 2's left border column must equal its own
    // column-0 texels, never slice 1's (the tile to its left in the 3-wide grid) or anything else.
    bool paddingCorrect = true;
    {
        const int col = 2 % layout.cols;
        const int row = 2 / layout.cols;
        const int originX = col * layout.tileWidth + 1;
        const int originY = row * layout.tileHeight + 1;
        const auto atlasAt = [&](int x, int y) {
            return atlasBytes.data()
                + (static_cast<std::size_t>(y) * layout.atlasWidth + x) * 4u;
        };
        for (int y = 0; y < kHeight; ++y)
        {
            const std::array<std::uint8_t, 4>& expected = (y < kHeight / 2) ? kQuadTL : kQuadBL;
            paddingCorrect = paddingCorrect
                && std::memcmp(atlasAt(originX - 1, originY + y), expected.data(), 4u) == 0;
        }
        // The top-left corner border pixel must replicate slice 2's own (0,0) texel (TL quadrant).
        paddingCorrect = paddingCorrect
            && std::memcmp(atlasAt(originX - 1, originY - 1), kQuadTL.data(), 4u) == 0;
    }
    Check(paddingCorrect,
          "slice 2's replicated border pixels are its own edge texels, never an adjacent tile's");

    SkiaSurface atlasSurface(layout.atlasWidth, layout.atlasHeight);
    Check(atlasSurface.WritePixels(0, 0, layout.atlasWidth, layout.atlasHeight, atlasBytes.data(),
                                   layout.atlasWidth * 4),
          "packed atlas bytes upload into a raster surface");
    const sk_sp<SkImage> atlasImage = atlasSurface.SnapshotImage();
    Check(atlasImage != nullptr, "atlas surface snapshots to a sampleable SkImage");

    const std::string source = std::string(kCnaSampleVolumePreambleEXT) + R"(
        uniform float3 testUvw;
        half4 main(float2 sourcePixel) {
            return cnaSampleVolumeEXT(testUvw);
        }
    )";
    const auto compiled = SkRuntimeEffect::MakeForShader(SkString(source));
    Check(compiled.effect != nullptr,
          "cnaSampleVolumeEXT preamble compiles for the pinned raster Skia ("
              + std::string(compiled.effect ? "" : compiled.errorText.c_str()) + ")");
    if (!compiled.effect || !atlasImage)
    {
        std::printf("=== %d failures ===\n", failures + 1);
        return 1;
    }

    const SkRuntimeEffect::Uniform* uvwUniform = compiled.effect->findUniform("testUvw");
    const SkRuntimeEffect::Uniform* meta0Uniform =
        compiled.effect->findUniform("cnaVolumeAtlasMeta0");
    const SkRuntimeEffect::Uniform* meta1Uniform =
        compiled.effect->findUniform("cnaVolumeAtlasMeta1");
    Check(uvwUniform && meta0Uniform && meta1Uniform,
          "reflection finds testUvw and both reserved cnaVolumeAtlasMeta uniforms");
    if (!uvwUniform || !meta0Uniform || !meta1Uniform)
    {
        std::printf("=== %d failures ===\n", failures + 1);
        return 1;
    }

    const auto sample = [&](float u, float v, float w) {
        return SampleVolumeEXT(*compiled.effect, *uvwUniform, *meta0Uniform, *meta1Uniform,
                               atlasImage, layout, u, v, w);
    };

    // Slice selection: (k + 0.5) / depth lands floor(w * depth) exactly on slice k for every
    // uniformly-coloured slice, at the dead centre (u, v) = (0.5, 0.5) where content is unambiguous.
    Check(SamePixel(sample(0.5f, 0.5f, 0.1f), kSlice0), "w=0.1 selects slice 0 exactly");
    Check(SamePixel(sample(0.5f, 0.5f, 0.3f), kSlice1), "w=0.3 selects slice 1 exactly");
    Check(SamePixel(sample(0.5f, 0.5f, 0.7f), kSlice3), "w=0.7 selects slice 3 exactly");
    Check(SamePixel(sample(0.5f, 0.5f, 0.9f), kSlice4), "w=0.9 selects slice 4 exactly");

    // Boundary/clamp behaviour: w=0 selects the first slice; w=1 (and beyond, via clamp) selects
    // the last, never an out-of-range or wrapped-around slice.
    Check(SamePixel(sample(0.5f, 0.5f, 0.0f), kSlice0), "w=0.0 exactly selects the first slice");
    Check(SamePixel(sample(0.5f, 0.5f, 1.0f), kSlice4),
          "w=1.0 exactly clamps to the last slice, not an out-of-range one");

    // Within-slice (u, v) precision: all four quadrants of the special slice 2 are individually
    // addressable, not just "some texel of the right slice".
    const float wSlice2 = 0.5f; // floor(0.5 * 5) = 2.
    Check(SamePixel(sample(0.25f, 0.25f, wSlice2), kQuadTL), "slice 2 top-left quadrant is exact");
    Check(SamePixel(sample(0.75f, 0.25f, wSlice2), kQuadTR), "slice 2 top-right quadrant is exact");
    Check(SamePixel(sample(0.25f, 0.75f, wSlice2), kQuadBL), "slice 2 bottom-left quadrant is exact");
    Check(SamePixel(sample(0.75f, 0.75f, wSlice2), kQuadBR), "slice 2 bottom-right quadrant is exact");

    std::printf("=== %d failures ===\n", failures);
    return failures == 0 ? 0 : 1;
}
