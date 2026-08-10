// SPDX-License-Identifier: MS-PL
// SKIA-96: headless feasibility spike for one projected VertexPositionColorTexture TriangleList.
//
// This deliberately stays below GraphicsDevice and the public Skia renderer. EasyGL's corresponding
// GLSL path computes gl_Position=uWVP*vec4(aPos,1) and exports unqualified colour/UV varyings, so
// rasterization performs homogeneous clipping and perspective-correct interpolation. SkVertices
// accepts only post-projection SkPoint positions and has no per-vertex clip W. The checks below
// separate the bounded equal-W case from those missing semantics; expected mismatches are evidence,
// not support claims.

#include "CNA/Internal/Renderers/Skia/SkiaSurface.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkImage.h"
#include "include/core/SkPaint.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkShader.h"
#include "include/core/SkTileMode.h"
#include "include/core/SkVertices.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using CNA::Internal::Renderers::Skia::SkiaSurface;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;

namespace
{
    constexpr int kExtent = 64;
    constexpr int kSampleX = 24;
    constexpr int kSampleY = 24;

    int failures = 0;

    void Check(bool condition, const char* label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
        if (!condition) ++failures;
    }

    [[nodiscard]] bool Near(int actual, int expected, int tolerance = 3)
    {
        return std::abs(actual - expected) <= tolerance;
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

    [[nodiscard]] sk_sp<SkImage> MakeWhiteImage()
    {
        constexpr std::array<std::uint8_t, 4> white = {255, 255, 255, 255};
        return MakeImage(1, 1, white.data(), white.size());
    }

    [[nodiscard]] sk_sp<SkImage> MakeHorizontalGradient()
    {
        std::vector<std::uint8_t> rgba(256u * 4u);
        for (std::size_t x = 0; x < 256u; ++x)
        {
            rgba[x * 4u + 0u] = static_cast<std::uint8_t>(x);
            rgba[x * 4u + 1u] = static_cast<std::uint8_t>(x);
            rgba[x * 4u + 2u] = static_cast<std::uint8_t>(x);
            rgba[x * 4u + 3u] = 255;
        }
        return MakeImage(256, 1, rgba.data(), rgba.size());
    }

    [[nodiscard]] SkPoint ProjectToRaster(const Vector4& clip)
    {
        const float inverseW = 1.0f / clip.W;
        const float ndcX = clip.X * inverseW;
        const float ndcY = clip.Y * inverseW;
        return SkPoint::Make(
            (ndcX + 1.0f) * 0.5f * kExtent,
            (1.0f - ndcY) * 0.5f * kExtent);
    }

    [[nodiscard]] std::array<float, 3> Barycentric(
        const std::array<SkPoint, 3>& positions, float x, float y)
    {
        const float denominator =
            (positions[1].y() - positions[2].y()) *
                (positions[0].x() - positions[2].x()) +
            (positions[2].x() - positions[1].x()) *
                (positions[0].y() - positions[2].y());
        const float a =
            ((positions[1].y() - positions[2].y()) * (x - positions[2].x()) +
             (positions[2].x() - positions[1].x()) * (y - positions[2].y())) /
            denominator;
        const float b =
            ((positions[2].y() - positions[0].y()) * (x - positions[2].x()) +
             (positions[0].x() - positions[2].x()) * (y - positions[2].y())) /
            denominator;
        return {a, b, 1.0f - a - b};
    }

    [[nodiscard]] float SignedArea(const std::array<SkPoint, 3>& p)
    {
        return (p[1].x() - p[0].x()) * (p[2].y() - p[0].y()) -
               (p[1].y() - p[0].y()) * (p[2].x() - p[0].x());
    }

    [[nodiscard]] std::array<std::uint8_t, 4> Pixel(
        const std::vector<std::uint8_t>& rgba, int x, int y)
    {
        const std::size_t offset = (static_cast<std::size_t>(y) * kExtent + x) * 4u;
        if (offset > rgba.size() || 4u > rgba.size() - offset)
            return {};
        return {rgba[offset], rgba[offset + 1u], rgba[offset + 2u], rgba[offset + 3u]};
    }

    [[nodiscard]] bool DrawPctTriangle(
        SkiaSurface& surface,
        const std::array<SkPoint, 3>& positions,
        const std::array<SkPoint, 3>& textureCoordinates,
        const std::array<SkColor, 3>& colors,
        const sk_sp<SkImage>& texture)
    {
        if (!surface.Canvas() || !texture)
            return false;
        sk_sp<SkVertices> vertices = SkVertices::MakeCopy(
            SkVertices::kTriangles_VertexMode,
            static_cast<int>(positions.size()), positions.data(), textureCoordinates.data(),
            colors.data());
        if (!vertices)
            return false;

        SkPaint paint;
        paint.setBlendMode(SkBlendMode::kSrc);
        paint.setShader(texture->makeShader(
            SkTileMode::kClamp, SkTileMode::kClamp,
            SkSamplingOptions(SkFilterMode::kNearest)));
        if (!paint.getShader())
            return false;
        // drawVertices defines the shader as source and vertex colour as destination for this
        // argument. kModulate therefore implements BasicEffect's texture * vertexColor slice.
        surface.Canvas()->drawVertices(vertices, SkBlendMode::kModulate, paint);
        return true;
    }

    [[nodiscard]] bool IsWhite(const std::array<std::uint8_t, 4>& pixel)
    {
        return pixel[0] > 250 && pixel[1] > 250 && pixel[2] > 250 && pixel[3] > 250;
    }
}

int main()
{
    // CNA's Matrix/Vector4 path is row-vector on the CPU. EasyGL uploads the same sixteen values
    // as a GLSL column matrix, making uWVP * vec4 exactly equivalent. SkVertices cannot consume
    // this 4D result, so the spike performs the divide and top-left viewport conversion on CPU.
    const Matrix wvp(
        0.5f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.75f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.25f, -0.125f, 0.0f, 1.0f);
    const Vector4 transformed = Vector4::Transform(Vector3(-0.5f, 0.5f, 0.25f), wvp);
    const SkPoint transformedRaster = ProjectToRaster(transformed);
    Check(Near(static_cast<int>(std::lround(transformed.X * 1000.0f)), 0, 0) &&
              Near(static_cast<int>(std::lround(transformed.Y * 1000.0f)), 250, 0) &&
              Near(static_cast<int>(std::lround(transformed.Z * 1000.0f)), 250, 0) &&
              Near(static_cast<int>(std::lround(transformed.W * 1000.0f)), 1000, 0),
          "CNA row-vector WVP produces the EasyGL-uploaded clip coordinate exactly");
    Check(Near(static_cast<int>(std::lround(transformedRaster.x())), 32, 0) &&
              Near(static_cast<int>(std::lround(transformedRaster.y())), 24, 0),
          "CPU perspective divide and XNA top-left viewport mapping produce (32,24)");

    const std::array<Vector4, 3> equalWClip = {
        Vector4(-0.75f, 0.75f, 0.5f, 1.0f),
        Vector4(0.75f, 0.75f, 0.5f, 1.0f),
        Vector4(-0.75f, -0.75f, 0.5f, 1.0f),
    };
    const std::array<SkPoint, 3> rasterPositions = {
        ProjectToRaster(equalWClip[0]),
        ProjectToRaster(equalWClip[1]),
        ProjectToRaster(equalWClip[2]),
    };
    constexpr std::array<SkPoint, 3> fixedTextureCoordinates = {
        SkPoint{0.5f, 0.5f}, SkPoint{0.5f, 0.5f}, SkPoint{0.5f, 0.5f},
    };
    constexpr std::array<SkColor, 3> primaryColors = {
        SK_ColorRED, SK_ColorGREEN, SK_ColorBLUE,
    };
    const sk_sp<SkImage> white = MakeWhiteImage();
    const sk_sp<SkImage> gradient = MakeHorizontalGradient();
    Check(white != nullptr && gradient != nullptr,
          "raster texture inputs are available without a window or public 3D resource");

    SkiaSurface equalWSurface(kExtent, kExtent);
    equalWSurface.Clear(0.0f, 0.0f, 0.0f, 1.0f);
    const bool equalWDrawn = DrawPctTriangle(
        equalWSurface, rasterPositions, fixedTextureCoordinates, primaryColors, white);
    const std::array<float, 3> barycentric = Barycentric(
        rasterPositions, kSampleX + 0.5f, kSampleY + 0.5f);
    const std::array<std::uint8_t, 4> equalWPixel = Pixel(
        equalWSurface.SnapshotRgba(), kSampleX, kSampleY);
    const std::array<int, 3> equalWExpected = {
        static_cast<int>(std::lround(barycentric[0] * 255.0f)),
        static_cast<int>(std::lround(barycentric[1] * 255.0f)),
        static_cast<int>(std::lround(barycentric[2] * 255.0f)),
    };
    std::printf("[INFO] equal-W sample: Skia=(%u,%u,%u) expected=(%d,%d,%d)\n",
                static_cast<unsigned int>(equalWPixel[0]),
                static_cast<unsigned int>(equalWPixel[1]),
                static_cast<unsigned int>(equalWPixel[2]),
                equalWExpected[0], equalWExpected[1], equalWExpected[2]);
    Check(equalWDrawn && Near(equalWPixel[0], equalWExpected[0]) &&
              Near(equalWPixel[1], equalWExpected[1]) &&
              Near(equalWPixel[2], equalWExpected[2]) && equalWPixel[3] == 255,
          "equal-W SkVertices colour interpolation matches EasyGL affine/perspective result");

    // Preserve the same post-divide triangle while assigning B clip-W=4. EasyGL's unqualified
    // vUV is interpolated as (sum(lambda*u/w) / sum(lambda/w)); SkVertices receives neither W nor
    // reciprocal W and therefore has only affine screen-space texture coordinates.
    const std::array<Vector4, 3> unequalWClip = {
        Vector4(-0.75f, 0.75f, 0.5f, 1.0f),
        Vector4(3.0f, 3.0f, 2.0f, 4.0f),
        Vector4(-0.75f, -0.75f, 0.5f, 1.0f),
    };
    const std::array<SkPoint, 3> unequalWPositions = {
        ProjectToRaster(unequalWClip[0]),
        ProjectToRaster(unequalWClip[1]),
        ProjectToRaster(unequalWClip[2]),
    };
    constexpr std::array<SkPoint, 3> gradientCoordinates = {
        SkPoint{0.5f, 0.5f}, SkPoint{255.5f, 0.5f}, SkPoint{0.5f, 0.5f},
    };
    constexpr std::array<SkColor, 3> whiteColors = {
        SK_ColorWHITE, SK_ColorWHITE, SK_ColorWHITE,
    };
    SkiaSurface perspectiveSurface(kExtent, kExtent);
    perspectiveSurface.Clear(0.0f, 0.0f, 0.0f, 1.0f);
    const bool perspectiveDrawn = DrawPctTriangle(
        perspectiveSurface, unequalWPositions, gradientCoordinates, whiteColors, gradient);
    const int actualGradient = Pixel(
        perspectiveSurface.SnapshotRgba(), kSampleX, kSampleY)[0];
    const float affineU = barycentric[1];
    const float perspectiveDenominator =
        barycentric[0] / unequalWClip[0].W +
        barycentric[1] / unequalWClip[1].W +
        barycentric[2] / unequalWClip[2].W;
    const float easyGlPerspectiveU =
        (barycentric[1] / unequalWClip[1].W) / perspectiveDenominator;
    const int affineGradient = static_cast<int>(std::lround(affineU * 255.0f));
    const int easyGlGradient = static_cast<int>(std::lround(easyGlPerspectiveU * 255.0f));
    std::printf("[INFO] unequal-W sample: Skia=%d affine=%d EasyGL-perspective=%d\n",
                actualGradient, affineGradient, easyGlGradient);
    Check(perspectiveDrawn && Near(actualGradient, affineGradient, 4),
          "SkVertices observably uses affine post-projection texture interpolation");
    Check(std::abs(actualGradient - easyGlGradient) >= 32,
          "unequal clip W records the first material mismatch with EasyGL perspective varyings");

    // EasyGL's GL clip volume rejects z < -w before perspective division. Feeding only projected
    // XY to SkVertices discards z/w, so the un-clipped apex still covers a pixel above the two
    // expected near-plane intersections (both at raster y=32).
    const std::array<Vector4, 3> nearClip = {
        Vector4(-0.75f, -0.75f, 0.0f, 1.0f),
        Vector4(0.75f, -0.75f, 0.0f, 1.0f),
        Vector4(0.0f, 0.75f, -2.0f, 1.0f),
    };
    const std::array<SkPoint, 3> nearClipPositions = {
        ProjectToRaster(nearClip[0]), ProjectToRaster(nearClip[1]), ProjectToRaster(nearClip[2]),
    };
    SkiaSurface clipSurface(kExtent, kExtent);
    clipSurface.Clear(0.0f, 0.0f, 0.0f, 1.0f);
    const bool unclippedDrawn = DrawPctTriangle(
        clipSurface, nearClipPositions, fixedTextureCoordinates, whiteColors, white);
    const std::array<std::uint8_t, 4> invalidApexPixel = Pixel(
        clipSurface.SnapshotRgba(), 32, 16);
    Check(nearClip[2].Z < -nearClip[2].W && unclippedDrawn && IsWhite(invalidApexPixel),
          "SkVertices has no homogeneous near-plane clipping; CPU clipping would be mandatory");

    // SkVertices exposes no cull mode. Reversing B/C changes signed area but both triangles draw;
    // an emulator would have to apply EasyGL's configured front-face rule before submission.
    const std::array<SkPoint, 3> reversedPositions = {
        rasterPositions[0], rasterPositions[2], rasterPositions[1],
    };
    SkiaSurface forwardSurface(kExtent, kExtent);
    SkiaSurface reversedSurface(kExtent, kExtent);
    forwardSurface.Clear(0.0f, 0.0f, 0.0f, 1.0f);
    reversedSurface.Clear(0.0f, 0.0f, 0.0f, 1.0f);
    const bool forwardDrawn = DrawPctTriangle(
        forwardSurface, rasterPositions, fixedTextureCoordinates, whiteColors, white);
    const bool reversedDrawn = DrawPctTriangle(
        reversedSurface, reversedPositions, fixedTextureCoordinates, whiteColors, white);
    Check(SignedArea(rasterPositions) == -SignedArea(reversedPositions) &&
              forwardDrawn && reversedDrawn &&
              IsWhite(Pixel(forwardSurface.SnapshotRgba(), kSampleX, kSampleY)) &&
              IsWhite(Pixel(reversedSurface.SnapshotRgba(), kSampleX, kSampleY)),
          "both windings render; EasyGL culling requires explicit CPU signed-area filtering");

    std::printf("=== %s ===\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
