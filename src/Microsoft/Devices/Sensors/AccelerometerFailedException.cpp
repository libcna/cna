// SPDX-License-Identifier: MS-PL
//
// Created by robertvokac on 6/1/25.
//

#include "Microsoft/Devices/Sensors/AccelerometerFailedException.hpp"

namespace Microsoft::Devices::Sensors
{
    AccelerometerFailedException::AccelerometerFailedException() : SensorFailedException("Accelerometer failed.")
    {
    }

    AccelerometerFailedException::AccelerometerFailedException(const char* str) : SensorFailedException(str)
    {
    }
}
