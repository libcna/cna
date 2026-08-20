// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Net/NetworkSessionJoinException.hpp"

namespace Microsoft::Xna::Framework::Net
{
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
    }

    NetworkSessionJoinError NetworkSessionJoinException::getJoinErrorProperty() const { return joinError_; }
    void NetworkSessionJoinException::setJoinErrorProperty(NetworkSessionJoinError value) { joinError_ = value; }
}
