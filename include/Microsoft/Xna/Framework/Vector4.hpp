// SPDX-License-Identifier: MS-PL

#pragma once

#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

namespace Microsoft::Xna::Framework
{
    struct Matrix;
    struct Quaternion;

    /// Describes a four-component floating point vector.
    struct Vector4
    {
        /// Vector with all components set to zero.
        static const Vector4 Zero;
        /// Vector with all components set to one.
        static const Vector4 One;
        /// Unit vector along the X axis (1, 0, 0, 0).
        static const Vector4 UnitX;
        /// Unit vector along the Y axis (0, 1, 0, 0).
        static const Vector4 UnitY;
        /// Unit vector along the Z axis (0, 0, 1, 0).
        static const Vector4 UnitZ;
        /// Unit vector along the W axis (0, 0, 0, 1).
        static const Vector4 UnitW;

        /// X component.
        float X;
        /// Y component.
        float Y;
        /// Z component.
        float Z;
        /// W component.
        float W;

        /// Creates a zero vector.
        Vector4();
        /// Creates a vector from four component values.
        Vector4(float x, float y, float z, float w);
        /// Creates a vector from a Vector2 and two scalar values.
        Vector4(Vector2 value, float z, float w);
        /// Creates a vector from a Vector3 and a W value.
        Vector4(Vector3 value, float w);
        /// Creates a vector with all four components set to the same value.
        explicit Vector4(float value);

        /// Compares this vector with another for equality.
        [[nodiscard]] bool Equals(const Vector4& other) const;
        /// Returns a hash code for this vector.
        [[nodiscard]] int GetHashCode() const;
        /// Returns the length of this vector.
        [[nodiscard]] float Length() const;
        /// Returns the squared length of this vector.
        [[nodiscard]] float LengthSquared() const;
        /// Turns this vector into a unit vector with the same direction.
        void Normalize();
        /// Returns a string in the form {X:... Y:... Z:... W:...}.
        [[nodiscard]] std::string ToString() const;

        /// Adds two vectors.
        [[nodiscard]] static Vector4 Add(Vector4 value1, Vector4 value2);
        /// Stores the sum of two vectors in an output parameter.
        static void Add(const Vector4& value1, const Vector4& value2, Vector4& result);

        /// Converts barycentric coordinates to a point inside a triangle.
        [[nodiscard]] static Vector4 Barycentric(Vector4 value1, Vector4 value2, Vector4 value3, float amount1,
                                                 float amount2);
        /// Stores the barycentric result in an output parameter.
        static void Barycentric(const Vector4& value1, const Vector4& value2, const Vector4& value3, float amount1,
                                float amount2, Vector4& result);

        /// Performs Catmull-Rom interpolation between vectors.
        [[nodiscard]] static Vector4 CatmullRom(Vector4 value1, Vector4 value2, Vector4 value3, Vector4 value4,
                                                float amount);
        /// Stores the Catmull-Rom result in an output parameter.
        static void CatmullRom(const Vector4& value1, const Vector4& value2, const Vector4& value3,
                               const Vector4& value4, float amount, Vector4& result);

        /// Clamps each component between the matching minimum and maximum component.
        [[nodiscard]] static Vector4 Clamp(Vector4 value1, Vector4 min, Vector4 max);
        /// Stores the clamped result in an output parameter.
        static void Clamp(const Vector4& value1, const Vector4& min, const Vector4& max, Vector4& result);

        /// Returns the distance between two vectors.
        [[nodiscard]] static float Distance(Vector4 value1, Vector4 value2);
        /// Stores the distance between two vectors in an output parameter.
        static void Distance(const Vector4& value1, const Vector4& value2, float& result);

        /// Returns the squared distance between two vectors.
        [[nodiscard]] static float DistanceSquared(Vector4 value1, Vector4 value2);
        /// Stores the squared distance between two vectors in an output parameter.
        static void DistanceSquared(const Vector4& value1, const Vector4& value2, float& result);

        /// Divides a vector component-wise by another vector.
        [[nodiscard]] static Vector4 Divide(Vector4 value1, Vector4 value2);
        /// Stores the component-wise division result in an output parameter.
        static void Divide(const Vector4& value1, const Vector4& value2, Vector4& result);

        /// Divides a vector by a scalar.
        [[nodiscard]] static Vector4 Divide(Vector4 value1, float divider);
        /// Stores the scalar division result in an output parameter.
        static void Divide(const Vector4& value1, float divider, Vector4& result);

        /// Returns the dot product of two vectors.
        [[nodiscard]] static float Dot(Vector4 value1, Vector4 value2);
        /// Stores the dot product of two vectors in an output parameter.
        static void Dot(const Vector4& value1, const Vector4& value2, float& result);

        /// Performs Hermite spline interpolation.
        [[nodiscard]] static Vector4 Hermite(Vector4 value1, Vector4 tangent1, Vector4 value2, Vector4 tangent2,
                                             float amount);
        /// Stores the Hermite spline result in an output parameter.
        static void Hermite(const Vector4& value1, const Vector4& tangent1, const Vector4& value2,
                            const Vector4& tangent2, float amount, Vector4& result);

        /// Linearly interpolates between two vectors.
        [[nodiscard]] static Vector4 Lerp(Vector4 value1, Vector4 value2, float amount);
        /// Stores the linearly interpolated vector in an output parameter.
        static void Lerp(const Vector4& value1, const Vector4& value2, float amount, Vector4& result);

        /// Returns the component-wise maximum.
        [[nodiscard]] static Vector4 Max(Vector4 value1, Vector4 value2);
        /// Stores the component-wise maximum in an output parameter.
        static void Max(const Vector4& value1, const Vector4& value2, Vector4& result);

        /// Returns the component-wise minimum.
        [[nodiscard]] static Vector4 Min(Vector4 value1, Vector4 value2);
        /// Stores the component-wise minimum in an output parameter.
        static void Min(const Vector4& value1, const Vector4& value2, Vector4& result);

        /// Multiplies vectors component-wise.
        [[nodiscard]] static Vector4 Multiply(Vector4 value1, Vector4 value2);
        /// Stores the component-wise product in an output parameter.
        static void Multiply(const Vector4& value1, const Vector4& value2, Vector4& result);

        /// Multiplies a vector by a scalar.
        [[nodiscard]] static Vector4 Multiply(Vector4 value1, float scaleFactor);
        /// Stores the scalar product in an output parameter.
        static void Multiply(const Vector4& value1, float scaleFactor, Vector4& result);

        /// Negates all vector components.
        [[nodiscard]] static Vector4 Negate(Vector4 value);
        /// Stores the negated vector in an output parameter.
        static void Negate(const Vector4& value, Vector4& result);

        /// Returns a normalized copy of a vector.
        [[nodiscard]] static Vector4 Normalize(Vector4 value);
        /// Stores the normalized vector in an output parameter.
        static void Normalize(const Vector4& value, Vector4& result);

        /// Performs smooth Hermite interpolation between two vectors.
        [[nodiscard]] static Vector4 SmoothStep(Vector4 value1, Vector4 value2, float amount);
        /// Stores the smooth-step result in an output parameter.
        static void SmoothStep(const Vector4& value1, const Vector4& value2, float amount, Vector4& result);

        /// Subtracts one vector from another.
        [[nodiscard]] static Vector4 Subtract(Vector4 value1, Vector4 value2);
        /// Stores the difference of two vectors in an output parameter.
        static void Subtract(const Vector4& value1, const Vector4& value2, Vector4& result);

        /// Transforms a Vector2 position by a matrix, producing a Vector4.
        [[nodiscard]] static Vector4 Transform(Vector2 position, const Matrix& matrix);
        /// Transforms a Vector3 position by a matrix, producing a Vector4.
        [[nodiscard]] static Vector4 Transform(Vector3 position, const Matrix& matrix);
        /// Transforms a Vector4 by a matrix.
        [[nodiscard]] static Vector4 Transform(Vector4 vector, const Matrix& matrix);
        /// Stores the matrix-transformed Vector2 result in an output parameter.
        static void Transform(const Vector2& position, const Matrix& matrix, Vector4& result);
        /// Stores the matrix-transformed Vector3 result in an output parameter.
        static void Transform(const Vector3& position, const Matrix& matrix, Vector4& result);
        /// Stores the matrix-transformed Vector4 result in an output parameter.
        static void Transform(const Vector4& vector, const Matrix& matrix, Vector4& result);
        /// Transforms an array of Vector4 values by a matrix.
        static void Transform(const std::vector<Vector4>& sourceArray, const Matrix& matrix,
                              std::vector<Vector4>& destinationArray);
        /// Transforms a range of Vector4 values in an array by a matrix.
        static void Transform(const std::vector<Vector4>& sourceArray, int sourceIndex, const Matrix& matrix,
                              std::vector<Vector4>& destinationArray, int destinationIndex, int length);

        /// Transforms a Vector2 by a quaternion rotation, producing a Vector4.
        [[nodiscard]] static Vector4 Transform(Vector2 value, const Quaternion& rotation);
        /// Transforms a Vector3 by a quaternion rotation, producing a Vector4.
        [[nodiscard]] static Vector4 Transform(Vector3 value, const Quaternion& rotation);
        /// Transforms a Vector4 by a quaternion rotation.
        [[nodiscard]] static Vector4 Transform(Vector4 value, const Quaternion& rotation);
        /// Stores the quaternion-rotated Vector2 result in an output parameter.
        static void Transform(const Vector2& value, const Quaternion& rotation, Vector4& result);
        /// Stores the quaternion-rotated Vector3 result in an output parameter.
        static void Transform(const Vector3& value, const Quaternion& rotation, Vector4& result);
        /// Stores the quaternion-rotated Vector4 result in an output parameter.
        static void Transform(const Vector4& value, const Quaternion& rotation, Vector4& result);
        /// Transforms an array of Vector4 values by a quaternion rotation.
        static void Transform(const std::vector<Vector4>& sourceArray, const Quaternion& rotation,
                              std::vector<Vector4>& destinationArray);
        /// Transforms a range of Vector4 values in an array by a quaternion rotation.
        static void Transform(const std::vector<Vector4>& sourceArray, int sourceIndex, const Quaternion& rotation,
                              std::vector<Vector4>& destinationArray, int destinationIndex, int length);

        /// Negates all components of a vector.
        friend Vector4 operator-(Vector4 value);
        /// Returns true when all four components are equal.
        friend bool operator==(Vector4 value1, Vector4 value2);
        /// Returns true when any component differs.
        friend bool operator!=(Vector4 value1, Vector4 value2);
        /// Adds two vectors component-wise.
        friend Vector4 operator+(Vector4 value1, Vector4 value2);
        /// Subtracts one vector from another component-wise.
        friend Vector4 operator-(Vector4 value1, Vector4 value2);
        /// Multiplies two vectors component-wise.
        friend Vector4 operator*(Vector4 value1, Vector4 value2);
        /// Multiplies all components of a vector by a scalar.
        friend Vector4 operator*(Vector4 value1, float scaleFactor);
        /// Multiplies all components of a vector by a scalar.
        friend Vector4 operator*(float scaleFactor, Vector4 value1);
        /// Divides one vector by another component-wise.
        friend Vector4 operator/(Vector4 value1, Vector4 value2);
        /// Divides all components of a vector by a scalar.
        friend Vector4 operator/(Vector4 value1, float divider);

    private:
        [[nodiscard]] std::string getDebugDisplayStringProperty() const;
        void CheckForNaNs() const;
    };
}
