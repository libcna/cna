// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Media/MediaSource.hpp"

using Microsoft::Xna::Framework::Media::MediaSource;
using Microsoft::Xna::Framework::Media::MediaSourceType;

// MEDIA-61: real implementation -- no FNA logic to port (MediaSource is 100% stub
// upstream), just the API shape.
TEST(MediaSourceTest, GetAvailableMediaSourcesReturnsExactlyOneLocalDevice)
{
    auto sources = MediaSource::GetAvailableMediaSources();
    ASSERT_EQ(sources.size(), 1u);
    EXPECT_EQ(sources[0]->getMediaSourceTypeProperty(), MediaSourceType::LocalDevice);
    EXPECT_FALSE(sources[0]->getNameProperty().empty());
}

TEST(MediaSourceTest, ToStringReturnsName)
{
    auto sources = MediaSource::GetAvailableMediaSources();
    ASSERT_EQ(sources.size(), 1u);
    EXPECT_EQ(sources[0]->ToString(), sources[0]->getNameProperty());
}

TEST(MediaSourceTest, GetTypeNameIsFullyQualified)
{
    auto sources = MediaSource::GetAvailableMediaSources();
    EXPECT_EQ(sources[0]->GetTypeName(), "Microsoft.Xna.Framework.Media.MediaSource");
}

TEST(MediaSourceTest, Tq052EnumerationUsesRefreshNameAndFreshSourceObjects)
{
    // Refresh GetAvailableMediaSources IL_0000..0010 allocates one fresh local source;
    // .ctor IL_0007..0013 reads WmpMediaSource from FrameworkResources.resources.
    auto first = MediaSource::GetAvailableMediaSources();
    auto second = MediaSource::GetAvailableMediaSources();
    ASSERT_EQ(first.size(), 1u);
    ASSERT_EQ(second.size(), 1u);
    ASSERT_NE(first[0], nullptr);
    ASSERT_NE(second[0], nullptr);
    EXPECT_NE(first[0], second[0]);
    EXPECT_EQ(first[0]->getMediaSourceTypeProperty(), MediaSourceType::LocalDevice);
    EXPECT_EQ(second[0]->getMediaSourceTypeProperty(), MediaSourceType::LocalDevice);
    EXPECT_EQ(first[0]->getNameProperty(), "Local Windows Media Player library");
    EXPECT_EQ(first[0]->ToString(), "Local Windows Media Player library");
    EXPECT_EQ(second[0]->ToString(), "Local Windows Media Player library");
    // No equality/hash override in the Refresh MediaSource class: inherited Object identity.
    EXPECT_TRUE(first[0]->Equals(first[0].get()));
    EXPECT_FALSE(first[0]->Equals(second[0].get()));
    EXPECT_FALSE(first[0]->Equals(static_cast<const System::Object*>(nullptr)));
}
