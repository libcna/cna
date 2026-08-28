// SPDX-License-Identifier: MS-PL
#include "CNA/Content/ObjectDictionaryEXT.hpp"

#include <any>
#include <map>

#include "CNA/Internal/Xnb/ModelContentTypeReaders.hpp"

#include <limits>

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
    using Microsoft::Xna::Framework::Graphics::VertexElement;
    using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
    using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

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

        // FNA's ModelReader.ReadBoneReference: the bone ID is encoded as an 8-bit value when the
        // model has fewer than 255 bones, else a full 32-bit value; 0 means "no bone", so a real
        // reference is stored 1-based.
        int32_t ReadBoneReference(ContentReader& input, uint32_t boneCount)
        {
            const uint32_t boneId = boneCount < 255 ? input.ReadByte() : input.ReadUInt32();
            if (boneId != 0)
            {
                return static_cast<int32_t>(boneId - 1);
            }
            return -1;
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

        System::Object* ReadTag(
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
    }

    VertexDeclaration VertexDeclarationReader::Read(
        ContentReader& input, std::optional<VertexDeclaration> /*existingInstance*/)
    {
        const int32_t vertexStride = input.ReadInt32();
        const int32_t elementCount = input.ReadInt32();
        if (elementCount < 0 || elementCount > 1024)
        {
            throw ContentLoadException(
                "VertexDeclarationReader: invalid element count (" + std::to_string(elementCount) + ").");
        }

        std::vector<VertexElement> elements;
        elements.reserve(static_cast<std::size_t>(elementCount));
        for (int32_t i = 0; i < elementCount; ++i)
        {
            const int32_t offset = input.ReadInt32();
            const auto format = static_cast<VertexElementFormat>(input.ReadInt32());
            const auto usage = static_cast<VertexElementUsage>(input.ReadInt32());
            const int32_t usageIndex = input.ReadInt32();
            elements.emplace_back(offset, format, usage, usageIndex);
        }

        return VertexDeclaration(vertexStride, std::move(elements));
    }

    std::shared_ptr<VertexBuffer> VertexBufferReader::Read(
        ContentReader& input, std::optional<std::shared_ptr<VertexBuffer>> /*existingInstance*/)
    {
        auto declReader = ContentTypeReaderManager::CreateReader(
            "Microsoft.Xna.Framework.Content.VertexDeclarationReader");
        if (!declReader)
        {
            throw ContentLoadException(
                "VertexBufferReader: VertexDeclarationReader is not registered.");
        }
        VertexDeclaration declaration = input.ReadRawObject<VertexDeclaration>(*declReader);

        const int32_t vertexCount = static_cast<int32_t>(input.ReadUInt32());
        if (vertexCount < 0)
        {
            throw ContentLoadException("VertexBufferReader: invalid vertex count.");
        }
        const int32_t stride = declaration.getVertexStrideProperty();
        // Widen to int64_t before multiplying: vertexCount can be as large as INT32_MAX (from an
        // adversarial file), and vertexCount * stride overflowing a 32-bit int would silently
        // wrap to a small/negative byte count, defeating ReadBytesExactOrThrow()'s own mismatch
        // check below and letting SetDataRaw() read past the (much shorter) actual buffer.
        const int64_t byteCount64 = static_cast<int64_t>(vertexCount) * static_cast<int64_t>(stride);
        if (stride < 0 || byteCount64 < 0 || byteCount64 > std::numeric_limits<int32_t>::max())
        {
            throw ContentLoadException("VertexBufferReader: vertex count/stride overflow the byte count.");
        }
        std::vector<uint8_t> data =
            input.ReadBytesExactOrThrow(static_cast<int32_t>(byteCount64), "VertexBufferReader");

        GraphicsDevice& device = RequireGraphicsDevice(input, "VertexBufferReader");
        auto buffer = std::make_shared<VertexBuffer>(device, declaration, vertexCount, BufferUsage::None);
        buffer->SetDataRaw(data.data(), vertexCount, stride);
        return buffer;
    }

    std::shared_ptr<IndexBuffer> IndexBufferReader::Read(
        ContentReader& input, std::optional<std::shared_ptr<IndexBuffer>> existingInstance)
    {
        const bool sixteenBits = input.ReadBoolean();
        const int32_t dataSize = input.ReadInt32();
        if (dataSize < 0)
        {
            throw ContentLoadException("IndexBufferReader: invalid data size.");
        }
        std::vector<uint8_t> data = input.ReadBytesExactOrThrow(dataSize, "IndexBufferReader");

        std::shared_ptr<IndexBuffer> indexBuffer = existingInstance.value_or(nullptr);
        if (!indexBuffer)
        {
            GraphicsDevice& device = RequireGraphicsDevice(input, "IndexBufferReader");
            indexBuffer = std::make_shared<IndexBuffer>(
                device,
                sixteenBits ? IndexElementSize::SixteenBits : IndexElementSize::ThirtyTwoBits,
                sixteenBits ? dataSize / 2 : dataSize / 4,
                BufferUsage::None);
        }

        if (sixteenBits)
        {
            indexBuffer->SetData(reinterpret_cast<const uint16_t*>(data.data()), dataSize / 2);
        }
        else
        {
            indexBuffer->SetData(reinterpret_cast<const uint32_t*>(data.data()), dataSize / 4);
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
        auto res = std::make_shared<ModelReaderOwnedResources>();

        // --- Bone names, transforms, and hierarchy (plans/plan_xnb.md XNB-37) ---
        const uint32_t boneCount = input.ReadUInt32();
        if (boneCount > 100000)
        {
            throw ContentLoadException("ModelReader: implausible bone count (" + std::to_string(boneCount) + ").");
        }
        std::vector<ModelBone*> boneRawPtrs;
        boneRawPtrs.reserve(boneCount);
        for (uint32_t i = 0; i < boneCount; ++i)
        {
            std::string name = input.ReadObject<std::string>();
            const Matrix matrix = input.ReadMatrix();
            auto bone = std::make_unique<ModelBone>(static_cast<int>(i), std::move(name));
            bone->setTransformProperty(matrix);
            boneRawPtrs.push_back(bone.get());
            res->boneOwners.push_back(std::move(bone));
        }

        for (uint32_t i = 0; i < boneCount; ++i)
        {
            // The scalar parent-bone reference is read here only for correct stream positioning --
            // ModelBone::AddChild() (called below from each bone's own child list) already sets
            // the child's Parent when its true parent processes its children, so this value is
            // deliberately not used to mutate state (matches FNA's own redundant parent+child-list
            // encoding; CNA's ModelBone exposes AddChild() but no separate SetParent()).
            ReadBoneReference(input, boneCount);

            const uint32_t childCount = input.ReadUInt32();
            for (uint32_t j = 0; j < childCount; ++j)
            {
                const int32_t childIndex = ReadBoneReference(input, boneCount);
                if (childIndex != -1)
                {
                    boneRawPtrs[i]->AddChild(RequireBone(boneRawPtrs, childIndex, "child"));
                }
            }
        }

        // --- Meshes, mesh parts, and shared resources (plans/plan_xnb.md XNB-38/39/40) ---
        const int32_t meshCount = input.ReadInt32();
        if (meshCount < 0 || meshCount > 100000)
        {
            throw ContentLoadException("ModelReader: invalid mesh count (" + std::to_string(meshCount) + ").");
        }
        std::vector<ModelMesh*> meshRawPtrs;
        std::vector<ModelBone*> meshParentBones;
        meshRawPtrs.reserve(static_cast<std::size_t>(meshCount));
        meshParentBones.reserve(static_cast<std::size_t>(meshCount));

        for (int32_t i = 0; i < meshCount; ++i)
        {
            std::string meshName = input.ReadObject<std::string>();
            const int32_t parentBoneIndex = ReadBoneReference(input, boneCount);
            const BoundingSphere boundingSphere = input.ReadBoundingSphere();
            System::Object* meshTag = ReadTag(input, "mesh", *res);

            const int32_t partCount = input.ReadInt32();
            if (partCount < 0 || partCount > 100000)
            {
                throw ContentLoadException("ModelReader: invalid mesh part count (" + std::to_string(partCount) + ").");
            }

            std::vector<ModelMeshPart*> partRawPtrs;
            partRawPtrs.reserve(static_cast<std::size_t>(partCount));

            for (int32_t j = 0; j < partCount; ++j)
            {
                auto part = std::make_unique<ModelMeshPart>();
                ModelMeshPart* partPtr = part.get();

                partPtr->SetVertexOffset(input.ReadInt32());
                partPtr->SetNumVertices(input.ReadInt32());
                partPtr->SetStartIndex(input.ReadInt32());
                partPtr->SetPrimitiveCount(input.ReadInt32());
                partPtr->setTagProperty(ReadTag(input, "mesh part", *res));

                partRawPtrs.push_back(partPtr);
                res->partOwners.push_back(std::move(part));

                input.ReadSharedResource<std::shared_ptr<VertexBuffer>>(
                    [res, partPtr](std::shared_ptr<VertexBuffer> vb)
                    {
                        partPtr->SetVertexBuffer(vb.get());
                        res->vertexBufferOwners.push_back(std::move(vb));
                    });
                input.ReadSharedResource<std::shared_ptr<IndexBuffer>>(
                    [res, partPtr](std::shared_ptr<IndexBuffer> ib)
                    {
                        partPtr->SetIndexBuffer(ib.get());
                        res->indexBufferOwners.push_back(std::move(ib));
                    });
                input.ReadSharedResource<std::shared_ptr<Effect>>(
                    [res, partPtr](std::shared_ptr<Effect> fx)
                    {
                        // setEffectProperty() itself maintains the owning ModelMesh's
                        // ModelEffectCollection (Add/Remove), so nothing else is needed here.
                        partPtr->setEffectProperty(fx.get());
                        res->effectOwners.push_back(std::move(fx));
                    });
            }

            auto mesh = std::make_unique<ModelMesh>(&device, std::move(meshName), std::move(partRawPtrs));
            mesh->setBoundingSphereProperty(boundingSphere);
            mesh->setTagProperty(meshTag);
            meshRawPtrs.push_back(mesh.get());
            meshParentBones.push_back(
                parentBoneIndex != -1 ? RequireBone(boneRawPtrs, parentBoneIndex, "mesh parent") : nullptr);
            res->meshOwners.push_back(std::move(mesh));
        }

        const int32_t rootBoneIndex = ReadBoneReference(input, boneCount);
        if (rootBoneIndex != -1)
        {
            RequireBone(boneRawPtrs, rootBoneIndex, "root"); // validates range only
        }
        const std::size_t rootIndex = rootBoneIndex != -1 ? static_cast<std::size_t>(rootBoneIndex) : 0;

        Model model(&device, boneRawPtrs, meshRawPtrs, meshParentBones, rootIndex);
        model.setTagProperty(ReadTag(input, "model", *res));

        model.setOwnedResources(res);
        return model;
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
