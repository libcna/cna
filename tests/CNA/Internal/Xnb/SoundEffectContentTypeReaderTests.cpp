// SPDX-License-Identifier: MS-PL
//
// plan_xnb.md XNB-33/XNB-33A: SoundEffectReader support-matrix survey + reader unit tests. Real,
// externally-produced fixtures (MonoGame's own Tests/Assets/Audio/tone_*.xnb, vendored at
// tests/assets/xnb/monogame/windows/uncompressed/audio/) cover every WaveFormatEx variant this
// matrix cares about -- never hand-crafted, since a hand-authored PCM buffer would trivially
// "pass" without proving the real WAVEFORMATEX byte layout was parsed correctly.
//
// Support matrix (plan_xnb.md XNB-33):
//   | Format                          | Status                                            |
//   |---------------------------------|----------------------------------------------------|
//   | PCM 16-bit (mono or stereo)      | Supported (maps directly onto SoundEffect's own    |
//   |                                  | SDL_AUDIO_S16LE-only PCM constructors)              |
//   | PCM 8-bit                        | Rejected -- CNA's SoundEffect has no 8-bit path     |
//   | IEEE float 32-bit                | Rejected -- CNA's SoundEffect has no float path     |
//   | MS-ADPCM                         | Rejected -- CNA's SoundEffect has no ADPCM decoder  |
//   | IMA-ADPCM                        | Rejected -- CNA's SoundEffect has no ADPCM decoder  |
//   | XMA2 (compressed, Xbox-oriented) | Rejected -- CNA's SoundEffect has no XMA2 decoder   |
// All rejections throw a documented ContentLoadException rather than silently constructing a
// SoundEffect that would play back as noise.

#include <fstream>
#include <gtest/gtest.h>
#include <sstream>

#include "CNA/Internal/Xnb/SoundEffectContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "System/IO/MemoryStream.hpp"

using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentReader;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;

namespace
{
    std::string ReadWholeFile(const std::string& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return {};
        std::ostringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }

    class SoundEffectContentTypeReaderTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            CNA::Internal::Xnb::RegisterSoundEffectXnbReader();
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }

        // Reads the fixture's uncompressed body (past the 10-byte header, no size field since
        // none of these fixtures are LZX-compressed) directly into a ContentReader positioned
        // right after where InitializeTypeReaders() would have consumed the type-reader table,
        // mirroring how ContentManager::LoadXnbAsset<T>() itself drives ContentReader::ReadAsset<T>().
        Microsoft::Xna::Framework::Audio::SoundEffect LoadFixture(const std::string& path)
        {
            const std::string bytes = ReadWholeFile(path);
            EXPECT_FALSE(bytes.empty()) << "Real .xnb fixture not found relative to CWD: " << path;

            body_ = std::make_unique<System::IO::MemoryStream>(
                reinterpret_cast<const uint8_t*>(bytes.data()) + 10, static_cast<int32_t>(bytes.size()) - 10);
            reader_ = std::make_unique<ContentReader>(&cm_, body_.get(), "test", 5, 'w');
            return reader_->ReadAsset<Microsoft::Xna::Framework::Audio::SoundEffect>();
        }

        std::string ownedBytes_;
        std::unique_ptr<System::IO::MemoryStream> body_;
        std::unique_ptr<ContentReader> reader_;
        ContentManager cm_;
    };

    constexpr const char* kAudioDir = "tests/assets/xnb/monogame/windows/uncompressed/audio/";
}

TEST_F(SoundEffectContentTypeReaderTest, IsRegisteredUnderRealFnaCanonicalName)
{
    EXPECT_TRUE(ContentTypeReaderManager::IsRegistered("Microsoft.Xna.Framework.Content.SoundEffectReader"));
}

TEST_F(SoundEffectContentTypeReaderTest, Pcm16BitMonoLoadsSuccessfully)
{
    auto effect = LoadFixture(std::string(kAudioDir) + "tone_mono_44khz_16bit.xnb");
    EXPECT_EQ(effect.getNameProperty(), "test");
    EXPECT_GT(effect.getDurationProperty().getTicksProperty(), 0);
}

TEST_F(SoundEffectContentTypeReaderTest, Pcm16BitStereoLoadsSuccessfully)
{
    auto effect = LoadFixture(std::string(kAudioDir) + "tone_stereo_44khz_16bit.xnb");
    EXPECT_EQ(effect.getNameProperty(), "test");
    EXPECT_GT(effect.getDurationProperty().getTicksProperty(), 0);
}

TEST_F(SoundEffectContentTypeReaderTest, Pcm8BitIsRejected)
{
    EXPECT_THROW(LoadFixture(std::string(kAudioDir) + "tone_mono_44khz_8bit.xnb"), ContentLoadException);
}

TEST_F(SoundEffectContentTypeReaderTest, IeeeFloatIsRejected)
{
    EXPECT_THROW(LoadFixture(std::string(kAudioDir) + "tone_mono_44khz_float.xnb"), ContentLoadException);
}

TEST_F(SoundEffectContentTypeReaderTest, MsAdpcmIsRejected)
{
    EXPECT_THROW(LoadFixture(std::string(kAudioDir) + "tone_mono_44khz_msadpcm.xnb"), ContentLoadException);
}

TEST_F(SoundEffectContentTypeReaderTest, ImaAdpcmIsRejected)
{
    EXPECT_THROW(LoadFixture(std::string(kAudioDir) + "tone_mono_44khz_imaadpcm.xnb"), ContentLoadException);
}
