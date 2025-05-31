//
// Created by robertvokac on 5/24/25.
//

#ifndef TOUCHPANELCAPABILITIES_H
#define TOUCHPANELCAPABILITIES_H
#include "CNA/Prop.h"


namespace Microsoft::Xna::Framework::Input::Touch {
    struct TouchPanelCapabilities {

    public:
        TouchPanelCapabilities();

        dgetter(bool, IsConnected)
};
}


#endif //TOUCHPANELCAPABILITIES_H
