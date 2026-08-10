// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/GuideAlreadyVisibleException.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    GuideAlreadyVisibleException::GuideAlreadyVisibleException()
        : System::Exception()
    {
    }

    GuideAlreadyVisibleException::GuideAlreadyVisibleException(const std::string& message)
        : System::Exception(message)
    {
    }

    GuideAlreadyVisibleException::GuideAlreadyVisibleException(const std::string& message, std::exception_ptr innerException)
        : System::Exception(message, innerException)
    {
    }

    GuideAlreadyVisibleException::GuideAlreadyVisibleException(
        System::Runtime::Serialization::SerializationInfo& /*info*/,
        System::Runtime::Serialization::StreamingContext& /*context*/
    )
        : System::Exception()
    {
    }
}
