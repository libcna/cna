//
// Created by robertvokac on 5/24/25.
//

#ifndef TOUCHPANELCAPABILITIES_H
#define TOUCHPANELCAPABILITIES_H
#include "NeoSdk/Property.h"


namespace Microsoft::Xna::Framework::Input::Touch {
    struct TouchPanelCapabilities {
    private:
        bool isConnected = false;
    public:
        TouchPanelCapabilities();
        NeoSdk::Property<bool> IsConnected;

    };
}


#endif //TOUCHPANELCAPABILITIES_H
