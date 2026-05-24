#include "Microsoft/Xna/Framework/Ray.hpp"

#include <cmath>
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
        std::optional<float> tMin;
        std::optional<float> tMax;

        if (MathHelper::WithinEpsilon(Direction.X, 0.0f))
        {
            if (Position.X < box.Min.X || Position.X > box.Max.X)
            {
                return std::nullopt;
            }
        }
        else
        {
            tMin = (box.Min.X - Position.X) / Direction.X;
            tMax = (box.Max.X - Position.X) / Direction.X;

            if (*tMin > *tMax)
            {
                std::swap(tMin, tMax);
            }
        }

        if (MathHelper::WithinEpsilon(Direction.Y, 0.0f))
        {
            if (Position.Y < box.Min.Y || Position.Y > box.Max.Y)
            {
                return std::nullopt;
            }
        }
        else
        {
            float tMinY = (box.Min.Y - Position.Y) / Direction.Y;
            float tMaxY = (box.Max.Y - Position.Y) / Direction.Y;

            if (tMinY > tMaxY)
            {
                std::swap(tMinY, tMaxY);
            }

            if ((tMin.has_value() && *tMin > tMaxY) ||
                (tMax.has_value() && tMinY > *tMax))
            {
                return std::nullopt;
            }

            if (!tMin.has_value() || tMinY > *tMin)
            {
                tMin = tMinY;
            }
            if (!tMax.has_value() || tMaxY < *tMax)
            {
                tMax = tMaxY;
            }
        }

        if (MathHelper::WithinEpsilon(Direction.Z, 0.0f))
        {
            if (Position.Z < box.Min.Z || Position.Z > box.Max.Z)
            {
                return std::nullopt;
            }
        }
        else
        {
            float tMinZ = (box.Min.Z - Position.Z) / Direction.Z;
            float tMaxZ = (box.Max.Z - Position.Z) / Direction.Z;

            if (tMinZ > tMaxZ)
            {
                std::swap(tMinZ, tMaxZ);
            }

            if ((tMin.has_value() && *tMin > tMaxZ) ||
                (tMax.has_value() && tMinZ > *tMax))
            {
                return std::nullopt;
            }

            if (!tMin.has_value() || tMinZ > *tMin)
            {
                tMin = tMinZ;
            }
            if (!tMax.has_value() || tMaxZ < *tMax)
            {
                tMax = tMaxZ;
            }
        }

        if (tMin.has_value() && *tMin < 0.0f && tMax.has_value() && *tMax > 0.0f)
        {
            return 0.0f;
        }

        if (tMin.has_value() && *tMin < 0.0f)
        {
            return std::nullopt;
        }

        return tMin;
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
        ss << "{{Position:" << Position.ToString() << " Direction:" << Direction.ToString() << "}}";
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
