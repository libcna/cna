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
         * @return Emulator for CNA's browser target, which emulates the Windows Phone input
         * environment without physical sensor access; Device for supported physical host targets.
         */
        [[nodiscard]] static DeviceType getDeviceTypeProperty();
    };
}
