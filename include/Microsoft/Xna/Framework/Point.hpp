// SPDX-License-Identifier: MS-PL

#pragma once

#include <string>

#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework
{
    using SharpRuntime::intcs;

    /** @brief Describes a point in two-dimensional integer space. */
    struct Point
    {
        /** @brief Point with X = 0 and Y = 0. */
        static const Point Zero;

        /** @brief X coordinate of this point. */
        intcs X;

        /** @brief Y coordinate of this point. */
        intcs Y;

        /** @brief Creates a point at 0, 0. */
        Point();

        /**
         * @brief Creates a point from the specified X and Y coordinates.
         *
         * @param x The X coordinate.
         * @param y The Y coordinate.
         */
        Point(intcs x, intcs y);

        /**
         * @brief Gets the zero point.
         *
         * @return A point with X = 0 and Y = 0.
         */
        [[nodiscard]] static Point getZeroProperty();

        /**
         * @brief Returns true when both coordinates match another point.
         *
         * @param other The point to compare against.
         * @return @c true if the points are equal; @c false otherwise.
         */
        [[nodiscard]] bool Equals(const Point& other) const;

        /**
         * @brief Returns a hash code for this point.
         *
         * @return Hash code of this point.
         */
        [[nodiscard]] intcs GetHashCode() const;

        /**
         * @brief Returns a string in the form {X:... Y:...}.
         *
         * @return String representation of this point.
         */
        [[nodiscard]] std::string ToString() const;

        /**
         * @brief Adds two points component-wise.
         *
         * @param value1 Left-hand point.
         * @param value2 Right-hand point.
         * @return The component-wise sum.
         */
        friend Point operator+(Point value1, Point value2);

        /**
         * @brief Subtracts one point from another component-wise.
         *
         * @param value1 Left-hand point.
         * @param value2 Right-hand point.
         * @return The component-wise difference.
         */
        friend Point operator-(Point value1, Point value2);

        /**
         * @brief Multiplies the components of two points together.
         *
         * @param value1 Left-hand point.
         * @param value2 Right-hand point.
         * @return The component-wise product.
         */
        friend Point operator*(Point value1, Point value2);

        /**
         * @brief Divides the components of one point by the components of another.
         *
         * @param value1 Left-hand point.
         * @param value2 Right-hand point.
         * @return The component-wise quotient.
         */
        friend Point operator/(Point value1, Point value2);

        /**
         * @brief Returns true when both points have equal coordinates.
         *
         * @param value1 Left-hand point.
         * @param value2 Right-hand point.
         * @return @c true if the points are equal; @c false otherwise.
         */
        friend bool operator==(Point value1, Point value2);

        /**
         * @brief Returns true when the points differ in any coordinate.
         *
         * @param value1 Left-hand point.
         * @param value2 Right-hand point.
         * @return @c true if the points are not equal; @c false otherwise.
         */
        friend bool operator!=(Point value1, Point value2);

    private:
        [[nodiscard]] std::string getDebugDisplayStringProperty() const;
    };
}
