// SPDX-License-Identifier: MS-PL
// ISSUE-101: genuine XNA pre-decoder file guard and exact HRESULT translation.
#include <gtest/gtest.h>
#include <filesystem>
#include <string>
#include <typeinfo>
#include "CNA/Internal/PathUtf8.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Media/Video/Video.hpp"
#include "Microsoft/Xna/Framework/Media/Video/VideoPlayer.hpp"
#include "System/ArgumentException.hpp"
#include "System/IO/File.hpp"

namespace
{
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using namespace Microsoft::Xna::Framework::Media;

class VideoMissingFileXnaTest : public testing::TestWithParam<int> {};

TEST_P(VideoMissingFileXnaTest, RefusesNonFileWithExactArgumentException)
{
    const std::string valid = "tests/assets/media/video/chroma_420.mkv";
    const std::string missing = "tests/assets/media/video/repair_xna_missing.mkv";
    const std::string paths[] = {
        "", missing, CNA::Internal::PathToGenericUtf8(std::filesystem::absolute(missing)),
        "tests/assets/media/video", valid + std::string(1, '\0') + "suffix"
    };
    ASSERT_TRUE(System::IO::File::Exists(valid));
    ASSERT_FALSE(System::IO::File::Exists(paths[GetParam()]));
    GraphicsDevice device;
    Video video(paths[GetParam()], &device, 1000, 160, 90, 25.0f,
                VideoSoundtrackType::Music);
    VideoPlayer player;
    bool caught = false;
    try { player.Play(&video); }
    catch (const System::ArgumentException& error)
    {
        caught = true;
        EXPECT_EQ(typeid(error), typeid(System::ArgumentException));
        // Sharp Runtime represents .NET's absent ParamName by an empty string.
        EXPECT_TRUE(error.getParamNameProperty().empty());
    }
    catch (...) { ADD_FAILURE() << "Wrong exception type for XNA's E_INVALIDARG guard"; }
    EXPECT_TRUE(caught);
    EXPECT_EQ(player.getStateProperty(), MediaState::Stopped);
    EXPECT_EQ(player.getVideoProperty(), nullptr);
}

INSTANTIATE_TEST_SUITE_P(NonFiles, VideoMissingFileXnaTest, testing::Range(0, 5));

TEST(VideoMissingFileXnaControlTest, ExistingDecodableFileStillPlays)
{
    GraphicsDevice device;
    Video video("tests/assets/media/video/chroma_420.mkv", &device);
    VideoPlayer player;
    ASSERT_NO_THROW(player.Play(&video));
    EXPECT_EQ(player.getStateProperty(), MediaState::Playing);
    EXPECT_EQ(player.getVideoProperty(), &video);
    EXPECT_NE(player.GetTexture(), nullptr);
}
} // namespace
