//
// Created by robertvokac on 5/28/25.
//

#include "Microsoft/Xna/Framework/Input/GamePadButtons.h"

namespace Microsoft::Xna::Framework::Input {
    ButtonState GamePadButtons::BackProperty() { return BackProperty_; }
    void GamePadButtons::BackProperty(const ButtonState v) { BackProperty_ = v; }
}
