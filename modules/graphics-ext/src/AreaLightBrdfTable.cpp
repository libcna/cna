// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/AreaLightBrdfTable.hpp"

#ifdef CNA_CNAEXT

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::Texture2D;

    namespace {

        constexpr float kPi = 3.14159265359f;

        struct Direction { float X, Y, Z; };

        float Dot(const Direction& a, const Direction& b)
        {
            return a.X * b.X + a.Y * b.Y + a.Z * b.Z;
        }

        Direction Normalize(const Direction& v)
        {
            const float length = std::sqrt(Dot(v, v));
            if (!(length > 1e-8f)) return Direction{0.0f, 0.0f, 1.0f};
            return Direction{v.X / length, v.Y / length, v.Z / length};
        }

        void Hammersley(const int index, const int count, float& x, float& y)
        {
            x = (static_cast<float>(index) + 0.5f) / static_cast<float>(count);
            std::uint32_t bits = static_cast<std::uint32_t>(index);
            bits = (bits << 16u) | (bits >> 16u);
            bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
            bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
            bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
            bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
            y = static_cast<float>(bits) * 2.3283064365386963e-10f;
        }

        /// A GGX half-vector around the +Z normal, which is the frame the whole table works in.
        Direction ImportanceSampleGgx(const float u1, const float u2, const float roughness)
        {
            const float a = roughness * roughness;
            const float phi = 2.0f * kPi * u1;
            const float cosTheta = std::sqrt((1.0f - u2) / (1.0f + (a * a - 1.0f) * u2));
            const float sinTheta = std::sqrt(std::max(1.0f - cosTheta * cosTheta, 0.0f));
            return Direction{sinTheta * std::cos(phi), sinTheta * std::sin(phi), cosTheta};
        }

    } // namespace

    AreaLightBrdfTable::Terms AreaLightBrdfTable::evaluate(const float roughness,
                                                           const float cosTheta,
                                                           const int sampleCount)
    {
        if (sampleCount <= 0)
            throw std::invalid_argument(
                "CNA::Graphics::AreaLightBrdfTable::evaluate: the sample count must be positive");

        const float alpha = std::clamp(roughness, 0.02f, 1.0f);
        const float nDotV = std::clamp(cosTheta, 1e-3f, 1.0f);
        const Direction normal{0.0f, 0.0f, 1.0f};
        const Direction view{std::sqrt(std::max(1.0f - nDotV * nDotV, 0.0f)), 0.0f, nDotV};

        float magnitude = 0.0f;
        float fresnel   = 0.0f;
        Direction average{0.0f, 0.0f, 0.0f};

        for (int i = 0; i < sampleCount; ++i)
        {
            float u1 = 0.0f, u2 = 0.0f;
            Hammersley(i, sampleCount, u1, u2);
            const Direction half = ImportanceSampleGgx(u1, u2, alpha);
            const float vDotH = Dot(view, half);
            const Direction light = Normalize(Direction{2.0f * vDotH * half.X - view.X,
                                                        2.0f * vDotH * half.Y - view.Y,
                                                        2.0f * vDotH * half.Z - view.Z});
            const float nDotL = light.Z;
            if (nDotL <= 0.0f) continue;

            // With the half-vector importance sampled from the GGX distribution, the weight of a
            // sample reduces to the visibility term times the geometric factor -- the D and the pdf
            // cancel. The k here is the direct-lighting one, because an area light is direct light;
            // the IBL k that generateBrdfLut uses would darken every rough surface.
            const float k = (alpha + 1.0f) * (alpha + 1.0f) / 8.0f;
            const float gv = nDotV / (nDotV * (1.0f - k) + k);
            const float gl = nDotL / (nDotL * (1.0f - k) + k);
            const float nDotH = std::max(half.Z, 1e-6f);
            const float weight = gv * gl * std::max(vDotH, 0.0f) / (nDotH * nDotV);

            magnitude += weight;
            fresnel   += weight * std::pow(1.0f - std::max(vDotH, 0.0f), 5.0f);
            average.X += weight * light.X;
            average.Y += weight * light.Y;
            average.Z += weight * light.Z;
        }

        const float inverse = 1.0f / static_cast<float>(sampleCount);
        Terms terms;
        terms.Magnitude = std::clamp(magnitude * inverse, 0.0f, 1.0f);
        terms.Fresnel   = std::clamp(fresnel * inverse, 0.0f, 1.0f);

        // The y component is zero for an isotropic BRDF and is cleared rather than trusted: with a
        // finite sample count it is a small residue, and normalising a direction that is supposed
        // to lie in a plane but does not tilts the whole lobe sideways.
        average.Y = 0.0f;
        if (Dot(average, average) > 1e-12f)
        {
            const Direction unit = Normalize(average);
            // Measured against the *reflection-side* tangent, normalize(N * (N.V) - V), rather than
            // against the view's own tangent. A reflected direction leans away from the view, so
            // its component along the view's tangent is negative -- and a table whose values must
            // fit in [0, 1] to survive an 8-bit texture would store that as zero and flatten the
            // lobe's tilt to nothing, which is exactly what the first version of this did.
            terms.AverageTangent = std::clamp(-unit.X, 0.0f, 1.0f);
            terms.AverageNormal  = std::clamp(unit.Z, 0.0f, 1.0f);
        }
        return terms;
    }

    AreaLightBrdfTable::AreaLightBrdfTable(GraphicsDevice& device, const int size,
                                           const int sampleCount)
        : size_(size), sampleCount_(sampleCount)
    {
        if (size <= 0 || sampleCount <= 0)
            throw std::invalid_argument(
                "CNA::Graphics::AreaLightBrdfTable: the size and the sample count must both be "
                "positive");

        const auto start = std::chrono::steady_clock::now();

        std::vector<Color> texels(static_cast<std::size_t>(size) * size, Color::Black);
        for (int y = 0; y < size; ++y)
        {
            const float roughness = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
            for (int x = 0; x < size; ++x)
            {
                const float nDotV = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);
                const Terms terms = evaluate(roughness, nDotV, sampleCount);
                texels[static_cast<std::size_t>(y) * size + x] =
                    Color(static_cast<int>(terms.Magnitude * 255.0f + 0.5f),
                          static_cast<int>(terms.Fresnel * 255.0f + 0.5f),
                          static_cast<int>(terms.AverageTangent * 255.0f + 0.5f),
                          static_cast<int>(terms.AverageNormal * 255.0f + 0.5f));
            }
        }

        texture_ = std::make_unique<Texture2D>(device, size, size);
        texture_->SetData(texels.data(), static_cast<int>(texels.size()));

        const auto end = std::chrono::steady_clock::now();
        generationMilliseconds_ =
            std::chrono::duration<double, std::milli>(end - start).count();
    }

    AreaLightBrdfTable::~AreaLightBrdfTable() = default;

    Texture2D* AreaLightBrdfTable::getTexture() const { return texture_.get(); }
    int        AreaLightBrdfTable::getSize() const { return size_; }
    int        AreaLightBrdfTable::getSampleCount() const { return sampleCount_; }
    double     AreaLightBrdfTable::getGenerationMilliseconds() const
    {
        return generationMilliseconds_;
    }

    std::string AreaLightBrdfTable::getLookupGlsl()
    {
        return R"(
uniform sampler2D uCnaAreaBrdf;
uniform float uCnaAreaBrdfSize;

/// x = directional albedo, y = Fresnel weight, z/w = the average reflection direction in the plane
/// the view and the normal span.
///
/// The coordinate is remapped onto the first and last texel *centres* rather than onto the edges
/// of the texture, and that is not tidiness. A texture bound through ShaderEffect::SetTexture keeps
/// the default wrap mode, and per-unit sampler state does not reach it (measured, MOD-2029) -- so a
/// lookup at N.V = 1 lands exactly on the seam and the filter averages the last column with the
/// *first*, which is the grazing end of the table. The result was a mirror looked at head-on being
/// told its reflection leans thirty degrees off the normal, which pushed a corner of every area
/// light below the horizon and left the highlight black.
vec4 cnaAreaBrdfTerms(float nDotV, float roughness) {
    vec2 index = clamp(vec2(nDotV, roughness), 0.0, 1.0);
    vec2 uv = (index * (uCnaAreaBrdfSize - 1.0) + 0.5) / max(uCnaAreaBrdfSize, 1.0);
    return texture(uCnaAreaBrdf, uv);
}
)";
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
