// SPDX-License-Identifier: MS-PL

#pragma once

#include <string>

#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"

namespace Microsoft::Xna::Framework
{
    /** @brief Efficient value type used to represent 3D rotations. */
    struct Quaternion
    {
        /** @brief Quaternion that represents no rotation. */
        static const Quaternion Identity;

        /** @brief X component of the quaternion vector part. */
        float X;

        /** @brief Y component of the quaternion vector part. */
        float Y;

        /** @brief Z component of the quaternion vector part. */
        float Z;

        /** @brief Scalar rotation component. */
        float W;

        /**
         * @brief Constructs a quaternion whose components are all zero.
         *
         * C# gives every struct a parameterless constructor that zeroes its fields, so an
         * undeclared XNA Quaternion field starts here; Vector3 and Matrix already match that.
         */
        Quaternion();

        /**
         * @brief Constructs a quaternion from four scalar components.
         *
         * @param x The X component.
         * @param y The Y component.
         * @param z The Z component.
         * @param w The W (scalar) component.
         */
        Quaternion(float x, float y, float z, float w);

        /**
         * @brief Constructs a quaternion from a vector part and a scalar part.
         *
         * @param vectorPart The vector part (X, Y, Z components).
         * @param scalarPart The scalar part (W component).
         */
        Quaternion(Vector3 vectorPart, float scalarPart);

        /** @brief Negates the vector part of this quaternion in place. */
        void Conjugate();

        /**
         * @brief Compares this quaternion with another quaternion.
         *
         * @param other The quaternion to compare against.
         * @return @c true if the quaternions are equal; @c false otherwise.
         */
        [[nodiscard]] bool Equals(const Quaternion& other) const;

        /**
         * @brief Returns a hash code for this quaternion.
         *
         * @return Hash code of this quaternion.
         */
        [[nodiscard]] int GetHashCode() const;

        /**
         * @brief Returns the magnitude of this quaternion.
         *
         * @return The magnitude of this quaternion.
         */
        [[nodiscard]] float Length() const;

        /**
         * @brief Returns the squared magnitude of this quaternion.
         *
         * @return The squared magnitude of this quaternion.
         */
        [[nodiscard]] float LengthSquared() const;

        /** @brief Scales this quaternion to unit length. */
        void Normalize();

        /**
         * @brief Returns a string in the form {X:... Y:... Z:... W:...}.
         *
         * @return String representation of this quaternion.
         */
        [[nodiscard]] std::string ToString() const;

        /**
         * @brief Adds two quaternions.
         *
         * @param quaternion1 Source quaternion.
         * @param quaternion2 Source quaternion.
         * @return The sum of the two quaternions.
         */
        [[nodiscard]] static Quaternion Add(Quaternion quaternion1, Quaternion quaternion2);

        /**
         * @brief Adds two quaternions and stores the result in an output parameter.
         *
         * @param quaternion1 Source quaternion.
         * @param quaternion2 Source quaternion.
         * @param result Output quaternion that receives the sum.
         */
        static void Add(const Quaternion& quaternion1, const Quaternion& quaternion2, Quaternion& result);

        /**
         * @brief Concatenates two rotations so that value1 is followed by value2.
         *
         * @param value1 The first quaternion rotation.
         * @param value2 The second quaternion rotation.
         * @return A quaternion representing both rotations applied sequentially.
         */
        [[nodiscard]] static Quaternion Concatenate(Quaternion value1, Quaternion value2);

        /**
         * @brief Concatenates two rotations and stores the result in an output parameter.
         *
         * @param value1 The first quaternion rotation.
         * @param value2 The second quaternion rotation.
         * @param result Output quaternion that receives the concatenated rotation.
         */
        static void Concatenate(const Quaternion& value1, const Quaternion& value2, Quaternion& result);

        /**
         * @brief Creates the conjugate of a quaternion.
         *
         * @param value Source quaternion.
         * @return The conjugate of the quaternion.
         */
        [[nodiscard]] static Quaternion Conjugate(Quaternion value);

        /**
         * @brief Creates the conjugate of a quaternion and stores it in an output parameter.
         *
         * @param value Source quaternion.
         * @param result Output quaternion that receives the conjugate.
         */
        static void Conjugate(const Quaternion& value, Quaternion& result);

        /**
         * @brief Creates a quaternion from an axis and an angle in radians.
         *
         * @param axis The axis to rotate around.
         * @param angle The angle in radians.
         * @return The rotation quaternion.
         */
        [[nodiscard]] static Quaternion CreateFromAxisAngle(Vector3 axis, float angle);

        /**
         * @brief Creates a quaternion from an axis and an angle and stores it in an output parameter.
         *
         * @param axis The axis to rotate around.
         * @param angle The angle in radians.
         * @param result Output quaternion that receives the rotation.
         */
        static void CreateFromAxisAngle(const Vector3& axis, float angle, Quaternion& result);

        /**
         * @brief Creates a quaternion from the rotation part of a matrix.
         *
         * @param matrix The rotation matrix.
         * @return A quaternion representing the matrix rotation.
         */
        [[nodiscard]] static Quaternion CreateFromRotationMatrix(Matrix matrix);

        /**
         * @brief Creates a quaternion from the rotation part of a matrix and stores it in an output parameter.
         *
         * @param matrix The rotation matrix.
         * @param result Output quaternion that receives the rotation.
         */
        static void CreateFromRotationMatrix(const Matrix& matrix, Quaternion& result);

        /**
         * @brief Creates a quaternion from yaw, pitch and roll angles in radians.
         *
         * @param yaw Rotation angle around the y-axis in radians.
         * @param pitch Rotation angle around the x-axis in radians.
         * @param roll Rotation angle around the z-axis in radians.
         * @return The rotation quaternion.
         */
        [[nodiscard]] static Quaternion CreateFromYawPitchRoll(float yaw, float pitch, float roll);

        /**
         * @brief Creates a quaternion from yaw, pitch and roll angles and stores it in an output parameter.
         *
         * @param yaw Rotation angle around the y-axis in radians.
         * @param pitch Rotation angle around the x-axis in radians.
         * @param roll Rotation angle around the z-axis in radians.
         * @param result Output quaternion that receives the rotation.
         */
        static void CreateFromYawPitchRoll(float yaw, float pitch, float roll, Quaternion& result);

        /**
         * @brief Divides one quaternion by another.
         *
         * @param quaternion1 Source quaternion.
         * @param quaternion2 Divisor quaternion.
         * @return The quotient quaternion.
         */
        [[nodiscard]] static Quaternion Divide(Quaternion quaternion1, Quaternion quaternion2);

        /**
         * @brief Divides one quaternion by another and stores the result in an output parameter.
         *
         * @param quaternion1 Source quaternion.
         * @param quaternion2 Divisor quaternion.
         * @param result Output quaternion that receives the quotient.
         */
        static void Divide(const Quaternion& quaternion1, const Quaternion& quaternion2, Quaternion& result);

        /**
         * @brief Returns the dot product of two quaternions.
         *
         * @param quaternion1 Source quaternion.
         * @param quaternion2 Source quaternion.
         * @return The dot product of the two quaternions.
         */
        [[nodiscard]] static float Dot(Quaternion quaternion1, Quaternion quaternion2);

        /**
         * @brief Returns the dot product of two quaternions in an output parameter.
         *
         * @param quaternion1 Source quaternion.
         * @param quaternion2 Source quaternion.
         * @param result Output scalar that receives the dot product.
         */
        static void Dot(const Quaternion& quaternion1, const Quaternion& quaternion2, float& result);

        /**
         * @brief Creates the inverse of a quaternion.
         *
         * @param quaternion Source quaternion.
         * @return The inverse quaternion.
         */
        [[nodiscard]] static Quaternion Inverse(Quaternion quaternion);

        /**
         * @brief Creates the inverse of a quaternion and stores it in an output parameter.
         *
         * @param quaternion Source quaternion.
         * @param result Output quaternion that receives the inverse.
         */
        static void Inverse(const Quaternion& quaternion, Quaternion& result);

        /**
         * @brief Performs normalized linear interpolation between two quaternions.
         *
         * @param quaternion1 Source quaternion.
         * @param quaternion2 Destination quaternion.
         * @param amount Interpolation weight between 0 and 1.
         * @return The normalized interpolated quaternion.
         */
        [[nodiscard]] static Quaternion Lerp(Quaternion quaternion1, Quaternion quaternion2, float amount);

        /**
         * @brief Performs normalized linear interpolation and stores the result in an output parameter.
         *
         * @param quaternion1 Source quaternion.
         * @param quaternion2 Destination quaternion.
         * @param amount Interpolation weight.
         * @param result Output quaternion that receives the result.
         */
        static void Lerp(const Quaternion& quaternion1, const Quaternion& quaternion2, float amount,
                         Quaternion& result);

        /**
         * @brief Performs spherical linear interpolation between two quaternions.
         *
         * @param quaternion1 Source quaternion.
         * @param quaternion2 Destination quaternion.
         * @param amount Interpolation weight between 0 and 1.
         * @return The spherically interpolated quaternion.
         */
        [[nodiscard]] static Quaternion Slerp(Quaternion quaternion1, Quaternion quaternion2, float amount);

        /**
         * @brief Performs spherical linear interpolation and stores the result in an output parameter.
         *
         * @param quaternion1 Source quaternion.
         * @param quaternion2 Destination quaternion.
         * @param amount Interpolation weight.
         * @param result Output quaternion that receives the result.
         */
        static void Slerp(const Quaternion& quaternion1, const Quaternion& quaternion2, float amount,
                          Quaternion& result);

        /**
         * @brief Subtracts one quaternion from another.
         *
         * @param quaternion1 Source quaternion.
         * @param quaternion2 Quaternion to subtract.
         * @return The difference quaternion.
         */
        [[nodiscard]] static Quaternion Subtract(Quaternion quaternion1, Quaternion quaternion2);

        /**
         * @brief Subtracts one quaternion from another and stores the result in an output parameter.
         *
         * @param quaternion1 Source quaternion.
         * @param quaternion2 Quaternion to subtract.
         * @param result Output quaternion that receives the difference.
         */
        static void Subtract(const Quaternion& quaternion1, const Quaternion& quaternion2, Quaternion& result);

        /**
         * @brief Multiplies two quaternions.
         *
         * @param quaternion1 Left-hand quaternion.
         * @param quaternion2 Right-hand quaternion.
         * @return The quaternion product.
         */
        [[nodiscard]] static Quaternion Multiply(Quaternion quaternion1, Quaternion quaternion2);

        /**
         * @brief Multiplies a quaternion by a scalar.
         *
         * @param quaternion1 Source quaternion.
         * @param scaleFactor Scalar multiplier.
         * @return The scaled quaternion.
         */
        [[nodiscard]] static Quaternion Multiply(Quaternion quaternion1, float scaleFactor);

        /**
         * @brief Multiplies two quaternions and stores the result in an output parameter.
         *
         * @param quaternion1 Left-hand quaternion.
         * @param quaternion2 Right-hand quaternion.
         * @param result Output quaternion that receives the product.
         */
        static void Multiply(const Quaternion& quaternion1, const Quaternion& quaternion2, Quaternion& result);

        /**
         * @brief Multiplies a quaternion by a scalar and stores the result in an output parameter.
         *
         * @param quaternion1 Source quaternion.
         * @param scaleFactor Scalar multiplier.
         * @param result Output quaternion that receives the result.
         */
        static void Multiply(const Quaternion& quaternion1, float scaleFactor, Quaternion& result);

        /**
         * @brief Negates all four components of a quaternion.
         *
         * @param quaternion Source quaternion.
         * @return The negated quaternion.
         */
        [[nodiscard]] static Quaternion Negate(Quaternion quaternion);

        /**
         * @brief Negates all four components and stores the result in an output parameter.
         *
         * @param quaternion Source quaternion.
         * @param result Output quaternion that receives the negated components.
         */
        static void Negate(const Quaternion& quaternion, Quaternion& result);

        /**
         * @brief Returns a unit-length copy of a quaternion.
         *
         * @param quaternion Source quaternion.
         * @return The normalized quaternion.
         */
        [[nodiscard]] static Quaternion Normalize(Quaternion quaternion);

        /**
         * @brief Normalizes a quaternion and stores the result in an output parameter.
         *
         * @param quaternion Source quaternion.
         * @param result Output quaternion that receives the normalized result.
         */
        static void Normalize(const Quaternion& quaternion, Quaternion& result);

        /**
         * @brief Adds two quaternions component-wise.
         *
         * @param quaternion1 Left-hand quaternion.
         * @param quaternion2 Right-hand quaternion.
         * @return The component-wise sum.
         */
        friend Quaternion operator+(Quaternion quaternion1, Quaternion quaternion2);

        /**
         * @brief Divides one quaternion by another.
         *
         * @param quaternion1 Left-hand quaternion.
         * @param quaternion2 Right-hand quaternion.
         * @return The quotient quaternion.
         */
        friend Quaternion operator/(Quaternion quaternion1, Quaternion quaternion2);

        /**
         * @brief Returns true when all four components are equal.
         *
         * @param quaternion1 Left-hand quaternion.
         * @param quaternion2 Right-hand quaternion.
         * @return @c true if the quaternions are equal; @c false otherwise.
         */
        friend bool operator==(Quaternion quaternion1, Quaternion quaternion2);

        /**
         * @brief Returns true when any component differs.
         *
         * @param quaternion1 Left-hand quaternion.
         * @param quaternion2 Right-hand quaternion.
         * @return @c true if the quaternions are not equal; @c false otherwise.
         */
        friend bool operator!=(Quaternion quaternion1, Quaternion quaternion2);

        /**
         * @brief Multiplies two quaternions.
         *
         * @param quaternion1 Left-hand quaternion.
         * @param quaternion2 Right-hand quaternion.
         * @return The quaternion product.
         */
        friend Quaternion operator*(Quaternion quaternion1, Quaternion quaternion2);

        /**
         * @brief Multiplies all components of a quaternion by a scalar.
         *
         * @param quaternion1 Source quaternion.
         * @param scaleFactor Scalar multiplier.
         * @return The scaled quaternion.
         */
        friend Quaternion operator*(Quaternion quaternion1, float scaleFactor);

        /**
         * @brief Subtracts one quaternion from another component-wise.
         *
         * @param quaternion1 Left-hand quaternion.
         * @param quaternion2 Right-hand quaternion.
         * @return The component-wise difference.
         */
        friend Quaternion operator-(Quaternion quaternion1, Quaternion quaternion2);

        /**
         * @brief Negates all four components of a quaternion.
         *
         * @param quaternion Source quaternion.
         * @return The negated quaternion.
         */
        friend Quaternion operator-(Quaternion quaternion);

    private:
        [[nodiscard]] std::string getDebugDisplayStringProperty() const;
        void CheckForNaNs() const;
    };
}
