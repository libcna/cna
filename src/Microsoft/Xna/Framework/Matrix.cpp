#include "Microsoft/Xna/Framework/Matrix.hpp"
#include <cmath>

namespace Microsoft::Xna::Framework {

    Matrix::Matrix()
        : M11(0), M12(0), M13(0), M14(0),
          M21(0), M22(0), M23(0), M24(0),
          M31(0), M32(0), M33(0), M34(0),
          M41(0), M42(0), M43(0), M44(0) {}

    Matrix::Matrix(float m11, float m12, float m13, float m14,
                   float m21, float m22, float m23, float m24,
                   float m31, float m32, float m33, float m34,
                   float m41, float m42, float m43, float m44)
        : M11(m11), M12(m12), M13(m13), M14(m14),
          M21(m21), M22(m22), M23(m23), M24(m24),
          M31(m31), M32(m32), M33(m33), M34(m34),
          M41(m41), M42(m42), M43(m43), M44(m44) {}

    Matrix Matrix::Identity() {
        return Matrix(
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1
        );
    }

    Matrix Matrix::CreateTranslation(const Vector3& p) {
        return CreateTranslation(p.X, p.Y, p.Z);
    }
    Matrix Matrix::CreateTranslation(float x, float y, float z) {
        Matrix m = Identity();
        m.M41 = x; m.M42 = y; m.M43 = z;
        return m;
    }

    Matrix Matrix::CreateScale(float s) { return CreateScale(s, s, s); }
    Matrix Matrix::CreateScale(const Vector3& s) { return CreateScale(s.X, s.Y, s.Z); }
    Matrix Matrix::CreateScale(float sx, float sy, float sz) {
        Matrix m = Identity();
        m.M11 = sx; m.M22 = sy; m.M33 = sz;
        return m;
    }

    Matrix Matrix::CreateRotationX(float r) {
        const float c = std::cos(r), s = std::sin(r);
        Matrix m = Identity();
        m.M22 =  c; m.M23 =  s;
        m.M32 = -s; m.M33 =  c;
        return m;
    }
    Matrix Matrix::CreateRotationY(float r) {
        const float c = std::cos(r), s = std::sin(r);
        Matrix m = Identity();
        m.M11 =  c; m.M13 = -s;
        m.M31 =  s; m.M33 =  c;
        return m;
    }
    Matrix Matrix::CreateRotationZ(float r) {
        const float c = std::cos(r), s = std::sin(r);
        Matrix m = Identity();
        m.M11 =  c; m.M12 =  s;
        m.M21 = -s; m.M22 =  c;
        return m;
    }

    Matrix Matrix::CreateLookAt(const Vector3& eye,
                                const Vector3& target,
                                const Vector3& up) {
        // Right-handed XNA convention.
        Vector3 zaxis = Vector3::Normalize(eye - target);     // camera looks down -Z
        Vector3 xaxis = Vector3::Normalize(Vector3::Cross(up, zaxis));
        Vector3 yaxis = Vector3::Cross(zaxis, xaxis);

        Matrix m = Identity();
        m.M11 = xaxis.X; m.M12 = yaxis.X; m.M13 = zaxis.X; m.M14 = 0;
        m.M21 = xaxis.Y; m.M22 = yaxis.Y; m.M23 = zaxis.Y; m.M24 = 0;
        m.M31 = xaxis.Z; m.M32 = yaxis.Z; m.M33 = zaxis.Z; m.M34 = 0;
        m.M41 = -Vector3::Dot(xaxis, eye);
        m.M42 = -Vector3::Dot(yaxis, eye);
        m.M43 = -Vector3::Dot(zaxis, eye);
        m.M44 = 1;
        return m;
    }

    Matrix Matrix::CreatePerspectiveFieldOfView(float fov, float aspect,
                                                float zn, float zf) {
        // Right-handed, depth range [-1, 1] (OpenGL-style clip volume).
        const float yScale = 1.0f / std::tan(fov * 0.5f);
        const float xScale = yScale / aspect;
        Matrix m;
        m.M11 = xScale; m.M12 = 0;      m.M13 = 0;                            m.M14 =  0;
        m.M21 = 0;      m.M22 = yScale; m.M23 = 0;                            m.M24 =  0;
        m.M31 = 0;      m.M32 = 0;      m.M33 = (zf + zn) / (zn - zf);        m.M34 = -1;
        m.M41 = 0;      m.M42 = 0;      m.M43 = (2.0f * zf * zn) / (zn - zf); m.M44 =  0;
        return m;
    }

    Matrix Matrix::CreateOrthographicOffCenter(float l, float r,
                                               float b, float t,
                                               float zn, float zf) {
        Matrix m = Identity();
        m.M11 = 2.0f / (r - l);
        m.M22 = 2.0f / (t - b);
        m.M33 = -2.0f / (zf - zn);
        m.M41 = -(r + l) / (r - l);
        m.M42 = -(t + b) / (t - b);
        m.M43 = -(zf + zn) / (zf - zn);
        return m;
    }

    Matrix Matrix::Multiply(const Matrix& a, const Matrix& b) {
        Matrix r;
        r.M11 = a.M11*b.M11 + a.M12*b.M21 + a.M13*b.M31 + a.M14*b.M41;
        r.M12 = a.M11*b.M12 + a.M12*b.M22 + a.M13*b.M32 + a.M14*b.M42;
        r.M13 = a.M11*b.M13 + a.M12*b.M23 + a.M13*b.M33 + a.M14*b.M43;
        r.M14 = a.M11*b.M14 + a.M12*b.M24 + a.M13*b.M34 + a.M14*b.M44;

        r.M21 = a.M21*b.M11 + a.M22*b.M21 + a.M23*b.M31 + a.M24*b.M41;
        r.M22 = a.M21*b.M12 + a.M22*b.M22 + a.M23*b.M32 + a.M24*b.M42;
        r.M23 = a.M21*b.M13 + a.M22*b.M23 + a.M23*b.M33 + a.M24*b.M43;
        r.M24 = a.M21*b.M14 + a.M22*b.M24 + a.M23*b.M34 + a.M24*b.M44;

        r.M31 = a.M31*b.M11 + a.M32*b.M21 + a.M33*b.M31 + a.M34*b.M41;
        r.M32 = a.M31*b.M12 + a.M32*b.M22 + a.M33*b.M32 + a.M34*b.M42;
        r.M33 = a.M31*b.M13 + a.M32*b.M23 + a.M33*b.M33 + a.M34*b.M43;
        r.M34 = a.M31*b.M14 + a.M32*b.M24 + a.M33*b.M34 + a.M34*b.M44;

        r.M41 = a.M41*b.M11 + a.M42*b.M21 + a.M43*b.M31 + a.M44*b.M41;
        r.M42 = a.M41*b.M12 + a.M42*b.M22 + a.M43*b.M32 + a.M44*b.M42;
        r.M43 = a.M41*b.M13 + a.M42*b.M23 + a.M43*b.M33 + a.M44*b.M43;
        r.M44 = a.M41*b.M14 + a.M42*b.M24 + a.M43*b.M34 + a.M44*b.M44;
        return r;
    }

    Matrix Matrix::operator*(const Matrix& rhs) const { return Multiply(*this, rhs); }

    void Matrix::ToColumnMajor(float out[16]) const {
        // XNA stores rows: (M11..M14)=row0. GL expects column-major.
        out[0]  = M11; out[1]  = M21; out[2]  = M31; out[3]  = M41;
        out[4]  = M12; out[5]  = M22; out[6]  = M32; out[7]  = M42;
        out[8]  = M13; out[9]  = M23; out[10] = M33; out[11] = M43;
        out[12] = M14; out[13] = M24; out[14] = M34; out[15] = M44;
    }
}
