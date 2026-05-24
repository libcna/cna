#pragma once

#include <optional>
#include <string>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

namespace Microsoft::Xna::Framework
{
    struct Plane;
    struct Quaternion;

    /// Represents a right-handed 4x4 matrix storing translation, scale and rotation data.
    struct Matrix
    {
        /// Returns the identity matrix.
        static const Matrix getIdentityProperty();

        /// A first row and first column value.
        float M11;
        /// A first row and second column value.
        float M12;
        /// A first row and third column value.
        float M13;
        /// A first row and fourth column value.
        float M14;

        /// A second row and first column value.
        float M21;
        /// A second row and second column value.
        float M22;
        /// A second row and third column value.
        float M23;
        /// A second row and fourth column value.
        float M24;

        /// A third row and first column value.
        float M31;
        /// A third row and second column value.
        float M32;
        /// A third row and third column value.
        float M33;
        /// A third row and fourth column value.
        float M34;

        /// A fourth row and first column value.
        float M41;
        /// A fourth row and second column value.
        float M42;
        /// A fourth row and third column value.
        float M43;
        /// A fourth row and fourth column value.
        float M44;

        /// Constructs a zero-filled matrix.
        Matrix();

        /// Constructs a matrix from all 16 row-major field values.
        Matrix(
            float m11, float m12, float m13, float m14,
            float m21, float m22, float m23, float m24,
            float m31, float m32, float m33, float m34,
            float m41, float m42, float m43, float m44
        );

        /// Gets the backward vector from the third matrix row.
        [[nodiscard]] Vector3 getBackwardProperty() const;
        /// Sets the backward vector in the third matrix row.
        void setBackwardProperty(Vector3 value);

        /// Gets the down vector from the negated second matrix row.
        [[nodiscard]] Vector3 getDownProperty() const;
        /// Sets the down vector into the negated second matrix row.
        void setDownProperty(Vector3 value);

        /// Gets the forward vector from the negated third matrix row.
        [[nodiscard]] Vector3 getForwardProperty() const;
        /// Sets the forward vector into the negated third matrix row.
        void setForwardProperty(Vector3 value);

        /// Gets the left vector from the negated first matrix row.
        [[nodiscard]] Vector3 getLeftProperty() const;
        /// Sets the left vector into the negated first matrix row.
        void setLeftProperty(Vector3 value);

        /// Gets the right vector from the first matrix row.
        [[nodiscard]] Vector3 getRightProperty() const;
        /// Sets the right vector into the first matrix row.
        void setRightProperty(Vector3 value);

        /// Gets the translation stored in this matrix.
        [[nodiscard]] Vector3 getTranslationProperty() const;
        /// Sets the translation stored in this matrix.
        void setTranslationProperty(Vector3 value);

        /// Gets the up vector from the second matrix row.
        [[nodiscard]] Vector3 getUpProperty() const;
        /// Sets the up vector into the second matrix row.
        void setUpProperty(Vector3 value);

        /// Decomposes this matrix into scale, rotation and translation parts.
        [[nodiscard]] bool Decompose(Vector3& scale, Quaternion& rotation, Vector3& translation) const;

        /// Returns the determinant of this matrix.
        [[nodiscard]] float Determinant() const;

        /// Compares this matrix with another matrix without tolerance.
        [[nodiscard]] bool Equals(const Matrix& other) const;

        /// Returns a hash code for this matrix.
        [[nodiscard]] int GetHashCode() const;

        /// Returns a string representation of all 16 fields.
        [[nodiscard]] std::string ToString() const;

        /// Adds two matrices component by component.
        [[nodiscard]] static Matrix Add(Matrix matrix1, Matrix matrix2);
        /// Adds two matrices component by component and stores the result in an output parameter.
        static void Add(const Matrix& matrix1, const Matrix& matrix2, Matrix& result);

        /// Creates a spherical billboard matrix.
        [[nodiscard]] static Matrix CreateBillboard(
            Vector3 objectPosition,
            Vector3 cameraPosition,
            Vector3 cameraUpVector,
            std::optional<Vector3> cameraForwardVector
        );

        /// Creates a spherical billboard matrix in an output parameter.
        static void CreateBillboard(
            const Vector3& objectPosition,
            const Vector3& cameraPosition,
            const Vector3& cameraUpVector,
            std::optional<Vector3> cameraForwardVector,
            Matrix& result
        );

        /// Creates a cylindrical billboard matrix constrained to a rotation axis.
        [[nodiscard]] static Matrix CreateConstrainedBillboard(
            Vector3 objectPosition,
            Vector3 cameraPosition,
            Vector3 rotateAxis,
            std::optional<Vector3> cameraForwardVector,
            std::optional<Vector3> objectForwardVector
        );

        /// Creates a cylindrical billboard matrix constrained to a rotation axis in an output parameter.
        static void CreateConstrainedBillboard(
            const Vector3& objectPosition,
            const Vector3& cameraPosition,
            const Vector3& rotateAxis,
            std::optional<Vector3> cameraForwardVector,
            std::optional<Vector3> objectForwardVector,
            Matrix& result
        );

        /// Creates a rotation matrix from an axis and angle in radians.
        [[nodiscard]] static Matrix CreateFromAxisAngle(Vector3 axis, float angle);
        /// Creates a rotation matrix from an axis and angle in radians in an output parameter.
        static void CreateFromAxisAngle(const Vector3& axis, float angle, Matrix& result);

        /// Creates a rotation matrix from a quaternion.
        [[nodiscard]] static Matrix CreateFromQuaternion(Quaternion quaternion);
        /// Creates a rotation matrix from a quaternion in an output parameter.
        static void CreateFromQuaternion(const Quaternion& quaternion, Matrix& result);

        /// Creates a rotation matrix from yaw, pitch and roll angles in radians.
        [[nodiscard]] static Matrix CreateFromYawPitchRoll(float yaw, float pitch, float roll);
        /// Creates a rotation matrix from yaw, pitch and roll angles in an output parameter.
        static void CreateFromYawPitchRoll(float yaw, float pitch, float roll, Matrix& result);

        /// Creates a right-handed view matrix.
        [[nodiscard]] static Matrix CreateLookAt(Vector3 cameraPosition, Vector3 cameraTarget, Vector3 cameraUpVector);
        /// Creates a right-handed view matrix in an output parameter.
        static void CreateLookAt(const Vector3& cameraPosition, const Vector3& cameraTarget, const Vector3& cameraUpVector, Matrix& result);

        /// Creates an orthographic projection matrix.
        [[nodiscard]] static Matrix CreateOrthographic(float width, float height, float zNearPlane, float zFarPlane);
        /// Creates an orthographic projection matrix in an output parameter.
        static void CreateOrthographic(float width, float height, float zNearPlane, float zFarPlane, Matrix& result);

        /// Creates an off-center orthographic projection matrix.
        [[nodiscard]] static Matrix CreateOrthographicOffCenter(float left, float right, float bottom, float top, float zNearPlane, float zFarPlane);
        /// Creates an off-center orthographic projection matrix in an output parameter.
        static void CreateOrthographicOffCenter(float left, float right, float bottom, float top, float zNearPlane, float zFarPlane, Matrix& result);

        /// Creates a perspective projection matrix.
        [[nodiscard]] static Matrix CreatePerspective(float width, float height, float nearPlaneDistance, float farPlaneDistance);
        /// Creates a perspective projection matrix in an output parameter.
        static void CreatePerspective(float width, float height, float nearPlaneDistance, float farPlaneDistance, Matrix& result);

        /// Creates a perspective projection matrix using a field of view.
        [[nodiscard]] static Matrix CreatePerspectiveFieldOfView(float fieldOfView, float aspectRatio, float nearPlaneDistance, float farPlaneDistance);
        /// Creates a perspective projection matrix using a field of view in an output parameter.
        static void CreatePerspectiveFieldOfView(float fieldOfView, float aspectRatio, float nearPlaneDistance, float farPlaneDistance, Matrix& result);

        /// Creates an off-center perspective projection matrix.
        [[nodiscard]] static Matrix CreatePerspectiveOffCenter(float left, float right, float bottom, float top, float nearPlaneDistance, float farPlaneDistance);
        /// Creates an off-center perspective projection matrix in an output parameter.
        static void CreatePerspectiveOffCenter(float left, float right, float bottom, float top, float nearPlaneDistance, float farPlaneDistance, Matrix& result);

        /// Creates a rotation matrix around the X axis.
        [[nodiscard]] static Matrix CreateRotationX(float radians);
        /// Creates a rotation matrix around the X axis in an output parameter.
        static void CreateRotationX(float radians, Matrix& result);

        /// Creates a rotation matrix around the Y axis.
        [[nodiscard]] static Matrix CreateRotationY(float radians);
        /// Creates a rotation matrix around the Y axis in an output parameter.
        static void CreateRotationY(float radians, Matrix& result);

        /// Creates a rotation matrix around the Z axis.
        [[nodiscard]] static Matrix CreateRotationZ(float radians);
        /// Creates a rotation matrix around the Z axis in an output parameter.
        static void CreateRotationZ(float radians, Matrix& result);

        /// Creates a uniform scale matrix.
        [[nodiscard]] static Matrix CreateScale(float scale);
        /// Creates a uniform scale matrix in an output parameter.
        static void CreateScale(float scale, Matrix& result);

        /// Creates a non-uniform scale matrix.
        [[nodiscard]] static Matrix CreateScale(float xScale, float yScale, float zScale);
        /// Creates a non-uniform scale matrix in an output parameter.
        static void CreateScale(float xScale, float yScale, float zScale, Matrix& result);

        /// Creates a scale matrix from a vector.
        [[nodiscard]] static Matrix CreateScale(Vector3 scales);
        /// Creates a scale matrix from a vector in an output parameter.
        static void CreateScale(const Vector3& scales, Matrix& result);

        /// Creates a shadow projection matrix using a light direction and plane.
        [[nodiscard]] static Matrix CreateShadow(Vector3 lightDirection, Plane plane);
        /// Creates a shadow projection matrix in an output parameter.
        static void CreateShadow(const Vector3& lightDirection, const Plane& plane, Matrix& result);

        /// Creates a translation matrix from coordinates.
        [[nodiscard]] static Matrix CreateTranslation(float xPosition, float yPosition, float zPosition);
        /// Creates a translation matrix from a vector.
        [[nodiscard]] static Matrix CreateTranslation(Vector3 position);
        /// Creates a translation matrix from a vector in an output parameter.
        static void CreateTranslation(const Vector3& position, Matrix& result);
        /// Creates a translation matrix from coordinates in an output parameter.
        static void CreateTranslation(float xPosition, float yPosition, float zPosition, Matrix& result);

        /// Creates a reflection matrix from a plane.
        [[nodiscard]] static Matrix CreateReflection(Plane value);
        /// Creates a reflection matrix from a plane in an output parameter.
        static void CreateReflection(const Plane& value, Matrix& result);

        /// Creates a world matrix from position, forward and up vectors.
        [[nodiscard]] static Matrix CreateWorld(Vector3 position, Vector3 forward, Vector3 up);
        /// Creates a world matrix in an output parameter.
        static void CreateWorld(const Vector3& position, const Vector3& forward, const Vector3& up, Matrix& result);

        /// Divides one matrix by another component by component.
        [[nodiscard]] static Matrix Divide(Matrix matrix1, Matrix matrix2);
        /// Divides one matrix by another component by component in an output parameter.
        static void Divide(const Matrix& matrix1, const Matrix& matrix2, Matrix& result);

        /// Divides a matrix by a scalar.
        [[nodiscard]] static Matrix Divide(Matrix matrix1, float divider);
        /// Divides a matrix by a scalar in an output parameter.
        static void Divide(const Matrix& matrix1, float divider, Matrix& result);

        /// Returns the inverse of a matrix.
        [[nodiscard]] static Matrix Invert(Matrix matrix);
        /// Returns the inverse of a matrix in an output parameter.
        static void Invert(const Matrix& matrix, Matrix& result);

        /// Linearly interpolates between two matrices.
        [[nodiscard]] static Matrix Lerp(Matrix matrix1, Matrix matrix2, float amount);
        /// Linearly interpolates between two matrices in an output parameter.
        static void Lerp(const Matrix& matrix1, const Matrix& matrix2, float amount, Matrix& result);

        /// Multiplies two matrices.
        [[nodiscard]] static Matrix Multiply(Matrix matrix1, Matrix matrix2);
        /// Multiplies two matrices in an output parameter.
        static void Multiply(const Matrix& matrix1, const Matrix& matrix2, Matrix& result);

        /// Multiplies a matrix by a scalar.
        [[nodiscard]] static Matrix Multiply(Matrix matrix1, float scaleFactor);
        /// Multiplies a matrix by a scalar in an output parameter.
        static void Multiply(const Matrix& matrix1, float scaleFactor, Matrix& result);

        /// Negates every matrix component.
        [[nodiscard]] static Matrix Negate(Matrix matrix);
        /// Negates every matrix component in an output parameter.
        static void Negate(const Matrix& matrix, Matrix& result);

        /// Subtracts one matrix from another.
        [[nodiscard]] static Matrix Subtract(Matrix matrix1, Matrix matrix2);
        /// Subtracts one matrix from another in an output parameter.
        static void Subtract(const Matrix& matrix1, const Matrix& matrix2, Matrix& result);

        /// Transposes a matrix.
        [[nodiscard]] static Matrix Transpose(Matrix matrix);
        /// Transposes a matrix in an output parameter.
        static void Transpose(const Matrix& matrix, Matrix& result);

        /// Transforms a matrix by a quaternion rotation.
        [[nodiscard]] static Matrix Transform(Matrix value, Quaternion rotation);
        /// Transforms a matrix by a quaternion rotation in an output parameter.
        static void Transform(const Matrix& value, const Quaternion& rotation, Matrix& result);

        friend Matrix operator+(Matrix matrix1, Matrix matrix2);
        friend Matrix operator/(Matrix matrix1, Matrix matrix2);
        friend Matrix operator/(Matrix matrix, float divider);
        friend bool operator==(Matrix matrix1, Matrix matrix2);
        friend bool operator!=(Matrix matrix1, Matrix matrix2);
        friend Matrix operator*(Matrix matrix1, Matrix matrix2);
        friend Matrix operator*(Matrix matrix, float scaleFactor);
        friend Matrix operator-(Matrix matrix1, Matrix matrix2);
        friend Matrix operator-(Matrix matrix);

        /// Returns this matrix in the transposed column-major form expected by OpenGL-style uniform uploads.
        NOXNA void ToColumnMajor(float out[16]) const;

    private:
        [[nodiscard]] std::string getDebugDisplayStringProperty() const;
        void CheckForNaNs() const;
    };
}
