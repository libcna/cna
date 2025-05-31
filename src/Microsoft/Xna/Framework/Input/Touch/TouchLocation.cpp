//
// Created by robertvokac on 5/25/25.
//

#include "Microsoft/Xna/Framework/Input/Touch/TouchLocation.h"

namespace Microsoft::Xna::Framework::Input::Touch {
    igetter(TouchLocationState, State, TouchLocation)

    Vector2 TouchLocation::PositionProperty() const { return PositionProperty_; }
    void TouchLocation::PositionProperty(const Vector2 v) { PositionProperty_ = v; }

    TouchLocation::TouchLocation() {
    }
}
