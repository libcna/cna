// SPDX-License-Identifier: MS-PL

#include <filesystem>

#include <gtest/gtest.h>

#include "System/InvalidOperationException.hpp"
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
// Correct for every indexable file, not a stub -- DRM-wrapped containers are not
// indexable at all (plan_media.md MEDIA-185).
TEST(SongTest, IsProtectedIsAlwaysFalse)
{
    Song song(kRealFixture);
    EXPECT_FALSE(song.getIsProtectedProperty());
}

// A standalone Song has no scanned tag data, so "not rated" is correct here. Library songs DO
// carry a real rating -- see MediaLibraryTests' LibrarySongsCarryTheirRealRatingFromTags
// (plan_media.md MEDIA-184).
TEST(SongTest, IsRatedIsFalseForAStandaloneSong)
{
    Song song(kRealFixture);
    EXPECT_FALSE(song.getIsRatedProperty());
}

TEST(SongTest, RatingIsZeroForAStandaloneSong)
{
    Song song(kRealFixture);
    EXPECT_EQ(song.getRatingProperty(), 0);
}

// A standalone Song has no scanned tag data, so 0 ("unknown") is correct here. Library
// songs DO carry their real track number -- see MediaLibraryTests'
// LibrarySongsCarryTheirRealTrackNumberFromTags (plan_media.md MEDIA-181).
TEST(SongTest, TrackNumberIsZeroForAStandaloneSong)
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

// plan_media.md MEDIA-217 (found by external code review): FromUri's own header promised "File URI
// or local path", but the raw string went straight to the Song constructor, which then asked
// std::filesystem::exists about a literal "file:///..." -- so a real file:// URI ALWAYS failed.
// FNA resolves this via Uri.LocalPath and rejects non-file schemes (Song.cs).
TEST(SongTest, FromUriAcceptsARealFileUri)
{
    // A file URI names an ABSOLUTE path ("file://<authority>/<path>"), so build it from the
    // fixture's real absolute location rather than a relative one.
    const std::string abs = std::filesystem::absolute(kRealFixture).string();

    // NOTE: `abs` already begins with '/', so "file://" + abs IS the empty-authority spelling
    // "file:///path". An earlier version of this test claimed to check "two forms" but built the
    // identical string twice -- caught by external code review (plan_media.md MEDIA-219). The
    // genuinely distinct forms are asserted separately below.
    Song* song = nullptr;
    ASSERT_NO_THROW(song = Song::FromUri("Sunrise", "file://" + abs));
    ASSERT_NE(song, nullptr);
    EXPECT_EQ(song->getNameProperty(), "Sunrise");
    delete song;
}

// Percent-escapes must be decoded, or any path containing a space fails.
TEST(SongTest, FromUriPercentDecodesEscapedCharacters)
{
    // Percent-escape every space in the real absolute path; without decoding, the file is not found.
    std::string abs = std::filesystem::absolute(kRealFixture).string();
    std::string escaped;
    for (char c : abs) { if (c == ' ') escaped += "%20"; else escaped += c; }
    ASSERT_NE(escaped, abs) << "fixture path must contain a space for this test to mean anything";

    Song* song = nullptr;
    ASSERT_NO_THROW(song = Song::FromUri("Sunrise", "file://" + escaped));
    ASSERT_NE(song, nullptr);
    delete song;
}

// FNA throws InvalidOperationException("Only local file URIs are supported for now") for any
// non-file scheme; CNA matches that rather than silently treating it as a path.
TEST(SongTest, FromUriRejectsNonFileSchemes)
{
    EXPECT_THROW((void)Song::FromUri("Remote", "http://example.com/song.mp3"),
                 System::InvalidOperationException);
    EXPECT_THROW((void)Song::FromUri("Remote", "https://example.com/song.mp3"),
                 System::InvalidOperationException);
}

// A plain path with no scheme keeps working exactly as before -- every existing caller relies on it.
TEST(SongTest, FromUriStillAcceptsAPlainPath)
{
    Song* song = nullptr;
    ASSERT_NO_THROW(song = Song::FromUri("Sunrise", kRealFixture));
    ASSERT_NE(song, nullptr);
    delete song;
}

// plan_media.md MEDIA-219: the three genuinely distinct spellings RFC 8089 permits for a local
// file, all of which .NET's Uri.LocalPath resolves to the same path. The first version of this fix
// only handled one of them.
TEST(SongTest, FromUriAcceptsEveryLocalFileUriSpelling)
{
    const std::string abs = std::filesystem::absolute(kRealFixture).string();

    // 1. Empty authority: file:///path
    Song* a = nullptr;
    ASSERT_NO_THROW(a = Song::FromUri("S", "file://" + abs));
    ASSERT_NE(a, nullptr);
    delete a;

    // 2. Explicit localhost authority: file://localhost/path
    Song* b = nullptr;
    ASSERT_NO_THROW(b = Song::FromUri("S", "file://localhost" + abs));
    ASSERT_NE(b, nullptr);
    delete b;

    // 3. No authority component at all: file:/path
    Song* c = nullptr;
    ASSERT_NO_THROW(c = Song::FromUri("S", "file:" + abs));
    ASSERT_NE(c, nullptr);
    delete c;
}

// A non-empty, non-localhost authority is a REMOTE host. Silently dropping it (as the first version
// of this fix did) would resolve a remote path to a local one -- a genuinely wrong file. It becomes
// a UNC path instead, matching Uri.LocalPath, so an unreachable share fails as not-found rather
// than quietly succeeding against the wrong file (plan_media.md MEDIA-219).
TEST(SongTest, FromUriTreatsARemoteAuthorityAsUncRatherThanSilentlyDroppingIt)
{
    // Constructed so that the CORRECT and the BUGGY behaviours give OPPOSITE results, which a
    // naive "file://server/share/song.ogg" cannot do -- there, dropping the authority yields
    // "/share/song.ogg", which is also missing, so the test would pass against broken code. My
    // first version of this test made exactly that mistake and only mutation testing exposed it.
    //
    // Here the path after the authority is a REAL, existing file:
    //   correct (UNC)          -> "//remotehost/<abs>"  -> does not exist -> throws
    //   buggy (authority lost) -> "<abs>"               -> exists         -> succeeds
    const std::string abs = std::filesystem::absolute(kRealFixture).string();

    EXPECT_THROW((void)Song::FromUri("S", "file://remotehost" + abs),
                 System::IO::FileNotFoundException)
        << "a remote authority was silently dropped, resolving a remote URI to a LOCAL file";
}

// A drive-letter path must not be mistaken for a URI scheme ("C:" is a path, not a scheme).
TEST(SongTest, FromUriDoesNotMistakeAWindowsDriveLetterForAScheme)
{
    // Fails as a missing file (the path does not exist here), not as an unsupported scheme.
    EXPECT_THROW((void)Song::FromUri("S", "C:/music/song.mp3"),
                 System::IO::FileNotFoundException);
}
