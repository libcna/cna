// SPDX-License-Identifier: MS-PL
//
// Created by robertvokac on 5/26/25.
//

#pragma once
#include "SensorFailedException.hpp"

namespace Microsoft::Devices::Sensors
{
    /// Exception thrown when an accelerometer operation fails.
    class AccelerometerFailedException : public SensorFailedException
    {
    public:
        /// Constructs an AccelerometerFailedException with a default message.
        AccelerometerFailedException();

        /// Constructs an AccelerometerFailedException with the given message string.
        explicit AccelerometerFailedException(const char* str);
    };
};
