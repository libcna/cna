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
        static const Vector4 Zero;
        static const Vector4 One;
        static const Vector4 UnitX;
        static const Vector4 UnitY;
        static const Vector4 UnitZ;
        static const Vector4 UnitW;

        float X;
        float Y;
        float Z;
        float W;

        Vector4();
        Vector4(float x, float y, float z, float w);
        Vector4(Vector2 value, float z, float w);
        Vector4(Vector3 value, float w);
        explicit Vector4(float value);

        [[nodiscard]] bool Equals(const Vector4& other) const;
        [[nodiscard]] int GetHashCode() const;
        [[nodiscard]] float Length() const;
        [[nodiscard]] float LengthSquared() const;
        void Normalize();
        [[nodiscard]] std::string ToString() const;

        [[nodiscard]] static Vector4 Add(Vector4 value1, Vector4 value2);
        static void Add(const Vector4& value1, const Vector4& value2, Vector4& result);
        [[nodiscard]] static Vector4 Barycentric(Vector4 value1, Vector4 value2, Vector4 value3, float amount1,
                                                 float amount2);
        static void Barycentric(const Vector4& value1, const Vector4& value2, const Vector4& value3, float amount1,
                                float amount2, Vector4& result);
        [[nodiscard]] static Vector4 CatmullRom(Vector4 value1, Vector4 value2, Vector4 value3, Vector4 value4,
                                                float amount);
        static void CatmullRom(const Vector4& value1, const Vector4& value2, const Vector4& value3,
                               const Vector4& value4, float amount, Vector4& result);
        [[nodiscard]] static Vector4 Clamp(Vector4 value1, Vector4 min, Vector4 max);
        static void Clamp(const Vector4& value1, const Vector4& min, const Vector4& max, Vector4& result);
        [[nodiscard]] static float Distance(Vector4 value1, Vector4 value2);
        static void Distance(const Vector4& value1, const Vector4& value2, float& result);
        [[nodiscard]] static float DistanceSquared(Vector4 value1, Vector4 value2);
        static void DistanceSquared(const Vector4& value1, const Vector4& value2, float& result);
        [[nodiscard]] static Vector4 Divide(Vector4 value1, Vector4 value2);
        static void Divide(const Vector4& value1, const Vector4& value2, Vector4& result);
        [[nodiscard]] static Vector4 Divide(Vector4 value1, float divider);
        static void Divide(const Vector4& value1, float divider, Vector4& result);
        [[nodiscard]] static float Dot(Vector4 value1, Vector4 value2);
        static void Dot(const Vector4& value1, const Vector4& value2, float& result);
        [[nodiscard]] static Vector4 Hermite(Vector4 value1, Vector4 tangent1, Vector4 value2, Vector4 tangent2,
                                             float amount);
        static void Hermite(const Vector4& value1, const Vector4& tangent1, const Vector4& value2,
                            const Vector4& tangent2, float amount, Vector4& result);
        [[nodiscard]] static Vector4 Lerp(Vector4 value1, Vector4 value2, float amount);
        static void Lerp(const Vector4& value1, const Vector4& value2, float amount, Vector4& result);
        [[nodiscard]] static Vector4 Max(Vector4 value1, Vector4 value2);
        static void Max(const Vector4& value1, const Vector4& value2, Vector4& result);
        [[nodiscard]] static Vector4 Min(Vector4 value1, Vector4 value2);
        static void Min(const Vector4& value1, const Vector4& value2, Vector4& result);
        [[nodiscard]] static Vector4 Multiply(Vector4 value1, Vector4 value2);
        static void Multiply(const Vector4& value1, const Vector4& value2, Vector4& result);
        [[nodiscard]] static Vector4 Multiply(Vector4 value1, float scaleFactor);
        static void Multiply(const Vector4& value1, float scaleFactor, Vector4& result);
        [[nodiscard]] static Vector4 Negate(Vector4 value);
        static void Negate(const Vector4& value, Vector4& result);
        [[nodiscard]] static Vector4 Normalize(Vector4 value);
        static void Normalize(const Vector4& value, Vector4& result);
        [[nodiscard]] static Vector4 SmoothStep(Vector4 value1, Vector4 value2, float amount);
        static void SmoothStep(const Vector4& value1, const Vector4& value2, float amount, Vector4& result);
        [[nodiscard]] static Vector4 Subtract(Vector4 value1, Vector4 value2);
        static void Subtract(const Vector4& value1, const Vector4& value2, Vector4& result);

        [[nodiscard]] static Vector4 Transform(Vector2 position, const Matrix& matrix);
        [[nodiscard]] static Vector4 Transform(Vector3 position, const Matrix& matrix);
        [[nodiscard]] static Vector4 Transform(Vector4 vector, const Matrix& matrix);
        static void Transform(const Vector2& position, const Matrix& matrix, Vector4& result);
        static void Transform(const Vector3& position, const Matrix& matrix, Vector4& result);
        static void Transform(const Vector4& vector, const Matrix& matrix, Vector4& result);
        static void Transform(const std::vector<Vector4>& sourceArray, const Matrix& matrix,
                              std::vector<Vector4>& destinationArray);
        static void Transform(const std::vector<Vector4>& sourceArray, int sourceIndex, const Matrix& matrix,
                              std::vector<Vector4>& destinationArray, int destinationIndex, int length);

        [[nodiscard]] static Vector4 Transform(Vector2 value, const Quaternion& rotation);
        [[nodiscard]] static Vector4 Transform(Vector3 value, const Quaternion& rotation);
        [[nodiscard]] static Vector4 Transform(Vector4 value, const Quaternion& rotation);
        static void Transform(const Vector2& value, const Quaternion& rotation, Vector4& result);
        static void Transform(const Vector3& value, const Quaternion& rotation, Vector4& result);
        static void Transform(const Vector4& value, const Quaternion& rotation, Vector4& result);
        static void Transform(const std::vector<Vector4>& sourceArray, const Quaternion& rotation,
                              std::vector<Vector4>& destinationArray);
        static void Transform(const std::vector<Vector4>& sourceArray, int sourceIndex, const Quaternion& rotation,
                              std::vector<Vector4>& destinationArray, int destinationIndex, int length);

        friend Vector4 operator-(Vector4 value);
        friend bool operator==(Vector4 value1, Vector4 value2);
        friend bool operator!=(Vector4 value1, Vector4 value2);
        friend Vector4 operator+(Vector4 value1, Vector4 value2);
        friend Vector4 operator-(Vector4 value1, Vector4 value2);
        friend Vector4 operator*(Vector4 value1, Vector4 value2);
        friend Vector4 operator*(Vector4 value1, float scaleFactor);
        friend Vector4 operator*(float scaleFactor, Vector4 value1);
        friend Vector4 operator/(Vector4 value1, Vector4 value2);
        friend Vector4 operator/(Vector4 value1, float divider);

    private:
        [[nodiscard]] std::string getDebugDisplayStringProperty() const;
        void CheckForNaNs() const;
    };
}
