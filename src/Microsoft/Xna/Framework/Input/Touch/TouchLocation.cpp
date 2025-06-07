//
// Created by robertvokac on 5/25/25.
//

#include "Microsoft/Xna/Framework/Input/Touch/TouchLocation.h"

namespace Microsoft::Xna::Framework::Input::Touch {
    igetter(TouchLocationState, State, TouchLocation)
    igetter(Vector2, Position, TouchLocation)

    TouchLocation::TouchLocation(): State_(Invalid), Position_(0, 0) {
    }
}
