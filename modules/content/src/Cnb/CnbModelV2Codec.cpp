// SPDX-License-Identifier: MS-PL
// EncodeModelV2ToCnb(): the writing half of the .cnb model codec (schema 2).
//
// Link boundary: the reading half is CnbModelV2Decode.cpp, which every program that loads a .cnb
// model links; this one, which needs CnbWriter, only a content tool does
// (docs/link-size-architecture.md).
#include "CNA/Content/Cnb/CnbModelV2Codec.hpp"
#include "CnbCodecDetail.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <span>
#include <string>
#include <utility>

#include "CNA/Content/Cnb/CnbByteReader.hpp"
#include "CNA/Content/Cnb/CnbByteWriter.hpp"
#include "CNA/Content/Cnb/CnbWriter.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

namespace CNA::Content::Cnb
{
    using Microsoft::Xna::Framework::Content::ContentLoadException;

    namespace
    {
        using namespace ModelV2CodecDetail;

        std::uint32_t ToU32(const std::size_t value, const char* what)
        {
            if (value > std::numeric_limits<std::uint32_t>::max())
            {
                Fail(std::string(what) + " exceeds the u32 wire range");
            }
            return static_cast<std::uint32_t>(value);
        }












        class StringTable
        {
        public:
            std::uint32_t Intern(const std::string& value)
            {
                if (const auto found = indices_.find(value); found != indices_.end())
                {
                    return found->second;
                }
                const std::uint32_t index = ToU32(values_.size(), "string count");
                values_.push_back(value);
                indices_.emplace(value, index);
                return index;
            }

            std::vector<std::uint8_t> Encode() const
            {
                CnbByteWriter writer;
                writer.WriteU32(ToU32(values_.size(), "string count"));
                for (const std::string& value : values_) { writer.WriteString(value); }
                return writer.Take();
            }

        private:
            std::map<std::string, std::uint32_t> indices_;
            std::vector<std::string> values_;
        };

        class XrefTable
        {
        public:
            std::uint32_t Intern(const std::string& value, const std::uint32_t assetType)
            {
                if (value.empty()) { return CnbModelV2NoIndex; }
                const auto key = std::make_pair(value, assetType);
                if (const auto found = indices_.find(key); found != indices_.end())
                {
                    return found->second;
                }
                const std::uint32_t index = ToU32(values_.size(), "external-reference count");
                values_.push_back({0u, assetType, value});
                indices_.emplace(key, index);
                return index;
            }

            std::vector<CnbExternalReference> Take() { return std::move(values_); }

        private:
            std::map<std::pair<std::string, std::uint32_t>, std::uint32_t> indices_;
            std::vector<CnbExternalReference> values_;
        };

        void WriteArray3(CnbByteWriter& writer, const std::array<float, 3>& values)
        {
            for (const float value : values) { writer.WriteF32(value); }
        }




    }

    std::vector<std::uint8_t> EncodeModelV2ToCnb(const CnbModelV2Data& model,
                                                  const std::string& contentName)
    {
        ValidateModel(model);

        const std::uint32_t boneCount = ToU32(model.bones.size(), "bone count");
        const std::uint32_t meshCount = ToU32(model.meshes.size(), "mesh count");
        std::uint32_t partCount = 0u;
        for (const CnbModelV2Mesh& mesh : model.meshes)
        {
            const std::uint32_t meshParts = ToU32(mesh.parts.size(), "mesh part count");
            if (meshParts > std::numeric_limits<std::uint32_t>::max() - partCount)
            {
                Fail("part count exceeds the u32 wire range");
            }
            partCount += meshParts;
        }
        const std::uint32_t declarationCount =
            ToU32(model.vertexDeclarations.size(), "declaration count");
        std::uint32_t elementCount = 0u;
        for (const CnbModelV2VertexDeclaration& declaration : model.vertexDeclarations)
        {
            const std::uint32_t declarationElements =
                ToU32(declaration.elements.size(), "declaration element count");
            if (declarationElements >
                std::numeric_limits<std::uint32_t>::max() - elementCount)
            {
                Fail("element count exceeds the u32 wire range");
            }
            elementCount += declarationElements;
        }
        const std::uint32_t vertexBufferCount =
            ToU32(model.vertexBuffers.size(), "vertex-buffer count");
        const std::uint32_t indexBufferCount =
            ToU32(model.indexBuffers.size(), "index-buffer count");
        const std::uint32_t effectCount = ToU32(model.effects.size(), "effect count");

        StringTable strings;
        for (const CnbModelV2Bone& bone : model.bones) { strings.Intern(bone.name); }
        for (const CnbModelV2Mesh& mesh : model.meshes) { strings.Intern(mesh.name); }

        CnbByteWriter header;
        header.WriteU32(0u);
        header.WriteU32(boneCount);
        header.WriteU32(meshCount);
        header.WriteU32(partCount);
        header.WriteU32(declarationCount);
        header.WriteU32(elementCount);
        header.WriteU32(vertexBufferCount);
        header.WriteU32(indexBufferCount);
        header.WriteU32(effectCount);
        header.WriteU32(model.rootBone);
        header.WriteZeros(24u);

        CnbByteWriter bones;
        for (const CnbModelV2Bone& bone : model.bones)
        {
            bones.WriteU32(strings.Intern(bone.name));
            bones.WriteI32(bone.parent);
            for (const float value : bone.transform) { bones.WriteF32(value); }
        }

        CnbByteWriter meshes;
        CnbByteWriter parts;
        std::uint32_t firstPart = 0u;
        for (const CnbModelV2Mesh& mesh : model.meshes)
        {
            meshes.WriteU32(strings.Intern(mesh.name));
            meshes.WriteI32(mesh.parentBone);
            for (const float value : mesh.boundingSphere) { meshes.WriteF32(value); }
            meshes.WriteU32(firstPart);
            meshes.WriteU32(ToU32(mesh.parts.size(), "mesh part count"));
            for (const CnbModelV2Part& part : mesh.parts)
            {
                parts.WriteU32(part.vertexOffset);
                parts.WriteU32(part.numVertices);
                parts.WriteU32(part.startIndex);
                parts.WriteU32(part.primitiveCount);
                parts.WriteU32(part.vertexBuffer);
                parts.WriteU32(part.indexBuffer);
                parts.WriteU32(part.effect);
                parts.WriteU32(0u);
            }
            firstPart += ToU32(mesh.parts.size(), "mesh part count");
        }

        CnbByteWriter declarations;
        std::uint32_t firstElement = 0u;
        for (const CnbModelV2VertexDeclaration& declaration : model.vertexDeclarations)
        {
            declarations.WriteU32(declaration.vertexStride);
            declarations.WriteU32(firstElement);
            declarations.WriteU32(ToU32(declaration.elements.size(), "element count"));
            declarations.WriteU32(0u);
            firstElement += ToU32(declaration.elements.size(), "element count");
        }
        for (const CnbModelV2VertexDeclaration& declaration : model.vertexDeclarations)
        {
            for (const CnbModelV2VertexElement& element : declaration.elements)
            {
                declarations.WriteU32(element.offset);
                declarations.WriteU32(static_cast<std::uint32_t>(element.format));
                declarations.WriteU32(static_cast<std::uint32_t>(element.usage));
                declarations.WriteU32(element.usageIndex);
                declarations.WriteU32(0u);
            }
        }

        CnbByteWriter vertexResources;
        for (std::size_t index = 0u; index < model.vertexBuffers.size(); ++index)
        {
            const CnbModelV2VertexBuffer& buffer = model.vertexBuffers[index];
            vertexResources.WriteU32(buffer.declaration);
            vertexResources.WriteU32(buffer.vertexCount);
            vertexResources.WriteU32(static_cast<std::uint32_t>(index));
            vertexResources.WriteU32(0u);
        }

        CnbByteWriter indexResources;
        for (std::size_t index = 0u; index < model.indexBuffers.size(); ++index)
        {
            const CnbModelV2IndexBuffer& buffer = model.indexBuffers[index];
            indexResources.WriteU32(buffer.indexElementSize);
            indexResources.WriteU32(buffer.indexCount);
            indexResources.WriteU32(static_cast<std::uint32_t>(index));
            indexResources.WriteU32(0u);
        }

        XrefTable xrefs;
        CnbByteWriter effects;
        for (const CnbModelV2Effect& effect : model.effects)
        {
            std::uint32_t flags = 0u;
            std::uint32_t primary = CnbModelV2NoIndex;
            std::uint32_t secondary = CnbModelV2NoIndex;
            std::uint32_t cube = CnbModelV2NoIndex;
            std::uint32_t integer0 = 0u;
            std::uint32_t integer1 = 0u;
            std::array<float, 3> vector0{};
            std::array<float, 3> vector1{};
            std::array<float, 3> vector2{};
            float scalar0 = 0.0f;
            float scalar1 = 0.0f;
            float scalar2 = 0.0f;
            float scalar3 = 0.0f;

            switch (effect.kind)
            {
                case CnbModelV2EffectKind::BasicEffect:
                    flags = effect.vertexColorEnabled ? 1u : 0u;
                    primary = xrefs.Intern(effect.primaryTexture, CnbAssetTypeId::Texture2D);
                    vector0 = effect.diffuse;
                    vector1 = effect.emissive;
                    vector2 = effect.specular;
                    scalar0 = effect.specularPower;
                    scalar1 = effect.alpha;
                    break;
                case CnbModelV2EffectKind::SkinnedEffect:
                    primary = xrefs.Intern(effect.primaryTexture, CnbAssetTypeId::Texture2D);
                    integer0 = effect.weightsPerVertex;
                    vector0 = effect.diffuse;
                    vector1 = effect.emissive;
                    vector2 = effect.specular;
                    scalar0 = effect.specularPower;
                    scalar1 = effect.alpha;
                    break;
                case CnbModelV2EffectKind::DualTextureEffect:
                    flags = effect.vertexColorEnabled ? 1u : 0u;
                    primary = xrefs.Intern(effect.primaryTexture, CnbAssetTypeId::Texture2D);
                    secondary = xrefs.Intern(effect.secondaryTexture, CnbAssetTypeId::Texture2D);
                    vector0 = effect.diffuse;
                    scalar0 = effect.alpha;
                    break;
                case CnbModelV2EffectKind::AlphaTestEffect:
                    flags = effect.vertexColorEnabled ? 1u : 0u;
                    primary = xrefs.Intern(effect.primaryTexture, CnbAssetTypeId::Texture2D);
                    integer0 = effect.alphaFunction;
                    integer1 = effect.referenceAlpha;
                    vector0 = effect.diffuse;
                    scalar0 = effect.alpha;
                    break;
                case CnbModelV2EffectKind::EnvironmentMapEffect:
                    primary = xrefs.Intern(effect.primaryTexture, CnbAssetTypeId::Texture2D);
                    cube = xrefs.Intern(effect.cubeTexture, CnbAssetTypeId::TextureCube);
                    vector0 = effect.diffuse;
                    vector1 = effect.emissive;
                    vector2 = effect.specular;
                    scalar0 = effect.environmentMapAmount;
                    scalar1 = effect.fresnelFactor;
                    scalar2 = effect.alpha;
                    break;
            }

            effects.WriteU32(static_cast<std::uint32_t>(effect.kind));
            effects.WriteU32(flags);
            effects.WriteU32(primary);
            effects.WriteU32(secondary);
            effects.WriteU32(cube);
            effects.WriteU32(integer0);
            effects.WriteU32(integer1);
            effects.WriteU32(0u);
            WriteArray3(effects, vector0);
            WriteArray3(effects, vector1);
            WriteArray3(effects, vector2);
            effects.WriteF32(scalar0);
            effects.WriteF32(scalar1);
            effects.WriteF32(scalar2);
            effects.WriteF32(scalar3);
            effects.WriteZeros(12u);
        }

        CnbWriter writer(CnbAssetTypeId::Model, CnbModelV2SchemaVersion);
        writer.SetMetadata("Microsoft.Xna.Framework.Graphics.Model", contentName);
        writer.SetExternalReferences(xrefs.Take());
        writer.AddChunk(CnbModelV2Chunk::Header, header.Take(), CnbChunkFlags::Mandatory, 4u);
        writer.AddChunk(CnbModelV2Chunk::Strings, strings.Encode(), CnbChunkFlags::Mandatory, 4u);
        writer.AddChunk(CnbModelV2Chunk::Bones, bones.Take(), CnbChunkFlags::Mandatory, 4u);
        writer.AddChunk(CnbModelV2Chunk::Meshes, meshes.Take(), CnbChunkFlags::Mandatory, 4u);
        writer.AddChunk(CnbModelV2Chunk::Parts, parts.Take(), CnbChunkFlags::Mandatory, 4u);
        writer.AddChunk(CnbModelV2Chunk::VertexDeclarations, declarations.Take(),
                        CnbChunkFlags::Mandatory, 4u);
        writer.AddChunk(CnbModelV2Chunk::VertexResources, vertexResources.Take(),
                        CnbChunkFlags::Mandatory, 4u);
        for (const CnbModelV2VertexBuffer& buffer : model.vertexBuffers)
        {
            writer.AddChunk(CnbModelV2Chunk::VertexData, buffer.bytes,
                            CnbChunkFlags::Mandatory, 16u);
        }
        writer.AddChunk(CnbModelV2Chunk::IndexResources, indexResources.Take(),
                        CnbChunkFlags::Mandatory, 4u);
        for (const CnbModelV2IndexBuffer& buffer : model.indexBuffers)
        {
            writer.AddChunk(CnbModelV2Chunk::IndexData, buffer.bytes,
                            CnbChunkFlags::Mandatory, 16u);
        }
        writer.AddChunk(CnbModelV2Chunk::Effects, effects.Take(),
                        CnbChunkFlags::Mandatory, 4u);
        return writer.Build();
    }
}
