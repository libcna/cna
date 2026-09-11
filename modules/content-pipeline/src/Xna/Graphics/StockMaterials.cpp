// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/StockMaterials.hpp"

#include <utility>

#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateSerializer.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Graphics
{
    // ------------------------------------------------------------------------------------------
    // BasicMaterialContent
    // ------------------------------------------------------------------------------------------

    std::optional<SharpRuntime::Single> BasicMaterialContent::getAlphaProperty() const
    {
        return GetValueTypeProperty<SharpRuntime::Single>(std::string(AlphaKey));
    }

    void BasicMaterialContent::setAlphaProperty(std::optional<SharpRuntime::Single> value)
    {
        SetProperty(std::string(AlphaKey), value);
    }

    std::optional<Vector3> BasicMaterialContent::getDiffuseColorProperty() const
    {
        return GetValueTypeProperty<Vector3>(std::string(DiffuseColorKey));
    }

    void BasicMaterialContent::setDiffuseColorProperty(std::optional<Vector3> value)
    {
        SetProperty(std::string(DiffuseColorKey), value);
    }

    std::optional<Vector3> BasicMaterialContent::getEmissiveColorProperty() const
    {
        return GetValueTypeProperty<Vector3>(std::string(EmissiveColorKey));
    }

    void BasicMaterialContent::setEmissiveColorProperty(std::optional<Vector3> value)
    {
        SetProperty(std::string(EmissiveColorKey), value);
    }

    std::optional<Vector3> BasicMaterialContent::getSpecularColorProperty() const
    {
        return GetValueTypeProperty<Vector3>(std::string(SpecularColorKey));
    }

    void BasicMaterialContent::setSpecularColorProperty(std::optional<Vector3> value)
    {
        SetProperty(std::string(SpecularColorKey), value);
    }

    std::optional<SharpRuntime::Single> BasicMaterialContent::getSpecularPowerProperty() const
    {
        return GetValueTypeProperty<SharpRuntime::Single>(std::string(SpecularPowerKey));
    }

    void BasicMaterialContent::setSpecularPowerProperty(std::optional<SharpRuntime::Single> value)
    {
        SetProperty(std::string(SpecularPowerKey), value);
    }

    std::shared_ptr<ExternalReference<TextureContent>> BasicMaterialContent::getTextureProperty() const
    {
        return GetTexture(std::string(TextureKey));
    }

    void BasicMaterialContent::setTextureProperty(std::shared_ptr<ExternalReference<TextureContent>> value)
    {
        SetTexture(std::string(TextureKey), std::move(value));
    }

    std::optional<bool> BasicMaterialContent::getVertexColorEnabledProperty() const
    {
        return GetValueTypeProperty<bool>(std::string(VertexColorEnabledKey));
    }

    void BasicMaterialContent::setVertexColorEnabledProperty(std::optional<bool> value)
    {
        SetProperty(std::string(VertexColorEnabledKey), value);
    }

    void BasicMaterialContent::DescribeContent(Serialization::Intermediate::ContentTypeDescriptor<BasicMaterialContent>& d)
    {
        // A stock material adds no serialized member of its own: every property is a view
        // over the base's OpaqueData and Textures (measured, material/serialize_basic).
        d.BaseType<MaterialContent>();
    }

    const std::string& BasicMaterialContent::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }

    // ------------------------------------------------------------------------------------------
    // AlphaTestMaterialContent
    // ------------------------------------------------------------------------------------------

    std::optional<SharpRuntime::Single> AlphaTestMaterialContent::getAlphaProperty() const
    {
        return GetValueTypeProperty<SharpRuntime::Single>(std::string(AlphaKey));
    }

    void AlphaTestMaterialContent::setAlphaProperty(std::optional<SharpRuntime::Single> value)
    {
        SetProperty(std::string(AlphaKey), value);
    }

    std::optional<Microsoft::Xna::Framework::Graphics::CompareFunction> AlphaTestMaterialContent::getAlphaFunctionProperty() const
    {
        return GetValueTypeProperty<Microsoft::Xna::Framework::Graphics::CompareFunction>(std::string(AlphaFunctionKey));
    }

    void AlphaTestMaterialContent::setAlphaFunctionProperty(std::optional<Microsoft::Xna::Framework::Graphics::CompareFunction> value)
    {
        SetProperty(std::string(AlphaFunctionKey), value);
    }

    std::optional<Vector3> AlphaTestMaterialContent::getDiffuseColorProperty() const
    {
        return GetValueTypeProperty<Vector3>(std::string(DiffuseColorKey));
    }

    void AlphaTestMaterialContent::setDiffuseColorProperty(std::optional<Vector3> value)
    {
        SetProperty(std::string(DiffuseColorKey), value);
    }

    std::optional<SharpRuntime::intcs> AlphaTestMaterialContent::getReferenceAlphaProperty() const
    {
        return GetValueTypeProperty<SharpRuntime::intcs>(std::string(ReferenceAlphaKey));
    }

    void AlphaTestMaterialContent::setReferenceAlphaProperty(std::optional<SharpRuntime::intcs> value)
    {
        SetProperty(std::string(ReferenceAlphaKey), value);
    }

    std::shared_ptr<ExternalReference<TextureContent>> AlphaTestMaterialContent::getTextureProperty() const
    {
        return GetTexture(std::string(TextureKey));
    }

    void AlphaTestMaterialContent::setTextureProperty(std::shared_ptr<ExternalReference<TextureContent>> value)
    {
        SetTexture(std::string(TextureKey), std::move(value));
    }

    std::optional<bool> AlphaTestMaterialContent::getVertexColorEnabledProperty() const
    {
        return GetValueTypeProperty<bool>(std::string(VertexColorEnabledKey));
    }

    void AlphaTestMaterialContent::setVertexColorEnabledProperty(std::optional<bool> value)
    {
        SetProperty(std::string(VertexColorEnabledKey), value);
    }

    void AlphaTestMaterialContent::DescribeContent(Serialization::Intermediate::ContentTypeDescriptor<AlphaTestMaterialContent>& d)
    {
        // A stock material adds no serialized member of its own: every property is a view
        // over the base's OpaqueData and Textures (measured, material/serialize_basic).
        d.BaseType<MaterialContent>();
    }

    const std::string& AlphaTestMaterialContent::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }

    // ------------------------------------------------------------------------------------------
    // DualTextureMaterialContent
    // ------------------------------------------------------------------------------------------

    std::optional<SharpRuntime::Single> DualTextureMaterialContent::getAlphaProperty() const
    {
        return GetValueTypeProperty<SharpRuntime::Single>(std::string(AlphaKey));
    }

    void DualTextureMaterialContent::setAlphaProperty(std::optional<SharpRuntime::Single> value)
    {
        SetProperty(std::string(AlphaKey), value);
    }

    std::optional<Vector3> DualTextureMaterialContent::getDiffuseColorProperty() const
    {
        return GetValueTypeProperty<Vector3>(std::string(DiffuseColorKey));
    }

    void DualTextureMaterialContent::setDiffuseColorProperty(std::optional<Vector3> value)
    {
        SetProperty(std::string(DiffuseColorKey), value);
    }

    std::shared_ptr<ExternalReference<TextureContent>> DualTextureMaterialContent::getTextureProperty() const
    {
        return GetTexture(std::string(TextureKey));
    }

    void DualTextureMaterialContent::setTextureProperty(std::shared_ptr<ExternalReference<TextureContent>> value)
    {
        SetTexture(std::string(TextureKey), std::move(value));
    }

    std::shared_ptr<ExternalReference<TextureContent>> DualTextureMaterialContent::getTexture2Property() const
    {
        return GetTexture(std::string(Texture2Key));
    }

    void DualTextureMaterialContent::setTexture2Property(std::shared_ptr<ExternalReference<TextureContent>> value)
    {
        SetTexture(std::string(Texture2Key), std::move(value));
    }

    std::optional<bool> DualTextureMaterialContent::getVertexColorEnabledProperty() const
    {
        return GetValueTypeProperty<bool>(std::string(VertexColorEnabledKey));
    }

    void DualTextureMaterialContent::setVertexColorEnabledProperty(std::optional<bool> value)
    {
        SetProperty(std::string(VertexColorEnabledKey), value);
    }

    void DualTextureMaterialContent::DescribeContent(Serialization::Intermediate::ContentTypeDescriptor<DualTextureMaterialContent>& d)
    {
        // A stock material adds no serialized member of its own: every property is a view
        // over the base's OpaqueData and Textures (measured, material/serialize_basic).
        d.BaseType<MaterialContent>();
    }

    const std::string& DualTextureMaterialContent::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }

    // ------------------------------------------------------------------------------------------
    // EnvironmentMapMaterialContent
    // ------------------------------------------------------------------------------------------

    std::optional<SharpRuntime::Single> EnvironmentMapMaterialContent::getAlphaProperty() const
    {
        return GetValueTypeProperty<SharpRuntime::Single>(std::string(AlphaKey));
    }

    void EnvironmentMapMaterialContent::setAlphaProperty(std::optional<SharpRuntime::Single> value)
    {
        SetProperty(std::string(AlphaKey), value);
    }

    std::optional<Vector3> EnvironmentMapMaterialContent::getDiffuseColorProperty() const
    {
        return GetValueTypeProperty<Vector3>(std::string(DiffuseColorKey));
    }

    void EnvironmentMapMaterialContent::setDiffuseColorProperty(std::optional<Vector3> value)
    {
        SetProperty(std::string(DiffuseColorKey), value);
    }

    std::optional<Vector3> EnvironmentMapMaterialContent::getEmissiveColorProperty() const
    {
        return GetValueTypeProperty<Vector3>(std::string(EmissiveColorKey));
    }

    void EnvironmentMapMaterialContent::setEmissiveColorProperty(std::optional<Vector3> value)
    {
        SetProperty(std::string(EmissiveColorKey), value);
    }

    std::shared_ptr<ExternalReference<TextureContent>> EnvironmentMapMaterialContent::getEnvironmentMapProperty() const
    {
        return GetTexture(std::string(EnvironmentMapKey));
    }

    void EnvironmentMapMaterialContent::setEnvironmentMapProperty(std::shared_ptr<ExternalReference<TextureContent>> value)
    {
        SetTexture(std::string(EnvironmentMapKey), std::move(value));
    }

    std::optional<SharpRuntime::Single> EnvironmentMapMaterialContent::getEnvironmentMapAmountProperty() const
    {
        return GetValueTypeProperty<SharpRuntime::Single>(std::string(EnvironmentMapAmountKey));
    }

    void EnvironmentMapMaterialContent::setEnvironmentMapAmountProperty(std::optional<SharpRuntime::Single> value)
    {
        SetProperty(std::string(EnvironmentMapAmountKey), value);
    }

    std::optional<Vector3> EnvironmentMapMaterialContent::getEnvironmentMapSpecularProperty() const
    {
        return GetValueTypeProperty<Vector3>(std::string(EnvironmentMapSpecularKey));
    }

    void EnvironmentMapMaterialContent::setEnvironmentMapSpecularProperty(std::optional<Vector3> value)
    {
        SetProperty(std::string(EnvironmentMapSpecularKey), value);
    }

    std::optional<SharpRuntime::Single> EnvironmentMapMaterialContent::getFresnelFactorProperty() const
    {
        return GetValueTypeProperty<SharpRuntime::Single>(std::string(FresnelFactorKey));
    }

    void EnvironmentMapMaterialContent::setFresnelFactorProperty(std::optional<SharpRuntime::Single> value)
    {
        SetProperty(std::string(FresnelFactorKey), value);
    }

    std::shared_ptr<ExternalReference<TextureContent>> EnvironmentMapMaterialContent::getTextureProperty() const
    {
        return GetTexture(std::string(TextureKey));
    }

    void EnvironmentMapMaterialContent::setTextureProperty(std::shared_ptr<ExternalReference<TextureContent>> value)
    {
        SetTexture(std::string(TextureKey), std::move(value));
    }

    void EnvironmentMapMaterialContent::DescribeContent(Serialization::Intermediate::ContentTypeDescriptor<EnvironmentMapMaterialContent>& d)
    {
        // A stock material adds no serialized member of its own: every property is a view
        // over the base's OpaqueData and Textures (measured, material/serialize_basic).
        d.BaseType<MaterialContent>();
    }

    const std::string& EnvironmentMapMaterialContent::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }

    // ------------------------------------------------------------------------------------------
    // SkinnedMaterialContent
    // ------------------------------------------------------------------------------------------

    std::optional<SharpRuntime::Single> SkinnedMaterialContent::getAlphaProperty() const
    {
        return GetValueTypeProperty<SharpRuntime::Single>(std::string(AlphaKey));
    }

    void SkinnedMaterialContent::setAlphaProperty(std::optional<SharpRuntime::Single> value)
    {
        SetProperty(std::string(AlphaKey), value);
    }

    std::optional<Vector3> SkinnedMaterialContent::getDiffuseColorProperty() const
    {
        return GetValueTypeProperty<Vector3>(std::string(DiffuseColorKey));
    }

    void SkinnedMaterialContent::setDiffuseColorProperty(std::optional<Vector3> value)
    {
        SetProperty(std::string(DiffuseColorKey), value);
    }

    std::optional<Vector3> SkinnedMaterialContent::getEmissiveColorProperty() const
    {
        return GetValueTypeProperty<Vector3>(std::string(EmissiveColorKey));
    }

    void SkinnedMaterialContent::setEmissiveColorProperty(std::optional<Vector3> value)
    {
        SetProperty(std::string(EmissiveColorKey), value);
    }

    std::optional<Vector3> SkinnedMaterialContent::getSpecularColorProperty() const
    {
        return GetValueTypeProperty<Vector3>(std::string(SpecularColorKey));
    }

    void SkinnedMaterialContent::setSpecularColorProperty(std::optional<Vector3> value)
    {
        SetProperty(std::string(SpecularColorKey), value);
    }

    std::optional<SharpRuntime::Single> SkinnedMaterialContent::getSpecularPowerProperty() const
    {
        return GetValueTypeProperty<SharpRuntime::Single>(std::string(SpecularPowerKey));
    }

    void SkinnedMaterialContent::setSpecularPowerProperty(std::optional<SharpRuntime::Single> value)
    {
        SetProperty(std::string(SpecularPowerKey), value);
    }

    std::shared_ptr<ExternalReference<TextureContent>> SkinnedMaterialContent::getTextureProperty() const
    {
        return GetTexture(std::string(TextureKey));
    }

    void SkinnedMaterialContent::setTextureProperty(std::shared_ptr<ExternalReference<TextureContent>> value)
    {
        SetTexture(std::string(TextureKey), std::move(value));
    }

    std::optional<SharpRuntime::intcs> SkinnedMaterialContent::getWeightsPerVertexProperty() const
    {
        return GetValueTypeProperty<SharpRuntime::intcs>(std::string(WeightsPerVertexKey));
    }

    void SkinnedMaterialContent::setWeightsPerVertexProperty(std::optional<SharpRuntime::intcs> value)
    {
        SetProperty(std::string(WeightsPerVertexKey), value);
    }

    void SkinnedMaterialContent::DescribeContent(Serialization::Intermediate::ContentTypeDescriptor<SkinnedMaterialContent>& d)
    {
        // A stock material adds no serialized member of its own: every property is a view
        // over the base's OpaqueData and Textures (measured, material/serialize_basic).
        d.BaseType<MaterialContent>();
    }

    const std::string& SkinnedMaterialContent::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }

    // ------------------------------------------------------------------------------------------
    // EffectMaterialContent
    // ------------------------------------------------------------------------------------------

    std::shared_ptr<ExternalReference<Processors::CompiledEffectContent>> EffectMaterialContent::getCompiledEffectProperty() const
    {
        return GetReferenceTypeProperty<ExternalReference<Processors::CompiledEffectContent>>(std::string(CompiledEffectKey));
    }

    void EffectMaterialContent::setCompiledEffectProperty(std::shared_ptr<ExternalReference<Processors::CompiledEffectContent>> value)
    {
        SetProperty(std::string(CompiledEffectKey), value);
    }

    std::shared_ptr<ExternalReference<EffectContent>> EffectMaterialContent::getEffectProperty() const
    {
        return GetReferenceTypeProperty<ExternalReference<EffectContent>>(std::string(EffectKey));
    }

    void EffectMaterialContent::setEffectProperty(std::shared_ptr<ExternalReference<EffectContent>> value)
    {
        SetProperty(std::string(EffectKey), value);
    }

    void EffectMaterialContent::DescribeContent(Serialization::Intermediate::ContentTypeDescriptor<EffectMaterialContent>& d)
    {
        // A stock material adds no serialized member of its own: every property is a view
        // over the base's OpaqueData and Textures (measured, material/serialize_basic).
        d.BaseType<MaterialContent>();
    }

    const std::string& EffectMaterialContent::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }

}
