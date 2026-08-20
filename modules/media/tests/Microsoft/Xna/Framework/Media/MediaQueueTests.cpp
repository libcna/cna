// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Media/MediaQueue.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

using Microsoft::Xna::Framework::Media::MediaQueue;
using Microsoft::Xna::Framework::Media::Song;

namespace
{
    constexpr const char* kRealFixture =
        "tests/assets/media/music/Artist One/Album Alpha/01 - Sunrise.ogg";
}

TEST(MediaQueueTest, StartsEmptyWithNoActiveSong)
{
    MediaQueue queue;
    EXPECT_EQ(queue.getCountProperty(), 0);
    EXPECT_EQ(queue.getActiveSongIndexProperty(), -1);
    EXPECT_EQ(queue.getActiveSongProperty(), nullptr);
}

TEST(MediaQueueTest, AddIncreasesCountAndActiveSongTracksIndex)
{
    MediaQueue queue;
    queue.Add(new Song(kRealFixture, "Sunrise"));
    queue.Add(new Song(kRealFixture, "Sunrise Again"));
    EXPECT_EQ(queue.getCountProperty(), 2);

    queue.setActiveSongIndexProperty(1);
    ASSERT_NE(queue.getActiveSongProperty(), nullptr);
    EXPECT_EQ(queue.getActiveSongProperty()->getNameProperty(), "Sunrise Again");
}

TEST(MediaQueueTest, ClearResetsCountAndActiveIndex)
{
    MediaQueue queue;
    queue.Add(new Song(kRealFixture));
    queue.setActiveSongIndexProperty(0);
    queue.Clear();
    EXPECT_EQ(queue.getCountProperty(), 0);
    EXPECT_EQ(queue.getActiveSongIndexProperty(), -1);
}

// plans/plan_media.md MEDIA-11: the indexer now throws System::ArgumentOutOfRangeException, matching
// the majority project precedent, instead of a bare std::out_of_range.
TEST(MediaQueueTest, IndexerThrowsArgumentOutOfRangeExceptionWhenOutOfBounds)
{
    MediaQueue queue;
    queue.Add(new Song(kRealFixture));
    EXPECT_THROW((void)queue[-1], System::ArgumentOutOfRangeException);
    EXPECT_THROW((void)queue[1], System::ArgumentOutOfRangeException);
}

TEST(MediaQueueTest, IndexerReturnsCorrectSongWithinBounds)
{
    MediaQueue queue;
    queue.Add(new Song(kRealFixture, "First"));
    queue.Add(new Song(kRealFixture, "Second"));
    ASSERT_NE(queue[0], nullptr);
    ASSERT_NE(queue[1], nullptr);
    EXPECT_EQ(queue[0]->getNameProperty(), "First");
    EXPECT_EQ(queue[1]->getNameProperty(), "Second");
}

TEST(MediaQueueTest, GetTypeNameIsFullyQualified)
{
    MediaQueue queue;
    EXPECT_EQ(queue.GetTypeName(), "Microsoft.Xna.Framework.Media.MediaQueue");
}
