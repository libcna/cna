//
// Created by robertvokac on 5/28/25.
//

#include "Microsoft/Xna/Framework/Input/GamePadButtons.h"

namespace Microsoft::Xna::Framework::Input {

    IMPL_PROP(ButtonState, Back, getter1, setter0, member0, static0, constret1, ref1, constmet1, GamePadButtons)

    GamePadButtons::GamePadButtons(): Back_(Released) {
    }
}
