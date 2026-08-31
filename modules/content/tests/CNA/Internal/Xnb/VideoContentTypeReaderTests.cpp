// SPDX-License-Identifier: MS-PL
//
// plans/plan_media.md MEDIA-70/71/73: unit tests for VideoReader's own behavior (registration,
// extension-probing fallback, field layout), mirroring SongContentTypeReaderTests.cpp's own
// structure. Unlike Song, no full ContentManager::Load<Video>() round-trip against a real,
// externally-produced .xnb exists here -- that needs a real MonoGame-built Video .xnb fixture,
// which requires MonoGame/dotnet content-pipeline tooling not available in this environment (no
// dotnet/mgcb installed). Documented as a real, honest gap (see NEXTmedia.md), not silently
// skipped -- these reader-level tests still exercise VideoReader::Read()'s real logic (field
// parsing, path normalization/fallback, GraphicsDevice wiring) via a hand-constructed in-memory
// buffer matching FNA's exact real binary layout, the same technique
// SongContentTypeReaderTests.cpp itself already uses for its own (non-full-round-trip) tests.

#include <filesystem>
#include <gtest/gtest.h>

#include "CNA/Internal/Xnb/VideoContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Media/VideoSoundtrackType.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"

using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentReader;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Media::Video;
using Microsoft::Xna::Framework::Media::VideoSoundtrackType;

namespace
{
    class VideoContentTypeReaderTest : public ::testing::Test
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

    Video ReadVideoFields(ContentManager& cm, const std::string& assetName, const std::string& reference,
                           int32_t durationMs, int32_t width, int32_t height, float fps,
                           VideoSoundtrackType soundtrackType)
    {
        System::IO::MemoryStream ms;
        System::IO::BinaryWriter writer(&ms, true);
        writer.Write(reference);
        writer.Write(durationMs);
        writer.Write(width);
        writer.Write(height);
        writer.Write(fps);
        writer.Write(static_cast<int32_t>(soundtrackType));
        writer.Flush();
        const auto buf = ms.ToArray();

        System::IO::MemoryStream body(buf.data(), static_cast<int32_t>(buf.size()));
        ContentReader reader(&cm, &body, assetName, 5, 'w');

        auto typeReader = ContentTypeReaderManager::CreateReader("Microsoft.Xna.Framework.Content.VideoReader");
        std::any resultAny = typeReader->ReadUntyped(reader, std::any{});
        return std::any_cast<Video>(resultAny);
    }
}

TEST_F(VideoContentTypeReaderTest, IsRegisteredUnderRealFnaCanonicalName)
{
    EXPECT_TRUE(ContentTypeReaderManager::IsRegistered("Microsoft.Xna.Framework.Content.VideoReader"));
}

// plans/plan_media.md MEDIA-70: the real fixture's own extension (.mkv) happens to be exactly 4
// characters, matching the same coincidental-length scenario
// SongContentTypeReaderTests.cpp's "ReferenceEndingInFourCharacterRealExtensionResolvesToRealFile"
// already relies on for .ogg -- the reader strips the last 4 characters, fails to find a
// .ogv/.ogg variant, and correctly falls back to the un-stripped original path.
TEST_F(VideoContentTypeReaderTest, ReferenceResolvesToRealFileViaFallback)
{
    ContentManager cm(nullptr, "tests/assets/media/video");
    cm.setGraphicsDevice(gd);

    Video video = ReadVideoFields(cm, "chroma_420", "chroma_420.mkv", 2000, 160, 90, 25.0f,
                                   VideoSoundtrackType::Music);

    EXPECT_EQ(video.getFileNameProperty(), "tests/assets/media/video/chroma_420.mkv");
    EXPECT_TRUE(std::filesystem::exists(video.getFileNameProperty()));
}

TEST_F(VideoContentTypeReaderTest, WindowsStyleNestedAssetNameResolvesMediaBesideXnb)
{
    ContentManager cm(nullptr, "tests/assets/media");
    cm.setGraphicsDevice(gd);

    Video video = ReadVideoFields(
        cm, R"(video\chroma_420)", "chroma_420.mkv", 2000, 160, 90, 25.0f,
        VideoSoundtrackType::Music);

    EXPECT_EQ(video.getFileNameProperty(), "tests/assets/media/video/chroma_420.mkv");
    EXPECT_TRUE(std::filesystem::exists(video.getFileNameProperty()));
}

// plans/plan_media.md MEDIA-70/71: all 5 numeric fields round-trip correctly using direct typed reads
// (ReadInt32/ReadSingle), matching SongContentTypeReader's established code style.
TEST_F(VideoContentTypeReaderTest, AllFieldsRoundTripCorrectly)
{
    ContentManager cm(nullptr, "tests/assets/media/video");
    cm.setGraphicsDevice(gd);

    Video video = ReadVideoFields(cm, "chroma_420", "chroma_420.mkv", 2000, 160, 90, 25.0f,
                                   VideoSoundtrackType::MusicAndDialog);

    EXPECT_EQ(video.getDurationProperty(), System::TimeSpan::FromMilliseconds(2000));
    EXPECT_EQ(video.getWidthProperty(), 160);
    EXPECT_EQ(video.getHeightProperty(), 90);
    EXPECT_FLOAT_EQ(video.getFramesPerSecondProperty(), 25.0f);
    EXPECT_EQ(video.getVideoSoundtrackTypeProperty(), VideoSoundtrackType::MusicAndDialog);
}

// The XNB-sourced constructor trusts the caller's metadata and does not probe the file at
// construction time -- confirms VideoReader::Read() doesn't accidentally validate against the
// decoder (that check only happens later, in VideoPlayer::Play(), per plans/plan_media.md MEDIA-42).
TEST_F(VideoContentTypeReaderTest, DoesNotThrowOnDeliberatelyMismatchedDeclaredMetadata)
{
    ContentManager cm(nullptr, "tests/assets/media/video");
    cm.setGraphicsDevice(gd);

    EXPECT_NO_THROW({
        ReadVideoFields(cm, "chroma_420", "chroma_420.mkv", 2000, 9999, 9999, 999.0f,
                         VideoSoundtrackType::Dialog);
    });
}

TEST_F(VideoContentTypeReaderTest, ThrowsContentLoadExceptionWithoutAGraphicsDevice)
{
    ContentManager cm(nullptr, "tests/assets/media/video");
    // Deliberately no setGraphicsDevice() call.
    EXPECT_THROW(
        ReadVideoFields(cm, "chroma_420", "chroma_420.mkv", 2000, 160, 90, 25.0f,
                         VideoSoundtrackType::Music),
        Microsoft::Xna::Framework::Content::ContentLoadException);
}
