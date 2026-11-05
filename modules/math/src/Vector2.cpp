// SPDX-License-Identifier: MS-PL

#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "MathDisplay.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <stdexcept>

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "System/ArgumentException.hpp"
#include "System/IndexOutOfRangeException.hpp"
#include "System/NotFiniteNumberException.hpp"

namespace Microsoft::Xna::Framework
{
    namespace
    {
        int FloatHash(float value)
        {
            if (value == 0.0f) return 0; // XNA/.NET 4 equates both zero signs; retain all other bits.
            std::uint32_t bits = 0;
            static_assert(sizeof(bits) == sizeof(value));
            std::memcpy(&bits, &value, sizeof(value));
            return static_cast<int>(bits);
        }

        float ClampScalar(float value, float minValue, float maxValue)
        {
            // XNA caps first, then floors: reversed bounds resolve to the minimum.
            value = value > maxValue ? maxValue : value;
            return value < minValue ? minValue : value;
        }

        float LerpScalar(float value1, float value2, float amount)
        {
            // XNA Refresh Vector2.Lerp, value IL_0000-0041, out IL_0000-0038:
            // preserve expression range until the Single component store.
            return static_cast<float>(static_cast<double>(value1) + ((static_cast<double>(value2) - static_cast<double>(value1)) * amount));
        }

        float BarycentricScalar(float value1, float value2, float value3, float amount1, float amount2)
        {
            // XNA Refresh Vector2.Barycentric, value IL_0000-0067, out IL_0000-005c:
            // preserve expression range until the Single component store.
            return static_cast<float>(static_cast<double>(value1) + ((static_cast<double>(value2) - static_cast<double>(value1)) * amount1) + ((static_cast<double>(value3) - static_cast<double>(value1)) * amount2));
        }

        float CatmullRomScalar(float value1, float value2, float value3, float value4, float amount)
        {
            const float amountSquared = amount * amount;
            const float amountCubed = amountSquared * amount;
            // XNA Refresh Vector2.CatmullRom, value IL_0000-012e, out IL_0000-0117:
            // preserve expression range until the Single component store.
            return static_cast<float>(0.5f * (
                (2.0f * static_cast<double>(value2)) +
                ((-static_cast<double>(value1) + static_cast<double>(value3)) * amount) +
                (((2.0f * static_cast<double>(value1)) - (5.0f * static_cast<double>(value2)) + (4.0f * static_cast<double>(value3)) - static_cast<double>(value4)) * amountSquared) +
                ((-static_cast<double>(value1) + (3.0f * static_cast<double>(value2)) - (3.0f * static_cast<double>(value3)) + static_cast<double>(value4)) * amountCubed)
            ));
        }

        float HermiteScalar(float value1, float tangent1, float value2, float tangent2, float amount)
        {
            const float amountSquared = amount * amount;
            const float amountCubed = amountSquared * amount;
            // Refresh Vector2.Hermite IL_000b-0043: round each complete basis at its Single store.
            const float h1 = static_cast<float>((2.0 * amountCubed) - (3.0 * amountSquared) + 1.0);
            const float h2 = static_cast<float>((-2.0 * amountCubed) + (3.0 * amountSquared));
            const float h3 = static_cast<float>(static_cast<double>(amountCubed) - (2.0 * amountSquared) + amount);
            const float h4 = static_cast<float>(static_cast<double>(amountCubed) - amountSquared);
            // XNA Refresh Vector2.Hermite, value IL_0000-00a6, out IL_0000-009c:
            // preserve expression range until the Single component store.
            return static_cast<float>((static_cast<double>(value1) * h1) + (static_cast<double>(value2) * h2) + (static_cast<double>(tangent1) * h3) + (static_cast<double>(tangent2) * h4));
        }

        float SmoothStepScalar(float value1, float value2, float amount)
        {
            amount = ClampScalar(amount, 0.0f, 1.0f);
            // XNA Vector2.SmoothStep uses the cubic weight in Lerp;
            // evaluating Hermite instead changes nonfinite endpoint results.
            // Refresh Vector2.SmoothStep IL_0021-0032 stores only the complete weight.
            const double s = amount;
            return LerpScalar(value1, value2, static_cast<float>(s * s * (3.0 - 2.0 * s)));
        }

        void CheckArrayRange(std::size_t sourceSize, int sourceIndex, std::size_t destinationSize, int destinationIndex,
                             int length)
        {
            // XNA validates wide endpoints even for empty/negative-length ranges.
            const auto sourceEnd = static_cast<std::int64_t>(sourceIndex) + length;
            const auto destinationEnd = static_cast<std::int64_t>(destinationIndex) + length;
            if (sourceEnd > 0 && static_cast<std::uint64_t>(sourceEnd) > sourceSize)
            {
                throw System::ArgumentException("source range exceeds source array size");
            }
            if (destinationEnd > 0 && static_cast<std::uint64_t>(destinationEnd) > destinationSize)
            {
                throw System::ArgumentException("destination range exceeds destination array size");
            }
            // A nonpositive length performs no access, even with negative indices.
            if (length > 0 && (sourceIndex < 0 || destinationIndex < 0))
            {
                throw System::IndexOutOfRangeException();
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

    bool Vector2::Equals(const std::any& obj) const
    {
        const Vector2* other = std::any_cast<Vector2>(&obj);
        return other != nullptr && Equals(*other);
    }

    bool Vector2::Equals(const Vector2& other) const { return X == other.X && Y == other.Y; }
    int Vector2::GetHashCode() const
    {
        // Unsigned wraparound avoids signed-overflow UB (UBSan, INPUT-BUILD-006); result unchanged.
        return static_cast<int>(static_cast<unsigned>(FloatHash(X)) + static_cast<unsigned>(FloatHash(Y)));
    }
    float Vector2::Length() const
    {
        // Preserve XNA's wide intermediate range until the returned Single.
        return static_cast<float>(std::sqrt((static_cast<double>(X) * X) + (static_cast<double>(Y) * Y)));
    }
    float Vector2::LengthSquared() const { return (X * X) + (Y * Y); }

    void Vector2::Normalize()
    {
        // XNA rounds the square root to Single, but not its reciprocal.
        const double inverse = 1.0 / static_cast<double>(Length());
        X = static_cast<float>(static_cast<double>(X) * inverse);
        Y = static_cast<float>(static_cast<double>(Y) * inverse);
    }

    std::string Vector2::ToString() const
    {
        using CNA::Internal::Math::FormatXnaSingle;
        std::ostringstream s;
        s << "{X:" << FormatXnaSingle(X) << " Y:" << FormatXnaSingle(Y) << "}";
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
        if (std::isnan(X) || std::isnan(Y)) throw System::NotFiniteNumberException("Vector2 contains NaNs!");
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
        const double x = static_cast<double>(value1.X) - value2.X;
        const double y = static_cast<double>(value1.Y) - value2.Y;
        return static_cast<float>(std::sqrt((x * x) + (y * y)));
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
        // XNA Vector2.Divide(scalar), IL_0006-IL_0007: store one reciprocal before multiplying.
        const float factor = 1.0f / divider;
        value1.X *= factor;
        value1.Y *= factor;
        return value1;
    }

    void Vector2::Divide(const Vector2& value1, float divider, Vector2& result)
    {
        // XNA Vector2.Divide(ref, scalar, out), IL_0006-IL_0007: store the reciprocal first.
        const float factor = 1.0f / divider;
        result.X = value1.X * factor;
        result.Y = value1.Y * factor;
    }

    float Vector2::Dot(Vector2 value1, Vector2 value2)
    {
        // Keep products wide until cancellation and the final Single result, as XNA does.
        return static_cast<float>(static_cast<double>(value1.X) * value2.X +
                                  static_cast<double>(value1.Y) * value2.Y);
    }
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
        // Vector comparisons select operand two on ties/NaNs, unlike MathHelper.
        return Vector2(value1.X > value2.X ? value1.X : value2.X,
                       value1.Y > value2.Y ? value1.Y : value2.Y);
    }

    void Vector2::Max(const Vector2& value1, const Vector2& value2, Vector2& result) { result = Max(value1, value2); }

    Vector2 Vector2::Min(Vector2 value1, Vector2 value2)
    {
        return Vector2(value1.X < value2.X ? value1.X : value2.X,
                       value1.Y < value2.Y ? value1.Y : value2.Y);
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
        // XNA Refresh Vector2.Reflect, value IL_0000-005d, out IL_0000-0052:
        // dot has a Single store; 2*dot has none before each component.
        const double val = 2.0 * Dot(vector, normal);
        return Vector2(static_cast<float>(vector.X - (normal.X * val)), static_cast<float>(vector.Y - (normal.Y * val)));
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
        // XNA Refresh Vector2.Transform(Vector2, Matrix), out IL_0000-0054:
        // keep products and sums wide until each Single component store.
        // XNA Vector2/4.Transform(Matrix) and TransformNormal: IL_0000 to the component stores, no intervening Single sum.
        const float x = static_cast<float>((static_cast<double>(position.X) * matrix.M11) + (static_cast<double>(position.Y) * matrix.M21) + matrix.M41);
        const float y = static_cast<float>((static_cast<double>(position.X) * matrix.M12) + (static_cast<double>(position.Y) * matrix.M22) + matrix.M42);
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
        // XNA Refresh Vector2.Transform(Quaternion), value IL_0000-00bd, out IL_0000-00aa:
        // build q coefficients before reading vector components; cross temporaries can overflow.
        const float x2 = rotation.X + rotation.X;
        const float y2 = rotation.Y + rotation.Y;
        const float z2 = rotation.Z + rotation.Z;
        const float xx = rotation.X * x2, xy = rotation.X * y2;
        const float yy = rotation.Y * y2, zz = rotation.Z * z2;
        const float wz = rotation.W * z2;
        const float x = static_cast<float>(static_cast<double>(value.X) * (1.0 - yy - zz) + static_cast<double>(value.Y) * (static_cast<double>(xy) - wz));
        const float y = static_cast<float>(static_cast<double>(value.X) * (static_cast<double>(xy) + wz) + static_cast<double>(value.Y) * (1.0 - xx - zz));
        result.X = x;
        result.Y = y;
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
        // XNA Refresh Vector2.TransformNormal, value IL_0000-0051, out IL_0000-0046:
        // both products precede the Single component store.
        return Vector2((static_cast<double>(normal.X) * matrix.M11) + (static_cast<double>(normal.Y) * matrix.M21),
                       (static_cast<double>(normal.X) * matrix.M12) + (static_cast<double>(normal.Y) * matrix.M22));
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
        // XNA Vector2.op_Division(scalar), IL_0006-IL_0007: keep the reciprocal rounding.
        const float factor = 1.0f / divider;
        X *= factor;
        Y *= factor;
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
