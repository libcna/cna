//
// Created by robertvokac on 5/24/25.
//

#pragma once
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework {
    using SharpRuntime::intcs;
    struct Rectangle {
        Rectangle(); // Default constructor

        Rectangle(
            intcs x,
            intcs y,
            intcs width,
            intcs height);

        /**
         * Represents the x-coordinate of the top-left corner of a rectangle.
         */
        intcs X;
        /**
         * Represents the Y-coordinate of the rectangle's top-left corner.
         */
        intcs Y;
        /**
         * Represents the width of the rectangle.
         * Defines the horizontal size of the rectangle.
         */
        intcs Width;
        /**
         * Represents the height of the rectangle.
         * Determines the vertical size of the rectangle.
         */
        intcs Height;

    private:
        static Rectangle emptyRectangle;

    public:
        [[nodiscard]] intcs getLeftProperty() const;

        [[nodiscard]] intcs getRightProperty() const;

        [[nodiscard]] intcs getTopProperty() const;

        [[nodiscard]] intcs getBottomProperty() const;

        [[nodiscard]] static Rectangle getEmptyProperty();
    };
}

