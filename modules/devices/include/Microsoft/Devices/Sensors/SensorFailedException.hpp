// SPDX-License-Identifier: MS-PL
//
// Created by robertvokac on 5/26/25.
//

#pragma once
#include "System/Exception.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"


namespace Microsoft::Devices::Sensors
{
    /** @brief Exception thrown when a sensor operation fails. */
    class SensorFailedException : public System::Exception
    {
    private:
        SharpRuntime::intcs errorId_ = 0;

    public:
        /** @brief Constructs a SensorFailedException with a default message. */
        SensorFailedException();

        /**
         * @brief Constructs a SensorFailedException with the given message.
         *
         * @param str Null-terminated error message string.
         */
        explicit SensorFailedException(const char* str);

        /**
         * @brief Constructs a SensorFailedException with the given message and platform error code.
         *
         * @param str Null-terminated error message string.
         * @param errorId Platform-specific error code identifying the failure.
         */
        SensorFailedException(const char* str, SharpRuntime::intcs errorId);

        /**
         * @brief Gets the platform-specific error code identifying the failure.
         *
         * @return Error code, or 0 if none was specified.
         */
        [[nodiscard]] SharpRuntime::intcs getErrorIdProperty() const noexcept;
    };
}
