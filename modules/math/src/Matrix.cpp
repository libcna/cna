// SPDX-License-Identifier: MS-PL

#include "Microsoft/Xna/Framework/Matrix.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <sstream>
#include <stdexcept>

#include "Microsoft/Xna/Framework/Plane.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"

namespace Microsoft::Xna::Framework
{
    const Matrix Matrix::getIdentityProperty()
    {
        static const Matrix identity = Matrix(
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        );
        return identity;
    }

    namespace
    {
        constexpr float BillboardEpsilon = 0.0001f;
        constexpr float BillboardParallelEpsilon = 0.9982547f;

        int FloatHash(float value)
        {
            std::uint32_t bits = 0;
            static_assert(sizeof(bits) == sizeof(value));
            std::memcpy(&bits, &value, sizeof(value));
            return static_cast<int>(bits);
        }

        Vector3 Add3(const Vector3& a, const Vector3& b)
        {
            return Vector3(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
        }

        Vector3 Sub3(const Vector3& a, const Vector3& b)
        {
            return Vector3(a.X - b.X, a.Y - b.Y, a.Z - b.Z);
        }

        Vector3 Neg3(const Vector3& v)
        {
            return Vector3(-v.X, -v.Y, -v.Z);
        }

        Vector3 Mul3(const Vector3& v, float s)
        {
            return Vector3(v.X * s, v.Y * s, v.Z * s);
        }

        float Dot3(const Vector3& a, const Vector3& b)
        {
            return a.X * b.X + a.Y * b.Y + a.Z * b.Z;
        }

        Vector3 Cross3(const Vector3& a, const Vector3& b)
        {
            return Vector3(
                a.Y * b.Z - b.Y * a.Z,
                -(a.X * b.Z - b.X * a.Z),
                a.X * b.Y - b.X * a.Y
            );
        }

        float LengthSquared3(const Vector3& v)
        {
            return Dot3(v, v);
        }

        Vector3 Normalize3(const Vector3& v)
        {
            const float length = std::sqrt(LengthSquared3(v));
            const float factor = 1.0f / length;
            return Mul3(v, factor);
        }

        // FNA delegates to MathHelper.WithinEpsilon which uses the computed MachineEpsilonFloat.
        // 1.192092896e-07f is FLT_EPSILON; the values are typically identical but computed
        // separately here to avoid a circular dependency from this anonymous namespace.
        bool WithinEpsilon(float a, float b)
        {
            return std::fabs(a - b) <= 1.192092896e-07f;
        }

        Plane NormalizePlaneValue(const Plane& value)
        {
            const float length = std::sqrt(Dot3(value.Normal, value.Normal));
            const float factor = 1.0f / length;
            return Plane(Mul3(value.Normal, factor), value.D * factor);
        }
    }

    Matrix::Matrix()
        : M11(0.0f), M12(0.0f), M13(0.0f), M14(0.0f),
          M21(0.0f), M22(0.0f), M23(0.0f), M24(0.0f),
          M31(0.0f), M32(0.0f), M33(0.0f), M34(0.0f),
          M41(0.0f), M42(0.0f), M43(0.0f), M44(0.0f)
    {
    }

    Matrix::Matrix(
        float m11, float m12, float m13, float m14,
        float m21, float m22, float m23, float m24,
        float m31, float m32, float m33, float m34,
        float m41, float m42, float m43, float m44
    )
        : M11(m11), M12(m12), M13(m13), M14(m14),
          M21(m21), M22(m22), M23(m23), M24(m24),
          M31(m31), M32(m32), M33(m33), M34(m34),
          M41(m41), M42(m42), M43(m43), M44(m44)
    {
    }

    Vector3 Matrix::getBackwardProperty() const { return Vector3(M31, M32, M33); }

    void Matrix::setBackwardProperty(Vector3 value)
    {
        M31 = value.X;
        M32 = value.Y;
        M33 = value.Z;
    }

    Vector3 Matrix::getDownProperty() const { return Vector3(-M21, -M22, -M23); }

    void Matrix::setDownProperty(Vector3 value)
    {
        M21 = -value.X;
        M22 = -value.Y;
        M23 = -value.Z;
    }

    Vector3 Matrix::getForwardProperty() const { return Vector3(-M31, -M32, -M33); }

    void Matrix::setForwardProperty(Vector3 value)
    {
        M31 = -value.X;
        M32 = -value.Y;
        M33 = -value.Z;
    }

    Vector3 Matrix::getLeftProperty() const { return Vector3(-M11, -M12, -M13); }

    void Matrix::setLeftProperty(Vector3 value)
    {
        M11 = -value.X;
        M12 = -value.Y;
        M13 = -value.Z;
    }

    Vector3 Matrix::getRightProperty() const { return Vector3(M11, M12, M13); }

    void Matrix::setRightProperty(Vector3 value)
    {
        M11 = value.X;
        M12 = value.Y;
        M13 = value.Z;
    }

    Vector3 Matrix::getTranslationProperty() const { return Vector3(M41, M42, M43); }

    void Matrix::setTranslationProperty(Vector3 value)
    {
        M41 = value.X;
        M42 = value.Y;
        M43 = value.Z;
    }

    Vector3 Matrix::getUpProperty() const { return Vector3(M21, M22, M23); }

    void Matrix::setUpProperty(Vector3 value)
    {
        M21 = value.X;
        M22 = value.Y;
        M23 = value.Z;
    }

    bool Matrix::Decompose(Vector3& scale, Quaternion& rotation, Vector3& translation) const
    {
        translation.X = M41;
        translation.Y = M42;
        translation.Z = M43;

        // FNA's test is `Math.Sign(row product) < 0`, and Math.Sign returns 0 -- not -1 -- for
        // negative zero, so FNA takes the +1 branch there. A previous version of this used
        // std::signbit, which IS true for negative zero, and called the difference an edge case
        // with no practical impact. It is not an edge case: every axis-aligned quarter turn has
        // exact zeros in each row, and (-1) * 0 is -0.0, so signbit fired and flipped that row's
        // sign. With unit scale that turns m1 below into a reflection, and
        // CreateFromRotationMatrix of a reflection is not a rotation at all -- a node authored
        // with a 90-degree rotation decomposed to the identity quaternion, silently losing its
        // orientation. `x < 0.0f` reproduces Math.Sign(x) < 0 exactly, including for -0.0f.
        const float xs = (M11 * M12 * M13 * M14) < 0.0f ? -1.0f : 1.0f;
        const float ys = (M21 * M22 * M23 * M24) < 0.0f ? -1.0f : 1.0f;
        const float zs = (M31 * M32 * M33 * M34) < 0.0f ? -1.0f : 1.0f;

        scale.X = xs * std::sqrt(M11 * M11 + M12 * M12 + M13 * M13);
        scale.Y = ys * std::sqrt(M21 * M21 + M22 * M22 + M23 * M23);
        scale.Z = zs * std::sqrt(M31 * M31 + M32 * M32 + M33 * M33);

        if (WithinEpsilon(scale.X, 0.0f) ||
            WithinEpsilon(scale.Y, 0.0f) ||
            WithinEpsilon(scale.Z, 0.0f))
        {
            rotation = Quaternion::Identity;
            return false;
        }

        const Matrix m1(
            M11 / scale.X, M12 / scale.X, M13 / scale.X, 0.0f,
            M21 / scale.Y, M22 / scale.Y, M23 / scale.Y, 0.0f,
            M31 / scale.Z, M32 / scale.Z, M33 / scale.Z, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        );

        rotation = Quaternion::CreateFromRotationMatrix(m1);
        return true;
    }

    float Matrix::Determinant() const
    {
        const float num18 = (M33 * M44) - (M34 * M43);
        const float num17 = (M32 * M44) - (M34 * M42);
        const float num16 = (M32 * M43) - (M33 * M42);
        const float num15 = (M31 * M44) - (M34 * M41);
        const float num14 = (M31 * M43) - (M33 * M41);
        const float num13 = (M31 * M42) - (M32 * M41);
        return (((M11 * (((M22 * num18) - (M23 * num17)) + (M24 * num16))) -
                    (M12 * (((M21 * num18) - (M23 * num15)) + (M24 * num14)))) +
                (M13 * (((M21 * num17) - (M22 * num15)) + (M24 * num13)))) -
            (M14 * (((M21 * num16) - (M22 * num14)) + (M23 * num13)));
    }

    bool Matrix::Equals(const Matrix& other) const
    {
        return M11 == other.M11 && M12 == other.M12 && M13 == other.M13 && M14 == other.M14 &&
            M21 == other.M21 && M22 == other.M22 && M23 == other.M23 && M24 == other.M24 &&
            M31 == other.M31 && M32 == other.M32 && M33 == other.M33 && M34 == other.M34 &&
            M41 == other.M41 && M42 == other.M42 && M43 == other.M43 && M44 == other.M44;
    }

    int Matrix::GetHashCode() const
    {
        return FloatHash(M11) + FloatHash(M12) + FloatHash(M13) + FloatHash(M14) +
            FloatHash(M21) + FloatHash(M22) + FloatHash(M23) + FloatHash(M24) +
            FloatHash(M31) + FloatHash(M32) + FloatHash(M33) + FloatHash(M34) +
            FloatHash(M41) + FloatHash(M42) + FloatHash(M43) + FloatHash(M44);
    }

    std::string Matrix::ToString() const
    {
        std::ostringstream stream;
        stream << "{M11:" << M11 << " M12:" << M12 << " M13:" << M13 << " M14:" << M14
            << "} {M21:" << M21 << " M22:" << M22 << " M23:" << M23 << " M24:" << M24
            << "} {M31:" << M31 << " M32:" << M32 << " M33:" << M33 << " M34:" << M34
            << "} {M41:" << M41 << " M42:" << M42 << " M43:" << M43 << " M44:" << M44 << "}";
        return stream.str();
    }

    std::string Matrix::getDebugDisplayStringProperty() const
    {
        std::ostringstream stream;
        stream << "( " << M11 << " " << M12 << " " << M13 << " " << M14 << " ) \r\n"
            << "( " << M21 << " " << M22 << " " << M23 << " " << M24 << " ) \r\n"
            << "( " << M31 << " " << M32 << " " << M33 << " " << M34 << " ) \r\n"
            << "( " << M41 << " " << M42 << " " << M43 << " " << M44 << " )";
        return stream.str();
    }

    void Matrix::CheckForNaNs() const
    {
#if !defined(NDEBUG)
        if (std::isnan(M11) || std::isnan(M12) || std::isnan(M13) || std::isnan(M14) ||
            std::isnan(M21) || std::isnan(M22) || std::isnan(M23) || std::isnan(M24) ||
            std::isnan(M31) || std::isnan(M32) || std::isnan(M33) || std::isnan(M34) ||
            std::isnan(M41) || std::isnan(M42) || std::isnan(M43) || std::isnan(M44))
        {
            throw std::logic_error("Matrix contains NaNs!");
        }
#endif
    }

    Matrix Matrix::Add(Matrix matrix1, Matrix matrix2)
    {
        Add(matrix1, matrix2, matrix1);
        return matrix1;
    }

    void Matrix::Add(const Matrix& matrix1, const Matrix& matrix2, Matrix& result)
    {
        result.M11 = matrix1.M11 + matrix2.M11;
        result.M12 = matrix1.M12 + matrix2.M12;
        result.M13 = matrix1.M13 + matrix2.M13;
        result.M14 = matrix1.M14 + matrix2.M14;
        result.M21 = matrix1.M21 + matrix2.M21;
        result.M22 = matrix1.M22 + matrix2.M22;
        result.M23 = matrix1.M23 + matrix2.M23;
        result.M24 = matrix1.M24 + matrix2.M24;
        result.M31 = matrix1.M31 + matrix2.M31;
        result.M32 = matrix1.M32 + matrix2.M32;
        result.M33 = matrix1.M33 + matrix2.M33;
        result.M34 = matrix1.M34 + matrix2.M34;
        result.M41 = matrix1.M41 + matrix2.M41;
        result.M42 = matrix1.M42 + matrix2.M42;
        result.M43 = matrix1.M43 + matrix2.M43;
        result.M44 = matrix1.M44 + matrix2.M44;
    }

    Matrix Matrix::CreateBillboard(Vector3 objectPosition, Vector3 cameraPosition, Vector3 cameraUpVector,
                                   std::optional<Vector3> cameraForwardVector)
    {
        Matrix result;
        CreateBillboard(objectPosition, cameraPosition, cameraUpVector, cameraForwardVector, result);
        return result;
    }

    void Matrix::CreateBillboard(const Vector3& objectPosition, const Vector3& cameraPosition,
                                 const Vector3& cameraUpVector, std::optional<Vector3> cameraForwardVector,
                                 Matrix& result)
    {
        Vector3 cameraDir = Sub3(objectPosition, cameraPosition);
        const float num = LengthSquared3(cameraDir);
        if (num < BillboardEpsilon)
        {
            cameraDir = cameraForwardVector ? Neg3(*cameraForwardVector) : Vector3::Forward;
        }
        else
        {
            cameraDir = Mul3(cameraDir, 1.0f / std::sqrt(num));
        }

        const Vector3 right = Normalize3(Cross3(cameraUpVector, cameraDir));
        const Vector3 up = Cross3(cameraDir, right);

        result.M11 = right.X;
        result.M12 = right.Y;
        result.M13 = right.Z;
        result.M14 = 0.0f;
        result.M21 = up.X;
        result.M22 = up.Y;
        result.M23 = up.Z;
        result.M24 = 0.0f;
        result.M31 = cameraDir.X;
        result.M32 = cameraDir.Y;
        result.M33 = cameraDir.Z;
        result.M34 = 0.0f;
        result.M41 = objectPosition.X;
        result.M42 = objectPosition.Y;
        result.M43 = objectPosition.Z;
        result.M44 = 1.0f;
    }

    Matrix Matrix::CreateConstrainedBillboard(Vector3 objectPosition, Vector3 cameraPosition, Vector3 rotateAxis,
                                              std::optional<Vector3> cameraForwardVector,
                                              std::optional<Vector3> objectForwardVector)
    {
        Matrix result;
        CreateConstrainedBillboard(objectPosition, cameraPosition, rotateAxis, cameraForwardVector, objectForwardVector,
                                   result);
        return result;
    }

    void Matrix::CreateConstrainedBillboard(const Vector3& objectPosition, const Vector3& cameraPosition,
                                            const Vector3& rotateAxis, std::optional<Vector3> cameraForwardVector,
                                            std::optional<Vector3> objectForwardVector, Matrix& result)
    {
        float num;
        Vector3 vector;
        Vector3 vector3;

        Vector3 vector2 = Sub3(objectPosition, cameraPosition);
        const float num2 = LengthSquared3(vector2);
        if (num2 < BillboardEpsilon)
        {
            vector2 = cameraForwardVector ? Neg3(*cameraForwardVector) : Vector3::Forward;
        }
        else
        {
            vector2 = Mul3(vector2, 1.0f / std::sqrt(num2));
        }

        Vector3 vector4 = rotateAxis;
        num = Dot3(rotateAxis, vector2);
        if (std::fabs(num) > BillboardParallelEpsilon)
        {
            if (objectForwardVector)
            {
                vector = *objectForwardVector;
                num = Dot3(rotateAxis, vector);
                if (std::fabs(num) > BillboardParallelEpsilon)
                {
                    num = Dot3(rotateAxis, Vector3::Forward);
                    vector = (std::fabs(num) > BillboardParallelEpsilon) ? Vector3::Right : Vector3::Forward;
                }
            }
            else
            {
                num = Dot3(rotateAxis, Vector3::Forward);
                vector = (std::fabs(num) > BillboardParallelEpsilon) ? Vector3::Right : Vector3::Forward;
            }

            vector3 = Cross3(rotateAxis, vector);
            vector3 = Normalize3(vector3);
            vector = Cross3(vector3, rotateAxis);
            vector = Normalize3(vector);
        }
        else
        {
            vector3 = Cross3(rotateAxis, vector2);
            vector3 = Normalize3(vector3);
            vector = Cross3(vector3, vector4);
            vector = Normalize3(vector);
        }

        result.M11 = vector3.X;
        result.M12 = vector3.Y;
        result.M13 = vector3.Z;
        result.M14 = 0.0f;
        result.M21 = vector4.X;
        result.M22 = vector4.Y;
        result.M23 = vector4.Z;
        result.M24 = 0.0f;
        result.M31 = vector.X;
        result.M32 = vector.Y;
        result.M33 = vector.Z;
        result.M34 = 0.0f;
        result.M41 = objectPosition.X;
        result.M42 = objectPosition.Y;
        result.M43 = objectPosition.Z;
        result.M44 = 1.0f;
    }

    Matrix Matrix::CreateFromAxisAngle(Vector3 axis, float angle)
    {
        Matrix result;
        CreateFromAxisAngle(axis, angle, result);
        return result;
    }

    void Matrix::CreateFromAxisAngle(const Vector3& axis, float angle, Matrix& result)
    {
        const float x = axis.X;
        const float y = axis.Y;
        const float z = axis.Z;
        const float num2 = std::sin(angle);
        const float num = std::cos(angle);
        const float num11 = x * x;
        const float num10 = y * y;
        const float num9 = z * z;
        const float num8 = x * y;
        const float num7 = x * z;
        const float num6 = y * z;

        result.M11 = num11 + (num * (1.0f - num11));
        result.M12 = (num8 - (num * num8)) + (num2 * z);
        result.M13 = (num7 - (num * num7)) - (num2 * y);
        result.M14 = 0.0f;
        result.M21 = (num8 - (num * num8)) - (num2 * z);
        result.M22 = num10 + (num * (1.0f - num10));
        result.M23 = (num6 - (num * num6)) + (num2 * x);
        result.M24 = 0.0f;
        result.M31 = (num7 - (num * num7)) + (num2 * y);
        result.M32 = (num6 - (num * num6)) - (num2 * x);
        result.M33 = num9 + (num * (1.0f - num9));
        result.M34 = 0.0f;
        result.M41 = 0.0f;
        result.M42 = 0.0f;
        result.M43 = 0.0f;
        result.M44 = 1.0f;
    }

    Matrix Matrix::CreateFromQuaternion(Quaternion quaternion)
    {
        Matrix result;
        CreateFromQuaternion(quaternion, result);
        return result;
    }

    void Matrix::CreateFromQuaternion(const Quaternion& quaternion, Matrix& result)
    {
        const float num9 = quaternion.X * quaternion.X;
        const float num8 = quaternion.Y * quaternion.Y;
        const float num7 = quaternion.Z * quaternion.Z;
        const float num6 = quaternion.X * quaternion.Y;
        const float num5 = quaternion.Z * quaternion.W;
        const float num4 = quaternion.Z * quaternion.X;
        const float num3 = quaternion.Y * quaternion.W;
        const float num2 = quaternion.Y * quaternion.Z;
        const float num = quaternion.X * quaternion.W;

        result.M11 = 1.0f - (2.0f * (num8 + num7));
        result.M12 = 2.0f * (num6 + num5);
        result.M13 = 2.0f * (num4 - num3);
        result.M14 = 0.0f;
        result.M21 = 2.0f * (num6 - num5);
        result.M22 = 1.0f - (2.0f * (num7 + num9));
        result.M23 = 2.0f * (num2 + num);
        result.M24 = 0.0f;
        result.M31 = 2.0f * (num4 + num3);
        result.M32 = 2.0f * (num2 - num);
        result.M33 = 1.0f - (2.0f * (num8 + num9));
        result.M34 = 0.0f;
        result.M41 = 0.0f;
        result.M42 = 0.0f;
        result.M43 = 0.0f;
        result.M44 = 1.0f;
    }

    Matrix Matrix::CreateFromYawPitchRoll(float yaw, float pitch, float roll)
    {
        Matrix result;
        CreateFromYawPitchRoll(yaw, pitch, roll, result);
        return result;
    }

    void Matrix::CreateFromYawPitchRoll(float yaw, float pitch, float roll, Matrix& result)
    {
        const Quaternion quaternion = Quaternion::CreateFromYawPitchRoll(yaw, pitch, roll);
        CreateFromQuaternion(quaternion, result);
    }

    Matrix Matrix::CreateLookAt(Vector3 cameraPosition, Vector3 cameraTarget, Vector3 cameraUpVector)
    {
        Matrix result;
        CreateLookAt(cameraPosition, cameraTarget, cameraUpVector, result);
        return result;
    }

    void Matrix::CreateLookAt(const Vector3& cameraPosition, const Vector3& cameraTarget, const Vector3& cameraUpVector,
                              Matrix& result)
    {
        const Vector3 vectorA = Normalize3(Sub3(cameraPosition, cameraTarget));
        const Vector3 vectorB = Normalize3(Cross3(cameraUpVector, vectorA));
        const Vector3 vectorC = Cross3(vectorA, vectorB);

        result.M11 = vectorB.X;
        result.M12 = vectorC.X;
        result.M13 = vectorA.X;
        result.M14 = 0.0f;
        result.M21 = vectorB.Y;
        result.M22 = vectorC.Y;
        result.M23 = vectorA.Y;
        result.M24 = 0.0f;
        result.M31 = vectorB.Z;
        result.M32 = vectorC.Z;
        result.M33 = vectorA.Z;
        result.M34 = 0.0f;
        result.M41 = -Dot3(vectorB, cameraPosition);
        result.M42 = -Dot3(vectorC, cameraPosition);
        result.M43 = -Dot3(vectorA, cameraPosition);
        result.M44 = 1.0f;
    }

    Matrix Matrix::CreateOrthographic(float width, float height, float zNearPlane, float zFarPlane)
    {
        Matrix result;
        CreateOrthographic(width, height, zNearPlane, zFarPlane, result);
        return result;
    }

    void Matrix::CreateOrthographic(float width, float height, float zNearPlane, float zFarPlane, Matrix& result)
    {
        result.M11 = 2.0f / width;
        result.M12 = result.M13 = result.M14 = 0.0f;
        result.M22 = 2.0f / height;
        result.M21 = result.M23 = result.M24 = 0.0f;
        result.M33 = 1.0f / (zNearPlane - zFarPlane);
        result.M31 = result.M32 = result.M34 = 0.0f;
        result.M41 = result.M42 = 0.0f;
        result.M43 = zNearPlane / (zNearPlane - zFarPlane);
        result.M44 = 1.0f;
    }

    Matrix Matrix::CreateOrthographicOffCenter(float left, float right, float bottom, float top, float zNearPlane,
                                               float zFarPlane)
    {
        Matrix result;
        CreateOrthographicOffCenter(left, right, bottom, top, zNearPlane, zFarPlane, result);
        return result;
    }

    void Matrix::CreateOrthographicOffCenter(float left, float right, float bottom, float top, float zNearPlane,
                                             float zFarPlane, Matrix& result)
    {
        result.M11 = 2.0f / (right - left);
        result.M12 = result.M13 = result.M14 = 0.0f;
        result.M21 = result.M23 = result.M24 = 0.0f;
        result.M22 = 2.0f / (top - bottom);
        result.M31 = result.M32 = result.M34 = 0.0f;
        result.M33 = 1.0f / (zNearPlane - zFarPlane);
        result.M41 = (left + right) / (left - right);
        result.M42 = (top + bottom) / (bottom - top);
        result.M43 = zNearPlane / (zNearPlane - zFarPlane);
        result.M44 = 1.0f;
    }

    Matrix Matrix::CreatePerspective(float width, float height, float nearPlaneDistance, float farPlaneDistance)
    {
        Matrix result;
        CreatePerspective(width, height, nearPlaneDistance, farPlaneDistance, result);
        return result;
    }

    void Matrix::CreatePerspective(float width, float height, float nearPlaneDistance, float farPlaneDistance,
                                   Matrix& result)
    {
        if (nearPlaneDistance <= 0.0f) throw std::invalid_argument("nearPlaneDistance <= 0");
        if (farPlaneDistance <= 0.0f) throw std::invalid_argument("farPlaneDistance <= 0");
        if (nearPlaneDistance >= farPlaneDistance) throw std::invalid_argument("nearPlaneDistance >= farPlaneDistance");

        result.M11 = (2.0f * nearPlaneDistance) / width;
        result.M12 = result.M13 = result.M14 = 0.0f;
        result.M22 = (2.0f * nearPlaneDistance) / height;
        result.M21 = result.M23 = result.M24 = 0.0f;
        result.M33 = farPlaneDistance / (nearPlaneDistance - farPlaneDistance);
        result.M31 = result.M32 = 0.0f;
        result.M34 = -1.0f;
        result.M41 = result.M42 = result.M44 = 0.0f;
        result.M43 = (nearPlaneDistance * farPlaneDistance) / (nearPlaneDistance - farPlaneDistance);
    }

    Matrix Matrix::CreatePerspectiveFieldOfView(float fieldOfView, float aspectRatio, float nearPlaneDistance,
                                                float farPlaneDistance)
    {
        Matrix result;
        CreatePerspectiveFieldOfView(fieldOfView, aspectRatio, nearPlaneDistance, farPlaneDistance, result);
        return result;
    }

    void Matrix::CreatePerspectiveFieldOfView(float fieldOfView, float aspectRatio, float nearPlaneDistance,
                                              float farPlaneDistance, Matrix& result)
    {
        if (fieldOfView <= 0.0f || fieldOfView >= 3.141593f) throw std::invalid_argument("fieldOfView <= 0 or >= PI");
        if (nearPlaneDistance <= 0.0f) throw std::invalid_argument("nearPlaneDistance <= 0");
        if (farPlaneDistance <= 0.0f) throw std::invalid_argument("farPlaneDistance <= 0");
        if (nearPlaneDistance >= farPlaneDistance) throw std::invalid_argument("nearPlaneDistance >= farPlaneDistance");

        const float num = 1.0f / std::tan(fieldOfView * 0.5f);
        const float num9 = num / aspectRatio;

        result.M11 = num9;
        result.M12 = result.M13 = result.M14 = 0.0f;
        result.M22 = num;
        result.M21 = result.M23 = result.M24 = 0.0f;
        result.M31 = result.M32 = 0.0f;
        result.M33 = farPlaneDistance / (nearPlaneDistance - farPlaneDistance);
        result.M34 = -1.0f;
        result.M41 = result.M42 = result.M44 = 0.0f;
        result.M43 = (nearPlaneDistance * farPlaneDistance) / (nearPlaneDistance - farPlaneDistance);
    }

    Matrix Matrix::CreatePerspectiveOffCenter(float left, float right, float bottom, float top, float nearPlaneDistance,
                                              float farPlaneDistance)
    {
        Matrix result;
        CreatePerspectiveOffCenter(left, right, bottom, top, nearPlaneDistance, farPlaneDistance, result);
        return result;
    }

    void Matrix::CreatePerspectiveOffCenter(float left, float right, float bottom, float top, float nearPlaneDistance,
                                            float farPlaneDistance, Matrix& result)
    {
        if (nearPlaneDistance <= 0.0f) throw std::invalid_argument("nearPlaneDistance <= 0");
        if (farPlaneDistance <= 0.0f) throw std::invalid_argument("farPlaneDistance <= 0");
        if (nearPlaneDistance >= farPlaneDistance) throw std::invalid_argument("nearPlaneDistance >= farPlaneDistance");

        result.M11 = (2.0f * nearPlaneDistance) / (right - left);
        result.M12 = result.M13 = result.M14 = 0.0f;
        result.M22 = (2.0f * nearPlaneDistance) / (top - bottom);
        result.M21 = result.M23 = result.M24 = 0.0f;
        result.M31 = (left + right) / (right - left);
        result.M32 = (top + bottom) / (top - bottom);
        result.M33 = farPlaneDistance / (nearPlaneDistance - farPlaneDistance);
        result.M34 = -1.0f;
        result.M43 = (nearPlaneDistance * farPlaneDistance) / (nearPlaneDistance - farPlaneDistance);
        result.M41 = result.M42 = result.M44 = 0.0f;
    }

    Matrix Matrix::CreateRotationX(float radians)
    {
        Matrix result;
        CreateRotationX(radians, result);
        return result;
    }

    void Matrix::CreateRotationX(float radians, Matrix& result)
    {
        result = Matrix::getIdentityProperty();
        const float val1 = std::cos(radians);
        const float val2 = std::sin(radians);
        result.M22 = val1;
        result.M23 = val2;
        result.M32 = -val2;
        result.M33 = val1;
    }

    Matrix Matrix::CreateRotationY(float radians)
    {
        Matrix result;
        CreateRotationY(radians, result);
        return result;
    }

    void Matrix::CreateRotationY(float radians, Matrix& result)
    {
        result = Matrix::getIdentityProperty();
        const float val1 = std::cos(radians);
        const float val2 = std::sin(radians);
        result.M11 = val1;
        result.M13 = -val2;
        result.M31 = val2;
        result.M33 = val1;
    }

    Matrix Matrix::CreateRotationZ(float radians)
    {
        Matrix result;
        CreateRotationZ(radians, result);
        return result;
    }

    void Matrix::CreateRotationZ(float radians, Matrix& result)
    {
        result = Matrix::getIdentityProperty();
        const float val1 = std::cos(radians);
        const float val2 = std::sin(radians);
        result.M11 = val1;
        result.M12 = val2;
        result.M21 = -val2;
        result.M22 = val1;
    }

    Matrix Matrix::CreateScale(float scale)
    {
        Matrix result;
        CreateScale(scale, result);
        return result;
    }

    void Matrix::CreateScale(float scale, Matrix& result)
    {
        CreateScale(scale, scale, scale, result);
    }

    Matrix Matrix::CreateScale(float xScale, float yScale, float zScale)
    {
        Matrix result;
        CreateScale(xScale, yScale, zScale, result);
        return result;
    }

    void Matrix::CreateScale(float xScale, float yScale, float zScale, Matrix& result)
    {
        result.M11 = xScale;
        result.M12 = result.M13 = result.M14 = 0.0f;
        result.M21 = 0.0f;
        result.M22 = yScale;
        result.M23 = result.M24 = 0.0f;
        result.M31 = result.M32 = 0.0f;
        result.M33 = zScale;
        result.M34 = 0.0f;
        result.M41 = result.M42 = result.M43 = 0.0f;
        result.M44 = 1.0f;
    }

    Matrix Matrix::CreateScale(Vector3 scales)
    {
        Matrix result;
        CreateScale(scales, result);
        return result;
    }

    void Matrix::CreateScale(const Vector3& scales, Matrix& result)
    {
        CreateScale(scales.X, scales.Y, scales.Z, result);
    }

    Matrix Matrix::CreateShadow(Vector3 lightDirection, Plane plane)
    {
        Matrix result;
        CreateShadow(lightDirection, plane, result);
        return result;
    }

    void Matrix::CreateShadow(const Vector3& lightDirection, const Plane& plane, Matrix& result)
    {
        const float dot = Dot3(plane.Normal, lightDirection);
        const float x = -plane.Normal.X;
        const float y = -plane.Normal.Y;
        const float z = -plane.Normal.Z;
        const float d = -plane.D;

        result.M11 = (x * lightDirection.X) + dot;
        result.M12 = x * lightDirection.Y;
        result.M13 = x * lightDirection.Z;
        result.M14 = 0.0f;
        result.M21 = y * lightDirection.X;
        result.M22 = (y * lightDirection.Y) + dot;
        result.M23 = y * lightDirection.Z;
        result.M24 = 0.0f;
        result.M31 = z * lightDirection.X;
        result.M32 = z * lightDirection.Y;
        result.M33 = (z * lightDirection.Z) + dot;
        result.M34 = 0.0f;
        result.M41 = d * lightDirection.X;
        result.M42 = d * lightDirection.Y;
        result.M43 = d * lightDirection.Z;
        result.M44 = dot;
    }

    Matrix Matrix::CreateTranslation(float xPosition, float yPosition, float zPosition)
    {
        Matrix result;
        CreateTranslation(xPosition, yPosition, zPosition, result);
        return result;
    }

    Matrix Matrix::CreateTranslation(Vector3 position)
    {
        Matrix result;
        CreateTranslation(position, result);
        return result;
    }

    void Matrix::CreateTranslation(const Vector3& position, Matrix& result)
    {
        CreateTranslation(position.X, position.Y, position.Z, result);
    }

    void Matrix::CreateTranslation(float xPosition, float yPosition, float zPosition, Matrix& result)
    {
        result.M11 = 1.0f;
        result.M12 = 0.0f;
        result.M13 = 0.0f;
        result.M14 = 0.0f;
        result.M21 = 0.0f;
        result.M22 = 1.0f;
        result.M23 = 0.0f;
        result.M24 = 0.0f;
        result.M31 = 0.0f;
        result.M32 = 0.0f;
        result.M33 = 1.0f;
        result.M34 = 0.0f;
        result.M41 = xPosition;
        result.M42 = yPosition;
        result.M43 = zPosition;
        result.M44 = 1.0f;
    }

    Matrix Matrix::CreateReflection(Plane value)
    {
        Matrix result;
        CreateReflection(value, result);
        return result;
    }

    void Matrix::CreateReflection(const Plane& value, Matrix& result)
    {
        const Plane plane = NormalizePlaneValue(value);
        const float x = plane.Normal.X;
        const float y = plane.Normal.Y;
        const float z = plane.Normal.Z;
        const float num3 = -2.0f * x;
        const float num2 = -2.0f * y;
        const float num = -2.0f * z;

        result.M11 = (num3 * x) + 1.0f;
        result.M12 = num2 * x;
        result.M13 = num * x;
        result.M14 = 0.0f;
        result.M21 = num3 * y;
        result.M22 = (num2 * y) + 1.0f;
        result.M23 = num * y;
        result.M24 = 0.0f;
        result.M31 = num3 * z;
        result.M32 = num2 * z;
        result.M33 = (num * z) + 1.0f;
        result.M34 = 0.0f;
        result.M41 = num3 * plane.D;
        result.M42 = num2 * plane.D;
        result.M43 = num * plane.D;
        result.M44 = 1.0f;
    }

    Matrix Matrix::CreateWorld(Vector3 position, Vector3 forward, Vector3 up)
    {
        Matrix result;
        CreateWorld(position, forward, up, result);
        return result;
    }

    void Matrix::CreateWorld(const Vector3& position, const Vector3& forward, const Vector3& up, Matrix& result)
    {
        const Vector3 z = Normalize3(forward);
        Vector3 x = Cross3(forward, up);
        Vector3 y = Cross3(x, forward);
        x = Normalize3(x);
        y = Normalize3(y);

        result = Matrix();
        result.setRightProperty(x);
        result.setUpProperty(y);
        result.setForwardProperty(z);
        result.setTranslationProperty(position);
        result.M44 = 1.0f;
    }

    Matrix Matrix::Divide(Matrix matrix1, Matrix matrix2)
    {
        Divide(matrix1, matrix2, matrix1);
        return matrix1;
    }

    void Matrix::Divide(const Matrix& matrix1, const Matrix& matrix2, Matrix& result)
    {
        result.M11 = matrix1.M11 / matrix2.M11;
        result.M12 = matrix1.M12 / matrix2.M12;
        result.M13 = matrix1.M13 / matrix2.M13;
        result.M14 = matrix1.M14 / matrix2.M14;
        result.M21 = matrix1.M21 / matrix2.M21;
        result.M22 = matrix1.M22 / matrix2.M22;
        result.M23 = matrix1.M23 / matrix2.M23;
        result.M24 = matrix1.M24 / matrix2.M24;
        result.M31 = matrix1.M31 / matrix2.M31;
        result.M32 = matrix1.M32 / matrix2.M32;
        result.M33 = matrix1.M33 / matrix2.M33;
        result.M34 = matrix1.M34 / matrix2.M34;
        result.M41 = matrix1.M41 / matrix2.M41;
        result.M42 = matrix1.M42 / matrix2.M42;
        result.M43 = matrix1.M43 / matrix2.M43;
        result.M44 = matrix1.M44 / matrix2.M44;
    }

    Matrix Matrix::Divide(Matrix matrix1, float divider)
    {
        Divide(matrix1, divider, matrix1);
        return matrix1;
    }

    void Matrix::Divide(const Matrix& matrix1, float divider, Matrix& result)
    {
        result.M11 = matrix1.M11 / divider;
        result.M12 = matrix1.M12 / divider;
        result.M13 = matrix1.M13 / divider;
        result.M14 = matrix1.M14 / divider;
        result.M21 = matrix1.M21 / divider;
        result.M22 = matrix1.M22 / divider;
        result.M23 = matrix1.M23 / divider;
        result.M24 = matrix1.M24 / divider;
        result.M31 = matrix1.M31 / divider;
        result.M32 = matrix1.M32 / divider;
        result.M33 = matrix1.M33 / divider;
        result.M34 = matrix1.M34 / divider;
        result.M41 = matrix1.M41 / divider;
        result.M42 = matrix1.M42 / divider;
        result.M43 = matrix1.M43 / divider;
        result.M44 = matrix1.M44 / divider;
    }

    Matrix Matrix::Invert(Matrix matrix)
    {
        Matrix result;
        Invert(matrix, result);
        return result;
    }

    void Matrix::Invert(const Matrix& matrix, Matrix& result)
    {
        /*
         * Use Laplace expansion theorem to calculate the inverse of a 4x4 matrix.
         *
         * 1. Calculate the 2x2 determinants needed the 4x4 determinant based on
         *    the 2x2 determinants.
         * 3. Create the adjugate matrix, which satisfies: A * adj(A) = det(A) * I.
         * 4. Divide adjugate matrix with the determinant to find the inverse.
         */
        // FNA casts each operand to double before multiplying to gain extra precision;
        // CNA uses plain float arithmetic for simplicity (no observable difference in practice).
        const float num1 = matrix.M11;
        const float num2 = matrix.M12;
        const float num3 = matrix.M13;
        const float num4 = matrix.M14;
        const float num5 = matrix.M21;
        const float num6 = matrix.M22;
        const float num7 = matrix.M23;
        const float num8 = matrix.M24;
        const float num9 = matrix.M31;
        const float num10 = matrix.M32;
        const float num11 = matrix.M33;
        const float num12 = matrix.M34;
        const float num13 = matrix.M41;
        const float num14 = matrix.M42;
        const float num15 = matrix.M43;
        const float num16 = matrix.M44;

        const float num17 = num11 * num16 - num12 * num15;
        const float num18 = num10 * num16 - num12 * num14;
        const float num19 = num10 * num15 - num11 * num14;
        const float num20 = num9 * num16 - num12 * num13;
        const float num21 = num9 * num15 - num11 * num13;
        const float num22 = num9 * num14 - num10 * num13;
        const float num23 = num6 * num17 - num7 * num18 + num8 * num19;
        const float num24 = -(num5 * num17 - num7 * num20 + num8 * num21);
        const float num25 = num5 * num18 - num6 * num20 + num8 * num22;
        const float num26 = -(num5 * num19 - num6 * num21 + num7 * num22);
        const float num27 = 1.0f / (num1 * num23 + num2 * num24 + num3 * num25 + num4 * num26);

        result.M11 = num23 * num27;
        result.M21 = num24 * num27;
        result.M31 = num25 * num27;
        result.M41 = num26 * num27;
        result.M12 = -(num2 * num17 - num3 * num18 + num4 * num19) * num27;
        result.M22 = (num1 * num17 - num3 * num20 + num4 * num21) * num27;
        result.M32 = -(num1 * num18 - num2 * num20 + num4 * num22) * num27;
        result.M42 = (num1 * num19 - num2 * num21 + num3 * num22) * num27;

        const float num28 = num7 * num16 - num8 * num15;
        const float num29 = num6 * num16 - num8 * num14;
        const float num30 = num6 * num15 - num7 * num14;
        const float num31 = num5 * num16 - num8 * num13;
        const float num32 = num5 * num15 - num7 * num13;
        const float num33 = num5 * num14 - num6 * num13;

        result.M13 = (num2 * num28 - num3 * num29 + num4 * num30) * num27;
        result.M23 = -(num1 * num28 - num3 * num31 + num4 * num32) * num27;
        result.M33 = (num1 * num29 - num2 * num31 + num4 * num33) * num27;
        result.M43 = -(num1 * num30 - num2 * num32 + num3 * num33) * num27;

        const float num34 = num7 * num12 - num8 * num11;
        const float num35 = num6 * num12 - num8 * num10;
        const float num36 = num6 * num11 - num7 * num10;
        const float num37 = num5 * num12 - num8 * num9;
        const float num38 = num5 * num11 - num7 * num9;
        const float num39 = num5 * num10 - num6 * num9;

        result.M14 = -(num2 * num34 - num3 * num35 + num4 * num36) * num27;
        result.M24 = (num1 * num34 - num3 * num37 + num4 * num38) * num27;
        result.M34 = -(num1 * num35 - num2 * num37 + num4 * num39) * num27;
        result.M44 = (num1 * num36 - num2 * num38 + num3 * num39) * num27;
    }

    Matrix Matrix::Lerp(Matrix matrix1, Matrix matrix2, float amount)
    {
        Matrix result;
        Lerp(matrix1, matrix2, amount, result);
        return result;
    }

    void Matrix::Lerp(const Matrix& matrix1, const Matrix& matrix2, float amount, Matrix& result)
    {
        result.M11 = matrix1.M11 + ((matrix2.M11 - matrix1.M11) * amount);
        result.M12 = matrix1.M12 + ((matrix2.M12 - matrix1.M12) * amount);
        result.M13 = matrix1.M13 + ((matrix2.M13 - matrix1.M13) * amount);
        result.M14 = matrix1.M14 + ((matrix2.M14 - matrix1.M14) * amount);
        result.M21 = matrix1.M21 + ((matrix2.M21 - matrix1.M21) * amount);
        result.M22 = matrix1.M22 + ((matrix2.M22 - matrix1.M22) * amount);
        result.M23 = matrix1.M23 + ((matrix2.M23 - matrix1.M23) * amount);
        result.M24 = matrix1.M24 + ((matrix2.M24 - matrix1.M24) * amount);
        result.M31 = matrix1.M31 + ((matrix2.M31 - matrix1.M31) * amount);
        result.M32 = matrix1.M32 + ((matrix2.M32 - matrix1.M32) * amount);
        result.M33 = matrix1.M33 + ((matrix2.M33 - matrix1.M33) * amount);
        result.M34 = matrix1.M34 + ((matrix2.M34 - matrix1.M34) * amount);
        result.M41 = matrix1.M41 + ((matrix2.M41 - matrix1.M41) * amount);
        result.M42 = matrix1.M42 + ((matrix2.M42 - matrix1.M42) * amount);
        result.M43 = matrix1.M43 + ((matrix2.M43 - matrix1.M43) * amount);
        result.M44 = matrix1.M44 + ((matrix2.M44 - matrix1.M44) * amount);
    }

    Matrix Matrix::Multiply(Matrix matrix1, Matrix matrix2)
    {
        Matrix result;
        Multiply(matrix1, matrix2, result);
        return result;
    }

    void Matrix::Multiply(const Matrix& matrix1, const Matrix& matrix2, Matrix& result)
    {
        result.M11 = matrix1.M11 * matrix2.M11 + matrix1.M12 * matrix2.M21 + matrix1.M13 * matrix2.M31 + matrix1.M14 *
            matrix2.M41;
        result.M12 = matrix1.M11 * matrix2.M12 + matrix1.M12 * matrix2.M22 + matrix1.M13 * matrix2.M32 + matrix1.M14 *
            matrix2.M42;
        result.M13 = matrix1.M11 * matrix2.M13 + matrix1.M12 * matrix2.M23 + matrix1.M13 * matrix2.M33 + matrix1.M14 *
            matrix2.M43;
        result.M14 = matrix1.M11 * matrix2.M14 + matrix1.M12 * matrix2.M24 + matrix1.M13 * matrix2.M34 + matrix1.M14 *
            matrix2.M44;

        result.M21 = matrix1.M21 * matrix2.M11 + matrix1.M22 * matrix2.M21 + matrix1.M23 * matrix2.M31 + matrix1.M24 *
            matrix2.M41;
        result.M22 = matrix1.M21 * matrix2.M12 + matrix1.M22 * matrix2.M22 + matrix1.M23 * matrix2.M32 + matrix1.M24 *
            matrix2.M42;
        result.M23 = matrix1.M21 * matrix2.M13 + matrix1.M22 * matrix2.M23 + matrix1.M23 * matrix2.M33 + matrix1.M24 *
            matrix2.M43;
        result.M24 = matrix1.M21 * matrix2.M14 + matrix1.M22 * matrix2.M24 + matrix1.M23 * matrix2.M34 + matrix1.M24 *
            matrix2.M44;

        result.M31 = matrix1.M31 * matrix2.M11 + matrix1.M32 * matrix2.M21 + matrix1.M33 * matrix2.M31 + matrix1.M34 *
            matrix2.M41;
        result.M32 = matrix1.M31 * matrix2.M12 + matrix1.M32 * matrix2.M22 + matrix1.M33 * matrix2.M32 + matrix1.M34 *
            matrix2.M42;
        result.M33 = matrix1.M31 * matrix2.M13 + matrix1.M32 * matrix2.M23 + matrix1.M33 * matrix2.M33 + matrix1.M34 *
            matrix2.M43;
        result.M34 = matrix1.M31 * matrix2.M14 + matrix1.M32 * matrix2.M24 + matrix1.M33 * matrix2.M34 + matrix1.M34 *
            matrix2.M44;

        result.M41 = matrix1.M41 * matrix2.M11 + matrix1.M42 * matrix2.M21 + matrix1.M43 * matrix2.M31 + matrix1.M44 *
            matrix2.M41;
        result.M42 = matrix1.M41 * matrix2.M12 + matrix1.M42 * matrix2.M22 + matrix1.M43 * matrix2.M32 + matrix1.M44 *
            matrix2.M42;
        result.M43 = matrix1.M41 * matrix2.M13 + matrix1.M42 * matrix2.M23 + matrix1.M43 * matrix2.M33 + matrix1.M44 *
            matrix2.M43;
        result.M44 = matrix1.M41 * matrix2.M14 + matrix1.M42 * matrix2.M24 + matrix1.M43 * matrix2.M34 + matrix1.M44 *
            matrix2.M44;
    }

    Matrix Matrix::Multiply(Matrix matrix1, float scaleFactor)
    {
        Matrix result;
        Multiply(matrix1, scaleFactor, result);
        return result;
    }

    void Matrix::Multiply(const Matrix& matrix1, float scaleFactor, Matrix& result)
    {
        result.M11 = matrix1.M11 * scaleFactor;
        result.M12 = matrix1.M12 * scaleFactor;
        result.M13 = matrix1.M13 * scaleFactor;
        result.M14 = matrix1.M14 * scaleFactor;
        result.M21 = matrix1.M21 * scaleFactor;
        result.M22 = matrix1.M22 * scaleFactor;
        result.M23 = matrix1.M23 * scaleFactor;
        result.M24 = matrix1.M24 * scaleFactor;
        result.M31 = matrix1.M31 * scaleFactor;
        result.M32 = matrix1.M32 * scaleFactor;
        result.M33 = matrix1.M33 * scaleFactor;
        result.M34 = matrix1.M34 * scaleFactor;
        result.M41 = matrix1.M41 * scaleFactor;
        result.M42 = matrix1.M42 * scaleFactor;
        result.M43 = matrix1.M43 * scaleFactor;
        result.M44 = matrix1.M44 * scaleFactor;
    }

    Matrix Matrix::Negate(Matrix matrix)
    {
        Matrix result;
        Negate(matrix, result);
        return result;
    }

    void Matrix::Negate(const Matrix& matrix, Matrix& result)
    {
        Multiply(matrix, -1.0f, result);
    }

    Matrix Matrix::Subtract(Matrix matrix1, Matrix matrix2)
    {
        Matrix result;
        Subtract(matrix1, matrix2, result);
        return result;
    }

    void Matrix::Subtract(const Matrix& matrix1, const Matrix& matrix2, Matrix& result)
    {
        result.M11 = matrix1.M11 - matrix2.M11;
        result.M12 = matrix1.M12 - matrix2.M12;
        result.M13 = matrix1.M13 - matrix2.M13;
        result.M14 = matrix1.M14 - matrix2.M14;
        result.M21 = matrix1.M21 - matrix2.M21;
        result.M22 = matrix1.M22 - matrix2.M22;
        result.M23 = matrix1.M23 - matrix2.M23;
        result.M24 = matrix1.M24 - matrix2.M24;
        result.M31 = matrix1.M31 - matrix2.M31;
        result.M32 = matrix1.M32 - matrix2.M32;
        result.M33 = matrix1.M33 - matrix2.M33;
        result.M34 = matrix1.M34 - matrix2.M34;
        result.M41 = matrix1.M41 - matrix2.M41;
        result.M42 = matrix1.M42 - matrix2.M42;
        result.M43 = matrix1.M43 - matrix2.M43;
        result.M44 = matrix1.M44 - matrix2.M44;
    }

    Matrix Matrix::Transpose(Matrix matrix)
    {
        Matrix result;
        Transpose(matrix, result);
        return result;
    }

    void Matrix::Transpose(const Matrix& matrix, Matrix& result)
    {
        result.M11 = matrix.M11;
        result.M12 = matrix.M21;
        result.M13 = matrix.M31;
        result.M14 = matrix.M41;
        result.M21 = matrix.M12;
        result.M22 = matrix.M22;
        result.M23 = matrix.M32;
        result.M24 = matrix.M42;
        result.M31 = matrix.M13;
        result.M32 = matrix.M23;
        result.M33 = matrix.M33;
        result.M34 = matrix.M43;
        result.M41 = matrix.M14;
        result.M42 = matrix.M24;
        result.M43 = matrix.M34;
        result.M44 = matrix.M44;
    }

    Matrix Matrix::Transform(Matrix value, Quaternion rotation)
    {
        Matrix result;
        Transform(value, rotation, result);
        return result;
    }

    void Matrix::Transform(const Matrix& value, const Quaternion& rotation, Matrix& result)
    {
        const Matrix rotMatrix = CreateFromQuaternion(rotation);
        Multiply(value, rotMatrix, result);
    }

    Matrix operator+(Matrix matrix1, Matrix matrix2) { return Matrix::Add(matrix1, matrix2); }
    Matrix operator/(Matrix matrix1, Matrix matrix2) { return Matrix::Divide(matrix1, matrix2); }
    Matrix operator/(Matrix matrix, float divider) { return Matrix::Divide(matrix, divider); }
    bool operator==(Matrix matrix1, Matrix matrix2) { return matrix1.Equals(matrix2); }
    bool operator!=(Matrix matrix1, Matrix matrix2) { return !matrix1.Equals(matrix2); }
    Matrix operator*(Matrix matrix1, Matrix matrix2) { return Matrix::Multiply(matrix1, matrix2); }
    Matrix operator*(Matrix matrix, float scaleFactor) { return Matrix::Multiply(matrix, scaleFactor); }
    Matrix operator-(Matrix matrix1, Matrix matrix2) { return Matrix::Subtract(matrix1, matrix2); }
    Matrix operator-(Matrix matrix) { return Matrix::Negate(matrix); }

    void Matrix::ToColumnMajor(float out[16]) const
    {
        out[0] = M11;
        out[1] = M12;
        out[2] = M13;
        out[3] = M14;
        out[4] = M21;
        out[5] = M22;
        out[6] = M23;
        out[7] = M24;
        out[8] = M31;
        out[9] = M32;
        out[10] = M33;
        out[11] = M34;
        out[12] = M41;
        out[13] = M42;
        out[14] = M43;
        out[15] = M44;
    }
}
