// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/EffectContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/MaterialContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/CompiledEffectContent.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Graphics
{
    /**
     * @brief Provides properties for maintaining a BasicEffect material: the stock material a
     *        model uses unless it names another.
     */
    class BasicMaterialContent : public MaterialContent
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.BasicMaterialContent";

        /** @brief Opaque-data key of the Alpha property. */
        static constexpr std::string_view AlphaKey = "Alpha";
        /** @brief Opaque-data key of the DiffuseColor property. */
        static constexpr std::string_view DiffuseColorKey = "DiffuseColor";
        /** @brief Opaque-data key of the EmissiveColor property. */
        static constexpr std::string_view EmissiveColorKey = "EmissiveColor";
        /** @brief Opaque-data key of the SpecularColor property. */
        static constexpr std::string_view SpecularColorKey = "SpecularColor";
        /** @brief Opaque-data key of the SpecularPower property. */
        static constexpr std::string_view SpecularPowerKey = "SpecularPower";
        /** @brief Texture-collection key of the Texture property. */
        static constexpr std::string_view TextureKey = "Texture";
        /** @brief Opaque-data key of the VertexColorEnabled property. */
        static constexpr std::string_view VertexColorEnabledKey = "VertexColorEnabled";

        /** @brief Initializes a new instance of BasicMaterialContent. */
        BasicMaterialContent() = default;

        /**
         * @brief Gets the alpha property.
         * @return The alpha, or an empty optional when the material does not set it.
         */
        [[nodiscard]] std::optional<SharpRuntime::Single> getAlphaProperty() const;
        /**
         * @brief Sets the alpha property.
         * @param value The alpha; an empty optional removes it.
         */
        void setAlphaProperty(std::optional<SharpRuntime::Single> value);

        /**
         * @brief Gets the diffuse color property.
         * @return The diffuse color, or an empty optional when the material does not set it.
         */
        [[nodiscard]] std::optional<Vector3> getDiffuseColorProperty() const;
        /**
         * @brief Sets the diffuse color property.
         * @param value The diffuse color; an empty optional removes it.
         */
        void setDiffuseColorProperty(std::optional<Vector3> value);

        /**
         * @brief Gets the emissive color property.
         * @return The emissive color, or an empty optional when the material does not set it.
         */
        [[nodiscard]] std::optional<Vector3> getEmissiveColorProperty() const;
        /**
         * @brief Sets the emissive color property.
         * @param value The emissive color; an empty optional removes it.
         */
        void setEmissiveColorProperty(std::optional<Vector3> value);

        /**
         * @brief Gets the specular color property.
         * @return The specular color, or an empty optional when the material does not set it.
         */
        [[nodiscard]] std::optional<Vector3> getSpecularColorProperty() const;
        /**
         * @brief Sets the specular color property.
         * @param value The specular color; an empty optional removes it.
         */
        void setSpecularColorProperty(std::optional<Vector3> value);

        /**
         * @brief Gets the specular power property.
         * @return The specular power, or an empty optional when the material does not set it.
         */
        [[nodiscard]] std::optional<SharpRuntime::Single> getSpecularPowerProperty() const;
        /**
         * @brief Sets the specular power property.
         * @param value The specular power; an empty optional removes it.
         */
        void setSpecularPowerProperty(std::optional<SharpRuntime::Single> value);

        /**
         * @brief Gets the diffuse texture reference.
         * @return The texture reference, or null when the material has none.
         */
        [[nodiscard]] std::shared_ptr<ExternalReference<TextureContent>> getTextureProperty() const;
        /**
         * @brief Sets the diffuse texture reference.
         * @param value The texture reference; null removes it.
         */
        void setTextureProperty(std::shared_ptr<ExternalReference<TextureContent>> value);

        /**
         * @brief Gets whether vertex color is enabled.
         * @return The flag, or an empty optional when the material does not set it.
         */
        [[nodiscard]] std::optional<bool> getVertexColorEnabledProperty() const;
        /**
         * @brief Sets whether vertex color is enabled.
         * @param value The flag; an empty optional removes it.
         */
        void setVertexColorEnabledProperty(std::optional<bool> value);

        /**
         * @brief Describes the material for the intermediate serializer: it adds no member of its
         *        own, because every property is a view over the base's two dictionaries.
         *
         * @param d The descriptor being filled.
         */
        static void DescribeContent(Serialization::Intermediate::ContentTypeDescriptor<BasicMaterialContent>& d);

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;
    };

    /**
     * @brief Provides properties for maintaining an AlphaTestEffect material.
     */
    class AlphaTestMaterialContent : public MaterialContent
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.AlphaTestMaterialContent";

        /** @brief Opaque-data key of the AlphaFunction property. */
        static constexpr std::string_view AlphaFunctionKey = "AlphaFunction";
        /** @brief Opaque-data key of the Alpha property. */
        static constexpr std::string_view AlphaKey = "Alpha";
        /** @brief Opaque-data key of the DiffuseColor property. */
        static constexpr std::string_view DiffuseColorKey = "DiffuseColor";
        /** @brief Opaque-data key of the ReferenceAlpha property. */
        static constexpr std::string_view ReferenceAlphaKey = "ReferenceAlpha";
        /** @brief Texture-collection key of the Texture property. */
        static constexpr std::string_view TextureKey = "Texture";
        /** @brief Opaque-data key of the VertexColorEnabled property. */
        static constexpr std::string_view VertexColorEnabledKey = "VertexColorEnabled";

        /** @brief Initializes a new instance of AlphaTestMaterialContent. */
        AlphaTestMaterialContent() = default;

        /**
         * @brief Gets the alpha property.
         * @return The alpha, or an empty optional when the material does not set it.
         */
        [[nodiscard]] std::optional<SharpRuntime::Single> getAlphaProperty() const;
        /**
         * @brief Sets the alpha property.
         * @param value The alpha; an empty optional removes it.
         */
        void setAlphaProperty(std::optional<SharpRuntime::Single> value);

        /**
         * @brief Gets the alpha comparison function.
         * @return The function, or an empty optional when the material does not set it.
         */
        [[nodiscard]] std::optional<Microsoft::Xna::Framework::Graphics::CompareFunction> getAlphaFunctionProperty()
            const;
        /**
         * @brief Sets the alpha comparison function.
         * @param value The function; an empty optional removes it.
         */
        void setAlphaFunctionProperty(std::optional<Microsoft::Xna::Framework::Graphics::CompareFunction> value);

        /**
         * @brief Gets the diffuse color property.
         * @return The diffuse color, or an empty optional when the material does not set it.
         */
        [[nodiscard]] std::optional<Vector3> getDiffuseColorProperty() const;
        /**
         * @brief Sets the diffuse color property.
         * @param value The diffuse color; an empty optional removes it.
         */
        void setDiffuseColorProperty(std::optional<Vector3> value);

        /**
         * @brief Gets the reference alpha the comparison uses.
         * @return The reference alpha, or an empty optional when the material does not set it.
         */
        [[nodiscard]] std::optional<SharpRuntime::intcs> getReferenceAlphaProperty() const;
        /**
         * @brief Sets the reference alpha the comparison uses.
         * @param value The reference alpha; an empty optional removes it.
         */
        void setReferenceAlphaProperty(std::optional<SharpRuntime::intcs> value);

        /**
         * @brief Gets the diffuse texture reference.
         * @return The texture reference, or null when the material has none.
         */
        [[nodiscard]] std::shared_ptr<ExternalReference<TextureContent>> getTextureProperty() const;
        /**
         * @brief Sets the diffuse texture reference.
         * @param value The texture reference; null removes it.
         */
        void setTextureProperty(std::shared_ptr<ExternalReference<TextureContent>> value);

        /**
         * @brief Gets whether vertex color is enabled.
         * @return The flag, or an empty optional when the material does not set it.
         */
        [[nodiscard]] std::optional<bool> getVertexColorEnabledProperty() const;
        /**
         * @brief Sets whether vertex color is enabled.
         * @param value The flag; an empty optional removes it.
         */
        void setVertexColorEnabledProperty(std::optional<bool> value);

        /**
         * @brief Describes the material for the intermediate serializer.
         * @param d The descriptor being filled.
         */
        static void DescribeContent(Serialization::Intermediate::ContentTypeDescriptor<AlphaTestMaterialContent>& d);

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;
    };

    /**
     * @brief Provides properties for maintaining a DualTextureEffect material.
     */
    class DualTextureMaterialContent : public MaterialContent
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.DualTextureMaterialContent";

        /** @brief Opaque-data key of the Alpha property. */
        static constexpr std::string_view AlphaKey = "Alpha";
        /** @brief Opaque-data key of the DiffuseColor property. */
        static constexpr std::string_view DiffuseColorKey = "DiffuseColor";
        /** @brief Texture-collection key of the Texture2 property. */
        static constexpr std::string_view Texture2Key = "Texture2";
        /** @brief Texture-collection key of the Texture property. */
        static constexpr std::string_view TextureKey = "Texture";
        /** @brief Opaque-data key of the VertexColorEnabled property. */
        static constexpr std::string_view VertexColorEnabledKey = "VertexColorEnabled";

        /** @brief Initializes a new instance of DualTextureMaterialContent. */
        DualTextureMaterialContent() = default;

        /**
         * @brief Gets the alpha property.
         * @return The alpha, or an empty optional when the material does not set it.
         */
        [[nodiscard]] std::optional<SharpRuntime::Single> getAlphaProperty() const;
        /**
         * @brief Sets the alpha property.
         * @param value The alpha; an empty optional removes it.
         */
        void setAlphaProperty(std::optional<SharpRuntime::Single> value);

        /**
         * @brief Gets the diffuse color property.
         * @return The diffuse color, or an empty optional when the material does not set it.
         */
        [[nodiscard]] std::optional<Vector3> getDiffuseColorProperty() const;
        /**
         * @brief Sets the diffuse color property.
         * @param value The diffuse color; an empty optional removes it.
         */
        void setDiffuseColorProperty(std::optional<Vector3> value);

        /**
         * @brief Gets the first texture reference.
         * @return The texture reference, or null when the material has none.
         */
        [[nodiscard]] std::shared_ptr<ExternalReference<TextureContent>> getTextureProperty() const;
        /**
         * @brief Sets the first texture reference.
         * @param value The texture reference; null removes it.
         */
        void setTextureProperty(std::shared_ptr<ExternalReference<TextureContent>> value);

        /**
         * @brief Gets the second texture reference.
         * @return The texture reference, or null when the material has none.
         */
        [[nodiscard]] std::shared_ptr<ExternalReference<TextureContent>> getTexture2Property() const;
        /**
         * @brief Sets the second texture reference.
         * @param value The texture reference; null removes it.
         */
        void setTexture2Property(std::shared_ptr<ExternalReference<TextureContent>> value);

        /**
         * @brief Gets whether vertex color is enabled.
         * @return The flag, or an empty optional when the material does not set it.
         */
        [[nodiscard]] std::optional<bool> getVertexColorEnabledProperty() const;
        /**
         * @brief Sets whether vertex color is enabled.
         * @param value The flag; an empty optional removes it.
         */
        void setVertexColorEnabledProperty(std::optional<bool> value);

        /**
         * @brief Describes the material for the intermediate serializer.
         * @param d The descriptor being filled.
         */
        static void DescribeContent(Serialization::Intermediate::ContentTypeDescriptor<DualTextureMaterialContent>& d);

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;
    };

    /**
     * @brief Provides properties for maintaining an EnvironmentMapEffect material.
     */
    class EnvironmentMapMaterialContent : public MaterialContent
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.EnvironmentMapMaterialContent";

        /** @brief Opaque-data key of the Alpha property. */
        static constexpr std::string_view AlphaKey = "Alpha";
        /** @brief Opaque-data key of the DiffuseColor property. */
        static constexpr std::string_view DiffuseColorKey = "DiffuseColor";
        /** @brief Opaque-data key of the EmissiveColor property. */
        static constexpr std::string_view EmissiveColorKey = "EmissiveColor";
        /** @brief Opaque-data key of the EnvironmentMapAmount property. */
        static constexpr std::string_view EnvironmentMapAmountKey = "EnvironmentMapAmount";
        /** @brief Texture-collection key of the EnvironmentMap property. */
        static constexpr std::string_view EnvironmentMapKey = "EnvironmentMap";
        /** @brief Opaque-data key of the EnvironmentMapSpecular property. */
        static constexpr std::string_view EnvironmentMapSpecularKey = "EnvironmentMapSpecular";
        /** @brief Opaque-data key of the FresnelFactor property. */
        static constexpr std::string_view FresnelFactorKey = "FresnelFactor";
        /** @brief Texture-collection key of the Texture property. */
        static constexpr std::string_view TextureKey = "Texture";

        /** @brief Initializes a new instance of EnvironmentMapMaterialContent. */
        EnvironmentMapMaterialContent() = default;

        /**
         * @brief Gets the alpha property.
         * @return The alpha, or an empty optional when the material does not set it.
         */
        [[nodiscard]] std::optional<SharpRuntime::Single> getAlphaProperty() const;
        /**
         * @brief Sets the alpha property.
         * @param value The alpha; an empty optional removes it.
         */
        void setAlphaProperty(std::optional<SharpRuntime::Single> value);

        /**
         * @brief Gets the diffuse color property.
         * @return The diffuse color, or an empty optional when the material does not set it.
         */
        [[nodiscard]] std::optional<Vector3> getDiffuseColorProperty() const;
        /**
         * @brief Sets the diffuse color property.
         * @param value The diffuse color; an empty optional removes it.
         */
        void setDiffuseColorProperty(std::optional<Vector3> value);

        /**
         * @brief Gets the emissive color property.
         * @return The emissive color, or an empty optional when the material does not set it.
         */
        [[nodiscard]] std::optional<Vector3> getEmissiveColorProperty() const;
        /**
         * @brief Sets the emissive color property.
         * @param value The emissive color; an empty optional removes it.
         */
        void setEmissiveColorProperty(std::optional<Vector3> value);

        /**
         * @brief Gets the environment map reference.
         * @return The texture reference, or null when the material has none.
         */
        [[nodiscard]] std::shared_ptr<ExternalReference<TextureContent>> getEnvironmentMapProperty() const;
        /**
         * @brief Sets the environment map reference.
         * @param value The texture reference; null removes it.
         */
        void setEnvironmentMapProperty(std::shared_ptr<ExternalReference<TextureContent>> value);

        /**
         * @brief Gets how much of the environment map shows through.
         * @return The amount, or an empty optional when the material does not set it.
         */
        [[nodiscard]] std::optional<SharpRuntime::Single> getEnvironmentMapAmountProperty() const;
        /**
         * @brief Sets how much of the environment map shows through.
         * @param value The amount; an empty optional removes it.
         */
        void setEnvironmentMapAmountProperty(std::optional<SharpRuntime::Single> value);

        /**
         * @brief Gets the environment map specular color.
         * @return The color, or an empty optional when the material does not set it.
         */
        [[nodiscard]] std::optional<Vector3> getEnvironmentMapSpecularProperty() const;
        /**
         * @brief Sets the environment map specular color.
         * @param value The color; an empty optional removes it.
         */
        void setEnvironmentMapSpecularProperty(std::optional<Vector3> value);

        /**
         * @brief Gets the Fresnel factor of the environment map.
         * @return The factor, or an empty optional when the material does not set it.
         */
        [[nodiscard]] std::optional<SharpRuntime::Single> getFresnelFactorProperty() const;
        /**
         * @brief Sets the Fresnel factor of the environment map.
         * @param value The factor; an empty optional removes it.
         */
        void setFresnelFactorProperty(std::optional<SharpRuntime::Single> value);

        /**
         * @brief Gets the diffuse texture reference.
         * @return The texture reference, or null when the material has none.
         */
        [[nodiscard]] std::shared_ptr<ExternalReference<TextureContent>> getTextureProperty() const;
        /**
         * @brief Sets the diffuse texture reference.
         * @param value The texture reference; null removes it.
         */
        void setTextureProperty(std::shared_ptr<ExternalReference<TextureContent>> value);

        /**
         * @brief Describes the material for the intermediate serializer.
         * @param d The descriptor being filled.
         */
        static void DescribeContent(
            Serialization::Intermediate::ContentTypeDescriptor<EnvironmentMapMaterialContent>& d);

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;
    };

    /**
     * @brief Provides properties for maintaining a SkinnedEffect material.
     */
    class SkinnedMaterialContent : public MaterialContent
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.SkinnedMaterialContent";

        /** @brief Opaque-data key of the Alpha property. */
        static constexpr std::string_view AlphaKey = "Alpha";
        /** @brief Opaque-data key of the DiffuseColor property. */
        static constexpr std::string_view DiffuseColorKey = "DiffuseColor";
        /** @brief Opaque-data key of the EmissiveColor property. */
        static constexpr std::string_view EmissiveColorKey = "EmissiveColor";
        /** @brief Opaque-data key of the SpecularColor property. */
        static constexpr std::string_view SpecularColorKey = "SpecularColor";
        /** @brief Opaque-data key of the SpecularPower property. */
        static constexpr std::string_view SpecularPowerKey = "SpecularPower";
        /** @brief Texture-collection key of the Texture property. */
        static constexpr std::string_view TextureKey = "Texture";
        /** @brief Opaque-data key of the WeightsPerVertex property. */
        static constexpr std::string_view WeightsPerVertexKey = "WeightsPerVertex";

        /** @brief Initializes a new instance of SkinnedMaterialContent. */
        SkinnedMaterialContent() = default;

        /**
         * @brief Gets the alpha property.
         * @return The alpha, or an empty optional when the material does not set it.
         */
        [[nodiscard]] std::optional<SharpRuntime::Single> getAlphaProperty() const;
        /**
         * @brief Sets the alpha property.
         * @param value The alpha; an empty optional removes it.
         */
        void setAlphaProperty(std::optional<SharpRuntime::Single> value);

        /**
         * @brief Gets the diffuse color property.
         * @return The diffuse color, or an empty optional when the material does not set it.
         */
        [[nodiscard]] std::optional<Vector3> getDiffuseColorProperty() const;
        /**
         * @brief Sets the diffuse color property.
         * @param value The diffuse color; an empty optional removes it.
         */
        void setDiffuseColorProperty(std::optional<Vector3> value);

        /**
         * @brief Gets the emissive color property.
         * @return The emissive color, or an empty optional when the material does not set it.
         */
        [[nodiscard]] std::optional<Vector3> getEmissiveColorProperty() const;
        /**
         * @brief Sets the emissive color property.
         * @param value The emissive color; an empty optional removes it.
         */
        void setEmissiveColorProperty(std::optional<Vector3> value);

        /**
         * @brief Gets the specular color property.
         * @return The specular color, or an empty optional when the material does not set it.
         */
        [[nodiscard]] std::optional<Vector3> getSpecularColorProperty() const;
        /**
         * @brief Sets the specular color property.
         * @param value The specular color; an empty optional removes it.
         */
        void setSpecularColorProperty(std::optional<Vector3> value);

        /**
         * @brief Gets the specular power property.
         * @return The specular power, or an empty optional when the material does not set it.
         */
        [[nodiscard]] std::optional<SharpRuntime::Single> getSpecularPowerProperty() const;
        /**
         * @brief Sets the specular power property.
         * @param value The specular power; an empty optional removes it.
         */
        void setSpecularPowerProperty(std::optional<SharpRuntime::Single> value);

        /**
         * @brief Gets the diffuse texture reference.
         * @return The texture reference, or null when the material has none.
         */
        [[nodiscard]] std::shared_ptr<ExternalReference<TextureContent>> getTextureProperty() const;
        /**
         * @brief Sets the diffuse texture reference.
         * @param value The texture reference; null removes it.
         */
        void setTextureProperty(std::shared_ptr<ExternalReference<TextureContent>> value);

        /**
         * @brief Gets how many bone weights each vertex carries.
         * @return The count, or an empty optional when the material does not set it.
         */
        [[nodiscard]] std::optional<SharpRuntime::intcs> getWeightsPerVertexProperty() const;
        /**
         * @brief Sets how many bone weights each vertex carries.
         * @param value The count; an empty optional removes it.
         */
        void setWeightsPerVertexProperty(std::optional<SharpRuntime::intcs> value);

        /**
         * @brief Describes the material for the intermediate serializer.
         * @param d The descriptor being filled.
         */
        static void DescribeContent(Serialization::Intermediate::ContentTypeDescriptor<SkinnedMaterialContent>& d);

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;
    };

    /**
     * @brief Provides properties for maintaining a material based on a game's own effect.
     */
    class EffectMaterialContent : public MaterialContent
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.EffectMaterialContent";

        /** @brief Opaque-data key of the CompiledEffect property. */
        static constexpr std::string_view CompiledEffectKey = "CompiledEffect";
        /** @brief Opaque-data key of the Effect property. */
        static constexpr std::string_view EffectKey = "Effect";

        /** @brief Initializes a new instance of EffectMaterialContent. */
        EffectMaterialContent() = default;

        /**
         * @brief Gets the reference to the compiled effect this material uses.
         * @return The reference, or null when the material has none.
         */
        [[nodiscard]] std::shared_ptr<ExternalReference<Processors::CompiledEffectContent>> getCompiledEffectProperty()
            const;
        /**
         * @brief Sets the reference to the compiled effect this material uses.
         * @param value The reference; null removes it.
         */
        void setCompiledEffectProperty(std::shared_ptr<ExternalReference<Processors::CompiledEffectContent>> value);

        /**
         * @brief Gets the reference to the effect source this material uses.
         * @return The reference, or null when the material has none.
         */
        [[nodiscard]] std::shared_ptr<ExternalReference<EffectContent>> getEffectProperty() const;
        /**
         * @brief Sets the reference to the effect source this material uses.
         * @param value The reference; null removes it.
         */
        void setEffectProperty(std::shared_ptr<ExternalReference<EffectContent>> value);

        /**
         * @brief Describes the material for the intermediate serializer.
         * @param d The descriptor being filled.
         */
        static void DescribeContent(Serialization::Intermediate::ContentTypeDescriptor<EffectMaterialContent>& d);

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;
    };
}

// AlphaTestMaterialContent stores its comparison as a framework enumeration, so the intermediate
// serializer needs its .NET name and member names (the values match XNA's own order).
CNA_XNA_CONTENT_ENUM(Microsoft::Xna::Framework::Graphics::CompareFunction,
                     "Microsoft.Xna.Framework.Graphics.CompareFunction", false,
                     {Microsoft::Xna::Framework::Graphics::CompareFunction::Always, "Always"},
                     {Microsoft::Xna::Framework::Graphics::CompareFunction::Never, "Never"},
                     {Microsoft::Xna::Framework::Graphics::CompareFunction::Less, "Less"},
                     {Microsoft::Xna::Framework::Graphics::CompareFunction::LessEqual, "LessEqual"},
                     {Microsoft::Xna::Framework::Graphics::CompareFunction::Equal, "Equal"},
                     {Microsoft::Xna::Framework::Graphics::CompareFunction::GreaterEqual, "GreaterEqual"},
                     {Microsoft::Xna::Framework::Graphics::CompareFunction::Greater, "Greater"},
                     {Microsoft::Xna::Framework::Graphics::CompareFunction::NotEqual, "NotEqual"});
