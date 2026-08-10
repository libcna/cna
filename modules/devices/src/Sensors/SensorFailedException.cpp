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

    SensorFailedException::SensorFailedException(const char* str, SharpRuntime::intcs errorId)
        : System::Exception(str), errorId_(errorId)
    {
    }

    SharpRuntime::intcs SensorFailedException::getErrorIdProperty() const noexcept
    {
        return errorId_;
    }
}
