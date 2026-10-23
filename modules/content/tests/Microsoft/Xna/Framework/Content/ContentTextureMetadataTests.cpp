// SPDX-License-Identifier: MS-PL
// ISSUE-011: cached resource metadata is distinct from explicit wrapper disposal.
#include <gtest/gtest.h>
#include <memory>
#include <vector>
#include <utility>
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "System/Object.hpp"

using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::GraphicsResource;
using Microsoft::Xna::Framework::Graphics::Texture2D;

namespace
{
    class MetadataTag final : public System::Object
    {
    public:
        const std::string& GetTypeName() const override
        {
            static const std::string name = "ContentMetadataTag";
            return name;
        }
    };

    class ContentTextureMetadataTest : public ::testing::Test
    {
    protected:
        GraphicsDevice device;
        ContentManager manager{nullptr, "tests/assets/xnb/monogame/windows/uncompressed"};
        void SetUp() override { manager.setGraphicsDevice(device); }
    };
}

TEST_F(ContentTextureMetadataTest, CachedResultsShareNamesAndTagsInBothDirections)
{
    MetadataTag firstTag, secondTag;
    auto first = manager.Load<Texture2D>("white-1");
    first.setNameProperty("first name");
    first.setTagProperty(&firstTag);
    auto second = manager.Load<Texture2D>("white-1");
    EXPECT_EQ(first.GetRendererWeak().lock(), second.GetRendererWeak().lock());
    EXPECT_EQ(second.getNameProperty(), "first name");
    EXPECT_EQ(second.getTagProperty(), &firstTag);

    second.setNameProperty("second name");
    second.setTagProperty(&secondTag);
    EXPECT_EQ(first.getNameProperty(), "second name");
    EXPECT_EQ(first.getTagProperty(), &secondTag);
    auto third = manager.Load<Texture2D>("white-1");
    EXPECT_EQ(third.getNameProperty(), "second name");
    EXPECT_EQ(third.getTagProperty(), &secondTag);

    second.setNameProperty("");
    second.setTagProperty(nullptr);
    EXPECT_TRUE(first.getNameProperty().empty());
    EXPECT_EQ(first.getTagProperty(), nullptr);
    EXPECT_TRUE(third.getNameProperty().empty());
    EXPECT_EQ(third.getTagProperty(), nullptr);
}

TEST_F(ContentTextureMetadataTest, ContentCopiesAndMovesKeepMetadataThroughBaseReferences)
{
    auto loaded = manager.Load<Texture2D>("white-1");
    Texture2D copied(loaded);
    Texture2D moved(std::move(copied));
    Texture2D assigned;
    assigned = loaded;
    Texture2D moveAssigned;
    moveAssigned = std::move(assigned);
    MetadataTag tag;
    GraphicsResource& writer = moved;
    writer.setNameProperty("shared through base");
    writer.setTagProperty(&tag);
    const GraphicsResource& reader = moveAssigned;
    EXPECT_EQ(reader.getNameProperty(), "shared through base");
    EXPECT_EQ(reader.getTagProperty(), &tag);
    EXPECT_EQ(loaded.getNameProperty(), "shared through base");
    EXPECT_EQ(loaded.getTagProperty(), &tag);
}

TEST_F(ContentTextureMetadataTest, ResourceDestroyedReportsCurrentSharedMetadata)
{
    MetadataTag tag;
    auto texture = manager.Load<Texture2D>("white-1");
    texture.setNameProperty("updated content name");
    texture.setTagProperty(&tag);
    using Notifications = std::vector<std::pair<std::string, System::Object*>>;
    auto notifications = std::make_shared<Notifications>();
    // The device owns this callback beyond the test body's locals.
    device.ResourceDestroyed += [notifications](System::Object*, const auto& args)
    {
        notifications->emplace_back(args.getNameProperty(), args.getTagProperty());
    };
    manager.Unload();
    ASSERT_FALSE(notifications->empty());
    for (const auto& [name, actualTag] : *notifications)
    {
        EXPECT_EQ(name, "updated content name");
        EXPECT_EQ(actualTag, &tag);
    }
}

TEST_F(ContentTextureMetadataTest, OtherManagersAndReloadedAssetsHaveSeparateMetadata)
{
    ContentManager other(nullptr, manager.getRootDirectoryProperty());
    other.setGraphicsDevice(device);
    auto first = manager.Load<Texture2D>("white-1");
    auto separate = other.Load<Texture2D>("white-1");
    const auto originalName = separate.getNameProperty();
    MetadataTag tag;
    first.setNameProperty("old lifetime");
    first.setTagProperty(&tag);
    EXPECT_EQ(separate.getNameProperty(), originalName);
    EXPECT_EQ(separate.getTagProperty(), nullptr);
    manager.Unload();
    auto reloaded = manager.Load<Texture2D>("white-1");
    EXPECT_EQ(reloaded.getNameProperty(), originalName);
    EXPECT_EQ(reloaded.getTagProperty(), nullptr);
    reloaded.setNameProperty("new lifetime");
    EXPECT_EQ(first.getNameProperty(), "old lifetime");
    EXPECT_EQ(first.getTagProperty(), &tag);
}

TEST(ContentTextureMetadataControl, OrdinaryUnrecordedCopiesRemainIndependent)
{
    Texture2D original;
    MetadataTag initialTag, replacementTag;
    original.setNameProperty("original");
    original.setTagProperty(&initialTag);
    Texture2D copy(original);
    EXPECT_EQ(copy.getNameProperty(), "original");
    EXPECT_EQ(copy.getTagProperty(), &initialTag);
    copy.setNameProperty("independent");
    copy.setTagProperty(&replacementTag);
    EXPECT_EQ(original.getNameProperty(), "original");
    EXPECT_EQ(original.getTagProperty(), &initialTag);
}
