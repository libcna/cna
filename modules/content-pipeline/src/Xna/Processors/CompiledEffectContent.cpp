// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/CompiledEffectContent.hpp"

#include <utility>

#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateSerializer.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Processors
{
    CompiledEffectContent::CompiledEffectContent(std::vector<SharpRuntime::bytecs> effectCode)
        : effectCode_(std::move(effectCode))
    {
    }

    const std::vector<SharpRuntime::bytecs>& CompiledEffectContent::GetEffectCode() const noexcept
    {
        return effectCode_;
    }

    void CompiledEffectContent::DescribeContent(
        Serialization::Intermediate::ContentTypeDescriptor<CompiledEffectContent>& d)
    {
        d.BaseType<ContentItem>();
        d.Field("EffectCode", &CompiledEffectContent::effectCode_);
    }

    const std::string& CompiledEffectContent::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }
}
