#include "Microsoft/Xna/Framework/Input/Touch/TouchLocation.hpp"

namespace Microsoft::Xna::Framework::Input::Touch {

    IMPL_PROP(TouchLocationState, State, getter1, setter0, member0, static0, constret1, ref1, constmet1, TouchLocation, nothing)
    IMPL_PROP(Vector2, Position, getter1, setter0, member0, static0, constret1, ref1, constmet1, TouchLocation, nothing)

    TouchLocation::TouchLocation()
        : State_(TouchLocationState::Invalid), Position_(0, 0)
    {
    }

    TouchLocation::TouchLocation(TouchLocationState state, const Vector2& position)
        : State_(state), Position_(position)
    {
    }
}