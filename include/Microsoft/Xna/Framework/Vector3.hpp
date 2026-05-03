//
// Created by robertvokac on 5/24/25.
//
#pragma once

namespace Microsoft::Xna::Framework {
    /**
     * @brief Three-component single-precision floating point vector.
     *
     * Mirrors the public shape of `Microsoft.Xna.Framework.Vector3` from XNA 4.0
     * with the minimal subset needed by the early CNA 3D pipeline.
     *
     * @note Status: PARTIAL. Only basic arithmetic, dot, cross, length and
     *       normalize are implemented. Static constants (Up, Forward, ...)
     *       and overloads accepting reference parameters are not yet provided.
     */
    struct Vector3 {
    public:
        float X;
        float Y;
        float Z;

        Vector3();
        Vector3(float x, float y, float z);

        /** Returns the vector's Euclidean length. */
        [[nodiscard]] float Length() const;
        /** Returns the squared Euclidean length (no sqrt). */
        [[nodiscard]] float LengthSquared() const;
        /** Normalizes this vector in place. Zero-length vectors are left unchanged. */
        void Normalize();

        /** Dot product. */
        static float Dot(const Vector3& a, const Vector3& b);
        /** Cross product (right-handed). */
        static Vector3 Cross(const Vector3& a, const Vector3& b);
        /** Returns a normalized copy of `v`. */
        static Vector3 Normalize(const Vector3& v);

        Vector3 operator+(const Vector3& rhs) const;
        Vector3 operator-(const Vector3& rhs) const;
        Vector3 operator-() const;
        Vector3 operator*(float s) const;
        Vector3 operator/(float s) const;
        Vector3& operator+=(const Vector3& rhs);
        Vector3& operator-=(const Vector3& rhs);
        Vector3& operator*=(float s);

        static const Vector3 Zero;
        static const Vector3 One;
        static const Vector3 UnitX;
        static const Vector3 UnitY;
        static const Vector3 UnitZ;
        static const Vector3 Up;
        static const Vector3 Down;
        static const Vector3 Right;
        static const Vector3 Left;
        /** XNA is right-handed: Forward is -Z. */
        static const Vector3 Forward;
        /** XNA is right-handed: Backward is +Z. */
        static const Vector3 Backward;
    };

    Vector3 operator*(float s, const Vector3& v);
}
