// SPDX-License-Identifier: MS-PL

#include "Microsoft/Xna/Framework/Vector4.hpp"
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

    const Vector4 Vector4::Zero(0.0f, 0.0f, 0.0f, 0.0f);
    const Vector4 Vector4::One(1.0f, 1.0f, 1.0f, 1.0f);
    const Vector4 Vector4::UnitX(1.0f, 0.0f, 0.0f, 0.0f);
    const Vector4 Vector4::UnitY(0.0f, 1.0f, 0.0f, 0.0f);
    const Vector4 Vector4::UnitZ(0.0f, 0.0f, 1.0f, 0.0f);
    const Vector4 Vector4::UnitW(0.0f, 0.0f, 0.0f, 1.0f);

    Vector4::Vector4() : X(0.0f), Y(0.0f), Z(0.0f), W(0.0f)
    {
    }

    Vector4::Vector4(float x, float y, float z, float w) : X(x), Y(y), Z(z), W(w)
    {
    }

    Vector4::Vector4(Vector2 value, float z, float w) : X(value.X), Y(value.Y), Z(z), W(w)
    {
    }

    Vector4::Vector4(Vector3 value, float w) : X(value.X), Y(value.Y), Z(value.Z), W(w)
    {
    }

    Vector4::Vector4(float value) : X(value), Y(value), Z(value), W(value)
    {
    }

    bool Vector4::Equals(const Vector4& other) const
    {
        return X == other.X && Y == other.Y && Z == other.Z && W == other.W;
    }

    int Vector4::GetHashCode() const { return FloatHash(W) + FloatHash(X) + FloatHash(Y) + FloatHash(Z); }
    float Vector4::Length() const { return std::sqrt((X * X) + (Y * Y) + (Z * Z) + (W * W)); }
    float Vector4::LengthSquared() const { return (X * X) + (Y * Y) + (Z * Z) + (W * W); }

    void Vector4::Normalize()
    {
        const float factor = 1.0f / std::sqrt((X * X) + (Y * Y) + (Z * Z) + (W * W));
        X *= factor;
        Y *= factor;
        Z *= factor;
        W *= factor;
    }

    std::string Vector4::ToString() const
    {
        std::ostringstream s;
        s << "{X:" << X << " Y:" << Y << " Z:" << Z << " W:" << W << "}";
        return s.str();
    }

    std::string Vector4::getDebugDisplayStringProperty() const
    {
        std::ostringstream s;
        s << X << " " << Y << " " << Z << " " << W;
        return s.str();
    }

    void Vector4::CheckForNaNs() const
    {
        if (std::isnan(X) || std::isnan(Y) || std::isnan(Z) || std::isnan(W)) throw std::logic_error(
            "Vector4 contains NaNs!");
    }

    Vector4 Vector4::Add(Vector4 value1, Vector4 value2)
    {
        value1.X += value2.X;
        value1.Y += value2.Y;
        value1.Z += value2.Z;
        value1.W += value2.W;
        return value1;
    }

    void Vector4::Add(const Vector4& value1, const Vector4& value2, Vector4& result)
    {
        result.X = value1.X + value2.X;
        result.Y = value1.Y + value2.Y;
        result.Z = value1.Z + value2.Z;
        result.W = value1.W + value2.W;
    }

    Vector4 Vector4::Barycentric(Vector4 value1, Vector4 value2, Vector4 value3, float amount1, float amount2)
    {
        return Vector4(BarycentricScalar(value1.X, value2.X, value3.X, amount1, amount2),
                       BarycentricScalar(value1.Y, value2.Y, value3.Y, amount1, amount2),
                       BarycentricScalar(value1.Z, value2.Z, value3.Z, amount1, amount2),
                       BarycentricScalar(value1.W, value2.W, value3.W, amount1, amount2));
    }

    void Vector4::Barycentric(const Vector4& value1, const Vector4& value2, const Vector4& value3, float amount1,
                              float amount2, Vector4& result)
    {
        result = Barycentric(value1, value2, value3, amount1, amount2);
    }

    Vector4 Vector4::CatmullRom(Vector4 value1, Vector4 value2, Vector4 value3, Vector4 value4, float amount)
    {
        return Vector4(CatmullRomScalar(value1.X, value2.X, value3.X, value4.X, amount),
                       CatmullRomScalar(value1.Y, value2.Y, value3.Y, value4.Y, amount),
                       CatmullRomScalar(value1.Z, value2.Z, value3.Z, value4.Z, amount),
                       CatmullRomScalar(value1.W, value2.W, value3.W, value4.W, amount));
    }

    void Vector4::CatmullRom(const Vector4& value1, const Vector4& value2, const Vector4& value3, const Vector4& value4,
                             float amount, Vector4& result)
    {
        result = CatmullRom(value1, value2, value3, value4, amount);
    }

    Vector4 Vector4::Clamp(Vector4 value1, Vector4 min, Vector4 max)
    {
        return Vector4(ClampScalar(value1.X, min.X, max.X), ClampScalar(value1.Y, min.Y, max.Y),
                       ClampScalar(value1.Z, min.Z, max.Z), ClampScalar(value1.W, min.W, max.W));
    }

    void Vector4::Clamp(const Vector4& value1, const Vector4& min, const Vector4& max, Vector4& result)
    {
        result = Clamp(value1, min, max);
    }

    float Vector4::Distance(Vector4 value1, Vector4 value2) { return std::sqrt(DistanceSquared(value1, value2)); }

    void Vector4::Distance(const Vector4& value1, const Vector4& value2, float& result)
    {
        result = Distance(value1, value2);
    }

    float Vector4::DistanceSquared(Vector4 value1, Vector4 value2)
    {
        return ((value1.W - value2.W) * (value1.W - value2.W)) + ((value1.X - value2.X) * (value1.X - value2.X)) + ((
            value1.Y - value2.Y) * (value1.Y - value2.Y)) + ((value1.Z - value2.Z) * (value1.Z - value2.Z));
    }

    void Vector4::DistanceSquared(const Vector4& value1, const Vector4& value2, float& result)
    {
        result = DistanceSquared(value1, value2);
    }

    Vector4 Vector4::Divide(Vector4 value1, Vector4 value2)
    {
        value1.X /= value2.X;
        value1.Y /= value2.Y;
        value1.Z /= value2.Z;
        value1.W /= value2.W;
        return value1;
    }

    void Vector4::Divide(const Vector4& value1, const Vector4& value2, Vector4& result)
    {
        result.X = value1.X / value2.X;
        result.Y = value1.Y / value2.Y;
        result.Z = value1.Z / value2.Z;
        result.W = value1.W / value2.W;
    }

    Vector4 Vector4::Divide(Vector4 value1, float divider)
    {
        value1.X /= divider;
        value1.Y /= divider;
        value1.Z /= divider;
        value1.W /= divider;
        return value1;
    }

    void Vector4::Divide(const Vector4& value1, float divider, Vector4& result)
    {
        result.X = value1.X / divider;
        result.Y = value1.Y / divider;
        result.Z = value1.Z / divider;
        result.W = value1.W / divider;
    }

    float Vector4::Dot(Vector4 value1, Vector4 value2)
    {
        return (value1.X * value2.X) + (value1.Y * value2.Y) + (value1.Z * value2.Z) + (value1.W * value2.W);
    }

    void Vector4::Dot(const Vector4& value1, const Vector4& value2, float& result) { result = Dot(value1, value2); }

    Vector4 Vector4::Hermite(Vector4 value1, Vector4 tangent1, Vector4 value2, Vector4 tangent2, float amount)
    {
        return Vector4(HermiteScalar(value1.X, tangent1.X, value2.X, tangent2.X, amount),
                       HermiteScalar(value1.Y, tangent1.Y, value2.Y, tangent2.Y, amount),
                       HermiteScalar(value1.Z, tangent1.Z, value2.Z, tangent2.Z, amount),
                       HermiteScalar(value1.W, tangent1.W, value2.W, tangent2.W, amount));
    }

    void Vector4::Hermite(const Vector4& value1, const Vector4& tangent1, const Vector4& value2,
                          const Vector4& tangent2, float amount, Vector4& result)
    {
        result = Hermite(value1, tangent1, value2, tangent2, amount);
    }

    Vector4 Vector4::Lerp(Vector4 value1, Vector4 value2, float amount)
    {
        return Vector4(LerpScalar(value1.X, value2.X, amount), LerpScalar(value1.Y, value2.Y, amount),
                       LerpScalar(value1.Z, value2.Z, amount), LerpScalar(value1.W, value2.W, amount));
    }

    void Vector4::Lerp(const Vector4& value1, const Vector4& value2, float amount, Vector4& result)
    {
        result = Lerp(value1, value2, amount);
    }

    Vector4 Vector4::Max(Vector4 value1, Vector4 value2)
    {
        return Vector4(std::max(value1.X, value2.X), std::max(value1.Y, value2.Y), std::max(value1.Z, value2.Z),
                       std::max(value1.W, value2.W));
    }

    void Vector4::Max(const Vector4& value1, const Vector4& value2, Vector4& result) { result = Max(value1, value2); }

    Vector4 Vector4::Min(Vector4 value1, Vector4 value2)
    {
        return Vector4(std::min(value1.X, value2.X), std::min(value1.Y, value2.Y), std::min(value1.Z, value2.Z),
                       std::min(value1.W, value2.W));
    }

    void Vector4::Min(const Vector4& value1, const Vector4& value2, Vector4& result) { result = Min(value1, value2); }

    Vector4 Vector4::Multiply(Vector4 value1, Vector4 value2)
    {
        value1.X *= value2.X;
        value1.Y *= value2.Y;
        value1.Z *= value2.Z;
        value1.W *= value2.W;
        return value1;
    }

    void Vector4::Multiply(const Vector4& value1, const Vector4& value2, Vector4& result)
    {
        result.X = value1.X * value2.X;
        result.Y = value1.Y * value2.Y;
        result.Z = value1.Z * value2.Z;
        result.W = value1.W * value2.W;
    }

    Vector4 Vector4::Multiply(Vector4 value1, float scaleFactor)
    {
        value1.X *= scaleFactor;
        value1.Y *= scaleFactor;
        value1.Z *= scaleFactor;
        value1.W *= scaleFactor;
        return value1;
    }

    void Vector4::Multiply(const Vector4& value1, float scaleFactor, Vector4& result)
    {
        result.X = value1.X * scaleFactor;
        result.Y = value1.Y * scaleFactor;
        result.Z = value1.Z * scaleFactor;
        result.W = value1.W * scaleFactor;
    }

    Vector4 Vector4::Negate(Vector4 value) { return Vector4(-value.X, -value.Y, -value.Z, -value.W); }
    void Vector4::Negate(const Vector4& value, Vector4& result) { result = Negate(value); }

    Vector4 Vector4::Normalize(Vector4 value)
    {
        value.Normalize();
        return value;
    }

    void Vector4::Normalize(const Vector4& value, Vector4& result) { result = Normalize(value); }

    Vector4 Vector4::SmoothStep(Vector4 value1, Vector4 value2, float amount)
    {
        return Vector4(SmoothStepScalar(value1.X, value2.X, amount), SmoothStepScalar(value1.Y, value2.Y, amount),
                       SmoothStepScalar(value1.Z, value2.Z, amount), SmoothStepScalar(value1.W, value2.W, amount));
    }

    void Vector4::SmoothStep(const Vector4& value1, const Vector4& value2, float amount, Vector4& result)
    {
        result = SmoothStep(value1, value2, amount);
    }

    Vector4 Vector4::Subtract(Vector4 value1, Vector4 value2)
    {
        value1.X -= value2.X;
        value1.Y -= value2.Y;
        value1.Z -= value2.Z;
        value1.W -= value2.W;
        return value1;
    }

    void Vector4::Subtract(const Vector4& value1, const Vector4& value2, Vector4& result)
    {
        result.X = value1.X - value2.X;
        result.Y = value1.Y - value2.Y;
        result.Z = value1.Z - value2.Z;
        result.W = value1.W - value2.W;
    }

    Vector4 Vector4::Transform(Vector2 position, const Matrix& matrix)
    {
        Vector4 result;
        Transform(position, matrix, result);
        return result;
    }

    Vector4 Vector4::Transform(Vector3 position, const Matrix& matrix)
    {
        Vector4 result;
        Transform(position, matrix, result);
        return result;
    }

    Vector4 Vector4::Transform(Vector4 vector, const Matrix& matrix)
    {
        Vector4 result;
        Transform(vector, matrix, result);
        return result;
    }

    void Vector4::Transform(const Vector2& position, const Matrix& matrix, Vector4& result)
    {
        result.X = (position.X * matrix.M11) + (position.Y * matrix.M21) + matrix.M41;
        result.Y = (position.X * matrix.M12) + (position.Y * matrix.M22) + matrix.M42;
        result.Z = (position.X * matrix.M13) + (position.Y * matrix.M23) + matrix.M43;
        result.W = (position.X * matrix.M14) + (position.Y * matrix.M24) + matrix.M44;
    }

    void Vector4::Transform(const Vector3& position, const Matrix& matrix, Vector4& result)
    {
        result.X = (position.X * matrix.M11) + (position.Y * matrix.M21) + (position.Z * matrix.M31) + matrix.M41;
        result.Y = (position.X * matrix.M12) + (position.Y * matrix.M22) + (position.Z * matrix.M32) + matrix.M42;
        result.Z = (position.X * matrix.M13) + (position.Y * matrix.M23) + (position.Z * matrix.M33) + matrix.M43;
        result.W = (position.X * matrix.M14) + (position.Y * matrix.M24) + (position.Z * matrix.M34) + matrix.M44;
    }

    void Vector4::Transform(const Vector4& vector, const Matrix& matrix, Vector4& result)
    {
        const float x = (vector.X * matrix.M11) + (vector.Y * matrix.M21) + (vector.Z * matrix.M31) + (vector.W * matrix
            .M41);
        const float y = (vector.X * matrix.M12) + (vector.Y * matrix.M22) + (vector.Z * matrix.M32) + (vector.W * matrix
            .M42);
        const float z = (vector.X * matrix.M13) + (vector.Y * matrix.M23) + (vector.Z * matrix.M33) + (vector.W * matrix
            .M43);
        const float w = (vector.X * matrix.M14) + (vector.Y * matrix.M24) + (vector.Z * matrix.M34) + (vector.W * matrix
            .M44);
        result.X = x;
        result.Y = y;
        result.Z = z;
        result.W = w;
    }

    void Vector4::Transform(const std::vector<Vector4>& sourceArray, const Matrix& matrix,
                            std::vector<Vector4>& destinationArray)
    {
        Transform(sourceArray, 0, matrix, destinationArray, 0, static_cast<int>(sourceArray.size()));
    }

    void Vector4::Transform(const std::vector<Vector4>& sourceArray, int sourceIndex, const Matrix& matrix,
                            std::vector<Vector4>& destinationArray, int destinationIndex, int length)
    {
        CheckArrayRange(sourceArray.size(), sourceIndex, destinationArray.size(), destinationIndex, length);
        for (int i = 0; i < length; ++i) Transform(sourceArray[sourceIndex + i], matrix,
                                                   destinationArray[destinationIndex + i]);
    }

    Vector4 Vector4::Transform(Vector2 value, const Quaternion& rotation)
    {
        Vector4 result;
        Transform(value, rotation, result);
        return result;
    }

    Vector4 Vector4::Transform(Vector3 value, const Quaternion& rotation)
    {
        Vector4 result;
        Transform(value, rotation, result);
        return result;
    }

    Vector4 Vector4::Transform(Vector4 value, const Quaternion& rotation)
    {
        Vector4 result;
        Transform(value, rotation, result);
        return result;
    }

    void Vector4::Transform(const Vector2& value, const Quaternion& rotation, Vector4& result)
    {
        Vector3 temp(value.X, value.Y, 0.0f);
        Vector3 transformed;
        Vector3::Transform(temp, rotation, transformed);
        result = Vector4(transformed, 1.0f);
    }

    void Vector4::Transform(const Vector3& value, const Quaternion& rotation, Vector4& result)
    {
        Vector3 transformed;
        Vector3::Transform(value, rotation, transformed);
        result = Vector4(transformed, 1.0f);
    }

    void Vector4::Transform(const Vector4& value, const Quaternion& rotation, Vector4& result)
    {
        Vector3 temp(value.X, value.Y, value.Z);
        Vector3 transformed;
        Vector3::Transform(temp, rotation, transformed);
        result = Vector4(transformed, value.W);
    }

    void Vector4::Transform(const std::vector<Vector4>& sourceArray, const Quaternion& rotation,
                            std::vector<Vector4>& destinationArray)
    {
        Transform(sourceArray, 0, rotation, destinationArray, 0, static_cast<int>(sourceArray.size()));
    }

    void Vector4::Transform(const std::vector<Vector4>& sourceArray, int sourceIndex, const Quaternion& rotation,
                            std::vector<Vector4>& destinationArray, int destinationIndex, int length)
    {
        CheckArrayRange(sourceArray.size(), sourceIndex, destinationArray.size(), destinationIndex, length);
        for (int i = 0; i < length; ++i) Transform(sourceArray[sourceIndex + i], rotation,
                                                   destinationArray[destinationIndex + i]);
    }

    Vector4 operator-(Vector4 value) { return Vector4::Negate(value); }
    bool operator==(Vector4 value1, Vector4 value2) { return value1.Equals(value2); }
    bool operator!=(Vector4 value1, Vector4 value2) { return !value1.Equals(value2); }
    Vector4 operator+(Vector4 value1, Vector4 value2) { return Vector4::Add(value1, value2); }
    Vector4 operator-(Vector4 value1, Vector4 value2) { return Vector4::Subtract(value1, value2); }
    Vector4 operator*(Vector4 value1, Vector4 value2) { return Vector4::Multiply(value1, value2); }
    Vector4 operator*(Vector4 value1, float scaleFactor) { return Vector4::Multiply(value1, scaleFactor); }
    Vector4 operator*(float scaleFactor, Vector4 value1) { return Vector4::Multiply(value1, scaleFactor); }
    Vector4 operator/(Vector4 value1, Vector4 value2) { return Vector4::Divide(value1, value2); }
    Vector4 operator/(Vector4 value1, float divider) { return Vector4::Divide(value1, divider); }
}
