// SPDX-License-Identifier: MS-PL

#pragma once

#include <string>

#include "Microsoft/Xna/Framework/Point.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework
{
    using SharpRuntime::intcs;

    /** @brief Describes a rectangle in two-dimensional integer space. */
    struct Rectangle
    {
        /** @brief Rectangle with X = 0, Y = 0, Width = 0 and Height = 0. */
        static const Rectangle Empty;

        /** @brief X coordinate of the top-left corner. */
        intcs X;

        /** @brief Y coordinate of the top-left corner. */
        intcs Y;

        /** @brief Width of the rectangle. */
        intcs Width;

        /** @brief Height of the rectangle. */
        intcs Height;

        /** @brief Creates an empty rectangle at 0, 0. */
        Rectangle();

        /**
         * @brief Creates a rectangle from position and size.
         *
         * @param x The X coordinate of the top-left corner.
         * @param y The Y coordinate of the top-left corner.
         * @param width The width of the rectangle.
         * @param height The height of the rectangle.
         */
        Rectangle(intcs x, intcs y, intcs width, intcs height);

        /**
         * @brief Gets the x coordinate of the left edge.
         *
         * @return The x coordinate of the left edge.
         */
        [[nodiscard]] intcs getLeftProperty() const;

        /**
         * @brief Gets the x coordinate of the right edge.
         *
         * @return The x coordinate of the right edge.
         */
        [[nodiscard]] intcs getRightProperty() const;

        /**
         * @brief Gets the y coordinate of the top edge.
         *
         * @return The y coordinate of the top edge.
         */
        [[nodiscard]] intcs getTopProperty() const;

        /**
         * @brief Gets the y coordinate of the bottom edge.
         *
         * @return The y coordinate of the bottom edge.
         */
        [[nodiscard]] intcs getBottomProperty() const;

        /**
         * @brief Gets the top-left location.
         *
         * @return The top-left location as a Point.
         */
        [[nodiscard]] Point getLocationProperty() const;

        /**
         * @brief Sets the top-left location.
         *
         * @param value The new top-left location.
         */
        void setLocationProperty(Point value);

        /**
         * @brief Gets the center point of the rectangle, rounded down for odd sizes.
         *
         * @return The center point of the rectangle.
         */
        [[nodiscard]] Point getCenterProperty() const;

        /**
         * @brief Gets whether this rectangle is exactly 0, 0, 0, 0.
         *
         * @return @c true if the rectangle is empty; @c false otherwise.
         */
        [[nodiscard]] bool getIsEmptyProperty() const;

        /**
         * @brief Gets the empty rectangle.
         *
         * @return A rectangle with all fields set to zero.
         */
        [[nodiscard]] static Rectangle getEmptyProperty();

        /**
         * @brief Returns true when the specified coordinates are inside this rectangle.
         *
         * @param x The X coordinate to test.
         * @param y The Y coordinate to test.
         * @return @c true if the point is inside this rectangle; @c false otherwise.
         */
        [[nodiscard]] bool Contains(intcs x, intcs y) const;

        /**
         * @brief Returns true when the specified point is inside this rectangle.
         *
         * @param value The point to test.
         * @return @c true if the point is inside this rectangle; @c false otherwise.
         */
        [[nodiscard]] bool Contains(Point value) const;

        /**
         * @brief Returns true when the specified rectangle is fully inside this rectangle.
         *
         * @param value The rectangle to test.
         * @return @c true if the rectangle is fully contained; @c false otherwise.
         */
        [[nodiscard]] bool Contains(Rectangle value) const;

        /**
         * @brief Tests whether a point is inside this rectangle and stores the result.
         *
         * @param value The point to test.
         * @param result Output that receives the containment result.
         */
        void Contains(const Point& value, bool& result) const;

        /**
         * @brief Tests whether a rectangle is fully inside this rectangle and stores the result.
         *
         * @param value The rectangle to test.
         * @param result Output that receives the containment result.
         */
        void Contains(const Rectangle& value, bool& result) const;

        /**
         * @brief Offsets this rectangle by a point.
         *
         * @param offset The offset to apply.
         */
        void Offset(Point offset);

        /**
         * @brief Offsets this rectangle by separate X and Y amounts.
         *
         * @param offsetX The horizontal offset.
         * @param offsetY The vertical offset.
         */
        void Offset(intcs offsetX, intcs offsetY);

        /**
         * @brief Expands this rectangle by the specified horizontal and vertical amounts.
         *
         * @param horizontalValue Amount to expand horizontally on each side.
         * @param verticalValue Amount to expand vertically on each side.
         */
        void Inflate(intcs horizontalValue, intcs verticalValue);

        /**
         * @brief Returns true when all fields match another rectangle.
         *
         * @param other The rectangle to compare against.
         * @return @c true if the rectangles are equal; @c false otherwise.
         */
        [[nodiscard]] bool Equals(const Rectangle& other) const;

        /**
         * @brief Returns a string in the form {X:... Y:... Width:... Height:...}.
         *
         * @return String representation of this rectangle.
         */
        [[nodiscard]] std::string ToString() const;

        /**
         * @brief Returns a hash code for this rectangle.
         *
         * @return Hash code of this rectangle.
         */
        [[nodiscard]] intcs GetHashCode() const;

        /**
         * @brief Returns true when the specified rectangle intersects this rectangle.
         *
         * @param value The rectangle to test.
         * @return @c true if the rectangles intersect; @c false otherwise.
         */
        [[nodiscard]] bool Intersects(Rectangle value) const;

        /**
         * @brief Tests whether the specified rectangle intersects this rectangle and stores the result.
         *
         * @param value The rectangle to test.
         * @param result Output that receives the intersection result.
         */
        void Intersects(const Rectangle& value, bool& result) const;

        /**
         * @brief Returns the intersection of two rectangles, or Empty when they do not overlap.
         *
         * @param value1 The first rectangle.
         * @param value2 The second rectangle.
         * @return The intersection rectangle, or Empty if there is no overlap.
         */
        [[nodiscard]] static Rectangle Intersect(Rectangle value1, Rectangle value2);

        /**
         * @brief Computes the intersection of two rectangles into an output parameter.
         *
         * @param value1 The first rectangle.
         * @param value2 The second rectangle.
         * @param result Output rectangle that receives the intersection.
         */
        static void Intersect(const Rectangle& value1, const Rectangle& value2, Rectangle& result);

        /**
         * @brief Returns the smallest rectangle that contains both input rectangles.
         *
         * @param value1 The first rectangle.
         * @param value2 The second rectangle.
         * @return The union rectangle.
         */
        [[nodiscard]] static Rectangle Union(Rectangle value1, Rectangle value2);

        /**
         * @brief Computes the union of two rectangles into an output parameter.
         *
         * @param value1 The first rectangle.
         * @param value2 The second rectangle.
         * @param result Output rectangle that receives the union.
         */
        static void Union(const Rectangle& value1, const Rectangle& value2, Rectangle& result);

        /**
         * @brief Returns true when all four fields are equal.
         *
         * @param value1 Left-hand rectangle.
         * @param value2 Right-hand rectangle.
         * @return @c true if the rectangles are equal; @c false otherwise.
         */
        friend bool operator==(Rectangle value1, Rectangle value2);

        /**
         * @brief Returns true when any field differs.
         *
         * @param value1 Left-hand rectangle.
         * @param value2 Right-hand rectangle.
         * @return @c true if the rectangles are not equal; @c false otherwise.
         */
        friend bool operator!=(Rectangle value1, Rectangle value2);

    private:
        [[nodiscard]] std::string getDebugDisplayStringProperty() const;
    };
}
