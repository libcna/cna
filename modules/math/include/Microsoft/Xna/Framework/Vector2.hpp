// SPDX-License-Identifier: MS-PL

#pragma once

#include <string>
#include <vector>

#include "CNA/CNAHelper.hpp"

namespace Microsoft::Xna::Framework
{
    struct Matrix;
    struct Quaternion;

    /** @brief Describes a two-component floating point vector. */
    struct Vector2
    {
        /** @brief Vector with both components set to zero. */
        static const Vector2 Zero;

        /** @brief Vector with both components set to one. */
        static const Vector2 One;

        /** @brief Unit vector on the X axis. */
        static const Vector2 UnitX;

        /** @brief Unit vector on the Y axis. */
        static const Vector2 UnitY;

        /** @brief X component. */
        float X;

        /** @brief Y component. */
        float Y;

        /** @brief Creates a zero vector. */
        Vector2();

        /**
         * @brief Creates a vector from two component values.
         *
         * @param x The X component.
         * @param y The Y component.
         */
        constexpr Vector2(float x, float y) : X(x), Y(y) {}

        /**
         * @brief Creates a vector whose components are both set to the same value.
         *
         * @param value The value to assign to both components.
         */
        explicit Vector2(float value);

        /**
         * @brief Compares this vector with another vector.
         *
         * @param other The vector to compare against.
         * @return @c true if the vectors are equal; @c false otherwise.
         */
        [[nodiscard]] bool Equals(const Vector2& other) const;

        /**
         * @brief Returns a hash code for this vector.
         *
         * @return Hash code of this vector.
         */
        [[nodiscard]] int GetHashCode() const;

        /**
         * @brief Returns the vector length.
         *
         * @return The length of this vector.
         */
        [[nodiscard]] float Length() const;

        /**
         * @brief Returns the squared vector length.
         *
         * @return The squared length of this vector.
         */
        [[nodiscard]] float LengthSquared() const;

        /** @brief Normalizes this vector in place. */
        void Normalize();

        /**
         * @brief Returns a string in the form {X:... Y:...}.
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
        [[nodiscard]] static Vector2 Add(Vector2 value1, Vector2 value2);

        /**
         * @brief Stores the sum of two vectors in an output parameter.
         *
         * @param value1 Source vector.
         * @param value2 Source vector.
         * @param result Output vector that receives the sum.
         */
        static void Add(const Vector2& value1, const Vector2& value2, Vector2& result);

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
        [[nodiscard]] static Vector2 Barycentric(Vector2 value1, Vector2 value2, Vector2 value3, float amount1,
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
        static void Barycentric(const Vector2& value1, const Vector2& value2, const Vector2& value3, float amount1,
                                float amount2, Vector2& result);

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
        [[nodiscard]] static Vector2 CatmullRom(Vector2 value1, Vector2 value2, Vector2 value3, Vector2 value4,
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
        static void CatmullRom(const Vector2& value1, const Vector2& value2, const Vector2& value3,
                               const Vector2& value4, float amount, Vector2& result);

        /**
         * @brief Clamps each component between the matching minimum and maximum component.
         *
         * @param value1 The vector to clamp.
         * @param min Minimum value vector.
         * @param max Maximum value vector.
         * @return The clamped vector.
         */
        [[nodiscard]] static Vector2 Clamp(Vector2 value1, Vector2 min, Vector2 max);

        /**
         * @brief Stores the clamped result in an output parameter.
         *
         * @param value1 The vector to clamp.
         * @param min Minimum value vector.
         * @param max Maximum value vector.
         * @param result Output vector that receives the clamped result.
         */
        static void Clamp(const Vector2& value1, const Vector2& min, const Vector2& max, Vector2& result);

        /**
         * @brief Returns the distance between two vectors.
         *
         * @param value1 Source vector.
         * @param value2 Source vector.
         * @return The distance between the vectors.
         */
        [[nodiscard]] static float Distance(Vector2 value1, Vector2 value2);

        /**
         * @brief Stores the distance between two vectors in an output parameter.
         *
         * @param value1 Source vector.
         * @param value2 Source vector.
         * @param result Output scalar that receives the distance.
         */
        static void Distance(const Vector2& value1, const Vector2& value2, float& result);

        /**
         * @brief Returns the squared distance between two vectors.
         *
         * @param value1 Source vector.
         * @param value2 Source vector.
         * @return The squared distance between the vectors.
         */
        [[nodiscard]] static float DistanceSquared(Vector2 value1, Vector2 value2);

        /**
         * @brief Stores the squared distance between two vectors in an output parameter.
         *
         * @param value1 Source vector.
         * @param value2 Source vector.
         * @param result Output scalar that receives the squared distance.
         */
        static void DistanceSquared(const Vector2& value1, const Vector2& value2, float& result);

        /**
         * @brief Divides a vector component-wise by another vector.
         *
         * @param value1 Source vector.
         * @param value2 Divisor vector.
         * @return The component-wise quotient.
         */
        [[nodiscard]] static Vector2 Divide(Vector2 value1, Vector2 value2);

        /**
         * @brief Stores the component-wise division result in an output parameter.
         *
         * @param value1 Source vector.
         * @param value2 Divisor vector.
         * @param result Output vector that receives the quotient.
         */
        static void Divide(const Vector2& value1, const Vector2& value2, Vector2& result);

        /**
         * @brief Divides a vector by a scalar.
         *
         * @param value1 Source vector.
         * @param divider Divisor scalar.
         * @return The scaled vector.
         */
        [[nodiscard]] static Vector2 Divide(Vector2 value1, float divider);

        /**
         * @brief Stores the scalar division result in an output parameter.
         *
         * @param value1 Source vector.
         * @param divider Divisor scalar.
         * @param result Output vector that receives the result.
         */
        static void Divide(const Vector2& value1, float divider, Vector2& result);

        /**
         * @brief Returns the dot product of two vectors.
         *
         * @param value1 Source vector.
         * @param value2 Source vector.
         * @return The dot product of the two vectors.
         */
        [[nodiscard]] static float Dot(Vector2 value1, Vector2 value2);

        /**
         * @brief Stores the dot product of two vectors in an output parameter.
         *
         * @param value1 Source vector.
         * @param value2 Source vector.
         * @param result Output scalar that receives the dot product.
         */
        static void Dot(const Vector2& value1, const Vector2& value2, float& result);

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
        [[nodiscard]] static Vector2 Hermite(Vector2 value1, Vector2 tangent1, Vector2 value2, Vector2 tangent2,
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
        static void Hermite(const Vector2& value1, const Vector2& tangent1, const Vector2& value2,
                            const Vector2& tangent2, float amount, Vector2& result);

        /**
         * @brief Linearly interpolates between two vectors.
         *
         * @param value1 Source vector.
         * @param value2 Destination vector.
         * @param amount Value between 0 and 1 indicating the interpolation weight.
         * @return The interpolated vector.
         */
        [[nodiscard]] static Vector2 Lerp(Vector2 value1, Vector2 value2, float amount);

        /**
         * @brief Stores the linearly interpolated vector in an output parameter.
         *
         * @param value1 Source vector.
         * @param value2 Destination vector.
         * @param amount Interpolation weight.
         * @param result Output vector that receives the result.
         */
        static void Lerp(const Vector2& value1, const Vector2& value2, float amount, Vector2& result);

        /**
         * @brief Returns the component-wise maximum.
         *
         * @param value1 Source vector.
         * @param value2 Source vector.
         * @return A vector whose components are the per-component maximum of the two inputs.
         */
        [[nodiscard]] static Vector2 Max(Vector2 value1, Vector2 value2);

        /**
         * @brief Stores the component-wise maximum in an output parameter.
         *
         * @param value1 Source vector.
         * @param value2 Source vector.
         * @param result Output vector that receives the per-component maximum.
         */
        static void Max(const Vector2& value1, const Vector2& value2, Vector2& result);

        /**
         * @brief Returns the component-wise minimum.
         *
         * @param value1 Source vector.
         * @param value2 Source vector.
         * @return A vector whose components are the per-component minimum of the two inputs.
         */
        [[nodiscard]] static Vector2 Min(Vector2 value1, Vector2 value2);

        /**
         * @brief Stores the component-wise minimum in an output parameter.
         *
         * @param value1 Source vector.
         * @param value2 Source vector.
         * @param result Output vector that receives the per-component minimum.
         */
        static void Min(const Vector2& value1, const Vector2& value2, Vector2& result);

        /**
         * @brief Multiplies vectors component-wise.
         *
         * @param value1 Source vector.
         * @param value2 Source vector.
         * @return The component-wise product.
         */
        [[nodiscard]] static Vector2 Multiply(Vector2 value1, Vector2 value2);

        /**
         * @brief Stores the component-wise product in an output parameter.
         *
         * @param value1 Source vector.
         * @param value2 Source vector.
         * @param result Output vector that receives the product.
         */
        static void Multiply(const Vector2& value1, const Vector2& value2, Vector2& result);

        /**
         * @brief Multiplies a vector by a scalar.
         *
         * @param value1 Source vector.
         * @param scaleFactor Scalar multiplier.
         * @return The scaled vector.
         */
        [[nodiscard]] static Vector2 Multiply(Vector2 value1, float scaleFactor);

        /**
         * @brief Stores the scalar product in an output parameter.
         *
         * @param value1 Source vector.
         * @param scaleFactor Scalar multiplier.
         * @param result Output vector that receives the product.
         */
        static void Multiply(const Vector2& value1, float scaleFactor, Vector2& result);

        /**
         * @brief Negates both vector components.
         *
         * @param value Source vector.
         * @return A vector with negated components.
         */
        [[nodiscard]] static Vector2 Negate(Vector2 value);

        /**
         * @brief Stores the negated vector in an output parameter.
         *
         * @param value Source vector.
         * @param result Output vector that receives the negated components.
         */
        static void Negate(const Vector2& value, Vector2& result);

        /**
         * @brief Returns a normalized copy of a vector.
         *
         * @param value Source vector.
         * @return The normalized vector.
         */
        [[nodiscard]] static Vector2 Normalize(Vector2 value);

        /**
         * @brief Stores the normalized vector in an output parameter.
         *
         * @param value Source vector.
         * @param result Output vector that receives the normalized result.
         */
        static void Normalize(const Vector2& value, Vector2& result);

        /**
         * @brief Reflects a vector across a normal.
         *
         * @param vector Source vector.
         * @param normal Normal of the surface to reflect across.
         * @return The reflected vector.
         */
        [[nodiscard]] static Vector2 Reflect(Vector2 vector, Vector2 normal);

        /**
         * @brief Stores the reflected vector in an output parameter.
         *
         * @param vector Source vector.
         * @param normal Normal of the surface to reflect across.
         * @param result Output vector that receives the reflected result.
         */
        static void Reflect(const Vector2& vector, const Vector2& normal, Vector2& result);

        /**
         * @brief Performs smooth Hermite interpolation between two vectors.
         *
         * @param value1 Source vector.
         * @param value2 Destination vector.
         * @param amount Weighting value between 0 and 1.
         * @return The smooth-stepped interpolation result.
         */
        [[nodiscard]] static Vector2 SmoothStep(Vector2 value1, Vector2 value2, float amount);

        /**
         * @brief Stores the smooth-step result in an output parameter.
         *
         * @param value1 Source vector.
         * @param value2 Destination vector.
         * @param amount Weighting value.
         * @param result Output vector that receives the result.
         */
        static void SmoothStep(const Vector2& value1, const Vector2& value2, float amount, Vector2& result);

        /**
         * @brief Subtracts one vector from another.
         *
         * @param value1 Source vector.
         * @param value2 Vector to subtract.
         * @return The difference vector.
         */
        [[nodiscard]] static Vector2 Subtract(Vector2 value1, Vector2 value2);

        /**
         * @brief Stores the difference of two vectors in an output parameter.
         *
         * @param value1 Source vector.
         * @param value2 Vector to subtract.
         * @param result Output vector that receives the difference.
         */
        static void Subtract(const Vector2& value1, const Vector2& value2, Vector2& result);

        /**
         * @brief Transforms a position by a matrix.
         *
         * @param position Source position vector.
         * @param matrix The transformation matrix.
         * @return The transformed position.
         */
        [[nodiscard]] static Vector2 Transform(Vector2 position, const Matrix& matrix);

        /**
         * @brief Stores the matrix-transformed position in an output parameter.
         *
         * @param position Source position vector.
         * @param matrix The transformation matrix.
         * @param result Output vector that receives the transformed position.
         */
        static void Transform(const Vector2& position, const Matrix& matrix, Vector2& result);

        /**
         * @brief Transforms an array of positions by a matrix.
         *
         * @param sourceArray Source array of vectors.
         * @param matrix The transformation matrix.
         * @param destinationArray Output array that receives the transformed vectors.
         */
        static void Transform(const std::vector<Vector2>& sourceArray, const Matrix& matrix,
                              std::vector<Vector2>& destinationArray);

        /**
         * @brief Transforms a range of positions in an array by a matrix.
         *
         * @param sourceArray Source array of vectors.
         * @param sourceIndex Starting index in the source array.
         * @param matrix The transformation matrix.
         * @param destinationArray Output array that receives the transformed vectors.
         * @param destinationIndex Starting index in the destination array.
         * @param length Number of elements to transform.
         */
        static void Transform(const std::vector<Vector2>& sourceArray, int sourceIndex, const Matrix& matrix,
                              std::vector<Vector2>& destinationArray, int destinationIndex, int length);

        /**
         * @brief Transforms a vector by a quaternion rotation.
         *
         * @param value Source vector.
         * @param rotation The quaternion rotation to apply.
         * @return The rotated vector.
         */
        [[nodiscard]] static Vector2 Transform(Vector2 value, const Quaternion& rotation);

        /**
         * @brief Stores the quaternion-rotated vector in an output parameter.
         *
         * @param value Source vector.
         * @param rotation The quaternion rotation to apply.
         * @param result Output vector that receives the rotated result.
         */
        static void Transform(const Vector2& value, const Quaternion& rotation, Vector2& result);

        /**
         * @brief Transforms an array of vectors by a quaternion rotation.
         *
         * @param sourceArray Source array of vectors.
         * @param rotation The quaternion rotation to apply.
         * @param destinationArray Output array that receives the rotated vectors.
         */
        static void Transform(const std::vector<Vector2>& sourceArray, const Quaternion& rotation,
                              std::vector<Vector2>& destinationArray);

        /**
         * @brief Transforms a range of vectors in an array by a quaternion rotation.
         *
         * @param sourceArray Source array of vectors.
         * @param sourceIndex Starting index in the source array.
         * @param rotation The quaternion rotation to apply.
         * @param destinationArray Output array that receives the rotated vectors.
         * @param destinationIndex Starting index in the destination array.
         * @param length Number of elements to transform.
         */
        static void Transform(const std::vector<Vector2>& sourceArray, int sourceIndex, const Quaternion& rotation,
                              std::vector<Vector2>& destinationArray, int destinationIndex, int length);

        /**
         * @brief Transforms a normal by a matrix without applying translation.
         *
         * @param normal Source normal vector.
         * @param matrix The transformation matrix.
         * @return The transformed normal.
         */
        [[nodiscard]] static Vector2 TransformNormal(Vector2 normal, const Matrix& matrix);

        /**
         * @brief Stores the matrix-transformed normal in an output parameter.
         *
         * @param normal Source normal vector.
         * @param matrix The transformation matrix.
         * @param result Output vector that receives the transformed normal.
         */
        static void TransformNormal(const Vector2& normal, const Matrix& matrix, Vector2& result);

        /**
         * @brief Transforms an array of normals by a matrix.
         *
         * @param sourceArray Source array of normals.
         * @param matrix The transformation matrix.
         * @param destinationArray Output array that receives the transformed normals.
         */
        static void TransformNormal(const std::vector<Vector2>& sourceArray, const Matrix& matrix,
                                    std::vector<Vector2>& destinationArray);

        /**
         * @brief Transforms a range of normals in an array by a matrix.
         *
         * @param sourceArray Source array of normals.
         * @param sourceIndex Starting index in the source array.
         * @param matrix The transformation matrix.
         * @param destinationArray Output array that receives the transformed normals.
         * @param destinationIndex Starting index in the destination array.
         * @param length Number of elements to transform.
         */
        static void TransformNormal(const std::vector<Vector2>& sourceArray, int sourceIndex, const Matrix& matrix,
                                    std::vector<Vector2>& destinationArray, int destinationIndex, int length);

        /**
         * @brief Adds another vector to this vector in place.
         *
         * C# synthesises `+=` from the declared `op_Addition`, so XNA game code writes
         * `position += delta` without the type declaring anything; C++ has to spell it out.
         * Marked CNAEXT because the XNA reference declares no such operator of its own --
         * the same treatment Vector3 already gives its pair.
         *
         * @param value The vector to add.
         * @return Reference to this vector after addition.
         */
        CNAEXT Vector2& operator+=(const Vector2& value);

        /**
         * @brief Subtracts another vector from this vector in place.
         *
         * The `-=` counterpart of operator+=; see its note for why this is CNAEXT.
         *
         * @param value The vector to subtract.
         * @return Reference to this vector after subtraction.
         */
        CNAEXT Vector2& operator-=(const Vector2& value);

        /**
         * @brief Negates all components of a vector.
         *
         * @param value Source vector.
         * @return The negated vector.
         */
        friend Vector2 operator-(Vector2 value);

        /**
         * @brief Returns true when both vectors have equal components.
         *
         * @param value1 Left-hand vector.
         * @param value2 Right-hand vector.
         * @return @c true if the vectors are equal; @c false otherwise.
         */
        friend bool operator==(Vector2 value1, Vector2 value2);

        /**
         * @brief Returns true when any component differs.
         *
         * @param value1 Left-hand vector.
         * @param value2 Right-hand vector.
         * @return @c true if the vectors are not equal; @c false otherwise.
         */
        friend bool operator!=(Vector2 value1, Vector2 value2);

        /**
         * @brief Adds two vectors component-wise.
         *
         * @param value1 Left-hand vector.
         * @param value2 Right-hand vector.
         * @return The component-wise sum.
         */
        friend Vector2 operator+(Vector2 value1, Vector2 value2);

        /**
         * @brief Subtracts one vector from another component-wise.
         *
         * @param value1 Left-hand vector.
         * @param value2 Right-hand vector.
         * @return The component-wise difference.
         */
        friend Vector2 operator-(Vector2 value1, Vector2 value2);

        /**
         * @brief Multiplies two vectors component-wise.
         *
         * @param value1 Left-hand vector.
         * @param value2 Right-hand vector.
         * @return The component-wise product.
         */
        friend Vector2 operator*(Vector2 value1, Vector2 value2);

        /**
         * @brief Multiplies all components of a vector by a scalar.
         *
         * @param value Source vector.
         * @param scaleFactor Scalar multiplier.
         * @return The scaled vector.
         */
        friend Vector2 operator*(Vector2 value, float scaleFactor);

        /**
         * @brief Multiplies all components of a vector by a scalar.
         *
         * @param scaleFactor Scalar multiplier.
         * @param value Source vector.
         * @return The scaled vector.
         */
        friend Vector2 operator*(float scaleFactor, Vector2 value);

        /**
         * @brief Divides one vector by another component-wise.
         *
         * @param value1 Left-hand vector.
         * @param value2 Right-hand vector.
         * @return The component-wise quotient.
         */
        friend Vector2 operator/(Vector2 value1, Vector2 value2);

        /**
         * @brief Divides all components of a vector by a scalar.
         *
         * @param value1 Source vector.
         * @param divider Divisor scalar.
         * @return The scaled vector.
         */
        friend Vector2 operator/(Vector2 value1, float divider);

    private:
        [[nodiscard]] std::string getDebugDisplayStringProperty() const;
        void CheckForNaNs() const;
    };
}
