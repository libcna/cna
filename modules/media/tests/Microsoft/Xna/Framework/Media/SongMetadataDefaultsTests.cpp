// SPDX-License-Identifier: MS-PL
#include <array>
#include <filesystem>
#include <memory>
#include <gtest/gtest.h>
#include "CNA/Internal/PathUtf8.hpp"
#include "CNA/Internal/TitlePath.hpp"
#include "CNA/Platform/CurrentPlatform.hpp"
#include "Microsoft/Xna/Framework/Media/Song.hpp"

using Microsoft::Xna::Framework::Media::Song;

namespace
{
constexpr const char* fixture = "tests/assets/media/music/Artist One/Album Alpha/01 - Sunrise.ogg";

void CheckUriDefaults(const Song& song)
{
    // Refresh Song::.ctor(string,Uri) IL_003a..004e, IL_00a0..00cb:
    // zero duration; name retained; rating/track remain -1 without native metadata.
    EXPECT_EQ(song.getNameProperty(), "URI metadata");
    EXPECT_EQ(song.ToString(), "URI metadata");
    EXPECT_EQ(song.getDurationProperty().getTicksProperty(), 0);
    EXPECT_EQ(song.getRatingProperty(), -1);
    EXPECT_EQ(song.getTrackNumberProperty(), -1);
    // get_IsRated IL_0007..000f: rating > 0; get_PlayCount IL_0006..001f and
    // get_IsProtected IL_0006..0023 use 0/false when native data is absent.
    EXPECT_FALSE(song.getIsRatedProperty());
    EXPECT_FALSE(song.getIsProtectedProperty());
    EXPECT_EQ(song.getPlayCountProperty(), 0);
    EXPECT_FALSE(song.getIsDisposedProperty());
}
}

TEST(SongMetadataDefaultsTest, UriOverloadsRetainUnavailableMetadataSentinels)
{
    const auto absolute = std::filesystem::absolute(fixture);
    const auto path = CNA::Internal::PathToGenericUtf8(absolute);
    std::string uriPath = path;
    if (uriPath.front() != '/') uriPath.insert(uriPath.begin(), '/');
    const std::string uri = "file://" + uriPath;
    const std::array<std::unique_ptr<Song>, 3> songs = {
        std::unique_ptr<Song>(Song::FromUri("URI metadata", path)),
        std::unique_ptr<Song>(Song::FromUri("URI metadata", uri)),
        std::unique_ptr<Song>(Song::FromUri("URI metadata", System::Uri(uri)))
    };
    for (const auto& song : songs)
    {
        ASSERT_NE(song, nullptr);
        CheckUriDefaults(*song);
        EXPECT_EQ(song->getHandle(), path);
    }
}

TEST(SongMetadataDefaultsTest, RelativeUriRetainsUnavailableMetadataSentinels)
{
    // Resolve the fixture relative to the runner's title root without changing global state.
    const auto configured = CNA::Internal::TryGetTitlePath();
    std::string title = configured ? *configured :
        CNA::Platform::GetCurrentPlatform().GetFileSystem()->GetBasePath();
    if (title.empty()) title = CNA::Internal::PathToGenericUtf8(std::filesystem::current_path());
    const auto absolute = std::filesystem::absolute(fixture);
    const auto relative = std::filesystem::relative(absolute, CNA::Internal::PathFromUtf8(title));
    std::unique_ptr<Song> song(Song::FromUri("URI metadata", CNA::Internal::PathToGenericUtf8(relative)));
    ASSERT_NE(song, nullptr);
    CheckUriDefaults(*song);
    EXPECT_EQ(std::filesystem::weakly_canonical(CNA::Internal::PathFromUtf8(song->getHandle())),
              std::filesystem::weakly_canonical(absolute));
}

TEST(SongMetadataDefaultsTest, FileDurationConstructorKeepsZeroMetadataAndExactSignedMilliseconds)
{
    // Refresh Song::.ctor(string,string,int) overrides the -1 field initializers
    // at IL_0060..0069 and converts integer milliseconds at IL_0081..008c.
    // 1 millisecond = 10,000 ticks. These signed products fit int64 exactly.
    struct Case { int milliseconds; long long ticks; };
    for (const Case value : {Case{0, 0}, Case{1234, 12340000}, Case{-7, -70000},
                            Case{2147483647, 21474836470000LL},
                            Case{-2147483647 - 1, -21474836480000LL}})
    {
        SCOPED_TRACE(value.milliseconds);
        Song song(fixture, "File metadata", value.milliseconds);
        EXPECT_EQ(song.getDurationProperty().getTicksProperty(), value.ticks);
        EXPECT_EQ(song.getNameProperty(), "File metadata");
        EXPECT_EQ(song.getRatingProperty(), 0);
        EXPECT_EQ(song.getTrackNumberProperty(), 0);
        EXPECT_FALSE(song.getIsRatedProperty());
        EXPECT_FALSE(song.getIsProtectedProperty());
        EXPECT_EQ(song.getPlayCountProperty(), 0);
    }
}

TEST(SongMetadataDefaultsTest, CopiesRetainUriSentinelsAndIndependentLifetime)
{
    std::unique_ptr<Song> source(Song::FromUri("URI metadata",
        CNA::Internal::PathToGenericUtf8(std::filesystem::absolute(fixture))));
    ASSERT_NE(source, nullptr);
    // CNAEXT copy contract, Song.hpp:49..52: copy every value, including unavailable metadata.
    Song copy(*source);
    Song assigned(fixture, "Old name", 999);
    assigned.setPlayCountProperty(8);
    assigned = *source;
    CheckUriDefaults(copy);
    CheckUriDefaults(assigned);
    source->Dispose();
    EXPECT_TRUE(source->getIsDisposedProperty());
    CheckUriDefaults(copy);
    CheckUriDefaults(assigned);
}
