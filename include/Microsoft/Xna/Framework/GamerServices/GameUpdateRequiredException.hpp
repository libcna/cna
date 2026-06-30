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
     * @brief The exception that is thrown when the game requires an update before it can connect.
     */
    class GameUpdateRequiredException : public System::Exception
    {
    public:
        /** @brief Initializes a new instance of GameUpdateRequiredException with a default message. */
        GameUpdateRequiredException();

        /**
         * @brief Initializes a new instance of GameUpdateRequiredException with the specified message.
         *
         * @param message A string that describes the error.
         */
        explicit GameUpdateRequiredException(const std::string& message);

        /**
         * @brief Initializes a new instance of GameUpdateRequiredException with the specified message and inner exception.
         *
         * @param message A string that describes the error.
         * @param innerException The exception that is the cause of the current exception.
         */
        GameUpdateRequiredException(const std::string& message, std::exception_ptr innerException);

    protected:
        /**
         * @brief Initializes a new instance of GameUpdateRequiredException with serialization data.
         *
         * @param info The object that holds the serialized object data.
         * @param context The contextual information about the source or destination.
         */
        GameUpdateRequiredException(
            System::Runtime::Serialization::SerializationInfo& info,
            System::Runtime::Serialization::StreamingContext& context
        );
    };
}
