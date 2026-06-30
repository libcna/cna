// SPDX-License-Identifier: MS-PL
#pragma once

#include "System/Exception.hpp"
#include "System/Runtime/Serialization/SerializationInfo.hpp"
#include "System/Runtime/Serialization/StreamingContext.hpp"
#include <exception>
#include <string>

namespace Microsoft::Xna::Framework::GamerServices
{
    /**
     * @brief The exception that is thrown when a network error occurs.
     */
    class NetworkException : public System::Exception
    {
    public:
        /** @brief Initializes a new instance of NetworkException with a default message. */
        NetworkException();

        /**
         * @brief Initializes a new instance of NetworkException with the specified message.
         *
         * @param message A string that describes the error.
         */
        explicit NetworkException(const std::string& message);

        /**
         * @brief Initializes a new instance of NetworkException with the specified message and inner exception.
         *
         * @param message A string that describes the error.
         * @param innerException The exception that is the cause of the current exception.
         */
        NetworkException(const std::string& message, std::exception_ptr innerException);

    protected:
        /**
         * @brief Initializes a new instance of NetworkException with serialization data.
         *
         * @param info The object that holds the serialized object data.
         * @param context The contextual information about the source or destination.
         */
        NetworkException(
            System::Runtime::Serialization::SerializationInfo& info,
            System::Runtime::Serialization::StreamingContext& context
        );
    };
}
