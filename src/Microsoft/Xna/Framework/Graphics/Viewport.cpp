// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include <string>

namespace Microsoft::Xna::Framework::Graphics
{
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Rectangle;
    using Microsoft::Xna::Framework::Vector3;

    IMPL_PROP(int, Height,     getter1, setter1, member0, static0, constret1, ref1, constmet1, Viewport, nothing)
    IMPL_PROP(float, MaxDepth, getter1, setter1, member0, static0, constret1, ref1, constmet1, Viewport, nothing)
    IMPL_PROP(float, MinDepth, getter1, setter1, member0, static0, constret1, ref1, constmet1, Viewport, nothing)
    IMPL_PROP(int, Width,      getter1, setter1, member0, static0, constret1, ref1, constmet1, Viewport, nothing)
    IMPL_PROP(int, Y,         getter1, setter1, member0, static0, constret1, ref1, constmet1, Viewport, nothing)
    IMPL_PROP(int, X,         getter1, setter1, member0, static0, constret1, ref1, constmet1, Viewport, nothing)

    Viewport::Viewport()
        : Height_(0), MaxDepth_(1.0f), MinDepth_(0.0f), Width_(0), Y_(0), X_(0)
    {
    }

    Viewport::Viewport(int x_, int y_, int width, int height)
        : Height_(height), MaxDepth_(1.0f), MinDepth_(0.0f), Width_(width), Y_(y_), X_(x_)
    {
    }

    Viewport::Viewport(const Rectangle& bounds)
        : Height_(bounds.Height), MaxDepth_(1.0f), MinDepth_(0.0f), Width_(bounds.Width),
          Y_(bounds.Y), X_(bounds.X)
    {
    }

    float Viewport::getAspectRatioProperty() const
    {
        if (Height_ != 0 && Width_ != 0)
            return static_cast<float>(Width_) / static_cast<float>(Height_);
        return 0.0f;
    }

    Rectangle Viewport::getBoundsProperty() const
    {
        return Rectangle(X_, Y_, Width_, Height_);
    }

    void Viewport::setBoundsProperty(const Rectangle& value)
    {
        X_      = value.X;
        Y_      = value.Y;
        Width_  = value.Width;
        Height_ = value.Height;
    }

    Rectangle Viewport::getTitleSafeAreaProperty() const
    {
        return getBoundsProperty();
    }

    Vector3 Viewport::Project(Vector3 source,
                               const Matrix& projection,
                               const Matrix& view,
                               const Matrix& world) const
    {
        const Matrix matrix = world * view * projection;
        Vector3 vector = Vector3::Transform(source, matrix);

        const float a = (source.X * matrix.M14 + source.Y * matrix.M24 +
                         source.Z * matrix.M34) + matrix.M44;
        if (!MathHelper::WithinEpsilon(a, 1.0f))
        {
            vector.X /= a;
            vector.Y /= a;
            vector.Z /= a;
        }

        vector.X = ((vector.X + 1.0f) * 0.5f) * static_cast<float>(Width_)  + static_cast<float>(X_);
        vector.Y = ((-vector.Y + 1.0f) * 0.5f) * static_cast<float>(Height_) + static_cast<float>(Y_);
        vector.Z = vector.Z * (MaxDepth_ - MinDepth_) + MinDepth_;
        return vector;
    }

    Vector3 Viewport::Unproject(Vector3 source,
                                 const Matrix& projection,
                                 const Matrix& view,
                                 const Matrix& world) const
    {
        const Matrix matrix = Matrix::Invert(world * view * projection);

        source.X = ((source.X - static_cast<float>(X_)) / static_cast<float>(Width_))  * 2.0f - 1.0f;
        source.Y = -(((source.Y - static_cast<float>(Y_)) / static_cast<float>(Height_)) * 2.0f - 1.0f);
        source.Z = (source.Z - MinDepth_) / (MaxDepth_ - MinDepth_);

        Vector3 vector = Vector3::Transform(source, matrix);

        const float a = (source.X * matrix.M14 + source.Y * matrix.M24 +
                         source.Z * matrix.M34) + matrix.M44;
        if (!MathHelper::WithinEpsilon(a, 1.0f))
        {
            vector.X /= a;
            vector.Y /= a;
            vector.Z /= a;
        }

        return vector;
    }

    std::string Viewport::ToString() const
    {
        return "{X:" + std::to_string(X_) +
               " Y:" + std::to_string(Y_) +
               " Width:" + std::to_string(Width_) +
               " Height:" + std::to_string(Height_) +
               " MinDepth:" + std::to_string(MinDepth_) +
               " MaxDepth:" + std::to_string(MaxDepth_) + "}";
    }
}
