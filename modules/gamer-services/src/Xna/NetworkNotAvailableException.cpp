// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/NetworkNotAvailableException.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    NetworkNotAvailableException::NetworkNotAvailableException()
        // sharp-runtime #2323 / downstream #2377: the base's default message names the BASE's
        // type, which for a DERIVED exception is a lie -- and #2323's own rule is that naming
        // the wrong type is worse than naming none. .NET/FNA interpolates the RUNTIME type
        // name; C++ has no reflection, so it is resolved statically at the site that knows it.
        : NetworkException(
              "Exception of type 'Microsoft.Xna.Framework.GamerServices.NetworkNotAvailableException' was thrown.")
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
        // sharp-runtime #2323 / downstream #2377: the base's default message names the BASE's
        // type, which for a DERIVED exception is a lie -- and #2323's own rule is that naming
        // the wrong type is worse than naming none. .NET/FNA interpolates the RUNTIME type
        // name; C++ has no reflection, so it is resolved statically at the site that knows it.
        : NetworkException(
              "Exception of type 'Microsoft.Xna.Framework.GamerServices.NetworkNotAvailableException' was thrown.")
    {
    }
}
