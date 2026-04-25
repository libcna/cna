//
// Created by robertvokac on 5/24/25.
//

#include "Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp"

#include "CNA/Internal/Input/InputManager.hpp"

namespace Microsoft::Xna::Framework::Input::Touch {
    TouchPanel::TouchPanel() {}

    TouchPanelCapabilities TouchPanel::GetCapabilities() {
        return TouchPanelCapabilities();
    }

    TouchCollection TouchPanel::GetState() {
        return CNA::Internal::Input::InputManager::GetTouchState();
    }
}
