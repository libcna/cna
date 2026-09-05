// SPDX-License-Identifier: MS-PL
#include "CNA/Content/Pipeline/XnaModelBridge.hpp"

#include <cstring>
#include <map>
#include <vector>

#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/StockMaterials.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/PipelineException.hpp"

namespace CNA::Content::Pipeline
{
    namespace
    {
        namespace Xna = Microsoft::Xna::Framework::Content::Pipeline;
        namespace Graphics = Microsoft::Xna::Framework::Content::Pipeline::Graphics;
        namespace Processors = Microsoft::Xna::Framework::Content::Pipeline::Processors;
        using Microsoft::Xna::Framework::Vector3;

        /** @brief The filename of an external texture reference, or an empty string. */
        [[nodiscard]] std::string ReferenceOf(
            const std::shared_ptr<Xna::ExternalReference<Graphics::TextureContent>>& reference)
        {
            return reference == nullptr ? std::string() : reference->getFilenameProperty();
        }

        /** @brief One stock material as the canonical effect resource it serializes to. */
        [[nodiscard]] Internal::Xnb::XnbModelSharedResourceData EffectResourceOf(
            const Graphics::MaterialContent& material)
        {
            Internal::Xnb::XnbModelSharedResourceData resource;
            if (const auto* basic = dynamic_cast<const Graphics::BasicMaterialContent*>(&material))
            {
                Internal::Xnb::XnbBasicEffectData data;
                data.textureReference = ReferenceOf(basic->getTextureProperty());
                data.diffuseColor = basic->getDiffuseColorProperty().value_or(Vector3(1, 1, 1));
                data.emissiveColor = basic->getEmissiveColorProperty().value_or(Vector3(0, 0, 0));
                data.specularColor = basic->getSpecularColorProperty().value_or(Vector3(1, 1, 1));
                data.specularPower = basic->getSpecularPowerProperty().value_or(16.0f);
                data.alpha = basic->getAlphaProperty().value_or(1.0f);
                data.vertexColorEnabled = basic->getVertexColorEnabledProperty().value_or(false);
                resource.reader = "Microsoft.Xna.Framework.Content.BasicEffectReader";
                resource.value = data;
                return resource;
            }
            if (const auto* alphaTest = dynamic_cast<const Graphics::AlphaTestMaterialContent*>(&material))
            {
                Internal::Xnb::XnbAlphaTestEffectData data;
                data.textureReference = ReferenceOf(alphaTest->getTextureProperty());
                data.alphaFunction = static_cast<std::int32_t>(
                    alphaTest->getAlphaFunctionProperty().value_or(
                        Microsoft::Xna::Framework::Graphics::CompareFunction::Greater));
                data.referenceAlpha =
                    static_cast<std::uint32_t>(alphaTest->getReferenceAlphaProperty().value_or(0));
                data.diffuseColor = alphaTest->getDiffuseColorProperty().value_or(Vector3(1, 1, 1));
                data.alpha = alphaTest->getAlphaProperty().value_or(1.0f);
                data.vertexColorEnabled = alphaTest->getVertexColorEnabledProperty().value_or(false);
                resource.reader = "Microsoft.Xna.Framework.Content.AlphaTestEffectReader";
                resource.value = data;
                return resource;
            }
            if (const auto* dual = dynamic_cast<const Graphics::DualTextureMaterialContent*>(&material))
            {
                Internal::Xnb::XnbDualTextureEffectData data;
                data.textureReference = ReferenceOf(dual->getTextureProperty());
                data.texture2Reference = ReferenceOf(dual->getTexture2Property());
                data.diffuseColor = dual->getDiffuseColorProperty().value_or(Vector3(1, 1, 1));
                data.alpha = dual->getAlphaProperty().value_or(1.0f);
                data.vertexColorEnabled = dual->getVertexColorEnabledProperty().value_or(false);
                resource.reader = "Microsoft.Xna.Framework.Content.DualTextureEffectReader";
                resource.value = data;
                return resource;
            }
            if (const auto* environment =
                    dynamic_cast<const Graphics::EnvironmentMapMaterialContent*>(&material))
            {
                Internal::Xnb::XnbEnvironmentMapEffectData data;
                data.textureReference = ReferenceOf(environment->getTextureProperty());
                data.environmentMapReference = ReferenceOf(environment->getEnvironmentMapProperty());
                data.environmentMapAmount = environment->getEnvironmentMapAmountProperty().value_or(1.0f);
                data.environmentMapSpecular =
                    environment->getEnvironmentMapSpecularProperty().value_or(Vector3(1, 1, 1));
                data.fresnelFactor = environment->getFresnelFactorProperty().value_or(1.0f);
                data.diffuseColor = environment->getDiffuseColorProperty().value_or(Vector3(1, 1, 1));
                data.emissiveColor = environment->getEmissiveColorProperty().value_or(Vector3(0, 0, 0));
                data.alpha = environment->getAlphaProperty().value_or(1.0f);
                resource.reader = "Microsoft.Xna.Framework.Content.EnvironmentMapEffectReader";
                resource.value = data;
                return resource;
            }
            if (const auto* skinned = dynamic_cast<const Graphics::SkinnedMaterialContent*>(&material))
            {
                Internal::Xnb::XnbSkinnedEffectData data;
                data.textureReference = ReferenceOf(skinned->getTextureProperty());
                data.weightsPerVertex =
                    static_cast<std::int32_t>(skinned->getWeightsPerVertexProperty().value_or(4));
                data.diffuseColor = skinned->getDiffuseColorProperty().value_or(Vector3(1, 1, 1));
                data.emissiveColor = skinned->getEmissiveColorProperty().value_or(Vector3(0, 0, 0));
                data.specularColor = skinned->getSpecularColorProperty().value_or(Vector3(1, 1, 1));
                data.specularPower = skinned->getSpecularPowerProperty().value_or(16.0f);
                data.alpha = skinned->getAlphaProperty().value_or(1.0f);
                resource.reader = "Microsoft.Xna.Framework.Content.SkinnedEffectReader";
                resource.value = data;
                return resource;
            }
            throw Xna::PipelineException(
                "ToCanonicalModel: material '{0}' is not one of the five stock materials an XNB "
                "model can carry.",
                material.GetTypeName());
        }
    }

    Internal::Xnb::XnbModelData ToCanonicalModel(const Processors::ModelContent& model)
    {
        Internal::Xnb::XnbModelData data;
        std::map<const Processors::ModelBoneContent*, std::int32_t> boneIndices;
        for (const std::shared_ptr<Processors::ModelBoneContent>& bone : model.getBonesProperty())
        {
            boneIndices.emplace(bone.get(), static_cast<std::int32_t>(boneIndices.size()));
        }
        for (const std::shared_ptr<Processors::ModelBoneContent>& bone : model.getBonesProperty())
        {
            Internal::Xnb::XnbModelBoneData written;
            written.name = bone->getNameProperty();
            written.transform = bone->getTransformProperty();
            const auto parent = boneIndices.find(bone->getParentProperty().get());
            written.parent = parent == boneIndices.end() ? -1 : parent->second;
            for (const std::shared_ptr<Processors::ModelBoneContent>& child : bone->getChildrenProperty())
            {
                const auto found = boneIndices.find(child.get());
                if (found != boneIndices.end())
                {
                    written.children.push_back(found->second);
                }
            }
            data.bones.push_back(std::move(written));
        }
        const auto root = boneIndices.find(model.getRootProperty().get());
        data.rootBone = root == boneIndices.end() ? -1 : root->second;

        // One shared resource per distinct buffer and material, so two parts that share a buffer
        // share its resource, as XNA's own writer does.
        std::map<const void*, std::int32_t> resources;
        const auto resourceOf = [&data, &resources](const void* key,
                                                    const Internal::Xnb::XnbModelSharedResourceData& resource)
        {
            const auto found = resources.find(key);
            if (found != resources.end())
            {
                return found->second;
            }
            const auto index = static_cast<std::int32_t>(data.sharedResources.size());
            data.sharedResources.push_back(resource);
            resources.emplace(key, index);
            return index;
        };
        for (const std::shared_ptr<Processors::ModelMeshContent>& mesh : model.getMeshesProperty())
        {
            Internal::Xnb::XnbModelMeshData written;
            written.name = mesh->getNameProperty();
            const auto bone = boneIndices.find(mesh->getParentBoneProperty().get());
            written.parentBone = bone == boneIndices.end() ? -1 : bone->second;
            written.boundingSphere = mesh->getBoundingSphereProperty();
            for (const std::shared_ptr<Processors::ModelMeshPartContent>& part : mesh->getMeshPartsProperty())
            {
                Internal::Xnb::XnbModelPartData writtenPart;
                writtenPart.vertexOffset = part->getVertexOffsetProperty();
                writtenPart.vertexCount = part->getNumVerticesProperty();
                writtenPart.startIndex = part->getStartIndexProperty();
                writtenPart.primitiveCount = part->getPrimitiveCountProperty();
                const std::shared_ptr<Processors::VertexBufferContent>& buffer = part->getVertexBufferProperty();
                if (buffer == nullptr || buffer->getVertexDeclarationProperty() == nullptr)
                {
                    throw Xna::PipelineException(
                        "ToCanonicalModel: mesh '{0}' has a part with no vertex buffer.",
                        mesh->getNameProperty());
                }
                Internal::Xnb::XnbVertexBufferData vertices;
                const Processors::VertexDeclarationContent& declaration = *buffer->getVertexDeclarationProperty();
                vertices.declaration.stride = declaration.getVertexStrideProperty().value_or(0);
                const auto& elements = declaration.getVertexElementsProperty();
                for (SharpRuntime::intcs i = 0; i < elements.getCountProperty(); ++i)
                {
                    vertices.declaration.elements.push_back(elements[i]);
                }
                vertices.bytes = buffer->getVertexDataProperty();
                vertices.vertexCount =
                    vertices.declaration.stride <= 0
                        ? 0u
                        : static_cast<std::uint32_t>(vertices.bytes.size() /
                                                     static_cast<std::size_t>(vertices.declaration.stride));
                Internal::Xnb::XnbModelSharedResourceData vertexResource;
                vertexResource.reader = "Microsoft.Xna.Framework.Content.VertexBufferReader";
                vertexResource.value = std::move(vertices);
                writtenPart.vertexBufferResource = resourceOf(buffer.get(), vertexResource);

                const std::shared_ptr<Graphics::IndexCollection>& indices = part->getIndexBufferProperty();
                if (indices != nullptr)
                {
                    const auto& reader = static_cast<const System::Collections::ObjectModel::Collection<
                        SharpRuntime::intcs>&>(*indices);
                    bool wide = false;
                    for (SharpRuntime::intcs i = 0; i < reader.getCountProperty(); ++i)
                    {
                        wide = wide || reader[i] > 0xFFFF || reader[i] < 0;
                    }
                    Internal::Xnb::XnbIndexBufferData buffered;
                    // Sixteen bits unless an index does not fit, which is the rule XNA writes by.
                    buffered.indexElementSize = wide ? 4u : 2u;
                    buffered.bytes.resize(static_cast<std::size_t>(reader.getCountProperty()) *
                                          buffered.indexElementSize);
                    for (SharpRuntime::intcs i = 0; i < reader.getCountProperty(); ++i)
                    {
                        const auto value = static_cast<std::uint32_t>(reader[i]);
                        std::uint8_t* at = buffered.bytes.data() +
                                           static_cast<std::size_t>(i) * buffered.indexElementSize;
                        at[0] = static_cast<std::uint8_t>(value & 0xFFu);
                        at[1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
                        if (buffered.indexElementSize == 4u)
                        {
                            at[2] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
                            at[3] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
                        }
                    }
                    Internal::Xnb::XnbModelSharedResourceData indexResource;
                    indexResource.reader = "Microsoft.Xna.Framework.Content.IndexBufferReader";
                    indexResource.value = std::move(buffered);
                    writtenPart.indexBufferResource = resourceOf(indices.get(), indexResource);
                }
                if (part->getMaterialProperty() != nullptr)
                {
                    writtenPart.effectResource = resourceOf(part->getMaterialProperty().get(),
                                                            EffectResourceOf(*part->getMaterialProperty()));
                }
                written.parts.push_back(writtenPart);
            }
            data.meshes.push_back(std::move(written));
        }
        return data;
    }
}
