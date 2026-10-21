// SPDX-License-Identifier: MS-PL
// ISSUE-015: relative FromUri paths belong to the title, not the process cwd.
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

#include "Microsoft/Xna/Framework/Media/Song.hpp"
#include "Microsoft/Xna/Framework/TitleLocation.hpp"
#include "System/IO/FileNotFoundException.hpp"
#include "System/Uri.hpp"

namespace
{
    namespace fs = std::filesystem;
    using Microsoft::Xna::Framework::Media::Song;
    using Microsoft::Xna::Framework::TitleLocation;

    class SongFromUriPathTest : public ::testing::TestWithParam<bool>
    {
    protected:
        fs::path previousWorking;
        std::string previousTitle;
        fs::path root;
        fs::path title;
        fs::path working;
        fs::path asset;
        bool ownsRoot = false;

        void SetUp() override
        {
            previousWorking = fs::current_path();
            previousTitle = TitleLocation::Path();
            asset = fs::absolute(
                "tests/assets/media/music/Artist One/Album Alpha/01 - Sunrise.ogg");
            ASSERT_TRUE(fs::is_regular_file(asset));
            root = fs::temp_directory_path() / ("cna-song-uri-path-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            ownsRoot = fs::create_directory(root);
            ASSERT_TRUE(ownsRoot);
            title = root / "title";
            working = root / "working";
            fs::create_directory(title);
            fs::create_directory(working);
            TitleLocation::setPathProperty(title.string());
            fs::current_path(working);
        }

        void TearDown() override
        {
            std::error_code error;
            fs::current_path(previousWorking, error);
            EXPECT_FALSE(error);
            TitleLocation::setPathProperty(previousTitle);
            if (ownsRoot)
            {
                fs::remove_all(root, error);
                EXPECT_FALSE(error);
            }
        }

        std::unique_ptr<Song> Open(const std::string& path)
        {
            if (GetParam())
            {
                return std::unique_ptr<Song>(Song::FromUri(
                    "Authored title", System::Uri(path, System::UriKind::RelativeOrAbsolute)));
            }
            return std::unique_ptr<Song>(Song::FromUri("Authored title", path));
        }
    };

    TEST_P(SongFromUriPathTest, FindsRelativeFileOnlyInTitleDirectory)
    {
        fs::copy_file(asset, title / "song.ogg");
        std::unique_ptr<Song> song;
        ASSERT_NO_THROW(song = Open("song.ogg"));
        ASSERT_NE(song, nullptr);
        EXPECT_TRUE(fs::equivalent(song->getHandle(), title / "song.ogg"));
        EXPECT_EQ(song->getNameProperty(), "Authored title");
    }

    TEST_P(SongFromUriPathTest, DoesNotSubstituteWorkingDirectoryFile)
    {
        fs::copy_file(asset, working / "song.ogg");
        EXPECT_THROW((void)Open("song.ogg"), System::IO::FileNotFoundException);
    }

    TEST_P(SongFromUriPathTest, TitleFileWinsOverSameNameWorkingDirectoryDecoy)
    {
        fs::copy_file(asset, title / "song.ogg");
        fs::copy_file(asset, working / "song.ogg");
        auto song = Open("song.ogg");
        ASSERT_NE(song, nullptr);
        EXPECT_TRUE(fs::equivalent(song->getHandle(), title / "song.ogg"));
        EXPECT_FALSE(fs::equivalent(song->getHandle(), working / "song.ogg"));
    }

    TEST_P(SongFromUriPathTest, AbsolutePathRemainsIndependentOfTitleDirectory)
    {
        fs::copy_file(asset, working / "song.ogg");
        auto song = Open((working / "song.ogg").string());
        ASSERT_NE(song, nullptr);
        EXPECT_TRUE(fs::equivalent(song->getHandle(), working / "song.ogg"));
        EXPECT_EQ(song->getNameProperty(), "Authored title");
    }

    INSTANTIATE_TEST_SUITE_P(StringAndTypedUri, SongFromUriPathTest,
                            ::testing::Values(false, true));
}

namespace
{
    // This existing case needs the public runtime TitleLocation contract, so it belongs
    // in the cross-module suite rather than adding runtime to cna_media's dependencies.
    TEST(SongTest, FromUriTypedOverloadAcceptsARelativeUri)
    {
        struct RestoreTitle
        {
            std::string previous = TitleLocation::Path();
            ~RestoreTitle() { TitleLocation::setPathProperty(previous); }
        } restore;
        TitleLocation::setPathProperty(fs::current_path().generic_string());
        constexpr const char* relative =
            "tests/assets/media/music/Artist One/Album Alpha/01 - Sunrise.ogg";
        std::unique_ptr<Song> song;
        ASSERT_NO_THROW(song.reset(Song::FromUri(
            "Sunrise", System::Uri(relative, System::UriKind::Relative))));
        ASSERT_NE(song, nullptr);
        EXPECT_EQ(song->getHandle(), (fs::current_path() / relative).generic_string());
    }
}
