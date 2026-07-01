// SPDX-License-Identifier: MS-PL
//
// Created by robertvokac on 6/1/25.
//

#include "Microsoft/Devices/Sensors/SensorFailedException.hpp"

namespace Microsoft::Devices::Sensors
{
    SensorFailedException::SensorFailedException() : Exception("Sensor failed.")
    {
    }

    SensorFailedException::SensorFailedException(const char* str) : System::Exception(str)
    {
    }
}
