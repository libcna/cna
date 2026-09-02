// SPDX-License-Identifier: MS-PL

#include "Microsoft/Xna/Framework/Vector3.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <stdexcept>

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"

namespace Microsoft::Xna::Framework
{
    namespace
    {
        int FloatHash(float value)
        {
            std::uint32_t bits = 0;
            static_assert(sizeof(bits) == sizeof(value));
            std::memcpy(&bits, &value, sizeof(value));
            return static_cast<int>(bits);
        }

        float ClampScalar(float value, float minValue, float maxValue)
        {
            return std::min(std::max(value, minValue), maxValue);
        }

        float LerpScalar(float value1, float value2, float amount)
        {
            return value1 + ((value2 - value1) * amount);
        }

        float BarycentricScalar(float value1, float value2, float value3, float amount1, float amount2)
        {
            return value1 + ((value2 - value1) * amount1) + ((value3 - value1) * amount2);
        }

        float HermiteScalar(float value1, float tangent1, float value2, float tangent2, float amount)
        {
            const float amountSquared = amount * amount;
            const float amountCubed = amountSquared * amount;
            const float h1 = (2.0f * amountCubed) - (3.0f * amountSquared) + 1.0f;
            const float h2 = (-2.0f * amountCubed) + (3.0f * amountSquared);
            const float h3 = amountCubed - (2.0f * amountSquared) + amount;
            const float h4 = amountCubed - amountSquared;
            return (value1 * h1) + (value2 * h2) + (tangent1 * h3) + (tangent2 * h4);
        }

        float SmoothStepScalar(float value1, float value2, float amount)
        {
            amount = ClampScalar(amount, 0.0f, 1.0f);
            return HermiteScalar(value1, 0.0f, value2, 0.0f, amount);
        }

        void CheckArrayRange(std::size_t sourceSize, int sourceIndex, std::size_t destinationSize, int destinationIndex,
                             int length)
        {
            if (sourceIndex < 0 || destinationIndex < 0 || length < 0)
            {
                throw std::out_of_range("array index and length values must be non-negative");
            }
            if (static_cast<std::size_t>(sourceIndex + length) > sourceSize)
            {
                throw std::out_of_range("source range exceeds source array size");
            }
            if (static_cast<std::size_t>(destinationIndex + length) > destinationSize)
            {
                throw std::out_of_range("destination range exceeds destination array size");
            }
        }
    }

    const Vector3 Vector3::Zero(0.0f, 0.0f, 0.0f);
    const Vector3 Vector3::One(1.0f, 1.0f, 1.0f);
    const Vector3 Vector3::UnitX(1.0f, 0.0f, 0.0f);
    const Vector3 Vector3::UnitY(0.0f, 1.0f, 0.0f);
    const Vector3 Vector3::UnitZ(0.0f, 0.0f, 1.0f);
    const Vector3 Vector3::Up(0.0f, 1.0f, 0.0f);
    const Vector3 Vector3::Down(0.0f, -1.0f, 0.0f);
    const Vector3 Vector3::Right(1.0f, 0.0f, 0.0f);
    const Vector3 Vector3::Left(-1.0f, 0.0f, 0.0f);
    const Vector3 Vector3::Forward(0.0f, 0.0f, -1.0f);
    const Vector3 Vector3::Backward(0.0f, 0.0f, 1.0f);

    Vector3::Vector3() : X(0.0f), Y(0.0f), Z(0.0f)
    {
    }

    Vector3::Vector3(float x, float y, float z) : X(x), Y(y), Z(z)
    {
    }

    Vector3::Vector3(float value) : X(value), Y(value), Z(value)
    {
    }

    Vector3::Vector3(Vector2 value, float z) : X(value.X), Y(value.Y), Z(z)
    {
    }

    bool Vector3::Equals(const Vector3& other) const { return X == other.X && Y == other.Y && Z == other.Z; }
    int Vector3::GetHashCode() const { return FloatHash(X) + FloatHash(Y) + FloatHash(Z); }
    float Vector3::Length() const { return std::sqrt((X * X) + (Y * Y) + (Z * Z)); }
    float Vector3::LengthSquared() const { return (X * X) + (Y * Y) + (Z * Z); }

    void Vector3::Normalize()
    {
        const float factor = 1.0f / std::sqrt((X * X) + (Y * Y) + (Z * Z));
        X *= factor;
        Y *= factor;
        Z *= factor;
    }

    std::string Vector3::ToString() const
    {
        std::ostringstream s;
        s << "{X:" << X << " Y:" << Y << " Z:" << Z << "}";
        return s.str();
    }

    Vector3& Vector3::operator+=(const Vector3& vector3)
    {
        X += vector3.X;
        Y += vector3.Y;
        Z += vector3.Z;
        return *this;
    }

    Vector3& Vector3::operator-=(const Vector3& vector3)
    {
        X -= vector3.X;
        Y -= vector3.Y;
        Z -= vector3.Z;
        return *this;
    }

    Vector3& Vector3::operator*=(const Vector3& vector3)
    {
        X *= vector3.X;
        Y *= vector3.Y;
        Z *= vector3.Z;
        return *this;
    }

    Vector3& Vector3::operator*=(const float scaleFactor)
    {
        X *= scaleFactor;
        Y *= scaleFactor;
        Z *= scaleFactor;
        return *this;
    }

    Vector3& Vector3::operator/=(const Vector3& vector3)
    {
        X /= vector3.X;
        Y /= vector3.Y;
        Z /= vector3.Z;
        return *this;
    }

    Vector3& Vector3::operator/=(const float divider)
    {
        // Matching Vector3::Divide's own reciprocal form, so `v /= s` and `Divide(v, s)` agree
        // bit for bit rather than differing by one rounding.
        const float factor = 1.0f / divider;
        X *= factor;
        Y *= factor;
        Z *= factor;
        return *this;
    }

    std::string Vector3::getDebugDisplayStringProperty() const
    {
        std::ostringstream s;
        s << X << " " << Y << " " << Z;
        return s.str();
    }

    void Vector3::CheckForNaNs() const
    {
        if (std::isnan(X) || std::isnan(Y) || std::isnan(Z)) throw std::logic_error("Vector3 contains NaNs!");
    }

    Vector3 Vector3::Add(Vector3 value1, Vector3 value2)
    {
        value1.X += value2.X;
        value1.Y += value2.Y;
        value1.Z += value2.Z;
        return value1;
    }

    void Vector3::Add(const Vector3& value1, const Vector3& value2, Vector3& result)
    {
        result.X = value1.X + value2.X;
        result.Y = value1.Y + value2.Y;
        result.Z = value1.Z + value2.Z;
    }

    Vector3 Vector3::Barycentric(Vector3 value1, Vector3 value2, Vector3 value3, float amount1, float amount2)
    {
        return Vector3(BarycentricScalar(value1.X, value2.X, value3.X, amount1, amount2),
                       BarycentricScalar(value1.Y, value2.Y, value3.Y, amount1, amount2),
                       BarycentricScalar(value1.Z, value2.Z, value3.Z, amount1, amount2));
    }

    void Vector3::Barycentric(const Vector3& value1, const Vector3& value2, const Vector3& value3, float amount1,
                              float amount2, Vector3& result)
    {
        result = Barycentric(value1, value2, value3, amount1, amount2);
    }

    Vector3 Vector3::CatmullRom(Vector3 value1, Vector3 value2, Vector3 value3, Vector3 value4, float amount)
    {
        return Vector3(MathHelper::CatmullRom(value1.X, value2.X, value3.X, value4.X, amount),
                       MathHelper::CatmullRom(value1.Y, value2.Y, value3.Y, value4.Y, amount),
                       MathHelper::CatmullRom(value1.Z, value2.Z, value3.Z, value4.Z, amount));
    }

    void Vector3::CatmullRom(const Vector3& value1, const Vector3& value2, const Vector3& value3, const Vector3& value4,
                             float amount, Vector3& result)
    {
        result = CatmullRom(value1, value2, value3, value4, amount);
    }

    Vector3 Vector3::Clamp(Vector3 value1, Vector3 min, Vector3 max)
    {
        return Vector3(ClampScalar(value1.X, min.X, max.X), ClampScalar(value1.Y, min.Y, max.Y),
                       ClampScalar(value1.Z, min.Z, max.Z));
    }

    void Vector3::Clamp(const Vector3& value1, const Vector3& min, const Vector3& max, Vector3& result)
    {
        result = Clamp(value1, min, max);
    }

    Vector3 Vector3::Cross(Vector3 vector1, Vector3 vector2)
    {
        Vector3 result;
        Cross(vector1, vector2, result);
        return result;
    }

    void Vector3::Cross(const Vector3& vector1, const Vector3& vector2, Vector3& result)
    {
        const float x = (vector1.Y * vector2.Z) - (vector2.Y * vector1.Z);
        const float y = -((vector1.X * vector2.Z) - (vector2.X * vector1.Z));
        const float z = (vector1.X * vector2.Y) - (vector2.X * vector1.Y);
        result.X = x;
        result.Y = y;
        result.Z = z;
    }

    float Vector3::Distance(Vector3 value1, Vector3 value2) { return std::sqrt(DistanceSquared(value1, value2)); }

    void Vector3::Distance(const Vector3& value1, const Vector3& value2, float& result)
    {
        result = Distance(value1, value2);
    }

    float Vector3::DistanceSquared(Vector3 value1, Vector3 value2)
    {
        return ((value1.X - value2.X) * (value1.X - value2.X)) + ((value1.Y - value2.Y) * (value1.Y - value2.Y)) + ((
            value1.Z - value2.Z) * (value1.Z - value2.Z));
    }

    void Vector3::DistanceSquared(const Vector3& value1, const Vector3& value2, float& result)
    {
        result = DistanceSquared(value1, value2);
    }

    Vector3 Vector3::Divide(Vector3 value1, Vector3 value2)
    {
        value1.X /= value2.X;
        value1.Y /= value2.Y;
        value1.Z /= value2.Z;
        return value1;
    }

    void Vector3::Divide(const Vector3& value1, const Vector3& value2, Vector3& result)
    {
        result.X = value1.X / value2.X;
        result.Y = value1.Y / value2.Y;
        result.Z = value1.Z / value2.Z;
    }

    Vector3 Vector3::Divide(Vector3 value1, float divider)
    {
        value1.X /= divider;
        value1.Y /= divider;
        value1.Z /= divider;
        return value1;
    }

    void Vector3::Divide(const Vector3& value1, float divider, Vector3& result)
    {
        result.X = value1.X / divider;
        result.Y = value1.Y / divider;
        result.Z = value1.Z / divider;
    }

    float Vector3::Dot(Vector3 value1, Vector3 value2)
    {
        return (value1.X * value2.X) + (value1.Y * value2.Y) + (value1.Z * value2.Z);
    }

    void Vector3::Dot(const Vector3& value1, const Vector3& value2, float& result) { result = Dot(value1, value2); }

    Vector3 Vector3::Hermite(Vector3 value1, Vector3 tangent1, Vector3 value2, Vector3 tangent2, float amount)
    {
        return Vector3(HermiteScalar(value1.X, tangent1.X, value2.X, tangent2.X, amount),
                       HermiteScalar(value1.Y, tangent1.Y, value2.Y, tangent2.Y, amount),
                       HermiteScalar(value1.Z, tangent1.Z, value2.Z, tangent2.Z, amount));
    }

    void Vector3::Hermite(const Vector3& value1, const Vector3& tangent1, const Vector3& value2,
                          const Vector3& tangent2, float amount, Vector3& result)
    {
        result = Hermite(value1, tangent1, value2, tangent2, amount);
    }

    Vector3 Vector3::Lerp(Vector3 value1, Vector3 value2, float amount)
    {
        return Vector3(LerpScalar(value1.X, value2.X, amount), LerpScalar(value1.Y, value2.Y, amount),
                       LerpScalar(value1.Z, value2.Z, amount));
    }

    void Vector3::Lerp(const Vector3& value1, const Vector3& value2, float amount, Vector3& result)
    {
        result = Lerp(value1, value2, amount);
    }

    Vector3 Vector3::Max(Vector3 value1, Vector3 value2)
    {
        return Vector3(std::max(value1.X, value2.X), std::max(value1.Y, value2.Y), std::max(value1.Z, value2.Z));
    }

    void Vector3::Max(const Vector3& value1, const Vector3& value2, Vector3& result) { result = Max(value1, value2); }

    Vector3 Vector3::Min(Vector3 value1, Vector3 value2)
    {
        return Vector3(std::min(value1.X, value2.X), std::min(value1.Y, value2.Y), std::min(value1.Z, value2.Z));
    }

    void Vector3::Min(const Vector3& value1, const Vector3& value2, Vector3& result) { result = Min(value1, value2); }

    Vector3 Vector3::Multiply(Vector3 value1, Vector3 value2)
    {
        value1.X *= value2.X;
        value1.Y *= value2.Y;
        value1.Z *= value2.Z;
        return value1;
    }

    void Vector3::Multiply(const Vector3& value1, const Vector3& value2, Vector3& result)
    {
        result.X = value1.X * value2.X;
        result.Y = value1.Y * value2.Y;
        result.Z = value1.Z * value2.Z;
    }

    Vector3 Vector3::Multiply(Vector3 value1, float scaleFactor)
    {
        value1.X *= scaleFactor;
        value1.Y *= scaleFactor;
        value1.Z *= scaleFactor;
        return value1;
    }

    void Vector3::Multiply(const Vector3& value1, float scaleFactor, Vector3& result)
    {
        result.X = value1.X * scaleFactor;
        result.Y = value1.Y * scaleFactor;
        result.Z = value1.Z * scaleFactor;
    }

    Vector3 Vector3::Negate(Vector3 value) { return Vector3(-value.X, -value.Y, -value.Z); }
    void Vector3::Negate(const Vector3& value, Vector3& result) { result = Negate(value); }

    Vector3 Vector3::Normalize(Vector3 value)
    {
        value.Normalize();
        return value;
    }

    void Vector3::Normalize(const Vector3& value, Vector3& result) { result = Normalize(value); }

    Vector3 Vector3::Reflect(Vector3 vector, Vector3 normal)
    {
        const float val = 2.0f * Dot(vector, normal);
        return Vector3(vector.X - (normal.X * val), vector.Y - (normal.Y * val), vector.Z - (normal.Z * val));
    }

    void Vector3::Reflect(const Vector3& vector, const Vector3& normal, Vector3& result)
    {
        result = Reflect(vector, normal);
    }

    Vector3 Vector3::SmoothStep(Vector3 value1, Vector3 value2, float amount)
    {
        return Vector3(SmoothStepScalar(value1.X, value2.X, amount), SmoothStepScalar(value1.Y, value2.Y, amount),
                       SmoothStepScalar(value1.Z, value2.Z, amount));
    }

    void Vector3::SmoothStep(const Vector3& value1, const Vector3& value2, float amount, Vector3& result)
    {
        result = SmoothStep(value1, value2, amount);
    }

    Vector3 Vector3::Subtract(Vector3 value1, Vector3 value2)
    {
        value1.X -= value2.X;
        value1.Y -= value2.Y;
        value1.Z -= value2.Z;
        return value1;
    }

    void Vector3::Subtract(const Vector3& value1, const Vector3& value2, Vector3& result)
    {
        result.X = value1.X - value2.X;
        result.Y = value1.Y - value2.Y;
        result.Z = value1.Z - value2.Z;
    }

    Vector3 Vector3::Transform(Vector3 position, const Matrix& matrix)
    {
        Vector3 result;
        Transform(position, matrix, result);
        return result;
    }

    void Vector3::Transform(const Vector3& position, const Matrix& matrix, Vector3& result)
    {
        const float x = (position.X * matrix.M11) + (position.Y * matrix.M21) + (position.Z * matrix.M31) + matrix.M41;
        const float y = (position.X * matrix.M12) + (position.Y * matrix.M22) + (position.Z * matrix.M32) + matrix.M42;
        const float z = (position.X * matrix.M13) + (position.Y * matrix.M23) + (position.Z * matrix.M33) + matrix.M43;
        result.X = x;
        result.Y = y;
        result.Z = z;
    }

    void Vector3::Transform(const std::vector<Vector3>& sourceArray, const Matrix& matrix,
                            std::vector<Vector3>& destinationArray)
    {
        Transform(sourceArray, 0, matrix, destinationArray, 0, static_cast<int>(sourceArray.size()));
    }

    void Vector3::Transform(const std::vector<Vector3>& sourceArray, int sourceIndex, const Matrix& matrix,
                            std::vector<Vector3>& destinationArray, int destinationIndex, int length)
    {
        CheckArrayRange(sourceArray.size(), sourceIndex, destinationArray.size(), destinationIndex, length);
        for (int i = 0; i < length; ++i) Transform(sourceArray[sourceIndex + i], matrix,
                                                   destinationArray[destinationIndex + i]);
    }

    Vector3 Vector3::Transform(Vector3 value, const Quaternion& rotation)
    {
        Vector3 result;
        Transform(value, rotation, result);
        return result;
    }

    void Vector3::Transform(const Vector3& value, const Quaternion& rotation, Vector3& result)
    {
        const float x = 2.0f * ((rotation.Y * value.Z) - (rotation.Z * value.Y));
        const float y = 2.0f * ((rotation.Z * value.X) - (rotation.X * value.Z));
        const float z = 2.0f * ((rotation.X * value.Y) - (rotation.Y * value.X));
        result.X = value.X + (x * rotation.W) + ((rotation.Y * z) - (rotation.Z * y));
        result.Y = value.Y + (y * rotation.W) + ((rotation.Z * x) - (rotation.X * z));
        result.Z = value.Z + (z * rotation.W) + ((rotation.X * y) - (rotation.Y * x));
    }

    void Vector3::Transform(const std::vector<Vector3>& sourceArray, const Quaternion& rotation,
                            std::vector<Vector3>& destinationArray)
    {
        Transform(sourceArray, 0, rotation, destinationArray, 0, static_cast<int>(sourceArray.size()));
    }

    void Vector3::Transform(const std::vector<Vector3>& sourceArray, int sourceIndex, const Quaternion& rotation,
                            std::vector<Vector3>& destinationArray, int destinationIndex, int length)
    {
        CheckArrayRange(sourceArray.size(), sourceIndex, destinationArray.size(), destinationIndex, length);
        for (int i = 0; i < length; ++i) Transform(sourceArray[sourceIndex + i], rotation,
                                                   destinationArray[destinationIndex + i]);
    }

    Vector3 Vector3::TransformNormal(Vector3 normal, const Matrix& matrix)
    {
        return Vector3((normal.X * matrix.M11) + (normal.Y * matrix.M21) + (normal.Z * matrix.M31),
                       (normal.X * matrix.M12) + (normal.Y * matrix.M22) + (normal.Z * matrix.M32),
                       (normal.X * matrix.M13) + (normal.Y * matrix.M23) + (normal.Z * matrix.M33));
    }

    void Vector3::TransformNormal(const Vector3& normal, const Matrix& matrix, Vector3& result)
    {
        result = TransformNormal(normal, matrix);
    }

    void Vector3::TransformNormal(const std::vector<Vector3>& sourceArray, const Matrix& matrix,
                                  std::vector<Vector3>& destinationArray)
    {
        TransformNormal(sourceArray, 0, matrix, destinationArray, 0, static_cast<int>(sourceArray.size()));
    }

    void Vector3::TransformNormal(const std::vector<Vector3>& sourceArray, int sourceIndex, const Matrix& matrix,
                                  std::vector<Vector3>& destinationArray, int destinationIndex, int length)
    {
        CheckArrayRange(sourceArray.size(), sourceIndex, destinationArray.size(), destinationIndex, length);
        for (int i = 0; i < length; ++i) TransformNormal(sourceArray[sourceIndex + i], matrix,
                                                         destinationArray[destinationIndex + i]);
    }

    bool operator==(Vector3 value1, Vector3 value2) { return value1.Equals(value2); }
    bool operator!=(Vector3 value1, Vector3 value2) { return !value1.Equals(value2); }
    Vector3 operator+(Vector3 value1, Vector3 value2) { return Vector3::Add(value1, value2); }
    Vector3 operator-(Vector3 value) { return Vector3::Negate(value); }
    Vector3 operator-(Vector3 value1, Vector3 value2) { return Vector3::Subtract(value1, value2); }
    Vector3 operator*(Vector3 value1, Vector3 value2) { return Vector3::Multiply(value1, value2); }
    Vector3 operator*(Vector3 value, float scaleFactor) { return Vector3::Multiply(value, scaleFactor); }
    Vector3 operator*(float scaleFactor, Vector3 value) { return Vector3::Multiply(value, scaleFactor); }
    Vector3 operator/(Vector3 value1, Vector3 value2) { return Vector3::Divide(value1, value2); }
    Vector3 operator/(Vector3 value1, float divider) { return Vector3::Divide(value1, divider); }
}
