// SPDX-License-Identifier: MS-PL
#pragma once

#include "System/Exception.hpp"
#include <exception>
#include <string>

namespace Microsoft::Xna::Framework::Audio
{
    /** @brief Thrown when a requested microphone device is not connected. */
    class NoMicrophoneConnectedException final : public System::Exception
    {
    public:
        /** @brief Constructs a NoMicrophoneConnectedException with the default message. */
        NoMicrophoneConnectedException();

        /**
         * @brief Constructs a NoMicrophoneConnectedException with the given message.
         *
         * @param message Error description.
         */
        explicit NoMicrophoneConnectedException(const std::string& message);

        /**
         * @brief Constructs a NoMicrophoneConnectedException with a message and an inner exception.
         *
         * @param message        Error description.
         * @param innerException Underlying exception that caused this one.
         */
        NoMicrophoneConnectedException(const std::string& message, std::exception_ptr innerException);
    };
}
