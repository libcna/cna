// SPDX-License-Identifier: MS-PL
// SKIA-148: volume trilinear interpolation, address modes (Clamp/Wrap/Mirror), and mip-level
// selection, extending SKIA-147's confirmed cnaSampleVolumeEXT body (fixed signature). Stays
// below the public ShaderEffect/SetTexture(Texture3D) API, matching the SKIA-93/145/146/147
// spike precedent; SKIA-149 wires the real, weak-lifetime-tracked public path.

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

#include <algorithm>
#include <array>
#include <cmath>
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

    // A pure-scalar C++ reference matching cnaSampleVolumeEXT's SkSL formula exactly (address
    // mode, then the corrected half-texel-centered trilinear blend). Used only to compute expected
    // values for the address-mode checks below, where hand computation is error-prone; the w-axis
    // boundary/midpoint checks use exact hand-picked integers instead and need no reference.
    [[nodiscard]] float ApplyAddress(float x, int mode)
    {
        if (mode == 0) return std::clamp(x, 0.0f, 1.0f);
        if (mode == 1) return x - std::floor(x);
        const float t = (x * 0.5f - std::floor(x * 0.5f)) * 2.0f;
        return 1.0f - std::abs(t - 1.0f);
    }

    [[nodiscard]] float ReferenceGraySample(
        const std::vector<float>& sliceGray, float u, float v, float w,
        int addressU, int addressV, int addressW)
    {
        (void)u; (void)v; // Every reference slice below is spatially uniform (u/v-independent).
        const auto depth = static_cast<float>(sliceGray.size());
        const float wAddr = ApplyAddress(w, addressW);
        const float sampleF = wAddr * depth - 0.5f;
        const float flooredS0 = std::floor(sampleF);
        const int s0 = static_cast<int>(std::clamp(flooredS0, 0.0f, depth - 1.0f));
        const int s1 = static_cast<int>(std::clamp(flooredS0 + 1.0f, 0.0f, depth - 1.0f));
        const float wf = std::clamp(sampleF - flooredS0, 0.0f, 1.0f);
        return sliceGray[static_cast<std::size_t>(s0)] * (1.0f - wf)
            + sliceGray[static_cast<std::size_t>(s1)] * wf;
    }

    [[nodiscard]] bool Near(std::uint8_t actual, float expected, float tolerance)
    {
        return std::abs(static_cast<float>(actual) - expected) <= tolerance;
    }

    struct VolumeHarness final
    {
        int width = 0, height = 0, depth = 0;
        VolumeAtlasLayoutEXT layout;
        sk_sp<SkImage> atlas;
        sk_sp<SkRuntimeEffect> effect;
        const SkRuntimeEffect::Uniform* uvwUniform = nullptr;
        const SkRuntimeEffect::Uniform* addressUniform = nullptr;
        const SkRuntimeEffect::Uniform* meta0Uniform = nullptr;
        const SkRuntimeEffect::Uniform* meta1Uniform = nullptr;

        [[nodiscard]] std::vector<std::uint8_t> Sample(
            float u, float v, float w, int addressU, int addressV, int addressW) const
        {
            std::vector<std::uint8_t> uniformBytes(effect->uniformSize(), 0u);
            const float uvw[3] = {u, v, w};
            const float address[3] = {static_cast<float>(addressU), static_cast<float>(addressV),
                                      static_cast<float>(addressW)};
            const float meta0[4] = {static_cast<float>(layout.cols), static_cast<float>(layout.rows),
                                    static_cast<float>(depth), 0.0f};
            const float meta1[4] = {static_cast<float>(layout.tileWidth),
                                    static_cast<float>(layout.tileHeight), 0.0f, 0.0f};
            std::memcpy(uniformBytes.data() + uvwUniform->offset, uvw, sizeof(uvw));
            std::memcpy(uniformBytes.data() + addressUniform->offset, address, sizeof(address));
            std::memcpy(uniformBytes.data() + meta0Uniform->offset, meta0, sizeof(meta0));
            std::memcpy(uniformBytes.data() + meta1Uniform->offset, meta1, sizeof(meta1));

            sk_sp<SkShader> child = atlas->makeShader(
                SkTileMode::kClamp, SkTileMode::kClamp, SkSamplingOptions(SkFilterMode::kLinear));
            if (!child)
                return {};
            std::array<sk_sp<SkShader>, 1> children{std::move(child)};
            sk_sp<SkShader> shader = effect->makeShader(
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
    };

    [[nodiscard]] bool BuildHarness(
        int width, int height, int depth, const std::vector<std::uint8_t>& voxels,
        VolumeHarness& harness)
    {
        harness.width = width;
        harness.height = height;
        harness.depth = depth;
        harness.layout = ComputeVolumeAtlasLayoutEXT(width, height, depth);
        const std::vector<std::uint8_t> atlasBytes =
            PackVolumeAtlasEXT(width, height, depth, voxels.data(), voxels.size());
        if (atlasBytes.empty())
            return false;

        SkiaSurface atlasSurface(harness.layout.atlasWidth, harness.layout.atlasHeight);
        if (!atlasSurface.WritePixels(0, 0, harness.layout.atlasWidth, harness.layout.atlasHeight,
                                      atlasBytes.data(), harness.layout.atlasWidth * 4))
        {
            return false;
        }
        harness.atlas = atlasSurface.SnapshotImage();
        if (!harness.atlas)
            return false;

        const std::string source = std::string(kCnaSampleVolumePreambleEXT) + R"(
            uniform float3 testUvw;
            half4 main(float2 sourcePixel) {
                return cnaSampleVolumeEXT(testUvw);
            }
        )";
        const auto compiled = SkRuntimeEffect::MakeForShader(SkString(source));
        if (!compiled.effect)
            return false;
        harness.effect = compiled.effect;
        harness.uvwUniform = harness.effect->findUniform("testUvw");
        harness.addressUniform = harness.effect->findUniform("cnaVolumeAddressModesEXT");
        harness.meta0Uniform = harness.effect->findUniform("cnaVolumeAtlasMeta0");
        harness.meta1Uniform = harness.effect->findUniform("cnaVolumeAtlasMeta1");
        return harness.uvwUniform && harness.addressUniform && harness.meta0Uniform
            && harness.meta1Uniform;
    }

    [[nodiscard]] bool SameGray(const std::vector<std::uint8_t>& pixels, std::uint8_t gray)
    {
        return pixels.size() >= 4u && pixels[0] == gray && pixels[1] == gray && pixels[2] == gray
            && pixels[3] == 255u;
    }
}

int main()
{
    // --- Part 1: three uniform grayscale slices (0, 100, 200) prove the corrected w-axis
    // clamp/blend formula exactly, with no rounding ambiguity. ---
    {
        constexpr int w = 2, h = 2, d = 3;
        std::vector<std::uint8_t> voxels(static_cast<std::size_t>(w) * h * d * 4u, 0u);
        const std::array<std::uint8_t, 3> gray{0, 100, 200};
        for (int slice = 0; slice < d; ++slice)
        {
            for (int i = 0; i < w * h; ++i)
            {
                std::uint8_t* pixel = voxels.data()
                    + (static_cast<std::size_t>(slice) * w * h + i) * 4u;
                pixel[0] = pixel[1] = pixel[2] = gray[static_cast<std::size_t>(slice)];
                pixel[3] = 255u;
            }
        }
        VolumeHarness harness;
        Check(BuildHarness(w, h, d, voxels, harness), "grayscale w-axis harness builds");
        if (harness.effect)
        {
            Check(SameGray(harness.Sample(0.5f, 0.5f, 0.0f, 0, 0, 0), 0),
                  "w=0.0 (corrected clamp formula) reads slice 0 exactly, no blend into slice 1");
            Check(SameGray(harness.Sample(0.5f, 0.5f, 1.0f, 0, 0, 0), 200),
                  "w=1.0 (corrected clamp formula) reads the last slice exactly, no over-read");
            // sampleF = w*3-0.5 = 1.0 at w=0.5: flooredS0=1 (slice 1, gray 100), s1=2 (slice 2,
            // gray 200), wf=0.0 -- reads slice 1 exactly, matching the half-texel-centred formula.
            Check(SameGray(harness.Sample(0.5f, 0.5f, 0.5f, 0, 0, 0), 100),
                  "w=0.5 lands exactly on the middle slice's own centre (half-texel convention)");
            // Exact midpoint between slice 0 (0) and slice 1 (100): w*3-0.5=0.5 -> w=1/3.
            Check(SameGray(harness.Sample(0.5f, 0.5f, 1.0f / 3.0f, 0, 0, 0), 50),
                  "w exactly midway between slices 0 and 1 blends to their exact average (50)");
            // Exact midpoint between slice 1 (100) and slice 2 (200): w*3-0.5=1.5 -> w=2/3.
            Check(SameGray(harness.Sample(0.5f, 0.5f, 2.0f / 3.0f, 0, 0, 0), 150),
                  "w exactly midway between slices 1 and 2 blends to their exact average (150)");
        }

        // Address modes: Wrap(1.8)=0.8, Mirror(1.8)=0.2 -- deliberately chosen so the two modes
        // disagree, proving the mode selector genuinely changes behaviour rather than being
        // ignored. Expected values come from the scalar C++ reference, matching the SkSL formula
        // exactly, with a small tolerance for float/byte rounding through the raster pipeline.
        const std::vector<float> grayF{0.0f, 100.0f, 200.0f};
        if (harness.effect)
        {
            const float expectedWrap = ReferenceGraySample(grayF, 0.5f, 0.5f, 1.8f, 0, 0, 1);
            const float expectedMirror = ReferenceGraySample(grayF, 0.5f, 0.5f, 1.8f, 0, 0, 2);
            Check(std::abs(expectedWrap - expectedMirror) > 10.0f,
                  "sanity: the chosen w=1.8 test value gives genuinely different Wrap/Mirror "
                  "references, so the test can actually discriminate them");
            const std::vector<std::uint8_t> wrapped = harness.Sample(0.5f, 0.5f, 1.8f, 0, 0, 1);
            const std::vector<std::uint8_t> mirrored = harness.Sample(0.5f, 0.5f, 1.8f, 0, 0, 2);
            Check(!wrapped.empty() && Near(wrapped[0], expectedWrap, 2.0f),
                  "Wrap address mode at w=1.8 matches the scalar reference within rounding");
            Check(!mirrored.empty() && Near(mirrored[0], expectedMirror, 2.0f),
                  "Mirror address mode at w=1.8 matches the scalar reference within rounding "
                  "and differs from Wrap's result");
        }
    }

    // --- Part 2: a genuine 2x2x2 volume with eight distinct voxel values proves real trilinear
    // (not just w-axis) interpolation. Sampling the exact geometric centre (u=v=w=0.5) is a
    // well-known trilinear identity: the result is the unweighted mean of all eight corners,
    // independent of their individual values, so the expected result is exact and easy to verify
    // by hand: mean(0,32,64,96,128,160,192,224) = 896/8 = 112. ---
    {
        constexpr int w = 2, h = 2, d = 2;
        // Row-major per slice: (0,0)=TL, (1,0)=TR, (0,1)=BL, (1,1)=BR.
        const std::array<std::uint8_t, 4> slice0{0, 32, 64, 96};
        const std::array<std::uint8_t, 4> slice1{128, 160, 192, 224};
        std::vector<std::uint8_t> voxels(static_cast<std::size_t>(w) * h * d * 4u, 0u);
        const auto fill = [&](int slice, const std::array<std::uint8_t, 4>& values) {
            for (int i = 0; i < 4; ++i)
            {
                std::uint8_t* pixel = voxels.data()
                    + (static_cast<std::size_t>(slice) * w * h + i) * 4u;
                pixel[0] = pixel[1] = pixel[2] = values[static_cast<std::size_t>(i)];
                pixel[3] = 255u;
            }
        };
        fill(0, slice0);
        fill(1, slice1);

        VolumeHarness harness;
        Check(BuildHarness(w, h, d, voxels, harness), "eight-voxel trilinear harness builds");
        if (harness.effect)
        {
            const std::vector<std::uint8_t> centre = harness.Sample(0.5f, 0.5f, 0.5f, 0, 0, 0);
            Check(!centre.empty() && Near(centre[0], 112.0f, 2.0f),
                  "sampling the exact geometric centre of a 2x2x2 volume averages all eight "
                  "distinct corner voxels (112), proving real trilinear (not just w-axis) blend");
        }
    }

    // --- Part 3: mip-level selection. Matching SKIA-146's cube precedent, which level's data to
    // sample is a C++-side choice of which already-packed atlas is bound as cnaVolumeAtlas0 --
    // not a new SkSL construct. Two independently packed, differently-valued 2x2x2 "levels" prove
    // there is no bleed or confusion between them: whichever atlas is bound is exactly what gets
    // sampled, with the same cnaSampleVolumeEXT formula and no per-level special-casing. ---
    {
        constexpr int w = 2, h = 2, d = 2;
        std::vector<std::uint8_t> level0Voxels(static_cast<std::size_t>(w) * h * d * 4u, 0u);
        std::vector<std::uint8_t> level1Voxels(static_cast<std::size_t>(w) * h * d * 4u, 0u);
        for (std::size_t i = 0; i < static_cast<std::size_t>(w) * h * d; ++i)
        {
            level0Voxels[i * 4u] = level0Voxels[i * 4u + 1u] = level0Voxels[i * 4u + 2u] = 50u;
            level0Voxels[i * 4u + 3u] = 255u;
            level1Voxels[i * 4u] = level1Voxels[i * 4u + 1u] = level1Voxels[i * 4u + 2u] = 200u;
            level1Voxels[i * 4u + 3u] = 255u;
        }
        VolumeHarness level0Harness;
        VolumeHarness level1Harness;
        Check(BuildHarness(w, h, d, level0Voxels, level0Harness)
                  && BuildHarness(w, h, d, level1Voxels, level1Harness),
              "two independently packed mip-level atlases build");
        if (level0Harness.effect && level1Harness.effect)
        {
            const std::vector<std::uint8_t> fromLevel0 =
                level0Harness.Sample(0.5f, 0.5f, 0.5f, 0, 0, 0);
            const std::vector<std::uint8_t> fromLevel1 =
                level1Harness.Sample(0.5f, 0.5f, 0.5f, 0, 0, 0);
            Check(!fromLevel0.empty() && SameGray(fromLevel0, 50)
                      && !fromLevel1.empty() && SameGray(fromLevel1, 200),
                  "sampling whichever level's atlas is bound reads exactly that level's own "
                  "values, with no bleed between mip levels");
        }
    }

    // --- Part 4: partial updates. PackVolumeAtlasEXT always rebuilds the complete atlas from the
    // current voxel buffer (no incremental/partial packing code path exists to have a distinct
    // bug), so a caller-side partial SetData into a sub-region, followed by a fresh pack, must
    // deterministically show the new region's values while leaving every untouched voxel exactly
    // as it was -- proven directly against the packed bytes, not through SkSL. ---
    {
        constexpr int w = 3, h = 3, d = 2;
        std::vector<std::uint8_t> voxels(static_cast<std::size_t>(w) * h * d * 4u, 0u);
        for (std::size_t i = 0; i < static_cast<std::size_t>(w) * h * d; ++i)
        {
            voxels[i * 4u] = voxels[i * 4u + 1u] = voxels[i * 4u + 2u] = 10u;
            voxels[i * 4u + 3u] = 255u;
        }
        const std::vector<std::uint8_t> before =
            PackVolumeAtlasEXT(w, h, d, voxels.data(), voxels.size());

        // Simulate a partial Texture3D::SetData: overwrite only voxel (1,1) of slice 0.
        const std::size_t dirtyVoxel = (static_cast<std::size_t>(1) * w + 1) * 4u;
        voxels[dirtyVoxel] = voxels[dirtyVoxel + 1u] = voxels[dirtyVoxel + 2u] = 250u;
        const std::vector<std::uint8_t> after =
            PackVolumeAtlasEXT(w, h, d, voxels.data(), voxels.size());

        const VolumeAtlasLayoutEXT layout = ComputeVolumeAtlasLayoutEXT(w, h, d);
        const int dirtyAtlasX = 0 * layout.tileWidth + 1 + 1; // slice 0's tile, voxel (1,1).
        const int dirtyAtlasY = 0 * layout.tileHeight + 1 + 1;
        const auto atlasPixel = [&](const std::vector<std::uint8_t>& atlas, int x, int y) {
            return atlas.data() + (static_cast<std::size_t>(y) * layout.atlasWidth + x) * 4u;
        };
        bool onlyDirtyVoxelChanged = true;
        int changedCount = 0;
        for (int y = 0; y < layout.atlasHeight; ++y)
        {
            for (int x = 0; x < layout.atlasWidth; ++x)
            {
                if (std::memcmp(atlasPixel(before, x, y), atlasPixel(after, x, y), 4u) != 0)
                {
                    ++changedCount;
                    onlyDirtyVoxelChanged =
                        onlyDirtyVoxelChanged && x == dirtyAtlasX && y == dirtyAtlasY;
                }
            }
        }
        Check(changedCount == 1 && onlyDirtyVoxelChanged
                  && std::memcmp(atlasPixel(after, dirtyAtlasX, dirtyAtlasY),
                                "\xFA\xFA\xFA\xFF", 4u) == 0,
              "re-packing after a partial voxel update changes exactly that voxel's atlas texel "
              "and leaves every other packed byte, including its own replicated border, unchanged");
    }

    std::printf("=== %d failures ===\n", failures);
    return failures == 0 ? 0 : 1;
}
