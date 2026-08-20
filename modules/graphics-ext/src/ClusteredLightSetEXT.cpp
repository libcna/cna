// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/ClusteredLightSetEXT.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/PointLightEXT.hpp"
#include "CNA/Graphics/SpotLightEXT.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::BoundingSphere;
    using Microsoft::Xna::Framework::Vector3;

    namespace {

        constexpr float kHalfPi = 1.57079632679f;

        bool IsFinite(const Vector3& v)
        {
            return std::isfinite(v.X) && std::isfinite(v.Y) && std::isfinite(v.Z);
        }

        Vector3 Normalized(const Vector3& v, const Vector3& fallback)
        {
            const float length = std::sqrt(v.X * v.X + v.Y * v.Y + v.Z * v.Z);
            if (!(length > 1e-6f)) return fallback;
            return Vector3(v.X / length, v.Y / length, v.Z / length);
        }

    } // namespace

    ClusteredLightSetEXT::ClusteredLightSetEXT() = default;

    bool ClusteredLightSetEXT::isUsable(const ClusteredLightEXT& light)
    {
        if (!IsFinite(light.Position) || !IsFinite(light.Color)) return false;
        if (!std::isfinite(light.Intensity) || light.Intensity < 0.0f) return false;
        if (!std::isfinite(light.Range) || !(light.Range > 0.0f)) return false;

        if (light.Type != ClusteredLightType::Spot) return true;

        if (!IsFinite(light.Direction)) return false;
        const float lengthSquared = light.Direction.X * light.Direction.X +
                                    light.Direction.Y * light.Direction.Y +
                                    light.Direction.Z * light.Direction.Z;
        if (!(lengthSquared > 1e-12f)) return false;
        if (!std::isfinite(light.InnerAngle) || !std::isfinite(light.OuterAngle)) return false;
        if (light.InnerAngle < 0.0f || light.OuterAngle > kHalfPi) return false;
        if (light.InnerAngle > light.OuterAngle) return false;
        return true;
    }

    int ClusteredLightSetEXT::add(const ClusteredLightEXT& light)
    {
        if (static_cast<int>(lights_.size()) >= kMaxLights)
            throw std::length_error(
                "CNA::Graphics::ClusteredLightSetEXT::add: the set already holds its maximum of 256 "
                "lights -- the uploaded buffer and the shader's index width are sized from that "
                "bound, so it is refused rather than grown");
        if (!isUsable(light))
            throw std::invalid_argument(
                "CNA::Graphics::ClusteredLightSetEXT::add: the light is not usable -- a range must "
                "be positive, an intensity non-negative, a spot's inner angle no wider than its "
                "outer and its outer no wider than a hemisphere, and every number finite. Refused "
                "here rather than skipped later, because a light that silently does nothing is "
                "harder to find than one that refused to be added");

        lights_.push_back(light);
        return static_cast<int>(lights_.size()) - 1;
    }

    int ClusteredLightSetEXT::add(const PointLightEXT& light)
    {
        ClusteredLightEXT converted;
        converted.Type         = ClusteredLightType::Point;
        converted.Position     = light.Position;
        converted.Color        = light.Color;
        converted.Intensity    = light.Intensity;
        converted.Range        = light.Range;
        converted.CastsShadows = light.CastsShadows;
        return add(converted);
    }

    int ClusteredLightSetEXT::add(const SpotLightEXT& light)
    {
        ClusteredLightEXT converted;
        converted.Type         = ClusteredLightType::Spot;
        converted.Position     = light.Position;
        converted.Direction    = light.Direction;
        converted.Color        = light.Color;
        converted.Intensity    = light.Intensity;
        converted.Range        = light.Range;
        converted.InnerAngle   = light.InnerAngle;
        converted.OuterAngle   = light.OuterAngle;
        converted.CastsShadows = light.CastsShadows;
        return add(converted);
    }

    void ClusteredLightSetEXT::replaceAt(const int index, const ClusteredLightEXT& light)
    {
        if (index < 0 || index >= static_cast<int>(lights_.size()))
            throw std::out_of_range(
                "CNA::Graphics::ClusteredLightSetEXT::replaceAt: no light has that index");
        if (!isUsable(light))
            throw std::invalid_argument(
                "CNA::Graphics::ClusteredLightSetEXT::replaceAt: the light is not usable");
        lights_[static_cast<std::size_t>(index)] = light;
    }

    void ClusteredLightSetEXT::removeAt(const int index)
    {
        if (index < 0 || index >= static_cast<int>(lights_.size()))
            throw std::out_of_range(
                "CNA::Graphics::ClusteredLightSetEXT::removeAt: no light has that index");
        lights_.erase(lights_.begin() + index);
    }

    void ClusteredLightSetEXT::clear() { lights_.clear(); }

    int  ClusteredLightSetEXT::getCount() const { return static_cast<int>(lights_.size()); }
    bool ClusteredLightSetEXT::isEmpty()  const { return lights_.empty(); }

    const ClusteredLightEXT& ClusteredLightSetEXT::getAt(const int index) const
    {
        if (index < 0 || index >= static_cast<int>(lights_.size()))
            throw std::out_of_range(
                "CNA::Graphics::ClusteredLightSetEXT::getAt: no light has that index");
        return lights_[static_cast<std::size_t>(index)];
    }

    const std::vector<ClusteredLightEXT>& ClusteredLightSetEXT::getLights() const { return lights_; }

    BoundingSphere ClusteredLightSetEXT::getBoundsAt(const int index) const
    {
        const ClusteredLightEXT& light = getAt(index);
        if (light.Type != ClusteredLightType::Spot)
            return BoundingSphere(light.Position, light.Range);

        const Vector3 axis = Normalized(light.Direction, Vector3(0.0f, -1.0f, 0.0f));
        const float cosine = std::cos(light.OuterAngle);

        // The standard bounding sphere of a cone, in its two cases. A cone wider than 45 degrees is
        // bounded by the sphere through its base rim, centred at the base; a narrower one is
        // bounded by the sphere through the apex *and* the rim, whose centre sits further out along
        // the axis than the base does. Using the wide case everywhere would be correct but loose,
        // and a torch would claim every cluster behind the person holding it.
        if (light.OuterAngle > 0.78539816339f)   // pi/4
        {
            const float radius = light.Range * std::sin(light.OuterAngle);
            const Vector3 centre(light.Position.X + axis.X * light.Range * cosine,
                                 light.Position.Y + axis.Y * light.Range * cosine,
                                 light.Position.Z + axis.Z * light.Range * cosine);
            return BoundingSphere(centre, radius);
        }

        const float radius = light.Range / (2.0f * std::max(cosine, 1e-4f));
        return BoundingSphere(Vector3(light.Position.X + axis.X * radius,
                                      light.Position.Y + axis.Y * radius,
                                      light.Position.Z + axis.Z * radius),
                              radius);
    }

    std::vector<BoundingSphere> ClusteredLightSetEXT::collectBounds() const
    {
        std::vector<BoundingSphere> bounds;
        bounds.reserve(lights_.size());
        for (int index = 0; index < static_cast<int>(lights_.size()); ++index)
            bounds.push_back(getBoundsAt(index));
        return bounds;
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
