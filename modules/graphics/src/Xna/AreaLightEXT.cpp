// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/AreaLightEXT.hpp"

#include <cmath>

namespace Microsoft::Xna::Framework::Graphics {

    namespace {

        float LengthSquared(const Vector3& v) { return v.X * v.X + v.Y * v.Y + v.Z * v.Z; }

        Vector3 Cross(const Vector3& a, const Vector3& b)
        {
            return Vector3(a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X);
        }

        bool IsFinite(const Vector3& v)
        {
            return std::isfinite(v.X) && std::isfinite(v.Y) && std::isfinite(v.Z);
        }

    } // namespace

    bool AreaLightEXT::IsValidEXT() const
    {
        if (!IsFinite(Position) || !IsFinite(RightAxis) || !IsFinite(UpAxis) || !IsFinite(Color))
            return false;
        if (!std::isfinite(Intensity) || Intensity < 0.0f) return false;
        if (!std::isfinite(Range) || !(Range > 0.0f)) return false;
        if (!(LengthSquared(RightAxis) > 1e-12f) || !(LengthSquared(UpAxis) > 1e-12f)) return false;

        // A tube is a line with a radius, so its two axes are allowed to be anything non-zero. The
        // other two shapes are surfaces, and parallel axes give them no area to emit from -- which
        // the form factor answers with a division by zero rather than with darkness.
        if (Shape == AreaLightShapeEXT::Tube) return true;
        return LengthSquared(Cross(RightAxis, UpAxis)) > 1e-12f;
    }

} // namespace Microsoft::Xna::Framework::Graphics
