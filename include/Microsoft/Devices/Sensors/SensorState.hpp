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
        NotSupported,
        Ready,
        Initializing,
        NoData,
        NoPermissions,
        Disabled,
    };
} // namespace Microsoft::Devices::Sensors
