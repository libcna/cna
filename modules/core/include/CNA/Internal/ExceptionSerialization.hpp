// SPDX-License-Identifier: MS-PL
#pragma once

#include <exception>
#include <string>
#include <utility>

#include "System/Runtime/Serialization/SerializationInfo.hpp"
#include "System/Runtime/Serialization/StreamingContext.hpp"

namespace CNA::Internal
{
    /**
     * @brief The exception base state XNA's serializable exception types write and restore.
     *
     * Several XNA 4.0 exception types declare .NET's obsolete-but-documented
     * `protected T(SerializationInfo, StreamingContext)` constructor and, for one of them, the
     * matching `GetObjectData`. In .NET both halves of the base state come from
     * `System.Exception` itself, which writes `Message` and `InnerException` among others and
     * restores them in its own serialization constructor.
     *
     * Sharp Runtime's `System::Exception` deliberately has neither -- in .NET they sit in CoreLib
     * beside `SerializationInfo`, while here the two are in different modules and adding them would
     * invert Runtime's dependency on Core.Base (recorded in sharp-runtime's CLAUDE.md). So a CNA
     * exception writes and restores its own base state, and these are the two keys and the two
     * operations it uses, in one place rather than once per exception.
     *
     * An inner exception travels as a `std::exception_ptr`, which is what `System::Exception`
     * stores: the round trip is in-process, and it is the exception object that travels, not a
     * serialized form of it. Nothing here writes bytes anywhere.
     */
    namespace ExceptionSerialization
    {
        /** @brief The name .NET's `Exception.GetObjectData` stores the message under. */
        inline constexpr const char* MessageKey = "Message";

        /** @brief The name .NET's `Exception.GetObjectData` stores the inner exception under. */
        inline constexpr const char* InnerExceptionKey = "InnerException";

        /**
         * @brief Writes the exception base state @p info must carry to reconstruct the exception.
         *
         * @param info The store to write into.
         * @param message The exception's message.
         * @param inner The exception's inner cause, or an empty pointer when there is none.
         */
        inline void WriteBaseState(System::Runtime::Serialization::SerializationInfo& info,
                                   const std::string& message,
                                   std::exception_ptr inner)
        {
            info.AddValue(MessageKey, message);
            info.AddValue(InnerExceptionKey, std::move(inner));
        }

        /**
         * @brief Reads the message back out of @p info.
         *
         * A missing message is an empty one rather than a failure: .NET's own serialization
         * constructor tolerates an absent `Message`, and a partially populated store is the normal
         * shape when a caller writes only the fields it cares about.
         *
         * @param info The store to read from.
         * @return The stored message, or an empty string when none was stored.
         */
        [[nodiscard]] inline std::string ReadMessage(
            const System::Runtime::Serialization::SerializationInfo& info)
        {
            return info.Contains(MessageKey) ? info.GetString(MessageKey) : std::string{};
        }

        /**
         * @brief Reads the inner exception back out of @p info.
         *
         * @param info The store to read from.
         * @return The stored inner cause, or an empty pointer when none was stored.
         */
        [[nodiscard]] inline std::exception_ptr ReadInnerException(
            const System::Runtime::Serialization::SerializationInfo& info)
        {
            return info.Contains(InnerExceptionKey)
                       ? info.GetValue<std::exception_ptr>(InnerExceptionKey)
                       : std::exception_ptr{};
        }
    }
}
