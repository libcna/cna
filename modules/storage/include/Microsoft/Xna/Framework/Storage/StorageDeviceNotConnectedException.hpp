// SPDX-License-Identifier: MS-PL
#pragma once

#include <exception>
#include <string>

#include "CNA/Internal/ExceptionSerialization.hpp"
#include "System/Runtime/InteropServices/ExternalException.hpp"
#include "System/Runtime/Serialization/SerializationInfo.hpp"
#include "System/Runtime/Serialization/StreamingContext.hpp"

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
                                           std::exception_ptr innerException);

        /**
         * @brief Writes this exception's state into @p info.
         *
         * The counterpart of the serialization constructor below.
         * `System.Exception.GetObjectData` is what XNA inherits for this; Sharp Runtime's Exception
         * has none (see CNA/Internal/ExceptionSerialization.hpp), so this type declares it and
         * writes the message and inner cause the constructor reads back.
         *
         * @param info The store to write into.
         * @param context The serialization context; unused, as it is in XNA.
         */
        void GetObjectData(System::Runtime::Serialization::SerializationInfo& info,
                           const System::Runtime::Serialization::StreamingContext& context) const;

        /** @brief Destroys the exception. */
        ~StorageDeviceNotConnectedException() override = default;

    protected:
        /**
         * @brief Creates a StorageDeviceNotConnectedException from serialized state.
         *
         * The documented protected `(SerializationInfo, StreamingContext)` constructor. XNA's body
         * is `base(info, context)` and nothing else -- this type adds no state of its own -- so what
         * it restores is the exception base state, which is what is restored here from the same two
         * keys .NET writes.
         *
         * @param info The store the exception's state was written into.
         * @param context The serialization context; unused, as it is in XNA.
         */
        StorageDeviceNotConnectedException(
            const System::Runtime::Serialization::SerializationInfo& info,
            const System::Runtime::Serialization::StreamingContext& context);
    };

} // namespace Microsoft::Xna::Framework::Storage
