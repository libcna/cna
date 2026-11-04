// SPDX-License-Identifier: MS-PL
#include "CNA/Content/ObjectDictionaryEXT.hpp"
#include "CNA/Content/ObjectValueEXT.hpp"

#include <any>
#include <cstring>
#include <map>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

#include "CNA/Internal/Xnb/CollectionContentTypeReaders.hpp"
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
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Plane.hpp"
#include "Microsoft/Xna/Framework/Point.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Ray.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "System/DateTime.hpp"
#include "System/TimeSpan.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

namespace CNA::Internal::Xnb
{
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::BoundingSphere;
    using Microsoft::Xna::Framework::Vector3;
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

        // XNA resolves every bone reference through ReadOnlyCollection<ModelBone>.get_Item
        // (Model.ReadBoneReference IL_002b-0034), so an index outside the bones read so far is
        // ArgumentOutOfRangeException there, for the parent, child, mesh and root references alike.
        ModelBone* RequireBone(const std::vector<ModelBone*>& bones, int32_t index, const char* context)
        {
            if (index < 0 || static_cast<std::size_t>(index) >= bones.size())
            {
                throw System::ArgumentOutOfRangeException(
                    "index", std::string("ModelReader: ") + context + " bone index out of range.");
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

        // The .NET full name a boxed Tag reports, i.e. what XNA's Tag.GetType().FullName would be,
        // for the strings and value types CNA's built-in readers produce. Any other boxed value
        // reports System.Object.
        std::string DotNetTypeNameOf(const std::any& value)
        {
            namespace Xna = Microsoft::Xna::Framework;
            static const std::pair<const std::type_info*, const char*> names[] = {
                {&typeid(std::string), "System.String"},
                {&typeid(bool), "System.Boolean"},
                {&typeid(std::uint8_t), "System.Byte"},
                {&typeid(std::int8_t), "System.SByte"},
                {&typeid(std::int16_t), "System.Int16"},
                {&typeid(std::uint16_t), "System.UInt16"},
                {&typeid(std::int32_t), "System.Int32"},
                {&typeid(std::uint32_t), "System.UInt32"},
                {&typeid(std::int64_t), "System.Int64"},
                {&typeid(std::uint64_t), "System.UInt64"},
                {&typeid(float), "System.Single"},
                {&typeid(double), "System.Double"},
                {&typeid(char16_t), "System.Char"},
                {&typeid(System::TimeSpan), "System.TimeSpan"},
                {&typeid(System::DateTime), "System.DateTime"},
                {&typeid(Xna::Vector2), "Microsoft.Xna.Framework.Vector2"},
                {&typeid(Xna::Vector3), "Microsoft.Xna.Framework.Vector3"},
                {&typeid(Xna::Vector4), "Microsoft.Xna.Framework.Vector4"},
                {&typeid(Xna::Matrix), "Microsoft.Xna.Framework.Matrix"},
                {&typeid(Xna::Quaternion), "Microsoft.Xna.Framework.Quaternion"},
                {&typeid(Xna::Color), "Microsoft.Xna.Framework.Color"},
                {&typeid(Xna::Point), "Microsoft.Xna.Framework.Point"},
                {&typeid(Xna::Rectangle), "Microsoft.Xna.Framework.Rectangle"},
                {&typeid(Xna::BoundingBox), "Microsoft.Xna.Framework.BoundingBox"},
                {&typeid(Xna::BoundingSphere), "Microsoft.Xna.Framework.BoundingSphere"},
                {&typeid(Xna::Ray), "Microsoft.Xna.Framework.Ray"},
                {&typeid(Xna::Plane), "Microsoft.Xna.Framework.Plane"},
            };
            for (const auto& [type, name] : names)
            {
                if (value.type() == *type) return name;
            }
            return "System.Object";
        }

        System::Object* ReadRuntimeTag(ContentReader& input, ModelReaderOwnedResources& resources)
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

            // XNA can cast a closed managed Dictionary back from Object. CNA's collection reader
            // produces an STL value, so box this stock Content Pipeline shape while retaining the
            // original managed type name and every already-type-checked value.
            using StringVector3ListDictionary =
                std::unordered_map<std::string, std::vector<Vector3>>;
            if (value.type() == typeid(StringVector3ListDictionary))
            {
                auto typed = std::any_cast<StringVector3ListDictionary>(std::move(value));
                std::map<std::string, std::any> erased;
                for (auto& [key, vertices] : typed)
                {
                    erased.emplace(std::move(key), std::move(vertices));
                }
                auto boxed = std::make_shared<CNA::Content::ObjectDictionaryEXT>(
                    std::move(erased),
                    "System.Collections.Generic.Dictionary`2[System.String,"
                    "System.Collections.Generic.List`1[Microsoft.Xna.Framework.Vector3]]");
                System::Object* boxedTag = boxed.get();
                resources.tagOwners.push_back(std::move(boxed));
                return boxedTag;
            }

            if (value.type() != typeid(std::shared_ptr<System::Object>))
            {
                // XNA reads every Tag with ReadObject<object> (Model.Read IL_0021-0028,
                // ReadMeshes IL_0045-004b, ReadMeshParts IL_004d-0053), so a string or any other
                // value is as valid a Tag as an object (TQ-033). It arrives boxed, keeping the
                // type its own reader produced.
                const std::string typeName = DotNetTypeNameOf(value);
                auto boxed = std::make_shared<CNA::Content::ObjectValueEXT>(std::move(value), typeName);
                System::Object* boxedTag = boxed.get();
                resources.tagOwners.push_back(std::move(boxed));
                return boxedTag;
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

            // Model.ReadBones IL_0054-008c: a bone's own parent reference (IL_0056), then its
            // child references (IL_0072-0084; a null one stays a null entry), handed together to
            // ModelBone.SetParentAndChildren (IL_008c). The parent is never inferred from another
            // bone's children.
            void BeginBoneLinks(const std::uint32_t /*bone*/, const std::int32_t parent,
                                const std::uint32_t /*childCount*/)
            {
                currentParent_ = parent != -1 ? RequireBone(boneRawPtrs_, parent, "parent") : nullptr;
                currentChildren_.clear();
            }

            void AddBoneChild(const std::uint32_t /*bone*/, const std::int32_t child)
            {
                currentChildren_.push_back(
                    child != -1 ? RequireBone(boneRawPtrs_, child, "child") : nullptr);
            }

            void EndBoneLinks(const std::uint32_t bone)
            {
                boneRawPtrs_.at(bone)->SetParentAndChildren(currentParent_, std::move(currentChildren_));
                currentChildren_.clear();
            }

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
                System::Object* tag = ReadRuntimeTag(input, *resources_);
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
                // XNA Model.Read IL_0014-001c stores the root reference as read: a null one leaves
                // Root null rather than naming bone 0 (TQ-033).
                if (root == -1) { model.setRootEXTProperty(nullptr); }
                model.setTagProperty(modelTag_);
                model.setOwnedResources(resources_);
                return model;
            }

        private:
            GraphicsDevice& device_;
            std::shared_ptr<ModelReaderOwnedResources> resources_;
            std::vector<ModelBone*> boneRawPtrs_;
            ModelBone* currentParent_ = nullptr;
            std::vector<ModelBone*> currentChildren_;
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
        // Keep the dictionary registration self-contained when callers register only the Model
        // reader family in a focused AOT setup. RegisterSpriteFontXnbReader installs the same
        // stateless closed reader in a normal built-in registration pass; duplicate registration
        // is intentionally idempotent.
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.ListReader`1[[Microsoft.Xna.Framework.Vector3]]",
            [] {
                return std::make_unique<ListReader<Vector3>>(
                    "System.Collections.Generic.List`1[[Microsoft.Xna.Framework.Vector3]]",
                    "Microsoft.Xna.Framework.Content.Vector3Reader");
            });
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.DictionaryReader`2[[System.String],"
            "[System.Collections.Generic.List`1[[Microsoft.Xna.Framework.Vector3]]]]",
            [] {
                return std::make_unique<DictionaryReader<std::string, std::vector<Vector3>>>(
                    "System.Collections.Generic.Dictionary`2[[System.String],"
                    "[System.Collections.Generic.List`1[[Microsoft.Xna.Framework.Vector3]]]]",
                    "Microsoft.Xna.Framework.Content.StringReader",
                    "Microsoft.Xna.Framework.Content.ListReader`1[[Microsoft.Xna.Framework.Vector3]]");
            });
    }
}
