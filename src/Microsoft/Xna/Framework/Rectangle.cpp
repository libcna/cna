//
// Created by robertvokac on 5/24/25.
//

#include "Microsoft/Xna/Framework/Rectangle.hpp"

namespace Microsoft::Xna::Framework {
    Rectangle Rectangle::emptyRectangle = Rectangle(0, 0, 0, 0);

    Rectangle::Rectangle(): Rectangle(0, 0, 0, 0)
    {
    }

    Rectangle::Rectangle(intcs x, intcs y, intcs width, intcs height): X(x),
                                                                       Y(y),
                                                                       Width(width),
                                                                       Height(height)
    {
    }

    intcs Rectangle::getLeftProperty() const { return this->X; }
    intcs Rectangle::getRightProperty() const { return this->X + this->Width; }
    intcs Rectangle::getTopProperty() const { return this->Y; }
    intcs Rectangle::getBottomProperty() const { return this->Y + this->Height; }

    Rectangle Rectangle::getEmptyProperty()
    {
        return emptyRectangle;
    }
}
