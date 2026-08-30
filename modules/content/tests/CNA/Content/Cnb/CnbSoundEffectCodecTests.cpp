// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnb.md CNBF-103A: the SoundEffect schema.
//
// The interesting failures in an audio asset are the ones that surface at PLAYBACK time on
// someone else's machine rather than at load: a loop region reaching past the end of the sound, a
// frame count disagreeing with the payload length. Those are what the schema checks, and what
// these tests exercise.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbSoundEffectCodec.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"

using CNA::Content::Cnb::CnbAudioFormat;
using CNA::Content::Cnb::CnbDocument;
using CNA::Content::Cnb::CnbSoundEffectData;
using CNA::Content::Cnb::DecodeSoundEffectFromCnb;
using CNA::Content::Cnb::EncodeSoundEffectToCnb;
using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;

namespace
{
    /// 100 stereo frames of Pcm16, each frame carrying its own index so a truncation or an
    /// off-by-one in the frame arithmetic is visible in the decoded bytes.
    CnbSoundEffectData MakeSound(std::uint32_t frames = 100u, std::uint32_t channels = 2u)
    {
        CnbSoundEffectData data;
        data.format = CnbAudioFormat::Pcm16;
        data.sampleRate = 44100u;
        data.channels = channels;
        data.frameCount = frames;
        data.samples.resize(static_cast<std::size_t>(frames) * channels * 2u);
        for (std::size_t i = 0; i < data.samples.size(); ++i)
        {
            data.samples[i] = static_cast<std::uint8_t>(i & 0xFFu);
        }
        return data;
    }

    class ScratchRoot
    {
    public:
        ScratchRoot()
            : path_(std::filesystem::temp_directory_path() /
                    ("cna_cnb_audio_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(path_);
        }
        ~ScratchRoot() { std::error_code ignored; std::filesystem::remove_all(path_, ignored); }
        ScratchRoot(const ScratchRoot&) = delete;
        ScratchRoot& operator=(const ScratchRoot&) = delete;
        [[nodiscard]] const std::filesystem::path& path() const { return path_; }

    private:
        std::filesystem::path path_;
    };

    void WriteBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }
}

TEST(CnbSoundEffectCodecTest, APcm16SoundRoundTrips)
{
    const CnbSoundEffectData source = MakeSound();
    const std::vector<std::uint8_t> bytes = EncodeSoundEffectToCnb(source, "Sfx/beep");
    const CnbDocument document = CnbDocument::Parse(bytes, "beep.cnb");
    EXPECT_EQ(document.AssetTypeId(), CNA::Content::Cnb::CnbAssetTypeId::SoundEffect);
    EXPECT_EQ(document.Metadata().assetTypeName, "Microsoft.Xna.Framework.Audio.SoundEffect");

    const CnbSoundEffectData decoded = DecodeSoundEffectFromCnb(document);
    EXPECT_EQ(decoded.format, CnbAudioFormat::Pcm16);
    EXPECT_EQ(decoded.sampleRate, 44100u);
    EXPECT_EQ(decoded.channels, 2u);
    EXPECT_EQ(decoded.frameCount, 100u);
    EXPECT_EQ(decoded.loopStart, 0u);
    EXPECT_EQ(decoded.loopLength, 0u);
    EXPECT_EQ(decoded.samples, source.samples);
}

TEST(CnbSoundEffectCodecTest, ALoopingMonoSoundRoundTripsItsLoopPoints)
{
    CnbSoundEffectData source = MakeSound(50u, 1u);
    source.sampleRate = 22050u;
    source.loopStart = 10u;
    source.loopLength = 30u;
    const std::vector<std::uint8_t> bytes = EncodeSoundEffectToCnb(source);
    const CnbSoundEffectData decoded =
        DecodeSoundEffectFromCnb(CnbDocument::Parse(bytes, "loop.cnb"));
    EXPECT_EQ(decoded.channels, 1u);
    EXPECT_EQ(decoded.sampleRate, 22050u);
    EXPECT_EQ(decoded.loopStart, 10u);
    EXPECT_EQ(decoded.loopLength, 30u);
}

TEST(CnbSoundEffectCodecTest, ALoopReachingPastTheEndIsRefused)
{
    // This is the check that earns its keep: an over-long loop is not a malformed file in any
    // structural sense -- every length and checksum is correct -- and without this rule it becomes
    // an out-of-range read inside the mixer, at playback time.
    CnbSoundEffectData tooLong = MakeSound(10u, 1u);
    tooLong.loopStart = 5u;
    tooLong.loopLength = 10u;
    try
    {
        (void)EncodeSoundEffectToCnb(tooLong);
        FAIL() << "a loop reaching past the end must be refused";
    }
    catch (const ContentLoadException& e)
    {
        EXPECT_NE(std::string(e.what()).find("frames long"), std::string::npos) << e.what();
    }

    CnbSoundEffectData startPastEnd = MakeSound(10u, 1u);
    startPastEnd.loopStart = 11u;
    startPastEnd.loopLength = 0u;
    EXPECT_THROW((void)EncodeSoundEffectToCnb(startPastEnd), ContentLoadException);
}

TEST(CnbSoundEffectCodecTest, TheFrameCountMustAgreeWithThePayloadLength)
{
    CnbSoundEffectData shortPayload = MakeSound(10u, 2u);
    shortPayload.samples.pop_back();
    EXPECT_THROW((void)EncodeSoundEffectToCnb(shortPayload), ContentLoadException);

    CnbSoundEffectData longPayload = MakeSound(10u, 2u);
    longPayload.samples.push_back(0u);
    EXPECT_THROW((void)EncodeSoundEffectToCnb(longPayload), ContentLoadException);
}

TEST(CnbSoundEffectCodecTest, ImpossibleRatesChannelsAndFormatsAreRefused)
{
    CnbSoundEffectData zeroRate = MakeSound();
    zeroRate.sampleRate = 0u;
    EXPECT_THROW((void)EncodeSoundEffectToCnb(zeroRate), ContentLoadException);

    CnbSoundEffectData absurdRate = MakeSound();
    absurdRate.sampleRate = CNA::Content::Cnb::CnbMaxAudioSampleRate + 1u;
    EXPECT_THROW((void)EncodeSoundEffectToCnb(absurdRate), ContentLoadException);

    // XNA's AudioChannels is Mono or Stereo; there is no third value to mean anything else.
    CnbSoundEffectData surround = MakeSound(10u, 1u);
    surround.channels = 6u;
    EXPECT_THROW((void)EncodeSoundEffectToCnb(surround), ContentLoadException);

    // A reserved identifier must be refused loudly rather than written as a file no reader in
    // this build can turn into a sound.
    CnbSoundEffectData vorbis = MakeSound();
    vorbis.format = CnbAudioFormat::Vorbis;
    EXPECT_THROW((void)EncodeSoundEffectToCnb(vorbis), ContentLoadException);
}

TEST(CnbSoundEffectCodecTest, ASoundEffectCnbLoadsThroughContentManager)
{
    // The point of this test is the RESOLUTION, not the decode: SoundEffect has its own Load<>
    // specialisation which did not consult the .cnb tier at all, so this file would previously
    // have fallen through to the loose-file reader and failed.
    ScratchRoot root;
    CnbSoundEffectData source = MakeSound(64u, 1u);
    source.sampleRate = 8000u;
    WriteBytes(root.path() / "beep.cnb", EncodeSoundEffectToCnb(source, "beep"));

    ContentManager cm(nullptr, root.path().string());
    auto sound = cm.Load<Microsoft::Xna::Framework::Audio::SoundEffect>("beep");
    // 64 frames at 8 kHz is 8 ms.
    EXPECT_NEAR(sound.getDurationProperty().getTotalMillisecondsProperty(), 8.0, 0.5);
    EXPECT_EQ(sound.getNameProperty(), "beep");
}

TEST(CnbSoundEffectCodecTest, ASoundEffectCnbAlsoResolvesWhenNamedWithItsExtension)
{
    ScratchRoot root;
    WriteBytes(root.path() / "beep.cnb", EncodeSoundEffectToCnb(MakeSound(32u, 1u), "beep"));
    ContentManager cm(nullptr, root.path().string());
    EXPECT_NO_THROW((void)cm.Load<Microsoft::Xna::Framework::Audio::SoundEffect>("beep.cnb"));
}
