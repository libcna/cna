// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

#include <utility>

namespace Microsoft::Xna::Framework::Content
{
    ContentLoadException::ContentLoadException()
        : System::Exception(
              "Exception of type 'Microsoft.Xna.Framework.Content.ContentLoadException' was thrown.")
    {
    }

    ContentLoadException::ContentLoadException(const std::string& message)
        : System::Exception(message)
    {
    }

    // XNA Refresh ContentLoadException(string, Exception) IL_0000-0008 delegates
    // message and cause separately to System.Exception; never concatenate or slice.
    ContentLoadException::ContentLoadException(const std::string& message, std::exception_ptr inner)
        : System::Exception(message, std::move(inner))
    {
    }

    ContentLoadException::ContentLoadException(
        const System::Runtime::Serialization::SerializationInfo& info,
        const System::Runtime::Serialization::StreamingContext& context)
        : System::Exception(CNA::Internal::ExceptionSerialization::ReadMessage(info),
                            CNA::Internal::ExceptionSerialization::ReadInnerException(info))
    {
        (void)context;
    }

    void ContentLoadException::GetObjectData(
        System::Runtime::Serialization::SerializationInfo& info,
        const System::Runtime::Serialization::StreamingContext& context) const
    {
        (void)context;
        CNA::Internal::ExceptionSerialization::WriteBaseState(info, what(), getInnerExceptionProperty());
    }
}
