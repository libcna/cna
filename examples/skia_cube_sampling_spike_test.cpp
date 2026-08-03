// SPDX-License-Identifier: MS-PL
// SKIA-145: headless feasibility spike proving (or correcting) the dominant-axis cube face/UV
// table docs/skia-cube-volume-sampling-contract.md hypothesized. This deliberately stays below
// the public ShaderEffect/SetTexture(TextureCube) API: weak lifetime tracking and the real
// CNA_SKIA_SKSL_V1 preamble wiring are SKIA-149's job. This spike only proves the SkSL formula
// itself, compiled and run against real pixels, exactly as SKIA-93 proved stock-effect fragment
// pieces before any public integration existed.

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

using CNA::Internal::Backends::Skia::SkiaSurface;

namespace
{
    int failures = 0;

    void Check(bool condition, const std::string& label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label.c_str());
        if (!condition) ++failures;
    }

    // The exact preamble docs/skia-cube-volume-sampling-contract.md specifies: six named face
    // children, the D3D dominant-axis table (deterministic corner tie-break: X before Y before Z,
    // positive before negative), and cnaSampleCubeEXT(dir) as the only entry point an author (or,
    // here, the spike's own wrapper) calls.
    constexpr char kCnaSampleCubePreamble[] = R"(
        uniform shader cnaCubeFace0;
        uniform shader cnaCubeFace1;
        uniform shader cnaCubeFace2;
        uniform shader cnaCubeFace3;
        uniform shader cnaCubeFace4;
        uniform shader cnaCubeFace5;
        uniform float cnaCubeFaceSizeEXT;

        half4 cnaSampleCubeEXT(float3 dir) {
            float ax = abs(dir.x);
            float ay = abs(dir.y);
            float az = abs(dir.z);
            float ma;
            float2 uv;
            int face;
            if (ax >= ay && ax >= az) {
                if (dir.x >= 0.0) { ma = dir.x; uv = float2(-dir.z, -dir.y); face = 0; }
                else { ma = -dir.x; uv = float2(dir.z, -dir.y); face = 1; }
            } else if (ay >= ax && ay >= az) {
                if (dir.y >= 0.0) { ma = dir.y; uv = float2(dir.x, dir.z); face = 2; }
                else { ma = -dir.y; uv = float2(dir.x, -dir.z); face = 3; }
            } else {
                if (dir.z >= 0.0) { ma = dir.z; uv = float2(dir.x, -dir.y); face = 4; }
                else { ma = -dir.z; uv = float2(-dir.x, -dir.y); face = 5; }
            }
            float2 st = clamp(0.5 * (uv / ma + 1.0), 0.0, 1.0);
            float2 px = st * cnaCubeFaceSizeEXT;
            if (face == 0) return cnaCubeFace0.eval(px);
            if (face == 1) return cnaCubeFace1.eval(px);
            if (face == 2) return cnaCubeFace2.eval(px);
            if (face == 3) return cnaCubeFace3.eval(px);
            if (face == 4) return cnaCubeFace4.eval(px);
            return cnaCubeFace5.eval(px);
        }

        uniform float3 testDirection;
        half4 main(float2 sourcePixel) {
            return cnaSampleCubeEXT(testDirection);
        }
    )";

    // Each face is a 2x2 image with four distinct quadrant colours in CNA's own top-row-first
    // convention: row 0 = top (v<0.5), row 1 = bottom (v>=0.5); column 0 = left (u<0.5), column 1
    // = right (u>=0.5). TL=Red, TR=Green, BL=Blue, BR=Yellow.
    constexpr std::uint8_t kRed[4]    = {255, 0, 0, 255};
    constexpr std::uint8_t kGreen[4]  = {0, 255, 0, 255};
    constexpr std::uint8_t kBlue[4]   = {0, 0, 255, 255};
    constexpr std::uint8_t kYellow[4] = {255, 255, 0, 255};

    [[nodiscard]] sk_sp<SkImage> MakeQuadrantFace()
    {
        std::array<std::uint8_t, 16> rgba{};
        std::memcpy(rgba.data() + 0, kRed, 4);
        std::memcpy(rgba.data() + 4, kGreen, 4);
        std::memcpy(rgba.data() + 8, kBlue, 4);
        std::memcpy(rgba.data() + 12, kYellow, 4);
        SkiaSurface source(2, 2);
        if (!source.WritePixels(0, 0, 2, 2, rgba.data(), 2 * 4))
            return nullptr;
        return source.SnapshotImage();
    }

    [[nodiscard]] bool SamePixel(const std::vector<std::uint8_t>& pixels, std::size_t index,
                                const std::uint8_t expected[4])
    {
        const std::size_t offset = index * 4u;
        return offset + 4u <= pixels.size()
            && std::memcmp(pixels.data() + offset, expected, 4u) == 0;
    }

    struct DirectionCase final
    {
        const char* label;
        float x, y, z;
        const std::uint8_t* expected;
    };

    // Draws one cnaSampleCubeEXT(direction) result into a 1x1 rect at (0, row) and returns its
    // resolved RGBA8 pixel, or an empty vector on any instantiation failure.
    [[nodiscard]] std::vector<std::uint8_t> SampleCubeEXT(
        const SkRuntimeEffect& effect, const SkRuntimeEffect::Uniform& directionUniform,
        const SkRuntimeEffect::Uniform& faceSizeUniform,
        const std::array<sk_sp<SkImage>, 6>& faces, float x, float y, float z, float faceSize,
        SkFilterMode filter)
    {
        std::vector<std::uint8_t> uniformBytes(effect.uniformSize(), 0u);
        const float direction[3] = {x, y, z};
        std::memcpy(uniformBytes.data() + directionUniform.offset, direction, sizeof(direction));
        std::memcpy(uniformBytes.data() + faceSizeUniform.offset, &faceSize, sizeof(faceSize));

        std::array<sk_sp<SkShader>, 6> children;
        for (std::size_t face = 0; face < faces.size(); ++face)
        {
            children[face] = faces[face]->makeShader(
                SkTileMode::kClamp, SkTileMode::kClamp, SkSamplingOptions(filter));
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
}

int main()
{
    const auto compiled = SkRuntimeEffect::MakeForShader(SkString(kCnaSampleCubePreamble));
    Check(compiled.effect != nullptr,
          "cnaSampleCubeEXT preamble compiles for the pinned raster Skia ("
              + std::string(compiled.effect ? "" : compiled.errorText.c_str()) + ")");
    if (!compiled.effect)
    {
        std::printf("=== %d/%d PASS ===\n", 1 - failures, 1);
        return 1;
    }

    std::array<sk_sp<SkImage>, 6> faces;
    bool allFacesBuilt = true;
    for (auto& face : faces)
    {
        face = MakeQuadrantFace();
        allFacesBuilt = allFacesBuilt && face != nullptr;
    }
    Check(allFacesBuilt, "six 2x2 quadrant-coloured face images build");

    // Six BR-biased (Yellow-expected) directions, one per face, each computed from
    // docs/skia-cube-volume-sampling-contract.md's per-face (ma, uc, vc) formula so that BOTH
    // uc/ma and vc/ma land positive (u>=0.5, v>=0.5) -- this proves face SELECTION (only the
    // correct dominant axis reaches that face's branch at all) and UV SIGN/ORIENTATION (a
    // transposed or sign-flipped per-face formula would land in a different quadrant) together,
    // independently for every face.
    const std::array<DirectionCase, 7> cases{{
        {"+X (0) dominant-axis selection and UV sign/orientation", 1.0f, -0.5f, -0.5f, kYellow},
        {"-X (1) dominant-axis selection and UV sign/orientation", -1.0f, -0.5f, 0.5f, kYellow},
        {"+Y (2) dominant-axis selection and UV sign/orientation", 0.5f, 1.0f, 0.5f, kYellow},
        {"-Y (3) dominant-axis selection and UV sign/orientation", 0.5f, -1.0f, -0.5f, kYellow},
        {"+Z (4) dominant-axis selection and UV sign/orientation", 0.5f, -0.5f, 1.0f, kYellow},
        {"-Z (5) dominant-axis selection and UV sign/orientation", -0.5f, -0.5f, -1.0f, kYellow},
        {"(1,1,1) corner ties resolve to +X face, TL quadrant (documented deterministic tie-break)",
         1.0f, 1.0f, 1.0f, kRed},
    }};

    const SkRuntimeEffect::Uniform* directionUniform = compiled.effect->findUniform("testDirection");
    const SkRuntimeEffect::Uniform* faceSizeUniform =
        compiled.effect->findUniform("cnaCubeFaceSizeEXT");
    Check(directionUniform != nullptr && faceSizeUniform != nullptr,
          "reflection finds both testDirection and cnaCubeFaceSizeEXT uniforms");

    bool allDrawsSucceeded = allFacesBuilt && directionUniform && faceSizeUniform;
    std::vector<std::vector<std::uint8_t>> results(cases.size());
    for (std::size_t row = 0; allDrawsSucceeded && row < cases.size(); ++row)
    {
        results[row] = SampleCubeEXT(*compiled.effect, *directionUniform, *faceSizeUniform, faces,
                                     cases[row].x, cases[row].y, cases[row].z, 2.0f,
                                     SkFilterMode::kNearest);
        allDrawsSucceeded = allDrawsSucceeded && !results[row].empty();
    }
    Check(allDrawsSucceeded, "every direction case's shader instantiates and draws");

    for (std::size_t row = 0; row < cases.size(); ++row)
        Check(SamePixel(results[row], 0, cases[row].expected), cases[row].label);

    // Point-versus-Linear filtering must be genuinely observable through cnaSampleCubeEXT's
    // pixel-space child sample, not silently collapsed to one mode. On the +X face, direction
    // (1, 0.2, 0) lands at u=0.5 exactly (px.x=1.0 on a 2-wide face -- the TL/TR quadrant
    // boundary) and v<0.5 (top half, TL=Red/TR=Green side): Nearest must snap to one pure
    // quadrant colour, while Linear -- sampling exactly between the two texel centres -- must
    // blend both, landing on neither pure colour.
    if (allFacesBuilt && directionUniform && faceSizeUniform)
    {
        const std::vector<std::uint8_t> nearestBoundary = SampleCubeEXT(
            *compiled.effect, *directionUniform, *faceSizeUniform, faces, 1.0f, 0.2f, 0.0f, 2.0f,
            SkFilterMode::kNearest);
        const std::vector<std::uint8_t> linearBoundary = SampleCubeEXT(
            *compiled.effect, *directionUniform, *faceSizeUniform, faces, 1.0f, 0.2f, 0.0f, 2.0f,
            SkFilterMode::kLinear);
        const bool nearestIsPure = nearestBoundary.size() >= 4
            && (SamePixel(nearestBoundary, 0, kRed) || SamePixel(nearestBoundary, 0, kGreen));
        const bool linearIsBlended = linearBoundary.size() >= 4
            && !SamePixel(linearBoundary, 0, kRed) && !SamePixel(linearBoundary, 0, kGreen)
            && linearBoundary[0] > 0 && linearBoundary[1] > 0;
        Check(nearestIsPure && linearIsBlended,
              "Point sampling snaps to one pure quadrant while Linear blends across the same "
              "quadrant boundary -- filter mode is observable through real pixels, not ignored");
    }
    else
    {
        Check(false, "Point/Linear comparison requires the earlier setup to have succeeded");
    }

    const int total = static_cast<int>(cases.size()) + 4;
    std::printf("=== %d/%d PASS ===\n", total - failures, total);
    return failures == 0 ? 0 : 1;
}
