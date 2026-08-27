// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnb.md CNBF-109/110/111/112: the CNB producer paths.
//
// A codec round trip proves a codec talks to itself. These tests prove the PRODUCERS are useful:
// a real PNG becomes a Texture2D someone can load and get the same pixels from, a real WAV becomes
// a playable SoundEffect, a SpriteFont .cnj plus its atlas becomes ONE self-contained file, and
// every one of those happens with no GraphicsDevice and no audio device anywhere in the compiler.

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbSoundEffectCodec.hpp"
#include "CNA/Content/Cnb/CnbSourceImport.hpp"
#include "CNA/Content/Cnb/CnbSpriteFontCodec.hpp"
#include "CNA/Content/Cnb/CnbTextureCodec.hpp"
#include "CNA/Internal/Graphics/ImageLoader.hpp"
#include "CNA/Content/Cnb/CnjToCnb.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

using CNA::Content::Cnb::CnbDocument;
using CNA::Content::Cnb::CompileCnjToCnb;
using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

namespace
{
    class Scratch
    {
    public:
        explicit Scratch(const std::string& tag)
            : path_(std::filesystem::temp_directory_path() /
                    ("cna_cnb_prod_" + tag + "_" +
                     std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(path_);
        }
        ~Scratch() { std::error_code e; std::filesystem::remove_all(path_, e); }
        Scratch(const Scratch&) = delete;
        Scratch& operator=(const Scratch&) = delete;
        [[nodiscard]] const std::filesystem::path& path() const { return path_; }
    private:
        std::filesystem::path path_;
    };

    void WriteText(const std::filesystem::path& p, const std::string& t)
    {
        std::ofstream o(p); o << t;
    }
    void WriteBytes(const std::filesystem::path& p, const std::vector<std::uint8_t>& b)
    {
        std::ofstream o(p, std::ios::binary);
        o.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
    }

    /// RGBA8 pixels where every texel differs, so a transposed, shifted or truncated decode is
    /// visible rather than merely "some pixels came back". Deliberately not one of the
    /// repository's image fixtures: those are synthetic placeholders and two of them are a single
    /// colour, which would make several of these assertions vacuous.
    std::vector<std::uint8_t> DistinctPixels(int width, int height)
    {
        std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * height * 4u);
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const std::size_t i = (static_cast<std::size_t>(y) * width + x) * 4u;
                rgba[i] = static_cast<std::uint8_t>(x * 8 + 1);
                rgba[i + 1u] = static_cast<std::uint8_t>(y * 8 + 2);
                rgba[i + 2u] = static_cast<std::uint8_t>((x + y) * 4 + 3);
                rgba[i + 3u] = 255u;
            }
        }
        return rgba;
    }

    /// A genuine PNG, encoded by CNA's own image path so the test does not carry a second PNG
    /// implementation of its own.
    std::vector<std::uint8_t> MakePng(const std::vector<std::uint8_t>& rgba, int w, int h)
    {
        return CNA::Internal::Graphics::ImageLoader::EncodePng(rgba.data(), w, h, w, h);
    }

    /// A genuine RIFF/WAVE file with the given PCM payload.
    std::vector<std::uint8_t> MakeWav(std::uint16_t channels, std::uint32_t sampleRate,
                                       std::uint16_t bitsPerSample,
                                       const std::vector<std::uint8_t>& payload,
                                       bool withLoop = false,
                                       std::uint32_t loopStart = 0, std::uint32_t loopEnd = 0)
    {
        std::vector<std::uint8_t> out;
        const auto u32 = [&](std::uint32_t v)
        { out.push_back(v & 0xFF); out.push_back((v >> 8) & 0xFF);
          out.push_back((v >> 16) & 0xFF); out.push_back((v >> 24) & 0xFF); };
        const auto u16 = [&](std::uint16_t v)
        { out.push_back(v & 0xFF); out.push_back((v >> 8) & 0xFF); };
        const auto tag = [&](const char* s) { for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(s[i])); };

        const std::uint16_t blockAlign = static_cast<std::uint16_t>(channels * (bitsPerSample / 8));
        std::vector<std::uint8_t> smpl;
        if (withLoop)
        {
            smpl.resize(36u + 24u, 0u);
            const auto put = [&](std::size_t at, std::uint32_t v)
            { smpl[at] = v & 0xFF; smpl[at+1] = (v>>8)&0xFF; smpl[at+2] = (v>>16)&0xFF; smpl[at+3] = (v>>24)&0xFF; };
            put(28, 1u);              // numSampleLoops
            put(36 + 8, loopStart);   // loop start
            put(36 + 12, loopEnd);    // loop end
        }

        const std::uint32_t riffSize =
            4u + (8u + 16u) + (8u + static_cast<std::uint32_t>(payload.size())) +
            (smpl.empty() ? 0u : 8u + static_cast<std::uint32_t>(smpl.size()));
        tag("RIFF"); u32(riffSize); tag("WAVE");
        tag("fmt "); u32(16u);
        u16(1u); u16(channels); u32(sampleRate);
        u32(sampleRate * blockAlign); u16(blockAlign); u16(bitsPerSample);
        tag("data"); u32(static_cast<std::uint32_t>(payload.size()));
        out.insert(out.end(), payload.begin(), payload.end());
        if (!smpl.empty())
        {
            tag("smpl"); u32(static_cast<std::uint32_t>(smpl.size()));
            out.insert(out.end(), smpl.begin(), smpl.end());
        }
        return out;
    }

    std::vector<std::uint8_t> Pcm16(std::size_t frames, std::uint16_t channels)
    {
        std::vector<std::uint8_t> out(frames * channels * 2u);
        for (std::size_t i = 0; i < frames * channels; ++i)
        {
            const auto v = static_cast<std::int16_t>((i * 37) % 30000 - 15000);
            out[i * 2u] = static_cast<std::uint8_t>(static_cast<std::uint16_t>(v) & 0xFF);
            out[i * 2u + 1u] = static_cast<std::uint8_t>(static_cast<std::uint16_t>(v) >> 8);
        }
        return out;
    }
}

// --------------------------------------------------------------------------------------------
// Direct image -> Texture2D (CNBF-109)
// --------------------------------------------------------------------------------------------

TEST(CnbProducerTest, APngCompilesToATexture2DWhosePixelsSurviveExactly)
{
    Scratch root("png");
    const std::vector<std::uint8_t> rgba = DistinctPixels(7, 5);
    WriteBytes(root.path() / "art.png", MakePng(rgba, 7, 5));

    const CNA::Content::Cnb::CnbTextureData texture =
        CNA::Content::Cnb::ImportImageAsCnbTexture2D((root.path() / "art.png").string());
    EXPECT_EQ(texture.width, 7u);
    EXPECT_EQ(texture.height, 5u);
    EXPECT_EQ(texture.mipCount, 1u);
    ASSERT_EQ(texture.representations.size(), 1u);
    EXPECT_EQ(texture.representations[0].format, CNA::Content::Cnb::CnbTextureFormat::Rgba8);
    // Exactly the decoded pixels: a compiler that resamples, premultiplies or reorders silently
    // is a compiler nobody can trust with their art.
    EXPECT_EQ(texture.representations[0].levels[0], rgba);

    WriteBytes(root.path() / "art.cnb",
               CNA::Content::Cnb::EncodeTexture2DToCnb(texture, "art"));

    GraphicsDevice device;
    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(device);
    auto loaded = cm.Load<Microsoft::Xna::Framework::Graphics::Texture2D>("art");
    EXPECT_EQ(loaded.getWidthProperty(), 7);
    EXPECT_EQ(loaded.getHeightProperty(), 5);

    std::vector<Microsoft::Xna::Framework::Color> back(35);
    loaded.GetData(back.data(), 35);
    for (std::size_t i = 0; i < back.size(); ++i)
    {
        EXPECT_EQ(back[i].getRProperty(), rgba[i * 4u + 0u]) << "texel " << i;
        EXPECT_EQ(back[i].getGProperty(), rgba[i * 4u + 1u]) << "texel " << i;
        EXPECT_EQ(back[i].getBProperty(), rgba[i * 4u + 2u]) << "texel " << i;
        EXPECT_EQ(back[i].getAProperty(), rgba[i * 4u + 3u]) << "texel " << i;
    }
}

TEST(CnbProducerTest, AColourKeyIsAppliedOnlyWhenAskedForAndKeepsTheColour)
{
    Scratch root("key");
    std::vector<std::uint8_t> rgba = DistinctPixels(4, 4);
    rgba[0] = 200u; rgba[1] = 100u; rgba[2] = 50u; rgba[3] = 255u; // the pixel to key
    WriteBytes(root.path() / "k.png", MakePng(rgba, 4, 4));

    // Not asked for: nothing changes. Silently rewriting pixels would be the worst possible
    // default for a content compiler.
    const auto plain =
        CNA::Content::Cnb::ImportImageAsCnbTexture2D((root.path() / "k.png").string());
    EXPECT_EQ(plain.representations[0].levels[0][3], 255u);

    CNA::Content::Cnb::CnbImageImportOptions options;
    options.colorKey = std::array<std::uint8_t, 3>{200u, 100u, 50u};
    const auto keyed =
        CNA::Content::Cnb::ImportImageAsCnbTexture2D((root.path() / "k.png").string(), options);
    // The RGB is KEPT and only the alpha goes to zero -- the runtime rule. Zeroing the colour too
    // would change what a bilinear filter blends toward at the edge of a keyed region.
    EXPECT_EQ(keyed.representations[0].levels[0][0], 200u);
    EXPECT_EQ(keyed.representations[0].levels[0][1], 100u);
    EXPECT_EQ(keyed.representations[0].levels[0][2], 50u);
    EXPECT_EQ(keyed.representations[0].levels[0][3], 0u);
    // Every other pixel is untouched.
    for (std::size_t i = 4u; i < rgba.size(); ++i)
    {
        EXPECT_EQ(keyed.representations[0].levels[0][i], rgba[i]) << "byte " << i;
    }
}

// --------------------------------------------------------------------------------------------
// Direct WAV -> SoundEffect (CNBF-110)
// --------------------------------------------------------------------------------------------

TEST(CnbProducerTest, AMonoPcm16WavCompilesWithItsSamplesAndRateIntact)
{
    const std::vector<std::uint8_t> pcm = Pcm16(500u, 1u);
    const auto sound = CNA::Content::Cnb::DecodeWavAsCnbSoundEffect(
        MakeWav(1u, 22050u, 16u, pcm), "mono.wav");
    EXPECT_EQ(sound.format, CNA::Content::Cnb::CnbAudioFormat::Pcm16);
    EXPECT_EQ(sound.channels, 1u);
    EXPECT_EQ(sound.sampleRate, 22050u);
    EXPECT_EQ(sound.frameCount, 500u);
    EXPECT_EQ(sound.samples, pcm);
    // The container bytes must never be mistaken for PCM; a RIFF header at the front would be
    // audible as a click and is exactly what SoundEffect's own docs warn about.
    EXPECT_NE(sound.samples.size(), 0u);
    EXPECT_FALSE(sound.samples.size() >= 4u && sound.samples[0] == 'R' && sound.samples[1] == 'I');
}

TEST(CnbProducerTest, AStereoPcm16WavKeepsBothChannels)
{
    const std::vector<std::uint8_t> pcm = Pcm16(300u, 2u);
    const auto sound = CNA::Content::Cnb::DecodeWavAsCnbSoundEffect(
        MakeWav(2u, 48000u, 16u, pcm), "stereo.wav");
    EXPECT_EQ(sound.channels, 2u);
    EXPECT_EQ(sound.sampleRate, 48000u);
    EXPECT_EQ(sound.frameCount, 300u);
    EXPECT_EQ(sound.samples.size(), 300u * 2u * 2u);
}

TEST(CnbProducerTest, AnEightBitWavIsWidenedExactly)
{
    // 8-bit WAV samples are UNSIGNED with a bias of 128. Getting that wrong shifts the whole
    // waveform by half its range, which is a very loud kind of wrong.
    const std::vector<std::uint8_t> pcm8{0u, 128u, 255u, 64u};
    const auto sound = CNA::Content::Cnb::DecodeWavAsCnbSoundEffect(
        MakeWav(1u, 8000u, 8u, pcm8), "eight.wav");
    ASSERT_EQ(sound.samples.size(), 8u);
    const auto sample = [&](std::size_t i)
    { return static_cast<std::int16_t>(sound.samples[i * 2u] |
                                        (sound.samples[i * 2u + 1u] << 8)); };
    EXPECT_EQ(sample(0), static_cast<std::int16_t>(-32768));
    EXPECT_EQ(sample(1), 0);
    EXPECT_EQ(sample(2), static_cast<std::int16_t>(32512));
    EXPECT_EQ(sample(3), static_cast<std::int16_t>(-16384));
}

TEST(CnbProducerTest, ASmplChunkBecomesTheSoundsLoopRegion)
{
    const auto sound = CNA::Content::Cnb::DecodeWavAsCnbSoundEffect(
        MakeWav(1u, 44100u, 16u, Pcm16(1000u, 1u), true, 100u, 400u), "loop.wav");
    EXPECT_EQ(sound.loopStart, 100u);
    EXPECT_EQ(sound.loopLength, 300u);
}

TEST(CnbProducerTest, MalformedAndUnsupportedWavsAreRefusedByReason)
{
    using CNA::Content::Cnb::DecodeWavAsCnbSoundEffect;
    const auto expectMessage = [](const std::vector<std::uint8_t>& bytes, const char* fragment)
    {
        try
        {
            (void)DecodeWavAsCnbSoundEffect(bytes, "bad.wav");
            ADD_FAILURE() << "expected a refusal mentioning '" << fragment << "'";
        }
        catch (const ContentLoadException& e)
        {
            EXPECT_NE(std::string(e.what()).find(fragment), std::string::npos) << e.what();
        }
    };

    expectMessage({}, "too short");
    expectMessage(std::vector<std::uint8_t>(64u, 0u), "not a RIFF/WAVE file");

    // Truncated: the data chunk claims more than the file holds.
    std::vector<std::uint8_t> truncated = MakeWav(1u, 44100u, 16u, Pcm16(100u, 1u));
    truncated.resize(truncated.size() - 50u);
    expectMessage(truncated, "runs past the end");

    // A fmt chunk that is too short to be a WAVEFORMAT at all.
    std::vector<std::uint8_t> shortFmt = MakeWav(1u, 44100u, 16u, Pcm16(4u, 1u));
    shortFmt[16] = 8u; // fmt chunk size 8
    expectMessage(shortFmt, "shorter than 16 bytes");

    // A data length that is not a whole number of frames. Stereo 16-bit frames are 4 bytes, so
    // a 6-byte payload cannot be whole -- built directly rather than by nudging a byte at a
    // guessed offset, which is how the first version of this case silently tested nothing.
    expectMessage(MakeWav(2u, 44100u, 16u, std::vector<std::uint8_t>(6u, 0u)),
                  "whole number of");

    // An encoding this compiler deliberately refuses rather than converting lossily.
    std::vector<std::uint8_t> ieee = MakeWav(1u, 44100u, 16u, Pcm16(8u, 1u));
    ieee[20] = 3u; // format tag 3 = IEEE float
    expectMessage(ieee, "IEEE float");

    std::vector<std::uint8_t> deep = MakeWav(1u, 44100u, 16u, Pcm16(8u, 1u));
    deep[34] = 24u; // 24 bits per sample
    expectMessage(deep, "24-bit PCM");
}

// --------------------------------------------------------------------------------------------
// CNJ -> CNB for the types the compiler could not previously reach (CNBF-111)
// --------------------------------------------------------------------------------------------

TEST(CnbProducerTest, ATexture2DCnjCompilesAndAgreesWithTheRuntimeCnjRoute)
{
    Scratch root("t2dcnj");
    const std::vector<std::uint8_t> rgba = DistinctPixels(6, 3);
    WriteBytes(root.path() / "src.png", MakePng(rgba, 6, 3));
    WriteText(root.path() / "pic.cnj",
              R"({"cnjVersion":1,"type":"Texture2D","sourceFile":"src.png"})");

    const auto compiled = CompileCnjToCnb((root.path() / "pic.cnj").string());
    EXPECT_EQ(compiled.assetTypeId, CNA::Content::Cnb::CnbAssetTypeId::Texture2D);
    // The image was absorbed: the compiled file needs nothing beside it.
    EXPECT_NE(std::find(compiled.absorbedFiles.begin(), compiled.absorbedFiles.end(), "src.png"),
              compiled.absorbedFiles.end());

    const auto decoded = CNA::Content::Cnb::DecodeTexture2DFromCnb(
        CnbDocument::Parse(compiled.bytes, "pic.cnb"));
    EXPECT_EQ(decoded.representations[0].levels[0], rgba);

    // And the runtime .cnj route produces the same pixels, which is the invariant that matters:
    // compiling an asset must not change what it is.
    GraphicsDevice device;
    ContentManager viaCnj(nullptr, root.path().string());
    viaCnj.setGraphicsDevice(device);
    auto fromCnj = viaCnj.Load<Microsoft::Xna::Framework::Graphics::Texture2D>("pic.cnj");
    EXPECT_EQ(fromCnj.getWidthProperty(), 6);
    std::vector<Microsoft::Xna::Framework::Color> cnjPixels(18);
    fromCnj.GetData(cnjPixels.data(), 18);
    for (std::size_t i = 0; i < cnjPixels.size(); ++i)
    {
        EXPECT_EQ(cnjPixels[i].getRProperty(), rgba[i * 4u]) << "texel " << i;
        EXPECT_EQ(cnjPixels[i].getAProperty(), rgba[i * 4u + 3u]) << "texel " << i;
    }
}

TEST(CnbProducerTest, ATexture2DCnjColourKeyReachesTheCompiledFile)
{
    Scratch root("t2dkey");
    std::vector<std::uint8_t> rgba = DistinctPixels(4, 2);
    rgba[0] = 9u; rgba[1] = 8u; rgba[2] = 7u; rgba[3] = 255u;
    WriteBytes(root.path() / "s.png", MakePng(rgba, 4, 2));
    WriteText(root.path() / "k.cnj",
              R"({"cnjVersion":1,"type":"Texture2D","sourceFile":"s.png","colorKey":[9,8,7]})");

    const auto compiled = CompileCnjToCnb((root.path() / "k.cnj").string());
    const auto decoded = CNA::Content::Cnb::DecodeTexture2DFromCnb(
        CnbDocument::Parse(compiled.bytes, "k.cnb"));
    EXPECT_EQ(decoded.representations[0].levels[0][3], 0u)
        << "the .cnj asked for a colour key, so the compiled file must carry its effect";
    EXPECT_EQ(decoded.representations[0].levels[0][0], 9u) << "the colour itself is kept";
}

TEST(CnbProducerTest, ATexture3DCnjCompilesFromItsRawSidecar)
{
    Scratch root("t3d");
    std::vector<std::uint8_t> voxels(2u * 2u * 3u * 4u);
    for (std::size_t i = 0; i < voxels.size(); ++i)
    {
        voxels[i] = static_cast<std::uint8_t>(i * 3u + 1u);
    }
    WriteBytes(root.path() / "vol.bin", voxels);
    WriteText(root.path() / "vol.cnj",
              R"({"cnjVersion":1,"type":"Texture3D","width":2,"height":2,"depth":3,"data":"vol.bin"})");

    const auto compiled = CompileCnjToCnb((root.path() / "vol.cnj").string());
    EXPECT_EQ(compiled.assetTypeId, CNA::Content::Cnb::CnbAssetTypeId::Texture3D);
    const auto decoded = CNA::Content::Cnb::DecodeTexture3DFromCnb(
        CnbDocument::Parse(compiled.bytes, "vol.cnb"));
    EXPECT_EQ(decoded.width, 2u);
    EXPECT_EQ(decoded.height, 2u);
    EXPECT_EQ(decoded.depth, 3u);
    EXPECT_EQ(decoded.representations[0].levels[0], voxels);
}

TEST(CnbProducerTest, ATexture3DCnjWhoseSidecarIsTheWrongSizeIsRefused)
{
    Scratch root("t3dbad");
    WriteBytes(root.path() / "v.bin", std::vector<std::uint8_t>(10u, 0u));
    WriteText(root.path() / "v.cnj",
              R"({"cnjVersion":1,"type":"Texture3D","width":2,"height":2,"depth":3,"data":"v.bin"})");
    EXPECT_THROW((void)CompileCnjToCnb((root.path() / "v.cnj").string()), ContentLoadException);
}

TEST(CnbProducerTest, ASoundEffectCnjCompilesThroughTheHeadlessWavPath)
{
    Scratch root("sfxcnj");
    const std::vector<std::uint8_t> pcm = Pcm16(256u, 1u);
    WriteBytes(root.path() / "beep.wav", MakeWav(1u, 16000u, 16u, pcm));
    WriteText(root.path() / "beep.cnj",
              R"({"cnjVersion":1,"type":"SoundEffect","sourceFile":"beep.wav"})");

    const auto compiled = CompileCnjToCnb((root.path() / "beep.cnj").string());
    EXPECT_EQ(compiled.assetTypeId, CNA::Content::Cnb::CnbAssetTypeId::SoundEffect);
    const auto decoded = CNA::Content::Cnb::DecodeSoundEffectFromCnb(
        CnbDocument::Parse(compiled.bytes, "beep.cnb"));
    EXPECT_EQ(decoded.sampleRate, 16000u);
    EXPECT_EQ(decoded.frameCount, 256u);
    EXPECT_EQ(decoded.samples, pcm);
}

// --------------------------------------------------------------------------------------------
// SpriteFont: the one that has to become a SINGLE self-contained file (CNBF-111)
// --------------------------------------------------------------------------------------------

namespace
{
    std::string SpriteFontCnj()
    {
        return R"({"cnjVersion":1,"type":"SpriteFont","texture":"atlas.png",)"
               R"("lineSpacing":12,"spacing":1.5,"defaultCharacter":"?","glyphs":[)"
               R"({"char":63,"source":[0,0,3,4],"crop":[0,1,3,4],"kerning":[0.0,3.0,0.5]},)"
               R"({"char":65,"source":[3,0,2,4],"crop":[1,0,2,4],"kerning":[-1.0,2.0,0.0]},)"
               R"({"char":66,"source":[5,0,2,4],"crop":[0,0,2,4],"kerning":[0.25,2.0,-0.25]}]})";
    }
}

TEST(CnbProducerTest, ASpriteFontCnjBecomesOneSelfContainedCnb)
{
    Scratch root("font");
    const std::vector<std::uint8_t> atlas = DistinctPixels(8, 4);
    WriteBytes(root.path() / "atlas.png", MakePng(atlas, 8, 4));
    WriteText(root.path() / "ui.cnj", SpriteFontCnj());

    const auto compiled = CompileCnjToCnb((root.path() / "ui.cnj").string());
    EXPECT_EQ(compiled.assetTypeId, CNA::Content::Cnb::CnbAssetTypeId::SpriteFont);
    EXPECT_NE(std::find(compiled.absorbedFiles.begin(), compiled.absorbedFiles.end(), "atlas.png"),
              compiled.absorbedFiles.end())
        << "the atlas must be absorbed, not referenced";

    const CnbDocument document = CnbDocument::Parse(compiled.bytes, "ui.cnb");
    EXPECT_TRUE(document.ExternalReferences().empty())
        << "a compiled SpriteFont must need nothing beside it at runtime";
    const auto font = CNA::Content::Cnb::DecodeSpriteFontFromCnb(document);
    EXPECT_EQ(font.lineSpacing, 12);
    EXPECT_FLOAT_EQ(font.spacing, 1.5f);
    ASSERT_TRUE(font.defaultCharacter.has_value());
    EXPECT_EQ(*font.defaultCharacter, u'?');
    EXPECT_EQ(font.characters.size(), 3u);
    EXPECT_EQ(font.atlas.width, 8u);
    EXPECT_EQ(font.atlas.representations[0].levels[0], atlas);
}

TEST(CnbProducerTest, TheCompiledSpriteFontLoadsWithoutItsAtlasFileAndMatchesTheCnjRoute)
{
    Scratch source("fontsrc");
    Scratch alone("fontalone");
    const std::vector<std::uint8_t> atlasPixels = DistinctPixels(8, 4);
    WriteBytes(source.path() / "atlas.png", MakePng(atlasPixels, 8, 4));
    WriteText(source.path() / "ui.cnj", SpriteFontCnj());

    const auto compiled = CompileCnjToCnb((source.path() / "ui.cnj").string());
    // Written into a directory holding NOTHING else: if the .cnb still needed the atlas, this
    // load would fail. That is the whole claim "self-contained" makes.
    WriteBytes(alone.path() / "ui.cnb", compiled.bytes);

    GraphicsDevice device;
    ContentManager viaCnb(nullptr, alone.path().string());
    viaCnb.setGraphicsDevice(device);
    auto fromCnb = viaCnb.Load<Microsoft::Xna::Framework::Graphics::SpriteFont>("ui");

    ContentManager viaCnj(nullptr, source.path().string());
    viaCnj.setGraphicsDevice(device);
    auto fromCnj = viaCnj.Load<Microsoft::Xna::Framework::Graphics::SpriteFont>("ui.cnj");

    EXPECT_EQ(fromCnb.getLineSpacingProperty(), fromCnj.getLineSpacingProperty());
    EXPECT_FLOAT_EQ(fromCnb.getSpacingProperty(), fromCnj.getSpacingProperty());
    EXPECT_EQ(fromCnb.getCharactersProperty(), fromCnj.getCharactersProperty());
    EXPECT_EQ(fromCnb.getDefaultCharacterProperty(), fromCnj.getDefaultCharacterProperty());
    ASSERT_EQ(fromCnb.getGlyphBoundsEXT().size(), fromCnj.getGlyphBoundsEXT().size());
    for (std::size_t i = 0; i < fromCnb.getGlyphBoundsEXT().size(); ++i)
    {
        EXPECT_EQ(fromCnb.getGlyphBoundsEXT()[i], fromCnj.getGlyphBoundsEXT()[i]) << i;
        EXPECT_EQ(fromCnb.getCroppingEXT()[i], fromCnj.getCroppingEXT()[i]) << i;
        EXPECT_FLOAT_EQ(fromCnb.getKerningEXT()[i].Y, fromCnj.getKerningEXT()[i].Y) << i;
    }
    EXPECT_EQ(fromCnb.getTextureEXT().getWidthProperty(),
              fromCnj.getTextureEXT().getWidthProperty());

    // The observable behaviour, not just the tables.
    EXPECT_EQ(fromCnb.MeasureString("AB").X, fromCnj.MeasureString("AB").X);
    EXPECT_EQ(fromCnb.MeasureString("AB").Y, fromCnj.MeasureString("AB").Y);
}

// --------------------------------------------------------------------------------------------
// Containment and determinism -- required of every producer
// --------------------------------------------------------------------------------------------

TEST(CnbProducerTest, ACompilerCannotBeUsedToEscapeTheContentRoot)
{
    // A compiler that resolved paths more permissively than the runtime would be the soft way
    // into a file the runtime refuses to open.
    Scratch root("escape");
    WriteText(root.path() / "bad.cnj",
              R"({"cnjVersion":1,"type":"Texture2D","sourceFile":"../../etc/passwd"})");
    EXPECT_THROW((void)CompileCnjToCnb((root.path() / "bad.cnj").string()), std::exception);

    WriteText(root.path() / "abs.cnj",
              R"({"cnjVersion":1,"type":"Texture2D","sourceFile":"/etc/passwd"})");
    EXPECT_THROW((void)CompileCnjToCnb((root.path() / "abs.cnj").string()), std::exception);

    WriteText(root.path() / "missing.cnj",
              R"({"cnjVersion":1,"type":"Texture2D","sourceFile":"nope.png"})");
    EXPECT_THROW((void)CompileCnjToCnb((root.path() / "missing.cnj").string()), std::exception);
}

TEST(CnbProducerTest, EveryProducerIsDeterministic)
{
    // Identical source bytes, options and logical name must give identical output bytes. No
    // clock, no randomness, no absolute path leaking into the file.
    Scratch root("det");
    const std::vector<std::uint8_t> rgba = DistinctPixels(5, 5);
    WriteBytes(root.path() / "d.png", MakePng(rgba, 5, 5));
    WriteBytes(root.path() / "d.wav", MakeWav(1u, 44100u, 16u, Pcm16(64u, 1u)));
    WriteBytes(root.path() / "atlas.png", MakePng(DistinctPixels(8, 4), 8, 4));
    WriteText(root.path() / "t.cnj", R"({"cnjVersion":1,"type":"Texture2D","sourceFile":"d.png"})");
    WriteText(root.path() / "s.cnj", R"({"cnjVersion":1,"type":"SoundEffect","sourceFile":"d.wav"})");
    WriteText(root.path() / "f.cnj", SpriteFontCnj());

    const auto image = [&]
    { return CNA::Content::Cnb::EncodeTexture2DToCnb(
          CNA::Content::Cnb::ImportImageAsCnbTexture2D((root.path() / "d.png").string()), "d"); };
    EXPECT_EQ(image(), image());

    const auto audio = [&]
    { return CNA::Content::Cnb::EncodeSoundEffectToCnb(
          CNA::Content::Cnb::ImportWavAsCnbSoundEffect((root.path() / "d.wav").string()), "d"); };
    EXPECT_EQ(audio(), audio());

    for (const char* document : {"t.cnj", "s.cnj", "f.cnj"})
    {
        const auto once = CompileCnjToCnb((root.path() / document).string());
        const auto twice = CompileCnjToCnb((root.path() / document).string());
        EXPECT_EQ(once.bytes, twice.bytes) << document;
    }
}

TEST(CnbProducerTest, AnUnsupportedTypeIsRefusedWithTheListOfWhatIsSupported)
{
    // TextureCube used to be the subject of this test, as the one implemented schema with no
    // producer. CNBF-113 closed that gap, so the assertion moved to a type that genuinely has no
    // CNB schema -- Effect, which is waiting on the FX/shader architecture by design.
    //
    // A refusal has to name what the compiler CAN do, otherwise the user's next step is guesswork.
    Scratch root("unsupported");
    WriteText(root.path() / "fx.cnj",
              R"({"cnjVersion":1,"type":"Effect","sourceFile":"fx.fx"})");
    try
    {
        (void)CompileCnjToCnb((root.path() / "fx.cnj").string());
        FAIL() << "Effect has no CNB schema and must be refused";
    }
    catch (const ContentLoadException& e)
    {
        const std::string message = e.what();
        EXPECT_NE(message.find("Effect"), std::string::npos) << message;
        EXPECT_NE(message.find("Supported types are"), std::string::npos)
            << "the refusal must say what IS supported: " << message;
        EXPECT_NE(message.find("TextureCube"), std::string::npos)
            << "TextureCube is supported now and must appear in that list: " << message;
    }
}
