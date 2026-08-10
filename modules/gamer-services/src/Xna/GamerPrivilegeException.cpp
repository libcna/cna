// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/GamerPrivilegeException.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    GamerPrivilegeException::GamerPrivilegeException()
        : System::Exception()
    {
    }

    GamerPrivilegeException::GamerPrivilegeException(const std::string& message)
        : System::Exception(message)
    {
    }

    GamerPrivilegeException::GamerPrivilegeException(const std::string& message, std::exception_ptr innerException)
        : System::Exception(message, innerException)
    {
    }

    GamerPrivilegeException::GamerPrivilegeException(
        System::Runtime::Serialization::SerializationInfo& /*info*/,
        System::Runtime::Serialization::StreamingContext& /*context*/
    )
        : System::Exception()
    {
    }
}
