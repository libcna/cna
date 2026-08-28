// SPDX-License-Identifier: MS-PL

#include "Microsoft/Xna/Framework/Quaternion.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace Microsoft::Xna::Framework
{
    const Quaternion Quaternion::Identity(0.0f, 0.0f, 0.0f, 1.0f);

    namespace
    {
        int FloatHash(float value)
        {
            std::uint32_t bits = 0;
            static_assert(sizeof(bits) == sizeof(value));
            std::memcpy(&bits, &value, sizeof(value));
            return static_cast<int>(bits);
        }
    }

    Quaternion::Quaternion() : X(0.0f), Y(0.0f), Z(0.0f), W(0.0f)
    {
    }

    Quaternion::Quaternion(float x, float y, float z, float w)
        : X(x), Y(y), Z(z), W(w)
    {
    }

    Quaternion::Quaternion(Vector3 vectorPart, float scalarPart)
        : X(vectorPart.X), Y(vectorPart.Y), Z(vectorPart.Z), W(scalarPart)
    {
    }

    void Quaternion::Conjugate()
    {
        X = -X;
        Y = -Y;
        Z = -Z;
    }

    bool Quaternion::Equals(const Quaternion& other) const
    {
        return X == other.X &&
            Y == other.Y &&
            Z == other.Z &&
            W == other.W;
    }

    int Quaternion::GetHashCode() const
    {
        return FloatHash(X) + FloatHash(Y) + FloatHash(Z) + FloatHash(W);
    }

    float Quaternion::Length() const
    {
        const float num = (X * X) + (Y * Y) + (Z * Z) + (W * W);
        return std::sqrt(num);
    }

    float Quaternion::LengthSquared() const
    {
        return (X * X) + (Y * Y) + (Z * Z) + (W * W);
    }

    void Quaternion::Normalize()
    {
        const float num = 1.0f / std::sqrt((X * X) + (Y * Y) + (Z * Z) + (W * W));
        X *= num;
        Y *= num;
        Z *= num;
        W *= num;
    }

    std::string Quaternion::ToString() const
    {
        std::ostringstream stream;
        stream << "{X:" << X << " Y:" << Y << " Z:" << Z << " W:" << W << "}";
        return stream.str();
    }

    std::string Quaternion::getDebugDisplayStringProperty() const
    {
        if (*this == Quaternion::Identity)
        {
            return "Identity";
        }

        std::ostringstream stream;
        stream << X << " " << Y << " " << Z << " " << W;
        return stream.str();
    }

    void Quaternion::CheckForNaNs() const
    {
#if !defined(NDEBUG)
        if (std::isnan(X) || std::isnan(Y) || std::isnan(Z) || std::isnan(W))
        {
            throw std::logic_error("Quaternion contains NaNs!");
        }
#endif
    }

    Quaternion Quaternion::Add(Quaternion quaternion1, Quaternion quaternion2)
    {
        Quaternion result(0.0f, 0.0f, 0.0f, 0.0f);
        Add(quaternion1, quaternion2, result);
        return result;
    }

    void Quaternion::Add(const Quaternion& quaternion1, const Quaternion& quaternion2, Quaternion& result)
    {
        result.X = quaternion1.X + quaternion2.X;
        result.Y = quaternion1.Y + quaternion2.Y;
        result.Z = quaternion1.Z + quaternion2.Z;
        result.W = quaternion1.W + quaternion2.W;
    }

    Quaternion Quaternion::Concatenate(Quaternion value1, Quaternion value2)
    {
        Quaternion result(0.0f, 0.0f, 0.0f, 0.0f);
        Concatenate(value1, value2, result);
        return result;
    }

    void Quaternion::Concatenate(const Quaternion& value1, const Quaternion& value2, Quaternion& result)
    {
        const float x1 = value1.X;
        const float y1 = value1.Y;
        const float z1 = value1.Z;
        const float w1 = value1.W;

        const float x2 = value2.X;
        const float y2 = value2.Y;
        const float z2 = value2.Z;
        const float w2 = value2.W;

        result.X = ((x2 * w1) + (x1 * w2)) + ((y2 * z1) - (z2 * y1));
        result.Y = ((y2 * w1) + (y1 * w2)) + ((z2 * x1) - (x2 * z1));
        result.Z = ((z2 * w1) + (z1 * w2)) + ((x2 * y1) - (y2 * x1));
        result.W = (w2 * w1) - (((x2 * x1) + (y2 * y1)) + (z2 * z1));
    }

    Quaternion Quaternion::Conjugate(Quaternion value)
    {
        return Quaternion(-value.X, -value.Y, -value.Z, value.W);
    }

    void Quaternion::Conjugate(const Quaternion& value, Quaternion& result)
    {
        result.X = -value.X;
        result.Y = -value.Y;
        result.Z = -value.Z;
        result.W = value.W;
    }

    Quaternion Quaternion::CreateFromAxisAngle(Vector3 axis, float angle)
    {
        Quaternion result(0.0f, 0.0f, 0.0f, 0.0f);
        CreateFromAxisAngle(axis, angle, result);
        return result;
    }

    void Quaternion::CreateFromAxisAngle(const Vector3& axis, float angle, Quaternion& result)
    {
        const float half = angle * 0.5f;
        const float sin = std::sin(half);
        const float cos = std::cos(half);
        result.X = axis.X * sin;
        result.Y = axis.Y * sin;
        result.Z = axis.Z * sin;
        result.W = cos;
    }

    Quaternion Quaternion::CreateFromRotationMatrix(Matrix matrix)
    {
        Quaternion result(0.0f, 0.0f, 0.0f, 0.0f);
        CreateFromRotationMatrix(matrix, result);
        return result;
    }

    void Quaternion::CreateFromRotationMatrix(const Matrix& matrix, Quaternion& result)
    {
        float sqrt;
        float half;
        const float scale = matrix.M11 + matrix.M22 + matrix.M33;

        if (scale > 0.0f)
        {
            sqrt = std::sqrt(scale + 1.0f);
            result.W = sqrt * 0.5f;
            sqrt = 0.5f / sqrt;

            result.X = (matrix.M23 - matrix.M32) * sqrt;
            result.Y = (matrix.M31 - matrix.M13) * sqrt;
            result.Z = (matrix.M12 - matrix.M21) * sqrt;
        }
        else if ((matrix.M11 >= matrix.M22) && (matrix.M11 >= matrix.M33))
        {
            sqrt = std::sqrt(1.0f + matrix.M11 - matrix.M22 - matrix.M33);
            half = 0.5f / sqrt;

            result.X = 0.5f * sqrt;
            result.Y = (matrix.M12 + matrix.M21) * half;
            result.Z = (matrix.M13 + matrix.M31) * half;
            result.W = (matrix.M23 - matrix.M32) * half;
        }
        else if (matrix.M22 > matrix.M33)
        {
            sqrt = std::sqrt(1.0f + matrix.M22 - matrix.M11 - matrix.M33);
            half = 0.5f / sqrt;

            result.X = (matrix.M21 + matrix.M12) * half;
            result.Y = 0.5f * sqrt;
            result.Z = (matrix.M32 + matrix.M23) * half;
            result.W = (matrix.M31 - matrix.M13) * half;
        }
        else
        {
            sqrt = std::sqrt(1.0f + matrix.M33 - matrix.M11 - matrix.M22);
            half = 0.5f / sqrt;

            result.X = (matrix.M31 + matrix.M13) * half;
            result.Y = (matrix.M32 + matrix.M23) * half;
            result.Z = 0.5f * sqrt;
            result.W = (matrix.M12 - matrix.M21) * half;
        }
    }

    Quaternion Quaternion::CreateFromYawPitchRoll(float yaw, float pitch, float roll)
    {
        Quaternion result(0.0f, 0.0f, 0.0f, 0.0f);
        CreateFromYawPitchRoll(yaw, pitch, roll, result);
        return result;
    }

    void Quaternion::CreateFromYawPitchRoll(float yaw, float pitch, float roll, Quaternion& result)
    {
        const float halfRoll = roll * 0.5f;
        const float sinRoll = std::sin(halfRoll);
        const float cosRoll = std::cos(halfRoll);
        const float halfPitch = pitch * 0.5f;
        const float sinPitch = std::sin(halfPitch);
        const float cosPitch = std::cos(halfPitch);
        const float halfYaw = yaw * 0.5f;
        const float sinYaw = std::sin(halfYaw);
        const float cosYaw = std::cos(halfYaw);

        result.X = ((cosYaw * sinPitch) * cosRoll) + ((sinYaw * cosPitch) * sinRoll);
        result.Y = ((sinYaw * cosPitch) * cosRoll) - ((cosYaw * sinPitch) * sinRoll);
        result.Z = ((cosYaw * cosPitch) * sinRoll) - ((sinYaw * sinPitch) * cosRoll);
        result.W = ((cosYaw * cosPitch) * cosRoll) + ((sinYaw * sinPitch) * sinRoll);
    }

    Quaternion Quaternion::Divide(Quaternion quaternion1, Quaternion quaternion2)
    {
        Quaternion result(0.0f, 0.0f, 0.0f, 0.0f);
        Divide(quaternion1, quaternion2, result);
        return result;
    }

    void Quaternion::Divide(const Quaternion& quaternion1, const Quaternion& quaternion2, Quaternion& result)
    {
        const float x = quaternion1.X;
        const float y = quaternion1.Y;
        const float z = quaternion1.Z;
        const float w = quaternion1.W;

        const float num14 = (quaternion2.X * quaternion2.X) +
            (quaternion2.Y * quaternion2.Y) +
            (quaternion2.Z * quaternion2.Z) +
            (quaternion2.W * quaternion2.W);
        const float num5 = 1.0f / num14;
        const float num4 = -quaternion2.X * num5;
        const float num3 = -quaternion2.Y * num5;
        const float num2 = -quaternion2.Z * num5;
        const float num = quaternion2.W * num5;
        const float num13 = (y * num2) - (z * num3);
        const float num12 = (z * num4) - (x * num2);
        const float num11 = (x * num3) - (y * num4);
        const float num10 = ((x * num4) + (y * num3)) + (z * num2);

        result.X = ((x * num) + (num4 * w)) + num13;
        result.Y = ((y * num) + (num3 * w)) + num12;
        result.Z = ((z * num) + (num2 * w)) + num11;
        result.W = (w * num) - num10;
    }

    float Quaternion::Dot(Quaternion quaternion1, Quaternion quaternion2)
    {
        return (quaternion1.X * quaternion2.X) +
            (quaternion1.Y * quaternion2.Y) +
            (quaternion1.Z * quaternion2.Z) +
            (quaternion1.W * quaternion2.W);
    }

    void Quaternion::Dot(const Quaternion& quaternion1, const Quaternion& quaternion2, float& result)
    {
        result = Dot(quaternion1, quaternion2);
    }

    Quaternion Quaternion::Inverse(Quaternion quaternion)
    {
        Quaternion result(0.0f, 0.0f, 0.0f, 0.0f);
        Inverse(quaternion, result);
        return result;
    }

    void Quaternion::Inverse(const Quaternion& quaternion, Quaternion& result)
    {
        const float num2 = (quaternion.X * quaternion.X) +
            (quaternion.Y * quaternion.Y) +
            (quaternion.Z * quaternion.Z) +
            (quaternion.W * quaternion.W);
        const float num = 1.0f / num2;
        result.X = -quaternion.X * num;
        result.Y = -quaternion.Y * num;
        result.Z = -quaternion.Z * num;
        result.W = quaternion.W * num;
    }

    Quaternion Quaternion::Lerp(Quaternion quaternion1, Quaternion quaternion2, float amount)
    {
        Quaternion result(0.0f, 0.0f, 0.0f, 0.0f);
        Lerp(quaternion1, quaternion2, amount, result);
        return result;
    }

    void Quaternion::Lerp(const Quaternion& quaternion1, const Quaternion& quaternion2, float amount,
                          Quaternion& result)
    {
        const float num = amount;
        const float num2 = 1.0f - num;
        const float num5 = (quaternion1.X * quaternion2.X) +
            (quaternion1.Y * quaternion2.Y) +
            (quaternion1.Z * quaternion2.Z) +
            (quaternion1.W * quaternion2.W);

        if (num5 >= 0.0f)
        {
            result.X = (num2 * quaternion1.X) + (num * quaternion2.X);
            result.Y = (num2 * quaternion1.Y) + (num * quaternion2.Y);
            result.Z = (num2 * quaternion1.Z) + (num * quaternion2.Z);
            result.W = (num2 * quaternion1.W) + (num * quaternion2.W);
        }
        else
        {
            result.X = (num2 * quaternion1.X) - (num * quaternion2.X);
            result.Y = (num2 * quaternion1.Y) - (num * quaternion2.Y);
            result.Z = (num2 * quaternion1.Z) - (num * quaternion2.Z);
            result.W = (num2 * quaternion1.W) - (num * quaternion2.W);
        }

        const float num4 = (result.X * result.X) +
            (result.Y * result.Y) +
            (result.Z * result.Z) +
            (result.W * result.W);
        const float num3 = 1.0f / std::sqrt(num4);
        result.X *= num3;
        result.Y *= num3;
        result.Z *= num3;
        result.W *= num3;
    }

    Quaternion Quaternion::Slerp(Quaternion quaternion1, Quaternion quaternion2, float amount)
    {
        Quaternion result(0.0f, 0.0f, 0.0f, 0.0f);
        Slerp(quaternion1, quaternion2, amount, result);
        return result;
    }

    void Quaternion::Slerp(const Quaternion& quaternion1, const Quaternion& quaternion2, float amount,
                           Quaternion& result)
    {
        float num2;
        float num3;
        const float num = amount;
        float num4 = (quaternion1.X * quaternion2.X) +
            (quaternion1.Y * quaternion2.Y) +
            (quaternion1.Z * quaternion2.Z) +
            (quaternion1.W * quaternion2.W);
        float flag = 1.0f;

        if (num4 < 0.0f)
        {
            flag = -1.0f;
            num4 = -num4;
        }

        if (num4 > 0.999999f)
        {
            num3 = 1.0f - num;
            num2 = num * flag;
        }
        else
        {
            const float num5 = std::acos(num4);
            const float num6 = 1.0f / std::sin(num5);
            num3 = std::sin((1.0f - num) * num5) * num6;
            num2 = flag * (std::sin(num * num5) * num6);
        }

        result.X = (num3 * quaternion1.X) + (num2 * quaternion2.X);
        result.Y = (num3 * quaternion1.Y) + (num2 * quaternion2.Y);
        result.Z = (num3 * quaternion1.Z) + (num2 * quaternion2.Z);
        result.W = (num3 * quaternion1.W) + (num2 * quaternion2.W);
    }

    Quaternion Quaternion::Subtract(Quaternion quaternion1, Quaternion quaternion2)
    {
        Quaternion result(0.0f, 0.0f, 0.0f, 0.0f);
        Subtract(quaternion1, quaternion2, result);
        return result;
    }

    void Quaternion::Subtract(const Quaternion& quaternion1, const Quaternion& quaternion2, Quaternion& result)
    {
        result.X = quaternion1.X - quaternion2.X;
        result.Y = quaternion1.Y - quaternion2.Y;
        result.Z = quaternion1.Z - quaternion2.Z;
        result.W = quaternion1.W - quaternion2.W;
    }

    Quaternion Quaternion::Multiply(Quaternion quaternion1, Quaternion quaternion2)
    {
        Quaternion result(0.0f, 0.0f, 0.0f, 0.0f);
        Multiply(quaternion1, quaternion2, result);
        return result;
    }

    Quaternion Quaternion::Multiply(Quaternion quaternion1, float scaleFactor)
    {
        Quaternion result(0.0f, 0.0f, 0.0f, 0.0f);
        Multiply(quaternion1, scaleFactor, result);
        return result;
    }

    void Quaternion::Multiply(const Quaternion& quaternion1, const Quaternion& quaternion2, Quaternion& result)
    {
        const float x = quaternion1.X;
        const float y = quaternion1.Y;
        const float z = quaternion1.Z;
        const float w = quaternion1.W;
        const float num4 = quaternion2.X;
        const float num3 = quaternion2.Y;
        const float num2 = quaternion2.Z;
        const float num = quaternion2.W;
        const float num12 = (y * num2) - (z * num3);
        const float num11 = (z * num4) - (x * num2);
        const float num10 = (x * num3) - (y * num4);
        const float num9 = ((x * num4) + (y * num3)) + (z * num2);

        result.X = ((x * num) + (num4 * w)) + num12;
        result.Y = ((y * num) + (num3 * w)) + num11;
        result.Z = ((z * num) + (num2 * w)) + num10;
        result.W = (w * num) - num9;
    }

    void Quaternion::Multiply(const Quaternion& quaternion1, float scaleFactor, Quaternion& result)
    {
        result.X = quaternion1.X * scaleFactor;
        result.Y = quaternion1.Y * scaleFactor;
        result.Z = quaternion1.Z * scaleFactor;
        result.W = quaternion1.W * scaleFactor;
    }

    Quaternion Quaternion::Negate(Quaternion quaternion)
    {
        return Quaternion(-quaternion.X, -quaternion.Y, -quaternion.Z, -quaternion.W);
    }

    void Quaternion::Negate(const Quaternion& quaternion, Quaternion& result)
    {
        result.X = -quaternion.X;
        result.Y = -quaternion.Y;
        result.Z = -quaternion.Z;
        result.W = -quaternion.W;
    }

    Quaternion Quaternion::Normalize(Quaternion quaternion)
    {
        Quaternion result(0.0f, 0.0f, 0.0f, 0.0f);
        Normalize(quaternion, result);
        return result;
    }

    void Quaternion::Normalize(const Quaternion& quaternion, Quaternion& result)
    {
        const float num = 1.0f / std::sqrt(
            (quaternion.X * quaternion.X) +
            (quaternion.Y * quaternion.Y) +
            (quaternion.Z * quaternion.Z) +
            (quaternion.W * quaternion.W)
        );

        result.X = quaternion.X * num;
        result.Y = quaternion.Y * num;
        result.Z = quaternion.Z * num;
        result.W = quaternion.W * num;
    }

    Quaternion operator+(Quaternion quaternion1, Quaternion quaternion2)
    {
        return Quaternion::Add(quaternion1, quaternion2);
    }

    Quaternion operator/(Quaternion quaternion1, Quaternion quaternion2)
    {
        return Quaternion::Divide(quaternion1, quaternion2);
    }

    bool operator==(Quaternion quaternion1, Quaternion quaternion2)
    {
        return quaternion1.Equals(quaternion2);
    }

    bool operator!=(Quaternion quaternion1, Quaternion quaternion2)
    {
        return !quaternion1.Equals(quaternion2);
    }

    Quaternion operator*(Quaternion quaternion1, Quaternion quaternion2)
    {
        return Quaternion::Multiply(quaternion1, quaternion2);
    }

    Quaternion operator*(Quaternion quaternion1, float scaleFactor)
    {
        return Quaternion::Multiply(quaternion1, scaleFactor);
    }

    Quaternion operator-(Quaternion quaternion1, Quaternion quaternion2)
    {
        return Quaternion::Subtract(quaternion1, quaternion2);
    }

    Quaternion operator-(Quaternion quaternion)
    {
        return Quaternion::Negate(quaternion);
    }
}
