#pragma once

#include "SharpRuntime/Prop.hpp"

namespace Microsoft::Xna::Framework::Input::Touch
{
    struct TouchPanelCapabilities
    {
    public:
        TouchPanelCapabilities();
        explicit TouchPanelCapabilities(bool isConnected);

        DEF_PROP(bool, IsConnected, getter1, setter0, member1, static0, constret1, ref1, constmet1)
    };
}
