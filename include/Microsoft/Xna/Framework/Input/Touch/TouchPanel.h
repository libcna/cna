//
// Created by robertvokac on 5/24/25.
//

#ifndef TOUCHPANEL_H
#define TOUCHPANEL_H
#include "TouchPanelCapabilities.h"


namespace Microsoft::Xna::Framework::Input::Touch {
    class TouchPanel {
    public:
        TouchPanel();

        static TouchPanelCapabilities GetCapabilities();
    };
}



#endif //TOUCHPANEL_H
