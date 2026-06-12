// SPDX-License-Identifier: MS-PL
//
// Created by robertvokac on 5/26/25.
//

#pragma once
#include "System/Exception.hpp"


namespace Microsoft::Devices::Sensors
{
    /// Exception thrown when a sensor operation fails.
    class SensorFailedException : public System::Exception
    {
    public:
        /// Constructs a SensorFailedException with a default message.
        SensorFailedException();

        /// Constructs a SensorFailedException with the given message string.
        explicit SensorFailedException(const char* str);
    };
}
