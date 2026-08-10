// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/NetworkException.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    NetworkException::NetworkException()
        : System::Exception()
    {
    }

    NetworkException::NetworkException(const std::string& message)
        : System::Exception(message)
    {
    }

    NetworkException::NetworkException(const std::string& message, std::exception_ptr innerException)
        : System::Exception(message, innerException)
    {
    }

    NetworkException::NetworkException(
        System::Runtime::Serialization::SerializationInfo& /*info*/,
        System::Runtime::Serialization::StreamingContext& /*context*/
    )
        : System::Exception()
    {
    }
}
