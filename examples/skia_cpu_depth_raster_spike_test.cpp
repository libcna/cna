// SPDX-License-Identifier: MS-PL
// SKIA-97: bounded CPU colour/depth rasterization and Skia handoff feasibility spike.
//
// SKIA-96 proved that SkVertices loses clip W and cannot preserve EasyGL's perspective varyings.
// This test therefore owns coverage, perspective interpolation and depth in CPU memory. It hands
// Skia only a completed opaque RGBA8 image and remains deliberately disconnected from public Draw*.

#include "CNA/Internal/Renderers/Skia/SkiaSurface.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <vector>

using CNA::Internal::Renderers::Skia::SkiaSurface;

namespace
{
    constexpr std::size_t kMaximumBytes = 256u * 1024u * 1024u;
    constexpr int kMaximumAxis = 16384;

    int failures = 0;

    void Check(bool condition, const char* label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
        if (!condition) ++failures;
    }

    struct FloatColor
    {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = 1.0f;
    };

    struct RasterVertex
    {
        float x = 0.0f;
        float y = 0.0f;
        float depth = 0.0f;       // Post-divide viewport depth, affine in screen space.
        float reciprocalW = 1.0f; // Retained for perspective-correct colour/varying interpolation.
        FloatColor color{};
    };

    [[nodiscard]] std::uint8_t ToByte(float value)
    {
        return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
    }

    [[nodiscard]] std::size_t CheckedPixelCount(int width, int height)
    {
        if (width <= 0 || height <= 0 || width > kMaximumAxis || height > kMaximumAxis)
            throw std::invalid_argument("CPU raster target dimensions must be within 1..16384");
        const std::size_t w = static_cast<std::size_t>(width);
        const std::size_t h = static_cast<std::size_t>(height);
        if (w > std::numeric_limits<std::size_t>::max() / h)
            throw std::length_error("CPU raster target pixel count overflow");
        const std::size_t pixels = w * h;
        constexpr std::size_t bytesPerPixel = 4u + sizeof(float);
        if (pixels > kMaximumBytes / bytesPerPixel)
            throw std::length_error("CPU raster target exceeds the 256 MiB colour+depth limit");
        return pixels;
    }

    class CpuDepthTarget
    {
    public:
        CpuDepthTarget(int width, int height)
            : width_(width), height_(height), pixelCount_(CheckedPixelCount(width, height)),
              color_(pixelCount_ * 4u), depth_(pixelCount_)
        {
            Clear({0.0f, 0.0f, 0.0f, 1.0f}, 1.0f);
        }

        [[nodiscard]] int Width() const { return width_; }
        [[nodiscard]] int Height() const { return height_; }
        [[nodiscard]] std::size_t OwnedBytes() const
        {
            return color_.size() + depth_.size() * sizeof(float);
        }

        void ClearColor(const FloatColor& value)
        {
            const std::array<std::uint8_t, 4> rgba = {
                ToByte(value.r), ToByte(value.g), ToByte(value.b), ToByte(value.a),
            };
            for (std::size_t pixel = 0; pixel < pixelCount_; ++pixel)
                std::copy(rgba.begin(), rgba.end(), color_.begin() + pixel * 4u);
        }

        void ClearDepth(float value)
        {
            std::fill(depth_.begin(), depth_.end(), value);
        }

        void Clear(const FloatColor& color, float depth)
        {
            ClearColor(color);
            ClearDepth(depth);
        }

        [[nodiscard]] bool RasterizeTriangle(const std::array<RasterVertex, 3>& vertices)
        {
            const float denominator =
                (vertices[1].y - vertices[2].y) * (vertices[0].x - vertices[2].x) +
                (vertices[2].x - vertices[1].x) * (vertices[0].y - vertices[2].y);
            if (!std::isfinite(denominator) || denominator == 0.0f)
                return false;
            for (const RasterVertex& vertex : vertices)
            {
                if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) ||
                    !std::isfinite(vertex.depth) || !std::isfinite(vertex.reciprocalW) ||
                    vertex.reciprocalW <= 0.0f)
                    return false;
            }

            const float minXf = std::min({vertices[0].x, vertices[1].x, vertices[2].x});
            const float maxXf = std::max({vertices[0].x, vertices[1].x, vertices[2].x});
            const float minYf = std::min({vertices[0].y, vertices[1].y, vertices[2].y});
            const float maxYf = std::max({vertices[0].y, vertices[1].y, vertices[2].y});
            const int minX = std::max(0, static_cast<int>(std::floor(minXf)));
            const int maxX = std::min(width_ - 1, static_cast<int>(std::ceil(maxXf)) - 1);
            const int minY = std::max(0, static_cast<int>(std::floor(minYf)));
            const int maxY = std::min(height_ - 1, static_cast<int>(std::ceil(maxYf)) - 1);

            bool wrotePixel = false;
            for (int y = minY; y <= maxY; ++y)
            {
                for (int x = minX; x <= maxX; ++x)
                {
                    const float sampleX = x + 0.5f;
                    const float sampleY = y + 0.5f;
                    const float lambda0 =
                        ((vertices[1].y - vertices[2].y) * (sampleX - vertices[2].x) +
                         (vertices[2].x - vertices[1].x) * (sampleY - vertices[2].y)) /
                        denominator;
                    const float lambda1 =
                        ((vertices[2].y - vertices[0].y) * (sampleX - vertices[2].x) +
                         (vertices[0].x - vertices[2].x) * (sampleY - vertices[2].y)) /
                        denominator;
                    const float lambda2 = 1.0f - lambda0 - lambda1;
                    constexpr float edgeEpsilon = -1.0e-6f;
                    if (lambda0 < edgeEpsilon || lambda1 < edgeEpsilon || lambda2 < edgeEpsilon)
                        continue;

                    const float fragmentDepth =
                        lambda0 * vertices[0].depth + lambda1 * vertices[1].depth +
                        lambda2 * vertices[2].depth;
                    const std::size_t pixel = static_cast<std::size_t>(y) * width_ + x;
                    // EasyGL/XNA default depth compare is LessEqual with writes enabled.
                    if (fragmentDepth > depth_[pixel])
                        continue;

                    const float perspectiveDenominator =
                        lambda0 * vertices[0].reciprocalW +
                        lambda1 * vertices[1].reciprocalW +
                        lambda2 * vertices[2].reciprocalW;
                    if (!(perspectiveDenominator > 0.0f) ||
                        !std::isfinite(perspectiveDenominator))
                        continue;
                    const float weights[3] = {
                        lambda0 * vertices[0].reciprocalW / perspectiveDenominator,
                        lambda1 * vertices[1].reciprocalW / perspectiveDenominator,
                        lambda2 * vertices[2].reciprocalW / perspectiveDenominator,
                    };
                    const FloatColor fragment = {
                        weights[0] * vertices[0].color.r + weights[1] * vertices[1].color.r +
                            weights[2] * vertices[2].color.r,
                        weights[0] * vertices[0].color.g + weights[1] * vertices[1].color.g +
                            weights[2] * vertices[2].color.g,
                        weights[0] * vertices[0].color.b + weights[1] * vertices[1].color.b +
                            weights[2] * vertices[2].color.b,
                        weights[0] * vertices[0].color.a + weights[1] * vertices[1].color.a +
                            weights[2] * vertices[2].color.a,
                    };
                    const std::size_t offset = pixel * 4u;
                    color_[offset + 0u] = ToByte(fragment.r);
                    color_[offset + 1u] = ToByte(fragment.g);
                    color_[offset + 2u] = ToByte(fragment.b);
                    color_[offset + 3u] = ToByte(fragment.a);
                    depth_[pixel] = fragmentDepth;
                    wrotePixel = true;
                }
            }
            return wrotePixel;
        }

        [[nodiscard]] std::array<std::uint8_t, 4> Pixel(int x, int y) const
        {
            if (x < 0 || y < 0 || x >= width_ || y >= height_)
                return {};
            const std::size_t offset =
                (static_cast<std::size_t>(y) * width_ + x) * 4u;
            return {color_[offset], color_[offset + 1u], color_[offset + 2u], color_[offset + 3u]};
        }

        [[nodiscard]] float Depth(int x, int y) const
        {
            if (x < 0 || y < 0 || x >= width_ || y >= height_)
                return std::numeric_limits<float>::quiet_NaN();
            return depth_[static_cast<std::size_t>(y) * width_ + x];
        }

        [[nodiscard]] bool HandoffTo(SkiaSurface& surface) const
        {
            if (surface.Width() != width_ || surface.Height() != height_)
                return false;
            return surface.WritePixels(0, 0, width_, height_, color_.data(), width_ * 4);
        }

    private:
        int width_;
        int height_;
        std::size_t pixelCount_;
        std::vector<std::uint8_t> color_;
        std::vector<float> depth_;
    };

    class CpuRasterContext
    {
    public:
        void Bind(CpuDepthTarget& target) { target_ = &target; }
        [[nodiscard]] bool HasTarget() const { return target_ != nullptr; }

        void Clear(const FloatColor& color, float depth)
        {
            if (!target_) throw std::logic_error("CPU raster context has no target");
            target_->Clear(color, depth);
        }

        [[nodiscard]] bool Draw(const std::array<RasterVertex, 3>& vertices)
        {
            if (!target_) throw std::logic_error("CPU raster context has no target");
            return target_->RasterizeTriangle(vertices);
        }

    private:
        CpuDepthTarget* target_ = nullptr;
    };

    [[nodiscard]] std::array<RasterVertex, 3> SolidTriangle(
        int width, int height, float depth, const FloatColor& color)
    {
        const float inset = 4.0f;
        return {
            RasterVertex{inset, inset, depth, 1.0f, color},
            RasterVertex{static_cast<float>(width) - inset, inset, depth, 1.0f, color},
            RasterVertex{inset, static_cast<float>(height) - inset, depth, 1.0f, color},
        };
    }

    [[nodiscard]] bool IsPixel(
        const std::array<std::uint8_t, 4>& actual,
        const std::array<std::uint8_t, 4>& expected,
        int tolerance = 0)
    {
        for (std::size_t component = 0; component < actual.size(); ++component)
            if (std::abs(static_cast<int>(actual[component]) - expected[component]) > tolerance)
                return false;
        return true;
    }

    [[nodiscard]] std::array<std::uint8_t, 4> SurfacePixel(
        const SkiaSurface& surface, int x, int y)
    {
        const std::vector<std::uint8_t> rgba = surface.SnapshotRgba();
        const std::size_t offset =
            (static_cast<std::size_t>(y) * surface.Width() + x) * 4u;
        if (offset > rgba.size() || 4u > rgba.size() - offset)
            return {};
        return {rgba[offset], rgba[offset + 1u], rgba[offset + 2u], rgba[offset + 3u]};
    }
}

int main()
{
    constexpr FloatColor black{0.0f, 0.0f, 0.0f, 1.0f};
    constexpr FloatColor red{1.0f, 0.0f, 0.0f, 1.0f};
    constexpr FloatColor green{0.0f, 1.0f, 0.0f, 1.0f};
    constexpr FloatColor blue{0.0f, 0.0f, 1.0f, 1.0f};
    constexpr FloatColor yellow{1.0f, 1.0f, 0.0f, 1.0f};

    bool invalidDimensionsRejected = false;
    bool oversizedStorageRejected = false;
    try { CpuDepthTarget invalid(0, 1); }
    catch (const std::invalid_argument&) { invalidDimensionsRejected = true; }
    try { CpuDepthTarget oversized(kMaximumAxis, kMaximumAxis); }
    catch (const std::length_error&) { oversizedStorageRejected = true; }
    Check(invalidDimensionsRejected && oversizedStorageRejected,
          "CPU target rejects invalid axes and colour+depth storage above 256 MiB before allocation");

    CpuDepthTarget depthTarget(48, 48);
    const auto farRed = SolidTriangle(48, 48, 0.8f, red);
    const auto nearGreen = SolidTriangle(48, 48, 0.2f, green);
    depthTarget.Clear(black, 1.0f);
    const bool farThenNear =
        depthTarget.RasterizeTriangle(farRed) && depthTarget.RasterizeTriangle(nearGreen);
    Check(farThenNear && IsPixel(depthTarget.Pixel(16, 16), {0, 255, 0, 255}) &&
              std::abs(depthTarget.Depth(16, 16) - 0.2f) < 1.0e-6f,
          "near triangle wins after far triangle and writes its depth");

    depthTarget.Clear(black, 1.0f);
    const bool nearWritten = depthTarget.RasterizeTriangle(nearGreen);
    const bool farFullyRejected = !depthTarget.RasterizeTriangle(farRed);
    Check(nearWritten && farFullyRejected &&
              IsPixel(depthTarget.Pixel(16, 16), {0, 255, 0, 255}) &&
              std::abs(depthTarget.Depth(16, 16) - 0.2f) < 1.0e-6f,
          "depth ordering is draw-order independent for opaque triangles");

    depthTarget.Clear(blue, 0.1f);
    const bool blockedBeforeDepthClear = !depthTarget.RasterizeTriangle(farRed) &&
        IsPixel(depthTarget.Pixel(16, 16), {0, 0, 255, 255});
    depthTarget.ClearDepth(1.0f);
    const bool drawnAfterDepthClear = depthTarget.RasterizeTriangle(farRed);
    Check(blockedBeforeDepthClear && drawnAfterDepthClear &&
              IsPixel(depthTarget.Pixel(16, 16), {255, 0, 0, 255}) &&
              std::abs(depthTarget.Depth(16, 16) - 0.8f) < 1.0e-6f,
          "depth-only clear re-enables a farther draw without changing the colour clear contract");

    // Recreate SKIA-96's same-screen triangle and W=(1,4,1), but retain reciprocal W through
    // rasterization. Black/white/black vertex colour encodes the discriminating varying directly.
    CpuDepthTarget perspectiveTarget(64, 64);
    perspectiveTarget.Clear(black, 1.0f);
    const std::array<RasterVertex, 3> perspectiveTriangle = {
        RasterVertex{8.0f, 8.0f, 0.5f, 1.0f, black},
        RasterVertex{56.0f, 8.0f, 0.5f, 0.25f, {1.0f, 1.0f, 1.0f, 1.0f}},
        RasterVertex{8.0f, 56.0f, 0.5f, 1.0f, black},
    };
    const bool perspectiveDrawn = perspectiveTarget.RasterizeTriangle(perspectiveTriangle);
    const std::array<std::uint8_t, 4> perspectivePixel = perspectiveTarget.Pixel(24, 24);
    std::printf("[INFO] CPU perspective sample=%u (SKIA-96 EasyGL expectation=30, SkVertices=88)\n",
                static_cast<unsigned int>(perspectivePixel[0]));
    Check(perspectiveDrawn && IsPixel(perspectivePixel, {30, 30, 30, 255}, 1),
          "CPU reciprocal-W interpolation recovers the SKIA-96 EasyGL result");

    CpuDepthTarget targetA(32, 32);
    CpuDepthTarget targetB(24, 16);
    CpuRasterContext context;
    context.Bind(targetA);
    context.Clear(black, 1.0f);
    const bool aFar = context.Draw(SolidTriangle(32, 32, 0.8f, red));
    context.Bind(targetB);
    context.Clear(blue, 1.0f);
    const bool bNear = context.Draw(SolidTriangle(24, 16, 0.3f, yellow));
    context.Bind(targetA);
    const bool aNear = context.Draw(SolidTriangle(32, 32, 0.2f, green));
    Check(context.HasTarget() && aFar && bNear && aNear &&
              IsPixel(targetA.Pixel(10, 10), {0, 255, 0, 255}) &&
              IsPixel(targetB.Pixel(8, 8), {255, 255, 0, 255}) &&
              std::abs(targetA.Depth(10, 10) - 0.2f) < 1.0e-6f &&
              std::abs(targetB.Depth(8, 8) - 0.3f) < 1.0e-6f,
          "A-to-B-to-A target switching preserves independent colour and depth storage");

    SkiaSurface surfaceA(32, 32);
    SkiaSurface surfaceB(24, 16);
    surfaceA.Clear(1.0f, 0.0f, 1.0f, 1.0f);
    surfaceB.Clear(1.0f, 0.0f, 1.0f, 1.0f);
    const bool handoffA = targetA.HandoffTo(surfaceA);
    const bool handoffB = targetB.HandoffTo(surfaceB);
    Check(handoffA && handoffB &&
              IsPixel(SurfacePixel(surfaceA, 10, 10), {0, 255, 0, 255}) &&
              IsPixel(SurfacePixel(surfaceB, 8, 8), {255, 255, 0, 255}),
          "whole-target RGBA8 WritePixels handoff reaches the matching Skia surfaces exactly");

    SkiaSurface wrongSize(1, 1);
    wrongSize.Clear(1.0f, 0.0f, 1.0f, 1.0f);
    Check(!targetA.HandoffTo(wrongSize) &&
              IsPixel(SurfacePixel(wrongSize, 0, 0), {255, 0, 255, 255}),
          "dimension-mismatched handoff rejects without mutating the Skia destination");

    constexpr int performanceWidth = 640;
    constexpr int performanceHeight = 360;
    constexpr int triangleCount = 128;
    CpuDepthTarget performanceTarget(performanceWidth, performanceHeight);
    const auto clearStart = std::chrono::steady_clock::now();
    performanceTarget.Clear(black, 1.0f);
    const auto clearEnd = std::chrono::steady_clock::now();
    bool allPerformanceDraws = true;
    for (int triangle = 0; triangle < triangleCount; ++triangle)
    {
        const float depth = 0.99f - static_cast<float>(triangle) / 256.0f;
        const FloatColor shade{
            static_cast<float>(triangle + 1) / triangleCount,
            0.25f,
            1.0f - static_cast<float>(triangle) / triangleCount,
            1.0f,
        };
        allPerformanceDraws &= performanceTarget.RasterizeTriangle({
            RasterVertex{20.0f, 20.0f, depth, 1.0f, shade},
            RasterVertex{620.0f, 20.0f, depth, 1.0f, shade},
            RasterVertex{20.0f, 340.0f, depth, 1.0f, shade},
        });
    }
    const auto rasterEnd = std::chrono::steady_clock::now();
    SkiaSurface performanceSurface(performanceWidth, performanceHeight);
    const bool performanceHandoff = performanceTarget.HandoffTo(performanceSurface);
    const auto handoffEnd = std::chrono::steady_clock::now();
    const auto clearUs =
        std::chrono::duration_cast<std::chrono::microseconds>(clearEnd - clearStart).count();
    const auto rasterUs =
        std::chrono::duration_cast<std::chrono::microseconds>(rasterEnd - clearEnd).count();
    const auto handoffUs =
        std::chrono::duration_cast<std::chrono::microseconds>(handoffEnd - rasterEnd).count();
    std::printf("[INFO] 640x360 CPU target: bytes=%zu clear_us=%lld "
                "raster_%d_triangles_us=%lld handoff_us=%lld\n",
                performanceTarget.OwnedBytes(), static_cast<long long>(clearUs), triangleCount,
                static_cast<long long>(rasterUs), static_cast<long long>(handoffUs));
    Check(performanceTarget.OwnedBytes() == 1843200u && allPerformanceDraws &&
              performanceHandoff && clearUs >= 0 && rasterUs > 0 && handoffUs >= 0 &&
              rasterUs < 10000000,
          "memory and clear/raster/handoff timings are bounded and reported for 640x360");

    std::printf("=== %s ===\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
