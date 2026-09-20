// SPDX-License-Identifier: MS-PL
#pragma once

#include <exception>
#include <stdexcept>
#include <string>

#include "CNA/Internal/ExceptionSerialization.hpp"
#include "System/Runtime/Serialization/SerializationInfo.hpp"
#include "System/Runtime/Serialization/StreamingContext.hpp"

namespace Microsoft::Xna::Framework::Content
{
    /** @brief Exception thrown when an asset cannot be loaded by the content manager. */
    class ContentLoadException : public std::runtime_error
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

        /**
         * @brief Returns the exception that caused this one, when one is known.
         *
         * `System.Exception.InnerException`, which XNA inherits. CNA's ContentLoadException derives
         * from `std::runtime_error` -- deliberately, because a great deal of code catches
         * `std::runtime_error` around content loads -- and that base has no inner-cause field, so
         * the cause is stored here.
         *
         * Populated by the serialization constructor above. The `(message, const std::exception&)`
         * constructor instead folds the cause's text into the message, which is CNA's long-standing
         * behaviour for it and is what every existing message assertion depends on; a
         * `std::exception` reference cannot be captured as an `exception_ptr` without slicing it to
         * its base, so promoting it there would lose the very thing an inner cause is for.
         *
         * @return The inner cause, or an empty pointer when there is none.
         */
        [[nodiscard]] std::exception_ptr getInnerExceptionProperty() const;

    private:
        std::exception_ptr innerException_;
    };
}
