//
// Created by robertvokac on 5/24/25.
//

#include "Microsoft/Xna/Framework/Input/Touch/TouchPanelCapabilities.h"
namespace Microsoft::Xna::Framework::Input::Touch {
TouchPanelCapabilities::TouchPanelCapabilities():
    IsConnected( [&]()   {return isConnected;}){}

}
