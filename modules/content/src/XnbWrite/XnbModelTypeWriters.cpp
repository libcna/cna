// SPDX-License-Identifier: MS-PL
#include <any>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "CNA/Content/Xnb/XnbAssetTypeWriters.hpp"
#include "CNA/Content/Xnb/XnbBuiltInTypeWriters.hpp"
#include "CNA/Content/Xnb/XnbWriter.hpp"

namespace CNA::Content::Xnb
{
    using CNA::Internal::Xnb::XnbAlphaTestEffectData;
    using CNA::Internal::Xnb::XnbBasicEffectData;
    using CNA::Internal::Xnb::XnbDualTextureEffectData;
    using CNA::Internal::Xnb::XnbEnvironmentMapEffectData;
    using CNA::Internal::Xnb::XnbIndexBufferData;
    using CNA::Internal::Xnb::XnbModelBoneData;
    using CNA::Internal::Xnb::XnbModelData;
    using CNA::Internal::Xnb::XnbModelMeshData;
    using CNA::Internal::Xnb::XnbModelPartData;
    using CNA::Internal::Xnb::XnbModelSharedResourceData;
    using CNA::Internal::Xnb::XnbSkinnedEffectData;
    using CNA::Internal::Xnb::XnbVertexBufferData;
    using CNA::Internal::Xnb::XnbVertexDeclarationData;
    using Microsoft::Xna::Framework::Vector3;

    namespace
    {
        /** @brief A writer whose payload is one emit function, closed over its declared identity. */
        template <typename T>
        class ModelPartWriter final : public XnbTypeWriterT<T>
        {
        public:
            using Emit = void (*)(XnbWriter&, const T&);

            ModelPartWriter(std::string readerName, const bool valueType, const Emit emit)
                : readerName_(std::move(readerName)), valueType_(valueType), emit_(emit)
            {
            }

            [[nodiscard]] std::string TargetTypeName() const override
            {
                return XnbTypeKey<T>::Name();
            }

            [[nodiscard]] std::string RuntimeReaderName() const override
            {
                // Every reader in this file lives in Microsoft.Xna.Framework.Graphics, so each
                // must carry that assembly for a real XNA runtime to resolve it.
                return XnbQualifiedReaderName(readerName_, XnaGraphicsAssembly);
            }

            [[nodiscard]] bool IsValueType() const override { return valueType_; }

            void Write(XnbWriter& output, const T& value) const override { emit_(output, value); }

        private:
            std::string readerName_;
            bool valueType_ = false;
            Emit emit_ = nullptr;
        };

        template <typename T>
        void Register(XnbTypeWriterRegistry& registry, const char* readerSuffix,
                      const bool valueType, void (*emit)(XnbWriter&, const T&))
        {
            registry.Register(std::make_shared<const ModelPartWriter<T>>(
                std::string("Microsoft.Xna.Framework.Content.") + readerSuffix, valueType, emit));
        }

        void EmitVector3(XnbWriter& output, const Vector3& value)
        {
            output.WriteSingle(value.X);
            output.WriteSingle(value.Y);
            output.WriteSingle(value.Z);
        }

        void EmitVertexDeclaration(XnbWriter& output, const XnbVertexDeclarationData& value)
        {
            if (value.stride <= 0)
            {
                throw XnbWriteException(
                    "XNB VertexDeclaration: the vertex stride must be positive, not " +
                    std::to_string(value.stride) + ".");
            }
            output.WriteUInt32(static_cast<std::uint32_t>(value.stride));
            output.WriteCollectionCount(value.elements.size(),
                                        "Microsoft.Xna.Framework.Graphics.VertexDeclaration");
            for (const auto& element : value.elements)
            {
                const int offset = element.getOffsetProperty();
                if (offset < 0 || offset >= value.stride)
                {
                    throw XnbWriteException(
                        "XNB VertexDeclaration: element offset " + std::to_string(offset) +
                        " lies outside the " + std::to_string(value.stride) + "-byte stride.");
                }
                output.WriteUInt32(static_cast<std::uint32_t>(offset));
                output.WriteInt32(
                    static_cast<std::int32_t>(element.getVertexElementFormatProperty()));
                output.WriteInt32(
                    static_cast<std::int32_t>(element.getVertexElementUsageProperty()));
                output.WriteUInt32(static_cast<std::uint32_t>(element.getUsageIndexProperty()));
            }
        }

        void EmitVertexBuffer(XnbWriter& output, const XnbVertexBufferData& value)
        {
            const std::int64_t expected = XnbCheckedMultiply(
                {static_cast<std::int64_t>(value.vertexCount), value.declaration.stride},
                "XNB VertexBuffer");
            if (static_cast<std::size_t>(expected) != value.bytes.size())
            {
                throw XnbWriteException(
                    "XNB VertexBuffer: " + std::to_string(value.vertexCount) + " vertices of " +
                    std::to_string(value.declaration.stride) + " bytes needs " +
                    std::to_string(expected) + " payload bytes, but " +
                    std::to_string(value.bytes.size()) + " are present.");
            }
            // The declaration is a raw value, not a dispatched object: XNA writes it inline.
            EmitVertexDeclaration(output, value.declaration);
            output.WriteUInt32(value.vertexCount);
            output.WriteBytes(value.bytes);
        }

        void EmitIndexBuffer(XnbWriter& output, const XnbIndexBufferData& value)
        {
            if (value.indexElementSize != 2u && value.indexElementSize != 4u)
            {
                throw XnbWriteException(
                    "XNB IndexBuffer: an index is 2 or 4 bytes, not " +
                    std::to_string(value.indexElementSize) + ".");
            }
            if (value.bytes.size() % value.indexElementSize != 0u)
            {
                throw XnbWriteException(
                    "XNB IndexBuffer: " + std::to_string(value.bytes.size()) +
                    " bytes is not a whole number of " +
                    std::to_string(value.indexElementSize) + "-byte indices.");
            }
            output.WriteBoolean(value.indexElementSize == 2u);
            output.WriteUInt32(static_cast<std::uint32_t>(value.bytes.size()));
            output.WriteBytes(value.bytes);
        }

        void EmitBasicEffect(XnbWriter& output, const XnbBasicEffectData& value)
        {
            output.WriteExternalReference(value.textureReference);
            EmitVector3(output, value.diffuseColor);
            EmitVector3(output, value.emissiveColor);
            EmitVector3(output, value.specularColor);
            output.WriteSingle(value.specularPower);
            output.WriteSingle(value.alpha);
            output.WriteBoolean(value.vertexColorEnabled);
        }

        void EmitAlphaTestEffect(XnbWriter& output, const XnbAlphaTestEffectData& value)
        {
            output.WriteExternalReference(value.textureReference);
            output.WriteInt32(value.alphaFunction);
            output.WriteUInt32(value.referenceAlpha);
            EmitVector3(output, value.diffuseColor);
            output.WriteSingle(value.alpha);
            output.WriteBoolean(value.vertexColorEnabled);
        }

        void EmitDualTextureEffect(XnbWriter& output, const XnbDualTextureEffectData& value)
        {
            output.WriteExternalReference(value.textureReference);
            output.WriteExternalReference(value.texture2Reference);
            EmitVector3(output, value.diffuseColor);
            output.WriteSingle(value.alpha);
            output.WriteBoolean(value.vertexColorEnabled);
        }

        void EmitEnvironmentMapEffect(XnbWriter& output, const XnbEnvironmentMapEffectData& value)
        {
            output.WriteExternalReference(value.textureReference);
            output.WriteExternalReference(value.environmentMapReference);
            output.WriteSingle(value.environmentMapAmount);
            EmitVector3(output, value.environmentMapSpecular);
            output.WriteSingle(value.fresnelFactor);
            EmitVector3(output, value.diffuseColor);
            EmitVector3(output, value.emissiveColor);
            output.WriteSingle(value.alpha);
        }

        void EmitSkinnedEffect(XnbWriter& output, const XnbSkinnedEffectData& value)
        {
            if (value.weightsPerVertex != 1 && value.weightsPerVertex != 2 &&
                value.weightsPerVertex != 4)
            {
                throw XnbWriteException(
                    "XNB SkinnedEffect: weights per vertex must be 1, 2 or 4, not " +
                    std::to_string(value.weightsPerVertex) + ".");
            }
            output.WriteExternalReference(value.textureReference);
            output.WriteUInt32(static_cast<std::uint32_t>(value.weightsPerVertex));
            EmitVector3(output, value.diffuseColor);
            EmitVector3(output, value.emissiveColor);
            EmitVector3(output, value.specularColor);
            output.WriteSingle(value.specularPower);
            output.WriteSingle(value.alpha);
        }

        void EmitCompiledEffect(XnbWriter& output, const XnbCompiledEffect& value)
        {
            if (value.bytecode.empty())
            {
                throw XnbWriteException(
                    "XNB Effect: the compiled bytecode is empty. This writer stores an already "
                    "compiled effect; it does not compile one.");
            }
            output.WriteUInt32(static_cast<std::uint32_t>(value.bytecode.size()));
            output.WriteBytes(value.bytecode);
        }

        /** @brief Returns the serialized type of one shared resource in a model graph. */
        [[nodiscard]] std::string SharedResourceTypeName(const XnbModelSharedResourceData& resource)
        {
            return std::visit(
                [](const auto& value) -> std::string
                {
                    using Value = std::decay_t<decltype(value)>;
                    return XnbTypeKey<Value>::Name();
                },
                resource.value);
        }

        /** @brief Boxes one shared resource's value for dispatch. */
        [[nodiscard]] std::any SharedResourceValue(const XnbModelSharedResourceData& resource)
        {
            return std::visit([](const auto& value) { return std::any(value); }, resource.value);
        }

        /** @brief The `Model` writer, including the shared-resource graph its parts reference. */
        class ModelXnbTypeWriter final : public XnbTypeWriterT<XnbModelData>
        {
        public:
            [[nodiscard]] std::string TargetTypeName() const override
            {
                return XnbTypeKey<XnbModelData>::Name();
            }

            [[nodiscard]] std::string RuntimeReaderName() const override
            {
                return XnbQualifiedReaderName("Microsoft.Xna.Framework.Content.ModelReader",
                                              XnaGraphicsAssembly);
            }

            [[nodiscard]] bool IsValueType() const override { return false; }

            void Write(XnbWriter& output, const XnbModelData& model) const override
            {
                Validate(model);
                const auto boneCount = static_cast<std::uint32_t>(model.bones.size());

                // Register every shared resource up front, so a part's reference resolves to a
                // stable index no matter which mesh reaches it first. The key is the resource's
                // own position, which is the identity the canonical graph already uses.
                std::vector<std::int32_t> resourceIds;
                resourceIds.reserve(model.sharedResources.size());
                for (std::size_t index = 0u; index < model.sharedResources.size(); ++index)
                {
                    const XnbModelSharedResourceData& resource = model.sharedResources[index];
                    resourceIds.push_back(output.RegisterSharedResource(
                        "model.shared:" + std::to_string(index),
                        SharedResourceTypeName(resource), SharedResourceValue(resource)));
                }

                output.WriteUInt32(boneCount);
                for (const XnbModelBoneData& bone : model.bones)
                {
                    output.WriteObject(XnbTypeKey<std::string>::Name(), std::any(bone.name));
                    output.WriteRawObject(XnbTypeKey<Microsoft::Xna::Framework::Matrix>::Name(),
                                          std::any(bone.transform));
                }
                for (const XnbModelBoneData& bone : model.bones)
                {
                    WriteBoneReference(output, boneCount, bone.parent);
                    output.WriteCollectionCount(bone.children.size(),
                                                "Microsoft.Xna.Framework.Graphics.ModelBone");
                    for (const std::int32_t child : bone.children)
                    {
                        WriteBoneReference(output, boneCount, child);
                    }
                }

                output.WriteCollectionCount(model.meshes.size(),
                                            "Microsoft.Xna.Framework.Graphics.ModelMesh");
                for (const XnbModelMeshData& mesh : model.meshes)
                {
                    output.WriteObject(XnbTypeKey<std::string>::Name(), std::any(mesh.name));
                    WriteBoneReference(output, boneCount, mesh.parentBone);
                    output.WriteRawObject(
                        XnbTypeKey<Microsoft::Xna::Framework::BoundingSphere>::Name(),
                        std::any(mesh.boundingSphere));
                    // The canonical graph carries no Tag, and a Tag is a polymorphic object, so
                    // the null reference is both correct and the only thing it can express.
                    output.WriteNullObject();

                    output.WriteCollectionCount(mesh.parts.size(),
                                                "Microsoft.Xna.Framework.Graphics.ModelMeshPart");
                    for (const XnbModelPartData& part : mesh.parts)
                    {
                        output.WriteUInt32(static_cast<std::uint32_t>(part.vertexOffset));
                        output.WriteUInt32(static_cast<std::uint32_t>(part.vertexCount));
                        output.WriteUInt32(static_cast<std::uint32_t>(part.startIndex));
                        output.WriteUInt32(static_cast<std::uint32_t>(part.primitiveCount));
                        output.WriteNullObject();   // the mesh part's own Tag
                        WriteSharedReference(output, resourceIds, part.vertexBufferResource);
                        WriteSharedReference(output, resourceIds, part.indexBufferResource);
                        WriteSharedReference(output, resourceIds, part.effectResource);
                    }
                }

                WriteBoneReference(output, boneCount, model.rootBone);
                output.WriteNullObject();   // the model's own Tag
            }

        private:
            /**
             * @brief Writes a bone reference at the width the bone count selects.
             *
             * The format sizes this field by the model: a `Byte` while there are fewer than 255
             * bones, a `UInt32` otherwise. Zero is the null reference, so a real bone is written
             * as its index plus one.
             */
            static void WriteBoneReference(XnbWriter& output, const std::uint32_t boneCount,
                                           const std::int32_t bone)
            {
                if (bone < -1 || (bone >= 0 && static_cast<std::uint32_t>(bone) >= boneCount))
                {
                    throw XnbWriteException(
                        "XNB Model: bone reference " + std::to_string(bone) +
                        " is outside the model's " + std::to_string(boneCount) + " bones.");
                }
                const std::uint32_t encoded =
                    bone < 0 ? 0u : static_cast<std::uint32_t>(bone) + 1u;
                if (boneCount < 255u)
                {
                    output.WriteByte(static_cast<std::uint8_t>(encoded));
                }
                else
                {
                    output.WriteUInt32(encoded);
                }
            }

            static void WriteSharedReference(XnbWriter& output,
                                             const std::vector<std::int32_t>& resourceIds,
                                             const std::int32_t resource)
            {
                if (resource < 0)
                {
                    output.WriteNullSharedResource();
                    return;
                }
                if (static_cast<std::size_t>(resource) >= resourceIds.size())
                {
                    throw XnbWriteException(
                        "XNB Model: shared-resource reference " + std::to_string(resource) +
                        " is outside the model's " + std::to_string(resourceIds.size()) +
                        " shared resources.");
                }
                output.WriteSharedResourceReference(
                    resourceIds[static_cast<std::size_t>(resource)]);
            }

            static void Validate(const XnbModelData& model)
            {
                if (model.bones.empty())
                {
                    throw XnbWriteException("XNB Model: a model must declare at least one bone.");
                }
                if (model.rootBone < 0 ||
                    static_cast<std::size_t>(model.rootBone) >= model.bones.size())
                {
                    throw XnbWriteException(
                        "XNB Model: the root bone index " + std::to_string(model.rootBone) +
                        " is outside the model's " + std::to_string(model.bones.size()) +
                        " bones.");
                }
                for (const XnbModelMeshData& mesh : model.meshes)
                {
                    for (const XnbModelPartData& part : mesh.parts)
                    {
                        if (part.vertexOffset < 0 || part.vertexCount < 0 ||
                            part.startIndex < 0 || part.primitiveCount < 0)
                        {
                            throw XnbWriteException(
                                "XNB Model: mesh '" + mesh.name +
                                "' has a part with a negative offset or count.");
                        }
                    }
                }
            }
        };
    }

    void RegisterXnbModelTypeWriters(XnbTypeWriterRegistry& registry)
    {
        Register<XnbVertexDeclarationData>(registry, "VertexDeclarationReader", false,
                                           EmitVertexDeclaration);
        Register<XnbVertexBufferData>(registry, "VertexBufferReader", false, EmitVertexBuffer);
        Register<XnbIndexBufferData>(registry, "IndexBufferReader", false, EmitIndexBuffer);
        Register<XnbBasicEffectData>(registry, "BasicEffectReader", false, EmitBasicEffect);
        Register<XnbAlphaTestEffectData>(registry, "AlphaTestEffectReader", false,
                                         EmitAlphaTestEffect);
        Register<XnbDualTextureEffectData>(registry, "DualTextureEffectReader", false,
                                           EmitDualTextureEffect);
        Register<XnbEnvironmentMapEffectData>(registry, "EnvironmentMapEffectReader", false,
                                              EmitEnvironmentMapEffect);
        Register<XnbSkinnedEffectData>(registry, "SkinnedEffectReader", false, EmitSkinnedEffect);
        Register<XnbCompiledEffect>(registry, "EffectReader", false, EmitCompiledEffect);
        registry.Register(std::make_shared<const ModelXnbTypeWriter>());
    }
}
