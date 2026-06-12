// SPDX-License-Identifier: MS-PL

#pragma once

#include <string>

#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework
{
    using SharpRuntime::intcs;

    /// Describes a point in two-dimensional integer space.
    struct Point
    {
        /// Point with X = 0 and Y = 0.
        static const Point Zero;

        /// X coordinate of this point.
        intcs X;

        /// Y coordinate of this point.
        intcs Y;

        /// Creates a point at 0, 0.
        Point();

        /// Creates a point from the specified X and Y coordinates.
        Point(intcs x, intcs y);

        /// Gets the zero point.
        [[nodiscard]] static Point getZeroProperty();

        /// Returns true when both coordinates match another point.
        [[nodiscard]] bool Equals(const Point& other) const;

        /// Returns a hash code for this point.
        [[nodiscard]] intcs GetHashCode() const;

        /// Returns a string in the form {X:... Y:...}.
        [[nodiscard]] std::string ToString() const;

        /// Adds two points component-wise.
        friend Point operator+(Point value1, Point value2);
        /// Subtracts one point from another component-wise.
        friend Point operator-(Point value1, Point value2);
        /// Multiplies the components of two points together.
        friend Point operator*(Point value1, Point value2);
        /// Divides the components of one point by the components of another.
        friend Point operator/(Point value1, Point value2);
        /// Returns true when both points have equal coordinates.
        friend bool operator==(Point value1, Point value2);
        /// Returns true when the points differ in any coordinate.
        friend bool operator!=(Point value1, Point value2);

    private:
        [[nodiscard]] std::string getDebugDisplayStringProperty() const;
    };
}
