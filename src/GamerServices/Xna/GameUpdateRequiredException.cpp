// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/GameUpdateRequiredException.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    GameUpdateRequiredException::GameUpdateRequiredException()
        : System::Exception()
    {
    }

    GameUpdateRequiredException::GameUpdateRequiredException(const std::string& message)
        : System::Exception(message)
    {
    }

    GameUpdateRequiredException::GameUpdateRequiredException(const std::string& message, std::exception_ptr innerException)
        : System::Exception(message, innerException)
    {
    }

    GameUpdateRequiredException::GameUpdateRequiredException(
        System::Runtime::Serialization::SerializationInfo& /*info*/,
        System::Runtime::Serialization::StreamingContext& /*context*/
    )
        : System::Exception()
    {
    }
}
