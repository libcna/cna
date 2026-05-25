#pragma once

#include <string>

#include "Microsoft/Xna/Framework/Point.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework
{
    using SharpRuntime::intcs;

    /// Describes a rectangle in two-dimensional integer space.
    struct Rectangle
    {
        /// Rectangle with X = 0, Y = 0, Width = 0 and Height = 0.
        static const Rectangle Empty;

        /// X coordinate of the top-left corner.
        intcs X;

        /// Y coordinate of the top-left corner.
        intcs Y;

        /// Width of the rectangle.
        intcs Width;

        /// Height of the rectangle.
        intcs Height;

        /// Creates an empty rectangle at 0, 0.
        Rectangle();

        /// Creates a rectangle from position and size.
        Rectangle(intcs x, intcs y, intcs width, intcs height);

        /// Gets the x coordinate of the left edge.
        [[nodiscard]] intcs getLeftProperty() const;

        /// Gets the x coordinate of the right edge.
        [[nodiscard]] intcs getRightProperty() const;

        /// Gets the y coordinate of the top edge.
        [[nodiscard]] intcs getTopProperty() const;

        /// Gets the y coordinate of the bottom edge.
        [[nodiscard]] intcs getBottomProperty() const;

        /// Gets the top-left location.
        [[nodiscard]] Point getLocationProperty() const;

        /// Sets the top-left location.
        void setLocationProperty(Point value);

        /// Gets the center point of the rectangle, rounded down for odd sizes.
        [[nodiscard]] Point getCenterProperty() const;

        /// Gets whether this rectangle is exactly 0, 0, 0, 0.
        [[nodiscard]] bool getIsEmptyProperty() const;

        /// Gets the empty rectangle.
        [[nodiscard]] static Rectangle getEmptyProperty();

        /// Returns true when the specified coordinates are inside this rectangle.
        [[nodiscard]] bool Contains(intcs x, intcs y) const;

        /// Returns true when the specified point is inside this rectangle.
        [[nodiscard]] bool Contains(Point value) const;

        /// Returns true when the specified rectangle is fully inside this rectangle.
        [[nodiscard]] bool Contains(Rectangle value) const;

        /// Tests whether a point is inside this rectangle and stores the result.
        void Contains(const Point& value, bool& result) const;

        /// Tests whether a rectangle is fully inside this rectangle and stores the result.
        void Contains(const Rectangle& value, bool& result) const;

        /// Offsets this rectangle by a point.
        void Offset(Point offset);

        /// Offsets this rectangle by separate X and Y amounts.
        void Offset(intcs offsetX, intcs offsetY);

        /// Expands this rectangle by the specified horizontal and vertical amounts.
        void Inflate(intcs horizontalValue, intcs verticalValue);

        /// Returns true when all fields match another rectangle.
        [[nodiscard]] bool Equals(const Rectangle& other) const;

        /// Returns a string in the form {X:... Y:... Width:... Height:...}.
        [[nodiscard]] std::string ToString() const;

        /// Returns a hash code for this rectangle.
        [[nodiscard]] intcs GetHashCode() const;

        /// Returns true when the specified rectangle intersects this rectangle.
        [[nodiscard]] bool Intersects(Rectangle value) const;

        /// Tests whether the specified rectangle intersects this rectangle and stores the result.
        void Intersects(const Rectangle& value, bool& result) const;

        /// Returns the intersection of two rectangles, or Empty when they do not overlap.
        [[nodiscard]] static Rectangle Intersect(Rectangle value1, Rectangle value2);

        /// Computes the intersection of two rectangles into an output parameter.
        static void Intersect(const Rectangle& value1, const Rectangle& value2, Rectangle& result);

        /// Returns the smallest rectangle that contains both input rectangles.
        [[nodiscard]] static Rectangle Union(Rectangle value1, Rectangle value2);

        /// Computes the union of two rectangles into an output parameter.
        static void Union(const Rectangle& value1, const Rectangle& value2, Rectangle& result);

        friend bool operator==(Rectangle value1, Rectangle value2);
        friend bool operator!=(Rectangle value1, Rectangle value2);

    private:
        [[nodiscard]] std::string getDebugDisplayStringProperty() const;
    };
}
