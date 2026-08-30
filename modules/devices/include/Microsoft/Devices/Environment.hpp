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
         * @return Device for CNA's supported physical host platforms.
         */
        [[nodiscard]] static DeviceType getDeviceTypeProperty();
    };
}
