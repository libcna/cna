//
// Created by robertvokac on 5/25/25.
//

#include "Microsoft/Xna/Framework/Input/Touch/TouchLocation.h"

namespace Microsoft::Xna::Framework::Input::Touch {

    IMPL_PROP(TouchLocationState, State, getter1, setter0, member0, static0, constret1, ref1, constmet1, TouchLocation, nothing)
    IMPL_PROP(Vector2, Position, getter1, setter0, member0, static0, constret1, ref1, constmet1, TouchLocation, nothing)

    TouchLocation::TouchLocation(): State_(Invalid), Position_(0, 0) {
    }
}
