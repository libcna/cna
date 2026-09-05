// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Devices/DeviceType.hpp"

namespace Microsoft::Devices
{
    /** @brief Provides information about the environment in which an application is running. */
    class Environment final
    {
    public:
        /** @brief Environment is a static class and cannot be instantiated. */
        Environment() = delete;

        /**
         * @brief Gets the kind of device on which the application is running.
         * @return Device for Android and iOS targets with physical sensors; Emulator for desktop
         * and browser targets, which emulate the Windows Phone input environment.
         */
        [[nodiscard]] static DeviceType getDeviceTypeProperty();
    };
}
