//
// Created by robertvokac on 5/24/25.
//

#include "Microsoft/Xna/Framework/Input/Touch/TouchPanelCapabilities.h"

namespace Microsoft::Xna::Framework::Input::Touch {
    igetter(bool, IsConnected, TouchPanelCapabilities)

    TouchPanelCapabilities::TouchPanelCapabilities(): IsConnected_(false) {
    }
}
