//
// Created by robertvokac on 5/24/25.
//

#include "Microsoft/Xna/Framework/Input/Touch/TouchPanelCapabilities.h"

namespace Microsoft::Xna::Framework::Input::Touch {
    bool TouchPanelCapabilities::IsConnectedProperty() const { return isConnected; }
    void TouchPanelCapabilities::IsConnectedProperty(const bool v) { isConnected = v; }

    TouchPanelCapabilities::TouchPanelCapabilities() {
    }
}
