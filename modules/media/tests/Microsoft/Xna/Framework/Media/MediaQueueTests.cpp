// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <limits>
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

// MEDIA-11: the indexer now throws System::ArgumentOutOfRangeException, matching
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

TEST(MediaQueueTest, Tq002ClearedQueueCanBeReusedWithoutAnImplicitActiveSong)
{
    // FNA MediaQueue: Clear sets ActiveSongIndex=-1; Add does not select a song.
    MediaQueue queue;
    queue.Add(new Song(kRealFixture, "Old"));
    queue.setActiveSongIndexProperty(0);
    queue.Clear();
    EXPECT_EQ(queue.getActiveSongProperty(), nullptr);
    queue.Add(new Song(kRealFixture, "Replacement"));
    EXPECT_EQ(queue.getCountProperty(), 1);
    EXPECT_EQ(queue.getActiveSongIndexProperty(), -1);
    EXPECT_EQ(queue.getActiveSongProperty(), nullptr);
    queue.setActiveSongIndexProperty(0);
    ASSERT_NE(queue.getActiveSongProperty(), nullptr);
    EXPECT_EQ(queue.getActiveSongProperty()->getNameProperty(), "Replacement");
    // XNA set_ActiveSongIndex IL_0000..0020 clamps a negative assignment to the first song.
    queue.setActiveSongIndexProperty(-1);
    EXPECT_EQ(queue.getActiveSongIndexProperty(), 0);
    EXPECT_EQ(queue.getActiveSongProperty(), queue[0]);
    EXPECT_EQ(queue[0]->getNameProperty(), "Replacement");
}


TEST(MediaQueueTest, Tq023ActiveIndexClampsToBothEndsIncludingIntegerExtremes)
{
    // XNA MediaQueue.set_ActiveSongIndex IL_0000..0020: max(value,0), then min(value,Count-1).
    MediaQueue queue;
    queue.Add(new Song(kRealFixture, "First"));
    queue.Add(new Song(kRealFixture, "Middle"));
    queue.Add(new Song(kRealFixture, "Last"));
    for (int index : {-1, -20, std::numeric_limits<int>::min()})
    {
        queue.setActiveSongIndexProperty(index);
        EXPECT_EQ(queue.getActiveSongIndexProperty(), 0);
        EXPECT_EQ(queue.getActiveSongProperty(), queue[0]);
    }
    for (int index : {3, 20, std::numeric_limits<int>::max()})
    {
        queue.setActiveSongIndexProperty(index);
        EXPECT_EQ(queue.getActiveSongIndexProperty(), 2);
        EXPECT_EQ(queue.getActiveSongProperty(), queue[2]);
    }
    queue.setActiveSongIndexProperty(1);
    EXPECT_EQ(queue.getActiveSongIndexProperty(), 1);
    EXPECT_EQ(queue.getActiveSongProperty(), queue[1]);
}
