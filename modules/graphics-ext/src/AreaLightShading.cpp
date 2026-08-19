// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/AreaLightShading.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/AreaLightBrdfTable.hpp"
#include "Microsoft/Xna/Framework/Graphics/AreaLightEXT.hpp"

#include <algorithm>
#include <cmath>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Graphics::AreaLightEXT;
    using Microsoft::Xna::Framework::Graphics::AreaLightShapeEXT;

    namespace {

        constexpr float kPi = 3.14159265359f;

        /// A disc and a rectangle of the same half-axes do not enclose the same area: pi*a*b
        /// against 4*a*b. Scaling both axes by sqrt(pi)/2 makes them equal, so a disc delivers the
        /// irradiance a disc delivers even though the outline integrated is a rectangle.
        constexpr float kDiscAxisScale = 0.88622692545f;   // sqrt(pi) / 2

        float Dot(const Vector3& a, const Vector3& b) { return a.X * b.X + a.Y * b.Y + a.Z * b.Z; }

        Vector3 Cross(const Vector3& a, const Vector3& b)
        {
            return Vector3(a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X);
        }

        Vector3 Add(const Vector3& a, const Vector3& b)
        {
            return Vector3(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
        }

        Vector3 Subtract(const Vector3& a, const Vector3& b)
        {
            return Vector3(a.X - b.X, a.Y - b.Y, a.Z - b.Z);
        }

        Vector3 Scale(const Vector3& v, const float s)
        {
            return Vector3(v.X * s, v.Y * s, v.Z * s);
        }

        Vector3 Normalize(const Vector3& v, const Vector3& fallback)
        {
            const float length = std::sqrt(Dot(v, v));
            if (!(length > 1e-8f)) return fallback;
            return Scale(v, 1.0f / length);
        }

        /// One edge's contribution to the irradiance of a polygon over a clamped cosine, with both
        /// endpoints already on the unit sphere and in a frame whose z axis is the lobe.
        float IntegrateEdge(const Vector3& a, const Vector3& b)
        {
            const float cosine = std::clamp(Dot(a, b), -0.9999f, 0.9999f);
            const float angle = std::acos(cosine);
            const float sine = std::max(std::sin(angle), 1e-4f);
            return Cross(a, b).Z * angle / sine;
        }

        /// Clips a quad to the z > 0 half-space, by walking its edges and emitting the crossing
        /// point wherever one changes side. Below the horizon a direction contributes nothing, and
        /// leaving it in makes the edge sum negative rather than merely wrong. Written as a loop
        /// rather than as the reference implementation's sixteen-case switch, which is one place to
        /// be wrong instead of sixteen -- and the same loop the emitted GLSL runs.
        int ClipToHorizon(const std::array<Vector3, 4>& quad, std::array<Vector3, 8>& clipped)
        {
            int count = 0;
            for (int i = 0; i < 4; ++i)
            {
                const Vector3& current = quad[static_cast<std::size_t>(i)];
                const Vector3& next = quad[static_cast<std::size_t>((i + 1) % 4)];
                const bool currentIn = current.Z > 0.0f;
                const bool nextIn = next.Z > 0.0f;
                if (currentIn) clipped[static_cast<std::size_t>(count++)] = current;
                if (currentIn != nextIn)
                {
                    const float t = current.Z / (current.Z - next.Z);
                    clipped[static_cast<std::size_t>(count++)] =
                        Add(current, Scale(Subtract(next, current), t));
                }
            }
            return count;
        }

        Vector3 FrameTangent(const Vector3& axis)
        {
            const Vector3 guess = std::fabs(axis.Z) < 0.9f ? Vector3(0.0f, 0.0f, 1.0f)
                                                           : Vector3(1.0f, 0.0f, 0.0f);
            return Normalize(Cross(guess, axis), Vector3(1.0f, 0.0f, 0.0f));
        }

    } // namespace

    float AreaLightShading::lobeScaleFor(const float roughness)
    {
        const float clamped = std::clamp(roughness, 0.0f, 1.0f);
        // The GGX alpha, floored so a mirror still has a lobe with a width rather than a line.
        return std::max(clamped * clamped, 0.02f);
    }

    AreaLightShading::Quad AreaLightShading::quadOf(const AreaLightEXT& light,
                                                    const Vector3& surface)
    {
        Vector3 right = light.RightAxis;
        Vector3 up    = light.UpAxis;

        if (light.Shape == AreaLightShapeEXT::Disc)
        {
            right = Scale(right, kDiscAxisScale);
            up    = Scale(up, kDiscAxisScale);
        }
        else if (light.Shape == AreaLightShapeEXT::Tube)
        {
            // A cylinder looks like a rectangle from wherever it is seen, as wide as its radius and
            // turned so its face is towards the surface. That is what makes one quad enough for all
            // three shapes rather than a second integrator for tubes.
            const float radius = std::sqrt(Dot(up, up));
            const Vector3 axis = Normalize(right, Vector3(1.0f, 0.0f, 0.0f));
            const Vector3 toSurface = Subtract(surface, light.Position);
            // Perpendicular to both the axis and the direction to the surface, so the quad's own
            // normal points at the surface. Taking the component of `toSurface` perpendicular to
            // the axis instead -- which is the obvious thing to write, and what this did first --
            // puts that direction *in* the quad's plane, so the billboard is seen edge-on and a
            // tube directly overhead delivers nothing at all.
            Vector3 facing = Normalize(Cross(toSurface, axis), FrameTangent(axis));
            up = Scale(facing, radius);
        }

        const Vector3 a = Add(light.Position, Add(Scale(right, -1.0f), Scale(up, -1.0f)));
        const Vector3 b = Add(light.Position, Add(right, Scale(up, -1.0f)));
        const Vector3 c = Add(light.Position, Add(right, up));
        const Vector3 d = Add(light.Position, Add(Scale(right, -1.0f), up));
        return Quad{a, b, c, d};
    }

    float AreaLightShading::coverage(const Quad& quad, const Vector3& surface,
                                     const Vector3& lobeAxis, const float lobeScale,
                                     const bool twoSided)
    {
        const Vector3 axis = Normalize(lobeAxis, Vector3(0.0f, 0.0f, 1.0f));
        const Vector3 tangent = FrameTangent(axis);
        const Vector3 bitangent = Cross(axis, tangent);
        const float inverseScale = 1.0f / std::max(lobeScale, 1e-4f);

        std::array<Vector3, 4> points{};
        for (int i = 0; i < 4; ++i)
        {
            const Vector3 relative = Subtract(quad[static_cast<std::size_t>(i)], surface);
            // Into the lobe's own frame, then widened by the inverse of its scale: a narrow lobe
            // sees the light as bigger than it is, which is the same statement as it being tighter.
            points[static_cast<std::size_t>(i)] =
                Vector3(Dot(relative, tangent) * inverseScale,
                        Dot(relative, bitangent) * inverseScale, Dot(relative, axis));
        }

        std::array<Vector3, 8> clipped{};
        const int count = ClipToHorizon(points, clipped);
        if (count < 3) return 0.0f;

        for (int i = 0; i < count; ++i)
            clipped[static_cast<std::size_t>(i)] =
                Normalize(clipped[static_cast<std::size_t>(i)], Vector3(0.0f, 0.0f, 1.0f));

        float sum = 0.0f;
        for (int i = 0; i < count; ++i)
            sum += IntegrateEdge(clipped[static_cast<std::size_t>(i)],
                                 clipped[static_cast<std::size_t>((i + 1) % count)]);

        sum = twoSided ? std::fabs(sum) : std::max(-sum, 0.0f);
        return std::clamp(sum / (2.0f * kPi), 0.0f, 1.0f);
    }

    Vector3 AreaLightShading::contribution(const AreaLightEXT& light, const Vector3& surface,
                                           const Vector3& normal, const Vector3& cameraPosition,
                                           const Vector3& baseColor, const float metallic,
                                           const float roughness)
    {
        if (!light.IsValidEXT()) return Vector3(0.0f, 0.0f, 0.0f);

        const Vector3 toLight = Subtract(light.Position, surface);
        if (Dot(toLight, toLight) >= light.Range * light.Range) return Vector3(0.0f, 0.0f, 0.0f);

        const Quad quad = quadOf(light, surface);
        const Vector3 unitNormal = Normalize(normal, Vector3(0.0f, 1.0f, 0.0f));
        const Vector3 viewDirection = Normalize(Subtract(cameraPosition, surface), unitNormal);

        const float diffuseCoverage = coverage(quad, surface, unitNormal, 1.0f, light.TwoSided);

        // The specular lobe: aimed along the BRDF's average reflection direction, which the table
        // holds, and widened with roughness. Its energy comes from the table too, so the shape being
        // approximate does not make the surface brighter or darker than it should be.
        const float nDotV = std::clamp(Dot(unitNormal, viewDirection), 1e-3f, 1.0f);
        const AreaLightBrdfTable::Terms terms =
            AreaLightBrdfTable::evaluate(roughness, nDotV, 64);

        const Vector3 tangent = Normalize(Subtract(Scale(unitNormal, nDotV), viewDirection),
                                          FrameTangent(unitNormal));
        const Vector3 lobeAxis = Normalize(Add(Scale(tangent, terms.AverageTangent),
                                               Scale(unitNormal, terms.AverageNormal)),
                                           unitNormal);
        const float specularCoverage =
            coverage(quad, surface, lobeAxis, lobeScaleFor(roughness), light.TwoSided);

        const float scale = terms.Magnitude - terms.Fresnel;
        const float bias  = terms.Fresnel;

        Vector3 result(0.0f, 0.0f, 0.0f);
        float* out = &result.X;
        const float* base = &baseColor.X;
        const float* emitted = &light.Color.X;
        for (int channel = 0; channel < 3; ++channel)
        {
            const float f0 = 0.04f + (base[channel] - 0.04f) * metallic;
            const float specular = specularCoverage * (f0 * scale + bias);
            const float diffuse = diffuseCoverage * base[channel] * (1.0f - metallic);
            out[channel] = (diffuse + specular) * emitted[channel] * light.Intensity;
        }
        return result;
    }

    std::string AreaLightShading::getShadingGlsl()
    {
        return R"(
uniform int   uAreaShape;        // -1 none, 0 rectangle, 1 disc, 2 tube
uniform vec3  uAreaPosition;
uniform vec3  uAreaRight;
uniform vec3  uAreaUp;
uniform vec3  uAreaColour;       // already multiplied by the light's intensity
uniform float uAreaRange;
uniform float uAreaTwoSided;

const float kCnaDiscAxisScale = 0.88622692545;   // sqrt(pi) / 2

vec3 cnaFrameTangent(vec3 axis) {
    vec3 guess = abs(axis.z) < 0.9 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    return normalize(cross(guess, axis));
}

void cnaAreaQuad(vec3 surface, out vec3 quad[4]) {
    vec3 right = uAreaRight;
    vec3 up = uAreaUp;
    if (uAreaShape == 1) {
        right *= kCnaDiscAxisScale;
        up *= kCnaDiscAxisScale;
    } else if (uAreaShape == 2) {
        float radius = length(up);
        vec3 axis = normalize(right);
        vec3 toSurface = surface - uAreaPosition;
        // Perpendicular to both the axis and the direction to the surface, so the quad's normal
        // points at the surface rather than lying in its plane.
        vec3 facing = cross(toSurface, axis);
        facing = dot(facing, facing) > 1e-12 ? normalize(facing) : cnaFrameTangent(axis);
        up = facing * radius;
    }
    quad[0] = uAreaPosition - right - up;
    quad[1] = uAreaPosition + right - up;
    quad[2] = uAreaPosition + right + up;
    quad[3] = uAreaPosition - right + up;
}

float cnaIntegrateEdge(vec3 a, vec3 b) {
    float cosine = clamp(dot(a, b), -0.9999, 0.9999);
    float angle = acos(cosine);
    return cross(a, b).z * angle / max(sin(angle), 1e-4);
}

/// The fraction of a clamped-cosine lobe the quad covers. With the lobe on the surface normal and a
/// scale of 1 this is the diffuse form factor, and it is exact rather than approximate.
float cnaAreaCoverage(vec3 quad[4], vec3 surface, vec3 lobeAxis, float lobeScale, bool twoSided) {
    vec3 axis = normalize(lobeAxis);
    vec3 tangent = cnaFrameTangent(axis);
    vec3 bitangent = cross(axis, tangent);
    float inverseScale = 1.0 / max(lobeScale, 1e-4);

    vec3 p[5];
    for (int i = 0; i < 4; ++i) {
        vec3 relative = quad[i] - surface;
        p[i] = vec3(dot(relative, tangent) * inverseScale,
                    dot(relative, bitangent) * inverseScale,
                    dot(relative, axis));
    }
    p[4] = p[0];

    // The same loop ClipToHorizon runs on the CPU, rather than the reference implementation's
    // sixteen-case switch: one place to be wrong instead of sixteen, and the two paths cannot
    // disagree about a case one of them forgot.
    vec3 clipped[8];
    int count = 0;
    for (int i = 0; i < 4; ++i) {
        vec3 current = p[i];
        vec3 next = p[i + 1];
        bool currentIn = current.z > 0.0;
        bool nextIn = next.z > 0.0;
        if (currentIn) { clipped[count] = current; ++count; }
        if (currentIn != nextIn) {
            float t = current.z / (current.z - next.z);
            clipped[count] = current + (next - current) * t;
            ++count;
        }
    }
    if (count < 3) return 0.0;

    for (int i = 0; i < 8; ++i) {
        if (i >= count) break;
        clipped[i] = normalize(clipped[i]);
    }

    float sum = 0.0;
    for (int i = 0; i < 8; ++i) {
        if (i >= count) break;
        int next = (i + 1 == count) ? 0 : i + 1;
        sum += cnaIntegrateEdge(clipped[i], clipped[next]);
    }

    sum = twoSided ? abs(sum) : max(-sum, 0.0);
    return clamp(sum / 6.28318530718, 0.0, 1.0);
}

vec3 cnaAreaContribution(vec3 surface, vec3 normal, vec3 viewDirection, vec3 baseColor,
                         float metallic, float roughness) {
    if (uAreaShape < 0) return vec3(0.0);
    vec3 toLight = uAreaPosition - surface;
    if (dot(toLight, toLight) >= uAreaRange * uAreaRange) return vec3(0.0);

    vec3 quad[4];
    cnaAreaQuad(surface, quad);
    bool twoSided = uAreaTwoSided > 0.5;

    float diffuseCoverage = cnaAreaCoverage(quad, surface, normal, 1.0, twoSided);

    float nDotV = clamp(dot(normal, viewDirection), 1e-3, 1.0);
    vec4 terms = cnaAreaBrdfTerms(nDotV, roughness);
    // Guarded, because at exactly normal incidence `normal * nDotV - viewDirection` is the zero
    // vector: normalizing it gives NaN, the NaN reaches the lobe axis, and the coverage clamps to
    // zero -- so a mirror looked at head-on, which is the case a highlight test aims for, comes
    // back black. The CPU path always had this fallback; the shader did not.
    vec3 tangentBase = normal * nDotV - viewDirection;
    vec3 tangent = dot(tangentBase, tangentBase) > 1e-12 ? normalize(tangentBase)
                                                         : cnaFrameTangent(normal);
    vec3 lobeAxis = normalize(tangent * terms.b + normal * terms.a);
    float lobeScale = max(roughness * roughness, 0.02);
    float specularCoverage = cnaAreaCoverage(quad, surface, lobeAxis, lobeScale, twoSided);

    float scale = terms.r - terms.g;
    float bias = terms.g;
    vec3 f0 = mix(vec3(0.04), baseColor, metallic);
    vec3 specular = specularCoverage * (f0 * scale + bias);
    vec3 diffuse = diffuseCoverage * baseColor * (1.0 - metallic);
    return (diffuse + specular) * uAreaColour;
}
)";
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
