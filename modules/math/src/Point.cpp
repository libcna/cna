// SPDX-License-Identifier: MS-PL

#include "Microsoft/Xna/Framework/Point.hpp"

#include <bit>
#include <cstdint>
#include <sstream>

namespace Microsoft::Xna::Framework
{
    const Point Point::Zero(0, 0);

    Point::Point()
        : X(0),
          Y(0)
    {
    }

    Point::Point(intcs x, intcs y)
        : X(x),
          Y(y)
    {
    }

    Point Point::getZeroProperty()
    {
        return Zero;
    }

    bool Point::Equals(const std::any& obj) const
    {
        const Point* other = std::any_cast<Point>(&obj);
        return other != nullptr && Equals(*other);
    }

    bool Point::Equals(const Point& other) const
    {
        return X == other.X && Y == other.Y;
    }

    intcs Point::GetHashCode() const
    {
        // XNA uses unchecked 32-bit addition; unsigned arithmetic defines the wrap.
        return std::bit_cast<intcs>(static_cast<std::uint32_t>(X) + static_cast<std::uint32_t>(Y));
    }

    std::string Point::ToString() const
    {
        std::ostringstream stream;
        stream << "{X:" << X << " Y:" << Y << "}";
        return stream.str();
    }

    std::string Point::getDebugDisplayStringProperty() const
    {
        std::ostringstream stream;
        stream << X << " " << Y;
        return stream.str();
    }

    Point operator+(Point value1, Point value2)
    {
        return Point(value1.X + value2.X, value1.Y + value2.Y);
    }

    Point operator-(Point value1, Point value2)
    {
        return Point(value1.X - value2.X, value1.Y - value2.Y);
    }

    Point operator*(Point value1, Point value2)
    {
        return Point(value1.X * value2.X, value1.Y * value2.Y);
    }

    Point operator/(Point value1, Point value2)
    {
        return Point(value1.X / value2.X, value1.Y / value2.Y);
    }

    bool operator==(Point value1, Point value2)
    {
        return value1.Equals(value2);
    }

    bool operator!=(Point value1, Point value2)
    {
        return !value1.Equals(value2);
    }
}
