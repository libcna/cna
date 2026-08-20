// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/GamerPrivilegeException.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    GamerPrivilegeException::GamerPrivilegeException()
        // sharp-runtime #2323 gave System::Exception() .NET's fallback message,
        // "Exception of type 'System.Exception' was thrown." -- which NAMES THE WRONG
        // TYPE for a derived exception. .NET/FNA's counterpart reaches Exception.Message
        // and interpolates its OWN runtime type name; C++ has no reflection, so the name
        // is resolved statically here, at the site that knows it. Same repair sharp-runtime
        // applied to HttpRequestException and JsonException. Downstream ticket #2377.
        : System::Exception(
              "Exception of type 'Microsoft.Xna.Framework.GamerServices.GamerPrivilegeException' was thrown.")
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
