// SPDX-License-Identifier: MS-PL
#include <algorithm>
#include <any>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "CNA/Content/Pipeline/ModelContentPipeline.hpp"
#include "CNA/Content/Pipeline/XnbOutput.hpp"
#include "CNA/Content/Xnb/XnbAssetTypeWriters.hpp"
#include "CNA/Content/Xnb/XnbWriter.hpp"

namespace CNA::Content::Pipeline
{
    using Cnb::CnbModelV2Data;
    using Cnb::CnbModelV2Effect;
    using Cnb::CnbModelV2EffectKind;
    using CNA::Internal::Xnb::XnbAlphaTestEffectData;
    using CNA::Internal::Xnb::XnbBasicEffectData;
    using CNA::Internal::Xnb::XnbDualTextureEffectData;
    using CNA::Internal::Xnb::XnbEnvironmentMapEffectData;
    using CNA::Internal::Xnb::XnbIndexBufferData;
    using CNA::Internal::Xnb::XnbModelData;
    using CNA::Internal::Xnb::XnbModelSharedResourceData;
    using CNA::Internal::Xnb::XnbSkinnedEffectData;
    using CNA::Internal::Xnb::XnbVertexBufferData;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector3;
    using Xnb::XnbWriteException;

    namespace
    {
        /** @brief Stable build version for the Model writer this file registers. */
        constexpr const char* kXnbModelWriterVersion = "1";

        [[nodiscard]] std::vector<std::string> SplitLogicalName(const std::string& name)
        {
            std::vector<std::string> segments;
            std::string current;
            for (const char character : name)
            {
                if (character == '/')
                {
                    segments.push_back(current);
                    current.clear();
                    continue;
                }
                current.push_back(character);
            }
            segments.push_back(current);
            return segments;
        }

        [[nodiscard]] Vector3 ToVector3(const std::array<float, 3>& value)
        {
            return Vector3(value[0], value[1], value[2]);
        }

        [[nodiscard]] Matrix ToMatrix(const std::array<float, 16>& value)
        {
            Matrix result;
            result.M11 = value[0];  result.M12 = value[1];  result.M13 = value[2];
            result.M14 = value[3];  result.M21 = value[4];  result.M22 = value[5];
            result.M23 = value[6];  result.M24 = value[7];  result.M31 = value[8];
            result.M32 = value[9];  result.M33 = value[10]; result.M34 = value[11];
            result.M41 = value[12]; result.M42 = value[13]; result.M43 = value[14];
            result.M44 = value[15];
            return result;
        }

        [[nodiscard]] XnbModelSharedResourceData ConvertEffect(const CnbModelV2Effect& effect,
                                                                const std::string& logicalName)
        {
            const auto reference = [&logicalName](const std::string& target)
            {
                return XnbRelativeAssetReference(logicalName, target);
            };

            switch (effect.kind)
            {
                case CnbModelV2EffectKind::BasicEffect:
                {
                    XnbBasicEffectData value;
                    value.textureReference = reference(effect.primaryTexture);
                    value.diffuseColor = ToVector3(effect.diffuse);
                    value.emissiveColor = ToVector3(effect.emissive);
                    value.specularColor = ToVector3(effect.specular);
                    value.specularPower = effect.specularPower;
                    value.alpha = effect.alpha;
                    value.vertexColorEnabled = effect.vertexColorEnabled;
                    return {"Microsoft.Xna.Framework.Content.BasicEffectReader", value};
                }
                case CnbModelV2EffectKind::SkinnedEffect:
                {
                    XnbSkinnedEffectData value;
                    value.textureReference = reference(effect.primaryTexture);
                    value.weightsPerVertex = static_cast<std::int32_t>(effect.weightsPerVertex);
                    value.diffuseColor = ToVector3(effect.diffuse);
                    value.emissiveColor = ToVector3(effect.emissive);
                    value.specularColor = ToVector3(effect.specular);
                    value.specularPower = effect.specularPower;
                    value.alpha = effect.alpha;
                    return {"Microsoft.Xna.Framework.Content.SkinnedEffectReader", value};
                }
                case CnbModelV2EffectKind::DualTextureEffect:
                {
                    XnbDualTextureEffectData value;
                    value.textureReference = reference(effect.primaryTexture);
                    value.texture2Reference = reference(effect.secondaryTexture);
                    value.diffuseColor = ToVector3(effect.diffuse);
                    value.alpha = effect.alpha;
                    value.vertexColorEnabled = effect.vertexColorEnabled;
                    return {"Microsoft.Xna.Framework.Content.DualTextureEffectReader", value};
                }
                case CnbModelV2EffectKind::AlphaTestEffect:
                {
                    XnbAlphaTestEffectData value;
                    value.textureReference = reference(effect.primaryTexture);
                    value.alphaFunction = static_cast<std::int32_t>(effect.alphaFunction);
                    value.referenceAlpha = effect.referenceAlpha;
                    value.diffuseColor = ToVector3(effect.diffuse);
                    value.alpha = effect.alpha;
                    value.vertexColorEnabled = effect.vertexColorEnabled;
                    return {"Microsoft.Xna.Framework.Content.AlphaTestEffectReader", value};
                }
                case CnbModelV2EffectKind::EnvironmentMapEffect:
                {
                    XnbEnvironmentMapEffectData value;
                    value.textureReference = reference(effect.primaryTexture);
                    value.environmentMapReference = reference(effect.cubeTexture);
                    value.environmentMapAmount = effect.environmentMapAmount;
                    value.environmentMapSpecular = ToVector3(effect.specular);
                    value.fresnelFactor = effect.fresnelFactor;
                    value.diffuseColor = ToVector3(effect.diffuse);
                    value.emissiveColor = ToVector3(effect.emissive);
                    value.alpha = effect.alpha;
                    return {"Microsoft.Xna.Framework.Content.EnvironmentMapEffectReader", value};
                }
            }
            throw XnbWriteException(
                "XNB Model: stock effect kind " +
                std::to_string(static_cast<std::uint32_t>(effect.kind)) + " is not one XNA 4.0 "
                "defines.");
        }

        /** @brief Serializes a canonical Model bundle, when its carrier is the lossless schema. */
        class ModelXnbAssetWriter final : public XnbAssetWriter
        {
        public:
            [[nodiscard]] ContentComponentIdentity Identity() const override
            {
                return ContentComponentIdentity{"CNA.Xnb.ModelWriter", kXnbModelWriterVersion};
            }

            [[nodiscard]] std::string InputType() const override { return ProcessedModelType; }

            [[nodiscard]] std::string RootReaderName() const override
            {
                return Xnb::XnbQualifiedReaderName("Microsoft.Xna.Framework.Content.ModelReader",
                                                   Xnb::XnaGraphicsAssembly);
            }

            [[nodiscard]] XnbWriteResult Write(const ContentValue& input,
                                                const Xnb::XnbTypeWriterRegistry& registry,
                                                const Xnb::XnbFileOptions& options,
                                                const std::string& logicalName) const override
            {
                const auto& bundle = input.Get<ProcessedModelBundle>();
                const auto* schemaTwo = std::get_if<CnbModelV2Data>(&bundle.primary);
                if (schemaTwo == nullptr)
                {
                    throw XnbWriteException(
                        "XNB Model: this model was processed into the frozen schema-1 carrier, "
                        "which stores a vertex stride but no vertex declaration, and an .xnb "
                        "VertexBuffer requires the full declaration. Only the lossless schema-2 "
                        "carrier can be written as an .xnb Model "
                        "(plans/plan_xnapipeline.md XNAP-022).");
                }
                if (!bundle.children.empty())
                {
                    throw XnbWriteException(
                        "XNB Model: this model generated " +
                        std::to_string(bundle.children.size()) +
                        " child assets, and the .xnb route does not publish generated children "
                        "yet.");
                }

                XnbWriteResult result;
                result.options = options;
                result.rootReaderName = RootReaderName();
                result.bytes = Xnb::WriteXnbFile(
                    registry, options, Xnb::XnbTypeKey<XnbModelData>::Name(),
                    std::any(ConvertCnbModelV2ToXnb(*schemaTwo, logicalName)));
                return result;
            }
        };
    }

    std::string XnbRelativeAssetReference(const std::string& from, const std::string& target)
    {
        if (target.empty()) { return {}; }

        const std::vector<std::string> fromSegments = SplitLogicalName(from);
        const std::vector<std::string> targetSegments = SplitLogicalName(target);
        // The last segment of each is the asset's own name, not a directory.
        const std::size_t fromDepth = fromSegments.empty() ? 0u : fromSegments.size() - 1u;
        const std::size_t targetDepth = targetSegments.empty() ? 0u : targetSegments.size() - 1u;

        std::size_t shared = 0u;
        while (shared < fromDepth && shared < targetDepth &&
               fromSegments[shared] == targetSegments[shared])
        {
            ++shared;
        }

        std::string reference;
        for (std::size_t up = shared; up < fromDepth; ++up) { reference += "../"; }
        for (std::size_t down = shared; down < targetSegments.size(); ++down)
        {
            reference += targetSegments[down];
            if (down + 1u < targetSegments.size()) { reference += "/"; }
        }
        return reference;
    }

    XnbModelData ConvertCnbModelV2ToXnb(const CnbModelV2Data& model,
                                         const std::string& logicalName)
    {
        if (model.bones.empty())
        {
            throw XnbWriteException("XNB Model: a model must declare at least one bone.");
        }
        if (model.rootBone >= model.bones.size())
        {
            throw XnbWriteException(
                "XNB Model: the root bone index " + std::to_string(model.rootBone) +
                " is outside the model's " + std::to_string(model.bones.size()) + " bones.");
        }

        XnbModelData result;
        result.rootBone = static_cast<std::int32_t>(model.rootBone);
        result.bones.reserve(model.bones.size());
        for (const auto& bone : model.bones)
        {
            result.bones.push_back({bone.name, ToMatrix(bone.transform), bone.parent, {}});
        }
        // Schema 2 records only each bone's parent; the wire form also lists each bone's children.
        // They are derived in ascending index order, which is the order the import path validated
        // on the way in, so the two representations stay exact inverses.
        for (std::size_t index = 0u; index < result.bones.size(); ++index)
        {
            const std::int32_t parent = result.bones[index].parent;
            if (parent < 0) { continue; }
            if (static_cast<std::size_t>(parent) >= result.bones.size())
            {
                throw XnbWriteException(
                    "XNB Model: bone " + std::to_string(index) + " names parent " +
                    std::to_string(parent) + ", which does not exist.");
            }
            result.bones[static_cast<std::size_t>(parent)].children.push_back(
                static_cast<std::int32_t>(index));
        }

        // One flat shared-resource list in a fixed order, so the mapping is a pure function of
        // the input: every vertex buffer, then every index buffer, then every effect.
        const std::size_t indexBufferBase = model.vertexBuffers.size();
        const std::size_t effectBase = indexBufferBase + model.indexBuffers.size();
        result.sharedResources.reserve(effectBase + model.effects.size());

        for (const auto& buffer : model.vertexBuffers)
        {
            if (buffer.declaration >= model.vertexDeclarations.size())
            {
                throw XnbWriteException(
                    "XNB Model: a vertex buffer names declaration " +
                    std::to_string(buffer.declaration) + ", which does not exist.");
            }
            const auto& declaration = model.vertexDeclarations[buffer.declaration];
            XnbVertexBufferData value;
            value.declaration.stride = static_cast<std::int32_t>(declaration.vertexStride);
            value.declaration.elements.reserve(declaration.elements.size());
            for (const auto& element : declaration.elements)
            {
                value.declaration.elements.emplace_back(
                    static_cast<int>(element.offset),
                    static_cast<Microsoft::Xna::Framework::Graphics::VertexElementFormat>(
                        element.format),
                    static_cast<Microsoft::Xna::Framework::Graphics::VertexElementUsage>(
                        element.usage),
                    static_cast<int>(element.usageIndex));
            }
            value.vertexCount = buffer.vertexCount;
            value.bytes = buffer.bytes;
            result.sharedResources.push_back(
                {"Microsoft.Xna.Framework.Content.VertexBufferReader", std::move(value)});
        }

        for (const auto& buffer : model.indexBuffers)
        {
            XnbIndexBufferData value;
            value.indexElementSize = buffer.indexElementSize;
            value.bytes = buffer.bytes;
            result.sharedResources.push_back(
                {"Microsoft.Xna.Framework.Content.IndexBufferReader", std::move(value)});
        }

        for (const auto& effect : model.effects)
        {
            result.sharedResources.push_back(ConvertEffect(effect, logicalName));
        }

        result.meshes.reserve(model.meshes.size());
        for (const auto& mesh : model.meshes)
        {
            CNA::Internal::Xnb::XnbModelMeshData converted;
            converted.name = mesh.name;
            converted.parentBone = mesh.parentBone;
            converted.boundingSphere.Center =
                Vector3(mesh.boundingSphere[0], mesh.boundingSphere[1], mesh.boundingSphere[2]);
            converted.boundingSphere.Radius = mesh.boundingSphere[3];
            converted.parts.reserve(mesh.parts.size());
            for (const auto& part : mesh.parts)
            {
                if (part.vertexBuffer >= model.vertexBuffers.size() ||
                    part.indexBuffer >= model.indexBuffers.size() ||
                    part.effect >= model.effects.size())
                {
                    throw XnbWriteException(
                        "XNB Model: mesh '" + mesh.name +
                        "' has a part naming a shared resource that does not exist.");
                }
                converted.parts.push_back(
                    {static_cast<std::int32_t>(part.vertexOffset),
                     static_cast<std::int32_t>(part.numVertices),
                     static_cast<std::int32_t>(part.startIndex),
                     static_cast<std::int32_t>(part.primitiveCount),
                     static_cast<std::int32_t>(part.vertexBuffer),
                     static_cast<std::int32_t>(indexBufferBase + part.indexBuffer),
                     static_cast<std::int32_t>(effectBase + part.effect)});
            }
            result.meshes.push_back(std::move(converted));
        }
        return result;
    }

    void RegisterXnbModelAssetWriter(ContentPipelineRegistry& registry)
    {
        registry.RegisterXnbWriter(std::make_shared<const ModelXnbAssetWriter>());
    }
}
