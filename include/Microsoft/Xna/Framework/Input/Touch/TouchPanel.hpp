//
// Created by robertvokac on 5/24/25.
//
#pragma once
#include "TouchCollection.hpp"
#include "TouchPanelCapabilities.hpp"

namespace Microsoft::Xna::Framework::Input::Touch {
    class TouchPanel {
    public:
        TouchPanel();

        static TouchPanelCapabilities GetCapabilities();

        static TouchCollection GetState() {
            return TouchCollection();
            /*todo TouchPanel.PrimaryWindow.TouchPanelState.GetState()*/
        };
    };
}

