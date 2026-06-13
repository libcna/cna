// SPDX-License-Identifier: MS-PL

#pragma once

#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

namespace Microsoft::Xna::Framework
{
    struct Matrix;
    struct Quaternion;

    /** @brief Describes a four-component floating point vector. */
    struct Vector4
    {
        /** @brief Vector with all components set to zero. */
        static const Vector4 Zero;
        /** @brief Vector with all components set to one. */
        static const Vector4 One;
        /** @brief Unit vector along the X axis (1, 0, 0, 0). */
        static const Vector4 UnitX;
        /** @brief Unit vector along the Y axis (0, 1, 0, 0). */
        static const Vector4 UnitY;
        /** @brief Unit vector along the Z axis (0, 0, 1, 0). */
        static const Vector4 UnitZ;
        /** @brief Unit vector along the W axis (0, 0, 0, 1). */
        static const Vector4 UnitW;

        /** @brief X component. */
        float X;
        /** @brief Y component. */
        float Y;
        /** @brief Z component. */
        float Z;
        /** @brief W component. */
        float W;

        /** @brief Creates a zero vector. */
        Vector4();

        /**
         * @brief Creates a vector from four component values.
         *
         * @param x The X component.
         * @param y The Y component.
         * @param z The Z component.
         * @param w The W component.
         */
        Vector4(float x, float y, float z, float w);

        /**
         * @brief Creates a vector from a Vector2 and two scalar values.
         *
         * @param value The X and Y components.
         * @param z The Z component.
         * @param w The W component.
         */
        Vector4(Vector2 value, float z, float w);

        /**
         * @brief Creates a vector from a Vector3 and a W value.
         *
         * @param value The X, Y and Z components.
         * @param w The W component.
         */
        Vector4(Vector3 value, float w);

        /**
         * @brief Creates a vector with all four components set to the same value.
         *
         * @param value The value to assign to all components.
         */
        explicit Vector4(float value);

        /**
         * @brief Compares this vector with another for equality.
         *
         * @param other The vector to compare against.
         * @return @c true if the vectors are equal; @c false otherwise.
         */
        [[nodiscard]] bool Equals(const Vector4& other) const;

        /**
         * @brief Returns a hash code for this vector.
         *
         * @return Hash code of this vector.
         */
        [[nodiscard]] int GetHashCode() const;

        /**
         * @brief Returns the length of this vector.
         *
         * @return The length of this vector.
         */
        [[nodiscard]] float Length() const;

        /**
         * @brief Returns the squared length of this vector.
         *
         * @return The squared length of this vector.
         */
        [[nodiscard]] float LengthSquared() const;

        /** @brief Turns this vector into a unit vector with the same direction. */
        void Normalize();

        /**
         * @brief Returns a string in the form {X:... Y:... Z:... W:...}.
         *
         * @return String representation of this vector.
         */
        [[nodiscard]] std::string ToString() const;

        /**
         * @brief Adds two vectors.
         *
         * @param value1 Source vector.
         * @param value2 Source vector.
         * @return Sum of the vectors.
         */
        [[nodiscard]] static Vector4 Add(Vector4 value1, Vector4 value2);

        /**
         * @brief Stores the sum of two vectors in an output parameter.
         *
         * @param value1 Source vector.
         * @param value2 Source vector.
         * @param result Output vector that receives the sum.
         */
        static void Add(const Vector4& value1, const Vector4& value2, Vector4& result);

        /**
         * @brief Converts barycentric coordinates to a point inside a triangle.
         *
         * @param value1 The first vertex of the triangle.
         * @param value2 The second vertex of the triangle.
         * @param value3 The third vertex of the triangle.
         * @param amount1 Barycentric scalar b2 that represents a weighting toward vertex 2.
         * @param amount2 Barycentric scalar b3 that represents a weighting toward vertex 3.
         * @return The Cartesian translation of the barycentric coordinates.
         */
        [[nodiscard]] static Vector4 Barycentric(Vector4 value1, Vector4 value2, Vector4 value3, float amount1,
                                                 float amount2);

        /**
         * @brief Stores the barycentric result in an output parameter.
         *
         * @param value1 The first vertex of the triangle.
         * @param value2 The second vertex of the triangle.
         * @param value3 The third vertex of the triangle.
         * @param amount1 Barycentric scalar b2.
         * @param amount2 Barycentric scalar b3.
         * @param result Output vector that receives the result.
         */
        static void Barycentric(const Vector4& value1, const Vector4& value2, const Vector4& value3, float amount1,
                                float amount2, Vector4& result);

        /**
         * @brief Performs Catmull-Rom interpolation between vectors.
         *
         * @param value1 The first position in the interpolation.
         * @param value2 The second position in the interpolation.
         * @param value3 The third position in the interpolation.
         * @param value4 The fourth position in the interpolation.
         * @param amount Weighting factor.
         * @return A vector that is the result of the Catmull-Rom interpolation.
         */
        [[nodiscard]] static Vector4 CatmullRom(Vector4 value1, Vector4 value2, Vector4 value3, Vector4 value4,
                                                float amount);

        /**
         * @brief Stores the Catmull-Rom result in an output parameter.
         *
         * @param value1 The first position in the interpolation.
         * @param value2 The second position in the interpolation.
         * @param value3 The third position in the interpolation.
         * @param value4 The fourth position in the interpolation.
         * @param amount Weighting factor.
         * @param result Output vector that receives the result.
         */
        static void CatmullRom(const Vector4& value1, const Vector4& value2, const Vector4& value3,
                               const Vector4& value4, float amount, Vector4& result);

        /**
         * @brief Clamps each component between the matching minimum and maximum component.
         *
         * @param value1 The vector to clamp.
         * @param min Minimum value vector.
         * @param max Maximum value vector.
         * @return The clamped vector.
         */
        [[nodiscard]] static Vector4 Clamp(Vector4 value1, Vector4 min, Vector4 max);

        /**
         * @brief Stores the clamped result in an output parameter.
         *
         * @param value1 The vector to clamp.
         * @param min Minimum value vector.
         * @param max Maximum value vector.
         * @param result Output vector that receives the clamped result.
         */
        static void Clamp(const Vector4& value1, const Vector4& min, const Vector4& max, Vector4& result);

        /**
         * @brief Returns the distance between two vectors.
         *
         * @param value1 Source vector.
         * @param value2 Source vector.
         * @return The distance between the vectors.
         */
        [[nodiscard]] static float Distance(Vector4 value1, Vector4 value2);

        /**
         * @brief Stores the distance between two vectors in an output parameter.
         *
         * @param value1 Source vector.
         * @param value2 Source vector.
         * @param result Output scalar that receives the distance.
         */
        static void Distance(const Vector4& value1, const Vector4& value2, float& result);

        /**
         * @brief Returns the squared distance between two vectors.
         *
         * @param value1 Source vector.
         * @param value2 Source vector.
         * @return The squared distance between the vectors.
         */
        [[nodiscard]] static float DistanceSquared(Vector4 value1, Vector4 value2);

        /**
         * @brief Stores the squared distance between two vectors in an output parameter.
         *
         * @param value1 Source vector.
         * @param value2 Source vector.
         * @param result Output scalar that receives the squared distance.
         */
        static void DistanceSquared(const Vector4& value1, const Vector4& value2, float& result);

        /**
         * @brief Divides a vector component-wise by another vector.
         *
         * @param value1 Source vector.
         * @param value2 Divisor vector.
         * @return The component-wise quotient.
         */
        [[nodiscard]] static Vector4 Divide(Vector4 value1, Vector4 value2);

        /**
         * @brief Stores the component-wise division result in an output parameter.
         *
         * @param value1 Source vector.
         * @param value2 Divisor vector.
         * @param result Output vector that receives the quotient.
         */
        static void Divide(const Vector4& value1, const Vector4& value2, Vector4& result);

        /**
         * @brief Divides a vector by a scalar.
         *
         * @param value1 Source vector.
         * @param divider Divisor scalar.
         * @return The scaled vector.
         */
        [[nodiscard]] static Vector4 Divide(Vector4 value1, float divider);

        /**
         * @brief Stores the scalar division result in an output parameter.
         *
         * @param value1 Source vector.
         * @param divider Divisor scalar.
         * @param result Output vector that receives the result.
         */
        static void Divide(const Vector4& value1, float divider, Vector4& result);

        /**
         * @brief Returns the dot product of two vectors.
         *
         * @param value1 Source vector.
         * @param value2 Source vector.
         * @return The dot product of the two vectors.
         */
        [[nodiscard]] static float Dot(Vector4 value1, Vector4 value2);

        /**
         * @brief Stores the dot product of two vectors in an output parameter.
         *
         * @param value1 Source vector.
         * @param value2 Source vector.
         * @param result Output scalar that receives the dot product.
         */
        static void Dot(const Vector4& value1, const Vector4& value2, float& result);

        /**
         * @brief Performs Hermite spline interpolation.
         *
         * @param value1 Source position vector.
         * @param tangent1 Source tangent vector.
         * @param value2 Destination position vector.
         * @param tangent2 Destination tangent vector.
         * @param amount Weighting factor.
         * @return The result of the Hermite spline interpolation.
         */
        [[nodiscard]] static Vector4 Hermite(Vector4 value1, Vector4 tangent1, Vector4 value2, Vector4 tangent2,
                                             float amount);

        /**
         * @brief Stores the Hermite spline result in an output parameter.
         *
         * @param value1 Source position vector.
         * @param tangent1 Source tangent vector.
         * @param value2 Destination position vector.
         * @param tangent2 Destination tangent vector.
         * @param amount Weighting factor.
         * @param result Output vector that receives the result.
         */
        static void Hermite(const Vector4& value1, const Vector4& tangent1, const Vector4& value2,
                            const Vector4& tangent2, float amount, Vector4& result);

        /**
         * @brief Linearly interpolates between two vectors.
         *
         * @param value1 Source vector.
         * @param value2 Destination vector.
         * @param amount Value between 0 and 1 indicating the interpolation weight.
         * @return The interpolated vector.
         */
        [[nodiscard]] static Vector4 Lerp(Vector4 value1, Vector4 value2, float amount);

        /**
         * @brief Stores the linearly interpolated vector in an output parameter.
         *
         * @param value1 Source vector.
         * @param value2 Destination vector.
         * @param amount Interpolation weight.
         * @param result Output vector that receives the result.
         */
        static void Lerp(const Vector4& value1, const Vector4& value2, float amount, Vector4& result);

        /**
         * @brief Returns the component-wise maximum.
         *
         * @param value1 Source vector.
         * @param value2 Source vector.
         * @return A vector whose components are the per-component maximum of the two inputs.
         */
        [[nodiscard]] static Vector4 Max(Vector4 value1, Vector4 value2);

        /**
         * @brief Stores the component-wise maximum in an output parameter.
         *
         * @param value1 Source vector.
         * @param value2 Source vector.
         * @param result Output vector that receives the per-component maximum.
         */
        static void Max(const Vector4& value1, const Vector4& value2, Vector4& result);

        /**
         * @brief Returns the component-wise minimum.
         *
         * @param value1 Source vector.
         * @param value2 Source vector.
         * @return A vector whose components are the per-component minimum of the two inputs.
         */
        [[nodiscard]] static Vector4 Min(Vector4 value1, Vector4 value2);

        /**
         * @brief Stores the component-wise minimum in an output parameter.
         *
         * @param value1 Source vector.
         * @param value2 Source vector.
         * @param result Output vector that receives the per-component minimum.
         */
        static void Min(const Vector4& value1, const Vector4& value2, Vector4& result);

        /**
         * @brief Multiplies vectors component-wise.
         *
         * @param value1 Source vector.
         * @param value2 Source vector.
         * @return The component-wise product.
         */
        [[nodiscard]] static Vector4 Multiply(Vector4 value1, Vector4 value2);

        /**
         * @brief Stores the component-wise product in an output parameter.
         *
         * @param value1 Source vector.
         * @param value2 Source vector.
         * @param result Output vector that receives the product.
         */
        static void Multiply(const Vector4& value1, const Vector4& value2, Vector4& result);

        /**
         * @brief Multiplies a vector by a scalar.
         *
         * @param value1 Source vector.
         * @param scaleFactor Scalar multiplier.
         * @return The scaled vector.
         */
        [[nodiscard]] static Vector4 Multiply(Vector4 value1, float scaleFactor);

        /**
         * @brief Stores the scalar product in an output parameter.
         *
         * @param value1 Source vector.
         * @param scaleFactor Scalar multiplier.
         * @param result Output vector that receives the product.
         */
        static void Multiply(const Vector4& value1, float scaleFactor, Vector4& result);

        /**
         * @brief Negates all vector components.
         *
         * @param value Source vector.
         * @return A vector with negated components.
         */
        [[nodiscard]] static Vector4 Negate(Vector4 value);

        /**
         * @brief Stores the negated vector in an output parameter.
         *
         * @param value Source vector.
         * @param result Output vector that receives the negated components.
         */
        static void Negate(const Vector4& value, Vector4& result);

        /**
         * @brief Returns a normalized copy of a vector.
         *
         * @param value Source vector.
         * @return The normalized vector.
         */
        [[nodiscard]] static Vector4 Normalize(Vector4 value);

        /**
         * @brief Stores the normalized vector in an output parameter.
         *
         * @param value Source vector.
         * @param result Output vector that receives the normalized result.
         */
        static void Normalize(const Vector4& value, Vector4& result);

        /**
         * @brief Performs smooth Hermite interpolation between two vectors.
         *
         * @param value1 Source vector.
         * @param value2 Destination vector.
         * @param amount Weighting value between 0 and 1.
         * @return The smooth-stepped interpolation result.
         */
        [[nodiscard]] static Vector4 SmoothStep(Vector4 value1, Vector4 value2, float amount);

        /**
         * @brief Stores the smooth-step result in an output parameter.
         *
         * @param value1 Source vector.
         * @param value2 Destination vector.
         * @param amount Weighting value.
         * @param result Output vector that receives the result.
         */
        static void SmoothStep(const Vector4& value1, const Vector4& value2, float amount, Vector4& result);

        /**
         * @brief Subtracts one vector from another.
         *
         * @param value1 Source vector.
         * @param value2 Vector to subtract.
         * @return The difference vector.
         */
        [[nodiscard]] static Vector4 Subtract(Vector4 value1, Vector4 value2);

        /**
         * @brief Stores the difference of two vectors in an output parameter.
         *
         * @param value1 Source vector.
         * @param value2 Vector to subtract.
         * @param result Output vector that receives the difference.
         */
        static void Subtract(const Vector4& value1, const Vector4& value2, Vector4& result);

        /**
         * @brief Transforms a Vector2 position by a matrix, producing a Vector4.
         *
         * @param position Source Vector2 position.
         * @param matrix The transformation matrix.
         * @return The transformed Vector4.
         */
        [[nodiscard]] static Vector4 Transform(Vector2 position, const Matrix& matrix);

        /**
         * @brief Transforms a Vector3 position by a matrix, producing a Vector4.
         *
         * @param position Source Vector3 position.
         * @param matrix The transformation matrix.
         * @return The transformed Vector4.
         */
        [[nodiscard]] static Vector4 Transform(Vector3 position, const Matrix& matrix);

        /**
         * @brief Transforms a Vector4 by a matrix.
         *
         * @param vector Source Vector4.
         * @param matrix The transformation matrix.
         * @return The transformed Vector4.
         */
        [[nodiscard]] static Vector4 Transform(Vector4 vector, const Matrix& matrix);

        /**
         * @brief Stores the matrix-transformed Vector2 result in an output parameter.
         *
         * @param position Source Vector2 position.
         * @param matrix The transformation matrix.
         * @param result Output Vector4 that receives the result.
         */
        static void Transform(const Vector2& position, const Matrix& matrix, Vector4& result);

        /**
         * @brief Stores the matrix-transformed Vector3 result in an output parameter.
         *
         * @param position Source Vector3 position.
         * @param matrix The transformation matrix.
         * @param result Output Vector4 that receives the result.
         */
        static void Transform(const Vector3& position, const Matrix& matrix, Vector4& result);

        /**
         * @brief Stores the matrix-transformed Vector4 result in an output parameter.
         *
         * @param vector Source Vector4.
         * @param matrix The transformation matrix.
         * @param result Output Vector4 that receives the result.
         */
        static void Transform(const Vector4& vector, const Matrix& matrix, Vector4& result);

        /**
         * @brief Transforms an array of Vector4 values by a matrix.
         *
         * @param sourceArray Source array of vectors.
         * @param matrix The transformation matrix.
         * @param destinationArray Output array that receives the transformed vectors.
         */
        static void Transform(const std::vector<Vector4>& sourceArray, const Matrix& matrix,
                              std::vector<Vector4>& destinationArray);

        /**
         * @brief Transforms a range of Vector4 values in an array by a matrix.
         *
         * @param sourceArray Source array of vectors.
         * @param sourceIndex Starting index in the source array.
         * @param matrix The transformation matrix.
         * @param destinationArray Output array that receives the transformed vectors.
         * @param destinationIndex Starting index in the destination array.
         * @param length Number of elements to transform.
         */
        static void Transform(const std::vector<Vector4>& sourceArray, int sourceIndex, const Matrix& matrix,
                              std::vector<Vector4>& destinationArray, int destinationIndex, int length);

        /**
         * @brief Transforms a Vector2 by a quaternion rotation, producing a Vector4.
         *
         * @param value Source Vector2.
         * @param rotation The quaternion rotation to apply.
         * @return The rotated Vector4.
         */
        [[nodiscard]] static Vector4 Transform(Vector2 value, const Quaternion& rotation);

        /**
         * @brief Transforms a Vector3 by a quaternion rotation, producing a Vector4.
         *
         * @param value Source Vector3.
         * @param rotation The quaternion rotation to apply.
         * @return The rotated Vector4.
         */
        [[nodiscard]] static Vector4 Transform(Vector3 value, const Quaternion& rotation);

        /**
         * @brief Transforms a Vector4 by a quaternion rotation.
         *
         * @param value Source Vector4.
         * @param rotation The quaternion rotation to apply.
         * @return The rotated Vector4.
         */
        [[nodiscard]] static Vector4 Transform(Vector4 value, const Quaternion& rotation);

        /**
         * @brief Stores the quaternion-rotated Vector2 result in an output parameter.
         *
         * @param value Source Vector2.
         * @param rotation The quaternion rotation to apply.
         * @param result Output Vector4 that receives the result.
         */
        static void Transform(const Vector2& value, const Quaternion& rotation, Vector4& result);

        /**
         * @brief Stores the quaternion-rotated Vector3 result in an output parameter.
         *
         * @param value Source Vector3.
         * @param rotation The quaternion rotation to apply.
         * @param result Output Vector4 that receives the result.
         */
        static void Transform(const Vector3& value, const Quaternion& rotation, Vector4& result);

        /**
         * @brief Stores the quaternion-rotated Vector4 result in an output parameter.
         *
         * @param value Source Vector4.
         * @param rotation The quaternion rotation to apply.
         * @param result Output Vector4 that receives the result.
         */
        static void Transform(const Vector4& value, const Quaternion& rotation, Vector4& result);

        /**
         * @brief Transforms an array of Vector4 values by a quaternion rotation.
         *
         * @param sourceArray Source array of vectors.
         * @param rotation The quaternion rotation to apply.
         * @param destinationArray Output array that receives the rotated vectors.
         */
        static void Transform(const std::vector<Vector4>& sourceArray, const Quaternion& rotation,
                              std::vector<Vector4>& destinationArray);

        /**
         * @brief Transforms a range of Vector4 values in an array by a quaternion rotation.
         *
         * @param sourceArray Source array of vectors.
         * @param sourceIndex Starting index in the source array.
         * @param rotation The quaternion rotation to apply.
         * @param destinationArray Output array that receives the rotated vectors.
         * @param destinationIndex Starting index in the destination array.
         * @param length Number of elements to transform.
         */
        static void Transform(const std::vector<Vector4>& sourceArray, int sourceIndex, const Quaternion& rotation,
                              std::vector<Vector4>& destinationArray, int destinationIndex, int length);

        /**
         * @brief Negates all components of a vector.
         *
         * @param value Source vector.
         * @return The negated vector.
         */
        friend Vector4 operator-(Vector4 value);

        /**
         * @brief Returns true when all four components are equal.
         *
         * @param value1 Left-hand vector.
         * @param value2 Right-hand vector.
         * @return @c true if the vectors are equal; @c false otherwise.
         */
        friend bool operator==(Vector4 value1, Vector4 value2);

        /**
         * @brief Returns true when any component differs.
         *
         * @param value1 Left-hand vector.
         * @param value2 Right-hand vector.
         * @return @c true if the vectors are not equal; @c false otherwise.
         */
        friend bool operator!=(Vector4 value1, Vector4 value2);

        /**
         * @brief Adds two vectors component-wise.
         *
         * @param value1 Left-hand vector.
         * @param value2 Right-hand vector.
         * @return The component-wise sum.
         */
        friend Vector4 operator+(Vector4 value1, Vector4 value2);

        /**
         * @brief Subtracts one vector from another component-wise.
         *
         * @param value1 Left-hand vector.
         * @param value2 Right-hand vector.
         * @return The component-wise difference.
         */
        friend Vector4 operator-(Vector4 value1, Vector4 value2);

        /**
         * @brief Multiplies two vectors component-wise.
         *
         * @param value1 Left-hand vector.
         * @param value2 Right-hand vector.
         * @return The component-wise product.
         */
        friend Vector4 operator*(Vector4 value1, Vector4 value2);

        /**
         * @brief Multiplies all components of a vector by a scalar.
         *
         * @param value1 Source vector.
         * @param scaleFactor Scalar multiplier.
         * @return The scaled vector.
         */
        friend Vector4 operator*(Vector4 value1, float scaleFactor);

        /**
         * @brief Multiplies all components of a vector by a scalar.
         *
         * @param scaleFactor Scalar multiplier.
         * @param value1 Source vector.
         * @return The scaled vector.
         */
        friend Vector4 operator*(float scaleFactor, Vector4 value1);

        /**
         * @brief Divides one vector by another component-wise.
         *
         * @param value1 Left-hand vector.
         * @param value2 Right-hand vector.
         * @return The component-wise quotient.
         */
        friend Vector4 operator/(Vector4 value1, Vector4 value2);

        /**
         * @brief Divides all components of a vector by a scalar.
         *
         * @param value1 Source vector.
         * @param divider Divisor scalar.
         * @return The scaled vector.
         */
        friend Vector4 operator/(Vector4 value1, float divider);

    private:
        [[nodiscard]] std::string getDebugDisplayStringProperty() const;
        void CheckForNaNs() const;
    };
}
