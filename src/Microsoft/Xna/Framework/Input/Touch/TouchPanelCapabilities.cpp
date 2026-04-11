#include "Microsoft/Xna/Framework/Input/Touch/TouchPanelCapabilities.hpp"

namespace Microsoft::Xna::Framework::Input::Touch {

    IMPL_PROP(bool, IsConnected, getter1, setter0, member0, static0, constret1, ref1, constmet1, TouchPanelCapabilities, nothing)

    TouchPanelCapabilities::TouchPanelCapabilities()
        : IsConnected_(false)
    {
    }

    TouchPanelCapabilities::TouchPanelCapabilities(bool isConnected)
        : IsConnected_(isConnected)
    {
    }
}