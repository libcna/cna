// SPDX-License-Identifier: MS-PL
// SKIA-146: cube mip selection, cross-face (non-seamless, matching real XNA/D3D9 hardware) edge
// behaviour, and RenderTargetCube snapshot invalidation for cube sampling. Reuses SKIA-145's
// confirmed cnaSampleCubeEXT formula (SkiaCubeSampling.hpp) and stays below the public
// ShaderEffect/SetTexture(TextureCube) API, matching the SKIA-93/145 spike precedent; SKIA-149
// wires the real, weak-lifetime-tracked public path.

#include "CNA/Internal/Backends/Skia/SkiaCubeSampling.hpp"
#include "CNA/Internal/Backends/Skia/SkiaRenderTargetBinding.hpp"
#include "CNA/Internal/Backends/Skia/SkiaRenderTargetCubeBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaSurface.hpp"

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

    const std::string kSource = std::string(kCnaSampleCubePreambleEXT) + R"(
        uniform float3 testDirection;
        half4 main(float2 sourcePixel) {
            return cnaSampleCubeEXT(testDirection);
        }
    )";

    // Face order matches ITextureCubeBackend/CubeMapFace: 0=+X,1=-X,2=+Y,3=-Y,4=+Z,5=-Z.
    constexpr std::array<std::array<float, 3>, 6> kFaceColors{{
        {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f},
        {1.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 1.0f},
    }};
    constexpr std::array<std::array<float, 3>, 6> kFaceCenterDirections{{
        {1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
        {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f},
    }};

    [[nodiscard]] std::array<std::uint8_t, 4> ExpectedRgba(int face)
    {
        return {static_cast<std::uint8_t>(kFaceColors[static_cast<std::size_t>(face)][0] * 255.0f),
                static_cast<std::uint8_t>(kFaceColors[static_cast<std::size_t>(face)][1] * 255.0f),
                static_cast<std::uint8_t>(kFaceColors[static_cast<std::size_t>(face)][2] * 255.0f),
                255u};
    }

    [[nodiscard]] std::vector<std::uint8_t> SampleCubeEXT(
        const SkRuntimeEffect& effect, const SkRuntimeEffect::Uniform& directionUniform,
        const SkRuntimeEffect::Uniform& faceSizeUniform,
        const std::array<sk_sp<SkImage>, 6>& faces, float x, float y, float z, float faceSize)
    {
        std::vector<std::uint8_t> uniformBytes(effect.uniformSize(), 0u);
        const float direction[3] = {x, y, z};
        std::memcpy(uniformBytes.data() + directionUniform.offset, direction, sizeof(direction));
        std::memcpy(uniformBytes.data() + faceSizeUniform.offset, &faceSize, sizeof(faceSize));

        std::array<sk_sp<SkShader>, 6> children;
        for (std::size_t face = 0; face < faces.size(); ++face)
        {
            if (!faces[face])
                return {};
            children[face] = faces[face]->makeShader(
                SkTileMode::kClamp, SkTileMode::kClamp, SkSamplingOptions(SkFilterMode::kNearest));
            if (!children[face])
                return {};
        }
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
    const auto compiled = SkRuntimeEffect::MakeForShader(SkString(kSource));
    Check(compiled.effect != nullptr, "shared cnaSampleCubeEXT preamble still compiles");
    if (!compiled.effect)
    {
        std::printf("=== 0/1 PASS ===\n");
        return 1;
    }
    const SkRuntimeEffect::Uniform* directionUniform = compiled.effect->findUniform("testDirection");
    const SkRuntimeEffect::Uniform* faceSizeUniform =
        compiled.effect->findUniform("cnaCubeFaceSizeEXT");

    auto counters = std::make_shared<SkiaResourceCounters>();
    auto binding = std::make_shared<SkiaRenderTargetBinding>();
    SkiaSurface backbuffer(4, 4);
    binding->SetBackbuffer(&backbuffer);

    {
        SkiaRenderTargetCubeBackend target(4, true, true, binding, counters);

        // Render a distinct solid colour into each face, matching CubeMapFace/ITextureCubeBackend
        // order. Each BindAsRenderTargetFace finalizes (syncs + regenerates mips for) the
        // previously bound face; the final UnbindAsRenderTarget finalizes face 5.
        for (int face = 0; face < 6; ++face)
        {
            target.BindAsRenderTargetFace(face);
            target.BeforeWriteEXT();
            binding->ActiveSurface()->Clear(kFaceColors[static_cast<std::size_t>(face)][0],
                                            kFaceColors[static_cast<std::size_t>(face)][1],
                                            kFaceColors[static_cast<std::size_t>(face)][2], 1.0f);
        }
        target.UnbindAsRenderTarget();

        std::array<sk_sp<SkImage>, 6> levelZero;
        bool allSnapshotsBuilt = true;
        for (int face = 0; face < 6; ++face)
        {
            levelZero[static_cast<std::size_t>(face)] = target.SnapshotFaceEXT(face, 0);
            allSnapshotsBuilt = allSnapshotsBuilt && levelZero[static_cast<std::size_t>(face)];
        }
        Check(allSnapshotsBuilt, "all six rendered faces produce a level-0 sampling snapshot");

        const SkiaResourceStats afterSnapshots = counters->GetStats();
        Check(afterSnapshots.cubeTargetSnapshots == 6,
              "six live face snapshots are individually tracked");

        bool allFaceCentersMatch = directionUniform && faceSizeUniform;
        for (int face = 0; allFaceCentersMatch && face < 6; ++face)
        {
            const auto& dir = kFaceCenterDirections[static_cast<std::size_t>(face)];
            const std::vector<std::uint8_t> pixel = SampleCubeEXT(
                *compiled.effect, *directionUniform, *faceSizeUniform, levelZero, dir[0], dir[1],
                dir[2], 4.0f);
            allFaceCentersMatch = SamePixel(pixel, ExpectedRgba(face));
        }
        Check(allFaceCentersMatch,
              "cnaSampleCubeEXT reads a rendered RenderTargetCube's own six faces correctly");

        // Cross-face seam policy (SKIA-146): CNA matches real XNA/D3D9 hardware, which has no
        // automatic seamless cube filtering -- each face samples independently with Clamp
        // addressing at its own edge. A direction landing extremely close to the +X/+Y edge (but
        // still resolving to +X by dominant-axis selection) must stay exactly +X's own colour: no
        // crash, no garbage, and no accidental read of the adjacent face's data.
        if (allFaceCentersMatch)
        {
            const std::vector<std::uint8_t> nearEdge = SampleCubeEXT(
                *compiled.effect, *directionUniform, *faceSizeUniform, levelZero, 1.0f, 0.999f,
                0.0f, 4.0f);
            Check(SamePixel(nearEdge, ExpectedRgba(0)),
                  "a direction near a face edge still resolves cleanly within its own face "
                  "(no seamless cross-face blend, matching real XNA/D3D9 hardware)");
        }

        // Staleness (SKIA-146's explicit "updates become visible without stale snapshots"):
        // redraw face 0 with a new colour and confirm both identity (a fresh SkImage, not the
        // cached one) and content (the new colour, not the old one) change.
        target.BindAsRenderTargetFace(0);
        target.BeforeWriteEXT();
        binding->ActiveSurface()->Clear(0.5f, 0.5f, 0.5f, 1.0f);
        target.UnbindAsRenderTarget();
        const sk_sp<SkImage> refreshedFace0 = target.SnapshotFaceEXT(0, 0);
        Check(refreshedFace0 && refreshedFace0 != levelZero[0],
              "redrawing a face invalidates its cached snapshot identity");
        levelZero[0] = refreshedFace0;
        if (directionUniform && faceSizeUniform)
        {
            const std::vector<std::uint8_t> updated = SampleCubeEXT(
                *compiled.effect, *directionUniform, *faceSizeUniform, levelZero, 1.0f, 0.0f,
                0.0f, 4.0f);
            const std::array<std::uint8_t, 4> expectedGrey{128, 128, 128, 255};
            Check(SamePixel(updated, expectedGrey),
                  "cube sampling observes the redrawn face's new colour, never a stale one");
        }

        // Mip selection (SKIA-146): level 1 (2x2) of a solid-colour face area-box-averages to
        // exactly that same colour, so its snapshot is an exact, unambiguous check of mip-level
        // addressing rather than a tolerance-based one.
        std::array<sk_sp<SkImage>, 6> levelOne;
        bool allLevelOneBuilt = true;
        for (int face = 1; face < 6; ++face) // face 0 was redrawn grey above; check the rest.
        {
            levelOne[static_cast<std::size_t>(face)] = target.SnapshotFaceEXT(face, 1);
            allLevelOneBuilt = allLevelOneBuilt && levelOne[static_cast<std::size_t>(face)];
        }
        levelOne[0] = target.SnapshotFaceEXT(0, 1);
        allLevelOneBuilt = allLevelOneBuilt && levelOne[0] != nullptr;
        Check(allLevelOneBuilt, "every face's generated level-1 mip produces a sampling snapshot");

        bool allMipsMatch = allLevelOneBuilt && directionUniform && faceSizeUniform;
        for (int face = 1; allMipsMatch && face < 6; ++face)
        {
            const auto& dir = kFaceCenterDirections[static_cast<std::size_t>(face)];
            const std::vector<std::uint8_t> pixel = SampleCubeEXT(
                *compiled.effect, *directionUniform, *faceSizeUniform, levelOne, dir[0], dir[1],
                dir[2], 2.0f);
            allMipsMatch = SamePixel(pixel, ExpectedRgba(face));
        }
        Check(allMipsMatch,
              "mip level 1 samples the correctly-generated, area-box-averaged solid colour");

        Check(target.SnapshotFaceEXT(0, 5) == nullptr && target.SnapshotFaceEXT(6, 0) == nullptr,
              "an out-of-range level or face snapshot request is rejected, not fabricated");
    }

    Check(counters->GetStats().cubeTargetSnapshots == 0
              && counters->GetStats().cubeTargetSnapshotBytes == 0
              && counters->GetStats().renderTargetCubes == 0,
          "destroying the RenderTargetCube releases every cached sampling snapshot and surface");

    std::printf("=== %d failures ===\n", failures);
    return failures == 0 ? 0 : 1;
}
