// SPDX-License-Identifier: MS-PL
// SKIA-100: isolated CPU stock-effect feasibility spike.
//
// This deliberately implements only BasicEffect's unlit textured PCT route. It reuses the
// CPU-bridge contracts established by SKIA-96--99, hands Skia a completed RGBA8 image, and keeps
// every public buffer/Draw/effect/capability path unchanged. The closed requirement inventory at
// the end prevents the successful route from being mistaken for the other stock-effect families.

#include "CNA/Internal/Backends/Skia/SkiaSurface.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

using CNA::Internal::Backends::Skia::SkiaSurface;

namespace
{
    constexpr int kMaximumAxis = 16384;
    constexpr std::size_t kMaximumBytes = 256u * 1024u * 1024u;
    int failures = 0;

    void Check(bool condition, const char* label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
        if (!condition) ++failures;
    }

    struct ColorF
    {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = 1.0f;
    };

    [[nodiscard]] ColorF operator*(const ColorF& left, const ColorF& right)
    {
        return {left.r * right.r, left.g * right.g, left.b * right.b, left.a * right.a};
    }

    [[nodiscard]] bool IsFinite(const ColorF& color)
    {
        return std::isfinite(color.r) && std::isfinite(color.g) &&
               std::isfinite(color.b) && std::isfinite(color.a);
    }

    [[nodiscard]] std::uint8_t ToByte(float value)
    {
        return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
    }

    [[nodiscard]] std::size_t CheckedPixels(int width, int height, std::size_t bytesPerPixel)
    {
        if (width <= 0 || height <= 0 || width > kMaximumAxis || height > kMaximumAxis)
            throw std::invalid_argument("CPU effect resource dimensions must be within 1..16384");
        const std::size_t w = static_cast<std::size_t>(width);
        const std::size_t h = static_cast<std::size_t>(height);
        if (w > std::numeric_limits<std::size_t>::max() / h)
            throw std::length_error("CPU effect resource pixel count overflow");
        const std::size_t pixels = w * h;
        if (bytesPerPixel == 0u || pixels > kMaximumBytes / bytesPerPixel)
            throw std::length_error("CPU effect resource exceeds the 256 MiB limit");
        return pixels;
    }

    class CpuTexture2D
    {
    public:
        CpuTexture2D(int width, int height, std::vector<std::uint8_t> rgba)
            : width_(width), height_(height), pixels_(CheckedPixels(width, height, 4u))
        {
            if (rgba.size() != pixels_ * 4u)
                throw std::invalid_argument("CPU texture upload must contain exact RGBA8 storage");
            rgba_ = std::move(rgba);
        }

        [[nodiscard]] ColorF SamplePointClamp(float u, float v) const
        {
            if (!std::isfinite(u) || !std::isfinite(v))
                throw std::invalid_argument("CPU texture coordinates must be finite");
            const float clampedU = std::clamp(u, 0.0f, 1.0f);
            const float clampedV = std::clamp(v, 0.0f, 1.0f);
            const int x = std::min(width_ - 1, static_cast<int>(std::floor(clampedU * width_)));
            const int y = std::min(height_ - 1, static_cast<int>(std::floor(clampedV * height_)));
            const std::size_t offset =
                (static_cast<std::size_t>(y) * width_ + x) * 4u;
            constexpr float scale = 1.0f / 255.0f;
            return {
                rgba_[offset] * scale,
                rgba_[offset + 1u] * scale,
                rgba_[offset + 2u] * scale,
                rgba_[offset + 3u] * scale,
            };
        }

    private:
        int width_;
        int height_;
        std::size_t pixels_;
        std::vector<std::uint8_t> rgba_;
    };

    struct ClipVertex
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 1.0f;
        float u = 0.0f;
        float v = 0.0f;
        ColorF color{};
    };

    struct BasicEffectParams
    {
        const CpuTexture2D* texture = nullptr;
        ColorF diffuse{1.0f, 1.0f, 1.0f, 1.0f};
        ColorF emissive{0.0f, 0.0f, 0.0f, 0.0f};
        float alpha = 1.0f;
        bool textureEnabled = true;
        bool vertexColorEnabled = false;
        bool lightingEnabled = false;
        bool fogEnabled = false;
    };

    struct ScreenVertex
    {
        float x;
        float y;
        float depth;
        float reciprocalW;
        float u;
        float v;
        ColorF color;
    };

    class CpuBasicEffectTarget
    {
    public:
        CpuBasicEffectTarget(int width, int height)
            : width_(width), height_(height), pixels_(CheckedPixels(width, height, 8u)),
              rgba_(pixels_ * 4u), depth_(pixels_)
        {
            Clear({0.0f, 0.0f, 0.0f, 1.0f}, 1.0f);
        }

        void Clear(const ColorF& color, float depth)
        {
            if (!IsFinite(color) || !std::isfinite(depth))
                throw std::invalid_argument("CPU effect clear values must be finite");
            const std::array<std::uint8_t, 4> bytes = {
                ToByte(color.r), ToByte(color.g), ToByte(color.b), ToByte(color.a),
            };
            for (std::size_t pixel = 0; pixel < pixels_; ++pixel)
                std::copy(bytes.begin(), bytes.end(), rgba_.begin() + pixel * 4u);
            std::fill(depth_.begin(), depth_.end(), depth);
        }

        [[nodiscard]] bool DrawTriangle(
            const std::array<ClipVertex, 3>& clipVertices, const BasicEffectParams& params)
        {
            ValidateParams(params);
            std::array<ScreenVertex, 3> vertices{};
            for (std::size_t index = 0; index < vertices.size(); ++index)
                vertices[index] = Project(clipVertices[index]);

            const float denominator =
                (vertices[1].y - vertices[2].y) * (vertices[0].x - vertices[2].x) +
                (vertices[2].x - vertices[1].x) * (vertices[0].y - vertices[2].y);
            if (!std::isfinite(denominator) || denominator == 0.0f)
                throw std::invalid_argument("CPU BasicEffect triangle must be finite and non-degenerate");

            const int minX = std::max(0, static_cast<int>(std::floor(
                std::min({vertices[0].x, vertices[1].x, vertices[2].x}))));
            const int maxX = std::min(width_ - 1, static_cast<int>(std::ceil(
                std::max({vertices[0].x, vertices[1].x, vertices[2].x}))) - 1);
            const int minY = std::max(0, static_cast<int>(std::floor(
                std::min({vertices[0].y, vertices[1].y, vertices[2].y}))));
            const int maxY = std::min(height_ - 1, static_cast<int>(std::ceil(
                std::max({vertices[0].y, vertices[1].y, vertices[2].y}))) - 1);

            bool wrote = false;
            for (int y = minY; y <= maxY; ++y)
            {
                for (int x = minX; x <= maxX; ++x)
                {
                    const float sampleX = static_cast<float>(x) + 0.5f;
                    const float sampleY = static_cast<float>(y) + 0.5f;
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
                    if (fragmentDepth > depth_[pixel])
                        continue;

                    const float perspectiveDenominator =
                        lambda0 * vertices[0].reciprocalW +
                        lambda1 * vertices[1].reciprocalW +
                        lambda2 * vertices[2].reciprocalW;
                    if (!(perspectiveDenominator > 0.0f) || !std::isfinite(perspectiveDenominator))
                        continue;
                    const std::array<float, 3> weights = {
                        lambda0 * vertices[0].reciprocalW / perspectiveDenominator,
                        lambda1 * vertices[1].reciprocalW / perspectiveDenominator,
                        lambda2 * vertices[2].reciprocalW / perspectiveDenominator,
                    };

                    const float u = Interpolate(vertices, weights, &ScreenVertex::u);
                    const float v = Interpolate(vertices, weights, &ScreenVertex::v);
                    ColorF fragment = params.textureEnabled
                        ? params.texture->SamplePointClamp(u, v)
                        : ColorF{1.0f, 1.0f, 1.0f, 1.0f};
                    if (params.vertexColorEnabled)
                    {
                        const ColorF vertexColor = {
                            InterpolateColor(vertices, weights, &ColorF::r),
                            InterpolateColor(vertices, weights, &ColorF::g),
                            InterpolateColor(vertices, weights, &ColorF::b),
                            InterpolateColor(vertices, weights, &ColorF::a),
                        };
                        fragment = fragment * vertexColor;
                    }

                    // FNA BasicEffect's disabled-lighting route:
                    // texture * vertexColor? * ((DiffuseColor + EmissiveColor) * Alpha, Alpha).
                    const ColorF material = {
                        (params.diffuse.r + params.emissive.r) * params.alpha,
                        (params.diffuse.g + params.emissive.g) * params.alpha,
                        (params.diffuse.b + params.emissive.b) * params.alpha,
                        params.alpha,
                    };
                    fragment = fragment * material;
                    const std::size_t offset = pixel * 4u;
                    rgba_[offset] = ToByte(fragment.r);
                    rgba_[offset + 1u] = ToByte(fragment.g);
                    rgba_[offset + 2u] = ToByte(fragment.b);
                    rgba_[offset + 3u] = ToByte(fragment.a);
                    depth_[pixel] = fragmentDepth;
                    wrote = true;
                }
            }
            return wrote;
        }

        [[nodiscard]] std::array<std::uint8_t, 4> Pixel(int x, int y) const
        {
            if (x < 0 || y < 0 || x >= width_ || y >= height_)
                return {};
            const std::size_t offset =
                (static_cast<std::size_t>(y) * width_ + x) * 4u;
            return {rgba_[offset], rgba_[offset + 1u], rgba_[offset + 2u], rgba_[offset + 3u]};
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
            return surface.WritePixels(0, 0, width_, height_, rgba_.data(), width_ * 4);
        }

    private:
        void ValidateParams(const BasicEffectParams& params) const
        {
            if (params.lightingEnabled || params.fogEnabled)
                throw std::invalid_argument(
                    "SKIA-100 BasicEffect spike accepts only the unlit, no-fog route");
            if (params.textureEnabled && !params.texture)
                throw std::invalid_argument(
                    "SKIA-100 BasicEffect textured route requires Texture2D");
            if (!IsFinite(params.diffuse) || !IsFinite(params.emissive) ||
                !std::isfinite(params.alpha))
                throw std::invalid_argument("SKIA-100 BasicEffect material must be finite");
        }

        [[nodiscard]] ScreenVertex Project(const ClipVertex& vertex) const
        {
            if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) ||
                !std::isfinite(vertex.z) || !std::isfinite(vertex.w) ||
                !std::isfinite(vertex.u) || !std::isfinite(vertex.v) ||
                !IsFinite(vertex.color) || !(vertex.w > 0.0f))
                throw std::invalid_argument("CPU BasicEffect vertex must be finite with positive W");
            // Homogeneous clipping is deliberately not guessed. Only fully inside vertices are
            // admitted so missing clipping can never paint the wrong pixels.
            if (vertex.x < -vertex.w || vertex.x > vertex.w ||
                vertex.y < -vertex.w || vertex.y > vertex.w ||
                vertex.z < -vertex.w || vertex.z > vertex.w)
                throw std::invalid_argument(
                    "SKIA-100 BasicEffect spike requires already-clipped vertices");
            const float reciprocalW = 1.0f / vertex.w;
            const float ndcX = vertex.x * reciprocalW;
            const float ndcY = vertex.y * reciprocalW;
            return {
                (ndcX * 0.5f + 0.5f) * width_,
                (0.5f - ndcY * 0.5f) * height_,
                vertex.z * reciprocalW,
                reciprocalW,
                vertex.u,
                vertex.v,
                vertex.color,
            };
        }

        [[nodiscard]] static float Interpolate(
            const std::array<ScreenVertex, 3>& vertices,
            const std::array<float, 3>& weights,
            float ScreenVertex::* member)
        {
            return weights[0] * vertices[0].*member +
                   weights[1] * vertices[1].*member +
                   weights[2] * vertices[2].*member;
        }

        [[nodiscard]] static float InterpolateColor(
            const std::array<ScreenVertex, 3>& vertices,
            const std::array<float, 3>& weights,
            float ColorF::* member)
        {
            return weights[0] * vertices[0].color.*member +
                   weights[1] * vertices[1].color.*member +
                   weights[2] * vertices[2].color.*member;
        }

        int width_;
        int height_;
        std::size_t pixels_;
        std::vector<std::uint8_t> rgba_;
        std::vector<float> depth_;
    };

    [[nodiscard]] ClipVertex Vertex(
        float ndcX, float ndcY, float u, float v, const ColorF& color,
        float w = 1.0f, float ndcDepth = 0.5f)
    {
        return {ndcX * w, ndcY * w, ndcDepth * w, w, u, v, color};
    }

    [[nodiscard]] bool DrawQuad(
        CpuBasicEffectTarget& target, float left, float top, float right, float bottom,
        float u, float v, const ColorF& color, const BasicEffectParams& params)
    {
        const ClipVertex tl = Vertex(left, top, u, v, color);
        const ClipVertex bl = Vertex(left, bottom, u, v, color);
        const ClipVertex br = Vertex(right, bottom, u, v, color);
        const ClipVertex tr = Vertex(right, top, u, v, color);
        return target.DrawTriangle({tl, bl, br}, params) &&
               target.DrawTriangle({tl, br, tr}, params);
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

    enum Requirement : std::uint64_t
    {
        GeometryInput       = 1ull << 0,
        TransformAndClip    = 1ull << 1,
        ExactCoverage       = 1ull << 2,
        DepthStencil        = 1ull << 3,
        Texture2DSampling   = 1ull << 4,
        VertexColor         = 1ull << 5,
        MaterialColor       = 1ull << 6,
        NormalTransform     = 1ull << 7,
        Lighting            = 1ull << 8,
        Fog                 = 1ull << 9,
        AlphaDiscard        = 1ull << 10,
        SecondTexture       = 1ull << 11,
        CubeSampling        = 1ull << 12,
        Skinning            = 1ull << 13,
        PbrBrdf             = 1ull << 14,
        TangentBasis        = 1ull << 15,
        AuxiliaryMaps      = 1ull << 16,
        SamplerLod          = 1ull << 17,
        Instancing          = 1ull << 18,
        PublicIntegration   = 1ull << 19,
        MixedOrdering       = 1ull << 20,
    };

    struct FamilyEvidence
    {
        std::string_view name;
        std::uint64_t required;
        std::uint64_t reusable;
        std::uint64_t prototype;
        std::uint64_t gaps;
    };

    constexpr std::uint64_t common = GeometryInput | TransformAndClip | ExactCoverage |
        DepthStencil | Texture2DSampling | MaterialColor | SamplerLod |
        PublicIntegration | MixedOrdering;
    constexpr std::uint64_t cpuInput = GeometryInput | TransformAndClip | DepthStencil;
    constexpr std::uint64_t publicGaps = ExactCoverage | SamplerLod | PublicIntegration | MixedOrdering;

    constexpr std::array<FamilyEvidence, 7> families = {{
        {"BasicEffect", common | VertexColor | NormalTransform | Lighting | Fog | Instancing,
         VertexColor, cpuInput | Texture2DSampling | MaterialColor,
         publicGaps | NormalTransform | Lighting | Fog | Instancing},
        {"AlphaTestEffect", common | VertexColor | Fog | AlphaDiscard,
         VertexColor | AlphaDiscard, cpuInput | Texture2DSampling | MaterialColor,
         publicGaps | Fog},
        {"DualTextureEffect", common | VertexColor | Fog | SecondTexture,
         VertexColor | SecondTexture, cpuInput | Texture2DSampling | MaterialColor,
         publicGaps | Fog},
        {"EnvironmentMapEffect", common | NormalTransform | Lighting | Fog | CubeSampling,
         0u, cpuInput | Texture2DSampling | MaterialColor,
         publicGaps | NormalTransform | Lighting | Fog | CubeSampling},
        {"SkinnedEffect", common | VertexColor | NormalTransform | Lighting | Fog | Skinning,
         VertexColor, cpuInput | Texture2DSampling | MaterialColor,
         publicGaps | NormalTransform | Lighting | Fog | Skinning},
        {"PbrEffect", common | NormalTransform | Lighting | Fog | PbrBrdf | TangentBasis |
             AuxiliaryMaps,
         0u, cpuInput | Texture2DSampling | MaterialColor,
         publicGaps | NormalTransform | Lighting | Fog | PbrBrdf | TangentBasis | AuxiliaryMaps},
        {"SkinnedPbrEffect", common | NormalTransform | Lighting | Fog | Skinning | PbrBrdf |
             TangentBasis | AuxiliaryMaps,
         0u, cpuInput | Texture2DSampling | MaterialColor,
         publicGaps | NormalTransform | Lighting | Fog | Skinning | PbrBrdf | TangentBasis |
             AuxiliaryMaps},
    }};

    [[nodiscard]] bool ValidateFamilyInventory()
    {
        std::uint64_t coveredRequirements = 0u;
        bool valid = true;
        for (std::size_t index = 0; index < families.size(); ++index)
        {
            const FamilyEvidence& family = families[index];
            const bool disjoint =
                (family.reusable & family.prototype) == 0u &&
                (family.reusable & family.gaps) == 0u &&
                (family.prototype & family.gaps) == 0u;
            const bool complete = (family.reusable | family.prototype | family.gaps) == family.required;
            const bool honestBoundary =
                (family.gaps & (ExactCoverage | PublicIntegration | MixedOrdering)) ==
                (ExactCoverage | PublicIntegration | MixedOrdering);
            valid &= !family.name.empty() && disjoint && complete && honestBoundary;
            coveredRequirements |= family.required;
            for (std::size_t other = index + 1u; other < families.size(); ++other)
                valid &= family.name != families[other].name;
        }
        constexpr std::uint64_t allRequirements = (1ull << 21) - 1ull;
        return valid && coveredRequirements == allRequirements;
    }
}

int main()
{
    bool badAxisRejected = false;
    bool badUploadRejected = false;
    bool oversizedRejected = false;
    try { CpuTexture2D texture(0, 1, {}); }
    catch (const std::invalid_argument&) { badAxisRejected = true; }
    try { CpuTexture2D texture(1, 1, {1u, 2u, 3u}); }
    catch (const std::invalid_argument&) { badUploadRejected = true; }
    try { CpuBasicEffectTarget target(kMaximumAxis, kMaximumAxis); }
    catch (const std::length_error&) { oversizedRejected = true; }
    Check(badAxisRejected && badUploadRejected && oversizedRejected,
          "CPU effect textures/targets reject invalid dimensions, exact-size violations and >256 MiB");

    const CpuTexture2D texture(2, 2, {
        200, 100,  50, 255,
         50, 200, 100, 255,
        100,  50, 200, 255,
        150, 150, 150, 255,
    });
    BasicEffectParams params;
    params.texture = &texture;
    params.vertexColorEnabled = true;
    params.diffuse = {0.6f, 0.4f, 0.8f, 1.0f};
    params.emissive = {0.1f, 0.2f, 0.05f, 0.0f};
    const ColorF vertexColor = {
        180.0f / 255.0f, 220.0f / 255.0f, 140.0f / 255.0f, 1.0f,
    };

    CpuBasicEffectTarget target(64, 64);
    target.Clear({0.0f, 0.0f, 0.0f, 1.0f}, 1.0f);
    const bool quadrantsDrawn =
        DrawQuad(target, -1.0f, 1.0f, 0.0f, 0.0f, 0.25f, 0.25f, vertexColor, params) &&
        DrawQuad(target, 0.0f, 1.0f, 1.0f, 0.0f, 0.75f, 0.25f, vertexColor, params) &&
        DrawQuad(target, -1.0f, 0.0f, 0.0f, -1.0f, 0.25f, 0.75f, vertexColor, params) &&
        DrawQuad(target, 0.0f, 0.0f, 1.0f, -1.0f, 0.75f, 0.75f, vertexColor, params);
    const bool easyGlOracle =
        IsPixel(target.Pixel(16, 16), {99, 52, 23, 255}) &&
        IsPixel(target.Pixel(48, 16), {25, 104, 47, 255}) &&
        IsPixel(target.Pixel(16, 48), {49, 26, 93, 255}) &&
        IsPixel(target.Pixel(48, 48), {74, 78, 70, 255});
    Check(quadrantsDrawn && easyGlOracle,
          "unlit textured PCT route matches all 4 EasyGL BasicEffect combined oracle pixels");

    const CpuTexture2D perspectiveTexture(4, 1, {
        255, 0, 0, 255,  0, 255, 0, 255,  0, 0, 255, 255,  255, 255, 255, 255,
    });
    BasicEffectParams perspectiveParams;
    perspectiveParams.texture = &perspectiveTexture;
    CpuBasicEffectTarget perspectiveTarget(64, 64);
    perspectiveTarget.Clear({0.0f, 0.0f, 0.0f, 1.0f}, 1.0f);
    constexpr ColorF white{1.0f, 1.0f, 1.0f, 1.0f};
    const std::array<ClipVertex, 3> perspectiveTriangle = {
        Vertex(-0.75f, 0.75f, 0.0f, 0.5f, white, 1.0f),
        Vertex(0.75f, 0.75f, 1.0f, 0.5f, white, 4.0f),
        Vertex(-0.75f, -0.75f, 0.0f, 0.5f, white, 1.0f),
    };
    const bool perspectiveDrawn =
        perspectiveTarget.DrawTriangle(perspectiveTriangle, perspectiveParams);
    Check(perspectiveDrawn &&
              IsPixel(perspectiveTarget.Pixel(24, 24), {255, 0, 0, 255}) &&
              std::abs(perspectiveTarget.Depth(24, 24) - 0.5f) < 1.0e-6f,
          "texture coordinates retain reciprocal W and depth through BasicEffect rasterization");

    SkiaSurface surface(64, 64);
    surface.Clear(1.0f, 0.0f, 1.0f, 1.0f);
    Check(target.HandoffTo(surface) &&
              IsPixel(SurfacePixel(surface, 16, 16), {99, 52, 23, 255}) &&
              IsPixel(SurfacePixel(surface, 48, 48), {74, 78, 70, 255}),
          "completed BasicEffect RGBA8 image hands off to Skia without re-shading or re-quantizing");

    CpuBasicEffectTarget atomicTarget(16, 16);
    atomicTarget.Clear({0.25f, 0.5f, 0.75f, 1.0f}, 0.25f);
    const auto sentinel = atomicTarget.Pixel(8, 8);
    BasicEffectParams missingTexture;
    bool missingTextureRejected = false;
    bool lightingRejected = false;
    bool clippingRejected = false;
    try
    {
        (void) atomicTarget.DrawTriangle({
            Vertex(-1.0f, 1.0f, 0.0f, 0.0f, white),
            Vertex(-1.0f, -1.0f, 0.0f, 0.0f, white),
            Vertex(1.0f, -1.0f, 0.0f, 0.0f, white),
        }, missingTexture);
    }
    catch (const std::invalid_argument&) { missingTextureRejected = true; }
    BasicEffectParams unsupported = params;
    unsupported.lightingEnabled = true;
    try { (void) atomicTarget.DrawTriangle(perspectiveTriangle, unsupported); }
    catch (const std::invalid_argument&) { lightingRejected = true; }
    std::array<ClipVertex, 3> outside = perspectiveTriangle;
    outside[0].x = -2.0f;
    try { (void) atomicTarget.DrawTriangle(outside, perspectiveParams); }
    catch (const std::invalid_argument&) { clippingRejected = true; }
    Check(missingTextureRejected && lightingRejected && clippingRejected &&
              atomicTarget.Pixel(8, 8) == sentinel &&
              std::abs(atomicTarget.Depth(8, 8) - 0.25f) < 1.0e-6f,
          "missing texture, lighting/fog and unclipped geometry reject before target mutation");

    Check(ValidateFamilyInventory(),
          "all 7 stock-effect family matrices classify every requirement exactly once and retain public gaps");

    std::printf("=== %s ===\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
