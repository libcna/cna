// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Input::Touch
{
    /**
     * @brief Describes the capabilities of the touch panel device.
     */
    struct TouchPanelCapabilities
    {
        /**
         * @brief Gets whether a touch device is connected.
         * @return True if connected; false otherwise.
         */
        [[nodiscard]] bool getIsConnectedProperty() const;

        /**
         * @brief Gets the maximum number of simultaneous touch points supported.
         * @return The maximum touch count.
         */
        [[nodiscard]] int getMaximumTouchCountProperty() const;

        /** @brief Constructs disconnected capabilities with zero touch count. */
        TouchPanelCapabilities();

        /**
         * @brief Constructs with explicit connected state and maximum touch count.
         * @param isConnected Whether a touch device is connected.
         * @param maximumTouchCount The maximum number of simultaneous touch points.
         */
        TouchPanelCapabilities(bool isConnected, int maximumTouchCount);

    private:
        bool isConnected_;
        int maximumTouchCount_;
    };
}
