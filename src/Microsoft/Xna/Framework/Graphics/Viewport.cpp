// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Rectangle;
    using Microsoft::Xna::Framework::Vector3;

    IMPL_PROP(int, Height, getter1, setter1, member0, static0, constret1, ref1, constmet1, Viewport, nothing)
    IMPL_PROP(int, Width,  getter1, setter1, member0, static0, constret1, ref1, constmet1, Viewport, nothing)

    Viewport::Viewport()
        : x(0), y(0), Height_(0), Width_(0), minDepth(0.0f), maxDepth(1.0f)
    {
    }

    Viewport::Viewport(int x_, int y_, int width, int height)
        : x(x_), y(y_), Height_(height), Width_(width), minDepth(0.0f), maxDepth(1.0f)
    {
    }

    Viewport::Viewport(const Rectangle& bounds)
        : x(bounds.X), y(bounds.Y), Height_(bounds.Height), Width_(bounds.Width),
          minDepth(0.0f), maxDepth(1.0f)
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
        return Rectangle(x, y, Width_, Height_);
    }

    void Viewport::setBoundsProperty(const Rectangle& value)
    {
        x       = value.X;
        y       = value.Y;
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

        vector.X = ((vector.X + 1.0f) * 0.5f) * static_cast<float>(Width_)  + static_cast<float>(x);
        vector.Y = ((-vector.Y + 1.0f) * 0.5f) * static_cast<float>(Height_) + static_cast<float>(y);
        vector.Z = vector.Z * (maxDepth - minDepth) + minDepth;
        return vector;
    }

    Vector3 Viewport::Unproject(Vector3 source,
                                 const Matrix& projection,
                                 const Matrix& view,
                                 const Matrix& world) const
    {
        const Matrix matrix = Matrix::Invert(world * view * projection);

        source.X = ((source.X - static_cast<float>(x)) / static_cast<float>(Width_))  * 2.0f - 1.0f;
        source.Y = -(((source.Y - static_cast<float>(y)) / static_cast<float>(Height_)) * 2.0f - 1.0f);
        source.Z = (source.Z - minDepth) / (maxDepth - minDepth);

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
}
