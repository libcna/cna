// SPDX-License-Identifier: MS-PL
#pragma once
#include "Microsoft/Xna/Framework/GamerServices/NetworkException.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionJoinError.hpp"
#include "CNA/Internal/ExceptionSerialization.hpp"
#include "System/Runtime/Serialization/SerializationInfo.hpp"
#include "System/Runtime/Serialization/StreamingContext.hpp"
#include <exception>
#include <string>

namespace Microsoft::Xna::Framework::Net
{
    /**
     * @brief The exception that is thrown when an attempt to join a network session fails.
     */
    class NetworkSessionJoinException : public Microsoft::Xna::Framework::GamerServices::NetworkException
    {
    public:
        /** @brief Initializes a new instance of NetworkSessionJoinException with a default message. */
        NetworkSessionJoinException();

        /**
         * @brief Initializes a new instance of NetworkSessionJoinException with the specified message.
         *
         * @param message A string that describes the error.
         */
        explicit NetworkSessionJoinException(const std::string& message);

        /**
         * @brief Initializes a new instance of NetworkSessionJoinException with the specified message and join error.
         *
         * @param message A string that describes the error.
         * @param joinError The reason the join attempt failed.
         */
        NetworkSessionJoinException(const std::string& message, NetworkSessionJoinError joinError);

        /**
         * @brief Initializes a new instance of NetworkSessionJoinException with the specified message and inner exception.
         *
         * @param message A string that describes the error.
         * @param innerException The exception that is the cause of the current exception.
         */
        NetworkSessionJoinException(const std::string& message, std::exception_ptr innerException);

        /**
         * @brief Gets the reason the join attempt failed.
         *
         * @return The join error.
         */
        [[nodiscard]] NetworkSessionJoinError getJoinErrorProperty() const;

        /**
         * @brief Sets the reason the join attempt failed.
         *
         * @param value The join error.
         */
        void setJoinErrorProperty(NetworkSessionJoinError value);

    protected:
        /**
         * @brief Initializes a new instance of NetworkSessionJoinException with serialization data.
         *
         * @param info The object that holds the serialized object data.
         * @param context The contextual information about the source or destination.
         */
        NetworkSessionJoinException(
            System::Runtime::Serialization::SerializationInfo& info,
            System::Runtime::Serialization::StreamingContext& context
        );

    public:
        /**
         * @brief Writes this exception's state into @p info.
         *
         * The documented `public override void GetObjectData(SerializationInfo, StreamingContext)`.
         * XNA writes the base state first and then adds the join error under the name `joinError`,
         * which is exactly the name the serialization constructor reads back; this does the same,
         * so the two halves cannot drift apart.
         *
         * @param info The store to write into; the base state is written before the join error.
         * @param context The serialization context, passed through to the base.
         */
        void GetObjectData(
            System::Runtime::Serialization::SerializationInfo& info,
            const System::Runtime::Serialization::StreamingContext& context) const override;

    private:
        NetworkSessionJoinError joinError_{NetworkSessionJoinError::SessionNotFound};
    };
}
