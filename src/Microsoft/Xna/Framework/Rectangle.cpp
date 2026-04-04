//
// Created by robertvokac on 5/24/25.
//

#include "Microsoft/Xna/Framework/Rectangle.hpp"

namespace Microsoft::Xna::Framework {
    static Rectangle emptyRectangle = {0,0,0,0};
    int Rectangle::getLeft() const { return this->X; }
    int Rectangle::getRight() const { return this->X + this->Width; }
    int Rectangle::getTop() const { return this->Y; }
    int Rectangle::getBottom() const { return this->Y + this->Height; }

    Rectangle Rectangle::getEmpty() { return emptyRectangle; }


}
