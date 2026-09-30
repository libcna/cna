// SPDX-License-Identifier: MS-PL
#pragma once

#include <exception>
#include <string>

#include "CNA/Internal/ExceptionSerialization.hpp"
#include "System/Exception.hpp"
#include "System/Runtime/Serialization/SerializationInfo.hpp"
#include "System/Runtime/Serialization/StreamingContext.hpp"

namespace Microsoft::Xna::Framework::Content
{
    /** @brief Exception thrown when an asset cannot be loaded by the content manager. */
    class ContentLoadException : public System::Exception
    {
    public:
        /**
         * @brief Creates a ContentLoadException with the default message.
         *
         * The documented parameterless constructor. XNA leaves the message to
         * `System.Exception`'s parameterless constructor, which produces .NET's fallback text
         * naming the exception's own type, so that is the message here.
         */
        ContentLoadException();

        /**
         * @brief Constructs a ContentLoadException with the given error message.
         *
         * @param message Description of the load failure.
         */
        explicit ContentLoadException(const std::string& message);

        /**
         * @brief Constructs a ContentLoadException with a message and an inner exception.
         *
         * @param message Description of the load failure.
         * @param inner   Exception that caused this one.
         */
        ContentLoadException(const std::string& message, const std::exception& inner);

    protected:
        /**
         * @brief Creates a ContentLoadException from serialized state.
         *
         * The documented protected `(SerializationInfo, StreamingContext)` constructor. XNA leaves
         * the whole job to `System.Exception`'s own serialization constructor, which restores the
         * message and the inner exception; Sharp Runtime's Exception has no such constructor
         * (see CNA/Internal/ExceptionSerialization.hpp for why), so the state is restored here from
         * the same two keys .NET writes. A store missing either one yields the empty value for it,
         * as .NET's does.
         *
         * @param info The store the exception's state was written into.
         * @param context The serialization context; unused, as it is in XNA.
         */
        ContentLoadException(const System::Runtime::Serialization::SerializationInfo& info,
                             const System::Runtime::Serialization::StreamingContext& context);

    public:
        /**
         * @brief Writes this exception's state into @p info.
         *
         * The counterpart of the serialization constructor above. `System.Exception.GetObjectData`
         * is what XNA inherits for this; Sharp Runtime's Exception has none, so ContentLoadException
         * declares it and writes the message and inner cause the constructor reads back.
         *
         * @param info The store to write into.
         * @param context The serialization context; unused, as it is in XNA.
         */
        void GetObjectData(System::Runtime::Serialization::SerializationInfo& info,
                           const System::Runtime::Serialization::StreamingContext& context) const;
    };
}
