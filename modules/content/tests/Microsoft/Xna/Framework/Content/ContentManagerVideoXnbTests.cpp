// SPDX-License-Identifier: MS-PL
//
// plans/plan_media.md MEDIA-73: end-to-end test -- content.Load<Video>("fixture") through the real,
// full ContentManager::Load<Video>() path (container header + type-reader table + object
// dispatch), against a real companion video file. Unlike ContentManagerSongXnbTests.cpp's
// fixture (a real MonoGame-produced .xnb, vendored as a binary), no MonoGame/dotnet content-
// pipeline tooling is available in this environment to produce a real Video .xnb the same way --
// documented as an honest gap in plans/plan_media.md/NEXTmedia.md. Instead, this hand-constructs a full,
// valid .xnb byte container (header + type-reader table + VideoReader's exact real field layout),
// the same technique ContentManagerXnbTests.cpp's own BuildTestXnbFile() and
// ContentManagerSongXnbTests.cpp's own broken-fixture builder already use elsewhere in this
// codebase. The container-parsing machinery itself doesn't care whether the bytes came from mgcb
// or were hand-assembled to match the same real binary format -- this still proves the full
// ContentManager::Load<Video>() path end-to-end, including that the resulting Video is genuinely
// playable via VideoPlayer, which VideoContentTypeReaderTests.cpp's reader-only tests do not cover.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

#include "CNA/Internal/Xnb/VideoContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Media/Video/Video.hpp"
#include "Microsoft/Xna/Framework/Media/Video/VideoPlayer.hpp"
#include "Microsoft/Xna/Framework/Media/VideoSoundtrackType.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"

using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Media::Video;
using Microsoft::Xna::Framework::Media::VideoPlayer;
using Microsoft::Xna::Framework::Media::VideoSoundtrackType;

namespace
{
    void WriteBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
    {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    // Builds a full, valid, minimal-header-plus-body .xnb byte sequence whose root object is a
    // VideoReader entry, matching FNA's real binary layout exactly (reference string + durationMS
    // + width + height + framesPerSecond + soundTrackType, all as VideoReader::Read() expects).
    std::vector<std::uint8_t> BuildVideoXnbFile(const std::string& reference, int32_t durationMs,
                                                 int32_t width, int32_t height, float fps,
                                                 VideoSoundtrackType soundtrackType)
    {
        System::IO::MemoryStream bodyMs;
        System::IO::BinaryWriter bodyWriter(&bodyMs, true);
        bodyWriter.Write7BitEncodedInt(1); // one type-reader table entry
        bodyWriter.Write(std::string("Microsoft.Xna.Framework.Content.VideoReader"));
        bodyWriter.Write((int32_t)0); // reader version
        bodyWriter.Write7BitEncodedInt(0); // zero shared resources
        bodyWriter.Write7BitEncodedInt(1); // root object -> table entry 1
        bodyWriter.Write(reference);
        bodyWriter.Write(durationMs);
        bodyWriter.Write(width);
        bodyWriter.Write(height);
        bodyWriter.Write(fps);
        bodyWriter.Write(static_cast<int32_t>(soundtrackType));
        bodyWriter.Flush();
        auto bodyBytes = bodyMs.ToArray();

        System::IO::MemoryStream fileMs;
        System::IO::BinaryWriter fileWriter(&fileMs, true);
        fileWriter.Write((uint8_t)'X'); fileWriter.Write((uint8_t)'N'); fileWriter.Write((uint8_t)'B');
        fileWriter.Write((uint8_t)'w'); // platform: Windows
        fileWriter.Write((uint8_t)5);   // version
        fileWriter.Write((uint8_t)0);   // flags: uncompressed
        fileWriter.Write((int32_t)(10 + (int32_t)bodyBytes.size())); // total length
        fileWriter.Write(bodyBytes.data(), 0, (int32_t)bodyBytes.size());
        fileWriter.Flush();

        auto fileBytes = fileMs.ToArray();
        return std::vector<std::uint8_t>(fileBytes.begin(), fileBytes.end());
    }

    class ContentManagerVideoXnbTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            CNA::Internal::Xnb::RegisterVideoXnbReader();
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }

        GraphicsDevice gd;
    };
}

TEST_F(ContentManagerVideoXnbTest, LoadRealFixtureEndToEndProducesAPlayableVideo)
{
    // Real, checked-in companion video file (tests/assets/media/video/chroma_420.mkv, 160x90 @
    // 25fps, 2000ms), matching Song's own "real companion file, hand-built .xnb container"
    // approach exactly.
    ContentManager cm(nullptr, "tests/assets/media/video");
    cm.setGraphicsDevice(gd);

    WriteBytes(std::filesystem::path("tests/assets/media/video") / "video_xnb_fixture.xnb",
               BuildVideoXnbFile("chroma_420.mkv", 2000, 160, 90, 25.0f, VideoSoundtrackType::Music));

    Video video = cm.Load<Video>("video_xnb_fixture");

    EXPECT_EQ(video.getWidthProperty(), 160);
    EXPECT_EQ(video.getHeightProperty(), 90);
    EXPECT_FLOAT_EQ(video.getFramesPerSecondProperty(), 25.0f);
    EXPECT_EQ(video.getVideoSoundtrackTypeProperty(), VideoSoundtrackType::Music);
    EXPECT_EQ(video.getDurationProperty(), System::TimeSpan::FromMilliseconds(2000));
    ASSERT_TRUE(std::filesystem::exists(video.getFileNameProperty()));

    // The real proof this task's Accept criterion asks for: the companion file resolves and is
    // genuinely playable via VideoPlayer, not just that the Video object's own fields round-trip.
    VideoPlayer player;
    player.Play(&video);
    auto* texture = player.GetTexture();
    ASSERT_NE(texture, nullptr);
    EXPECT_EQ(texture->getWidthProperty(), 160);
    EXPECT_EQ(texture->getHeightProperty(), 90);

    std::filesystem::remove("tests/assets/media/video/video_xnb_fixture.xnb");
}
