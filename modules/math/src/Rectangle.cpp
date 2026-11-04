// SPDX-License-Identifier: MS-PL

#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <sstream>

namespace Microsoft::Xna::Framework
{
    namespace
    {
        // Refresh Rectangle arithmetic uses unchecked Int32 add/sub (for example
        // get_Right IL_000c, get_Center IL_000e/001d, Union IL_00b1/00bc).
        // Unsigned arithmetic defines the same wrap without C++ signed-overflow UB.
        intcs WrapAdd(intcs left, intcs right)
        {
            return std::bit_cast<intcs>(static_cast<std::uint32_t>(left) + static_cast<std::uint32_t>(right));
        }
        intcs WrapSubtract(intcs left, intcs right)
        {
            return std::bit_cast<intcs>(static_cast<std::uint32_t>(left) - static_cast<std::uint32_t>(right));
        }
    }

    const Rectangle Rectangle::Empty(0, 0, 0, 0);

    Rectangle::Rectangle()
        : Rectangle(0, 0, 0, 0)
    {
    }

    Rectangle::Rectangle(intcs x, intcs y, intcs width, intcs height)
        : X(x),
          Y(y),
          Width(width),
          Height(height)
    {
    }

    intcs Rectangle::getLeftProperty() const
    {
        return X;
    }

    intcs Rectangle::getRightProperty() const
    {
        return WrapAdd(X, Width);
    }

    intcs Rectangle::getTopProperty() const
    {
        return Y;
    }

    intcs Rectangle::getBottomProperty() const
    {
        return WrapAdd(Y, Height);
    }

    Point Rectangle::getLocationProperty() const
    {
        return Point(X, Y);
    }

    void Rectangle::setLocationProperty(Point value)
    {
        X = value.X;
        Y = value.Y;
    }

    Point Rectangle::getCenterProperty() const
    {
        return Point(WrapAdd(X, Width / 2), WrapAdd(Y, Height / 2));
    }

    bool Rectangle::getIsEmptyProperty() const
    {
        return Width == 0 && Height == 0 && X == 0 && Y == 0;
    }

    Rectangle Rectangle::getEmptyProperty()
    {
        return Empty;
    }

    bool Rectangle::Contains(intcs x, intcs y) const
    {
        return X <= x &&
            x < getRightProperty() &&
            Y <= y &&
            y < getBottomProperty();
    }

    bool Rectangle::Contains(Point value) const
    {
        return X <= value.X &&
            value.X < getRightProperty() &&
            Y <= value.Y &&
            value.Y < getBottomProperty();
    }

    bool Rectangle::Contains(Rectangle value) const
    {
        return X <= value.X &&
            value.getRightProperty() <= getRightProperty() &&
            Y <= value.Y &&
            value.getBottomProperty() <= getBottomProperty();
    }

    void Rectangle::Contains(const Point& value, bool& result) const
    {
        result = Contains(value);
    }

    void Rectangle::Contains(const Rectangle& value, bool& result) const
    {
        result = Contains(value);
    }

    void Rectangle::Offset(Point offset)
    {
        X = WrapAdd(X, offset.X);
        Y = WrapAdd(Y, offset.Y);
    }

    void Rectangle::Offset(intcs offsetX, intcs offsetY)
    {
        X = WrapAdd(X, offsetX);
        Y = WrapAdd(Y, offsetY);
    }

    void Rectangle::Inflate(intcs horizontalValue, intcs verticalValue)
    {
        X = WrapSubtract(X, horizontalValue);
        Y = WrapSubtract(Y, verticalValue);
        Width = WrapAdd(Width, WrapAdd(horizontalValue, horizontalValue));
        Height = WrapAdd(Height, WrapAdd(verticalValue, verticalValue));
    }

    bool Rectangle::Equals(const std::any& obj) const
    {
        const Rectangle* other = std::any_cast<Rectangle>(&obj);
        return other != nullptr && Equals(*other);
    }

    bool Rectangle::Equals(const Rectangle& other) const
    {
        return *this == other;
    }

    std::string Rectangle::ToString() const
    {
        std::ostringstream stream;
        stream << "{X:" << X << " Y:" << Y << " Width:" << Width << " Height:" << Height << "}";
        return stream.str();
    }

    intcs Rectangle::GetHashCode() const
    {
        // XNA uses unchecked 32-bit addition; unsigned arithmetic defines the wrap.
        return std::bit_cast<intcs>(static_cast<std::uint32_t>(X) + static_cast<std::uint32_t>(Y) +
            static_cast<std::uint32_t>(Width) + static_cast<std::uint32_t>(Height));
    }

    bool Rectangle::Intersects(Rectangle value) const
    {
        return value.getLeftProperty() < getRightProperty() &&
            getLeftProperty() < value.getRightProperty() &&
            value.getTopProperty() < getBottomProperty() &&
            getTopProperty() < value.getBottomProperty();
    }

    void Rectangle::Intersects(const Rectangle& value, bool& result) const
    {
        result = Intersects(value);
    }

    Rectangle Rectangle::Intersect(Rectangle value1, Rectangle value2)
    {
        Rectangle result;
        Intersect(value1, value2, result);
        return result;
    }

    void Rectangle::Intersect(const Rectangle& value1, const Rectangle& value2, Rectangle& result)
    {
        const intcs rightSide = std::min(value1.getRightProperty(), value2.getRightProperty());
        const intcs leftSide = std::max(value1.X, value2.X);
        const intcs topSide = std::max(value1.Y, value2.Y);
        const intcs bottomSide = std::min(value1.getBottomProperty(), value2.getBottomProperty());
        // Refresh Intersect(value,value) IL_00a0-00a6 and ref/ref/out IL_0090-0096
        // require strictly positive endpoint overlap, as does current FNA. The
        // former CNA Intersects gate accepted degenerate interior rectangles.
        if (rightSide > leftSide && bottomSide > topSide)
        {
            result = Rectangle(leftSide, topSide,
                WrapSubtract(rightSide, leftSide), WrapSubtract(bottomSide, topSide));
        }
        else
        {
            result = Rectangle(0, 0, 0, 0);
        }
    }

    Rectangle Rectangle::Union(Rectangle value1, Rectangle value2)
    {
        const intcs x = std::min(value1.X, value2.X);
        const intcs y = std::min(value1.Y, value2.Y);

        return Rectangle(
            x,
            y,
            WrapSubtract(std::max(value1.getRightProperty(), value2.getRightProperty()), x),
            WrapSubtract(std::max(value1.getBottomProperty(), value2.getBottomProperty()), y)
        );
    }

    void Rectangle::Union(const Rectangle& value1, const Rectangle& value2, Rectangle& result)
    {
        // Compute before assigning: either input may also be the output.
        result = Union(value1, value2);
    }

    std::string Rectangle::getDebugDisplayStringProperty() const
    {
        std::ostringstream stream;
        stream << X << " " << Y << " " << Width << " " << Height;
        return stream.str();
    }

    bool operator==(Rectangle value1, Rectangle value2)
    {
        return value1.X == value2.X &&
            value1.Y == value2.Y &&
            value1.Width == value2.Width &&
            value1.Height == value2.Height;
    }

    bool operator!=(Rectangle value1, Rectangle value2)
    {
        return !(value1 == value2);
    }
}
