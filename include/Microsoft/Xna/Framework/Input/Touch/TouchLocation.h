//
// Created by robertvokac on 5/25/25.
//

#ifndef TOUCHLOCATION_H
#define TOUCHLOCATION_H
#include "TouchLocationState.h"
#include "NeoSdk/Property.h"
#include "Microsoft/Xna/Framework/Vector2.h"

namespace Microsoft::Xna::Framework::Input::Touch {

struct TouchLocation {
public:
    DEF_PROP_AUTO(TouchLocationState, State, TouchLocationState::Invalid),
    DEF_PROP_AUTO(Vector2, Position, Vector2())

    TouchLocation():
    IMPL_PROP_AUTO(TouchLocationState, State),
    IMPL_PROP_AUTO(Vector2, Position)
    {
    }

};

}

#endif //TOUCHLOCATION_H
