// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Describes the status of the GraphicsDevice. */
    enum class GraphicsDeviceStatus
    {
        /** @brief The device is operating normally. */
        Normal,
        /** @brief The device has been lost and cannot process commands. */
        Lost,
        /** @brief The device has not been reset after being lost. */
        NotReset,
    };
}
