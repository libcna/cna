// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/NetworkNotAvailableException.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    NetworkNotAvailableException::NetworkNotAvailableException()
        : NetworkException()
    {
    }

    NetworkNotAvailableException::NetworkNotAvailableException(const std::string& message)
        : NetworkException(message)
    {
    }

    NetworkNotAvailableException::NetworkNotAvailableException(const std::string& message, std::exception_ptr innerException)
        : NetworkException(message, innerException)
    {
    }

    NetworkNotAvailableException::NetworkNotAvailableException(
        System::Runtime::Serialization::SerializationInfo& /*info*/,
        System::Runtime::Serialization::StreamingContext& /*context*/
    )
        : NetworkException()
    {
    }
}
