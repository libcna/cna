// SPDX-License-Identifier: MS-PL
// ISSUE-101: cross-module public TitleLocation + Video URI contract.
#include <gtest/gtest.h>
#include <filesystem>
#include <memory>
#include <string>
#include <typeinfo>
#include "CNA/Internal/PathUtf8.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Media/Video/Video.hpp"
#include "Microsoft/Xna/Framework/Media/Video/VideoPlayer.hpp"
#include "Microsoft/Xna/Framework/TitleLocation.hpp"
#include "System/IO/FileNotFoundException.hpp"
#include "System/ArgumentException.hpp"
#include "System/InvalidOperationException.hpp"

namespace
{
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::TitleLocation;
    using namespace Microsoft::Xna::Framework::Media;

    template<class Exception, class Action>
    bool ThrowsExactly(Action action)
    {
        try { action(); }
        catch (const Exception& error) { return typeid(error) == typeid(Exception); }
        catch (...) { return false; }
        return false;
    }

    class VideoUriContractTest : public testing::Test
    {
    protected:
        const std::string relative = "tests/assets/media/video/chroma_420.mkv";
        std::filesystem::path originalCwd;
        std::filesystem::path fixture;
        std::string originalTitle;

        void SetUp() override
        {
            originalCwd = std::filesystem::current_path();
            originalTitle = TitleLocation::Path();
            fixture = originalCwd / relative;
            ASSERT_TRUE(std::filesystem::is_regular_file(fixture));
            TitleLocation::setPathProperty(CNA::Internal::PathToGenericUtf8(originalCwd));
        }

        void TearDown() override
        {
            std::error_code error;
            std::filesystem::current_path(originalCwd, error);
            EXPECT_FALSE(error);
            TitleLocation::setPathProperty(originalTitle);
        }

        std::string FileUri() const
        {
            const auto path = CNA::Internal::PathToGenericUtf8(fixture);
            return (path.starts_with('/') ? "file://" : "file:///") + path;
        }

        std::string MissingPath() const
        {
            return CNA::Internal::PathToGenericUtf8(fixture.parent_path() / "repair_video_uri_missing.mkv");
        }
    };

    TEST_F(VideoUriContractTest, PlainAbsolutePathRemainsSupported)
    {
        GraphicsDevice device;
        std::unique_ptr<Video> video(Video::FromUriEXT(CNA::Internal::PathToGenericUtf8(fixture), &device));
        ASSERT_NE(video, nullptr);
        EXPECT_EQ(video->getWidthProperty(), 160);
        EXPECT_EQ(video->getHeightProperty(), 90);
    }

    TEST_F(VideoUriContractTest, AbsoluteFileUriResolvesToTheLocalFile)
    {
        GraphicsDevice device;
        std::unique_ptr<Video> video(Video::FromUriEXT(FileUri(), &device));
        ASSERT_NE(video, nullptr);
        EXPECT_EQ(video->getWidthProperty(), 160);
        EXPECT_EQ(video->getHeightProperty(), 90);
    }

    TEST_F(VideoUriContractTest, FileUriDecodesAnEscapedFilenameCharacter)
    {
        auto uri = FileUri();
        const auto filename = uri.rfind("chroma_420.mkv");
        ASSERT_NE(filename, std::string::npos);
        uri.replace(filename, 1, "%63");
        GraphicsDevice device;
        std::unique_ptr<Video> video(Video::FromUriEXT(uri, &device));
        ASSERT_NE(video, nullptr);
        EXPECT_EQ(video->getWidthProperty(), 160);
    }

    TEST_F(VideoUriContractTest, RelativeUriUsesTitleLocationInsteadOfCurrentDirectory)
    {
        std::filesystem::current_path(originalCwd / "tests");
        ASSERT_FALSE(std::filesystem::exists(relative));
        GraphicsDevice device;
        std::unique_ptr<Video> video(Video::FromUriEXT(relative, &device));
        ASSERT_NE(video, nullptr);
        EXPECT_EQ(video->getWidthProperty(), 160);
    }

    TEST_F(VideoUriContractTest, NonFileAbsoluteUriHasTheExactRefusalType)
    {
        GraphicsDevice device;
        EXPECT_TRUE(ThrowsExactly<System::InvalidOperationException>([&] {
            std::unique_ptr<Video> video(
                Video::FromUriEXT("https://example.invalid/movie.ogv", &device));
        }));
    }

    TEST_F(VideoUriContractTest, RawConstructorStillRejectsAMissingFile)
    {
        ASSERT_FALSE(std::filesystem::exists(MissingPath()));
        GraphicsDevice device;
        EXPECT_TRUE(ThrowsExactly<System::IO::FileNotFoundException>([&] {
            Video video(MissingPath(), &device);
        }));
    }

    TEST_F(VideoUriContractTest, TrustedMetadataConstructionDoesNotReadTheFile)
    {
        ASSERT_FALSE(std::filesystem::exists(MissingPath()));
        GraphicsDevice device;
        Video video(MissingPath(), &device, 2000, 160, 90, 25.0f,
                    VideoSoundtrackType::Dialog);
        EXPECT_EQ(video.getWidthProperty(), 160);
        EXPECT_EQ(video.getHeightProperty(), 90);
        EXPECT_FLOAT_EQ(video.getFramesPerSecondProperty(), 25.0f);
        EXPECT_EQ(video.getDurationProperty(), System::TimeSpan::FromMilliseconds(2000));
        EXPECT_EQ(video.getVideoSoundtrackTypeProperty(), VideoSoundtrackType::Dialog);
    }

    TEST_F(VideoUriContractTest, DeferredMissingFileIsReportedAndPlayerRemainsClosed)
    {
        ASSERT_FALSE(std::filesystem::exists(MissingPath()));
        GraphicsDevice device;
        Video video(MissingPath(), &device, 2000, 160, 90, 25.0f,
                    VideoSoundtrackType::Dialog);
        VideoPlayer player;
        EXPECT_TRUE(ThrowsExactly<System::ArgumentException>([&] {
            player.Play(&video);
        }));
        EXPECT_EQ(player.getStateProperty(), MediaState::Stopped);
        EXPECT_EQ(player.getVideoProperty(), nullptr);
    }

    TEST_F(VideoUriContractTest, MetadataMismatchStillClosesAtomically)
    {
        GraphicsDevice device;
        Video video(CNA::Internal::PathToGenericUtf8(fixture), &device, 2000, 159, 90, 25.0f,
                    VideoSoundtrackType::MusicAndDialog);
        VideoPlayer player;
        EXPECT_TRUE(ThrowsExactly<System::InvalidOperationException>([&] {
            player.Play(&video);
        }));
        EXPECT_EQ(player.getStateProperty(), MediaState::Stopped);
        EXPECT_EQ(player.getVideoProperty(), nullptr);
    }
}
