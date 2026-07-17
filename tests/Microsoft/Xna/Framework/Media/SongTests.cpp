// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Media/Song.hpp"
#include "System/IO/FileNotFoundException.hpp"

using Microsoft::Xna::Framework::Media::Song;

namespace
{
    constexpr const char* kRealFixture =
        "tests/assets/media/music/Artist One/Album Alpha/01 - Sunrise.ogg";
    constexpr const char* kOtherRealFixture =
        "tests/assets/media/music/Artist One/Album Beta/01 - Twilight.mp3";
}

// plan_media.md MEDIA-10: the ctor's missing-file path now throws the same typed exception FNA
// throws (FileNotFoundException(fileName)) instead of a bare std::runtime_error.
TEST(SongTest, ConstructorThrowsFileNotFoundExceptionForMissingFile)
{
    EXPECT_THROW(
        { Song song("tests/assets/media/music/does-not-exist.ogg"); },
        System::IO::FileNotFoundException);
}

TEST(SongTest, ConstructorDoesNotThrowForExistingFile)
{
    EXPECT_NO_THROW({ Song song(kRealFixture, "Sunrise"); });
}

TEST(SongTest, NameAndDurationFromThreeArgConstructor)
{
    Song song(kRealFixture, "Sunrise", 2000);
    EXPECT_EQ(song.getNameProperty(), "Sunrise");
    EXPECT_EQ(song.getDurationProperty(), System::TimeSpan::FromMilliseconds(2000));
}

// plan_media.md MEDIA-13: these four getters are FNA-faithful hardcoded constants, not
// unfinished stubs -- asserted explicitly so a future audit doesn't "fix" correct behavior.
TEST(SongTest, IsProtectedIsAlwaysFalse)
{
    Song song(kRealFixture);
    EXPECT_FALSE(song.getIsProtectedProperty());
}

TEST(SongTest, IsRatedIsAlwaysFalse)
{
    Song song(kRealFixture);
    EXPECT_FALSE(song.getIsRatedProperty());
}

TEST(SongTest, RatingIsAlwaysZero)
{
    Song song(kRealFixture);
    EXPECT_EQ(song.getRatingProperty(), 0);
}

TEST(SongTest, TrackNumberIsAlwaysZero)
{
    Song song(kRealFixture);
    EXPECT_EQ(song.getTrackNumberProperty(), 0);
}

// plan_media.md MEDIA-14: GetHashCode() is deliberately content-based (hash of the resolved
// handle), unlike FNA's identity-based base.GetHashCode() -- locks in the improved behavior.
TEST(SongTest, GetHashCodeIsContentBasedNotIdentityBased)
{
    Song a(kRealFixture, "Sunrise");
    Song b(kRealFixture, "Sunrise");
    EXPECT_NE(&a, &b);
    EXPECT_TRUE(a.Equals(&b));
    EXPECT_EQ(a.GetHashCode(), b.GetHashCode());
}

TEST(SongTest, EqualsAndOperatorsCompareByHandle)
{
    Song a(kRealFixture, "Sunrise");
    Song b(kRealFixture, "Different Display Name");
    EXPECT_TRUE(a.Equals(&b));
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

// plan_media.md MEDIA-76: the unequal case -- two different handles must NOT compare equal, even
// with the same display Name, confirming Equals()/operator==/operator!= genuinely compare by
// handle rather than always returning true.
TEST(SongTest, EqualsAndOperatorsCompareUnequalForDifferentHandles)
{
    Song a(kRealFixture, "Same Display Name");
    Song b(kOtherRealFixture, "Same Display Name");
    EXPECT_FALSE(a.Equals(&b));
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);
}

TEST(SongTest, PlayCountGetSet)
{
    Song song(kRealFixture);
    EXPECT_EQ(song.getPlayCountProperty(), 0);
    song.setPlayCountProperty(3);
    EXPECT_EQ(song.getPlayCountProperty(), 3);
}

TEST(SongTest, DisposeSetsIsDisposed)
{
    Song song(kRealFixture);
    EXPECT_FALSE(song.getIsDisposedProperty());
    song.Dispose();
    EXPECT_TRUE(song.getIsDisposedProperty());
}

TEST(SongTest, FromUriConstructsFromLocalPath)
{
    Song* song = Song::FromUri("Sunrise", kRealFixture);
    ASSERT_NE(song, nullptr);
    EXPECT_EQ(song->getNameProperty(), "Sunrise");
    delete song;
}

TEST(SongTest, GetTypeNameIsFullyQualified)
{
    Song song(kRealFixture);
    EXPECT_EQ(song.GetTypeName(), "Microsoft.Xna.Framework.Media.Song");
}
