//
// Vector4.hpp — minimal Microsoft::Xna::Framework::Vector4 implementation.
//
// Mirrors the public shape of Microsoft.Xna.Framework.Vector4 from XNA 4.0
// with the subset needed to support Color (constructor, ToVector4, etc.).
//
// Style follows the existing Vector3.hpp in this project.

#pragma once

namespace Microsoft::Xna::Framework
{
    /**
     * @brief Four-component single-precision floating-point vector.
     *
     * Mirrors @c Microsoft.Xna.Framework.Vector4 from XNA 4.0.
     * Only the minimal subset required to support @c Color is provided here;
     * a full implementation can be added later following the Vector3 pattern.
     */
    struct Vector4
    {
        float X;
        float Y;
        float Z;
        float W;

        /** Default constructor — initializes all components to zero. */
        Vector4();

        /** Constructs a Vector4 with the given X, Y, Z, W components. */
        Vector4(float x, float y, float z, float w);

        /** Equality comparison. */
        bool operator==(const Vector4& rhs) const;
        /** Inequality comparison. */
        bool operator!=(const Vector4& rhs) const;

        static const Vector4 Zero;
        static const Vector4 One;
    };
}
