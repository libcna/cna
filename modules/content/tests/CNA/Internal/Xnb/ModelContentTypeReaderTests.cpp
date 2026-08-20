// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnb.md XNB-36/37/38/39/40/41: end-to-end test for ModelReader (and its
// VertexDeclarationReader/VertexBufferReader/IndexBufferReader dependencies) against a real,
// externally-produced fixture (MonoGame's BlenderDefaultCube.xnb, already vendored for XNB-32's
// BasicEffectReader tests). Field values asserted below were independently verified with a
// standalone Python parser before this reader was written -- see the fixture's own manifest.json
// for the full parsed structure.

#include <gtest/gtest.h>

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Xnb/ModelContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/PrimitiveContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/StockEffectContentTypeReaders.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::IndexBuffer;
using Microsoft::Xna::Framework::Graphics::Model;
using Microsoft::Xna::Framework::Graphics::ModelBone;
using Microsoft::Xna::Framework::Graphics::ModelMesh;
using Microsoft::Xna::Framework::Graphics::ModelMeshPart;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

namespace
{
    class ModelContentTypeReaderTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            CNA::Internal::Xnb::RegisterPrimitiveXnbReaders();
            CNA::Internal::Xnb::RegisterStockEffectXnbReaders();
            CNA::Internal::Xnb::RegisterModelXnbReaders();
            cm.setGraphicsDevice(gd);
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }

        GraphicsDevice gd;
        ContentManager cm{nullptr, "tests/assets/xnb/monogame/windows/uncompressed"};
    };
}

TEST_F(ModelContentTypeReaderTest, AllFourReadersAreRegisteredUnderRealFnaCanonicalNames)
{
    EXPECT_TRUE(ContentTypeReaderManager::IsRegistered("Microsoft.Xna.Framework.Content.VertexDeclarationReader"));
    EXPECT_TRUE(ContentTypeReaderManager::IsRegistered("Microsoft.Xna.Framework.Content.VertexBufferReader"));
    EXPECT_TRUE(ContentTypeReaderManager::IsRegistered("Microsoft.Xna.Framework.Content.IndexBufferReader"));
    EXPECT_TRUE(ContentTypeReaderManager::IsRegistered("Microsoft.Xna.Framework.Content.ModelReader"));
}

TEST_F(ModelContentTypeReaderTest, LoadRealMonoGameFixtureEndToEnd)
{
    // Model loading builds a real VertexBuffer/IndexBuffer -- inherently 3D-pipeline concepts a
    // renderer with no 3D pipeline has no storage for. Guarded per-test (not in SetUp()) so the
    // pure reader-registration test above, which never loads a Model, keeps running everywhere.
    if (!gd.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";

    Model model = cm.Load<Model>("BlenderDefaultCube");

    // --- Bones (XNB-37): 2 bones, RootNode -> Cube, real multi-bone hierarchy ---
    ASSERT_EQ(model.getBonesProperty().getCountProperty(), 2);
    ModelBone* rootNode = model.getBonesProperty()["RootNode"];
    ModelBone* cubeBone = model.getBonesProperty()["Cube"];
    ASSERT_NE(rootNode, nullptr);
    ASSERT_NE(cubeBone, nullptr);
    EXPECT_EQ(rootNode->getIndexProperty(), 0);
    EXPECT_EQ(cubeBone->getIndexProperty(), 1);
    EXPECT_EQ(rootNode->getParentProperty(), nullptr);
    EXPECT_EQ(cubeBone->getParentProperty(), rootNode);
    ASSERT_EQ(rootNode->getChildrenProperty().getCountProperty(), 1);
    EXPECT_EQ(rootNode->getChildrenProperty()[0], cubeBone);
    EXPECT_EQ(model.getRootProperty(), rootNode);

    // --- Mesh (XNB-38/39): 1 mesh, parent bone 'Cube', real BoundingSphere ---
    ASSERT_EQ(model.getMeshesProperty().getCountProperty(), 1);
    ModelMesh* mesh = model.getMeshesProperty()[0];
    EXPECT_EQ(mesh->getNameProperty(), "Cube");
    EXPECT_EQ(mesh->getParentBoneProperty(), cubeBone);
    EXPECT_NEAR(mesh->getBoundingSphereProperty().Radius, 1.732050895690918f, 1e-4f);
    EXPECT_NEAR(mesh->getBoundingSphereProperty().Center.X, 0.0f, 1e-5f);
    EXPECT_NEAR(mesh->getBoundingSphereProperty().Center.Y, 0.0f, 1e-5f);
    EXPECT_NEAR(mesh->getBoundingSphereProperty().Center.Z, 0.0f, 1e-5f);

    // --- Mesh part (XNB-38/40): 24 vertices / 12 primitives, real VB/IB/Effect shared resources ---
    ASSERT_EQ(mesh->getMeshPartsProperty().getCountProperty(), 1);
    ModelMeshPart* part = mesh->getMeshPartsProperty()[0];
    EXPECT_EQ(part->getVertexOffsetProperty(), 0);
    EXPECT_EQ(part->getNumVerticesProperty(), 24);
    EXPECT_EQ(part->getStartIndexProperty(), 0);
    EXPECT_EQ(part->getPrimitiveCountProperty(), 12);

    ASSERT_NE(part->getVertexBufferProperty(), nullptr);
    VertexBuffer* vb = part->getVertexBufferProperty();
    EXPECT_EQ(vb->getVertexCountProperty(), 24);
    ASSERT_EQ(vb->getVertexDeclarationProperty().getVertexStrideProperty(), 24);
    ASSERT_EQ(vb->getVertexDeclarationProperty().GetVertexElements().size(), 2u);
    EXPECT_EQ(vb->getVertexDeclarationProperty().GetVertexElements()[0].getOffsetProperty(), 0);
    EXPECT_EQ(vb->getVertexDeclarationProperty().GetVertexElements()[0].getVertexElementFormatProperty(),
              VertexElementFormat::Vector3);
    EXPECT_EQ(vb->getVertexDeclarationProperty().GetVertexElements()[0].getVertexElementUsageProperty(),
              VertexElementUsage::Position);
    EXPECT_EQ(vb->getVertexDeclarationProperty().GetVertexElements()[1].getOffsetProperty(), 12);
    EXPECT_EQ(vb->getVertexDeclarationProperty().GetVertexElements()[1].getVertexElementUsageProperty(),
              VertexElementUsage::Normal);

    ASSERT_NE(part->getIndexBufferProperty(), nullptr);
    IndexBuffer* ib = part->getIndexBufferProperty();
    EXPECT_EQ(ib->getIndexElementSizeProperty(), Microsoft::Xna::Framework::Graphics::IndexElementSize::SixteenBits);
    EXPECT_EQ(ib->getIndexCountProperty(), 36); // 72 bytes / 2 = 36 sixteen-bit indices, 12 triangles * 3

    ASSERT_NE(part->getEffectProperty(), nullptr);
    auto* basicEffect = dynamic_cast<BasicEffect*>(part->getEffectProperty());
    ASSERT_NE(basicEffect, nullptr);
    EXPECT_NEAR(basicEffect->getDiffuseColorProperty().X, 0.64000004529953f, 1e-6f);
    EXPECT_FLOAT_EQ(basicEffect->getAlphaProperty(), 1.0f);

    // setEffectProperty() must have kept the mesh's own ModelEffectCollection in sync.
    EXPECT_EQ(mesh->getEffectsProperty().getCountProperty(), 1);
    EXPECT_EQ(mesh->getEffectsProperty()[0], part->getEffectProperty());

    // --- Root-level Tag (all null in this fixture) ---
    EXPECT_EQ(model.getTagProperty(), nullptr);
}

TEST_F(ModelContentTypeReaderTest, LoadingTwiceReusesTheCachedModelLikeAnyOtherAsset)
{
    // ContentManager::Load<T>()'s generic strong (std::any-based) cache applies to Model exactly
    // like Texture2D/SpriteFont: a second Load<Model>() for the same asset name returns a copy of
    // the *same* cached Model (Model is a lightweight, copy-constructible handle -- its real GPU
    // resources live behind setOwnedResources()), not a fresh decode. Confirmed here via pointer
    // identity of the underlying VertexBuffer, since that's what would actually double the GPU
    // memory/upload cost if caching silently didn't work for this type.
    if (!gd.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";

    Model first = cm.Load<Model>("BlenderDefaultCube");
    Model second = cm.Load<Model>("BlenderDefaultCube");

    VertexBuffer* firstVb = first.getMeshesProperty()[0]->getMeshPartsProperty()[0]->getVertexBufferProperty();
    VertexBuffer* secondVb = second.getMeshesProperty()[0]->getMeshPartsProperty()[0]->getVertexBufferProperty();
    EXPECT_EQ(firstVb, secondVb);
}
