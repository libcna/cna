#pragma once

#include <string>
#include <vector>

namespace Microsoft::Xna::Framework
{
    struct Matrix;
    struct Quaternion;

    /// Describes a two-component floating point vector.
    struct Vector2
    {
        /// Vector with both components set to zero.
        static const Vector2 Zero;

        /// Vector with both components set to one.
        static const Vector2 One;

        /// Unit vector on the X axis.
        static const Vector2 UnitX;

        /// Unit vector on the Y axis.
        static const Vector2 UnitY;

        /// X component.
        float X;

        /// Y component.
        float Y;

        /// Creates a zero vector.
        Vector2();

        /// Creates a vector from two component values.
        Vector2(float x, float y);

        /// Creates a vector whose components are both set to the same value.
        explicit Vector2(float value);

        /// Compares this vector with another vector.
        [[nodiscard]] bool Equals(const Vector2& other) const;

        /// Returns a hash code for this vector.
        [[nodiscard]] int GetHashCode() const;

        /// Returns the vector length.
        [[nodiscard]] float Length() const;

        /// Returns the squared vector length.
        [[nodiscard]] float LengthSquared() const;

        /// Normalizes this vector in place.
        void Normalize();

        /// Returns a string in the form {X:... Y:...}.
        [[nodiscard]] std::string ToString() const;

        /// Adds two vectors.
        [[nodiscard]] static Vector2 Add(Vector2 value1, Vector2 value2);
        static void Add(const Vector2& value1, const Vector2& value2, Vector2& result);

        /// Converts barycentric coordinates to a vector inside a triangle.
        [[nodiscard]] static Vector2 Barycentric(Vector2 value1, Vector2 value2, Vector2 value3, float amount1, float amount2);
        static void Barycentric(const Vector2& value1, const Vector2& value2, const Vector2& value3, float amount1, float amount2, Vector2& result);

        /// Performs Catmull-Rom interpolation between vectors.
        [[nodiscard]] static Vector2 CatmullRom(Vector2 value1, Vector2 value2, Vector2 value3, Vector2 value4, float amount);
        static void CatmullRom(const Vector2& value1, const Vector2& value2, const Vector2& value3, const Vector2& value4, float amount, Vector2& result);

        /// Clamps each component between the matching minimum and maximum component.
        [[nodiscard]] static Vector2 Clamp(Vector2 value1, Vector2 min, Vector2 max);
        static void Clamp(const Vector2& value1, const Vector2& min, const Vector2& max, Vector2& result);

        /// Returns the distance between two vectors.
        [[nodiscard]] static float Distance(Vector2 value1, Vector2 value2);
        static void Distance(const Vector2& value1, const Vector2& value2, float& result);

        /// Returns the squared distance between two vectors.
        [[nodiscard]] static float DistanceSquared(Vector2 value1, Vector2 value2);
        static void DistanceSquared(const Vector2& value1, const Vector2& value2, float& result);

        /// Divides a vector component-wise by another vector.
        [[nodiscard]] static Vector2 Divide(Vector2 value1, Vector2 value2);
        static void Divide(const Vector2& value1, const Vector2& value2, Vector2& result);

        /// Divides a vector by a scalar.
        [[nodiscard]] static Vector2 Divide(Vector2 value1, float divider);
        static void Divide(const Vector2& value1, float divider, Vector2& result);

        /// Returns the dot product of two vectors.
        [[nodiscard]] static float Dot(Vector2 value1, Vector2 value2);
        static void Dot(const Vector2& value1, const Vector2& value2, float& result);

        /// Performs Hermite spline interpolation.
        [[nodiscard]] static Vector2 Hermite(Vector2 value1, Vector2 tangent1, Vector2 value2, Vector2 tangent2, float amount);
        static void Hermite(const Vector2& value1, const Vector2& tangent1, const Vector2& value2, const Vector2& tangent2, float amount, Vector2& result);

        /// Linearly interpolates between two vectors.
        [[nodiscard]] static Vector2 Lerp(Vector2 value1, Vector2 value2, float amount);
        static void Lerp(const Vector2& value1, const Vector2& value2, float amount, Vector2& result);

        /// Returns the component-wise maximum.
        [[nodiscard]] static Vector2 Max(Vector2 value1, Vector2 value2);
        static void Max(const Vector2& value1, const Vector2& value2, Vector2& result);

        /// Returns the component-wise minimum.
        [[nodiscard]] static Vector2 Min(Vector2 value1, Vector2 value2);
        static void Min(const Vector2& value1, const Vector2& value2, Vector2& result);

        /// Multiplies vectors component-wise.
        [[nodiscard]] static Vector2 Multiply(Vector2 value1, Vector2 value2);
        static void Multiply(const Vector2& value1, const Vector2& value2, Vector2& result);

        /// Multiplies a vector by a scalar.
        [[nodiscard]] static Vector2 Multiply(Vector2 value1, float scaleFactor);
        static void Multiply(const Vector2& value1, float scaleFactor, Vector2& result);

        /// Negates both vector components.
        [[nodiscard]] static Vector2 Negate(Vector2 value);
        static void Negate(const Vector2& value, Vector2& result);

        /// Returns a normalized copy of a vector.
        [[nodiscard]] static Vector2 Normalize(Vector2 value);
        static void Normalize(const Vector2& value, Vector2& result);

        /// Reflects a vector across a normal.
        [[nodiscard]] static Vector2 Reflect(Vector2 vector, Vector2 normal);
        static void Reflect(const Vector2& vector, const Vector2& normal, Vector2& result);

        /// Performs smooth Hermite interpolation between two vectors.
        [[nodiscard]] static Vector2 SmoothStep(Vector2 value1, Vector2 value2, float amount);
        static void SmoothStep(const Vector2& value1, const Vector2& value2, float amount, Vector2& result);

        /// Subtracts one vector from another.
        [[nodiscard]] static Vector2 Subtract(Vector2 value1, Vector2 value2);
        static void Subtract(const Vector2& value1, const Vector2& value2, Vector2& result);

        /// Transforms a position by a matrix.
        [[nodiscard]] static Vector2 Transform(Vector2 position, const Matrix& matrix);
        static void Transform(const Vector2& position, const Matrix& matrix, Vector2& result);
        static void Transform(const std::vector<Vector2>& sourceArray, const Matrix& matrix, std::vector<Vector2>& destinationArray);
        static void Transform(const std::vector<Vector2>& sourceArray, int sourceIndex, const Matrix& matrix, std::vector<Vector2>& destinationArray, int destinationIndex, int length);

        /// Transforms a vector by a quaternion rotation.
        [[nodiscard]] static Vector2 Transform(Vector2 value, const Quaternion& rotation);
        static void Transform(const Vector2& value, const Quaternion& rotation, Vector2& result);
        static void Transform(const std::vector<Vector2>& sourceArray, const Quaternion& rotation, std::vector<Vector2>& destinationArray);
        static void Transform(const std::vector<Vector2>& sourceArray, int sourceIndex, const Quaternion& rotation, std::vector<Vector2>& destinationArray, int destinationIndex, int length);

        /// Transforms a normal by a matrix without applying translation.
        [[nodiscard]] static Vector2 TransformNormal(Vector2 normal, const Matrix& matrix);
        static void TransformNormal(const Vector2& normal, const Matrix& matrix, Vector2& result);
        static void TransformNormal(const std::vector<Vector2>& sourceArray, const Matrix& matrix, std::vector<Vector2>& destinationArray);
        static void TransformNormal(const std::vector<Vector2>& sourceArray, int sourceIndex, const Matrix& matrix, std::vector<Vector2>& destinationArray, int destinationIndex, int length);

        friend Vector2 operator-(Vector2 value);
        friend bool operator==(Vector2 value1, Vector2 value2);
        friend bool operator!=(Vector2 value1, Vector2 value2);
        friend Vector2 operator+(Vector2 value1, Vector2 value2);
        friend Vector2 operator-(Vector2 value1, Vector2 value2);
        friend Vector2 operator*(Vector2 value1, Vector2 value2);
        friend Vector2 operator*(Vector2 value, float scaleFactor);
        friend Vector2 operator*(float scaleFactor, Vector2 value);
        friend Vector2 operator/(Vector2 value1, Vector2 value2);
        friend Vector2 operator/(Vector2 value1, float divider);

    private:
        [[nodiscard]] std::string getDebugDisplayStringProperty() const;
        void CheckForNaNs() const;
    };
}
