// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/LightProbeEXT.hpp"

#ifdef CNA_CNAEXT

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Vector3;

    namespace {

        // Ramamoorthi and Hanrahan's constants: the cosine lobe's own spherical-harmonic
        // coefficients, already folded together with the basis normalisation. Their whole result is
        // that irradiance needs only these nine terms, because convolving *anything* with a cosine
        // lobe leaves almost nothing above second order.
        constexpr float kC1 = 0.429043f;
        constexpr float kC2 = 0.511664f;
        constexpr float kC3 = 0.743125f;
        constexpr float kC4 = 0.886227f;
        constexpr float kC5 = 0.247708f;

        Vector3 Normalized(const Vector3& v, const Vector3& fallback)
        {
            const float length = std::sqrt(v.X * v.X + v.Y * v.Y + v.Z * v.Z);
            if (!(length > 1e-8f)) return fallback;
            return Vector3(v.X / length, v.Y / length, v.Z / length);
        }

    } // namespace

    LightProbeEXT::LightProbeEXT() = default;

    LightProbeEXT::LightProbeEXT(const Vector3& position) : position_(position) {}

    Vector3 LightProbeEXT::getPosition() const { return position_; }
    void    LightProbeEXT::setPosition(const Vector3& value) { position_ = value; }

    Vector3 LightProbeEXT::getCoefficient(const int index) const
    {
        if (index < 0 || index >= kCoefficientCount)
            throw std::out_of_range(
                "CNA::Graphics::LightProbeEXT::getCoefficient: a second-order probe has nine "
                "coefficients");
        return coefficients_[static_cast<std::size_t>(index)];
    }

    void LightProbeEXT::setCoefficient(const int index, const Vector3& value)
    {
        if (index < 0 || index >= kCoefficientCount)
            throw std::out_of_range(
                "CNA::Graphics::LightProbeEXT::setCoefficient: a second-order probe has nine "
                "coefficients");
        coefficients_[static_cast<std::size_t>(index)] = value;
    }

    const std::array<Vector3, LightProbeEXT::kCoefficientCount>&
    LightProbeEXT::getCoefficients() const { return coefficients_; }

    Vector3 LightProbeEXT::irradiance(const Vector3& normal) const
    {
        const Vector3 n = Normalized(normal, Vector3(0.0f, 1.0f, 0.0f));

        Vector3 result(0.0f, 0.0f, 0.0f);
        float* out = &result.X;
        for (int channel = 0; channel < 3; ++channel)
        {
            const auto at = [&](const int index) {
                return (&coefficients_[static_cast<std::size_t>(index)].X)[channel];
            };

            const float value =
                  kC4 * at(0)
                + 2.0f * kC2 * (at(1) * n.Y + at(2) * n.Z + at(3) * n.X)
                + 2.0f * kC1 * (at(4) * n.X * n.Y + at(5) * n.Y * n.Z + at(7) * n.X * n.Z)
                + kC3 * at(6) * n.Z * n.Z - kC5 * at(6)
                + kC1 * at(8) * (n.X * n.X - n.Y * n.Y);

            // A projection can go slightly negative where the environment is dark and the fit
            // overshoots; negative irradiance is light being removed from a surface, which nothing
            // downstream is prepared for.
            out[channel] = std::max(value, 0.0f);
        }
        return result;
    }

    void LightProbeEXT::setVisibility(const int direction, const float meanDistance,
                                      const float meanSquaredDistance)
    {
        if (direction < 0 || direction >= kVisibilityDirections)
            throw std::out_of_range(
                "CNA::Graphics::LightProbeEXT::setVisibility: a probe records six directions");

        const std::size_t index = static_cast<std::size_t>(direction);
        const float mean = std::max(meanDistance, 0.0f);
        visibilityMean_[index] = mean;
        // No distribution has negative variance, and one that appeared to would make the Chebyshev
        // test answer a number outside [0, 1] -- which is a probe contributing negatively.
        visibilityMeanSquared_[index] = std::max(meanSquaredDistance, mean * mean);
    }

    float LightProbeEXT::getVisibilityMean(const int direction) const
    {
        if (direction < 0 || direction >= kVisibilityDirections)
            throw std::out_of_range(
                "CNA::Graphics::LightProbeEXT::getVisibilityMean: a probe records six directions");
        return visibilityMean_[static_cast<std::size_t>(direction)];
    }

    float LightProbeEXT::getVisibilityMeanSquared(const int direction) const
    {
        if (direction < 0 || direction >= kVisibilityDirections)
            throw std::out_of_range(
                "CNA::Graphics::LightProbeEXT::getVisibilityMeanSquared: a probe records six "
                "directions");
        return visibilityMeanSquared_[static_cast<std::size_t>(direction)];
    }

    bool LightProbeEXT::hasVisibility() const
    {
        for (const float mean : visibilityMean_)
            if (mean > 0.0f) return true;
        return false;
    }

    float LightProbeEXT::visibilityWeight(const Vector3& direction, const float distance) const
    {
        if (!hasVisibility()) return 1.0f;
        if (!(distance > 0.0f)) return 1.0f;

        const Vector3 d = Normalized(direction, Vector3(0.0f, 1.0f, 0.0f));
        // Blended across the up-to-three axes the direction actually points along, rather than
        // snapped to the nearest one: snapping makes the weight jump as a surface turns, and a
        // discontinuity in an ambient term is more visible than the leak it was fixing.
        const float components[kVisibilityDirections] = {
            std::max(d.X, 0.0f), std::max(-d.X, 0.0f), std::max(d.Y, 0.0f),
            std::max(-d.Y, 0.0f), std::max(d.Z, 0.0f), std::max(-d.Z, 0.0f)};

        float total = 0.0f;
        float mean = 0.0f;
        float meanSquared = 0.0f;
        for (int index = 0; index < kVisibilityDirections; ++index)
        {
            const std::size_t at = static_cast<std::size_t>(index);
            if (visibilityMean_[at] <= 0.0f) continue;
            const float weight = components[index] * components[index];
            if (weight <= 0.0f) continue;
            total += weight;
            mean += visibilityMean_[at] * weight;
            meanSquared += visibilityMeanSquared_[at] * weight;
        }
        // Every direction that points anywhere useful was left unrecorded, so there is nothing to
        // test against and the probe is trusted rather than discarded.
        if (!(total > 0.0f)) return 1.0f;

        mean /= total;
        meanSquared /= total;
        if (distance <= mean) return 1.0f;

        // Chebyshev, exactly as a variance shadow map uses it: a flat wall has almost no variance
        // and cuts off sharply, a cluttered direction has a lot and fades.
        const float variance = std::max(meanSquared - mean * mean, 0.0f);
        const float gap = distance - mean;
        return std::clamp(variance / (variance + gap * gap), 0.0f, 1.0f);
    }

    bool LightProbeEXT::isZero() const
    {
        for (const Vector3& coefficient : coefficients_)
            if (coefficient.X != 0.0f || coefficient.Y != 0.0f || coefficient.Z != 0.0f)
                return false;
        return true;
    }

    void LightProbeEXT::scale(const float factor)
    {
        if (!(factor >= 0.0f)) return;
        for (Vector3& coefficient : coefficients_)
            coefficient = Vector3(coefficient.X * factor, coefficient.Y * factor,
                                  coefficient.Z * factor);
    }

    bool LightProbeEXT::operator==(const LightProbeEXT& other) const
    {
        if (position_.X != other.position_.X || position_.Y != other.position_.Y ||
            position_.Z != other.position_.Z)
            return false;
        for (int index = 0; index < kCoefficientCount; ++index)
        {
            const Vector3& mine = coefficients_[static_cast<std::size_t>(index)];
            const Vector3& theirs = other.coefficients_[static_cast<std::size_t>(index)];
            if (mine.X != theirs.X || mine.Y != theirs.Y || mine.Z != theirs.Z) return false;
        }
        return true;
    }

    bool LightProbeEXT::operator!=(const LightProbeEXT& other) const { return !(*this == other); }

    std::string LightProbeEXT::getEvaluationGlsl()
    {
        return R"(
const float kCnaShC1 = 0.429043;
const float kCnaShC2 = 0.511664;
const float kCnaShC3 = 0.743125;
const float kCnaShC4 = 0.886227;
const float kCnaShC5 = 0.247708;

/// Irradiance, not outgoing radiance: a Lambertian surface reflects albedo/pi of this, and the
/// caller applies that. Baking the albedo in here would put a surface's colour into a probe that
/// has nothing to do with any surface.
vec3 cnaProbeIrradiance(vec3 coefficients[9], vec3 normal) {
    vec3 n = normalize(normal);
    vec3 result =
          kCnaShC4 * coefficients[0]
        + 2.0 * kCnaShC2 * (coefficients[1] * n.y + coefficients[2] * n.z + coefficients[3] * n.x)
        + 2.0 * kCnaShC1 * (coefficients[4] * n.x * n.y + coefficients[5] * n.y * n.z
                            + coefficients[7] * n.x * n.z)
        + kCnaShC3 * coefficients[6] * n.z * n.z - kCnaShC5 * coefficients[6]
        + kCnaShC1 * coefficients[8] * (n.x * n.x - n.y * n.y);
    return max(result, vec3(0.0));
}
)";
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
