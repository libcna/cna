// SPDX-License-Identifier: MS-PL

#include <cstring>
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

    class NonSongObject final : public System::Object
    {
    public:
        [[nodiscard]] const std::string& GetTypeName() const override
        {
            static const std::string typeName = "CNA.Tests.NonSongObject";
            return typeName;
        }
    };
}

// plans/plan_media.md MEDIA-10: the ctor's missing-file path now throws the same typed exception FNA
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

// plans/plan_media.md MEDIA-13: these four getters are FNA-faithful hardcoded constants, not
// unfinished stubs -- asserted explicitly so a future audit doesn't "fix" correct behavior.
// Correct for every indexable file, not a stub -- DRM-wrapped containers are not
// indexable at all (plans/plan_media.md MEDIA-185).
TEST(SongTest, IsProtectedIsAlwaysFalse)
{
    Song song(kRealFixture);
    EXPECT_FALSE(song.getIsProtectedProperty());
}

// A standalone Song has no scanned tag data, so "not rated" is correct here. Library songs DO
// carry a real rating -- see MediaLibraryTests' LibrarySongsCarryTheirRealRatingFromTags
// (plans/plan_media.md MEDIA-184).
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
// LibrarySongsCarryTheirRealTrackNumberFromTags (plans/plan_media.md MEDIA-181).
TEST(SongTest, TrackNumberIsZeroForAStandaloneSong)
{
    Song song(kRealFixture);
    EXPECT_EQ(song.getTrackNumberProperty(), 0);
}

// plans/plan_media.md MEDIA-14: GetHashCode() is deliberately content-based (hash of the resolved
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

// plans/plan_media.md MEDIA-76: the unequal case -- two different handles must NOT compare equal, even
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

// plans/plan_media.md MEDIA-234: cover the System.Object override separately from Equals(Song).
TEST(SongTest, ObjectEqualsChecksRuntimeTypeAndValue)
{
    Song song(kRealFixture, "Sunrise");
    Song equalSong(kRealFixture, "Different Display Name");
    Song unequalSong(kOtherRealFixture, "Sunrise");
    NonSongObject wrongType;

    const System::Object* equalObject = &equalSong;
    const System::Object* unequalObject = &unequalSong;
    const System::Object* wrongTypeObject = &wrongType;
    const System::Object* nullObject = nullptr;
    EXPECT_TRUE(song.Equals(equalObject));
    EXPECT_FALSE(song.Equals(unequalObject));
    EXPECT_FALSE(song.Equals(wrongTypeObject));
    EXPECT_FALSE(song.Equals(nullObject));
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

namespace
{
    // Builds an RFC 8089 file URI from a real path, correctly on BOTH platforms.
    //
    // Hand-concatenating "file://" + path.string() is NOT portable, which is how an earlier version
    // of these tests got it wrong (plans/plan_media.md MEDIA-228):
    //   * on Windows, string() yields backslashes ("C:\\x"), which are not valid in a URI at all;
    //   * even after converting to forward slashes, "file://C:/x" parses "C:" as the AUTHORITY,
    //     making it look like a remote host rather than a local drive.
    // The correct Windows spelling needs a third slash: "file:///C:/x".
    //
    // generic_string() normalises separators to '/', and prefixing '/' when the path does not
    // already start with one turns "C:/x" into "/C:/x", yielding "file:///C:/x". On POSIX the path
    // already starts with '/', so the result is the usual "file:///home/...".
    std::string MakeFileUri(const std::filesystem::path& p)
    {
        // Always resolve to absolute first: a file URI names an absolute location, and the test
        // fixtures are given as paths relative to the working directory.
        std::string generic = std::filesystem::absolute(p).generic_string();
        if (generic.empty() || generic.front() != '/')
        {
            generic.insert(generic.begin(), '/');
        }
        return "file://" + generic;
    }
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

// plans/plan_media.md MEDIA-217 (found by external code review): FromUri's own header promised "File URI
// or local path", but the raw string went straight to the Song constructor, which then asked
// std::filesystem::exists about a literal "file:///..." -- so a real file:// URI ALWAYS failed.
// FNA resolves this via Uri.LocalPath and rejects non-file schemes (Song.cs).
TEST(SongTest, FromUriAcceptsARealFileUri)
{
    // A file URI names an ABSOLUTE path ("file://<authority>/<path>"), so build it from the
    // fixture's real absolute location rather than a relative one.

    // MakeFileUri() produces the empty-authority spelling ("file:///path") on both platforms.
    // Two earlier versions of this test got this wrong: one built the identical string twice while
    // claiming to check "two forms" (MEDIA-219), and one hand-concatenated the native path, which
    // is invalid on Windows (MEDIA-228). The genuinely distinct spellings are asserted separately
    // in FromUriAcceptsEveryLocalFileUriSpelling below.
    Song* song = nullptr;
    ASSERT_NO_THROW(song = Song::FromUri("Sunrise", MakeFileUri(kRealFixture)));
    ASSERT_NE(song, nullptr);
    EXPECT_EQ(song->getNameProperty(), "Sunrise");
    delete song;
}

// Percent-escapes must be decoded, or any path containing a space fails.
TEST(SongTest, FromUriPercentDecodesEscapedCharacters)
{
    // Percent-escape every space in the real URI; without decoding, the file is not found.
    const std::string uri = MakeFileUri(kRealFixture);
    std::string escaped;
    for (char c : uri) { if (c == ' ') escaped += "%20"; else escaped += c; }
    ASSERT_NE(escaped, uri) << "fixture path must contain a space for this test to mean anything";

    Song* song = nullptr;
    ASSERT_NO_THROW(song = Song::FromUri("Sunrise", escaped));
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

// plans/plan_media.md MEDIA-219: the three genuinely distinct spellings RFC 8089 permits for a local
// file, all of which .NET's Uri.LocalPath resolves to the same path. The first version of this fix
// only handled one of them.
TEST(SongTest, FromUriAcceptsEveryLocalFileUriSpelling)
{

    // 1. Empty authority: file:///path
    Song* a = nullptr;
    ASSERT_NO_THROW(a = Song::FromUri("S", MakeFileUri(kRealFixture)));
    ASSERT_NE(a, nullptr);
    delete a;

    // The path portion, always '/'-separated and always leading with '/' (so a Windows drive
    // becomes "/C:/..."), shared by the two remaining spellings.
    const std::string uriPath = MakeFileUri(kRealFixture).substr(std::strlen("file://"));

    // 2. Explicit localhost authority: file://localhost/path
    Song* b = nullptr;
    ASSERT_NO_THROW(b = Song::FromUri("S", "file://localhost" + uriPath));
    ASSERT_NE(b, nullptr);
    delete b;

    // 3. No authority component at all: file:/path
    Song* c = nullptr;
    ASSERT_NO_THROW(c = Song::FromUri("S", "file:" + uriPath));
    ASSERT_NE(c, nullptr);
    delete c;
}

// A non-empty, non-localhost authority is a REMOTE host. Silently dropping it (as the first version
// of this fix did) would resolve a remote path to a local one -- a genuinely wrong file. It becomes
// a UNC path instead, matching Uri.LocalPath, so an unreachable share fails as not-found rather
// than quietly succeeding against the wrong file (plans/plan_media.md MEDIA-219).
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

    EXPECT_THROW((void)Song::FromUri(
                     "S", "file://remotehost" +
                          MakeFileUri(kRealFixture).substr(std::strlen("file://"))),
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

// plans/plan_media.md MEDIA-223: System.Uri.LocalPath returns ONLY the path component, so a query or
// fragment must not end up in the filename. Previously everything after "file:" was treated as
// path, so this failed with a FileNotFoundException naming a file nobody asked for.
TEST(SongTest, FromUriIgnoresQueryAndFragment)
{

    Song* withQuery = nullptr;
    ASSERT_NO_THROW(withQuery = Song::FromUri("S", MakeFileUri(kRealFixture) + "?version=1"))
        << "a query string was treated as part of the filename";
    ASSERT_NE(withQuery, nullptr);
    delete withQuery;

    Song* withFragment = nullptr;
    ASSERT_NO_THROW(withFragment = Song::FromUri("S", MakeFileUri(kRealFixture) + "#intro"))
        << "a fragment was treated as part of the filename";
    ASSERT_NE(withFragment, nullptr);
    delete withFragment;

    Song* withBoth = nullptr;
    ASSERT_NO_THROW(withBoth = Song::FromUri("S", MakeFileUri(kRealFixture) + "?version=1#intro"));
    ASSERT_NE(withBoth, nullptr);
    delete withBoth;
}

// The mirror case: a percent-encoded delimiter is a LITERAL character in the filename, not a
// delimiter, so stripping must happen before decoding -- a file genuinely named "q#.ogg" must not
// be truncated to "q".
//
// Uses '#' (%23) rather than '?' (%3F) deliberately: '?' is an ILLEGAL filename character on
// Windows, so creating the fixture would fail there before FromUri was ever exercised, making this
// test non-portable (found by external code review, plans/plan_media.md MEDIA-225). '#' is legal on both
// Windows and POSIX and exercises exactly the same code path, since fragment stripping runs first.
TEST(SongTest, FromUriTreatsPercentEncodedDelimitersAsLiteralFilenameCharacters)
{
    const std::filesystem::path dir =
        std::filesystem::absolute("tests/assets/media/music/Artist One/Album Alpha");
    const std::filesystem::path tricky = dir / "q#.ogg";

    std::filesystem::copy_file(dir / "01 - Sunrise.ogg", tricky,
                               std::filesystem::copy_options::overwrite_existing);

    Song* song = nullptr;
    // %23 is an encoded '#', so the real filename contains it -- the path must NOT be cut there.
    EXPECT_NO_THROW(song = Song::FromUri("S", MakeFileUri(dir) + "/q%23.ogg"))
        << "an encoded '#' was treated as a fragment delimiter and truncated the filename";
    delete song;

    std::filesystem::remove(tricky);
}
