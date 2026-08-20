// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Media/MediaSource.hpp"

using Microsoft::Xna::Framework::Media::MediaSource;
using Microsoft::Xna::Framework::Media::MediaSourceType;

// plans/plan_media.md MEDIA-61: real implementation -- no FNA logic to port (MediaSource is 100% stub
// upstream), just the API shape.
TEST(MediaSourceTest, GetAvailableMediaSourcesReturnsExactlyOneLocalDevice)
{
    auto sources = MediaSource::GetAvailableMediaSources();
    ASSERT_EQ(sources.size(), 1u);
    EXPECT_EQ(sources[0]->getMediaSourceTypeProperty(), MediaSourceType::LocalDevice);
    EXPECT_FALSE(sources[0]->getNameProperty().empty());
    delete sources[0];
}

TEST(MediaSourceTest, ToStringReturnsName)
{
    auto sources = MediaSource::GetAvailableMediaSources();
    ASSERT_EQ(sources.size(), 1u);
    EXPECT_EQ(sources[0]->ToString(), sources[0]->getNameProperty());
    delete sources[0];
}

TEST(MediaSourceTest, GetTypeNameIsFullyQualified)
{
    auto sources = MediaSource::GetAvailableMediaSources();
    EXPECT_EQ(sources[0]->GetTypeName(), "Microsoft.Xna.Framework.Media.MediaSource");
    delete sources[0];
}
