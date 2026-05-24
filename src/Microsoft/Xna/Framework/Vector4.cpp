//
// Vector4.cpp — implementation of Microsoft::Xna::Framework::Vector4.
//

#include "Microsoft/Xna/Framework/Vector4.hpp"

namespace Microsoft::Xna::Framework
{
    const Vector4 Vector4::Zero(0.0f, 0.0f, 0.0f, 0.0f);
    const Vector4 Vector4::One (1.0f, 1.0f, 1.0f, 1.0f);

    Vector4::Vector4() : X(0.0f), Y(0.0f), Z(0.0f), W(0.0f) {}

    Vector4::Vector4(float x, float y, float z, float w) : X(x), Y(y), Z(z), W(w) {}

    bool Vector4::operator==(const Vector4& rhs) const
    {
        return X == rhs.X && Y == rhs.Y && Z == rhs.Z && W == rhs.W;
    }

    bool Vector4::operator!=(const Vector4& rhs) const
    {
        return !(*this == rhs);
    }
}
