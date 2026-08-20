// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Media/SongCollection.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

using Microsoft::Xna::Framework::Media::Song;
using Microsoft::Xna::Framework::Media::SongCollection;

namespace
{
    constexpr const char* kRealFixture =
        "tests/assets/media/music/Artist One/Album Alpha/01 - Sunrise.ogg";
}

TEST(SongCollectionTest, CountReflectsConstructorArgument)
{
    Song a(kRealFixture, "A");
    Song b(kRealFixture, "B");
    SongCollection collection({&a, &b});
    EXPECT_EQ(collection.getCountProperty(), 2);
    EXPECT_FALSE(collection.getIsDisposedProperty());
}

// plans/plan_media.md MEDIA-12: the indexer now throws System::ArgumentOutOfRangeException, matching
// the majority project precedent, instead of a bare std::out_of_range.
TEST(SongCollectionTest, IndexerThrowsArgumentOutOfRangeExceptionWhenOutOfBounds)
{
    Song a(kRealFixture);
    SongCollection collection({&a});
    EXPECT_THROW((void)collection[-1], System::ArgumentOutOfRangeException);
    EXPECT_THROW((void)collection[1], System::ArgumentOutOfRangeException);
}

TEST(SongCollectionTest, IndexerReturnsCorrectSongWithinBounds)
{
    Song a(kRealFixture, "First");
    Song b(kRealFixture, "Second");
    SongCollection collection({&a, &b});
    ASSERT_NE(collection[0], nullptr);
    EXPECT_EQ(collection[0]->getNameProperty(), "First");
    EXPECT_EQ(collection[1]->getNameProperty(), "Second");
}

TEST(SongCollectionTest, DisposeClearsAndMarksDisposed)
{
    Song a(kRealFixture);
    SongCollection collection({&a});
    collection.Dispose();
    EXPECT_TRUE(collection.getIsDisposedProperty());
    EXPECT_EQ(collection.getCountProperty(), 0);
}

TEST(SongCollectionTest, IterationVisitsEverySong)
{
    Song a(kRealFixture, "First");
    Song b(kRealFixture, "Second");
    SongCollection collection({&a, &b});

    std::vector<std::string> names;
    for (Song* song : collection)
    {
        names.push_back(song->getNameProperty());
    }
    ASSERT_EQ(names.size(), 2u);
    EXPECT_EQ(names[0], "First");
    EXPECT_EQ(names[1], "Second");
}

TEST(SongCollectionTest, GetTypeNameIsFullyQualified)
{
    Song a(kRealFixture);
    SongCollection collection({&a});
    EXPECT_EQ(collection.GetTypeName(), "Microsoft.Xna.Framework.Media.SongCollection");
}
