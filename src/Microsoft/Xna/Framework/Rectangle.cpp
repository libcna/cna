//
// Created by robertvokac on 5/24/25.
//

#include "Microsoft/Xna/Framework/Rectangle.h"

namespace Microsoft::Xna::Framework {
    CNA::Property<Rectangle> Rectangle::Empty{ []() { return Rectangle::emptyRectangle; }};

}
