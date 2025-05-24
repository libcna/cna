//
// Created by robertvokac on 5/24/25.
//

#ifndef RECTANGLE_H
#define RECTANGLE_H
#include "NeoSdk/Property.h"


namespace Microsoft::Xna::Framework {
    struct Rectangle {
        Rectangle() : Rectangle(0,0,0,0) {} // Default constructor

        Rectangle(
            int x,
            int y,
            int width,
            int height) :
        Left( [*this]() { return this->X; }),
        Right( [*this]() { return this->X + this->Width; }),
        Top( [*this]() { return this->Y; }),
        Bottom( [*this]() { return this->Y + this->Height; }),
              X(x),
              Y(y),
              Width(width),
              Height(height) {
        }

    private: static Rectangle emptyRectangle;


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

        NeoSdk::Property<int> Left;
        NeoSdk::Property<int> Right;
        NeoSdk::Property<int> Top;
        NeoSdk::Property<int> Bottom;

        static NeoSdk::Property<Rectangle> Empty;


    };
}


#endif //RECTANGLE_H
