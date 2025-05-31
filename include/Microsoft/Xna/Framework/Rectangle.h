//
// Created by robertvokac on 5/24/25.
//

#ifndef RECTANGLE_H
#define RECTANGLE_H
#include "CNA/Prop.h"


namespace Microsoft::Xna::Framework {
    struct Rectangle {
        Rectangle() : Rectangle(0, 0, 0, 0) {
        } // Default constructor

        Rectangle(
            int x,
            int y,
            int width,
            int height) : X(x),
                          Y(y),
                          Width(width),
                          Height(height) {
        }

        /**
         * Represents the x-coordinate of the top-left corner of a rectangle.
         */
    public:
        int X;
        /**
         * Represents the Y-coordinate of the rectangle's top-left corner.
         */
        int Y;
        /**
         * Represents the width of the rectangle.
         * Defines the horizontal size of the rectangle.
         */
        int Width;
        /**
         * Represents the height of the rectangle.
         * Determines the vertical size of the rectangle.
         */
        int Height;

    public:
        [[nodiscard]] int getLeft() const;

    public:
        [[nodiscard]] int getRight() const;

    public:
        [[nodiscard]] int getTop() const;

    public:
        [[nodiscard]] int getBottom() const;


    public: static [[nodiscard]] Rectangle getEmpty();
    };
}


#endif //RECTANGLE_H
