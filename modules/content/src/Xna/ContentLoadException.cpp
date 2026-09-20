// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

#include <utility>

namespace Microsoft::Xna::Framework::Content
{
    ContentLoadException::ContentLoadException()
        : std::runtime_error(
              "Exception of type 'Microsoft.Xna.Framework.Content.ContentLoadException' was thrown.")
    {
    }

    ContentLoadException::ContentLoadException(const std::string& message)
        : std::runtime_error(message)
    {
    }

    ContentLoadException::ContentLoadException(const std::string& message, const std::exception& inner)
        : std::runtime_error(message + std::string(" ---> ") + inner.what())
    {
    }

    ContentLoadException::ContentLoadException(
        const System::Runtime::Serialization::SerializationInfo& info,
        const System::Runtime::Serialization::StreamingContext& context)
        : std::runtime_error(CNA::Internal::ExceptionSerialization::ReadMessage(info))
        , innerException_(CNA::Internal::ExceptionSerialization::ReadInnerException(info))
    {
        (void)context;
    }

    void ContentLoadException::GetObjectData(
        System::Runtime::Serialization::SerializationInfo& info,
        const System::Runtime::Serialization::StreamingContext& context) const
    {
        (void)context;
        CNA::Internal::ExceptionSerialization::WriteBaseState(info, what(), innerException_);
    }

    std::exception_ptr ContentLoadException::getInnerExceptionProperty() const
    {
        return innerException_;
    }
}
