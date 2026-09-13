// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/EffectContent.hpp"

#include <utility>

#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateSerializer.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Graphics
{
    const std::optional<std::string>& EffectContent::getEffectCodeProperty() const noexcept { return effectCode_; }

    void EffectContent::setEffectCodeProperty(std::optional<std::string> value)
    {
        effectCode_ = std::move(value);
    }

    void EffectContent::DescribeContent(Serialization::Intermediate::ContentTypeDescriptor<EffectContent>& d)
    {
        d.BaseType<ContentItem>();
        d.Property("EffectCode", &EffectContent::getEffectCodeProperty, &EffectContent::setEffectCodeProperty);
    }

    const std::string& EffectContent::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }
}
