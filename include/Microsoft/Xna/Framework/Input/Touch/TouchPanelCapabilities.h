//
// Created by robertvokac on 5/24/25.
//

#ifndef TOUCHPANELCAPABILITIES_H
#define TOUCHPANELCAPABILITIES_H
#include "CNA/Prop.h"


namespace Microsoft::Xna::Framework::Input::Touch {
    struct TouchPanelCapabilities {
    private:
        bool isConnected = false;

    public:
        TouchPanelCapabilities();

    public:
        bool IsConnectedProperty() const;

    public:
        void IsConnectedProperty(bool v);;
    };
}


#endif //TOUCHPANELCAPABILITIES_H
