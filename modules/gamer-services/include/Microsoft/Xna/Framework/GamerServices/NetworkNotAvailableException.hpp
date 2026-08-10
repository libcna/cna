// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/GamerServices/NetworkException.hpp"
#include "System/Runtime/Serialization/SerializationInfo.hpp"
#include "System/Runtime/Serialization/StreamingContext.hpp"
#include <exception>
#include <string>

namespace Microsoft::Xna::Framework::GamerServices
{
    /**
     * @brief The exception that is thrown when the network is not available.
     */
    class NetworkNotAvailableException : public NetworkException
    {
    public:
        /** @brief Initializes a new instance of NetworkNotAvailableException with a default message. */
        NetworkNotAvailableException();

        /**
         * @brief Initializes a new instance of NetworkNotAvailableException with the specified message.
         *
         * @param message A string that describes the error.
         */
        explicit NetworkNotAvailableException(const std::string& message);

        /**
         * @brief Initializes a new instance of NetworkNotAvailableException with the specified message and inner exception.
         *
         * @param message A string that describes the error.
         * @param innerException The exception that is the cause of the current exception.
         */
        NetworkNotAvailableException(const std::string& message, std::exception_ptr innerException);

    protected:
        /**
         * @brief Initializes a new instance of NetworkNotAvailableException with serialization data.
         *
         * @param info The object that holds the serialized object data.
         * @param context The contextual information about the source or destination.
         */
        NetworkNotAvailableException(
            System::Runtime::Serialization::SerializationInfo& info,
            System::Runtime::Serialization::StreamingContext& context
        );
    };
}
