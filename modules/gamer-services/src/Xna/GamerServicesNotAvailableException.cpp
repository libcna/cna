// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesNotAvailableException.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    GamerServicesNotAvailableException::GamerServicesNotAvailableException()
        : System::Exception()
    {
    }

    GamerServicesNotAvailableException::GamerServicesNotAvailableException(const std::string& message)
        : System::Exception(message)
    {
    }

    GamerServicesNotAvailableException::GamerServicesNotAvailableException(const std::string& message, std::exception_ptr innerException)
        : System::Exception(message, innerException)
    {
    }

    GamerServicesNotAvailableException::GamerServicesNotAvailableException(
        System::Runtime::Serialization::SerializationInfo& /*info*/,
        System::Runtime::Serialization::StreamingContext& /*context*/
    )
        : System::Exception()
    {
    }
}
