//
// Created by robertvokac on 5/28/25.
//

#include "Microsoft/Xna/Framework/Input/GamePadButtons.h"

namespace Microsoft::Xna::Framework::Input {
    igetter(ButtonState, Back, GamePadButtons)
    GamePadButtons::GamePadButtons(): Back_(Released) {
    }
}
