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
        DEF_PROP(bool, IsConnected, getter1, setter0, member1, static0, constret1, ref1, constmet1)
};
}


#endif //TOUCHPANELCAPABILITIES_H
