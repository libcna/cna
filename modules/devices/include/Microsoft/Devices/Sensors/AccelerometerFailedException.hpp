// SPDX-License-Identifier: MS-PL
//
// Created by robertvokac on 5/26/25.
//

#pragma once
#include "Microsoft/Devices/Sensors/SensorFailedException.hpp"

namespace Microsoft::Devices::Sensors
{
    /** @brief Exception thrown when an accelerometer operation fails. */
    class AccelerometerFailedException : public SensorFailedException
    {
    public:
        /** @brief Constructs an AccelerometerFailedException with a default message. */
        AccelerometerFailedException();

        /**
         * @brief Constructs an AccelerometerFailedException with the given message.
         *
         * @param str Null-terminated error message string.
         */
        explicit AccelerometerFailedException(const char* str);

        /**
         * @brief Constructs an AccelerometerFailedException with the given message and platform error code.
         *
         * @param str Null-terminated error message string.
         * @param errorId Platform-specific error code identifying the failure.
         */
        AccelerometerFailedException(const char* str, SharpRuntime::intcs errorId);
    };
} // namespace Microsoft::Devices::Sensors
