// SPDX-License-Identifier: MS-PL
#pragma once

#include "System/Exception.hpp"
#include "CNA/Internal/ExceptionSerialization.hpp"
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

        /**
         * @brief Writes this exception's state into @p info.
         *
         * `System.Exception.GetObjectData`, which every XNA exception inherits and
         * NetworkSessionJoinException overrides. Sharp Runtime's Exception declares no such member
         * -- in .NET it sits in CoreLib beside SerializationInfo, while here the two are in
         * different modules -- so the chain starts here, at the topmost CNA exception that needs
         * it, and is virtual so a derived type can add its own state after the base state.
         *
         * @param info The store to write into.
         * @param context The serialization context; unused here, as it is in XNA.
         */
        virtual void GetObjectData(
            System::Runtime::Serialization::SerializationInfo& info,
            const System::Runtime::Serialization::StreamingContext& context) const;

    protected:
        /**
         * @brief Initializes a new instance of NetworkException with serialization data.
         *
         * Restores the exception base state from the two keys .NET's own serialization constructor
         * reads. It previously discarded @p info entirely and produced a default-constructed
         * exception, which made a round trip lose the message it had just written.
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
