// SPDX-License-Identifier: MS-PL
//
// TQ-033: a runtime .xnb Model's mesh parts -- their four counts, Tags and the three shared-resource
// references -- built byte by byte: one bone, one mesh, three parts, four shared resources (a
// position-only VertexBuffer, a 32-bit IndexBuffer, two BasicEffects).
//
// Oracle (Microsoft.Xna.Framework.Graphics.dll / Microsoft.Xna.Framework.dll 4.0):
//   Model.ReadMeshParts IL_002d-00ac: vertexOffset, numVertices, startIndex, primitiveCount
//     (ModelMeshPart .ctor IL_0007-0026 stores them in that order), ReadObject<object> Tag, then
//     ReadSharedResource for the VertexBuffer, IndexBuffer and Effect (fixups b__0..b__2);
//   ContentReader.ReadSharedResource IL_0027-0070: index 0 = no reference, no fixup;
//   ContentReader.ReadSharedResources IL_0000-0067: every resource first, then the fixups in
//     resource-index order, each index's list in registration order;
//   the fixup closure '<ReadSharedResource>b__0' IL_0000-001e: a resource that is not a T -- null
//     included (isinst) -- is ContentLoadException;
//   ModelMeshPart.set_Effect IL_0000-00ae: the mesh's Effects gains an effect once, when the
//     first part takes it;
//   IndexBufferReader.Read IL_0000-0044: false = 32-bit, count = bytes / 4;
//   every Tag is ReadObject<object> -- Model.Read IL_0021-0028, ReadMeshes IL_0045-004b,
//     ReadMeshParts IL_004d-0053 -- so any serialized object is a valid Tag.

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Content/ObjectDictionaryEXT.hpp"
#include "CNA/Content/ObjectValueEXT.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Xnb/XnbBuiltInReaders.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelEffectCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/InvalidCastException.hpp"
#include "System/IO/MemoryStream.hpp"

using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::GraphicsAdapter;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
using Microsoft::Xna::Framework::Graphics::PresentationParameters;
using Microsoft::Xna::Framework::Graphics::IndexElementSize;
using Microsoft::Xna::Framework::Graphics::Model;
using Microsoft::Xna::Framework::Graphics::ModelMesh;
using Microsoft::Xna::Framework::Graphics::ModelMeshPart;

namespace
{
    using Bytes = std::vector<std::uint8_t>;

    // Type-reader table (1-based ids as the body uses them).
    constexpr int kModelReader = 1;
    constexpr int kStringReader = 2;
    constexpr int kVertexBufferReader = 3;
    constexpr int kIndexBufferReader = 4;
    constexpr int kBasicEffectReader = 5;
    constexpr int kObjectDictionaryReader = 6;
    constexpr int kInt32Reader = 7;
    constexpr int kVector3Reader = 9;

    using TagWriter = std::function<void(System::IO::BinaryWriter&)>;

    struct PartSpec
    {
        std::int32_t vertexOffset = 0;
        std::int32_t numVertices = 3;
        std::int32_t startIndex = 0;
        std::int32_t primitiveCount = 1;
        const char* tagKey = nullptr;  // a Dictionary<string, object> {tagKey: tagValue} Tag
        std::int32_t tagValue = 0;
        std::int32_t effect = 0;       // shared-resource reference, 0 = none
        TagWriter tag;                 // when set, written instead of the tagKey dictionary
    };

    struct ModelSpec
    {
        std::vector<PartSpec> parts;
        bool thirdSharedResourceIsNull = false;
        TagWriter meshTag;             // default: the dictionary {"mesh": 11}
        TagWriter modelTag;            // default: null
    };

    void WriteTag(System::IO::BinaryWriter& writer, const char* key, const std::int32_t value)
    {
        if (key == nullptr) { writer.Write7BitEncodedInt(0); return; }
        writer.Write7BitEncodedInt(kObjectDictionaryReader);
        writer.Write(static_cast<std::int32_t>(1));
        writer.Write7BitEncodedInt(kStringReader);
        writer.Write(std::string(key));
        writer.Write7BitEncodedInt(kInt32Reader);
        writer.Write(value);
    }

    void WriteBasicEffect(System::IO::BinaryWriter& writer, const Vector3& diffuse,
                          const Vector3& emissive, const float specularPower, const float alpha,
                          const bool vertexColor)
    {
        writer.Write7BitEncodedInt(kBasicEffectReader);
        writer.Write(std::string());  // no texture reference
        for (const float value : {diffuse.X, diffuse.Y, diffuse.Z, emissive.X, emissive.Y, emissive.Z})
            writer.Write(value);
        for (int i = 0; i < 3; ++i) { writer.Write(0.0f); }  // specular colour
        writer.Write(specularPower);
        writer.Write(alpha);
        writer.Write(static_cast<std::uint8_t>(vertexColor ? 1 : 0));
    }

    Bytes BuildModel(const ModelSpec& spec)
    {
        System::IO::MemoryStream stream;
        System::IO::BinaryWriter writer(&stream, true);
        const char* readers[] = {
            "Microsoft.Xna.Framework.Content.ModelReader",
            "Microsoft.Xna.Framework.Content.StringReader",
            "Microsoft.Xna.Framework.Content.VertexBufferReader",
            "Microsoft.Xna.Framework.Content.IndexBufferReader",
            "Microsoft.Xna.Framework.Content.BasicEffectReader",
            "Microsoft.Xna.Framework.Content.DictionaryReader`2[[System.String],[System.Object]]",
            "Microsoft.Xna.Framework.Content.Int32Reader",
            "Microsoft.Xna.Framework.Content.VertexDeclarationReader",
            "Microsoft.Xna.Framework.Content.Vector3Reader",
        };
        writer.Write7BitEncodedInt(9);
        for (const char* name : readers)
        {
            writer.Write(std::string(name));
            writer.Write(static_cast<std::int32_t>(0));
        }
        writer.Write7BitEncodedInt(4);             // shared resources
        writer.Write7BitEncodedInt(kModelReader);  // root
        writer.Write(static_cast<std::uint32_t>(1));
        writer.Write7BitEncodedInt(kStringReader);
        writer.Write(std::string("Root"));
        for (int i = 0; i < 16; ++i) { writer.Write(i % 5 == 0 ? 1.0f : 0.0f); }
        writer.Write(static_cast<std::uint8_t>(0));   // no parent
        writer.Write(static_cast<std::uint32_t>(0));  // no children
        writer.Write(static_cast<std::int32_t>(1));   // one mesh
        writer.Write7BitEncodedInt(kStringReader);
        writer.Write(std::string("Mesh"));
        writer.Write(static_cast<std::uint8_t>(1));   // parent bone: Root
        for (const float value : {0.0f, 0.0f, 0.0f, 1.0f}) { writer.Write(value); }
        if (spec.meshTag) { spec.meshTag(writer); }
        else { WriteTag(writer, "mesh", 11); }
        writer.Write(static_cast<std::int32_t>(spec.parts.size()));
        for (const PartSpec& part : spec.parts)
        {
            writer.Write(part.vertexOffset);
            writer.Write(part.numVertices);
            writer.Write(part.startIndex);
            writer.Write(part.primitiveCount);
            if (part.tag) { part.tag(writer); }
            else { WriteTag(writer, part.tagKey, part.tagValue); }
            writer.Write7BitEncodedInt(1);  // VertexBuffer: shared resource 1
            writer.Write7BitEncodedInt(2);  // IndexBuffer: shared resource 2
            writer.Write7BitEncodedInt(part.effect);
        }
        writer.Write(static_cast<std::uint8_t>(1));  // root bone
        if (spec.modelTag) { spec.modelTag(writer); }
        else { writer.Write7BitEncodedInt(0); }      // model Tag: null

        // 1: three Vector3 positions (VertexDeclaration read raw, VertexBufferReader IL_0001).
        writer.Write7BitEncodedInt(kVertexBufferReader);
        writer.Write(static_cast<std::int32_t>(12));  // stride
        writer.Write(static_cast<std::int32_t>(1));   // one element
        for (const std::int32_t value : {0, 2, 0, 0}) { writer.Write(value); }  // 0, Vector3, Position, 0
        writer.Write(static_cast<std::uint32_t>(3));
        for (const float value : {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f})
            writer.Write(value);
        // 2: a 32-bit IndexBuffer, indices 2, 0, 1.
        writer.Write7BitEncodedInt(kIndexBufferReader);
        writer.Write(static_cast<std::uint8_t>(0));
        writer.Write(static_cast<std::int32_t>(12));
        for (const std::uint32_t value : {2u, 0u, 1u}) { writer.Write(value); }
        // 3 and 4: two BasicEffects with different fields.
        if (spec.thirdSharedResourceIsNull) { writer.Write7BitEncodedInt(0); }
        else { WriteBasicEffect(writer, Vector3(0.25f, 0.5f, 0.75f), Vector3(0, 0, 0), 8.0f, 0.5f, true); }
        WriteBasicEffect(writer, Vector3(1, 0, 0), Vector3(0.125f, 0, 0), 2.0f, 1.0f, false);
        writer.Flush();

        const auto body = stream.ToArray();
        // HiDef (flags 0x01): a 32-bit IndexBuffer is HiDef-only.
        Bytes file{'X', 'N', 'B', 'w', 5, 1, 0, 0, 0, 0};
        const auto total = static_cast<std::uint32_t>(10 + body.size());
        for (int i = 0; i < 4; ++i) { file[6 + i] = static_cast<std::uint8_t>(total >> (8 * i)); }
        file.insert(file.end(), body.begin(), body.end());
        return file;
    }

    class InMemoryContentManager final : public ContentManager
    {
    public:
        InMemoryContentManager() : ContentManager(nullptr, "Content") { RouteLoadsThroughOpenStreamEXT(true); }

        std::map<std::string, Bytes> files;

    protected:
        std::unique_ptr<System::IO::Stream> OpenStream(const std::string& assetName) override
        {
            const auto found = files.find(assetName);
            if (found == files.end()) { throw ContentLoadException("no asset '" + assetName + "'"); }
            return std::make_unique<System::IO::MemoryStream>(
                found->second.data(), static_cast<std::int32_t>(found->second.size()), false);
        }
    };

    class ModelMeshPartFixupTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            if (!device.SupportsCapability(CNA::GraphicsCapability::ThreeD))
                GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";
            ContentTypeReaderManager::ClearTypeCreators();
            CNA::Internal::Xnb::RegisterAllBuiltInXnbReaders();
            manager.setGraphicsDevice(device);
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }

        GraphicsDevice device{GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                              PresentationParameters::CreateWindowedEXT(800, 480)};
        InMemoryContentManager manager;
    };

    const BasicEffect* AsBasicEffect(const ModelMeshPart& part)
    {
        return dynamic_cast<const BasicEffect*>(part.getEffectProperty());
    }
}

TEST_F(ModelMeshPartFixupTest, PartCountsTagsAndSharedBuffersArriveAsWritten)
{
    ModelSpec spec;
    spec.parts = {{1, 2, 3, 4, "part", 22, 4}};
    manager.files["model"] = BuildModel(spec);
    const Model model = manager.Load<Model>("model");

    const ModelMesh& mesh = *model.getMeshesProperty()[0];
    auto* meshTag = dynamic_cast<CNA::Content::ObjectDictionaryEXT*>(mesh.getTagProperty());
    ASSERT_NE(meshTag, nullptr);
    EXPECT_EQ(meshTag->Get<std::int32_t>("mesh"), 11);

    const ModelMeshPart& part = *mesh.getMeshPartsProperty()[0];
    // Distinct values, so a reader that swaps any two of the four counts fails.
    EXPECT_EQ(part.getVertexOffsetProperty(), 1);
    EXPECT_EQ(part.getNumVerticesProperty(), 2);
    EXPECT_EQ(part.getStartIndexProperty(), 3);
    EXPECT_EQ(part.getPrimitiveCountProperty(), 4);
    auto* partTag = dynamic_cast<CNA::Content::ObjectDictionaryEXT*>(part.getTagProperty());
    ASSERT_NE(partTag, nullptr);
    EXPECT_EQ(partTag->Get<std::int32_t>("part"), 22);

    ASSERT_NE(part.getVertexBufferProperty(), nullptr);
    EXPECT_EQ(part.getVertexBufferProperty()->getVertexCountProperty(), 3);
    EXPECT_EQ(part.getVertexBufferProperty()->getVertexDeclarationProperty().getVertexStrideProperty(), 12);
    auto* indices = part.getIndexBufferProperty();
    ASSERT_NE(indices, nullptr);
    EXPECT_EQ(indices->getIndexElementSizeProperty(), IndexElementSize::ThirtyTwoBits);
    EXPECT_EQ(indices->getIndexCountProperty(), 3);  // 12 bytes / 4
    std::uint32_t readBack[3]{};
    indices->GetData(readBack, 3);
    EXPECT_EQ(readBack[0], 2u);
    EXPECT_EQ(readBack[1], 0u);
    EXPECT_EQ(readBack[2], 1u);

    const BasicEffect* effect = AsBasicEffect(part);
    ASSERT_NE(effect, nullptr);  // shared resource 4
    EXPECT_EQ(effect->getDiffuseColorProperty(), Vector3(1, 0, 0));
    EXPECT_EQ(effect->getEmissiveColorProperty(), Vector3(0.125f, 0, 0));
    EXPECT_EQ(effect->getSpecularPowerProperty(), 2.0f);
    EXPECT_FALSE(effect->getVertexColorEnabledProperty());
}

TEST_F(ModelMeshPartFixupTest, MeshEffectsFollowSharedResourceOrderAndHoldEachEffectOnce)
{
    // Part 0 takes resource 4, parts 1 and 2 take resource 3. The fixups run in resource order,
    // so resource 3 (part 1's) is added first and part 2 does not add it again.
    ModelSpec spec;
    spec.parts = {{0, 3, 0, 1, nullptr, 0, 4}, {0, 3, 0, 1, nullptr, 0, 3}, {0, 3, 0, 1, nullptr, 0, 3}};
    manager.files["model"] = BuildModel(spec);
    const Model model = manager.Load<Model>("model");

    const ModelMesh& mesh = *model.getMeshesProperty()[0];
    const ModelMeshPart& part0 = *mesh.getMeshPartsProperty()[0];
    const ModelMeshPart& part1 = *mesh.getMeshPartsProperty()[1];
    const ModelMeshPart& part2 = *mesh.getMeshPartsProperty()[2];
    ASSERT_NE(AsBasicEffect(part0), nullptr);
    ASSERT_NE(AsBasicEffect(part1), nullptr);
    EXPECT_EQ(AsBasicEffect(part0)->getDiffuseColorProperty(), Vector3(1, 0, 0));
    EXPECT_EQ(AsBasicEffect(part1)->getDiffuseColorProperty(), Vector3(0.25f, 0.5f, 0.75f));
    EXPECT_EQ(AsBasicEffect(part1)->getAlphaProperty(), 0.5f);
    EXPECT_TRUE(AsBasicEffect(part1)->getVertexColorEnabledProperty());
    EXPECT_EQ(part1.getEffectProperty(), part2.getEffectProperty());
    EXPECT_EQ(part0.getVertexBufferProperty(), part2.getVertexBufferProperty());

    ASSERT_EQ(mesh.getEffectsProperty().getCountProperty(), 2);
    EXPECT_EQ(mesh.getEffectsProperty()[0], part1.getEffectProperty());
    EXPECT_EQ(mesh.getEffectsProperty()[1], part0.getEffectProperty());
}

TEST_F(ModelMeshPartFixupTest, AZeroEffectReferenceLeavesThePartWithoutAnEffect)
{
    ModelSpec spec;
    spec.parts = {{0, 3, 0, 1, nullptr, 0, 0}, {0, 3, 0, 1, nullptr, 0, 4}};
    manager.files["model"] = BuildModel(spec);
    const Model model = manager.Load<Model>("model");

    const ModelMesh& mesh = *model.getMeshesProperty()[0];
    EXPECT_EQ(mesh.getMeshPartsProperty()[0]->getEffectProperty(), nullptr);
    ASSERT_EQ(mesh.getEffectsProperty().getCountProperty(), 1);
    EXPECT_EQ(mesh.getEffectsProperty()[0], mesh.getMeshPartsProperty()[1]->getEffectProperty());
}

TEST_F(ModelMeshPartFixupTest, ANullSharedResourceAPartReferencesIsContentLoadException)
{
    ModelSpec spec;
    spec.parts = {{0, 3, 0, 1, nullptr, 0, 3}};
    spec.thirdSharedResourceIsNull = true;
    manager.files["model"] = BuildModel(spec);
    EXPECT_THROW((void)manager.Load<Model>("model"), ContentLoadException);
}

TEST_F(ModelMeshPartFixupTest, TagsOfAnyObjectTypeArriveBoxedWithTheirOwnType)
{
    // A string model Tag, an Int32 mesh Tag and a Vector3 part Tag: XNA keeps each as the boxed
    // object its reader produced.
    ModelSpec spec;
    PartSpec part{0, 3, 0, 1, nullptr, 0, 4};
    part.tag = [](System::IO::BinaryWriter& w)
    {
        w.Write7BitEncodedInt(kVector3Reader);
        w.Write(1.0f); w.Write(2.0f); w.Write(3.0f);
    };
    spec.parts = {part};
    spec.meshTag = [](System::IO::BinaryWriter& w)
    {
        w.Write7BitEncodedInt(kInt32Reader);
        w.Write(static_cast<std::int32_t>(7));
    };
    spec.modelTag = [](System::IO::BinaryWriter& w)
    {
        w.Write7BitEncodedInt(kStringReader);
        w.Write(std::string("hello"));
    };
    manager.files["model"] = BuildModel(spec);
    const Model model = manager.Load<Model>("model");

    auto* modelTag = dynamic_cast<CNA::Content::ObjectValueEXT*>(model.getTagProperty());
    ASSERT_NE(modelTag, nullptr);
    EXPECT_EQ(modelTag->Get<std::string>(), "hello");
    EXPECT_EQ(modelTag->GetTypeName(), "System.String");
    EXPECT_THROW((void)modelTag->Get<float>(), System::InvalidCastException);

    const ModelMesh& mesh = *model.getMeshesProperty()[0];
    auto* meshTag = dynamic_cast<CNA::Content::ObjectValueEXT*>(mesh.getTagProperty());
    ASSERT_NE(meshTag, nullptr);
    EXPECT_EQ(meshTag->Get<std::int32_t>(), 7);
    EXPECT_EQ(meshTag->GetTypeName(), "System.Int32");

    auto* partTag = dynamic_cast<CNA::Content::ObjectValueEXT*>(
        mesh.getMeshPartsProperty()[0]->getTagProperty());
    ASSERT_NE(partTag, nullptr);
    EXPECT_EQ(partTag->Get<Vector3>(), Vector3(1, 2, 3));
    EXPECT_EQ(partTag->GetTypeName(), "Microsoft.Xna.Framework.Vector3");
}
