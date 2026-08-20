// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/LightProbeVolumeEXT.hpp"

#ifdef CNA_CNAEXT

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::BoundingBox;
    using Microsoft::Xna::Framework::Vector3;

    namespace {

        /// Where a value sits between two bounds, as 0..1. A single-probe axis has no span, so it
        /// answers 0 rather than dividing by zero -- every point on that axis is that one probe.
        float Fraction(const float value, const float low, const float high)
        {
            if (!(high - low > 1e-8f)) return 0.0f;
            return std::clamp((value - low) / (high - low), 0.0f, 1.0f);
        }

    } // namespace

    LightProbeVolumeEXT::LightProbeVolumeEXT(const BoundingBox& bounds, const int countX,
                                             const int countY, const int countZ)
        : bounds_(bounds), countX_(countX), countY_(countY), countZ_(countZ)
    {
        if (countX < 1 || countY < 1 || countZ < 1)
            throw std::invalid_argument(
                "CNA::Graphics::LightProbeVolumeEXT: every axis needs at least one probe; a volume "
                "with none has nothing to interpolate between and nothing to return");
        if (static_cast<long long>(countX) * countY * countZ > kMaxProbes)
            throw std::invalid_argument(
                "CNA::Graphics::LightProbeVolumeEXT: more probes than the volume accepts -- the "
                "grid is uploaded as a texture and the bound is what its size is chosen from");
        if (bounds.Max.X < bounds.Min.X || bounds.Max.Y < bounds.Min.Y ||
            bounds.Max.Z < bounds.Min.Z)
            throw std::invalid_argument(
                "CNA::Graphics::LightProbeVolumeEXT: the box is inverted on at least one axis, so "
                "the grid it describes has no inside");

        probes_.resize(static_cast<std::size_t>(countX) * countY * countZ);
        for (int z = 0; z < countZ_; ++z)
            for (int y = 0; y < countY_; ++y)
                for (int x = 0; x < countX_; ++x)
                    probes_[static_cast<std::size_t>(indexOf(x, y, z))]
                        .setPosition(getProbePosition(x, y, z));
    }

    int LightProbeVolumeEXT::indexOf(const int x, const int y, const int z) const
    {
        return (z * countY_ + y) * countX_ + x;
    }

    BoundingBox LightProbeVolumeEXT::getBounds() const { return bounds_; }
    int LightProbeVolumeEXT::getCountX() const { return countX_; }
    int LightProbeVolumeEXT::getCountY() const { return countY_; }
    int LightProbeVolumeEXT::getCountZ() const { return countZ_; }
    int LightProbeVolumeEXT::getProbeCount() const { return countX_ * countY_ * countZ_; }

    Vector3 LightProbeVolumeEXT::getProbePosition(const int x, const int y, const int z) const
    {
        if (x < 0 || x >= countX_ || y < 0 || y >= countY_ || z < 0 || z >= countZ_)
            throw std::out_of_range(
                "CNA::Graphics::LightProbeVolumeEXT::getProbePosition: no probe has that index");

        const auto along = [](const int index, const int count, const float low, const float high) {
            if (count <= 1) return low;
            return low + (high - low) * static_cast<float>(index) / static_cast<float>(count - 1);
        };
        return Vector3(along(x, countX_, bounds_.Min.X, bounds_.Max.X),
                       along(y, countY_, bounds_.Min.Y, bounds_.Max.Y),
                       along(z, countZ_, bounds_.Min.Z, bounds_.Max.Z));
    }

    const LightProbeEXT& LightProbeVolumeEXT::getProbe(const int x, const int y, const int z) const
    {
        if (x < 0 || x >= countX_ || y < 0 || y >= countY_ || z < 0 || z >= countZ_)
            throw std::out_of_range(
                "CNA::Graphics::LightProbeVolumeEXT::getProbe: no probe has that index");
        return probes_[static_cast<std::size_t>(indexOf(x, y, z))];
    }

    void LightProbeVolumeEXT::setProbe(const int x, const int y, const int z,
                                       const LightProbeEXT& probe)
    {
        if (x < 0 || x >= countX_ || y < 0 || y >= countY_ || z < 0 || z >= countZ_)
            throw std::out_of_range(
                "CNA::Graphics::LightProbeVolumeEXT::setProbe: no probe has that index");

        LightProbeEXT stored = probe;
        // The grid decides where a probe is. A probe placed somewhere else makes the interpolation
        // weights describe one arrangement and the light another, and the result looks like the
        // lighting is lagging behind the geometry.
        stored.setPosition(getProbePosition(x, y, z));
        probes_[static_cast<std::size_t>(indexOf(x, y, z))] = stored;
    }

    bool LightProbeVolumeEXT::contains(const Vector3& position) const
    {
        return position.X >= bounds_.Min.X && position.X <= bounds_.Max.X &&
               position.Y >= bounds_.Min.Y && position.Y <= bounds_.Max.Y &&
               position.Z >= bounds_.Min.Z && position.Z <= bounds_.Max.Z;
    }

    LightProbeEXT LightProbeVolumeEXT::sampleProbe(const Vector3& position) const
    {
        const float fx = Fraction(position.X, bounds_.Min.X, bounds_.Max.X) *
                         static_cast<float>(countX_ - 1);
        const float fy = Fraction(position.Y, bounds_.Min.Y, bounds_.Max.Y) *
                         static_cast<float>(countY_ - 1);
        const float fz = Fraction(position.Z, bounds_.Min.Z, bounds_.Max.Z) *
                         static_cast<float>(countZ_ - 1);

        const int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, countX_ - 1);
        const int y0 = std::clamp(static_cast<int>(std::floor(fy)), 0, countY_ - 1);
        const int z0 = std::clamp(static_cast<int>(std::floor(fz)), 0, countZ_ - 1);
        const int x1 = std::min(x0 + 1, countX_ - 1);
        const int y1 = std::min(y0 + 1, countY_ - 1);
        const int z1 = std::min(z0 + 1, countZ_ - 1);

        const float tx = std::clamp(fx - static_cast<float>(x0), 0.0f, 1.0f);
        const float ty = std::clamp(fy - static_cast<float>(y0), 0.0f, 1.0f);
        const float tz = std::clamp(fz - static_cast<float>(z0), 0.0f, 1.0f);

        LightProbeEXT blended(Vector3(std::clamp(position.X, bounds_.Min.X, bounds_.Max.X),
                                      std::clamp(position.Y, bounds_.Min.Y, bounds_.Max.Y),
                                      std::clamp(position.Z, bounds_.Min.Z, bounds_.Max.Z)));

        // The trilinear weight of each corner, and then the visibility each corner has of the point
        // being lit (MOD-2083). A probe that recorded a wall closer than the point cannot be
        // lighting it, and multiplying its weight by that test is what stops a lit room from
        // leaking into the dark one next door.
        const Vector3 target = blended.getPosition();
        float weights[8] = {};
        float total = 0.0f;
        float trilinearTotal = 0.0f;
        for (int corner = 0; corner < 8; ++corner)
        {
            const bool useX1 = (corner & 1) != 0;
            const bool useY1 = (corner & 2) != 0;
            const bool useZ1 = (corner & 4) != 0;
            const float trilinear = (useX1 ? tx : 1.0f - tx) * (useY1 ? ty : 1.0f - ty) *
                                    (useZ1 ? tz : 1.0f - tz);
            trilinearTotal += trilinear;
            if (trilinear <= 0.0f) continue;

            const LightProbeEXT& probe =
                probes_[static_cast<std::size_t>(
                    indexOf(useX1 ? x1 : x0, useY1 ? y1 : y0, useZ1 ? z1 : z0))];
            const Vector3 toTarget(target.X - probe.getPosition().X,
                                   target.Y - probe.getPosition().Y,
                                   target.Z - probe.getPosition().Z);
            const float distance = std::sqrt(toTarget.X * toTarget.X + toTarget.Y * toTarget.Y +
                                             toTarget.Z * toTarget.Z);
            weights[corner] = trilinear * probe.visibilityWeight(toTarget, distance);
            total += weights[corner];
        }

        // Every visible corner was rejected, which happens where a point is enclosed by geometry on
        // all sides. Falling back to the plain trilinear blend is a leak; returning black is a hole
        // in the lighting, and a hole is the more visible mistake -- so the leak is chosen, and
        // said out loud rather than left to be discovered.
        if (!(total > 0.0f))
        {
            for (int corner = 0; corner < 8; ++corner)
            {
                const bool useX1 = (corner & 1) != 0;
                const bool useY1 = (corner & 2) != 0;
                const bool useZ1 = (corner & 4) != 0;
                weights[corner] = (useX1 ? tx : 1.0f - tx) * (useY1 ? ty : 1.0f - ty) *
                                  (useZ1 ? tz : 1.0f - tz);
            }
            total = trilinearTotal;
        }

        // Trilinear on the coefficients themselves, which is only meaningful because the projection
        // onto them is linear: the blend of eight probes' coefficients is the projection of the
        // blend of their light, so the result is a valid probe rather than an approximation of one.
        for (int index = 0; index < LightProbeEXT::kCoefficientCount; ++index)
        {
            Vector3 sum(0.0f, 0.0f, 0.0f);
            for (int corner = 0; corner < 8; ++corner)
            {
                if (weights[corner] <= 0.0f) continue;
                const bool useX1 = (corner & 1) != 0;
                const bool useY1 = (corner & 2) != 0;
                const bool useZ1 = (corner & 4) != 0;
                // Renormalised, so rejecting a corner redistributes its share rather than darkening
                // the result: a surface beside a wall should be lit by the probes that can see it,
                // not by a fraction of the ones that cannot.
                const float weight = weights[corner] / total;

                const Vector3 value =
                    probes_[static_cast<std::size_t>(
                                indexOf(useX1 ? x1 : x0, useY1 ? y1 : y0, useZ1 ? z1 : z0))]
                        .getCoefficient(index);
                sum = Vector3(sum.X + value.X * weight, sum.Y + value.Y * weight,
                              sum.Z + value.Z * weight);
            }
            blended.setCoefficient(index, sum);
        }
        return blended;
    }

    Vector3 LightProbeVolumeEXT::irradiance(const Vector3& position, const Vector3& normal) const
    {
        return sampleProbe(position).irradiance(normal);
    }

    bool LightProbeVolumeEXT::isZero() const
    {
        for (const LightProbeEXT& probe : probes_)
            if (!probe.isZero()) return false;
        return true;
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
