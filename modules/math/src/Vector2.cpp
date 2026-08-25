// SPDX-License-Identifier: MS-PL

#include "Microsoft/Xna/Framework/Vector2.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <stdexcept>

#include "Microsoft/Xna/Framework/Matrix.hpp"
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

        float CatmullRomScalar(float value1, float value2, float value3, float value4, float amount)
        {
            const float amountSquared = amount * amount;
            const float amountCubed = amountSquared * amount;
            return 0.5f * (
                (2.0f * value2) +
                ((-value1 + value3) * amount) +
                (((2.0f * value1) - (5.0f * value2) + (4.0f * value3) - value4) * amountSquared) +
                ((-value1 + (3.0f * value2) - (3.0f * value3) + value4) * amountCubed)
            );
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

    constinit const Vector2 Vector2::Zero(0.0f, 0.0f);
    constinit const Vector2 Vector2::One(1.0f, 1.0f);
    constinit const Vector2 Vector2::UnitX(1.0f, 0.0f);
    constinit const Vector2 Vector2::UnitY(0.0f, 1.0f);

    Vector2::Vector2() : X(0.0f), Y(0.0f)
    {
    }

    Vector2::Vector2(float value) : X(value), Y(value)
    {
    }

    bool Vector2::Equals(const Vector2& other) const { return X == other.X && Y == other.Y; }
    int Vector2::GetHashCode() const
    {
        // Unsigned wraparound avoids signed-overflow UB (UBSan, INPUT-BUILD-006); result unchanged.
        return static_cast<int>(static_cast<unsigned>(FloatHash(X)) + static_cast<unsigned>(FloatHash(Y)));
    }
    float Vector2::Length() const { return std::sqrt((X * X) + (Y * Y)); }
    float Vector2::LengthSquared() const { return (X * X) + (Y * Y); }

    void Vector2::Normalize()
    {
        const float val = 1.0f / std::sqrt((X * X) + (Y * Y));
        X *= val;
        Y *= val;
    }

    std::string Vector2::ToString() const
    {
        std::ostringstream s;
        s << "{X:" << X << " Y:" << Y << "}";
        return s.str();
    }

    std::string Vector2::getDebugDisplayStringProperty() const
    {
        std::ostringstream s;
        s << X << " " << Y;
        return s.str();
    }

    void Vector2::CheckForNaNs() const
    {
        if (std::isnan(X) || std::isnan(Y)) throw std::logic_error("Vector2 contains NaNs!");
    }

    Vector2 Vector2::Add(Vector2 value1, Vector2 value2)
    {
        value1.X += value2.X;
        value1.Y += value2.Y;
        return value1;
    }

    void Vector2::Add(const Vector2& value1, const Vector2& value2, Vector2& result)
    {
        result.X = value1.X + value2.X;
        result.Y = value1.Y + value2.Y;
    }

    Vector2 Vector2::Barycentric(Vector2 value1, Vector2 value2, Vector2 value3, float amount1, float amount2)
    {
        return Vector2(BarycentricScalar(value1.X, value2.X, value3.X, amount1, amount2),
                       BarycentricScalar(value1.Y, value2.Y, value3.Y, amount1, amount2));
    }

    void Vector2::Barycentric(const Vector2& value1, const Vector2& value2, const Vector2& value3, float amount1,
                              float amount2, Vector2& result)
    {
        result = Barycentric(value1, value2, value3, amount1, amount2);
    }

    Vector2 Vector2::CatmullRom(Vector2 value1, Vector2 value2, Vector2 value3, Vector2 value4, float amount)
    {
        return Vector2(CatmullRomScalar(value1.X, value2.X, value3.X, value4.X, amount),
                       CatmullRomScalar(value1.Y, value2.Y, value3.Y, value4.Y, amount));
    }

    void Vector2::CatmullRom(const Vector2& value1, const Vector2& value2, const Vector2& value3, const Vector2& value4,
                             float amount, Vector2& result)
    {
        result = CatmullRom(value1, value2, value3, value4, amount);
    }

    Vector2 Vector2::Clamp(Vector2 value1, Vector2 min, Vector2 max)
    {
        return Vector2(ClampScalar(value1.X, min.X, max.X), ClampScalar(value1.Y, min.Y, max.Y));
    }

    void Vector2::Clamp(const Vector2& value1, const Vector2& min, const Vector2& max, Vector2& result)
    {
        result = Clamp(value1, min, max);
    }

    float Vector2::Distance(Vector2 value1, Vector2 value2)
    {
        const float x = value1.X - value2.X;
        const float y = value1.Y - value2.Y;
        return std::sqrt((x * x) + (y * y));
    }

    void Vector2::Distance(const Vector2& value1, const Vector2& value2, float& result)
    {
        result = Distance(value1, value2);
    }

    float Vector2::DistanceSquared(Vector2 value1, Vector2 value2)
    {
        const float x = value1.X - value2.X;
        const float y = value1.Y - value2.Y;
        return (x * x) + (y * y);
    }

    void Vector2::DistanceSquared(const Vector2& value1, const Vector2& value2, float& result)
    {
        result = DistanceSquared(value1, value2);
    }

    Vector2 Vector2::Divide(Vector2 value1, Vector2 value2)
    {
        value1.X /= value2.X;
        value1.Y /= value2.Y;
        return value1;
    }

    void Vector2::Divide(const Vector2& value1, const Vector2& value2, Vector2& result)
    {
        result.X = value1.X / value2.X;
        result.Y = value1.Y / value2.Y;
    }

    Vector2 Vector2::Divide(Vector2 value1, float divider)
    {
        value1.X /= divider;
        value1.Y /= divider;
        return value1;
    }

    void Vector2::Divide(const Vector2& value1, float divider, Vector2& result)
    {
        result.X = value1.X / divider;
        result.Y = value1.Y / divider;
    }

    float Vector2::Dot(Vector2 value1, Vector2 value2) { return (value1.X * value2.X) + (value1.Y * value2.Y); }
    void Vector2::Dot(const Vector2& value1, const Vector2& value2, float& result) { result = Dot(value1, value2); }

    Vector2 Vector2::Hermite(Vector2 value1, Vector2 tangent1, Vector2 value2, Vector2 tangent2, float amount)
    {
        return Vector2(HermiteScalar(value1.X, tangent1.X, value2.X, tangent2.X, amount),
                       HermiteScalar(value1.Y, tangent1.Y, value2.Y, tangent2.Y, amount));
    }

    void Vector2::Hermite(const Vector2& value1, const Vector2& tangent1, const Vector2& value2,
                          const Vector2& tangent2, float amount, Vector2& result)
    {
        result = Hermite(value1, tangent1, value2, tangent2, amount);
    }

    Vector2 Vector2::Lerp(Vector2 value1, Vector2 value2, float amount)
    {
        return Vector2(LerpScalar(value1.X, value2.X, amount), LerpScalar(value1.Y, value2.Y, amount));
    }

    void Vector2::Lerp(const Vector2& value1, const Vector2& value2, float amount, Vector2& result)
    {
        result = Lerp(value1, value2, amount);
    }

    Vector2 Vector2::Max(Vector2 value1, Vector2 value2)
    {
        return Vector2(std::max(value1.X, value2.X), std::max(value1.Y, value2.Y));
    }

    void Vector2::Max(const Vector2& value1, const Vector2& value2, Vector2& result) { result = Max(value1, value2); }

    Vector2 Vector2::Min(Vector2 value1, Vector2 value2)
    {
        return Vector2(std::min(value1.X, value2.X), std::min(value1.Y, value2.Y));
    }

    void Vector2::Min(const Vector2& value1, const Vector2& value2, Vector2& result) { result = Min(value1, value2); }

    Vector2 Vector2::Multiply(Vector2 value1, Vector2 value2)
    {
        value1.X *= value2.X;
        value1.Y *= value2.Y;
        return value1;
    }

    void Vector2::Multiply(const Vector2& value1, const Vector2& value2, Vector2& result)
    {
        result.X = value1.X * value2.X;
        result.Y = value1.Y * value2.Y;
    }

    Vector2 Vector2::Multiply(Vector2 value1, float scaleFactor)
    {
        value1.X *= scaleFactor;
        value1.Y *= scaleFactor;
        return value1;
    }

    void Vector2::Multiply(const Vector2& value1, float scaleFactor, Vector2& result)
    {
        result.X = value1.X * scaleFactor;
        result.Y = value1.Y * scaleFactor;
    }

    Vector2 Vector2::Negate(Vector2 value) { return Vector2(-value.X, -value.Y); }
    void Vector2::Negate(const Vector2& value, Vector2& result) { result = Negate(value); }

    Vector2 Vector2::Normalize(Vector2 value)
    {
        value.Normalize();
        return value;
    }

    void Vector2::Normalize(const Vector2& value, Vector2& result) { result = Normalize(value); }

    Vector2 Vector2::Reflect(Vector2 vector, Vector2 normal)
    {
        const float val = 2.0f * Dot(vector, normal);
        return Vector2(vector.X - (normal.X * val), vector.Y - (normal.Y * val));
    }

    void Vector2::Reflect(const Vector2& vector, const Vector2& normal, Vector2& result)
    {
        result = Reflect(vector, normal);
    }

    Vector2 Vector2::SmoothStep(Vector2 value1, Vector2 value2, float amount)
    {
        return Vector2(SmoothStepScalar(value1.X, value2.X, amount), SmoothStepScalar(value1.Y, value2.Y, amount));
    }

    void Vector2::SmoothStep(const Vector2& value1, const Vector2& value2, float amount, Vector2& result)
    {
        result = SmoothStep(value1, value2, amount);
    }

    Vector2 Vector2::Subtract(Vector2 value1, Vector2 value2)
    {
        value1.X -= value2.X;
        value1.Y -= value2.Y;
        return value1;
    }

    void Vector2::Subtract(const Vector2& value1, const Vector2& value2, Vector2& result)
    {
        result.X = value1.X - value2.X;
        result.Y = value1.Y - value2.Y;
    }

    Vector2 Vector2::Transform(Vector2 position, const Matrix& matrix)
    {
        Vector2 result;
        Transform(position, matrix, result);
        return result;
    }

    void Vector2::Transform(const Vector2& position, const Matrix& matrix, Vector2& result)
    {
        const float x = (position.X * matrix.M11) + (position.Y * matrix.M21) + matrix.M41;
        const float y = (position.X * matrix.M12) + (position.Y * matrix.M22) + matrix.M42;
        result.X = x;
        result.Y = y;
    }

    void Vector2::Transform(const std::vector<Vector2>& sourceArray, const Matrix& matrix,
                            std::vector<Vector2>& destinationArray)
    {
        Transform(sourceArray, 0, matrix, destinationArray, 0, static_cast<int>(sourceArray.size()));
    }

    void Vector2::Transform(const std::vector<Vector2>& sourceArray, int sourceIndex, const Matrix& matrix,
                            std::vector<Vector2>& destinationArray, int destinationIndex, int length)
    {
        CheckArrayRange(sourceArray.size(), sourceIndex, destinationArray.size(), destinationIndex, length);
        for (int i = 0; i < length; ++i) Transform(sourceArray[sourceIndex + i], matrix,
                                                   destinationArray[destinationIndex + i]);
    }

    Vector2 Vector2::Transform(Vector2 value, const Quaternion& rotation)
    {
        Vector2 result;
        Transform(value, rotation, result);
        return result;
    }

    void Vector2::Transform(const Vector2& value, const Quaternion& rotation, Vector2& result)
    {
        const float x = 2.0f * -(rotation.Z * value.Y);
        const float y = 2.0f * (rotation.Z * value.X);
        const float z = 2.0f * ((rotation.X * value.Y) - (rotation.Y * value.X));
        result.X = value.X + (x * rotation.W) + ((rotation.Y * z) - (rotation.Z * y));
        result.Y = value.Y + (y * rotation.W) + ((rotation.Z * x) - (rotation.X * z));
    }

    void Vector2::Transform(const std::vector<Vector2>& sourceArray, const Quaternion& rotation,
                            std::vector<Vector2>& destinationArray)
    {
        Transform(sourceArray, 0, rotation, destinationArray, 0, static_cast<int>(sourceArray.size()));
    }

    void Vector2::Transform(const std::vector<Vector2>& sourceArray, int sourceIndex, const Quaternion& rotation,
                            std::vector<Vector2>& destinationArray, int destinationIndex, int length)
    {
        CheckArrayRange(sourceArray.size(), sourceIndex, destinationArray.size(), destinationIndex, length);
        for (int i = 0; i < length; ++i) Transform(sourceArray[sourceIndex + i], rotation,
                                                   destinationArray[destinationIndex + i]);
    }

    Vector2 Vector2::TransformNormal(Vector2 normal, const Matrix& matrix)
    {
        return Vector2((normal.X * matrix.M11) + (normal.Y * matrix.M21),
                       (normal.X * matrix.M12) + (normal.Y * matrix.M22));
    }

    void Vector2::TransformNormal(const Vector2& normal, const Matrix& matrix, Vector2& result)
    {
        result = TransformNormal(normal, matrix);
    }

    void Vector2::TransformNormal(const std::vector<Vector2>& sourceArray, const Matrix& matrix,
                                  std::vector<Vector2>& destinationArray)
    {
        TransformNormal(sourceArray, 0, matrix, destinationArray, 0, static_cast<int>(sourceArray.size()));
    }

    void Vector2::TransformNormal(const std::vector<Vector2>& sourceArray, int sourceIndex, const Matrix& matrix,
                                  std::vector<Vector2>& destinationArray, int destinationIndex, int length)
    {
        CheckArrayRange(sourceArray.size(), sourceIndex, destinationArray.size(), destinationIndex, length);
        for (int i = 0; i < length; ++i) TransformNormal(sourceArray[sourceIndex + i], matrix,
                                                         destinationArray[destinationIndex + i]);
    }

    Vector2& Vector2::operator+=(const Vector2& value)
    {
        X += value.X;
        Y += value.Y;
        return *this;
    }

    Vector2& Vector2::operator-=(const Vector2& value)
    {
        X -= value.X;
        Y -= value.Y;
        return *this;
    }

    Vector2& Vector2::operator*=(const Vector2& value)
    {
        X *= value.X;
        Y *= value.Y;
        return *this;
    }

    Vector2& Vector2::operator*=(const float scaleFactor)
    {
        X *= scaleFactor;
        Y *= scaleFactor;
        return *this;
    }

    Vector2& Vector2::operator/=(const Vector2& value)
    {
        X /= value.X;
        Y /= value.Y;
        return *this;
    }

    Vector2& Vector2::operator/=(const float divider)
    {
        X /= divider;
        Y /= divider;
        return *this;
    }

    Vector2 operator-(Vector2 value) { return Vector2::Negate(value); }
    bool operator==(Vector2 value1, Vector2 value2) { return value1.Equals(value2); }
    bool operator!=(Vector2 value1, Vector2 value2) { return !value1.Equals(value2); }
    Vector2 operator+(Vector2 value1, Vector2 value2) { return Vector2::Add(value1, value2); }
    Vector2 operator-(Vector2 value1, Vector2 value2) { return Vector2::Subtract(value1, value2); }
    Vector2 operator*(Vector2 value1, Vector2 value2) { return Vector2::Multiply(value1, value2); }
    Vector2 operator*(Vector2 value, float scaleFactor) { return Vector2::Multiply(value, scaleFactor); }
    Vector2 operator*(float scaleFactor, Vector2 value) { return Vector2::Multiply(value, scaleFactor); }
    Vector2 operator/(Vector2 value1, Vector2 value2) { return Vector2::Divide(value1, value2); }
    Vector2 operator/(Vector2 value1, float divider) { return Vector2::Divide(value1, divider); }
}
