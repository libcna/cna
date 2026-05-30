#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include <algorithm>
#include <sstream>

namespace Microsoft::Xna::Framework
{
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
        return X + Width;
    }

    intcs Rectangle::getTopProperty() const
    {
        return Y;
    }

    intcs Rectangle::getBottomProperty() const
    {
        return Y + Height;
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
        return Point(X + (Width / 2), Y + (Height / 2));
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
            x < X + Width &&
            Y <= y &&
            y < Y + Height;
    }

    bool Rectangle::Contains(Point value) const
    {
        return X <= value.X &&
            value.X < X + Width &&
            Y <= value.Y &&
            value.Y < Y + Height;
    }

    bool Rectangle::Contains(Rectangle value) const
    {
        return X <= value.X &&
            value.X + value.Width <= X + Width &&
            Y <= value.Y &&
            value.Y + value.Height <= Y + Height;
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
        X += offset.X;
        Y += offset.Y;
    }

    void Rectangle::Offset(intcs offsetX, intcs offsetY)
    {
        X += offsetX;
        Y += offsetY;
    }

    void Rectangle::Inflate(intcs horizontalValue, intcs verticalValue)
    {
        X -= horizontalValue;
        Y -= verticalValue;
        Width += horizontalValue * 2;
        Height += verticalValue * 2;
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
        return X ^ Y ^ Width ^ Height;
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
        if (value1.Intersects(value2))
        {
            const intcs rightSide = std::min(value1.X + value1.Width, value2.X + value2.Width);
            const intcs leftSide = std::max(value1.X, value2.X);
            const intcs topSide = std::max(value1.Y, value2.Y);
            const intcs bottomSide = std::min(value1.Y + value1.Height, value2.Y + value2.Height);

            result = Rectangle(
                leftSide,
                topSide,
                rightSide - leftSide,
                bottomSide - topSide
            );
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
            std::max(value1.getRightProperty(), value2.getRightProperty()) - x,
            std::max(value1.getBottomProperty(), value2.getBottomProperty()) - y
        );
    }

    void Rectangle::Union(const Rectangle& value1, const Rectangle& value2, Rectangle& result)
    {
        result.X = std::min(value1.X, value2.X);
        result.Y = std::min(value1.Y, value2.Y);
        result.Width = std::max(value1.getRightProperty(), value2.getRightProperty()) - result.X;
        result.Height = std::max(value1.getBottomProperty(), value2.getBottomProperty()) - result.Y;
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
