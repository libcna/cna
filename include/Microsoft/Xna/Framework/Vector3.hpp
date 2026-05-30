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
        static const Vector3 Zero;
        static const Vector3 One;
        static const Vector3 UnitX;
        static const Vector3 UnitY;
        static const Vector3 UnitZ;
        static const Vector3 Up;
        static const Vector3 Down;
        static const Vector3 Right;
        static const Vector3 Left;
        static const Vector3 Forward;
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

        [[nodiscard]] bool Equals(const Vector3& other) const;
        [[nodiscard]] int GetHashCode() const;
        [[nodiscard]] float Length() const;
        [[nodiscard]] float LengthSquared() const;
        void Normalize();
        [[nodiscard]] std::string ToString() const;
        NOXNA Vector3& operator+=(const Vector3& vector3);
        NOXNA Vector3& operator-=(const Vector3& vector3);

        [[nodiscard]] static Vector3 Add(Vector3 value1, Vector3 value2);
        static void Add(const Vector3& value1, const Vector3& value2, Vector3& result);
        [[nodiscard]] static Vector3 Barycentric(Vector3 value1, Vector3 value2, Vector3 value3, float amount1,
                                                 float amount2);
        static void Barycentric(const Vector3& value1, const Vector3& value2, const Vector3& value3, float amount1,
                                float amount2, Vector3& result);
        [[nodiscard]] static Vector3 CatmullRom(Vector3 value1, Vector3 value2, Vector3 value3, Vector3 value4,
                                                float amount);
        static void CatmullRom(const Vector3& value1, const Vector3& value2, const Vector3& value3,
                               const Vector3& value4, float amount, Vector3& result);
        [[nodiscard]] static Vector3 Clamp(Vector3 value1, Vector3 min, Vector3 max);
        static void Clamp(const Vector3& value1, const Vector3& min, const Vector3& max, Vector3& result);
        [[nodiscard]] static Vector3 Cross(Vector3 vector1, Vector3 vector2);
        static void Cross(const Vector3& vector1, const Vector3& vector2, Vector3& result);
        [[nodiscard]] static float Distance(Vector3 value1, Vector3 value2);
        static void Distance(const Vector3& value1, const Vector3& value2, float& result);
        [[nodiscard]] static float DistanceSquared(Vector3 value1, Vector3 value2);
        static void DistanceSquared(const Vector3& value1, const Vector3& value2, float& result);
        [[nodiscard]] static Vector3 Divide(Vector3 value1, Vector3 value2);
        static void Divide(const Vector3& value1, const Vector3& value2, Vector3& result);
        [[nodiscard]] static Vector3 Divide(Vector3 value1, float divider);
        static void Divide(const Vector3& value1, float divider, Vector3& result);
        [[nodiscard]] static float Dot(Vector3 value1, Vector3 value2);
        static void Dot(const Vector3& value1, const Vector3& value2, float& result);
        [[nodiscard]] static Vector3 Hermite(Vector3 value1, Vector3 tangent1, Vector3 value2, Vector3 tangent2,
                                             float amount);
        static void Hermite(const Vector3& value1, const Vector3& tangent1, const Vector3& value2,
                            const Vector3& tangent2, float amount, Vector3& result);
        [[nodiscard]] static Vector3 Lerp(Vector3 value1, Vector3 value2, float amount);
        static void Lerp(const Vector3& value1, const Vector3& value2, float amount, Vector3& result);
        [[nodiscard]] static Vector3 Max(Vector3 value1, Vector3 value2);
        static void Max(const Vector3& value1, const Vector3& value2, Vector3& result);
        [[nodiscard]] static Vector3 Min(Vector3 value1, Vector3 value2);
        static void Min(const Vector3& value1, const Vector3& value2, Vector3& result);
        [[nodiscard]] static Vector3 Multiply(Vector3 value1, Vector3 value2);
        static void Multiply(const Vector3& value1, const Vector3& value2, Vector3& result);
        [[nodiscard]] static Vector3 Multiply(Vector3 value1, float scaleFactor);
        static void Multiply(const Vector3& value1, float scaleFactor, Vector3& result);
        [[nodiscard]] static Vector3 Negate(Vector3 value);
        static void Negate(const Vector3& value, Vector3& result);
        [[nodiscard]] static Vector3 Normalize(Vector3 value);
        static void Normalize(const Vector3& value, Vector3& result);
        [[nodiscard]] static Vector3 Reflect(Vector3 vector, Vector3 normal);
        static void Reflect(const Vector3& vector, const Vector3& normal, Vector3& result);
        [[nodiscard]] static Vector3 SmoothStep(Vector3 value1, Vector3 value2, float amount);
        static void SmoothStep(const Vector3& value1, const Vector3& value2, float amount, Vector3& result);
        [[nodiscard]] static Vector3 Subtract(Vector3 value1, Vector3 value2);
        static void Subtract(const Vector3& value1, const Vector3& value2, Vector3& result);
        [[nodiscard]] static Vector3 Transform(Vector3 position, const Matrix& matrix);
        static void Transform(const Vector3& position, const Matrix& matrix, Vector3& result);
        static void Transform(const std::vector<Vector3>& sourceArray, const Matrix& matrix,
                              std::vector<Vector3>& destinationArray);
        static void Transform(const std::vector<Vector3>& sourceArray, int sourceIndex, const Matrix& matrix,
                              std::vector<Vector3>& destinationArray, int destinationIndex, int length);
        [[nodiscard]] static Vector3 Transform(Vector3 value, const Quaternion& rotation);
        static void Transform(const Vector3& value, const Quaternion& rotation, Vector3& result);
        static void Transform(const std::vector<Vector3>& sourceArray, const Quaternion& rotation,
                              std::vector<Vector3>& destinationArray);
        static void Transform(const std::vector<Vector3>& sourceArray, int sourceIndex, const Quaternion& rotation,
                              std::vector<Vector3>& destinationArray, int destinationIndex, int length);
        [[nodiscard]] static Vector3 TransformNormal(Vector3 normal, const Matrix& matrix);
        static void TransformNormal(const Vector3& normal, const Matrix& matrix, Vector3& result);
        static void TransformNormal(const std::vector<Vector3>& sourceArray, const Matrix& matrix,
                                    std::vector<Vector3>& destinationArray);
        static void TransformNormal(const std::vector<Vector3>& sourceArray, int sourceIndex, const Matrix& matrix,
                                    std::vector<Vector3>& destinationArray, int destinationIndex, int length);

        friend bool operator==(Vector3 value1, Vector3 value2);
        friend bool operator!=(Vector3 value1, Vector3 value2);
        friend Vector3 operator+(Vector3 value1, Vector3 value2);
        friend Vector3 operator-(Vector3 value);
        friend Vector3 operator-(Vector3 value1, Vector3 value2);
        friend Vector3 operator*(Vector3 value1, Vector3 value2);
        friend Vector3 operator*(Vector3 value, float scaleFactor);
        friend Vector3 operator*(float scaleFactor, Vector3 value);
        friend Vector3 operator/(Vector3 value1, Vector3 value2);
        friend Vector3 operator/(Vector3 value1, float divider);

    private:
        [[nodiscard]] std::string getDebugDisplayStringProperty() const;
        void CheckForNaNs() const;
    };
}
