// SPDX-License-Identifier: MS-PL

#pragma once

#include <string>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"

namespace Microsoft::Xna::Framework
{
    struct Matrix;
    struct Quaternion;

    /** @brief Describes a three-component floating point vector. */
    struct Vector3
    {
        /** @brief Vector with all components set to zero. */
        static const Vector3 Zero;
        /** @brief Vector with all components set to one. */
        static const Vector3 One;
        /** @brief Unit vector pointing along the positive X axis. */
        static const Vector3 UnitX;
        /** @brief Unit vector pointing along the positive Y axis. */
        static const Vector3 UnitY;
        /** @brief Unit vector pointing along the positive Z axis. */
        static const Vector3 UnitZ;
        /** @brief Unit vector pointing up (0, 1, 0). */
        static const Vector3 Up;
        /** @brief Unit vector pointing down (0, -1, 0). */
        static const Vector3 Down;
        /** @brief Unit vector pointing right (1, 0, 0). */
        static const Vector3 Right;
        /** @brief Unit vector pointing left (-1, 0, 0). */
        static const Vector3 Left;
        /** @brief Unit vector pointing forward in a right-handed coordinate system (0, 0, -1). */
        static const Vector3 Forward;
        /** @brief Unit vector pointing backward in a right-handed coordinate system (0, 0, 1). */
        static const Vector3 Backward;

        /** @brief X component. */
        float X;

        /** @brief Y component. */
        float Y;

        /** @brief Z component. */
        float Z;

        /** @brief Creates a zero vector. */
        Vector3();

        /**
         * @brief Creates a vector from three component values.
         *
         * @param x The X component.
         * @param y The Y component.
         * @param z The Z component.
         */
        Vector3(float x, float y, float z);

        /**
         * @brief Creates a vector with all components set to the same value.
         *
         * @param value The value to assign to all components.
         */
        explicit Vector3(float value);

        /**
         * @brief Creates a vector from a Vector2 and a Z value.
         *
         * @param value The X and Y components.
         * @param z The Z component.
         */
        Vector3(Vector2 value, float z);

        /**
         * @brief Compares this vector with another for equality.
         *
         * @param other The vector to compare against.
         * @return @c true if the vectors are equal; @c false otherwise.
         */
        [[nodiscard]] bool Equals(const Vector3& other) const;

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
         * @brief Returns a string in the form {X:... Y:... Z:...}.
         *
         * @return String representation of this vector.
         */
        [[nodiscard]] std::string ToString() const;

        /**
         * @brief Adds another vector to this vector in place.
         *
         * @param vector3 The vector to add.
         * @return Reference to this vector after addition.
         */
        CNAEXT Vector3& operator+=(const Vector3& vector3);

        /**
         * @brief Subtracts another vector from this vector in place.
         *
         * @param vector3 The vector to subtract.
         * @return Reference to this vector after subtraction.
         */
        CNAEXT Vector3& operator-=(const Vector3& vector3);

        /**
         * @brief Multiplies this vector by another vector component-wise, in place.
         *
         * C# synthesises `*=` from the declared `op_Multiply`, so XNA game code writes
         * `velocity *= sensitivity` without the type declaring anything; C++ has to spell it out.
         * Marked CNAEXT because the XNA reference declares no such operator of its own -- the
         * same treatment Vector2 already gives its pair.
         *
         * @param vector3 The vector to multiply by.
         * @return Reference to this vector after multiplication.
         */
        CNAEXT Vector3& operator*=(const Vector3& vector3);

        /**
         * @brief Multiplies this vector by a scalar, in place.
         *
         * The scalar `*=` counterpart of operator*=; see its note for why this is CNAEXT.
         *
         * @param scaleFactor The scalar to multiply by.
         * @return Reference to this vector after multiplication.
         */
        CNAEXT Vector3& operator*=(float scaleFactor);

        /**
         * @brief Divides this vector by another vector component-wise, in place.
         *
         * The `/=` counterpart of operator*=; see its note for why this is CNAEXT.
         *
         * @param vector3 The vector to divide by.
         * @return Reference to this vector after division.
         */
        CNAEXT Vector3& operator/=(const Vector3& vector3);

        /**
         * @brief Divides this vector by a scalar, in place.
         *
         * The scalar `/=` counterpart of operator*=; see its note for why this is CNAEXT.
         *
         * @param divider The scalar to divide by.
         * @return Reference to this vector after division.
         */
        CNAEXT Vector3& operator/=(float divider);

        /**
         * @brief Adds two vectors.
         *
         * @param value1 Source vector.
         * @param value2 Source vector.
         * @return Sum of the vectors.
         */
        [[nodiscard]] static Vector3 Add(Vector3 value1, Vector3 value2);

        /**
         * @brief Stores the sum of two vectors in an output parameter.
         *
         * @param value1 Source vector.
         * @param value2 Source vector.
         * @param result Output vector that receives the sum.
         */
        static void Add(const Vector3& value1, const Vector3& value2, Vector3& result);

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
        [[nodiscard]] static Vector3 Barycentric(Vector3 value1, Vector3 value2, Vector3 value3, float amount1,
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
        static void Barycentric(const Vector3& value1, const Vector3& value2, const Vector3& value3, float amount1,
                                float amount2, Vector3& result);

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
        [[nodiscard]] static Vector3 CatmullRom(Vector3 value1, Vector3 value2, Vector3 value3, Vector3 value4,
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
        static void CatmullRom(const Vector3& value1, const Vector3& value2, const Vector3& value3,
                               const Vector3& value4, float amount, Vector3& result);

        /**
         * @brief Clamps each component between the matching minimum and maximum component.
         *
         * @param value1 The vector to clamp.
         * @param min Minimum value vector.
         * @param max Maximum value vector.
         * @return The clamped vector.
         */
        [[nodiscard]] static Vector3 Clamp(Vector3 value1, Vector3 min, Vector3 max);

        /**
         * @brief Stores the clamped result in an output parameter.
         *
         * @param value1 The vector to clamp.
         * @param min Minimum value vector.
         * @param max Maximum value vector.
         * @param result Output vector that receives the clamped result.
         */
        static void Clamp(const Vector3& value1, const Vector3& min, const Vector3& max, Vector3& result);

        /**
         * @brief Returns the cross product of two vectors.
         *
         * @param vector1 Source vector.
         * @param vector2 Source vector.
         * @return The cross product of the two vectors.
         */
        [[nodiscard]] static Vector3 Cross(Vector3 vector1, Vector3 vector2);

        /**
         * @brief Stores the cross product of two vectors in an output parameter.
         *
         * @param vector1 Source vector.
         * @param vector2 Source vector.
         * @param result Output vector that receives the cross product.
         */
        static void Cross(const Vector3& vector1, const Vector3& vector2, Vector3& result);

        /**
         * @brief Returns the distance between two vectors.
         *
         * @param value1 Source vector.
         * @param value2 Source vector.
         * @return The distance between the vectors.
         */
        [[nodiscard]] static float Distance(Vector3 value1, Vector3 value2);

        /**
         * @brief Stores the distance between two vectors in an output parameter.
         *
         * @param value1 Source vector.
         * @param value2 Source vector.
         * @param result Output scalar that receives the distance.
         */
        static void Distance(const Vector3& value1, const Vector3& value2, float& result);

        /**
         * @brief Returns the squared distance between two vectors.
         *
         * @param value1 Source vector.
         * @param value2 Source vector.
         * @return The squared distance between the vectors.
         */
        [[nodiscard]] static float DistanceSquared(Vector3 value1, Vector3 value2);

        /**
         * @brief Stores the squared distance between two vectors in an output parameter.
         *
         * @param value1 Source vector.
         * @param value2 Source vector.
         * @param result Output scalar that receives the squared distance.
         */
        static void DistanceSquared(const Vector3& value1, const Vector3& value2, float& result);

        /**
         * @brief Divides a vector component-wise by another vector.
         *
         * @param value1 Source vector.
         * @param value2 Divisor vector.
         * @return The component-wise quotient.
         */
        [[nodiscard]] static Vector3 Divide(Vector3 value1, Vector3 value2);

        /**
         * @brief Stores the component-wise division result in an output parameter.
         *
         * @param value1 Source vector.
         * @param value2 Divisor vector.
         * @param result Output vector that receives the quotient.
         */
        static void Divide(const Vector3& value1, const Vector3& value2, Vector3& result);

        /**
         * @brief Divides a vector by a scalar.
         *
         * @param value1 Source vector.
         * @param divider Divisor scalar.
         * @return The scaled vector.
         */
        [[nodiscard]] static Vector3 Divide(Vector3 value1, float divider);

        /**
         * @brief Stores the scalar division result in an output parameter.
         *
         * @param value1 Source vector.
         * @param divider Divisor scalar.
         * @param result Output vector that receives the result.
         */
        static void Divide(const Vector3& value1, float divider, Vector3& result);

        /**
         * @brief Returns the dot product of two vectors.
         *
         * @param value1 Source vector.
         * @param value2 Source vector.
         * @return The dot product of the two vectors.
         */
        [[nodiscard]] static float Dot(Vector3 value1, Vector3 value2);

        /**
         * @brief Stores the dot product of two vectors in an output parameter.
         *
         * @param value1 Source vector.
         * @param value2 Source vector.
         * @param result Output scalar that receives the dot product.
         */
        static void Dot(const Vector3& value1, const Vector3& value2, float& result);

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
        [[nodiscard]] static Vector3 Hermite(Vector3 value1, Vector3 tangent1, Vector3 value2, Vector3 tangent2,
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
        static void Hermite(const Vector3& value1, const Vector3& tangent1, const Vector3& value2,
                            const Vector3& tangent2, float amount, Vector3& result);

        /**
         * @brief Linearly interpolates between two vectors.
         *
         * @param value1 Source vector.
         * @param value2 Destination vector.
         * @param amount Value between 0 and 1 indicating the interpolation weight.
         * @return The interpolated vector.
         */
        [[nodiscard]] static Vector3 Lerp(Vector3 value1, Vector3 value2, float amount);

        /**
         * @brief Stores the linearly interpolated vector in an output parameter.
         *
         * @param value1 Source vector.
         * @param value2 Destination vector.
         * @param amount Interpolation weight.
         * @param result Output vector that receives the result.
         */
        static void Lerp(const Vector3& value1, const Vector3& value2, float amount, Vector3& result);

        /**
         * @brief Returns the component-wise maximum.
         *
         * @param value1 Source vector.
         * @param value2 Source vector.
         * @return A vector whose components are the per-component maximum of the two inputs.
         */
        [[nodiscard]] static Vector3 Max(Vector3 value1, Vector3 value2);

        /**
         * @brief Stores the component-wise maximum in an output parameter.
         *
         * @param value1 Source vector.
         * @param value2 Source vector.
         * @param result Output vector that receives the per-component maximum.
         */
        static void Max(const Vector3& value1, const Vector3& value2, Vector3& result);

        /**
         * @brief Returns the component-wise minimum.
         *
         * @param value1 Source vector.
         * @param value2 Source vector.
         * @return A vector whose components are the per-component minimum of the two inputs.
         */
        [[nodiscard]] static Vector3 Min(Vector3 value1, Vector3 value2);

        /**
         * @brief Stores the component-wise minimum in an output parameter.
         *
         * @param value1 Source vector.
         * @param value2 Source vector.
         * @param result Output vector that receives the per-component minimum.
         */
        static void Min(const Vector3& value1, const Vector3& value2, Vector3& result);

        /**
         * @brief Multiplies vectors component-wise.
         *
         * @param value1 Source vector.
         * @param value2 Source vector.
         * @return The component-wise product.
         */
        [[nodiscard]] static Vector3 Multiply(Vector3 value1, Vector3 value2);

        /**
         * @brief Stores the component-wise product in an output parameter.
         *
         * @param value1 Source vector.
         * @param value2 Source vector.
         * @param result Output vector that receives the product.
         */
        static void Multiply(const Vector3& value1, const Vector3& value2, Vector3& result);

        /**
         * @brief Multiplies a vector by a scalar.
         *
         * @param value1 Source vector.
         * @param scaleFactor Scalar multiplier.
         * @return The scaled vector.
         */
        [[nodiscard]] static Vector3 Multiply(Vector3 value1, float scaleFactor);

        /**
         * @brief Stores the scalar product in an output parameter.
         *
         * @param value1 Source vector.
         * @param scaleFactor Scalar multiplier.
         * @param result Output vector that receives the product.
         */
        static void Multiply(const Vector3& value1, float scaleFactor, Vector3& result);

        /**
         * @brief Negates all vector components.
         *
         * @param value Source vector.
         * @return A vector with negated components.
         */
        [[nodiscard]] static Vector3 Negate(Vector3 value);

        /**
         * @brief Stores the negated vector in an output parameter.
         *
         * @param value Source vector.
         * @param result Output vector that receives the negated components.
         */
        static void Negate(const Vector3& value, Vector3& result);

        /**
         * @brief Returns a normalized copy of a vector.
         *
         * @param value Source vector.
         * @return The normalized vector.
         */
        [[nodiscard]] static Vector3 Normalize(Vector3 value);

        /**
         * @brief Stores the normalized vector in an output parameter.
         *
         * @param value Source vector.
         * @param result Output vector that receives the normalized result.
         */
        static void Normalize(const Vector3& value, Vector3& result);

        /**
         * @brief Reflects a vector across a normal.
         *
         * @param vector Source vector.
         * @param normal Normal of the surface to reflect across.
         * @return The reflected vector.
         */
        [[nodiscard]] static Vector3 Reflect(Vector3 vector, Vector3 normal);

        /**
         * @brief Stores the reflected vector in an output parameter.
         *
         * @param vector Source vector.
         * @param normal Normal of the surface to reflect across.
         * @param result Output vector that receives the reflected result.
         */
        static void Reflect(const Vector3& vector, const Vector3& normal, Vector3& result);

        /**
         * @brief Performs smooth Hermite interpolation between two vectors.
         *
         * @param value1 Source vector.
         * @param value2 Destination vector.
         * @param amount Weighting value between 0 and 1.
         * @return The smooth-stepped interpolation result.
         */
        [[nodiscard]] static Vector3 SmoothStep(Vector3 value1, Vector3 value2, float amount);

        /**
         * @brief Stores the smooth-step result in an output parameter.
         *
         * @param value1 Source vector.
         * @param value2 Destination vector.
         * @param amount Weighting value.
         * @param result Output vector that receives the result.
         */
        static void SmoothStep(const Vector3& value1, const Vector3& value2, float amount, Vector3& result);

        /**
         * @brief Subtracts one vector from another.
         *
         * @param value1 Source vector.
         * @param value2 Vector to subtract.
         * @return The difference vector.
         */
        [[nodiscard]] static Vector3 Subtract(Vector3 value1, Vector3 value2);

        /**
         * @brief Stores the difference of two vectors in an output parameter.
         *
         * @param value1 Source vector.
         * @param value2 Vector to subtract.
         * @param result Output vector that receives the difference.
         */
        static void Subtract(const Vector3& value1, const Vector3& value2, Vector3& result);

        /**
         * @brief Transforms a position by a matrix.
         *
         * @param position Source position vector.
         * @param matrix The transformation matrix.
         * @return The transformed position.
         */
        [[nodiscard]] static Vector3 Transform(Vector3 position, const Matrix& matrix);

        /**
         * @brief Stores the matrix-transformed position in an output parameter.
         *
         * @param position Source position vector.
         * @param matrix The transformation matrix.
         * @param result Output vector that receives the transformed position.
         */
        static void Transform(const Vector3& position, const Matrix& matrix, Vector3& result);

        /**
         * @brief Transforms an array of positions by a matrix.
         *
         * @param sourceArray Source array of vectors.
         * @param matrix The transformation matrix.
         * @param destinationArray Output array that receives the transformed vectors.
         */
        static void Transform(const std::vector<Vector3>& sourceArray, const Matrix& matrix,
                              std::vector<Vector3>& destinationArray);

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
        static void Transform(const std::vector<Vector3>& sourceArray, int sourceIndex, const Matrix& matrix,
                              std::vector<Vector3>& destinationArray, int destinationIndex, int length);

        /**
         * @brief Transforms a vector by a quaternion rotation.
         *
         * @param value Source vector.
         * @param rotation The quaternion rotation to apply.
         * @return The rotated vector.
         */
        [[nodiscard]] static Vector3 Transform(Vector3 value, const Quaternion& rotation);

        /**
         * @brief Stores the quaternion-rotated vector in an output parameter.
         *
         * @param value Source vector.
         * @param rotation The quaternion rotation to apply.
         * @param result Output vector that receives the rotated result.
         */
        static void Transform(const Vector3& value, const Quaternion& rotation, Vector3& result);

        /**
         * @brief Transforms an array of vectors by a quaternion rotation.
         *
         * @param sourceArray Source array of vectors.
         * @param rotation The quaternion rotation to apply.
         * @param destinationArray Output array that receives the rotated vectors.
         */
        static void Transform(const std::vector<Vector3>& sourceArray, const Quaternion& rotation,
                              std::vector<Vector3>& destinationArray);

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
        static void Transform(const std::vector<Vector3>& sourceArray, int sourceIndex, const Quaternion& rotation,
                              std::vector<Vector3>& destinationArray, int destinationIndex, int length);

        /**
         * @brief Transforms a normal by a matrix without applying translation.
         *
         * @param normal Source normal vector.
         * @param matrix The transformation matrix.
         * @return The transformed normal.
         */
        [[nodiscard]] static Vector3 TransformNormal(Vector3 normal, const Matrix& matrix);

        /**
         * @brief Stores the matrix-transformed normal in an output parameter.
         *
         * @param normal Source normal vector.
         * @param matrix The transformation matrix.
         * @param result Output vector that receives the transformed normal.
         */
        static void TransformNormal(const Vector3& normal, const Matrix& matrix, Vector3& result);

        /**
         * @brief Transforms an array of normals by a matrix.
         *
         * @param sourceArray Source array of normals.
         * @param matrix The transformation matrix.
         * @param destinationArray Output array that receives the transformed normals.
         */
        static void TransformNormal(const std::vector<Vector3>& sourceArray, const Matrix& matrix,
                                    std::vector<Vector3>& destinationArray);

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
        static void TransformNormal(const std::vector<Vector3>& sourceArray, int sourceIndex, const Matrix& matrix,
                                    std::vector<Vector3>& destinationArray, int destinationIndex, int length);

        /**
         * @brief Returns true when all components are equal.
         *
         * @param value1 Left-hand vector.
         * @param value2 Right-hand vector.
         * @return @c true if the vectors are equal; @c false otherwise.
         */
        friend bool operator==(Vector3 value1, Vector3 value2);

        /**
         * @brief Returns true when any component differs.
         *
         * @param value1 Left-hand vector.
         * @param value2 Right-hand vector.
         * @return @c true if the vectors are not equal; @c false otherwise.
         */
        friend bool operator!=(Vector3 value1, Vector3 value2);

        /**
         * @brief Adds two vectors component-wise.
         *
         * @param value1 Left-hand vector.
         * @param value2 Right-hand vector.
         * @return The component-wise sum.
         */
        friend Vector3 operator+(Vector3 value1, Vector3 value2);

        /**
         * @brief Negates all components of a vector.
         *
         * @param value Source vector.
         * @return The negated vector.
         */
        friend Vector3 operator-(Vector3 value);

        /**
         * @brief Subtracts one vector from another component-wise.
         *
         * @param value1 Left-hand vector.
         * @param value2 Right-hand vector.
         * @return The component-wise difference.
         */
        friend Vector3 operator-(Vector3 value1, Vector3 value2);

        /**
         * @brief Multiplies two vectors component-wise.
         *
         * @param value1 Left-hand vector.
         * @param value2 Right-hand vector.
         * @return The component-wise product.
         */
        friend Vector3 operator*(Vector3 value1, Vector3 value2);

        /**
         * @brief Multiplies all components of a vector by a scalar.
         *
         * @param value Source vector.
         * @param scaleFactor Scalar multiplier.
         * @return The scaled vector.
         */
        friend Vector3 operator*(Vector3 value, float scaleFactor);

        /**
         * @brief Multiplies all components of a vector by a scalar.
         *
         * @param scaleFactor Scalar multiplier.
         * @param value Source vector.
         * @return The scaled vector.
         */
        friend Vector3 operator*(float scaleFactor, Vector3 value);

        /**
         * @brief Divides one vector by another component-wise.
         *
         * @param value1 Left-hand vector.
         * @param value2 Right-hand vector.
         * @return The component-wise quotient.
         */
        friend Vector3 operator/(Vector3 value1, Vector3 value2);

        /**
         * @brief Divides all components of a vector by a scalar.
         *
         * @param value1 Source vector.
         * @param divider Divisor scalar.
         * @return The scaled vector.
         */
        friend Vector3 operator/(Vector3 value1, float divider);

    private:
        [[nodiscard]] std::string getDebugDisplayStringProperty() const;
        void CheckForNaNs() const;
    };
}
