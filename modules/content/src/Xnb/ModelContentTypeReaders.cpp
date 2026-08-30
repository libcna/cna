// SPDX-License-Identifier: MS-PL
#include "CNA/Content/ObjectDictionaryEXT.hpp"

#include <any>
#include <cstring>
#include <map>

#include "CNA/Internal/Xnb/ModelContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/XnbCanonicalData.hpp"
#include "XnbModelGraphReader.hpp"

#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"

namespace CNA::Internal::Xnb
{
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::BoundingSphere;
    using Microsoft::Xna::Framework::Content::ContentLoadException;
    using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
    using Microsoft::Xna::Framework::Graphics::BufferUsage;
    using Microsoft::Xna::Framework::Graphics::Effect;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::IndexElementSize;
    using Microsoft::Xna::Framework::Graphics::ModelBone;
    using Microsoft::Xna::Framework::Graphics::ModelMesh;
    using Microsoft::Xna::Framework::Graphics::ModelMeshPart;

    namespace
    {
        GraphicsDevice& RequireGraphicsDevice(ContentReader& input, const char* readerName)
        {
            if (!input.getContentManagerProperty())
            {
                throw ContentLoadException(
                    std::string(readerName) +
                    ": no GraphicsDevice available (ContentManager was not set on this ContentReader).");
            }
            return input.getContentManagerProperty()->getGraphicsDeviceInternal();
        }

        ModelBone* RequireBone(const std::vector<ModelBone*>& bones, int32_t index, const char* context)
        {
            if (index < 0 || static_cast<std::size_t>(index) >= bones.size())
            {
                throw ContentLoadException(
                    std::string("ModelReader: ") + context + " bone index out of range.");
            }
            return bones[static_cast<std::size_t>(index)];
        }

        // Owns every GPU/CPU resource this ModelReader allocates, kept alive via the returned
        // Model's own Model::setOwnedResources() -- mirrors ContentManager.cpp's own .model.json
        // ModelTypeReader::ModelResources precedent exactly (same ownership shape, same reason).
        struct ModelReaderOwnedResources
        {
            std::vector<std::unique_ptr<ModelBone>> boneOwners;
            std::vector<std::unique_ptr<ModelMesh>> meshOwners;
            std::vector<std::unique_ptr<ModelMeshPart>> partOwners;
            std::vector<std::shared_ptr<VertexBuffer>> vertexBufferOwners;
            std::vector<std::shared_ptr<IndexBuffer>> indexBufferOwners;
            std::vector<std::shared_ptr<Effect>> effectOwners;
            std::vector<std::shared_ptr<System::Object>> tagOwners;
        };

        System::Object* ReadRuntimeTag(
            ContentReader& input, const char* fieldContext, ModelReaderOwnedResources& resources)
        {
            std::any value = input.ReadObject();
            if (!value.has_value())
            {
                return nullptr;
            }

            // A `Dictionary<string, object>` is what a custom ContentProcessor attaches to
            // Model.Tag -- XNA's own TrianglePickingSample does exactly that -- and it is the one
            // shape the stock pipeline can write there without a custom reader on the game side.
            // `DictionaryReader<String, Object>` produces a std::map<std::string, std::any>, which
            // is not a System::Object, so it was refused: before this, the ONLY value this
            // function accepted came from a test-only reader, and no production reader in the tree
            // produced one. Boxing it in ObjectDictionaryEXT is what makes `model.Tag` reachable
            // from a game, and it keeps each entry's own reader-produced type intact.
            if (value.type() == typeid(std::map<std::string, std::any>))
            {
                auto boxed = std::make_shared<CNA::Content::ObjectDictionaryEXT>(
                    std::any_cast<std::map<std::string, std::any>>(std::move(value)));
                System::Object* boxedTag = boxed.get();
                resources.tagOwners.push_back(std::move(boxed));
                return boxedTag;
            }

            if (value.type() != typeid(std::shared_ptr<System::Object>))
            {
                throw ContentLoadException(
                    std::string("ModelReader: non-null ") + fieldContext +
                    " Tag must deserialize as std::shared_ptr<System::Object> or as a "
                    "Dictionary<string, object>.");
            }

            auto owner = std::any_cast<std::shared_ptr<System::Object>>(std::move(value));
            System::Object* tag = owner.get();
            resources.tagOwners.push_back(std::move(owner));
            return tag;
        }

        class RuntimeModelSink
        {
        public:
            RuntimeModelSink(GraphicsDevice& device,
                             std::shared_ptr<ModelReaderOwnedResources> resources)
                : device_(device), resources_(std::move(resources)) {}

            [[nodiscard]] std::string ReadString(ContentReader& input) const
            {
                return input.ReadObject<std::string>();
            }

            void BeginBones(const std::uint32_t count)
            {
                boneRawPtrs_.reserve(count);
            }

            void AddBone(const std::uint32_t index, std::string name, const Matrix& transform)
            {
                auto bone = std::make_unique<ModelBone>(static_cast<int>(index), std::move(name));
                bone->setTransformProperty(transform);
                boneRawPtrs_.push_back(bone.get());
                resources_->boneOwners.push_back(std::move(bone));
            }

            void BeginBoneLinks(const std::uint32_t /*bone*/, const std::int32_t /*parent*/,
                                const std::uint32_t /*childCount*/) {}

            void AddBoneChild(const std::uint32_t bone, const std::int32_t child)
            {
                if (child != -1)
                {
                    boneRawPtrs_.at(bone)->AddChild(
                        RequireBone(boneRawPtrs_, child, "child"));
                }
            }

            void EndBoneLinks(const std::uint32_t /*bone*/) {}

            void BeginMeshes(const std::uint32_t count)
            {
                meshRawPtrs_.reserve(count);
                meshParentBones_.reserve(count);
            }

            void BeginMesh(const std::uint32_t /*index*/, std::string name,
                           const std::int32_t parentBone, const BoundingSphere& bounds)
            {
                currentMeshName_ = std::move(name);
                currentMeshParent_ = parentBone;
                currentMeshBounds_ = bounds;
                currentMeshTag_ = nullptr;
                currentPartPtrs_.clear();
            }

            void ReadTag(ContentReader& input, const XnbModelTagKind kind)
            {
                System::Object* tag = ReadRuntimeTag(
                    input, kind == XnbModelTagKind::Mesh
                               ? "mesh"
                               : kind == XnbModelTagKind::MeshPart ? "mesh part" : "model",
                    *resources_);
                if (kind == XnbModelTagKind::Mesh) { currentMeshTag_ = tag; }
                else if (kind == XnbModelTagKind::MeshPart) { currentPart_->setTagProperty(tag); }
                else { modelTag_ = tag; }
            }

            void BeginMeshParts(const std::uint32_t count)
            {
                currentPartPtrs_.reserve(count);
            }

            void BeginMeshPart(const std::uint32_t /*index*/, const std::int32_t vertexOffset,
                               const std::int32_t vertexCount, const std::int32_t startIndex,
                               const std::int32_t primitiveCount)
            {
                auto part = std::make_unique<ModelMeshPart>();
                currentPart_ = part.get();
                currentPart_->SetVertexOffset(vertexOffset);
                currentPart_->SetNumVertices(vertexCount);
                currentPart_->SetStartIndex(startIndex);
                currentPart_->SetPrimitiveCount(primitiveCount);
                currentPartPtrs_.push_back(currentPart_);
                resources_->partOwners.push_back(std::move(part));
            }

            void ReadSharedReference(ContentReader& input, const XnbModelSharedKind kind)
            {
                ModelMeshPart* const part = currentPart_;
                const auto resources = resources_;
                if (kind == XnbModelSharedKind::VertexBuffer)
                {
                    input.ReadSharedResource<std::shared_ptr<VertexBuffer>>(
                        [resources, part](std::shared_ptr<VertexBuffer> value)
                        {
                            part->SetVertexBuffer(value.get());
                            resources->vertexBufferOwners.push_back(std::move(value));
                        });
                }
                else if (kind == XnbModelSharedKind::IndexBuffer)
                {
                    input.ReadSharedResource<std::shared_ptr<IndexBuffer>>(
                        [resources, part](std::shared_ptr<IndexBuffer> value)
                        {
                            part->SetIndexBuffer(value.get());
                            resources->indexBufferOwners.push_back(std::move(value));
                        });
                }
                else
                {
                    input.ReadSharedResource<std::shared_ptr<Effect>>(
                        [resources, part](std::shared_ptr<Effect> value)
                        {
                            part->setEffectProperty(value.get());
                            resources->effectOwners.push_back(std::move(value));
                        });
                }
            }

            void EndMeshPart() { currentPart_ = nullptr; }

            void EndMesh()
            {
                auto mesh = std::make_unique<ModelMesh>(
                    &device_, std::move(currentMeshName_), std::move(currentPartPtrs_));
                mesh->setBoundingSphereProperty(currentMeshBounds_);
                mesh->setTagProperty(currentMeshTag_);
                meshRawPtrs_.push_back(mesh.get());
                meshParentBones_.push_back(
                    currentMeshParent_ != -1
                        ? RequireBone(boneRawPtrs_, currentMeshParent_, "mesh parent")
                        : nullptr);
                resources_->meshOwners.push_back(std::move(mesh));
            }

            [[nodiscard]] Model Finish(const std::int32_t root)
            {
                if (root != -1) { RequireBone(boneRawPtrs_, root, "root"); }
                const std::size_t rootIndex =
                    root != -1 ? static_cast<std::size_t>(root) : 0u;
                Model model(
                    &device_, boneRawPtrs_, meshRawPtrs_, meshParentBones_, rootIndex);
                model.setTagProperty(modelTag_);
                model.setOwnedResources(resources_);
                return model;
            }

        private:
            GraphicsDevice& device_;
            std::shared_ptr<ModelReaderOwnedResources> resources_;
            std::vector<ModelBone*> boneRawPtrs_;
            std::vector<ModelMesh*> meshRawPtrs_;
            std::vector<ModelBone*> meshParentBones_;
            std::string currentMeshName_;
            std::int32_t currentMeshParent_ = -1;
            BoundingSphere currentMeshBounds_;
            System::Object* currentMeshTag_ = nullptr;
            std::vector<ModelMeshPart*> currentPartPtrs_;
            ModelMeshPart* currentPart_ = nullptr;
            System::Object* modelTag_ = nullptr;
        };
    }

    VertexDeclaration VertexDeclarationReader::Read(
        ContentReader& input, std::optional<VertexDeclaration> /*existingInstance*/)
    {
        XnbVertexDeclarationData decoded = DecodeVertexDeclarationXnbData(input);
        return VertexDeclaration(decoded.stride, std::move(decoded.elements));
    }

    std::shared_ptr<VertexBuffer> VertexBufferReader::Read(
        ContentReader& input, std::optional<std::shared_ptr<VertexBuffer>> /*existingInstance*/)
    {
        XnbVertexBufferData decoded = DecodeVertexBufferXnbData(input);
        VertexDeclaration declaration(
            decoded.declaration.stride, std::move(decoded.declaration.elements));
        const int32_t vertexCount = static_cast<int32_t>(decoded.vertexCount);

        GraphicsDevice& device = RequireGraphicsDevice(input, "VertexBufferReader");
        auto buffer = std::make_shared<VertexBuffer>(device, declaration, vertexCount, BufferUsage::None);
        buffer->SetDataRaw(decoded.bytes.data(), vertexCount, decoded.declaration.stride);
        return buffer;
    }

    std::shared_ptr<IndexBuffer> IndexBufferReader::Read(
        ContentReader& input, std::optional<std::shared_ptr<IndexBuffer>> existingInstance)
    {
        const XnbIndexBufferData decoded = DecodeIndexBufferXnbData(input);
        const bool sixteenBits = decoded.indexElementSize == 2u;
        const int32_t elementCount = static_cast<int32_t>(
            decoded.bytes.size() / decoded.indexElementSize);

        std::shared_ptr<IndexBuffer> indexBuffer = existingInstance.value_or(nullptr);
        if (!indexBuffer)
        {
            GraphicsDevice& device = RequireGraphicsDevice(input, "IndexBufferReader");
            indexBuffer = std::make_shared<IndexBuffer>(
                device,
                sixteenBits ? IndexElementSize::SixteenBits : IndexElementSize::ThirtyTwoBits,
                elementCount,
                BufferUsage::None);
        }

        if (sixteenBits)
        {
            std::vector<std::uint16_t> indices(static_cast<std::size_t>(elementCount));
            std::memcpy(indices.data(), decoded.bytes.data(), decoded.bytes.size());
            indexBuffer->SetData(indices.data(), elementCount);
        }
        else
        {
            std::vector<std::uint32_t> indices(static_cast<std::size_t>(elementCount));
            std::memcpy(indices.data(), decoded.bytes.data(), decoded.bytes.size());
            indexBuffer->SetData(indices.data(), elementCount);
        }
        return indexBuffer;
    }

    Model ModelReader::Read(ContentReader& input, std::optional<Model> existingInstance)
    {
        if (existingInstance.has_value())
        {
            // FNA supports reloading into an existing Model (mesh parts reused in place, only new
            // VertexBuffer/IndexBuffer/Effect shared-resource fixups re-applied). CNA's mesh/part
            // API has no in-place-rebind path, and CanDeserializeIntoExistingObject stays false
            // (matching every other reader in this plan), so this is unreachable via normal
            // ContentManager dispatch -- documented, not silently misbehaving, if reached directly.
            throw ContentLoadException(
                "ModelReader: reloading into an existing Model instance is not supported.");
        }

        GraphicsDevice& device = RequireGraphicsDevice(input, "ModelReader");
        RuntimeModelSink sink(device, std::make_shared<ModelReaderOwnedResources>());
        return ReadXnbModelGraph(input, sink);
    }

    void RegisterModelXnbReaders()
    {
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.VertexDeclarationReader",
            [] { return std::make_unique<VertexDeclarationReader>(); });
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.VertexBufferReader",
            [] { return std::make_unique<VertexBufferReader>(); });
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.IndexBufferReader",
            [] { return std::make_unique<IndexBufferReader>(); });
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.ModelReader",
            [] { return std::make_unique<ModelReader>(); });
    }
}
