//
// Created by robertvokac on 5/25/25.
//

#include "Microsoft/Xna/Framework/Input/Mouse.hpp"
#include "CNA/Internal/Input/InputManager.hpp"

namespace Microsoft::Xna::Framework::Input {
    MouseState Mouse::GetState() {
        return CNA::Internal::Input::InputManager::GetMouseState();

    }

    void Mouse::SetCursor(MouseCursor arrow) {

    }
}
