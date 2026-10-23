// SPDX-License-Identifier: MS-PL
// ISSUE-011: metadata sharing must not erase independent ownership boundaries.
#include <gtest/gtest.h>
#include <utility>
#include <vector>
#include "CNA/Internal/ContentOwnership.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Content::ContentManager;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::ContentOwnershipAccess;

namespace
{
    class BoundaryTag final : public System::Object
    {
    public:
        const std::string& GetTypeName() const override
        {
            static const std::string name = "ContentBoundaryTag";
            return name;
        }
    };
    class ContentTextureMetadataBoundaryTest : public ::testing::Test
    {
    protected:
        GraphicsDevice device;
        ContentManager manager{nullptr, "tests/assets/xnb/monogame/windows/uncompressed"};
        void SetUp() override { manager.setGraphicsDevice(device); }
        void UseCubeRoot()
        {
            manager.setRootDirectoryProperty(std::string("tests/assets/xnb/cna/windows/uncompressed"));
        }
    };
}

TEST_F(ContentTextureMetadataBoundaryTest, CubeCopiesShareMetadataButExplicitDisposalStaysIndependent)
{
    UseCubeRoot();
    BoundaryTag tag;
    auto first = manager.Load<TextureCube>("texturecube_six_faces");
    first.setNameProperty("cube");
    first.setTagProperty(&tag);
    auto second = manager.Load<TextureCube>("texturecube_six_faces");
    TextureCube moved(std::move(second));
    TextureCube copied(moved);
    const GraphicsResource& base = copied;
    EXPECT_EQ(base.getNameProperty(), "cube");
    EXPECT_EQ(base.getTagProperty(), &tag);
    copied.setNameProperty("renamed");
    EXPECT_EQ(first.getNameProperty(), "renamed");
    first.Dispose();
    EXPECT_FALSE(moved.getIsDisposedProperty());
    std::vector<Color> pixels(moved.getSizeProperty() * moved.getSizeProperty());
    EXPECT_NO_THROW(moved.GetData(CubeMapFace::PositiveX, pixels.data(), static_cast<int>(pixels.size())));
    manager.Unload();
    EXPECT_TRUE(moved.getIsDisposedProperty());
    EXPECT_TRUE(copied.getIsDisposedProperty());
}

TEST_F(ContentTextureMetadataBoundaryTest, DetachedConstTextureKeepsSnapshotAndSurvivesUnload)
{
    BoundaryTag tag;
    auto owner = manager.Load<Texture2D>("white-1");
    owner.setNameProperty("snapshot");
    owner.setTagProperty(&tag);
    const auto independent = manager.Load<Texture2D>("white-1");
    ContentOwnershipAccess::Detach(independent);
    owner.setNameProperty("owner changed");
    owner.setTagProperty(nullptr);
    EXPECT_EQ(independent.getNameProperty(), "snapshot");
    EXPECT_EQ(independent.getTagProperty(), &tag);
    manager.Unload();
    EXPECT_TRUE(owner.getIsDisposedProperty());
    EXPECT_FALSE(independent.getIsDisposedProperty());
    Color pixel;
    ASSERT_NO_THROW(independent.GetData(&pixel, 1));
    EXPECT_EQ(pixel, Color::White);
}

TEST_F(ContentTextureMetadataBoundaryTest, DetachedCubeHasIndependentMetadataAndReadablePixels)
{
    UseCubeRoot();
    BoundaryTag ownerTag, independentTag;
    auto owner = manager.Load<TextureCube>("texturecube_six_faces");
    owner.setNameProperty("owner");
    owner.setTagProperty(&ownerTag);
    auto independent = manager.Load<TextureCube>("texturecube_six_faces");
    ContentOwnershipAccess::Detach(independent);
    independent.setNameProperty("independent");
    independent.setTagProperty(&independentTag);
    EXPECT_EQ(owner.getNameProperty(), "owner");
    EXPECT_EQ(owner.getTagProperty(), &ownerTag);
    std::vector<Color> before(owner.getSizeProperty() * owner.getSizeProperty());
    ASSERT_NO_THROW(owner.GetData(CubeMapFace::PositiveX, before.data(), static_cast<int>(before.size())));
    manager.Unload();
    EXPECT_FALSE(independent.getIsDisposedProperty());
    EXPECT_EQ(independent.getNameProperty(), "independent");
    EXPECT_EQ(independent.getTagProperty(), &independentTag);
    std::vector<Color> after(before.size());
    ASSERT_NO_THROW(independent.GetData(CubeMapFace::PositiveX, after.data(), static_cast<int>(after.size())));
    EXPECT_EQ(after, before);
}

TEST_F(ContentTextureMetadataBoundaryTest, AssigningAnUnrecordedTextureClearsTheOldMetadataScope)
{
    BoundaryTag managedTag, ordinaryTag;
    auto target = manager.Load<Texture2D>("white-1");
    target.setNameProperty("managed");
    target.setTagProperty(&managedTag);
    Texture2D ordinary;
    ordinary.setNameProperty("ordinary");
    ordinary.setTagProperty(&ordinaryTag);
    target = ordinary;
    EXPECT_EQ(target.getNameProperty(), "ordinary");
    EXPECT_EQ(target.getTagProperty(), &ordinaryTag);
    target.setNameProperty("assigned");
    target.setTagProperty(nullptr);
    const auto cached = manager.Load<Texture2D>("white-1");
    EXPECT_EQ(cached.getNameProperty(), "managed");
    EXPECT_EQ(cached.getTagProperty(), &managedTag);
    EXPECT_EQ(ordinary.getNameProperty(), "ordinary");
    EXPECT_EQ(ordinary.getTagProperty(), &ordinaryTag);
}

TEST_F(ContentTextureMetadataBoundaryTest, MoveAssignmentReplacesThePreviousManagersScope)
{
    ContentManager other(nullptr, manager.getRootDirectoryProperty());
    other.setGraphicsDevice(device);
    auto target = manager.Load<Texture2D>("white-1");
    auto incoming = other.Load<Texture2D>("white-1");
    target.setNameProperty("old owner");
    incoming.setNameProperty("new owner");
    target = std::move(incoming);
    target.setNameProperty("moved");
    EXPECT_EQ(other.Load<Texture2D>("white-1").getNameProperty(), "moved");
    EXPECT_EQ(manager.Load<Texture2D>("white-1").getNameProperty(), "old owner");
    manager.Unload();
    EXPECT_FALSE(target.getIsDisposedProperty());
    Color pixel;
    ASSERT_NO_THROW(target.GetData(&pixel, 1));
    EXPECT_EQ(pixel, Color::White);
    other.Unload();
    EXPECT_TRUE(target.getIsDisposedProperty());
}
