#pragma once

#include "Microsoft/Xna/Framework/Vector3.hpp"

namespace Microsoft::Xna::Framework {

    /**
     * @brief 4x4 single-precision matrix in row-major XNA layout.
     *
     * Storage matches the public layout of XNA `Microsoft.Xna.Framework.Matrix`:
     * the named fields go row by row (M11..M14, M21..M24, ...).
     *
     * @note Status: PARTIAL. Provides identity, multiply, translation, scale,
     *       rotation around the principal axes and the most common camera/projection
     *       helpers (`CreateLookAt`, `CreatePerspectiveFieldOfView`,
     *       `CreateOrthographicOffCenter`). Many XNA helpers (decompose, invert,
     *       quaternion construction, ...) are not yet implemented.
     */
    struct Matrix {
        float M11, M12, M13, M14;
        float M21, M22, M23, M24;
        float M31, M32, M33, M34;
        float M41, M42, M43, M44;

        Matrix();
        Matrix(float m11, float m12, float m13, float m14,
               float m21, float m22, float m23, float m24,
               float m31, float m32, float m33, float m34,
               float m41, float m42, float m43, float m44);

        static Matrix Identity();

        /** Translation matrix from a Vector3. */
        static Matrix CreateTranslation(const Vector3& position);
        static Matrix CreateTranslation(float x, float y, float z);

        /** Uniform scale. */
        static Matrix CreateScale(float scale);
        /** Non-uniform scale. */
        static Matrix CreateScale(float sx, float sy, float sz);
        static Matrix CreateScale(const Vector3& scale);

        /** Rotation around X axis (radians). */
        static Matrix CreateRotationX(float radians);
        /** Rotation around Y axis (radians). */
        static Matrix CreateRotationY(float radians);
        /** Rotation around Z axis (radians). */
        static Matrix CreateRotationZ(float radians);

        /** Right-handed look-at view matrix (XNA convention). */
        static Matrix CreateLookAt(const Vector3& cameraPosition,
                                   const Vector3& cameraTarget,
                                   const Vector3& cameraUpVector);

        /** Right-handed perspective FoV projection (XNA convention). */
        static Matrix CreatePerspectiveFieldOfView(float fieldOfView,
                                                   float aspectRatio,
                                                   float nearPlaneDistance,
                                                   float farPlaneDistance);

        /** Right-handed off-center orthographic projection. */
        static Matrix CreateOrthographicOffCenter(float left, float right,
                                                  float bottom, float top,
                                                  float zNearPlane, float zFarPlane);

        /** Matrix multiplication. */
        static Matrix Multiply(const Matrix& a, const Matrix& b);

        Matrix operator*(const Matrix& rhs) const;

        /**
         * @brief Returns a column-major (OpenGL-style) copy of this matrix.
         *
         * @param out Pointer to 16 floats receiving the column-major data.
         *
         * @note CNA-specific helper. Used internally by the EasyGL backend
         *       to upload uniforms; intentionally not part of the original
         *       XNA API.
         */
        void ToColumnMajor(float out[16]) const;
    };
}
