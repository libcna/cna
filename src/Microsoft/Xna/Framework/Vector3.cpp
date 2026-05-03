//
// Created by robertvokac on 5/24/25.
//

#include "Microsoft/Xna/Framework/Vector3.hpp"
#include <cmath>

namespace Microsoft::Xna::Framework {

    const Vector3 Vector3::Zero     (0.0f, 0.0f, 0.0f);
    const Vector3 Vector3::One      (1.0f, 1.0f, 1.0f);
    const Vector3 Vector3::UnitX    (1.0f, 0.0f, 0.0f);
    const Vector3 Vector3::UnitY    (0.0f, 1.0f, 0.0f);
    const Vector3 Vector3::UnitZ    (0.0f, 0.0f, 1.0f);
    const Vector3 Vector3::Up       (0.0f, 1.0f, 0.0f);
    const Vector3 Vector3::Down     (0.0f, -1.0f, 0.0f);
    const Vector3 Vector3::Right    (1.0f, 0.0f, 0.0f);
    const Vector3 Vector3::Left     (-1.0f, 0.0f, 0.0f);
    const Vector3 Vector3::Forward  (0.0f, 0.0f, -1.0f);
    const Vector3 Vector3::Backward (0.0f, 0.0f, 1.0f);

    Vector3::Vector3() : X(0), Y(0), Z(0) {}
    Vector3::Vector3(const float x, const float y, const float z) : X(x), Y(y), Z(z) {}

    float Vector3::Length() const {
        return std::sqrt(X*X + Y*Y + Z*Z);
    }
    float Vector3::LengthSquared() const {
        return X*X + Y*Y + Z*Z;
    }
    void Vector3::Normalize() {
        const float len = Length();
        if (len > 0.0f) {
            X /= len; Y /= len; Z /= len;
        }
    }
    float Vector3::Dot(const Vector3& a, const Vector3& b) {
        return a.X*b.X + a.Y*b.Y + a.Z*b.Z;
    }
    Vector3 Vector3::Cross(const Vector3& a, const Vector3& b) {
        return Vector3(
            a.Y*b.Z - a.Z*b.Y,
            a.Z*b.X - a.X*b.Z,
            a.X*b.Y - a.Y*b.X
        );
    }
    Vector3 Vector3::Normalize(const Vector3& v) {
        Vector3 r = v; r.Normalize(); return r;
    }

    Vector3 Vector3::operator+(const Vector3& r) const { return Vector3(X+r.X, Y+r.Y, Z+r.Z); }
    Vector3 Vector3::operator-(const Vector3& r) const { return Vector3(X-r.X, Y-r.Y, Z-r.Z); }
    Vector3 Vector3::operator-() const                 { return Vector3(-X, -Y, -Z); }
    Vector3 Vector3::operator*(float s) const          { return Vector3(X*s, Y*s, Z*s); }
    Vector3 Vector3::operator/(float s) const          { return Vector3(X/s, Y/s, Z/s); }
    Vector3& Vector3::operator+=(const Vector3& r)     { X+=r.X; Y+=r.Y; Z+=r.Z; return *this; }
    Vector3& Vector3::operator-=(const Vector3& r)     { X-=r.X; Y-=r.Y; Z-=r.Z; return *this; }
    Vector3& Vector3::operator*=(float s)              { X*=s; Y*=s; Z*=s; return *this; }

    Vector3 operator*(float s, const Vector3& v) { return v * s; }
}
