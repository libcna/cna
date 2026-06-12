// SPDX-License-Identifier: MS-PL

#pragma once

#include <string>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"

namespace Microsoft::Xna::Framework
{
    struct Matrix;
    struct Quaternion;

    /// Describes a three-component floating point vector.
    struct Vector3
    {
        /// Vector with all components set to zero.
        static const Vector3 Zero;
        /// Vector with all components set to one.
        static const Vector3 One;
        /// Unit vector pointing along the positive X axis.
        static const Vector3 UnitX;
        /// Unit vector pointing along the positive Y axis.
        static const Vector3 UnitY;
        /// Unit vector pointing along the positive Z axis.
        static const Vector3 UnitZ;
        /// Unit vector pointing up (0, 1, 0).
        static const Vector3 Up;
        /// Unit vector pointing down (0, -1, 0).
        static const Vector3 Down;
        /// Unit vector pointing right (1, 0, 0).
        static const Vector3 Right;
        /// Unit vector pointing left (-1, 0, 0).
        static const Vector3 Left;
        /// Unit vector pointing forward in a right-handed coordinate system (0, 0, -1).
        static const Vector3 Forward;
        /// Unit vector pointing backward in a right-handed coordinate system (0, 0, 1).
        static const Vector3 Backward;

        /// X component.
        float X;

        /// Y component.
        float Y;

        /// Z component.
        float Z;

        /// Creates a zero vector.
        Vector3();

        /// Creates a vector from three component values.
        Vector3(float x, float y, float z);

        /// Creates a vector with all components set to the same value.
        explicit Vector3(float value);

        /// Creates a vector from a Vector2 and a Z value.
        Vector3(Vector2 value, float z);

        /// Compares this vector with another for equality.
        [[nodiscard]] bool Equals(const Vector3& other) const;
        /// Returns a hash code for this vector.
        [[nodiscard]] int GetHashCode() const;
        /// Returns the length of this vector.
        [[nodiscard]] float Length() const;
        /// Returns the squared length of this vector.
        [[nodiscard]] float LengthSquared() const;
        /// Turns this vector into a unit vector with the same direction.
        void Normalize();
        /// Returns a string in the form {X:... Y:... Z:...}.
        [[nodiscard]] std::string ToString() const;
        /// Adds another vector to this vector in place.
        NOXNA Vector3& operator+=(const Vector3& vector3);
        /// Subtracts another vector from this vector in place.
        NOXNA Vector3& operator-=(const Vector3& vector3);

        /// Adds two vectors.
        [[nodiscard]] static Vector3 Add(Vector3 value1, Vector3 value2);
        /// Stores the sum of two vectors in an output parameter.
        static void Add(const Vector3& value1, const Vector3& value2, Vector3& result);

        /// Converts barycentric coordinates to a point inside a triangle.
        [[nodiscard]] static Vector3 Barycentric(Vector3 value1, Vector3 value2, Vector3 value3, float amount1,
                                                 float amount2);
        /// Stores the barycentric result in an output parameter.
        static void Barycentric(const Vector3& value1, const Vector3& value2, const Vector3& value3, float amount1,
                                float amount2, Vector3& result);

        /// Performs Catmull-Rom interpolation between vectors.
        [[nodiscard]] static Vector3 CatmullRom(Vector3 value1, Vector3 value2, Vector3 value3, Vector3 value4,
                                                float amount);
        /// Stores the Catmull-Rom result in an output parameter.
        static void CatmullRom(const Vector3& value1, const Vector3& value2, const Vector3& value3,
                               const Vector3& value4, float amount, Vector3& result);

        /// Clamps each component between the matching minimum and maximum component.
        [[nodiscard]] static Vector3 Clamp(Vector3 value1, Vector3 min, Vector3 max);
        /// Stores the clamped result in an output parameter.
        static void Clamp(const Vector3& value1, const Vector3& min, const Vector3& max, Vector3& result);

        /// Returns the cross product of two vectors.
        [[nodiscard]] static Vector3 Cross(Vector3 vector1, Vector3 vector2);
        /// Stores the cross product of two vectors in an output parameter.
        static void Cross(const Vector3& vector1, const Vector3& vector2, Vector3& result);

        /// Returns the distance between two vectors.
        [[nodiscard]] static float Distance(Vector3 value1, Vector3 value2);
        /// Stores the distance between two vectors in an output parameter.
        static void Distance(const Vector3& value1, const Vector3& value2, float& result);

        /// Returns the squared distance between two vectors.
        [[nodiscard]] static float DistanceSquared(Vector3 value1, Vector3 value2);
        /// Stores the squared distance between two vectors in an output parameter.
        static void DistanceSquared(const Vector3& value1, const Vector3& value2, float& result);

        /// Divides a vector component-wise by another vector.
        [[nodiscard]] static Vector3 Divide(Vector3 value1, Vector3 value2);
        /// Stores the component-wise division result in an output parameter.
        static void Divide(const Vector3& value1, const Vector3& value2, Vector3& result);

        /// Divides a vector by a scalar.
        [[nodiscard]] static Vector3 Divide(Vector3 value1, float divider);
        /// Stores the scalar division result in an output parameter.
        static void Divide(const Vector3& value1, float divider, Vector3& result);

        /// Returns the dot product of two vectors.
        [[nodiscard]] static float Dot(Vector3 value1, Vector3 value2);
        /// Stores the dot product of two vectors in an output parameter.
        static void Dot(const Vector3& value1, const Vector3& value2, float& result);

        /// Performs Hermite spline interpolation.
        [[nodiscard]] static Vector3 Hermite(Vector3 value1, Vector3 tangent1, Vector3 value2, Vector3 tangent2,
                                             float amount);
        /// Stores the Hermite spline result in an output parameter.
        static void Hermite(const Vector3& value1, const Vector3& tangent1, const Vector3& value2,
                            const Vector3& tangent2, float amount, Vector3& result);

        /// Linearly interpolates between two vectors.
        [[nodiscard]] static Vector3 Lerp(Vector3 value1, Vector3 value2, float amount);
        /// Stores the linearly interpolated vector in an output parameter.
        static void Lerp(const Vector3& value1, const Vector3& value2, float amount, Vector3& result);

        /// Returns the component-wise maximum.
        [[nodiscard]] static Vector3 Max(Vector3 value1, Vector3 value2);
        /// Stores the component-wise maximum in an output parameter.
        static void Max(const Vector3& value1, const Vector3& value2, Vector3& result);

        /// Returns the component-wise minimum.
        [[nodiscard]] static Vector3 Min(Vector3 value1, Vector3 value2);
        /// Stores the component-wise minimum in an output parameter.
        static void Min(const Vector3& value1, const Vector3& value2, Vector3& result);

        /// Multiplies vectors component-wise.
        [[nodiscard]] static Vector3 Multiply(Vector3 value1, Vector3 value2);
        /// Stores the component-wise product in an output parameter.
        static void Multiply(const Vector3& value1, const Vector3& value2, Vector3& result);

        /// Multiplies a vector by a scalar.
        [[nodiscard]] static Vector3 Multiply(Vector3 value1, float scaleFactor);
        /// Stores the scalar product in an output parameter.
        static void Multiply(const Vector3& value1, float scaleFactor, Vector3& result);

        /// Negates all vector components.
        [[nodiscard]] static Vector3 Negate(Vector3 value);
        /// Stores the negated vector in an output parameter.
        static void Negate(const Vector3& value, Vector3& result);

        /// Returns a normalized copy of a vector.
        [[nodiscard]] static Vector3 Normalize(Vector3 value);
        /// Stores the normalized vector in an output parameter.
        static void Normalize(const Vector3& value, Vector3& result);

        /// Reflects a vector across a normal.
        [[nodiscard]] static Vector3 Reflect(Vector3 vector, Vector3 normal);
        /// Stores the reflected vector in an output parameter.
        static void Reflect(const Vector3& vector, const Vector3& normal, Vector3& result);

        /// Performs smooth Hermite interpolation between two vectors.
        [[nodiscard]] static Vector3 SmoothStep(Vector3 value1, Vector3 value2, float amount);
        /// Stores the smooth-step result in an output parameter.
        static void SmoothStep(const Vector3& value1, const Vector3& value2, float amount, Vector3& result);

        /// Subtracts one vector from another.
        [[nodiscard]] static Vector3 Subtract(Vector3 value1, Vector3 value2);
        /// Stores the difference of two vectors in an output parameter.
        static void Subtract(const Vector3& value1, const Vector3& value2, Vector3& result);

        /// Transforms a position by a matrix.
        [[nodiscard]] static Vector3 Transform(Vector3 position, const Matrix& matrix);
        /// Stores the matrix-transformed position in an output parameter.
        static void Transform(const Vector3& position, const Matrix& matrix, Vector3& result);
        /// Transforms an array of positions by a matrix.
        static void Transform(const std::vector<Vector3>& sourceArray, const Matrix& matrix,
                              std::vector<Vector3>& destinationArray);
        /// Transforms a range of positions in an array by a matrix.
        static void Transform(const std::vector<Vector3>& sourceArray, int sourceIndex, const Matrix& matrix,
                              std::vector<Vector3>& destinationArray, int destinationIndex, int length);

        /// Transforms a vector by a quaternion rotation.
        [[nodiscard]] static Vector3 Transform(Vector3 value, const Quaternion& rotation);
        /// Stores the quaternion-rotated vector in an output parameter.
        static void Transform(const Vector3& value, const Quaternion& rotation, Vector3& result);
        /// Transforms an array of vectors by a quaternion rotation.
        static void Transform(const std::vector<Vector3>& sourceArray, const Quaternion& rotation,
                              std::vector<Vector3>& destinationArray);
        /// Transforms a range of vectors in an array by a quaternion rotation.
        static void Transform(const std::vector<Vector3>& sourceArray, int sourceIndex, const Quaternion& rotation,
                              std::vector<Vector3>& destinationArray, int destinationIndex, int length);

        /// Transforms a normal by a matrix without applying translation.
        [[nodiscard]] static Vector3 TransformNormal(Vector3 normal, const Matrix& matrix);
        /// Stores the matrix-transformed normal in an output parameter.
        static void TransformNormal(const Vector3& normal, const Matrix& matrix, Vector3& result);
        /// Transforms an array of normals by a matrix.
        static void TransformNormal(const std::vector<Vector3>& sourceArray, const Matrix& matrix,
                                    std::vector<Vector3>& destinationArray);
        /// Transforms a range of normals in an array by a matrix.
        static void TransformNormal(const std::vector<Vector3>& sourceArray, int sourceIndex, const Matrix& matrix,
                                    std::vector<Vector3>& destinationArray, int destinationIndex, int length);

        /// Returns true when all components are equal.
        friend bool operator==(Vector3 value1, Vector3 value2);
        /// Returns true when any component differs.
        friend bool operator!=(Vector3 value1, Vector3 value2);
        /// Adds two vectors component-wise.
        friend Vector3 operator+(Vector3 value1, Vector3 value2);
        /// Negates all components of a vector.
        friend Vector3 operator-(Vector3 value);
        /// Subtracts one vector from another component-wise.
        friend Vector3 operator-(Vector3 value1, Vector3 value2);
        /// Multiplies two vectors component-wise.
        friend Vector3 operator*(Vector3 value1, Vector3 value2);
        /// Multiplies all components of a vector by a scalar.
        friend Vector3 operator*(Vector3 value, float scaleFactor);
        /// Multiplies all components of a vector by a scalar.
        friend Vector3 operator*(float scaleFactor, Vector3 value);
        /// Divides one vector by another component-wise.
        friend Vector3 operator/(Vector3 value1, Vector3 value2);
        /// Divides all components of a vector by a scalar.
        friend Vector3 operator/(Vector3 value1, float divider);

    private:
        [[nodiscard]] std::string getDebugDisplayStringProperty() const;
        void CheckForNaNs() const;
    };
}
