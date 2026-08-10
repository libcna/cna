// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::GamerServices
{
    /**
     * @brief Specifies the screen position of gamer notification toasts.
     */
    enum class NotificationPosition
    {
        /** @brief Top-left corner. */
        TopLeft,
        /** @brief Top-center. */
        TopCenter,
        /** @brief Top-right corner. */
        TopRight,
        /** @brief Center-left. */
        CenterLeft,
        /** @brief Screen center. */
        Center,
        /** @brief Center-right. */
        CenterRight,
        /** @brief Bottom-left corner. */
        BottomLeft,
        /** @brief Bottom-center. */
        BottomCenter,
        /** @brief Bottom-right corner. */
        BottomRight
    };
}
