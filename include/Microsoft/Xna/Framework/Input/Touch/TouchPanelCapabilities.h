//
// Created by robertvokac on 5/24/25.
//

#ifndef TOUCHPANELCAPABILITIES_H
#define TOUCHPANELCAPABILITIES_H



namespace Microsoft::Xna::Framework::Input::Touch {
    struct TouchPanelCapabilities {
    private:
        bool isConnected = false;
    public:
        TouchPanelCapabilities();
        CNA::Property<bool> IsConnected;

    };
}


#endif //TOUCHPANELCAPABILITIES_H
