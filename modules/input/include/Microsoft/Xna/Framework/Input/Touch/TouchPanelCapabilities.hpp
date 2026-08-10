// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"

namespace Microsoft::Xna::Framework::Input::Touch
{
    /**
     * @brief Describes the capabilities of the touch panel device.
     *
     * FNA declares only one constructor for this type, and it is `internal`
     * (`TouchPanelCapabilities(bool, int)`); there is no public XNA constructor at all
     * (instances are normally obtained only via `TouchPanel::GetCapabilities()`).
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

        /**
         * @brief Constructs disconnected capabilities with zero touch count.
         * @note NOXNA — FNA has no explicit parameterless constructor for this type.
         */
        NOXNA TouchPanelCapabilities();

        /**
         * @brief Constructs with explicit connected state and maximum touch count.
         * @note NOXNA — mirrors FNA's sole constructor, which is `internal`
         *       (see the class-level note); exposed publicly here since C++ has no
         *       assembly-internal visibility, but not part of the public XNA API.
         * @param isConnected Whether a touch device is connected.
         * @param maximumTouchCount The maximum number of simultaneous touch points.
         */
        NOXNA TouchPanelCapabilities(bool isConnected, int maximumTouchCount);

    private:
        bool isConnected_;
        int maximumTouchCount_;
    };
}
