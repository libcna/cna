// SPDX-License-Identifier: MS-PL
#include "CNA/Content/Cnb/CnbModelV2Codec.hpp"

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
        constexpr std::uint32_t MaxVertexStride = 4096u;
        constexpr std::uint32_t MaxUsageIndex = 31u;

        [[noreturn]] void Fail(const std::string& detail)
        {
            throw ContentLoadException("CNB Model schema 2: " + detail + ".");
        }

        std::uint32_t ToU32(const std::size_t value, const char* what)
        {
            if (value > std::numeric_limits<std::uint32_t>::max())
            {
                Fail(std::string(what) + " exceeds the u32 wire range");
            }
            return static_cast<std::uint32_t>(value);
        }

        std::uint64_t CheckedMultiply(const std::uint64_t left, const std::uint64_t right,
                                      const std::string& what)
        {
            if (left != 0u && right > std::numeric_limits<std::uint64_t>::max() / left)
            {
                Fail(what + " overflows its byte/count product");
            }
            return left * right;
        }

        std::uint64_t CheckedAdd(const std::uint64_t left, const std::uint64_t right,
                                 const std::string& what)
        {
            if (right > std::numeric_limits<std::uint64_t>::max() - left)
            {
                Fail(what + " overflows its range end");
            }
            return left + right;
        }

        template<std::size_t N>
        bool AllFinite(const std::array<float, N>& values)
        {
            return std::all_of(values.begin(), values.end(), [](const float value)
            {
                return std::isfinite(value);
            });
        }

        template<std::size_t N>
        bool AllPositiveZero(const std::array<float, N>& values)
        {
            return std::all_of(values.begin(), values.end(), [](const float value)
            {
                return value == 0.0f && !std::signbit(value);
            });
        }

        bool IsPositiveZero(const float value)
        {
            return value == 0.0f && !std::signbit(value);
        }

        std::uint32_t VertexFormatSize(const CnbModelV2VertexFormat format)
        {
            switch (format)
            {
                case CnbModelV2VertexFormat::Single: return 4u;
                case CnbModelV2VertexFormat::Vector2: return 8u;
                case CnbModelV2VertexFormat::Vector3: return 12u;
                case CnbModelV2VertexFormat::Vector4: return 16u;
                case CnbModelV2VertexFormat::Color: return 4u;
                case CnbModelV2VertexFormat::Byte4: return 4u;
                case CnbModelV2VertexFormat::Short2: return 4u;
                case CnbModelV2VertexFormat::Short4: return 8u;
                case CnbModelV2VertexFormat::NormalizedShort2: return 4u;
                case CnbModelV2VertexFormat::NormalizedShort4: return 8u;
                case CnbModelV2VertexFormat::HalfVector2: return 4u;
                case CnbModelV2VertexFormat::HalfVector4: return 8u;
            }
            return 0u;
        }

        bool ValidUsage(const CnbModelV2VertexUsage usage)
        {
            return static_cast<std::uint32_t>(usage) <=
                   static_cast<std::uint32_t>(CnbModelV2VertexUsage::TessellateFactor);
        }

        std::uint32_t ReadIndex(const CnbModelV2IndexBuffer& buffer, const std::uint32_t index)
        {
            const std::size_t offset = static_cast<std::size_t>(index) * buffer.indexElementSize;
            if (buffer.indexElementSize == 2u)
            {
                return static_cast<std::uint32_t>(buffer.bytes[offset]) |
                       (static_cast<std::uint32_t>(buffer.bytes[offset + 1u]) << 8u);
            }
            return static_cast<std::uint32_t>(buffer.bytes[offset]) |
                   (static_cast<std::uint32_t>(buffer.bytes[offset + 1u]) << 8u) |
                   (static_cast<std::uint32_t>(buffer.bytes[offset + 2u]) << 16u) |
                   (static_cast<std::uint32_t>(buffer.bytes[offset + 3u]) << 24u);
        }

        void RequireLogicalName(const std::string& value, const std::string& what)
        {
            if (value.empty()) { return; }
            const std::string problem = CnbLogicalNameProblem(value);
            if (!problem.empty()) { Fail(what + " is " + problem); }
        }

        void ValidateEffect(const CnbModelV2Effect& effect, const std::size_t index)
        {
            const std::string where = "effect " + std::to_string(index);
            RequireLogicalName(effect.primaryTexture, where + " primary texture");
            RequireLogicalName(effect.secondaryTexture, where + " secondary texture");
            RequireLogicalName(effect.cubeTexture, where + " cube texture");
            if (!AllFinite(effect.diffuse) || !AllFinite(effect.emissive) ||
                !AllFinite(effect.specular) || !std::isfinite(effect.specularPower) ||
                !std::isfinite(effect.alpha) || !std::isfinite(effect.environmentMapAmount) ||
                !std::isfinite(effect.fresnelFactor))
            {
                Fail(where + " contains a non-finite material value");
            }

            const auto requireZeroCommon = [&]
            {
                if (!effect.secondaryTexture.empty() || !effect.cubeTexture.empty() ||
                    effect.weightsPerVertex != 0u || effect.alphaFunction != 0u ||
                    effect.referenceAlpha != 0u || !IsPositiveZero(effect.environmentMapAmount) ||
                    !IsPositiveZero(effect.fresnelFactor))
                {
                    Fail(where + " has noncanonical inactive fields");
                }
            };

            switch (effect.kind)
            {
                case CnbModelV2EffectKind::BasicEffect:
                    requireZeroCommon();
                    return;
                case CnbModelV2EffectKind::SkinnedEffect:
                    if (!effect.secondaryTexture.empty() || !effect.cubeTexture.empty() ||
                        effect.alphaFunction != 0u || effect.referenceAlpha != 0u ||
                        !IsPositiveZero(effect.environmentMapAmount) ||
                        !IsPositiveZero(effect.fresnelFactor) || effect.vertexColorEnabled ||
                        (effect.weightsPerVertex != 1u && effect.weightsPerVertex != 2u &&
                         effect.weightsPerVertex != 4u))
                    {
                        Fail(where + " has invalid SkinnedEffect-only fields");
                    }
                    return;
                case CnbModelV2EffectKind::DualTextureEffect:
                    if (!effect.cubeTexture.empty() || !AllPositiveZero(effect.emissive) ||
                        !AllPositiveZero(effect.specular) || !IsPositiveZero(effect.specularPower) ||
                        !IsPositiveZero(effect.environmentMapAmount) ||
                        !IsPositiveZero(effect.fresnelFactor) || effect.weightsPerVertex != 0u ||
                        effect.alphaFunction != 0u || effect.referenceAlpha != 0u)
                    {
                        Fail(where + " has noncanonical inactive DualTextureEffect fields");
                    }
                    return;
                case CnbModelV2EffectKind::AlphaTestEffect:
                    if (!effect.secondaryTexture.empty() || !effect.cubeTexture.empty() ||
                        !AllPositiveZero(effect.emissive) || !AllPositiveZero(effect.specular) ||
                        !IsPositiveZero(effect.specularPower) ||
                        !IsPositiveZero(effect.environmentMapAmount) ||
                        !IsPositiveZero(effect.fresnelFactor) || effect.weightsPerVertex != 0u ||
                        effect.alphaFunction > 7u)
                    {
                        Fail(where + " has invalid or noncanonical AlphaTestEffect fields");
                    }
                    return;
                case CnbModelV2EffectKind::EnvironmentMapEffect:
                    if (!effect.secondaryTexture.empty() || effect.vertexColorEnabled ||
                        !IsPositiveZero(effect.specularPower) || effect.weightsPerVertex != 0u ||
                        effect.alphaFunction != 0u || effect.referenceAlpha != 0u)
                    {
                        Fail(where + " has noncanonical inactive EnvironmentMapEffect fields");
                    }
                    return;
            }
            Fail(where + " has an unknown stock-effect kind");
        }

        void ValidateModel(const CnbModelV2Data& model)
        {
            if (model.bones.empty()) { Fail("the bone table is empty"); }
            if (model.rootBone >= model.bones.size()) { Fail("rootBone is out of range"); }

            for (std::size_t index = 0u; index < model.bones.size(); ++index)
            {
                const CnbModelV2Bone& bone = model.bones[index];
                if (!CnbByteReader::IsWellFormedUtf8(bone.name))
                {
                    Fail("bone " + std::to_string(index) + " has an invalid UTF-8 name");
                }
                if (bone.parent < -1 ||
                    (bone.parent >= 0 && static_cast<std::size_t>(bone.parent) >= index))
                {
                    Fail("bone " + std::to_string(index) +
                         " does not name an earlier parent or -1");
                }
                if (!AllFinite(bone.transform))
                {
                    Fail("bone " + std::to_string(index) + " has a non-finite transform");
                }
            }
            if (model.bones[model.rootBone].parent != -1)
            {
                Fail("rootBone does not name a parentless bone");
            }

            for (std::size_t declarationIndex = 0u;
                 declarationIndex < model.vertexDeclarations.size(); ++declarationIndex)
            {
                const CnbModelV2VertexDeclaration& declaration =
                    model.vertexDeclarations[declarationIndex];
                const std::string where =
                    "vertex declaration " + std::to_string(declarationIndex);
                if (declaration.vertexStride == 0u || declaration.vertexStride > MaxVertexStride)
                {
                    Fail(where + " has an invalid stride");
                }
                if (declaration.elements.empty()) { Fail(where + " has no elements"); }
                std::vector<std::pair<std::uint32_t, std::uint32_t>> ranges;
                std::set<std::pair<std::uint32_t, std::uint32_t>> semantics;
                ranges.reserve(declaration.elements.size());
                for (const CnbModelV2VertexElement& element : declaration.elements)
                {
                    const std::uint32_t size = VertexFormatSize(element.format);
                    if (size == 0u || !ValidUsage(element.usage) ||
                        element.usageIndex > MaxUsageIndex)
                    {
                        Fail(where + " has an invalid format, usage, or usage index");
                    }
                    const std::uint64_t end = CheckedAdd(element.offset, size, where);
                    if (end > declaration.vertexStride)
                    {
                        Fail(where + " has an element outside its stride");
                    }
                    if (!semantics.insert({static_cast<std::uint32_t>(element.usage),
                                           element.usageIndex}).second)
                    {
                        Fail(where + " repeats a usage and usage index");
                    }
                    ranges.emplace_back(element.offset, static_cast<std::uint32_t>(end));
                }
                std::sort(ranges.begin(), ranges.end());
                for (std::size_t element = 1u; element < ranges.size(); ++element)
                {
                    if (ranges[element].first < ranges[element - 1u].second)
                    {
                        Fail(where + " has overlapping elements");
                    }
                }
            }

            if (model.vertexDeclarations.empty() != model.vertexBuffers.empty())
            {
                Fail("vertex declaration and vertex-buffer emptiness disagree");
            }
            for (std::size_t index = 0u; index < model.vertexBuffers.size(); ++index)
            {
                const CnbModelV2VertexBuffer& buffer = model.vertexBuffers[index];
                if (buffer.declaration >= model.vertexDeclarations.size() ||
                    buffer.vertexCount == 0u)
                {
                    Fail("vertex buffer " + std::to_string(index) +
                         " has an invalid declaration or count");
                }
                const std::uint64_t expected = CheckedMultiply(
                    model.vertexDeclarations[buffer.declaration].vertexStride,
                    buffer.vertexCount, "vertex buffer " + std::to_string(index));
                if (expected != buffer.bytes.size())
                {
                    Fail("vertex buffer " + std::to_string(index) +
                         " byte size disagrees with its declaration and count");
                }
            }
            for (std::size_t index = 0u; index < model.indexBuffers.size(); ++index)
            {
                const CnbModelV2IndexBuffer& buffer = model.indexBuffers[index];
                if ((buffer.indexElementSize != 2u && buffer.indexElementSize != 4u) ||
                    buffer.indexCount == 0u)
                {
                    Fail("index buffer " + std::to_string(index) +
                         " has an invalid width or count");
                }
                const std::uint64_t expected = CheckedMultiply(
                    buffer.indexElementSize, buffer.indexCount,
                    "index buffer " + std::to_string(index));
                if (expected != buffer.bytes.size())
                {
                    Fail("index buffer " + std::to_string(index) +
                         " byte size disagrees with its width and count");
                }
            }
            for (std::size_t index = 0u; index < model.effects.size(); ++index)
            {
                ValidateEffect(model.effects[index], index);
            }

            std::size_t flatPartIndex = 0u;
            for (std::size_t meshIndex = 0u; meshIndex < model.meshes.size(); ++meshIndex)
            {
                const CnbModelV2Mesh& mesh = model.meshes[meshIndex];
                if (!CnbByteReader::IsWellFormedUtf8(mesh.name))
                {
                    Fail("mesh " + std::to_string(meshIndex) + " has an invalid UTF-8 name");
                }
                if (mesh.parentBone < 0 ||
                    static_cast<std::size_t>(mesh.parentBone) >= model.bones.size())
                {
                    Fail("mesh " + std::to_string(meshIndex) + " has an invalid parent bone");
                }
                if (!AllFinite(mesh.boundingSphere) || mesh.boundingSphere[3] < 0.0f)
                {
                    Fail("mesh " + std::to_string(meshIndex) +
                         " has an invalid bounding sphere");
                }
                for (const CnbModelV2Part& part : mesh.parts)
                {
                    const std::string where = "part " + std::to_string(flatPartIndex);
                    ++flatPartIndex;
                    if (part.vertexBuffer >= model.vertexBuffers.size() ||
                        part.indexBuffer >= model.indexBuffers.size() ||
                        part.effect >= model.effects.size())
                    {
                        Fail(where + " has an out-of-range resource index");
                    }
                    if (part.numVertices == 0u || part.primitiveCount == 0u)
                    {
                        Fail(where + " has an empty draw window");
                    }
                    const CnbModelV2VertexBuffer& vertex =
                        model.vertexBuffers[part.vertexBuffer];
                    const CnbModelV2IndexBuffer& indices =
                        model.indexBuffers[part.indexBuffer];
                    if (CheckedAdd(part.vertexOffset, part.numVertices, where) >
                        vertex.vertexCount)
                    {
                        Fail(where + " exceeds its vertex buffer");
                    }
                    const std::uint64_t selectedIndexCount =
                        CheckedMultiply(part.primitiveCount, 3u, where);
                    if (CheckedAdd(part.startIndex, selectedIndexCount, where) >
                        indices.indexCount)
                    {
                        Fail(where + " exceeds its index buffer");
                    }
                    for (std::uint64_t selected = 0u; selected < selectedIndexCount; ++selected)
                    {
                        const std::uint32_t value = ReadIndex(
                            indices, part.startIndex + static_cast<std::uint32_t>(selected));
                        if (value >= part.numVertices)
                        {
                            Fail(where + " selects an index outside NumVertices");
                        }
                    }
                }
            }
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

        std::array<float, 3> ReadArray3(CnbByteReader& reader)
        {
            return {{reader.ReadF32(), reader.ReadF32(), reader.ReadF32()}};
        }

        std::string ResolveXref(const CnbDocument& document, const std::uint32_t index,
                                const std::uint32_t expectedType, const char* what)
        {
            if (index == CnbModelV2NoIndex) { return {}; }
            const CnbExternalReference& reference = document.ExternalReferenceAt(index, what);
            if (reference.expectedAssetTypeId != expectedType)
            {
                Fail(std::string(what) + " has the wrong expected asset type");
            }
            return reference.logicalName;
        }

        void RequireExactSize(CnbByteReader& reader, const std::uint64_t expected,
                              const char* what)
        {
            if (reader.Size() != expected)
            {
                reader.Fail(std::string("has the wrong size for ") + what + ": expected " +
                            std::to_string(expected) + ", got " +
                            std::to_string(reader.Size()) + ".");
            }
        }

        std::size_t RequireSchemaChunk(const CnbDocument& document, const CnbChunkId type,
                                       const std::uint32_t alignment, const char* what)
        {
            const std::size_t index = document.RequireSingle(type);
            const CnbChunkEntry& entry = document.ChunkAt(index);
            if (!entry.IsMandatory())
            {
                Fail(std::string(what) + " is not marked mandatory");
            }
            if (entry.alignment != alignment)
            {
                Fail(std::string(what) + " has the wrong declared alignment");
            }
            return index;
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

    CnbModelV2Data DecodeModelV2FromCnb(const CnbDocument& document)
    {
        document.RequireAsset(CnbAssetTypeId::Model, CnbModelV2SchemaVersion);
        if (document.AssetSchemaVersion() != CnbModelV2SchemaVersion)
        {
            Fail("decoder requires asset schema version 2 exactly");
        }
        const CnbChunkId known[] = {
            CnbModelV2Chunk::Header, CnbModelV2Chunk::Strings, CnbModelV2Chunk::Bones,
            CnbModelV2Chunk::Meshes, CnbModelV2Chunk::Parts,
            CnbModelV2Chunk::VertexDeclarations, CnbModelV2Chunk::VertexResources,
            CnbModelV2Chunk::VertexData, CnbModelV2Chunk::IndexResources,
            CnbModelV2Chunk::IndexData, CnbModelV2Chunk::Effects};
        document.RequireMandatoryChunksUnderstood(known);

        CnbByteReader header = document.OpenChunk(
            RequireSchemaChunk(document, CnbModelV2Chunk::Header, 4u, "M2HD"));
        RequireExactSize(header, CnbModelV2HeaderStride, "M2HD");
        if (header.ReadU32() != 0u) { header.Fail("sets undefined header flags."); }
        const std::uint32_t boneCount = header.ReadU32();
        const std::uint32_t meshCount = header.ReadU32();
        const std::uint32_t partCount = header.ReadU32();
        const std::uint32_t declarationCount = header.ReadU32();
        const std::uint32_t elementCount = header.ReadU32();
        const std::uint32_t vertexBufferCount = header.ReadU32();
        const std::uint32_t indexBufferCount = header.ReadU32();
        const std::uint32_t effectCount = header.ReadU32();
        const std::uint32_t rootBone = header.ReadU32();
        for (int reserved = 0; reserved < 6; ++reserved)
        {
            if (header.ReadU32() != 0u) { header.Fail("has a nonzero reserved header word."); }
        }
        header.RequireExhausted();

        const std::uint32_t maxElements = document.Limits().maxArrayElementCount;
        if (boneCount == 0u || boneCount > maxElements || meshCount > maxElements ||
            partCount > maxElements || declarationCount > maxElements ||
            elementCount > maxElements || vertexBufferCount > maxElements ||
            indexBufferCount > maxElements || effectCount > maxElements)
        {
            header.Fail("declares an empty bone table or a count above the configured limit.");
        }
        if ((declarationCount == 0u) != (vertexBufferCount == 0u))
        {
            header.Fail("makes declaration and vertex-buffer emptiness disagree.");
        }

        std::vector<std::string> strings;
        {
            CnbByteReader reader = document.OpenChunk(
                RequireSchemaChunk(document, CnbModelV2Chunk::Strings, 4u, "M2ST"));
            const std::uint32_t count = reader.ReadCount(0u, "Model-v2 strings");
            strings.reserve(count);
            for (std::uint32_t index = 0u; index < count; ++index)
            {
                strings.push_back(reader.ReadString());
            }
            reader.RequireExhausted();
        }
        const auto stringAt = [&](CnbByteReader& reader, const std::uint32_t index,
                                  const char* what) -> std::string
        {
            if (index >= strings.size())
            {
                reader.Fail(std::string(what) + " names an out-of-range string.");
            }
            return strings[index];
        };

        CnbModelV2Data model;
        model.rootBone = rootBone;
        {
            CnbByteReader reader = document.OpenChunk(
                RequireSchemaChunk(document, CnbModelV2Chunk::Bones, 4u, "M2BN"));
            RequireExactSize(reader, CheckedMultiply(boneCount, CnbModelV2BoneStride, "M2BN"),
                             "M2BN bone rows");
            model.bones.resize(boneCount);
            for (CnbModelV2Bone& bone : model.bones)
            {
                bone.name = stringAt(reader, reader.ReadU32(), "a bone");
                bone.parent = reader.ReadI32();
                for (float& value : bone.transform) { value = reader.ReadF32(); }
            }
            reader.RequireExhausted();
        }

        struct MeshRow
        {
            std::uint32_t firstPart = 0u;
            std::uint32_t partCount = 0u;
        };
        std::vector<MeshRow> meshRows(meshCount);
        {
            CnbByteReader reader = document.OpenChunk(
                RequireSchemaChunk(document, CnbModelV2Chunk::Meshes, 4u, "M2MS"));
            RequireExactSize(reader, CheckedMultiply(meshCount, CnbModelV2MeshStride, "M2MS"),
                             "M2MS mesh rows");
            model.meshes.resize(meshCount);
            std::uint32_t expectedFirstPart = 0u;
            for (std::uint32_t index = 0u; index < meshCount; ++index)
            {
                CnbModelV2Mesh& mesh = model.meshes[index];
                mesh.name = stringAt(reader, reader.ReadU32(), "a mesh");
                mesh.parentBone = reader.ReadI32();
                for (float& value : mesh.boundingSphere) { value = reader.ReadF32(); }
                meshRows[index].firstPart = reader.ReadU32();
                meshRows[index].partCount = reader.ReadU32();
                if (meshRows[index].firstPart != expectedFirstPart ||
                    meshRows[index].partCount > partCount - expectedFirstPart)
                {
                    reader.Fail("mesh part ranges are not a contiguous partition.");
                }
                expectedFirstPart += meshRows[index].partCount;
            }
            if (expectedFirstPart != partCount)
            {
                reader.Fail("mesh part ranges do not cover the part table.");
            }
            reader.RequireExhausted();
        }

        std::vector<CnbModelV2Part> flatParts(partCount);
        {
            CnbByteReader reader = document.OpenChunk(
                RequireSchemaChunk(document, CnbModelV2Chunk::Parts, 4u, "M2PT"));
            RequireExactSize(reader, CheckedMultiply(partCount, CnbModelV2PartStride, "M2PT"),
                             "M2PT part rows");
            for (CnbModelV2Part& part : flatParts)
            {
                part.vertexOffset = reader.ReadU32();
                part.numVertices = reader.ReadU32();
                part.startIndex = reader.ReadU32();
                part.primitiveCount = reader.ReadU32();
                part.vertexBuffer = reader.ReadU32();
                part.indexBuffer = reader.ReadU32();
                part.effect = reader.ReadU32();
                if (reader.ReadU32() != 0u) { reader.Fail("has a nonzero part reserved word."); }
            }
            reader.RequireExhausted();
        }
        for (std::uint32_t mesh = 0u; mesh < meshCount; ++mesh)
        {
            const MeshRow& row = meshRows[mesh];
            model.meshes[mesh].parts.assign(
                flatParts.begin() + row.firstPart,
                flatParts.begin() + row.firstPart + row.partCount);
        }

        struct DeclarationRow
        {
            std::uint32_t firstElement = 0u;
            std::uint32_t elementCount = 0u;
        };
        std::vector<DeclarationRow> declarationRows(declarationCount);
        {
            CnbByteReader reader = document.OpenChunk(
                RequireSchemaChunk(document, CnbModelV2Chunk::VertexDeclarations, 4u, "M2VD"));
            const std::uint64_t expected = CheckedAdd(
                CheckedMultiply(declarationCount, CnbModelV2DeclarationStride, "M2VD"),
                CheckedMultiply(elementCount, CnbModelV2ElementStride, "M2VD"), "M2VD");
            RequireExactSize(reader, expected, "M2VD declaration and element rows");
            model.vertexDeclarations.resize(declarationCount);
            std::uint32_t expectedFirstElement = 0u;
            for (std::uint32_t index = 0u; index < declarationCount; ++index)
            {
                model.vertexDeclarations[index].vertexStride = reader.ReadU32();
                declarationRows[index].firstElement = reader.ReadU32();
                declarationRows[index].elementCount = reader.ReadU32();
                if (reader.ReadU32() != 0u)
                {
                    reader.Fail("has a nonzero declaration reserved word.");
                }
                if (declarationRows[index].firstElement != expectedFirstElement ||
                    declarationRows[index].elementCount > elementCount - expectedFirstElement)
                {
                    reader.Fail("declaration element ranges are not a contiguous partition.");
                }
                expectedFirstElement += declarationRows[index].elementCount;
            }
            if (expectedFirstElement != elementCount)
            {
                reader.Fail("declaration ranges do not cover the element table.");
            }
            std::vector<CnbModelV2VertexElement> elements(elementCount);
            for (CnbModelV2VertexElement& element : elements)
            {
                element.offset = reader.ReadU32();
                element.format = static_cast<CnbModelV2VertexFormat>(reader.ReadU32());
                element.usage = static_cast<CnbModelV2VertexUsage>(reader.ReadU32());
                element.usageIndex = reader.ReadU32();
                if (reader.ReadU32() != 0u) { reader.Fail("has a nonzero element reserved word."); }
            }
            for (std::uint32_t declaration = 0u; declaration < declarationCount; ++declaration)
            {
                const DeclarationRow& row = declarationRows[declaration];
                model.vertexDeclarations[declaration].elements.assign(
                    elements.begin() + row.firstElement,
                    elements.begin() + row.firstElement + row.elementCount);
            }
            reader.RequireExhausted();
        }

        const std::vector<std::size_t> vertexChunks =
            document.FindAll(CnbModelV2Chunk::VertexData);
        if (vertexChunks.size() != vertexBufferCount)
        {
            Fail("MVTX chunk count disagrees with vertexBufferCount");
        }
        for (const std::size_t index : vertexChunks)
        {
            const CnbChunkEntry& entry = document.ChunkAt(index);
            if (!entry.IsMandatory() || entry.alignment != 16u)
            {
                Fail("an MVTX chunk is not mandatory with alignment 16");
            }
        }
        {
            CnbByteReader reader = document.OpenChunk(
                RequireSchemaChunk(document, CnbModelV2Chunk::VertexResources, 4u, "M2VR"));
            RequireExactSize(reader,
                             CheckedMultiply(vertexBufferCount, CnbModelV2ResourceStride, "M2VR"),
                             "M2VR resource rows");
            model.vertexBuffers.resize(vertexBufferCount);
            for (std::uint32_t index = 0u; index < vertexBufferCount; ++index)
            {
                CnbModelV2VertexBuffer& buffer = model.vertexBuffers[index];
                buffer.declaration = reader.ReadU32();
                buffer.vertexCount = reader.ReadU32();
                if (reader.ReadU32() != index) { reader.Fail("has a wrong MVTX payload ordinal."); }
                if (reader.ReadU32() != 0u) { reader.Fail("has a nonzero M2VR reserved word."); }
                CnbByteReader payload = document.OpenChunk(vertexChunks[index]);
                const std::span<const std::uint8_t> bytes = payload.ReadBytes(payload.Size());
                buffer.bytes.assign(bytes.begin(), bytes.end());
            }
            reader.RequireExhausted();
        }

        const std::vector<std::size_t> indexChunks = document.FindAll(CnbModelV2Chunk::IndexData);
        if (indexChunks.size() != indexBufferCount)
        {
            Fail("MIDX chunk count disagrees with indexBufferCount");
        }
        for (const std::size_t index : indexChunks)
        {
            const CnbChunkEntry& entry = document.ChunkAt(index);
            if (!entry.IsMandatory() || entry.alignment != 16u)
            {
                Fail("a MIDX chunk is not mandatory with alignment 16");
            }
        }
        {
            CnbByteReader reader = document.OpenChunk(
                RequireSchemaChunk(document, CnbModelV2Chunk::IndexResources, 4u, "M2IR"));
            RequireExactSize(reader,
                             CheckedMultiply(indexBufferCount, CnbModelV2ResourceStride, "M2IR"),
                             "M2IR resource rows");
            model.indexBuffers.resize(indexBufferCount);
            for (std::uint32_t index = 0u; index < indexBufferCount; ++index)
            {
                CnbModelV2IndexBuffer& buffer = model.indexBuffers[index];
                buffer.indexElementSize = reader.ReadU32();
                buffer.indexCount = reader.ReadU32();
                if (reader.ReadU32() != index) { reader.Fail("has a wrong MIDX payload ordinal."); }
                if (reader.ReadU32() != 0u) { reader.Fail("has a nonzero M2IR reserved word."); }
                CnbByteReader payload = document.OpenChunk(indexChunks[index]);
                const std::span<const std::uint8_t> bytes = payload.ReadBytes(payload.Size());
                buffer.bytes.assign(bytes.begin(), bytes.end());
            }
            reader.RequireExhausted();
        }

        {
            CnbByteReader reader = document.OpenChunk(
                RequireSchemaChunk(document, CnbModelV2Chunk::Effects, 4u, "M2FX"));
            RequireExactSize(reader,
                             CheckedMultiply(effectCount, CnbModelV2EffectStride, "M2FX"),
                             "M2FX effect rows");
            model.effects.resize(effectCount);
            for (std::uint32_t index = 0u; index < effectCount; ++index)
            {
                CnbModelV2Effect& effect = model.effects[index];
                effect.kind = static_cast<CnbModelV2EffectKind>(reader.ReadU32());
                const std::uint32_t flags = reader.ReadU32();
                const std::uint32_t primary = reader.ReadU32();
                const std::uint32_t secondary = reader.ReadU32();
                const std::uint32_t cube = reader.ReadU32();
                const std::uint32_t integer0 = reader.ReadU32();
                const std::uint32_t integer1 = reader.ReadU32();
                if (reader.ReadU32() != 0u) { reader.Fail("has a nonzero effect reserved word."); }
                const std::array<float, 3> vector0 = ReadArray3(reader);
                const std::array<float, 3> vector1 = ReadArray3(reader);
                const std::array<float, 3> vector2 = ReadArray3(reader);
                const float scalar0 = reader.ReadF32();
                const float scalar1 = reader.ReadF32();
                const float scalar2 = reader.ReadF32();
                const float scalar3 = reader.ReadF32();
                for (int reserved = 0; reserved < 3; ++reserved)
                {
                    if (reader.ReadU32() != 0u)
                    {
                        reader.Fail("has a nonzero trailing effect reserved word.");
                    }
                }
                if (!IsPositiveZero(scalar3))
                {
                    reader.Fail("has a nonzero inactive effect scalar.");
                }

                switch (effect.kind)
                {
                    case CnbModelV2EffectKind::BasicEffect:
                        if (flags > 1u || secondary != CnbModelV2NoIndex ||
                            cube != CnbModelV2NoIndex || integer0 != 0u || integer1 != 0u ||
                            !IsPositiveZero(scalar2))
                        {
                            reader.Fail("has noncanonical BasicEffect fields.");
                        }
                        effect.primaryTexture = ResolveXref(
                            document, primary, CnbAssetTypeId::Texture2D, "BasicEffect texture");
                        effect.diffuse = vector0;
                        effect.emissive = vector1;
                        effect.specular = vector2;
                        effect.specularPower = scalar0;
                        effect.alpha = scalar1;
                        effect.vertexColorEnabled = flags != 0u;
                        break;
                    case CnbModelV2EffectKind::SkinnedEffect:
                        if (flags != 0u || secondary != CnbModelV2NoIndex ||
                            cube != CnbModelV2NoIndex || integer1 != 0u ||
                            (integer0 != 1u && integer0 != 2u && integer0 != 4u) ||
                            !IsPositiveZero(scalar2))
                        {
                            reader.Fail("has noncanonical SkinnedEffect fields.");
                        }
                        effect.primaryTexture = ResolveXref(
                            document, primary, CnbAssetTypeId::Texture2D, "SkinnedEffect texture");
                        effect.weightsPerVertex = integer0;
                        effect.diffuse = vector0;
                        effect.emissive = vector1;
                        effect.specular = vector2;
                        effect.specularPower = scalar0;
                        effect.alpha = scalar1;
                        break;
                    case CnbModelV2EffectKind::DualTextureEffect:
                        if (flags > 1u || cube != CnbModelV2NoIndex || integer0 != 0u ||
                            integer1 != 0u || !AllPositiveZero(vector1) ||
                            !AllPositiveZero(vector2) || !IsPositiveZero(scalar1) ||
                            !IsPositiveZero(scalar2))
                        {
                            reader.Fail("has noncanonical DualTextureEffect fields.");
                        }
                        effect.primaryTexture = ResolveXref(
                            document, primary, CnbAssetTypeId::Texture2D, "DualTexture texture 1");
                        effect.secondaryTexture = ResolveXref(
                            document, secondary, CnbAssetTypeId::Texture2D, "DualTexture texture 2");
                        effect.diffuse = vector0;
                        effect.alpha = scalar0;
                        effect.vertexColorEnabled = flags != 0u;
                        break;
                    case CnbModelV2EffectKind::AlphaTestEffect:
                        if (flags > 1u || secondary != CnbModelV2NoIndex ||
                            cube != CnbModelV2NoIndex || integer0 > 7u ||
                            !AllPositiveZero(vector1) || !AllPositiveZero(vector2) ||
                            !IsPositiveZero(scalar1) || !IsPositiveZero(scalar2))
                        {
                            reader.Fail("has noncanonical AlphaTestEffect fields.");
                        }
                        effect.primaryTexture = ResolveXref(
                            document, primary, CnbAssetTypeId::Texture2D, "AlphaTestEffect texture");
                        effect.alphaFunction = integer0;
                        effect.referenceAlpha = integer1;
                        effect.diffuse = vector0;
                        effect.alpha = scalar0;
                        effect.vertexColorEnabled = flags != 0u;
                        break;
                    case CnbModelV2EffectKind::EnvironmentMapEffect:
                        if (flags != 0u || secondary != CnbModelV2NoIndex || integer0 != 0u ||
                            integer1 != 0u)
                        {
                            reader.Fail("has noncanonical EnvironmentMapEffect fields.");
                        }
                        effect.primaryTexture = ResolveXref(
                            document, primary, CnbAssetTypeId::Texture2D,
                            "EnvironmentMapEffect texture");
                        effect.cubeTexture = ResolveXref(
                            document, cube, CnbAssetTypeId::TextureCube,
                            "EnvironmentMapEffect cube texture");
                        effect.diffuse = vector0;
                        effect.emissive = vector1;
                        effect.specular = vector2;
                        effect.environmentMapAmount = scalar0;
                        effect.fresnelFactor = scalar1;
                        effect.alpha = scalar2;
                        break;
                    default:
                        reader.Fail("has an unknown stock-effect kind.");
                }
            }
            reader.RequireExhausted();
        }

        ValidateModel(model);
        return model;
    }
}
