// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Compiler/ContentTypeWriter.hpp"

#include <utility>

namespace Microsoft::Xna::Framework::Content::Pipeline::Serialization::Compiler
{
    ContentTypeWriterBase::ContentTypeWriterBase(System::Type targetType)
        : targetType_(targetType), runtimeTypeName_(targetType.getFullNameProperty())
    {
    }

    ContentTypeWriterBase::ContentTypeWriterBase(System::Type targetType, std::string runtimeTypeName)
        : targetType_(targetType), runtimeTypeName_(std::move(runtimeTypeName))
    {
    }

    bool ContentTypeWriterBase::getCanDeserializeIntoExistingObjectProperty() const
    {
        return false;
    }

    System::Type ContentTypeWriterBase::getTargetTypeProperty() const noexcept
    {
        return targetType_;
    }

    std::int32_t ContentTypeWriterBase::getTypeVersionProperty() const
    {
        return 0;
    }

    std::string ContentTypeWriterBase::GetRuntimeType(TargetPlatform targetPlatform) const
    {
        (void)targetPlatform;
        return runtimeTypeName_;
    }

    void ContentTypeWriterBase::Initialize(ContentCompiler& compiler)
    {
        (void)compiler;
    }

    bool ContentTypeWriterBase::ShouldCompressContent(TargetPlatform targetPlatform,
                                                      const ContentObject& value) const
    {
        (void)targetPlatform;
        (void)value;
        return true;
    }
}
