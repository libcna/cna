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
        /// The sensor is not supported on this device.
        NotSupported,
        /// The sensor is ready and providing data.
        Ready,
        /// The sensor is initializing.
        Initializing,
        /// The sensor has no data available.
        NoData,
        /// The sensor cannot be accessed due to missing permissions.
        NoPermissions,
        /// The sensor is disabled.
        Disabled,
    };
} // namespace Microsoft::Devices::Sensors
