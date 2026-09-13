// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/MaterialContent.hpp"

#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateSerializer.hpp"
#include "System/ArgumentNullException.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Graphics
{
    TextureReferenceDictionary& MaterialContent::getTexturesProperty() noexcept { return textures_; }

    const TextureReferenceDictionary& MaterialContent::getTexturesProperty() const noexcept { return textures_; }

    std::shared_ptr<ExternalReference<TextureContent>> MaterialContent::GetTexture(const std::string& key) const
    {
        std::shared_ptr<ExternalReference<TextureContent>> stored;
        if (!textures_.TryGetValue(key, stored))
        {
            return nullptr;
        }
        return stored;
    }

    void MaterialContent::SetTexture(const std::string& key,
                                     const std::shared_ptr<ExternalReference<TextureContent>>& value)
    {
        RequireKey(key);
        if (value == nullptr)
        {
            textures_.Remove(key);
            return;
        }
        textures_.Set(key, value);
    }

    void MaterialContent::RequireKey(const std::string& key)
    {
        // C++ has no null std::string; an empty key stands in for the null the runtime refuses
        // (measured: material/set_property_null_name, material/set_texture_null_name).
        if (key.empty())
        {
            throw System::ArgumentNullException("key");
        }
    }

    void MaterialContent::DescribeContent(Serialization::Intermediate::ContentTypeDescriptor<MaterialContent>& d)
    {
        d.BaseType<ContentItem>();
        d.ReadOnlyProperty("Textures", [](MaterialContent& material) -> TextureReferenceDictionary&
                           { return material.getTexturesProperty(); })
            .Optional();
    }

    const std::string& MaterialContent::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }

    std::string MaterialContent::ToString() const { return GetTypeName(); }
}
