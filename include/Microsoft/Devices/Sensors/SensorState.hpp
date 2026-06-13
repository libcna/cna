// SPDX-License-Identifier: MS-PL
//
// Created by robertvokac on 6/7/25.
//

#pragma once

namespace Microsoft::Devices::Sensors
{
    /**
     * @brief Specifies the current state of a sensor.
     *
     * This is a C++ counterpart of the .NET
     * Microsoft.Devices.Sensors.SensorState enumeration.
     *
     * @note Status: Partial.
     */
    enum class SensorState
    {
        /** @brief The sensor is not supported on this device. */
        NotSupported,
        /** @brief The sensor is ready and providing data. */
        Ready,
        /** @brief The sensor is currently initializing. */
        Initializing,
        /** @brief The sensor has no data available. */
        NoData,
        /** @brief The sensor cannot be accessed due to missing permissions. */
        NoPermissions,
        /** @brief The sensor is disabled. */
        Disabled,
    };
} // namespace Microsoft::Devices::Sensors
