// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
#pragma once

#include "System/Runtime/InteropServices/ExternalException.hpp"
#include <string>

namespace Microsoft::Xna::Framework::Storage
{
    /** @brief Thrown when an operation is attempted on a StorageDevice that is no longer connected. */
    class StorageDeviceNotConnectedException
        : public System::Runtime::InteropServices::ExternalException
    {
    public:
        /** @brief Constructs a StorageDeviceNotConnectedException with a default message. */
        StorageDeviceNotConnectedException();

        /**
         * @brief Constructs a StorageDeviceNotConnectedException with the given message.
         *
         * @param message Description of the error.
         */
        explicit StorageDeviceNotConnectedException(const std::string& message);

        /**
         * @brief Constructs a StorageDeviceNotConnectedException with a message and inner exception.
         *
         * @param message        Description of the error.
         * @param innerException Exception that caused this one.
         */
        StorageDeviceNotConnectedException(const std::string& message,
                                           const std::exception& innerException);

        /** @brief Destroys the exception. */
        ~StorageDeviceNotConnectedException() override = default;
    };

} // namespace Microsoft::Xna::Framework::Storage
