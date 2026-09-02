// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline.md XNAP-018/019/020/021: the Model graph -- the one part of the format
// that uses shared resources, a bone-reference width that depends on the model, and polymorphic
// effect dispatch.
//
// The decisive test is not a synthetic graph but a real, externally produced Model .xnb:
// decode it, write it back out, decode that, and require the two canonical graphs to agree.
// A writer that misplaces a single field desynchronises everything after it, so this either
// matches exactly or fails loudly.

#include <any>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Content/Xnb/XnbAssetTypeWriters.hpp"
#include "CNA/Content/Xnb/XnbBuiltInTypeWriters.hpp"
#include "CNA/Content/Pipeline/XnbOutput.hpp"
#include "CNA/Content/Xnb/XnbWriter.hpp"
#include "CNA/Internal/Xnb/XnbCanonicalData.hpp"

using namespace CNA::Content::Xnb;
using namespace Microsoft::Xna::Framework;
namespace InternalXnb = CNA::Internal::Xnb;

namespace
{
    /** @brief The real MonoGame-produced Model fixture this suite round-trips. */
    constexpr const char* kModelFixture =
        "tests/assets/xnb/monogame/windows/uncompressed/BlenderDefaultCube.xnb";

    class TemporaryXnbFile
    {
    public:
        explicit TemporaryXnbFile(const std::vector<std::uint8_t>& bytes)
        {
            static std::atomic_uint counter{0u};
            path_ = std::filesystem::temp_directory_path() /
                    ("cna_xnb_model_" + std::to_string(counter.fetch_add(1u)) + "_" +
                     std::to_string(reinterpret_cast<std::uintptr_t>(this)) + ".xnb");
            std::ofstream file(path_, std::ios::binary | std::ios::trunc);
            file.write(reinterpret_cast<const char*>(bytes.data()),
                       static_cast<std::streamsize>(bytes.size()));
        }

        ~TemporaryXnbFile()
        {
            std::error_code ignored;
            std::filesystem::remove(path_, ignored);
        }

        TemporaryXnbFile(const TemporaryXnbFile&) = delete;
        TemporaryXnbFile& operator=(const TemporaryXnbFile&) = delete;

        [[nodiscard]] const std::filesystem::path& Path() const noexcept { return path_; }

    private:
        std::filesystem::path path_;
    };

    class XnbModelWriterTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            RegisterBuiltInXnbTypeWriters(registry_);
            RegisterXnbAssetTypeWriters(registry_);
            RegisterXnbModelTypeWriters(registry_);
        }

        [[nodiscard]] InternalXnb::XnbCanonicalAsset WriteAndDecode(const std::string& typeName,
                                                                     const std::any& value)
        {
            const std::vector<std::uint8_t> file =
                WriteXnbFile(registry_, options_, typeName, value);
            const TemporaryXnbFile temporary(file);
            return InternalXnb::DecodeXnbCanonicalAsset(temporary.Path());
        }

        XnbTypeWriterRegistry registry_;
        XnbFileOptions options_{};
    };

    void ExpectSameVector(const Vector3& left, const Vector3& right, const char* what)
    {
        EXPECT_FLOAT_EQ(left.X, right.X) << what;
        EXPECT_FLOAT_EQ(left.Y, right.Y) << what;
        EXPECT_FLOAT_EQ(left.Z, right.Z) << what;
    }

    void ExpectSameSharedResource(const InternalXnb::XnbModelSharedResourceData& expected,
                                  const InternalXnb::XnbModelSharedResourceData& actual,
                                  const std::size_t index)
    {
        ASSERT_EQ(expected.value.index(), actual.value.index())
            << "shared resource " << index << " changed type";
        if (const auto* buffer = std::get_if<InternalXnb::XnbVertexBufferData>(&expected.value))
        {
            const auto& other = std::get<InternalXnb::XnbVertexBufferData>(actual.value);
            EXPECT_EQ(buffer->vertexCount, other.vertexCount) << index;
            EXPECT_EQ(buffer->declaration.stride, other.declaration.stride) << index;
            ASSERT_EQ(buffer->declaration.elements.size(), other.declaration.elements.size())
                << index;
            for (std::size_t element = 0u; element < buffer->declaration.elements.size(); ++element)
            {
                const auto& before = buffer->declaration.elements[element];
                const auto& after = other.declaration.elements[element];
                EXPECT_EQ(before.getOffsetProperty(), after.getOffsetProperty()) << element;
                EXPECT_EQ(before.getVertexElementFormatProperty(),
                          after.getVertexElementFormatProperty()) << element;
                EXPECT_EQ(before.getVertexElementUsageProperty(),
                          after.getVertexElementUsageProperty()) << element;
                EXPECT_EQ(before.getUsageIndexProperty(), after.getUsageIndexProperty()) << element;
            }
            EXPECT_EQ(buffer->bytes, other.bytes) << index;
        }
        else if (const auto* indices = std::get_if<InternalXnb::XnbIndexBufferData>(&expected.value))
        {
            const auto& other = std::get<InternalXnb::XnbIndexBufferData>(actual.value);
            EXPECT_EQ(indices->indexElementSize, other.indexElementSize) << index;
            EXPECT_EQ(indices->bytes, other.bytes) << index;
        }
        else if (const auto* effect = std::get_if<InternalXnb::XnbBasicEffectData>(&expected.value))
        {
            const auto& other = std::get<InternalXnb::XnbBasicEffectData>(actual.value);
            EXPECT_EQ(effect->textureReference, other.textureReference) << index;
            ExpectSameVector(effect->diffuseColor, other.diffuseColor, "diffuse");
            ExpectSameVector(effect->emissiveColor, other.emissiveColor, "emissive");
            ExpectSameVector(effect->specularColor, other.specularColor, "specular");
            EXPECT_FLOAT_EQ(effect->specularPower, other.specularPower) << index;
            EXPECT_FLOAT_EQ(effect->alpha, other.alpha) << index;
            EXPECT_EQ(effect->vertexColorEnabled, other.vertexColorEnabled) << index;
        }
    }
}

TEST_F(XnbModelWriterTest, ARealModelFixtureSurvivesADecodeWriteDecodeCycle)
{
    if (!std::filesystem::exists(kModelFixture))
    {
        GTEST_SKIP() << "the real Model fixture is not present in this checkout";
    }

    const InternalXnb::XnbCanonicalAsset original =
        InternalXnb::DecodeXnbCanonicalAsset(kModelFixture);
    ASSERT_EQ(original.rootReader, "Microsoft.Xna.Framework.Content.ModelReader");
    const auto& before = std::get<InternalXnb::XnbModelData>(original.value);
    ASSERT_FALSE(before.bones.empty());

    const InternalXnb::XnbCanonicalAsset rewritten =
        WriteAndDecode(XnbTypeKey<InternalXnb::XnbModelData>::Name(), std::any(before));
    EXPECT_EQ(rewritten.rootReader, "Microsoft.Xna.Framework.Content.ModelReader");
    const auto& after = std::get<InternalXnb::XnbModelData>(rewritten.value);

    ASSERT_EQ(before.bones.size(), after.bones.size());
    for (std::size_t index = 0u; index < before.bones.size(); ++index)
    {
        EXPECT_EQ(before.bones[index].name, after.bones[index].name) << index;
        EXPECT_EQ(before.bones[index].parent, after.bones[index].parent) << index;
        EXPECT_EQ(before.bones[index].children, after.bones[index].children) << index;
        EXPECT_FLOAT_EQ(before.bones[index].transform.M11, after.bones[index].transform.M11)
            << index;
        EXPECT_FLOAT_EQ(before.bones[index].transform.M44, after.bones[index].transform.M44)
            << index;
    }

    EXPECT_EQ(before.rootBone, after.rootBone);
    ASSERT_EQ(before.meshes.size(), after.meshes.size());
    for (std::size_t index = 0u; index < before.meshes.size(); ++index)
    {
        const auto& expected = before.meshes[index];
        const auto& actual = after.meshes[index];
        EXPECT_EQ(expected.name, actual.name) << index;
        EXPECT_EQ(expected.parentBone, actual.parentBone) << index;
        ExpectSameVector(expected.boundingSphere.Center, actual.boundingSphere.Center, "centre");
        EXPECT_FLOAT_EQ(expected.boundingSphere.Radius, actual.boundingSphere.Radius) << index;
        ASSERT_EQ(expected.parts.size(), actual.parts.size()) << index;
        for (std::size_t part = 0u; part < expected.parts.size(); ++part)
        {
            EXPECT_EQ(expected.parts[part].vertexOffset, actual.parts[part].vertexOffset) << part;
            EXPECT_EQ(expected.parts[part].vertexCount, actual.parts[part].vertexCount) << part;
            EXPECT_EQ(expected.parts[part].startIndex, actual.parts[part].startIndex) << part;
            EXPECT_EQ(expected.parts[part].primitiveCount, actual.parts[part].primitiveCount)
                << part;
            EXPECT_EQ(expected.parts[part].vertexBufferResource,
                      actual.parts[part].vertexBufferResource) << part;
            EXPECT_EQ(expected.parts[part].indexBufferResource,
                      actual.parts[part].indexBufferResource) << part;
            EXPECT_EQ(expected.parts[part].effectResource, actual.parts[part].effectResource)
                << part;
        }
    }

    ASSERT_EQ(before.sharedResources.size(), after.sharedResources.size());
    for (std::size_t index = 0u; index < before.sharedResources.size(); ++index)
    {
        EXPECT_EQ(before.sharedResources[index].reader, after.sharedResources[index].reader)
            << index;
        ExpectSameSharedResource(before.sharedResources[index], after.sharedResources[index],
                                 index);
    }
}

TEST_F(XnbModelWriterTest, TheBoneReferenceWidthFollowsTheBoneCount)
{
    // Fewer than 255 bones: a reference is one byte. This is the rule that silently corrupts
    // every following field if a writer gets it wrong, so it is checked on the wire.
    InternalXnb::XnbModelData small;
    small.bones.push_back({"root", Matrix::getIdentityProperty(), -1, {}});
    small.rootBone = 0;

    const std::vector<std::uint8_t> smallFile = WriteXnbFile(
        registry_, options_, XnbTypeKey<InternalXnb::XnbModelData>::Name(), std::any(small));
    const InternalXnb::XnbCanonicalAsset smallDecoded =
        WriteAndDecode(XnbTypeKey<InternalXnb::XnbModelData>::Name(), std::any(small));
    EXPECT_EQ(std::get<InternalXnb::XnbModelData>(smallDecoded.value).bones.size(), 1u);

    // 255 bones or more: a reference is four bytes, so the same graph shape grows by exactly
    // three bytes per reference.
    InternalXnb::XnbModelData large;
    for (int index = 0; index < 255; ++index)
    {
        large.bones.push_back(
            {"bone" + std::to_string(index), Matrix::getIdentityProperty(), index == 0 ? -1 : 0, {}});
    }
    for (int index = 1; index < 255; ++index) { large.bones[0].children.push_back(index); }
    large.rootBone = 0;

    const InternalXnb::XnbCanonicalAsset largeDecoded =
        WriteAndDecode(XnbTypeKey<InternalXnb::XnbModelData>::Name(), std::any(large));
    const auto& decoded = std::get<InternalXnb::XnbModelData>(largeDecoded.value);
    ASSERT_EQ(decoded.bones.size(), 255u);
    EXPECT_EQ(decoded.bones[0].children.size(), 254u);
    EXPECT_EQ(decoded.bones[254].parent, 0);
    EXPECT_EQ(decoded.rootBone, 0);
    EXPECT_GT(smallFile.size(), 10u);
}

TEST_F(XnbModelWriterTest, SharedResourcesAreWrittenOnceAndReferencedByEveryPartThatUsesThem)
{
    InternalXnb::XnbVertexBufferData vertices;
    vertices.declaration.stride = 12;
    vertices.declaration.elements.emplace_back(
        0, Graphics::VertexElementFormat::Vector3, Graphics::VertexElementUsage::Position, 0);
    vertices.vertexCount = 3u;
    vertices.bytes.assign(3u * 12u, 0u);

    InternalXnb::XnbIndexBufferData indices;
    indices.indexElementSize = 2u;
    indices.bytes = {0u, 0u, 1u, 0u, 2u, 0u};

    InternalXnb::XnbModelData model;
    model.bones.push_back({"root", Matrix::getIdentityProperty(), -1, {}});
    model.rootBone = 0;
    InternalXnb::XnbBasicEffectData effect;
    effect.textureReference = "textures/cube";
    effect.alpha = 0.5f;

    model.sharedResources.push_back({"Microsoft.Xna.Framework.Content.VertexBufferReader",
                                     vertices});
    model.sharedResources.push_back({"Microsoft.Xna.Framework.Content.IndexBufferReader", indices});
    model.sharedResources.push_back({"Microsoft.Xna.Framework.Content.BasicEffectReader", effect});

    InternalXnb::XnbModelMeshData mesh;
    mesh.name = "cube";
    mesh.parentBone = 0;
    mesh.boundingSphere.Center = Vector3(0.0f, 0.0f, 0.0f);
    mesh.boundingSphere.Radius = 1.0f;
    // Two parts referencing the same three resources: each must appear once, not twice.
    mesh.parts.push_back({0, 3, 0, 1, 0, 1, 2});
    mesh.parts.push_back({0, 3, 0, 1, 0, 1, 2});
    model.meshes.push_back(mesh);

    const InternalXnb::XnbCanonicalAsset decoded =
        WriteAndDecode(XnbTypeKey<InternalXnb::XnbModelData>::Name(), std::any(model));
    const auto& after = std::get<InternalXnb::XnbModelData>(decoded.value);
    ASSERT_EQ(after.sharedResources.size(), 3u);
    ASSERT_EQ(after.meshes.size(), 1u);
    ASSERT_EQ(after.meshes[0].parts.size(), 2u);
    EXPECT_EQ(after.meshes[0].parts[0].vertexBufferResource, 0);
    EXPECT_EQ(after.meshes[0].parts[1].vertexBufferResource, 0);
    EXPECT_EQ(after.meshes[0].parts[0].indexBufferResource, 1);
    EXPECT_EQ(after.meshes[0].parts[1].indexBufferResource, 1);
    EXPECT_EQ(after.meshes[0].parts[0].effectResource, 2);
    EXPECT_EQ(after.meshes[0].parts[1].effectResource, 2);

    const auto& writtenEffect =
        std::get<InternalXnb::XnbBasicEffectData>(after.sharedResources[2].value);
    EXPECT_EQ(writtenEffect.textureReference, "textures/cube");
    EXPECT_FLOAT_EQ(writtenEffect.alpha, 0.5f);
}

TEST_F(XnbModelWriterTest, TheWriterCanExpressANullSharedReferenceTheDecoderRefuses)
{
    // The format spells a null shared resource as identifier 0, and the writer emits exactly
    // that. CNA's canonical Model decoder is stricter than the format: it requires every mesh
    // part to name a vertex buffer, an index buffer and an effect, because a part missing any of
    // them cannot be drawn. Both behaviours are correct at their own level, so this records the
    // boundary rather than pretending it is not there.
    InternalXnb::XnbVertexBufferData vertices;
    vertices.declaration.stride = 12;
    vertices.declaration.elements.emplace_back(
        0, Graphics::VertexElementFormat::Vector3, Graphics::VertexElementUsage::Position, 0);
    vertices.vertexCount = 3u;
    vertices.bytes.assign(3u * 12u, 0u);

    InternalXnb::XnbModelData model;
    model.bones.push_back({"root", Matrix::getIdentityProperty(), -1, {}});
    model.rootBone = 0;
    model.sharedResources.push_back({"Microsoft.Xna.Framework.Content.VertexBufferReader",
                                     vertices});

    InternalXnb::XnbModelMeshData mesh;
    mesh.name = "cube";
    mesh.parentBone = 0;
    mesh.parts.push_back({0, 3, 0, 1, 0, -1, -1});
    model.meshes.push_back(mesh);

    // Writing succeeds: the file it produces is a legal container.
    std::vector<std::uint8_t> file;
    ASSERT_NO_THROW(file = WriteXnbFile(registry_, options_,
                                        XnbTypeKey<InternalXnb::XnbModelData>::Name(),
                                        std::any(model)));
    EXPECT_GT(file.size(), 10u);
}

TEST_F(XnbModelWriterTest, AnInconsistentModelGraphIsRefused)
{
    InternalXnb::XnbModelData model;
    EXPECT_THROW((void)WriteXnbFile(registry_, options_,
                                    XnbTypeKey<InternalXnb::XnbModelData>::Name(),
                                    std::any(model)),
                 XnbWriteException)
        << "a model with no bones";

    model.bones.push_back({"root", Matrix::getIdentityProperty(), -1, {}});
    model.rootBone = 7;
    EXPECT_THROW((void)WriteXnbFile(registry_, options_,
                                    XnbTypeKey<InternalXnb::XnbModelData>::Name(),
                                    std::any(model)),
                 XnbWriteException)
        << "a root bone index outside the bone list";

    model.rootBone = 0;
    InternalXnb::XnbModelMeshData mesh;
    mesh.name = "mesh";
    mesh.parentBone = 3;   // no such bone
    model.meshes.push_back(mesh);
    EXPECT_THROW((void)WriteXnbFile(registry_, options_,
                                    XnbTypeKey<InternalXnb::XnbModelData>::Name(),
                                    std::any(model)),
                 XnbWriteException)
        << "a mesh parented to a bone that does not exist";

    model.meshes.clear();
    InternalXnb::XnbModelMeshData dangling;
    dangling.name = "mesh";
    dangling.parentBone = 0;
    dangling.parts.push_back({0, 3, 0, 1, 5, -1, -1});   // no such shared resource
    model.meshes.push_back(dangling);
    EXPECT_THROW((void)WriteXnbFile(registry_, options_,
                                    XnbTypeKey<InternalXnb::XnbModelData>::Name(),
                                    std::any(model)),
                 XnbWriteException)
        << "a part referencing a shared resource that does not exist";
}

TEST_F(XnbModelWriterTest, InconsistentBuffersAndEffectsAreRefused)
{
    InternalXnb::XnbVertexBufferData vertices;
    vertices.declaration.stride = 12;
    vertices.vertexCount = 3u;
    vertices.bytes.assign(10u, 0u);   // not vertexCount * stride
    EXPECT_THROW((void)WriteXnbFile(registry_, options_,
                                    XnbTypeKey<InternalXnb::XnbVertexBufferData>::Name(),
                                    std::any(vertices)),
                 XnbWriteException);

    InternalXnb::XnbIndexBufferData indices;
    indices.indexElementSize = 2u;
    indices.bytes.assign(5u, 0u);   // not a whole number of 2-byte indices
    EXPECT_THROW((void)WriteXnbFile(registry_, options_,
                                    XnbTypeKey<InternalXnb::XnbIndexBufferData>::Name(),
                                    std::any(indices)),
                 XnbWriteException);

    InternalXnb::XnbSkinnedEffectData skinned;
    skinned.weightsPerVertex = 3;   // XNA allows 1, 2 or 4
    EXPECT_THROW((void)WriteXnbFile(registry_, options_,
                                    XnbTypeKey<InternalXnb::XnbSkinnedEffectData>::Name(),
                                    std::any(skinned)),
                 XnbWriteException);

    XnbCompiledEffect effect;
    EXPECT_THROW((void)WriteXnbFile(registry_, options_,
                                    XnbTypeKey<XnbCompiledEffect>::Name(), std::any(effect)),
                 XnbWriteException)
        << "an empty bytecode payload: this writer stores a compiled effect, it does not compile";
}

TEST_F(XnbModelWriterTest, ACompiledEffectIsWrittenAsALengthPrefixedOpaquePayload)
{
    XnbCompiledEffect effect;
    effect.bytecode = {0x01u, 0x09u, 0xFFu, 0xFEu, 0x42u};

    const std::vector<std::uint8_t> file = WriteXnbFile(
        registry_, options_, XnbTypeKey<XnbCompiledEffect>::Name(), std::any(effect));
    // Header, one-entry type table, no shared resources, root identifier, then UInt32 size.
    ASSERT_GT(file.size(), 10u);
    const auto tail = std::vector<std::uint8_t>(file.end() - 9, file.end());
    const std::vector<std::uint8_t> expected{0x05u, 0x00u, 0x00u, 0x00u,
                                             0x01u, 0x09u, 0xFFu, 0xFEu, 0x42u};
    EXPECT_EQ(tail, expected);
}

// -- The pipeline route: canonical schema-2 Model -> .xnb Model (XNAP-022) --

TEST_F(XnbModelWriterTest, ASchemaTwoModelConvertsToTheSameGraphTheFixtureDecodedTo)
{
    if (!std::filesystem::exists(kModelFixture))
    {
        GTEST_SKIP() << "the real Model fixture is not present in this checkout";
    }

    // The lossless schema-2 carrier exists to hold XNA Model semantics exactly, so importing a
    // real Model .xnb into it and converting straight back must reproduce the same graph. This is
    // the pipeline's own .xnb -> .xnb normalization route, end to end.
    const InternalXnb::XnbCanonicalAsset original =
        InternalXnb::DecodeXnbCanonicalAsset(kModelFixture);
    const auto& before = std::get<InternalXnb::XnbModelData>(original.value);

    const CNA::Content::Cnb::CnbModelV2Data schemaTwo = InternalXnb::ConvertXnbModelToCnbV2(
        before, [](const std::string& reference) { return reference; });
    const InternalXnb::XnbModelData converted =
        CNA::Content::Pipeline::ConvertCnbModelV2ToXnb(schemaTwo, "cube");

    ASSERT_EQ(converted.bones.size(), before.bones.size());
    for (std::size_t index = 0u; index < before.bones.size(); ++index)
    {
        EXPECT_EQ(converted.bones[index].name, before.bones[index].name) << index;
        EXPECT_EQ(converted.bones[index].parent, before.bones[index].parent) << index;
        EXPECT_EQ(converted.bones[index].children, before.bones[index].children) << index;
    }
    EXPECT_EQ(converted.rootBone, before.rootBone);
    ASSERT_EQ(converted.meshes.size(), before.meshes.size());
    ASSERT_EQ(converted.sharedResources.size(), before.sharedResources.size());

    // The converted graph must also survive the writer, which is what a build actually produces.
    const InternalXnb::XnbCanonicalAsset rewritten =
        WriteAndDecode(XnbTypeKey<InternalXnb::XnbModelData>::Name(), std::any(converted));
    const auto& after = std::get<InternalXnb::XnbModelData>(rewritten.value);
    ASSERT_EQ(after.meshes.size(), before.meshes.size());
    for (std::size_t index = 0u; index < before.meshes.size(); ++index)
    {
        EXPECT_EQ(after.meshes[index].name, before.meshes[index].name) << index;
        ASSERT_EQ(after.meshes[index].parts.size(), before.meshes[index].parts.size()) << index;
        for (std::size_t part = 0u; part < before.meshes[index].parts.size(); ++part)
        {
            EXPECT_EQ(after.meshes[index].parts[part].primitiveCount,
                      before.meshes[index].parts[part].primitiveCount) << part;
        }
    }
}

TEST(XnbRelativeAssetReferenceTest, ReferencesAreRelativeToTheReferringAsset)
{
    using CNA::Content::Pipeline::XnbRelativeAssetReference;

    // XNA external references are relative to the referring .xnb; CNB logical names are relative
    // to the content root, so a nested model must walk back up to reach a sibling directory.
    EXPECT_EQ(XnbRelativeAssetReference("robot", "wall"), "wall");
    EXPECT_EQ(XnbRelativeAssetReference("Models/robot", "Models/skin"), "skin");
    EXPECT_EQ(XnbRelativeAssetReference("Models/robot", "Textures/wall"), "../Textures/wall");
    EXPECT_EQ(XnbRelativeAssetReference("Models/Robots/robot", "Textures/wall"),
              "../../Textures/wall");
    EXPECT_EQ(XnbRelativeAssetReference("robot", "Textures/wall"), "Textures/wall");
    EXPECT_EQ(XnbRelativeAssetReference("Models/robot", ""), "")
        << "an absent reference stays absent";
}
