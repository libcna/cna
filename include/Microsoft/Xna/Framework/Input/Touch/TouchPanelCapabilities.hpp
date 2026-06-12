// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Input::Touch
{
    /// Describes the capabilities of the touch panel device.
    struct TouchPanelCapabilities
    {
        [[nodiscard]] bool getIsConnectedProperty() const;
        [[nodiscard]] int getMaximumTouchCountProperty() const;

        /// Constructs disconnected capabilities with zero touch count.
        TouchPanelCapabilities();

        /// Constructs with explicit connected state and maximum touch count.
        TouchPanelCapabilities(bool isConnected, int maximumTouchCount);

    private:
        bool isConnected_;
        int maximumTouchCount_;
    };
}
