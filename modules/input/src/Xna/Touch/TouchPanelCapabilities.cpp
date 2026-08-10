// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Input/Touch/TouchPanelCapabilities.hpp"

namespace Microsoft::Xna::Framework::Input::Touch
{
    TouchPanelCapabilities::TouchPanelCapabilities()
        : isConnected_(false), maximumTouchCount_(0)
    {
    }

    TouchPanelCapabilities::TouchPanelCapabilities(bool isConnected, int maximumTouchCount)
        : isConnected_(isConnected), maximumTouchCount_(maximumTouchCount)
    {
    }

    bool TouchPanelCapabilities::getIsConnectedProperty()      const { return isConnected_; }
    int  TouchPanelCapabilities::getMaximumTouchCountProperty() const { return maximumTouchCount_; }
}
