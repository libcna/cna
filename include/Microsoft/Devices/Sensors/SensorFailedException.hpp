// SPDX-License-Identifier: MS-PL
//
// Created by robertvokac on 5/26/25.
//

#pragma once
#include "System/Exception.hpp"


namespace Microsoft::Devices::Sensors
{
    /** @brief Exception thrown when a sensor operation fails. */
    class SensorFailedException : public System::Exception
    {
    public:
        /** @brief Constructs a SensorFailedException with a default message. */
        SensorFailedException();

        /**
         * @brief Constructs a SensorFailedException with the given message.
         *
         * @param str Null-terminated error message string.
         */
        explicit SensorFailedException(const char* str);
    };
}
