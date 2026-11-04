// SPDX-License-Identifier: MS-PL
//
// TQ-033: the runtime ModelReader family against .xnb files the genuine XNA 4.0 pipeline built
// (tests/reference/xna40/differential, sources in tests/assets/xna40/model, recipes in
// tools/xna-pipeline-oracle/differential/corpus.json). Every expected value below is a field of
// those files, decoded by hand following the reader IL of Microsoft.Xna.Framework.Graphics.dll
// 4.0 -- never CNA's own output:
//   Model.Read IL_0000-002e: ReadBones, ReadMeshes, root bone reference, Tag;
//   Model.ReadBones IL_0000-009c: count, per bone ReadObject<string> + ReadMatrix, then per bone
//     the parent reference and the child references (SetParentAndChildren);
//   Model.ReadBoneReference IL_0000-003b: one byte while bones + 1 <= 255, 0 = null;
//   Model.ReadMeshes IL_0000-0079: name, parent bone, BoundingSphere (Vector3 + Single), Tag, parts;
//   Model.ReadMeshParts IL_0000-00ba: vertexOffset, numVertices, startIndex, primitiveCount, Tag,
//     then the VertexBuffer, IndexBuffer and Effect shared-resource references;
//   VertexBufferReader.Read IL_0000-003a, VertexDeclarationReader.Read IL_0000-0061,
//   IndexBufferReader.Read IL_0000-0044 (true = 16-bit), BasicEffectReader.Read IL_0000-0085
//     (texture reference; Texture + TextureEnabled = true only when it is not null, IL_002b-0037).
// ModelMeshPart.set_Effect IL_0090-00a2 adds an effect to the mesh's Effects once, in the order the
// fixups run, which ContentReader.ReadSharedResources IL_0020-0065 makes shared-resource order.

#include <bit>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Xnb/XnbBuiltInReaders.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelEffectCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/IO/MemoryStream.hpp"

using Microsoft::Xna::Framework::Matrix;
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
using Microsoft::Xna::Framework::Graphics::ModelBone;
using Microsoft::Xna::Framework::Graphics::ModelMesh;
using Microsoft::Xna::Framework::Graphics::ModelMeshPart;
using Microsoft::Xna::Framework::Graphics::VertexElement;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

namespace
{
    using Bytes = std::vector<std::uint8_t>;

    Bytes ReadDifferentialFixture(const std::string& name)
    {
        const std::string path = "tests/reference/xna40/differential/" + name + ".xnb";
        std::ifstream stream(path, std::ios::binary);
        EXPECT_TRUE(stream.good()) << path << " (the tests run from the source root)";
        return Bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
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

    struct Floats6 { float v[6]; };
    struct Floats8 { float v[8]; };

    float Bits(const std::uint32_t value) { return std::bit_cast<float>(value); }

    Matrix Translation(const float x, const float y, const float z)
    {
        return Matrix(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, z, 1);
    }

    BasicEffect* BasicEffectOf(const ModelMeshPart& part)
    {
        return dynamic_cast<BasicEffect*>(part.getEffectProperty());
    }

    class XnaBuiltModelFixtureTest : public ::testing::Test
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

        Model LoadFixture(const std::string& name)
        {
            manager.files[name] = ReadDifferentialFixture(name);
            return manager.Load<Model>(name);
        }

        // Every model fixture is a HiDef build (header flags 0x01), which XNA loads only on a HiDef
        // device (GraphicsContentHelper.GraphicsDeviceFromContentReader IL_0054-0090).
        GraphicsDevice device{GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                              PresentationParameters::CreateWindowedEXT(800, 480)};
        InMemoryContentManager manager;
    };
}

// hierarchy.x: Frame World (translate 5,0,0) > Frame Child (scale 2, translate 0,3,0) > Mesh Tri.
TEST_F(XnaBuiltModelFixtureTest, HierarchyModelBonesMeshAndPartMatchTheXnaBuiltFile)
{
    const Model model = LoadFixture("model_x_hierarchy");

    const auto& bones = model.getBonesProperty();
    ASSERT_EQ(bones.getCountProperty(), 3);
    EXPECT_EQ(bones[0]->getNameProperty(), "World");
    EXPECT_EQ(bones[1]->getNameProperty(), "Child");
    EXPECT_EQ(bones[2]->getNameProperty(), "Tri");
    for (int i = 0; i < 3; ++i) { EXPECT_EQ(bones[i]->getIndexProperty(), i); }
    EXPECT_EQ(bones[0]->getTransformProperty(), Translation(5, 0, 0));
    EXPECT_EQ(bones[1]->getTransformProperty(), Matrix(2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0, 0, 3, 0, 1));
    EXPECT_EQ(bones[2]->getTransformProperty(), Matrix::getIdentityProperty());
    // Serialized parents: none, bone 0, bone 1; children lists: [1], [2], [].
    EXPECT_EQ(bones[0]->getParentProperty(), nullptr);
    EXPECT_EQ(bones[1]->getParentProperty(), bones[0]);
    EXPECT_EQ(bones[2]->getParentProperty(), bones[1]);
    ASSERT_EQ(bones[0]->getChildrenProperty().getCountProperty(), 1);
    EXPECT_EQ(bones[0]->getChildrenProperty()[0], bones[1]);
    ASSERT_EQ(bones[1]->getChildrenProperty().getCountProperty(), 1);
    EXPECT_EQ(bones[1]->getChildrenProperty()[0], bones[2]);
    EXPECT_EQ(bones[2]->getChildrenProperty().getCountProperty(), 0);
    EXPECT_EQ(model.getRootProperty(), bones[0]);   // root reference byte 1
    EXPECT_EQ(model.getTagProperty(), nullptr);     // model Tag type id 0

    // Absolute transforms follow the serialized parents: Tri = I * Child * World, which is
    // scale 2 with translation (0,3,0) + (5,0,0) = (5,3,0).
    std::vector<Matrix> absolute(3);
    model.CopyAbsoluteBoneTransformsTo(absolute);
    EXPECT_EQ(absolute[2], Matrix(2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0, 5, 3, 0, 1));

    ASSERT_EQ(model.getMeshesProperty().getCountProperty(), 1);
    const ModelMesh& mesh = *model.getMeshesProperty()[0];
    EXPECT_EQ(mesh.getNameProperty(), "Tri");
    EXPECT_EQ(mesh.getParentBoneProperty(), bones[2]);  // mesh parent reference byte 3
    EXPECT_EQ(mesh.getTagProperty(), nullptr);
    // The pipeline's bounding sphere, bit for bit (bytes 69 83 8d 3e, 4c 3e b9 3e, 0, bd 1b 4f 3f).
    EXPECT_EQ(mesh.getBoundingSphereProperty().Center, Vector3(Bits(0x3e8d8369u), Bits(0x3eb93e4cu), 0.0f));
    EXPECT_EQ(mesh.getBoundingSphereProperty().Radius, Bits(0x3f4f1bbdu));

    ASSERT_EQ(mesh.getMeshPartsProperty().getCountProperty(), 1);
    const ModelMeshPart& part = *mesh.getMeshPartsProperty()[0];
    EXPECT_EQ(part.getVertexOffsetProperty(), 0);
    EXPECT_EQ(part.getNumVerticesProperty(), 3);
    EXPECT_EQ(part.getStartIndexProperty(), 0);
    EXPECT_EQ(part.getPrimitiveCountProperty(), 1);
    EXPECT_EQ(part.getTagProperty(), nullptr);

    auto* vertices = part.getVertexBufferProperty();
    ASSERT_NE(vertices, nullptr);
    EXPECT_EQ(vertices->getVertexCountProperty(), 3);
    EXPECT_EQ(vertices->getVertexDeclarationProperty().getVertexStrideProperty(), 24);
    EXPECT_EQ(vertices->getVertexDeclarationProperty().GetVertexElements(),
              (std::vector<VertexElement>{
                  VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                  VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0)}));
    Floats6 data[3]{};
    vertices->GetData(data, 3);
    const float expected[3][6] = {{0, 0, 0, 0, 0, -1}, {1, 0, 0, 0, 0, -1}, {0, 1, 0, 0, 0, -1}};
    for (int v = 0; v < 3; ++v)
        for (int c = 0; c < 6; ++c) { EXPECT_EQ(data[v].v[c], expected[v][c]) << v << "," << c; }

    auto* indices = part.getIndexBufferProperty();
    ASSERT_NE(indices, nullptr);
    EXPECT_EQ(indices->getIndexElementSizeProperty(), IndexElementSize::SixteenBits);
    EXPECT_EQ(indices->getIndexCountProperty(), 3);  // 6 bytes / 2 (IndexBufferReader IL_001e-0021)
    std::uint16_t readBack[3]{};
    indices->GetData(readBack, 3);
    EXPECT_EQ(readBack[0], 0);
    EXPECT_EQ(readBack[1], 1);
    EXPECT_EQ(readBack[2], 2);

    BasicEffect* const effect = BasicEffectOf(part);
    ASSERT_NE(effect, nullptr) << "the part's effect must be the BasicEffect the file names";
    EXPECT_EQ(effect->getDiffuseColorProperty(), Vector3(1, 1, 1));
    EXPECT_EQ(effect->getEmissiveColorProperty(), Vector3(0, 0, 0));
    EXPECT_EQ(effect->getSpecularColorProperty(), Vector3(1, 1, 1));
    EXPECT_EQ(effect->getSpecularPowerProperty(), 16.0f);
    EXPECT_EQ(effect->getAlphaProperty(), 1.0f);
    EXPECT_FALSE(effect->getVertexColorEnabledProperty());
    // Empty texture reference: null texture, TextureEnabled left at the clone's default.
    EXPECT_EQ(effect->getTextureProperty(), nullptr);
    EXPECT_FALSE(effect->getTextureEnabledProperty());
    ASSERT_EQ(mesh.getEffectsProperty().getCountProperty(), 1);
    EXPECT_EQ(mesh.getEffectsProperty()[0], part.getEffectProperty());
}

// bare_mesh.x has a mesh outside any frame: the XNA processor adds an unnamed root bone, which it
// writes as a null string (bone 0's name type id is 0).
TEST_F(XnaBuiltModelFixtureTest, BareMeshModelKeepsTheUnnamedRootAndTheLooseMeshBone)
{
    const Model model = LoadFixture("model_x_bare_mesh");

    const auto& bones = model.getBonesProperty();
    ASSERT_EQ(bones.getCountProperty(), 2);
    // XNA: Name == null. CNA represents the null System.String as an empty std::string.
    EXPECT_EQ(bones[0]->getNameProperty(), "");
    EXPECT_EQ(bones[1]->getNameProperty(), "Loose");
    EXPECT_EQ(bones[1]->getParentProperty(), bones[0]);
    EXPECT_EQ(model.getRootProperty(), bones[0]);

    ASSERT_EQ(model.getMeshesProperty().getCountProperty(), 1);
    const ModelMesh& mesh = *model.getMeshesProperty()[0];
    EXPECT_EQ(mesh.getNameProperty(), "Loose");
    EXPECT_EQ(mesh.getParentBoneProperty(), bones[1]);
    EXPECT_EQ(mesh.getBoundingSphereProperty().Center, Vector3(Bits(0x3f0d8369u), Bits(0x3f393e4cu), 0.0f));
    EXPECT_EQ(mesh.getBoundingSphereProperty().Radius, Bits(0x3fcf1bbdu));

    // The source triangle's vertices (0,0,0), (2,0,0), (0,2,0) arrive unchanged.
    Floats6 data[3]{};
    mesh.getMeshPartsProperty()[0]->getVertexBufferProperty()->GetData(data, 3);
    EXPECT_EQ(Vector3(data[1].v[0], data[1].v[1], data[1].v[2]), Vector3(2, 0, 0));
    EXPECT_EQ(Vector3(data[2].v[0], data[2].v[1], data[2].v[2]), Vector3(0, 2, 0));
}

// quad_textured.x: one four-vertex quad with a material that names surface_0, vertex colours on.
TEST_F(XnaBuiltModelFixtureTest, TexturedQuadBindsItsExternalTextureAndEnablesTexturing)
{
    manager.files["surface_0"] = ReadDifferentialFixture("texture_png_texture_mipmaps");
    const Model model = LoadFixture("model_x_textured");

    const ModelMeshPart& part = *model.getMeshesProperty()[0]->getMeshPartsProperty()[0];
    EXPECT_EQ(part.getNumVerticesProperty(), 4);
    EXPECT_EQ(part.getPrimitiveCountProperty(), 2);
    EXPECT_EQ(part.getVertexBufferProperty()->getVertexDeclarationProperty().getVertexStrideProperty(), 36);
    EXPECT_EQ(part.getVertexBufferProperty()->getVertexDeclarationProperty().GetVertexElements(),
              (std::vector<VertexElement>{
                  VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                  VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
                  VertexElement(24, VertexElementFormat::Color, VertexElementUsage::Color, 0),
                  VertexElement(28, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0)}));
    std::uint16_t indices[6]{};
    part.getIndexBufferProperty()->GetData(indices, 6);
    const std::uint16_t expectedIndices[6] = {0, 1, 2, 0, 3, 1};
    for (int i = 0; i < 6; ++i) { EXPECT_EQ(indices[i], expectedIndices[i]) << i; }

    BasicEffect* const effect = BasicEffectOf(part);
    ASSERT_NE(effect, nullptr) << "the part's effect must be the BasicEffect the file names";
    EXPECT_EQ(effect->getDiffuseColorProperty(), Vector3(0.8f, 0.7f, 0.6f));
    EXPECT_EQ(effect->getEmissiveColorProperty(), Vector3(0.01f, 0.02f, 0.03f));
    EXPECT_EQ(effect->getSpecularColorProperty(), Vector3(0.1f, 0.2f, 0.3f));
    EXPECT_EQ(effect->getSpecularPowerProperty(), 24.0f);
    EXPECT_EQ(effect->getAlphaProperty(), 1.0f);
    EXPECT_TRUE(effect->getVertexColorEnabledProperty());
    // A non-null texture reference sets Texture and TextureEnabled (BasicEffectReader IL_002b-0037).
    ASSERT_NE(effect->getTextureProperty(), nullptr);
    EXPECT_TRUE(effect->getTextureEnabledProperty());
    EXPECT_EQ(effect->getTextureProperty()->getWidthProperty(), 2);
    EXPECT_EQ(effect->getTextureProperty()->getHeightProperty(), 2);
    EXPECT_EQ(effect->getTextureProperty()->getLevelCountProperty(), 2);
}

// two_textures.x: one mesh, two subsets with different materials -> two parts sharing one vertex
// and one index buffer, each with its own BasicEffect (shared resources 3 and 4).
TEST_F(XnaBuiltModelFixtureTest, TwoMaterialPartsShareTheBuffersAndListTheirEffectsInFixupOrder)
{
    manager.files["surface_0"] = ReadDifferentialFixture("texture_png_texture_mipmaps");
    manager.files["second_0"] = ReadDifferentialFixture("texture_png_texture_mipmaps");
    const Model model = LoadFixture("model_x_two_textures");

    const ModelMesh& mesh = *model.getMeshesProperty()[0];
    EXPECT_EQ(mesh.getNameProperty(), "Split");
    ASSERT_EQ(mesh.getMeshPartsProperty().getCountProperty(), 2);
    const ModelMeshPart& first = *mesh.getMeshPartsProperty()[0];
    const ModelMeshPart& second = *mesh.getMeshPartsProperty()[1];
    EXPECT_EQ(first.getVertexOffsetProperty(), 0);
    EXPECT_EQ(first.getStartIndexProperty(), 0);
    EXPECT_EQ(second.getVertexOffsetProperty(), 3);
    EXPECT_EQ(second.getNumVerticesProperty(), 3);
    EXPECT_EQ(second.getStartIndexProperty(), 3);
    EXPECT_EQ(second.getPrimitiveCountProperty(), 1);
    // Shared-resource references 1 and 2 name the same objects for both parts.
    EXPECT_EQ(first.getVertexBufferProperty(), second.getVertexBufferProperty());
    EXPECT_EQ(first.getIndexBufferProperty(), second.getIndexBufferProperty());
    EXPECT_EQ(first.getVertexBufferProperty()->getVertexCountProperty(), 6);
    EXPECT_EQ(first.getVertexBufferProperty()->getVertexDeclarationProperty().GetVertexElements(),
              (std::vector<VertexElement>{
                  VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                  VertexElement(12, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
                  VertexElement(20, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0)}));
    Floats8 data[6]{};
    first.getVertexBufferProperty()->GetData(data, 6);
    EXPECT_EQ(Vector3(data[1].v[0], data[1].v[1], data[1].v[2]), Vector3(1, 0, 0));
    EXPECT_EQ(data[1].v[3], 1.0f);  // its texture coordinate (1, 1) at offset 12
    EXPECT_EQ(data[1].v[4], 1.0f);
    EXPECT_EQ(Vector3(data[1].v[5], data[1].v[6], data[1].v[7]), Vector3(0, 0, -1));  // normal at 20

    ASSERT_NE(first.getEffectProperty(), nullptr);
    ASSERT_NE(second.getEffectProperty(), nullptr);
    EXPECT_NE(first.getEffectProperty(), second.getEffectProperty());
    ASSERT_EQ(mesh.getEffectsProperty().getCountProperty(), 2);
    EXPECT_EQ(mesh.getEffectsProperty()[0], first.getEffectProperty());   // shared resource 3
    EXPECT_EQ(mesh.getEffectsProperty()[1], second.getEffectProperty());  // shared resource 4
    for (const ModelMeshPart* part : {&first, &second})
    {
        BasicEffect* const effect = BasicEffectOf(*part);
        ASSERT_NE(effect, nullptr);
        EXPECT_TRUE(effect->getTextureEnabledProperty());
        EXPECT_NE(effect->getTextureProperty(), nullptr);
        EXPECT_EQ(effect->getSpecularColorProperty(), Vector3(0, 0, 0));
        EXPECT_EQ(effect->getSpecularPowerProperty(), 1.0f);
    }
}

// skinned_animated.x through the plain ModelProcessor: four bones, a two-child root, and the
// skinning channels in the vertex declaration.
TEST_F(XnaBuiltModelFixtureTest, SkinnedSourceKeepsItsBoneGraphAndSkinningVertexChannels)
{
    const Model model = LoadFixture("model_x_skinned");

    const auto& bones = model.getBonesProperty();
    ASSERT_EQ(bones.getCountProperty(), 4);
    EXPECT_EQ(bones[0]->getNameProperty(), "Armature");
    EXPECT_EQ(bones[1]->getNameProperty(), "Skin");
    EXPECT_EQ(bones[2]->getNameProperty(), "Bone0");
    EXPECT_EQ(bones[3]->getNameProperty(), "Bone1");
    ASSERT_EQ(bones[0]->getChildrenProperty().getCountProperty(), 2);
    EXPECT_EQ(bones[0]->getChildrenProperty()[0], bones[1]);
    EXPECT_EQ(bones[0]->getChildrenProperty()[1], bones[2]);
    EXPECT_EQ(bones[1]->getParentProperty(), bones[0]);
    EXPECT_EQ(bones[2]->getParentProperty(), bones[0]);
    EXPECT_EQ(bones[3]->getParentProperty(), bones[2]);
    EXPECT_EQ(bones[3]->getTransformProperty(), Translation(0, 2, 0));

    const ModelMesh& mesh = *model.getMeshesProperty()[0];
    EXPECT_EQ(mesh.getParentBoneProperty(), bones[1]);
    EXPECT_EQ(mesh.getBoundingSphereProperty().Center, Vector3(0, 1, 0));
    EXPECT_EQ(mesh.getBoundingSphereProperty().Radius, Bits(0x3fb504f3u));
    const auto& declaration =
        mesh.getMeshPartsProperty()[0]->getVertexBufferProperty()->getVertexDeclarationProperty();
    EXPECT_EQ(declaration.getVertexStrideProperty(), 44);
    EXPECT_EQ(declaration.GetVertexElements(),
              (std::vector<VertexElement>{
                  VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                  VertexElement(12, VertexElementFormat::Byte4, VertexElementUsage::BlendIndices, 0),
                  VertexElement(16, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 0),
                  VertexElement(32, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0)}));
}
