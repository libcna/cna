// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Net/NetworkSessionJoinException.hpp"

namespace Microsoft::Xna::Framework::Net
{
    namespace
    {
        /// The name XNA stores the join error under, read by the serialization constructor and
        /// written by GetObjectData. Named once so the two cannot disagree.
        constexpr const char* kJoinErrorKey = "joinError";
    }

    NetworkSessionJoinException::NetworkSessionJoinException()
        // sharp-runtime #2323 / downstream #2377: the base's default message names the BASE's
        // type, which for a DERIVED exception is a lie -- and #2323's own rule is that naming
        // the wrong type is worse than naming none. .NET/FNA interpolates the RUNTIME type
        // name; C++ has no reflection, so it is resolved statically at the site that knows it.
        : GamerServices::NetworkException(
              "Exception of type 'Microsoft.Xna.Framework.Net.NetworkSessionJoinException' was thrown.")
    {
    }

    NetworkSessionJoinException::NetworkSessionJoinException(const std::string& message)
        : GamerServices::NetworkException(message)
    {
    }

    NetworkSessionJoinException::NetworkSessionJoinException(
        const std::string& message,
        NetworkSessionJoinError joinError
    )
        : GamerServices::NetworkException(message)
        , joinError_(joinError)
    {
    }

    NetworkSessionJoinException::NetworkSessionJoinException(
        const std::string& message,
        std::exception_ptr innerException
    )
        : GamerServices::NetworkException(message, innerException)
    {
    }

    NetworkSessionJoinException::NetworkSessionJoinException(
        System::Runtime::Serialization::SerializationInfo& info,
        System::Runtime::Serialization::StreamingContext& context
    )
        : GamerServices::NetworkException(info, context)
    {
        // XNA reads the join error back here, under the same name GetObjectData wrote it. This
        // previously left joinError_ at its default, so a round trip silently reported
        // SessionNotFound whatever had been serialized.
        if (info.Contains(kJoinErrorKey))
        {
            joinError_ = static_cast<NetworkSessionJoinError>(info.GetInt32(kJoinErrorKey));
        }
    }

    void NetworkSessionJoinException::GetObjectData(
        System::Runtime::Serialization::SerializationInfo& info,
        const System::Runtime::Serialization::StreamingContext& context) const
    {
        GamerServices::NetworkException::GetObjectData(info, context);
        info.AddValue(kJoinErrorKey, static_cast<SharpRuntime::intcs>(joinError_));
    }

    NetworkSessionJoinError NetworkSessionJoinException::getJoinErrorProperty() const { return joinError_; }
    void NetworkSessionJoinException::setJoinErrorProperty(NetworkSessionJoinError value) { joinError_ = value; }
}
