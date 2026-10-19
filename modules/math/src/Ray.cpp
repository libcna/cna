// SPDX-License-Identifier: MS-PL

#include "Microsoft/Xna/Framework/Ray.hpp"

#include <cmath>
#include <limits>
#include <functional>
#include <optional>
#include <sstream>
#include <utility>

#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Plane.hpp"

namespace Microsoft::Xna::Framework
{
    Ray::Ray(Vector3 position, Vector3 direction)
        : Position(position), Direction(direction)
    {
    }

    bool Ray::Equals(const std::any& obj) const
    {
        const Ray* other = std::any_cast<Ray>(&obj);
        return other != nullptr && Equals(*other);
    }

    bool Ray::Equals(Ray other) const
    {
        return Position.Equals(other.Position) && Direction.Equals(other.Direction);
    }

    std::size_t Ray::GetHashCode() const
    {
        return Position.GetHashCode() ^ Direction.GetHashCode();
    }

    std::optional<float> Ray::Intersects(BoundingBox box) const
    {
        // XNA BoundingBox.Intersects(Ray) IL clips a nonnegative interval and
        // uses this strict parallel threshold, including stationary/boundary rays.
        float minimum = 0.0f;
        float maximum = std::numeric_limits<float>::max();
        const float origins[] = {Position.X, Position.Y, Position.Z};
        const float directions[] = {Direction.X, Direction.Y, Direction.Z};
        const float lower[] = {box.Min.X, box.Min.Y, box.Min.Z};
        const float upper[] = {box.Max.X, box.Max.Y, box.Max.Z};
        for (int axis = 0; axis < 3; ++axis)
        {
            if (std::abs(directions[axis]) < 1e-6f)
            {
                if (origins[axis] < lower[axis] || origins[axis] > upper[axis])
                    return std::nullopt;
                continue;
            }
            const float inverse = 1.0f / directions[axis];
            float nearParameter = (lower[axis] - origins[axis]) * inverse;
            float farParameter = (upper[axis] - origins[axis]) * inverse;
            if (nearParameter > farParameter) std::swap(nearParameter, farParameter);
            minimum = MathHelper::Max(nearParameter, minimum);
            maximum = MathHelper::Min(farParameter, maximum);
            if (minimum > maximum) return std::nullopt;
        }
        return minimum;
    }

    void Ray::Intersects(const BoundingBox& box, std::optional<float>& result) const
    {
        result = Intersects(box);
    }

    std::optional<float> Ray::Intersects(BoundingSphere sphere) const
    {
        std::optional<float> result;
        Intersects(sphere, result);
        return result;
    }

    void Ray::Intersects(const BoundingSphere& sphere, std::optional<float>& result) const
    {
        Vector3 difference = sphere.Center - Position;

        float differenceLengthSquared = difference.LengthSquared();
        float sphereRadiusSquared = sphere.Radius * sphere.Radius;

        if (differenceLengthSquared < sphereRadiusSquared)
        {
            result = 0.0f;
            return;
        }

        float distanceAlongRay;
        Vector3::Dot(Direction, difference, distanceAlongRay);

        if (distanceAlongRay < 0.0f)
        {
            result = std::nullopt;
            return;
        }

        float dist = sphereRadiusSquared + (distanceAlongRay * distanceAlongRay) - differenceLengthSquared;
        result = (dist < 0.0f) ? std::optional<float>{} : std::optional<float>{distanceAlongRay - std::sqrt(dist)};
    }

    std::optional<float> Ray::Intersects(Plane plane) const
    {
        std::optional<float> result;
        Intersects(plane, result);
        return result;
    }

    void Ray::Intersects(const Plane& plane, std::optional<float>& result) const
    {
        float den = Vector3::Dot(Direction, plane.Normal);
        if (std::fabs(den) < 0.00001f)
        {
            result = std::nullopt;
            return;
        }

        result = (-plane.D - Vector3::Dot(plane.Normal, Position)) / den;

        if (result.has_value() && *result < 0.0f)
        {
            if (*result < -0.00001f)
            {
                result = std::nullopt;
                return;
            }

            result = 0.0f;
        }
    }

    std::optional<float> Ray::Intersects(const BoundingFrustum& frustum) const
    {
        std::optional<float> result;
        frustum.Intersects(*this, result);
        return result;
    }

    std::string Ray::ToString() const
    {
        std::ostringstream ss;
        ss << "{Position:" << Position.ToString() << " Direction:" << Direction.ToString() << "}";
        return ss.str();
    }

    bool operator==(Ray a, Ray b)
    {
        return a.Equals(b);
    }

    bool operator!=(Ray a, Ray b)
    {
        return !a.Equals(b);
    }
}
