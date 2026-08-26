// SPDX-License-Identifier: MS-PL

#pragma once

#include <optional>
#include <string>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

namespace Microsoft::Xna::Framework
{
    struct Plane;
    struct Quaternion;

    /** @brief Represents a right-handed 4x4 matrix storing translation, scale and rotation data. */
    struct Matrix
    {
        /**
         * @brief Returns the identity matrix.
         *
         * @return The identity matrix.
         */
        static const Matrix getIdentityProperty();

        /** @brief A first row and first column value. */
        float M11;
        /** @brief A first row and second column value. */
        float M12;
        /** @brief A first row and third column value. */
        float M13;
        /** @brief A first row and fourth column value. */
        float M14;

        /** @brief A second row and first column value. */
        float M21;
        /** @brief A second row and second column value. */
        float M22;
        /** @brief A second row and third column value. */
        float M23;
        /** @brief A second row and fourth column value. */
        float M24;

        /** @brief A third row and first column value. */
        float M31;
        /** @brief A third row and second column value. */
        float M32;
        /** @brief A third row and third column value. */
        float M33;
        /** @brief A third row and fourth column value. */
        float M34;

        /** @brief A fourth row and first column value. */
        float M41;
        /** @brief A fourth row and second column value. */
        float M42;
        /** @brief A fourth row and third column value. */
        float M43;
        /** @brief A fourth row and fourth column value. */
        float M44;

        /** @brief Constructs a zero-filled matrix. */
        Matrix();

        /**
         * @brief Constructs a matrix from all 16 row-major field values.
         *
         * @param m11 Value for row 1, column 1.
         * @param m12 Value for row 1, column 2.
         * @param m13 Value for row 1, column 3.
         * @param m14 Value for row 1, column 4.
         * @param m21 Value for row 2, column 1.
         * @param m22 Value for row 2, column 2.
         * @param m23 Value for row 2, column 3.
         * @param m24 Value for row 2, column 4.
         * @param m31 Value for row 3, column 1.
         * @param m32 Value for row 3, column 2.
         * @param m33 Value for row 3, column 3.
         * @param m34 Value for row 3, column 4.
         * @param m41 Value for row 4, column 1.
         * @param m42 Value for row 4, column 2.
         * @param m43 Value for row 4, column 3.
         * @param m44 Value for row 4, column 4.
         */
        Matrix(
            float m11, float m12, float m13, float m14,
            float m21, float m22, float m23, float m24,
            float m31, float m32, float m33, float m34,
            float m41, float m42, float m43, float m44
        );

        /**
         * @brief Gets the backward vector from the third matrix row.
         *
         * @return The backward direction vector.
         */
        [[nodiscard]] Vector3 getBackwardProperty() const;

        /**
         * @brief Sets the backward vector in the third matrix row.
         *
         * @param value The backward direction vector to set.
         */
        void setBackwardProperty(Vector3 value);

        /**
         * @brief Gets the down vector from the negated second matrix row.
         *
         * @return The down direction vector.
         */
        [[nodiscard]] Vector3 getDownProperty() const;

        /**
         * @brief Sets the down vector into the negated second matrix row.
         *
         * @param value The down direction vector to set.
         */
        void setDownProperty(Vector3 value);

        /**
         * @brief Gets the forward vector from the negated third matrix row.
         *
         * @return The forward direction vector.
         */
        [[nodiscard]] Vector3 getForwardProperty() const;

        /**
         * @brief Sets the forward vector into the negated third matrix row.
         *
         * @param value The forward direction vector to set.
         */
        void setForwardProperty(Vector3 value);

        /**
         * @brief Gets the left vector from the negated first matrix row.
         *
         * @return The left direction vector.
         */
        [[nodiscard]] Vector3 getLeftProperty() const;

        /**
         * @brief Sets the left vector into the negated first matrix row.
         *
         * @param value The left direction vector to set.
         */
        void setLeftProperty(Vector3 value);

        /**
         * @brief Gets the right vector from the first matrix row.
         *
         * @return The right direction vector.
         */
        [[nodiscard]] Vector3 getRightProperty() const;

        /**
         * @brief Sets the right vector into the first matrix row.
         *
         * @param value The right direction vector to set.
         */
        void setRightProperty(Vector3 value);

        /**
         * @brief Gets the translation stored in this matrix.
         *
         * @return The translation vector.
         */
        [[nodiscard]] Vector3 getTranslationProperty() const;

        /**
         * @brief Sets the translation stored in this matrix.
         *
         * @param value The translation vector to set.
         */
        void setTranslationProperty(Vector3 value);

        /**
         * @brief Gets the up vector from the second matrix row.
         *
         * @return The up direction vector.
         */
        [[nodiscard]] Vector3 getUpProperty() const;

        /**
         * @brief Sets the up vector into the second matrix row.
         *
         * @param value The up direction vector to set.
         */
        void setUpProperty(Vector3 value);

        /**
         * @brief Decomposes this matrix into scale, rotation and translation parts.
         *
         * @param scale Output vector that receives the scale components.
         * @param rotation Output quaternion that receives the rotation.
         * @param translation Output vector that receives the translation.
         * @return @c true if the decomposition succeeded; @c false otherwise.
         */
        [[nodiscard]] bool Decompose(Vector3& scale, Quaternion& rotation, Vector3& translation) const;

        /**
         * @brief Returns the determinant of this matrix.
         *
         * @return The determinant.
         */
        [[nodiscard]] float Determinant() const;

        /**
         * @brief Compares this matrix with another matrix without tolerance.
         *
         * @param other The matrix to compare against.
         * @return @c true if the matrices are equal; @c false otherwise.
         */
        [[nodiscard]] bool Equals(const Matrix& other) const;

        /**
         * @brief Returns a hash code for this matrix.
         *
         * @return Hash code of this matrix.
         */
        [[nodiscard]] int GetHashCode() const;

        /**
         * @brief Returns a string representation of all 16 fields.
         *
         * @return String representation of this matrix.
         */
        [[nodiscard]] std::string ToString() const;

        /**
         * @brief Adds two matrices component by component.
         *
         * @param matrix1 Source matrix.
         * @param matrix2 Source matrix.
         * @return The component-wise sum.
         */
        [[nodiscard]] static Matrix Add(Matrix matrix1, Matrix matrix2);

        /**
         * @brief Adds two matrices component by component and stores the result in an output parameter.
         *
         * @param matrix1 Source matrix.
         * @param matrix2 Source matrix.
         * @param result Output matrix that receives the sum.
         */
        static void Add(const Matrix& matrix1, const Matrix& matrix2, Matrix& result);

        /**
         * @brief Creates a spherical billboard matrix.
         *
         * @param objectPosition Position of the object the billboard will rotate to face.
         * @param cameraPosition Position of the camera.
         * @param cameraUpVector The up vector of the camera.
         * @param cameraForwardVector Optional forward vector of the camera.
         * @return The billboard matrix.
         */
        [[nodiscard]] static Matrix CreateBillboard(
            Vector3 objectPosition,
            Vector3 cameraPosition,
            Vector3 cameraUpVector,
            std::optional<Vector3> cameraForwardVector
        );

        /**
         * @brief Creates a spherical billboard matrix in an output parameter.
         *
         * @param objectPosition Position of the object the billboard will rotate to face.
         * @param cameraPosition Position of the camera.
         * @param cameraUpVector The up vector of the camera.
         * @param cameraForwardVector Optional forward vector of the camera.
         * @param result Output matrix that receives the billboard matrix.
         */
        static void CreateBillboard(
            const Vector3& objectPosition,
            const Vector3& cameraPosition,
            const Vector3& cameraUpVector,
            std::optional<Vector3> cameraForwardVector,
            Matrix& result
        );

        /**
         * @brief Creates a cylindrical billboard matrix constrained to a rotation axis.
         *
         * @param objectPosition Position of the object the billboard will rotate to face.
         * @param cameraPosition Position of the camera.
         * @param rotateAxis The axis to rotate around.
         * @param cameraForwardVector Optional forward vector of the camera.
         * @param objectForwardVector Optional forward vector of the object.
         * @return The constrained billboard matrix.
         */
        [[nodiscard]] static Matrix CreateConstrainedBillboard(
            Vector3 objectPosition,
            Vector3 cameraPosition,
            Vector3 rotateAxis,
            std::optional<Vector3> cameraForwardVector,
            std::optional<Vector3> objectForwardVector
        );

        /**
         * @brief Creates a cylindrical billboard matrix constrained to a rotation axis in an output parameter.
         *
         * @param objectPosition Position of the object.
         * @param cameraPosition Position of the camera.
         * @param rotateAxis The axis to rotate around.
         * @param cameraForwardVector Optional forward vector of the camera.
         * @param objectForwardVector Optional forward vector of the object.
         * @param result Output matrix that receives the constrained billboard matrix.
         */
        static void CreateConstrainedBillboard(
            const Vector3& objectPosition,
            const Vector3& cameraPosition,
            const Vector3& rotateAxis,
            std::optional<Vector3> cameraForwardVector,
            std::optional<Vector3> objectForwardVector,
            Matrix& result
        );

        /**
         * @brief Creates a rotation matrix from an axis and angle in radians.
         *
         * @param axis The axis to rotate around.
         * @param angle The angle in radians.
         * @return The rotation matrix.
         */
        [[nodiscard]] static Matrix CreateFromAxisAngle(Vector3 axis, float angle);

        /**
         * @brief Creates a rotation matrix from an axis and angle in radians in an output parameter.
         *
         * @param axis The axis to rotate around.
         * @param angle The angle in radians.
         * @param result Output matrix that receives the rotation matrix.
         */
        static void CreateFromAxisAngle(const Vector3& axis, float angle, Matrix& result);

        /**
         * @brief Creates a rotation matrix from a quaternion.
         *
         * @param quaternion The source quaternion.
         * @return The rotation matrix.
         */
        [[nodiscard]] static Matrix CreateFromQuaternion(Quaternion quaternion);

        /**
         * @brief Creates a rotation matrix from a quaternion in an output parameter.
         *
         * @param quaternion The source quaternion.
         * @param result Output matrix that receives the rotation matrix.
         */
        static void CreateFromQuaternion(const Quaternion& quaternion, Matrix& result);

        /**
         * @brief Creates a rotation matrix from yaw, pitch and roll angles in radians.
         *
         * @param yaw Rotation angle around the y-axis in radians.
         * @param pitch Rotation angle around the x-axis in radians.
         * @param roll Rotation angle around the z-axis in radians.
         * @return The rotation matrix.
         */
        [[nodiscard]] static Matrix CreateFromYawPitchRoll(float yaw, float pitch, float roll);

        /**
         * @brief Creates a rotation matrix from yaw, pitch and roll angles in an output parameter.
         *
         * @param yaw Rotation angle around the y-axis in radians.
         * @param pitch Rotation angle around the x-axis in radians.
         * @param roll Rotation angle around the z-axis in radians.
         * @param result Output matrix that receives the rotation matrix.
         */
        static void CreateFromYawPitchRoll(float yaw, float pitch, float roll, Matrix& result);

        /**
         * @brief Creates a right-handed view matrix.
         *
         * @param cameraPosition The position of the camera.
         * @param cameraTarget The target the camera is looking at.
         * @param cameraUpVector The up vector of the camera.
         * @return The view matrix.
         */
        [[nodiscard]] static Matrix CreateLookAt(Vector3 cameraPosition, Vector3 cameraTarget, Vector3 cameraUpVector);

        /**
         * @brief Creates a right-handed view matrix in an output parameter.
         *
         * @param cameraPosition The position of the camera.
         * @param cameraTarget The target the camera is looking at.
         * @param cameraUpVector The up vector of the camera.
         * @param result Output matrix that receives the view matrix.
         */
        static void CreateLookAt(const Vector3& cameraPosition, const Vector3& cameraTarget,
                                 const Vector3& cameraUpVector, Matrix& result);

        /**
         * @brief Creates an orthographic projection matrix.
         *
         * @param width Width of the view volume.
         * @param height Height of the view volume.
         * @param zNearPlane Minimum z-value of the view volume.
         * @param zFarPlane Maximum z-value of the view volume.
         * @return The orthographic projection matrix.
         */
        [[nodiscard]] static Matrix CreateOrthographic(float width, float height, float zNearPlane, float zFarPlane);

        /**
         * @brief Creates an orthographic projection matrix in an output parameter.
         *
         * @param width Width of the view volume.
         * @param height Height of the view volume.
         * @param zNearPlane Minimum z-value of the view volume.
         * @param zFarPlane Maximum z-value of the view volume.
         * @param result Output matrix that receives the orthographic projection.
         */
        static void CreateOrthographic(float width, float height, float zNearPlane, float zFarPlane, Matrix& result);

        /**
         * @brief Creates an off-center orthographic projection matrix.
         *
         * @param left Minimum x-value of the view volume.
         * @param right Maximum x-value of the view volume.
         * @param bottom Minimum y-value of the view volume.
         * @param top Maximum y-value of the view volume.
         * @param zNearPlane Minimum z-value of the view volume.
         * @param zFarPlane Maximum z-value of the view volume.
         * @return The off-center orthographic projection matrix.
         */
        [[nodiscard]] static Matrix CreateOrthographicOffCenter(float left, float right, float bottom, float top,
                                                                float zNearPlane, float zFarPlane);

        /**
         * @brief Creates an off-center orthographic projection matrix in an output parameter.
         *
         * @param left Minimum x-value of the view volume.
         * @param right Maximum x-value of the view volume.
         * @param bottom Minimum y-value of the view volume.
         * @param top Maximum y-value of the view volume.
         * @param zNearPlane Minimum z-value of the view volume.
         * @param zFarPlane Maximum z-value of the view volume.
         * @param result Output matrix that receives the projection.
         */
        static void CreateOrthographicOffCenter(float left, float right, float bottom, float top, float zNearPlane,
                                                float zFarPlane, Matrix& result);

        /**
         * @brief Creates a perspective projection matrix.
         *
         * @param width Width of the view volume at the near plane.
         * @param height Height of the view volume at the near plane.
         * @param nearPlaneDistance Distance to the near view plane.
         * @param farPlaneDistance Distance to the far view plane.
         * @return The perspective projection matrix.
         */
        [[nodiscard]] static Matrix CreatePerspective(float width, float height, float nearPlaneDistance,
                                                      float farPlaneDistance);

        /**
         * @brief Creates a perspective projection matrix in an output parameter.
         *
         * @param width Width of the view volume at the near plane.
         * @param height Height of the view volume at the near plane.
         * @param nearPlaneDistance Distance to the near view plane.
         * @param farPlaneDistance Distance to the far view plane.
         * @param result Output matrix that receives the perspective projection.
         */
        static void CreatePerspective(float width, float height, float nearPlaneDistance, float farPlaneDistance,
                                      Matrix& result);

        /**
         * @brief Creates a perspective projection matrix using a field of view.
         *
         * @param fieldOfView Field of view in the y direction, in radians.
         * @param aspectRatio Aspect ratio (width divided by height).
         * @param nearPlaneDistance Distance to the near view plane.
         * @param farPlaneDistance Distance to the far view plane.
         * @return The field-of-view perspective projection matrix.
         */
        [[nodiscard]] static Matrix CreatePerspectiveFieldOfView(float fieldOfView, float aspectRatio,
                                                                 float nearPlaneDistance, float farPlaneDistance);

        /**
         * @brief Creates a perspective projection matrix using a field of view in an output parameter.
         *
         * @param fieldOfView Field of view in the y direction, in radians.
         * @param aspectRatio Aspect ratio.
         * @param nearPlaneDistance Distance to the near view plane.
         * @param farPlaneDistance Distance to the far view plane.
         * @param result Output matrix that receives the projection.
         */
        static void CreatePerspectiveFieldOfView(float fieldOfView, float aspectRatio, float nearPlaneDistance,
                                                 float farPlaneDistance, Matrix& result);

        /**
         * @brief Creates an off-center perspective projection matrix.
         *
         * @param left Minimum x-value of the view volume at the near plane.
         * @param right Maximum x-value of the view volume at the near plane.
         * @param bottom Minimum y-value of the view volume at the near plane.
         * @param top Maximum y-value of the view volume at the near plane.
         * @param nearPlaneDistance Distance to the near view plane.
         * @param farPlaneDistance Distance to the far view plane.
         * @return The off-center perspective projection matrix.
         */
        [[nodiscard]] static Matrix CreatePerspectiveOffCenter(float left, float right, float bottom, float top,
                                                               float nearPlaneDistance, float farPlaneDistance);

        /**
         * @brief Creates an off-center perspective projection matrix in an output parameter.
         *
         * @param left Minimum x-value at the near plane.
         * @param right Maximum x-value at the near plane.
         * @param bottom Minimum y-value at the near plane.
         * @param top Maximum y-value at the near plane.
         * @param nearPlaneDistance Distance to the near view plane.
         * @param farPlaneDistance Distance to the far view plane.
         * @param result Output matrix that receives the projection.
         */
        static void CreatePerspectiveOffCenter(float left, float right, float bottom, float top,
                                               float nearPlaneDistance, float farPlaneDistance, Matrix& result);

        /**
         * @brief Creates a rotation matrix around the X axis.
         *
         * @param radians Angle in radians to rotate around the X axis.
         * @return The rotation matrix.
         */
        [[nodiscard]] static Matrix CreateRotationX(float radians);

        /**
         * @brief Creates a rotation matrix around the X axis in an output parameter.
         *
         * @param radians Angle in radians.
         * @param result Output matrix that receives the rotation matrix.
         */
        static void CreateRotationX(float radians, Matrix& result);

        /**
         * @brief Creates a rotation matrix around the Y axis.
         *
         * @param radians Angle in radians to rotate around the Y axis.
         * @return The rotation matrix.
         */
        [[nodiscard]] static Matrix CreateRotationY(float radians);

        /**
         * @brief Creates a rotation matrix around the Y axis in an output parameter.
         *
         * @param radians Angle in radians.
         * @param result Output matrix that receives the rotation matrix.
         */
        static void CreateRotationY(float radians, Matrix& result);

        /**
         * @brief Creates a rotation matrix around the Z axis.
         *
         * @param radians Angle in radians to rotate around the Z axis.
         * @return The rotation matrix.
         */
        [[nodiscard]] static Matrix CreateRotationZ(float radians);

        /**
         * @brief Creates a rotation matrix around the Z axis in an output parameter.
         *
         * @param radians Angle in radians.
         * @param result Output matrix that receives the rotation matrix.
         */
        static void CreateRotationZ(float radians, Matrix& result);

        /**
         * @brief Creates a uniform scale matrix.
         *
         * @param scale Scale factor for all axes.
         * @return The scale matrix.
         */
        [[nodiscard]] static Matrix CreateScale(float scale);

        /**
         * @brief Creates a uniform scale matrix in an output parameter.
         *
         * @param scale Scale factor for all axes.
         * @param result Output matrix that receives the scale matrix.
         */
        static void CreateScale(float scale, Matrix& result);

        /**
         * @brief Creates a non-uniform scale matrix.
         *
         * @param xScale Scale factor for the X axis.
         * @param yScale Scale factor for the Y axis.
         * @param zScale Scale factor for the Z axis.
         * @return The scale matrix.
         */
        [[nodiscard]] static Matrix CreateScale(float xScale, float yScale, float zScale);

        /**
         * @brief Creates a non-uniform scale matrix in an output parameter.
         *
         * @param xScale Scale factor for the X axis.
         * @param yScale Scale factor for the Y axis.
         * @param zScale Scale factor for the Z axis.
         * @param result Output matrix that receives the scale matrix.
         */
        static void CreateScale(float xScale, float yScale, float zScale, Matrix& result);

        /**
         * @brief Creates a scale matrix from a vector.
         *
         * @param scales Vector containing the scale factors for each axis.
         * @return The scale matrix.
         */
        [[nodiscard]] static Matrix CreateScale(Vector3 scales);

        /**
         * @brief Creates a scale matrix from a vector in an output parameter.
         *
         * @param scales Vector containing the scale factors.
         * @param result Output matrix that receives the scale matrix.
         */
        static void CreateScale(const Vector3& scales, Matrix& result);

        /**
         * @brief Creates a shadow projection matrix using a light direction and plane.
         *
         * @param lightDirection The direction of the light casting the shadow.
         * @param plane The plane on which the shadow is projected.
         * @return The shadow matrix.
         */
        [[nodiscard]] static Matrix CreateShadow(Vector3 lightDirection, Plane plane);

        /**
         * @brief Creates a shadow projection matrix in an output parameter.
         *
         * @param lightDirection The direction of the light casting the shadow.
         * @param plane The plane on which the shadow is projected.
         * @param result Output matrix that receives the shadow matrix.
         */
        static void CreateShadow(const Vector3& lightDirection, const Plane& plane, Matrix& result);

        /**
         * @brief Creates a translation matrix from coordinates.
         *
         * @param xPosition Translation along the X axis.
         * @param yPosition Translation along the Y axis.
         * @param zPosition Translation along the Z axis.
         * @return The translation matrix.
         */
        [[nodiscard]] static Matrix CreateTranslation(float xPosition, float yPosition, float zPosition);

        /**
         * @brief Creates a translation matrix from a vector.
         *
         * @param position The translation vector.
         * @return The translation matrix.
         */
        [[nodiscard]] static Matrix CreateTranslation(Vector3 position);

        /**
         * @brief Creates a translation matrix from a vector in an output parameter.
         *
         * @param position The translation vector.
         * @param result Output matrix that receives the translation matrix.
         */
        static void CreateTranslation(const Vector3& position, Matrix& result);

        /**
         * @brief Creates a translation matrix from coordinates in an output parameter.
         *
         * @param xPosition Translation along the X axis.
         * @param yPosition Translation along the Y axis.
         * @param zPosition Translation along the Z axis.
         * @param result Output matrix that receives the translation matrix.
         */
        static void CreateTranslation(float xPosition, float yPosition, float zPosition, Matrix& result);

        /**
         * @brief Creates a reflection matrix from a plane.
         *
         * @param value The plane to reflect across.
         * @return The reflection matrix.
         */
        [[nodiscard]] static Matrix CreateReflection(Plane value);

        /**
         * @brief Creates a reflection matrix from a plane in an output parameter.
         *
         * @param value The plane to reflect across.
         * @param result Output matrix that receives the reflection matrix.
         */
        static void CreateReflection(const Plane& value, Matrix& result);

        /**
         * @brief Creates a world matrix from position, forward and up vectors.
         *
         * @param position The position of the object.
         * @param forward The forward direction of the object.
         * @param up The up direction of the object.
         * @return The world matrix.
         */
        [[nodiscard]] static Matrix CreateWorld(Vector3 position, Vector3 forward, Vector3 up);

        /**
         * @brief Creates a world matrix in an output parameter.
         *
         * @param position The position of the object.
         * @param forward The forward direction of the object.
         * @param up The up direction of the object.
         * @param result Output matrix that receives the world matrix.
         */
        static void CreateWorld(const Vector3& position, const Vector3& forward, const Vector3& up, Matrix& result);

        /**
         * @brief Divides one matrix by another component by component.
         *
         * @param matrix1 Source matrix.
         * @param matrix2 Divisor matrix.
         * @return The component-wise quotient.
         */
        [[nodiscard]] static Matrix Divide(Matrix matrix1, Matrix matrix2);

        /**
         * @brief Divides one matrix by another component by component in an output parameter.
         *
         * @param matrix1 Source matrix.
         * @param matrix2 Divisor matrix.
         * @param result Output matrix that receives the quotient.
         */
        static void Divide(const Matrix& matrix1, const Matrix& matrix2, Matrix& result);

        /**
         * @brief Divides a matrix by a scalar.
         *
         * @param matrix1 Source matrix.
         * @param divider The divisor scalar.
         * @return The scaled matrix.
         */
        [[nodiscard]] static Matrix Divide(Matrix matrix1, float divider);

        /**
         * @brief Divides a matrix by a scalar in an output parameter.
         *
         * @param matrix1 Source matrix.
         * @param divider The divisor scalar.
         * @param result Output matrix that receives the result.
         */
        static void Divide(const Matrix& matrix1, float divider, Matrix& result);

        /**
         * @brief Returns the inverse of a matrix.
         *
         * @param matrix The matrix to invert.
         * @return The inverted matrix.
         */
        [[nodiscard]] static Matrix Invert(Matrix matrix);

        /**
         * @brief Returns the inverse of a matrix in an output parameter.
         *
         * @param matrix The matrix to invert.
         * @param result Output matrix that receives the inverse.
         */
        static void Invert(const Matrix& matrix, Matrix& result);

        /**
         * @brief Linearly interpolates between two matrices.
         *
         * @param matrix1 Source matrix.
         * @param matrix2 Destination matrix.
         * @param amount Interpolation weight between 0 and 1.
         * @return The interpolated matrix.
         */
        [[nodiscard]] static Matrix Lerp(Matrix matrix1, Matrix matrix2, float amount);

        /**
         * @brief Linearly interpolates between two matrices in an output parameter.
         *
         * @param matrix1 Source matrix.
         * @param matrix2 Destination matrix.
         * @param amount Interpolation weight.
         * @param result Output matrix that receives the result.
         */
        static void Lerp(const Matrix& matrix1, const Matrix& matrix2, float amount, Matrix& result);

        /**
         * @brief Multiplies two matrices.
         *
         * @param matrix1 Left-hand matrix.
         * @param matrix2 Right-hand matrix.
         * @return The matrix product.
         */
        [[nodiscard]] static Matrix Multiply(Matrix matrix1, Matrix matrix2);

        /**
         * @brief Multiplies two matrices in an output parameter.
         *
         * @param matrix1 Left-hand matrix.
         * @param matrix2 Right-hand matrix.
         * @param result Output matrix that receives the product.
         */
        static void Multiply(const Matrix& matrix1, const Matrix& matrix2, Matrix& result);

        /**
         * @brief Multiplies a matrix by a scalar.
         *
         * @param matrix1 Source matrix.
         * @param scaleFactor Scalar multiplier.
         * @return The scaled matrix.
         */
        [[nodiscard]] static Matrix Multiply(Matrix matrix1, float scaleFactor);

        /**
         * @brief Multiplies a matrix by a scalar in an output parameter.
         *
         * @param matrix1 Source matrix.
         * @param scaleFactor Scalar multiplier.
         * @param result Output matrix that receives the result.
         */
        static void Multiply(const Matrix& matrix1, float scaleFactor, Matrix& result);

        /**
         * @brief Negates every matrix component.
         *
         * @param matrix Source matrix.
         * @return The negated matrix.
         */
        [[nodiscard]] static Matrix Negate(Matrix matrix);

        /**
         * @brief Negates every matrix component in an output parameter.
         *
         * @param matrix Source matrix.
         * @param result Output matrix that receives the negated result.
         */
        static void Negate(const Matrix& matrix, Matrix& result);

        /**
         * @brief Subtracts one matrix from another.
         *
         * @param matrix1 Source matrix.
         * @param matrix2 Matrix to subtract.
         * @return The difference matrix.
         */
        [[nodiscard]] static Matrix Subtract(Matrix matrix1, Matrix matrix2);

        /**
         * @brief Subtracts one matrix from another in an output parameter.
         *
         * @param matrix1 Source matrix.
         * @param matrix2 Matrix to subtract.
         * @param result Output matrix that receives the difference.
         */
        static void Subtract(const Matrix& matrix1, const Matrix& matrix2, Matrix& result);

        /**
         * @brief Transposes a matrix.
         *
         * @param matrix Source matrix.
         * @return The transposed matrix.
         */
        [[nodiscard]] static Matrix Transpose(Matrix matrix);

        /**
         * @brief Transposes a matrix in an output parameter.
         *
         * @param matrix Source matrix.
         * @param result Output matrix that receives the transposed result.
         */
        static void Transpose(const Matrix& matrix, Matrix& result);

        /**
         * @brief Transforms a matrix by a quaternion rotation.
         *
         * @param value Source matrix.
         * @param rotation The quaternion rotation to apply.
         * @return The transformed matrix.
         */
        [[nodiscard]] static Matrix Transform(Matrix value, Quaternion rotation);

        /**
         * @brief Transforms a matrix by a quaternion rotation in an output parameter.
         *
         * @param value Source matrix.
         * @param rotation The quaternion rotation to apply.
         * @param result Output matrix that receives the transformed result.
         */
        static void Transform(const Matrix& value, const Quaternion& rotation, Matrix& result);

        /**
         * @brief Adds two matrices component-wise.
         *
         * @param matrix1 Left-hand matrix.
         * @param matrix2 Right-hand matrix.
         * @return The component-wise sum.
         */
        friend Matrix operator+(Matrix matrix1, Matrix matrix2);

        /**
         * @brief Divides the elements of one matrix by the elements of another.
         *
         * @param matrix1 Left-hand matrix.
         * @param matrix2 Right-hand matrix.
         * @return The component-wise quotient.
         */
        friend Matrix operator/(Matrix matrix1, Matrix matrix2);

        /**
         * @brief Divides all elements of a matrix by a scalar.
         *
         * @param matrix Source matrix.
         * @param divider Divisor scalar.
         * @return The scaled matrix.
         */
        friend Matrix operator/(Matrix matrix, float divider);

        /**
         * @brief Returns true when both matrices are equal without tolerance.
         *
         * @param matrix1 Left-hand matrix.
         * @param matrix2 Right-hand matrix.
         * @return @c true if the matrices are equal; @c false otherwise.
         */
        friend bool operator==(Matrix matrix1, Matrix matrix2);

        /**
         * @brief Returns true when any element differs between the two matrices.
         *
         * @param matrix1 Left-hand matrix.
         * @param matrix2 Right-hand matrix.
         * @return @c true if the matrices are not equal; @c false otherwise.
         */
        friend bool operator!=(Matrix matrix1, Matrix matrix2);

        /**
         * @brief Multiplies two matrices using standard matrix multiplication.
         *
         * @param matrix1 Left-hand matrix.
         * @param matrix2 Right-hand matrix.
         * @return The matrix product.
         */
        friend Matrix operator*(Matrix matrix1, Matrix matrix2);

        /**
         * @brief Multiplies all matrix elements by a scalar.
         *
         * @param matrix Source matrix.
         * @param scaleFactor Scalar multiplier.
         * @return The scaled matrix.
         */
        friend Matrix operator*(Matrix matrix, float scaleFactor);

        /**
         * @brief Multiplies this matrix by another in place.
         *
         * C# derives `m *= other` from its `operator*` automatically; C++ does not, so the
         * compound form has to be declared for XNA game code to transcribe as written. It is
         * CNAEXT for that reason -- the operation is XNA's, the spelling is C++'s.
         *
         * @param matrix The right-hand matrix.
         * @return Reference to this matrix after multiplication.
         */
        CNAEXT Matrix& operator*=(const Matrix& matrix);

        /**
         * @brief Multiplies all elements of this matrix by a scalar in place.
         *
         * The scalar counterpart of the matrix `*=`; see its note for why this is CNAEXT.
         *
         * @param scaleFactor Scalar multiplier.
         * @return Reference to this matrix after scaling.
         */
        CNAEXT Matrix& operator*=(float scaleFactor);

        /**
         * @brief Subtracts one matrix from another component-wise.
         *
         * @param matrix1 Left-hand matrix.
         * @param matrix2 Right-hand matrix.
         * @return The component-wise difference.
         */
        friend Matrix operator-(Matrix matrix1, Matrix matrix2);

        /**
         * @brief Negates all matrix elements.
         *
         * @param matrix Source matrix.
         * @return The negated matrix.
         */
        friend Matrix operator-(Matrix matrix);

        /**
         * @brief Returns this matrix in the transposed column-major form expected by OpenGL-style uniform uploads.
         *
         * @param out Array of 16 floats that receives the column-major matrix data.
         */
        CNAEXT void ToColumnMajor(float out[16]) const;

    private:
        [[nodiscard]] std::string getDebugDisplayStringProperty() const;
        void CheckForNaNs() const;
    };
}
