#pragma once

#include <string>

#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"

namespace Microsoft::Xna::Framework
{
    /// Efficient value type used to represent 3D rotations.
    struct Quaternion
    {
        /// Quaternion that represents no rotation.
        static const Quaternion Identity;

        /// X component of the quaternion vector part.
        float X;

        /// Y component of the quaternion vector part.
        float Y;

        /// Z component of the quaternion vector part.
        float Z;

        /// Scalar rotation component.
        float W;

        /// Constructs a quaternion from four scalar components.
        Quaternion(float x, float y, float z, float w);

        /// Constructs a quaternion from a vector part and a scalar part.
        Quaternion(Vector3 vectorPart, float scalarPart);

        /// Negates the vector part of this quaternion in place.
        void Conjugate();

        /// Compares this quaternion with another quaternion.
        [[nodiscard]] bool Equals(const Quaternion& other) const;

        /// Returns a hash code for this quaternion.
        [[nodiscard]] int GetHashCode() const;

        /// Returns the magnitude of this quaternion.
        [[nodiscard]] float Length() const;

        /// Returns the squared magnitude of this quaternion.
        [[nodiscard]] float LengthSquared() const;

        /// Scales this quaternion to unit length.
        void Normalize();

        /// Returns a string in the form {X:... Y:... Z:... W:...}.
        [[nodiscard]] std::string ToString() const;

        /// Adds two quaternions.
        [[nodiscard]] static Quaternion Add(Quaternion quaternion1, Quaternion quaternion2);

        /// Adds two quaternions and stores the result in an output parameter.
        static void Add(const Quaternion& quaternion1, const Quaternion& quaternion2, Quaternion& result);

        /// Concatenates two rotations so that value1 is followed by value2.
        [[nodiscard]] static Quaternion Concatenate(Quaternion value1, Quaternion value2);

        /// Concatenates two rotations and stores the result in an output parameter.
        static void Concatenate(const Quaternion& value1, const Quaternion& value2, Quaternion& result);

        /// Creates the conjugate of a quaternion.
        [[nodiscard]] static Quaternion Conjugate(Quaternion value);

        /// Creates the conjugate of a quaternion and stores it in an output parameter.
        static void Conjugate(const Quaternion& value, Quaternion& result);

        /// Creates a quaternion from an axis and an angle in radians.
        [[nodiscard]] static Quaternion CreateFromAxisAngle(Vector3 axis, float angle);

        /// Creates a quaternion from an axis and an angle and stores it in an output parameter.
        static void CreateFromAxisAngle(const Vector3& axis, float angle, Quaternion& result);

        /// Creates a quaternion from the rotation part of a matrix.
        [[nodiscard]] static Quaternion CreateFromRotationMatrix(Matrix matrix);

        /// Creates a quaternion from the rotation part of a matrix and stores it in an output parameter.
        static void CreateFromRotationMatrix(const Matrix& matrix, Quaternion& result);

        /// Creates a quaternion from yaw, pitch and roll angles in radians.
        [[nodiscard]] static Quaternion CreateFromYawPitchRoll(float yaw, float pitch, float roll);

        /// Creates a quaternion from yaw, pitch and roll angles and stores it in an output parameter.
        static void CreateFromYawPitchRoll(float yaw, float pitch, float roll, Quaternion& result);

        /// Divides one quaternion by another.
        [[nodiscard]] static Quaternion Divide(Quaternion quaternion1, Quaternion quaternion2);

        /// Divides one quaternion by another and stores the result in an output parameter.
        static void Divide(const Quaternion& quaternion1, const Quaternion& quaternion2, Quaternion& result);

        /// Returns the dot product of two quaternions.
        [[nodiscard]] static float Dot(Quaternion quaternion1, Quaternion quaternion2);

        /// Returns the dot product of two quaternions in an output parameter.
        static void Dot(const Quaternion& quaternion1, const Quaternion& quaternion2, float& result);

        /// Creates the inverse of a quaternion.
        [[nodiscard]] static Quaternion Inverse(Quaternion quaternion);

        /// Creates the inverse of a quaternion and stores it in an output parameter.
        static void Inverse(const Quaternion& quaternion, Quaternion& result);

        /// Performs normalized linear interpolation between two quaternions.
        [[nodiscard]] static Quaternion Lerp(Quaternion quaternion1, Quaternion quaternion2, float amount);

        /// Performs normalized linear interpolation and stores the result in an output parameter.
        static void Lerp(const Quaternion& quaternion1, const Quaternion& quaternion2, float amount, Quaternion& result);

        /// Performs spherical linear interpolation between two quaternions.
        [[nodiscard]] static Quaternion Slerp(Quaternion quaternion1, Quaternion quaternion2, float amount);

        /// Performs spherical linear interpolation and stores the result in an output parameter.
        static void Slerp(const Quaternion& quaternion1, const Quaternion& quaternion2, float amount, Quaternion& result);

        /// Subtracts one quaternion from another.
        [[nodiscard]] static Quaternion Subtract(Quaternion quaternion1, Quaternion quaternion2);

        /// Subtracts one quaternion from another and stores the result in an output parameter.
        static void Subtract(const Quaternion& quaternion1, const Quaternion& quaternion2, Quaternion& result);

        /// Multiplies two quaternions.
        [[nodiscard]] static Quaternion Multiply(Quaternion quaternion1, Quaternion quaternion2);

        /// Multiplies a quaternion by a scalar.
        [[nodiscard]] static Quaternion Multiply(Quaternion quaternion1, float scaleFactor);

        /// Multiplies two quaternions and stores the result in an output parameter.
        static void Multiply(const Quaternion& quaternion1, const Quaternion& quaternion2, Quaternion& result);

        /// Multiplies a quaternion by a scalar and stores the result in an output parameter.
        static void Multiply(const Quaternion& quaternion1, float scaleFactor, Quaternion& result);

        /// Negates all four components of a quaternion.
        [[nodiscard]] static Quaternion Negate(Quaternion quaternion);

        /// Negates all four components and stores the result in an output parameter.
        static void Negate(const Quaternion& quaternion, Quaternion& result);

        /// Returns a unit-length copy of a quaternion.
        [[nodiscard]] static Quaternion Normalize(Quaternion quaternion);

        /// Normalizes a quaternion and stores the result in an output parameter.
        static void Normalize(const Quaternion& quaternion, Quaternion& result);

        friend Quaternion operator+(Quaternion quaternion1, Quaternion quaternion2);
        friend Quaternion operator/(Quaternion quaternion1, Quaternion quaternion2);
        friend bool operator==(Quaternion quaternion1, Quaternion quaternion2);
        friend bool operator!=(Quaternion quaternion1, Quaternion quaternion2);
        friend Quaternion operator*(Quaternion quaternion1, Quaternion quaternion2);
        friend Quaternion operator*(Quaternion quaternion1, float scaleFactor);
        friend Quaternion operator-(Quaternion quaternion1, Quaternion quaternion2);
        friend Quaternion operator-(Quaternion quaternion);

    private:
        [[nodiscard]] std::string getDebugDisplayStringProperty() const;
        void CheckForNaNs() const;
    };
}
