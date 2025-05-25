//
// Created by robertvokac on 5/24/25.
//

#ifndef TOUCHPANEL_H
#define TOUCHPANEL_H
#include "TouchCollection.h"
#include "TouchPanelCapabilities.h"
#include "NeoSdk/ReadonlyProperty.h"


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


#endif //TOUCHPANEL_H
