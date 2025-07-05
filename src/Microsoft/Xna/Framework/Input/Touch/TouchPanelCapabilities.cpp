//
// Created by robertvokac on 5/24/25.
//

#include "Microsoft/Xna/Framework/Input/Touch/TouchPanelCapabilities.h"

namespace Microsoft::Xna::Framework::Input::Touch {

    IMPL_PROP(bool, IsConnected, getter1, setter0, member0, static0, constret1, ref1, constmet1, TouchPanelCapabilities, nothing)

    TouchPanelCapabilities::TouchPanelCapabilities(): IsConnected_(false) {
    }
}
