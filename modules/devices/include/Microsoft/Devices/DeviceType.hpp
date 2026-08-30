// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Devices
{
    /** @brief Identifies whether a Windows Phone application is running on a device or emulator. */
    enum class DeviceType
    {
        /** @brief The application is running on a physical device. */
        Device,

        /** @brief The application is running in the Windows Phone emulator. */
        Emulator,
    };
}
