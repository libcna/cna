// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Net/NetworkSessionJoinException.hpp"

namespace Microsoft::Xna::Framework::Net
{
    NetworkSessionJoinException::NetworkSessionJoinException()
        : GamerServices::NetworkException()
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
