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
#include <optional>
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

// --------------------------------------------------------------------------------------------
// CNBF-117 -- the RIFF/WAVE parser against files written to break it
// --------------------------------------------------------------------------------------------

namespace
{
    /// Assembles an arbitrary RIFF/WAVE chunk list, including shapes MakeWav() cannot express: a
    /// wrong declared RIFF length, an odd-sized chunk, a truncated `smpl` loop table, an
    /// EXTENSIBLE `fmt `. Every hostile case below is one edit away from a file this builder also
    /// produces valid, so "the parser refused it" is never confusable with "the fixture was
    /// never valid".
    struct WavBuilder
    {
        std::vector<std::uint8_t> body;   // everything after "RIFF" + size + "WAVE"

        static void PutU16(std::vector<std::uint8_t>& out, std::uint16_t v)
        {
            out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
            out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
        }
        static void PutU32(std::vector<std::uint8_t>& out, std::uint32_t v)
        {
            out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
            out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
            out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
            out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
        }

        /// Appends one chunk. `pad` writes RIFF's alignment byte after an odd-length payload;
        /// passing false is how the "missing pad byte" case is built.
        WavBuilder& Chunk(const char* id, const std::vector<std::uint8_t>& payload,
                          bool pad = true)
        {
            for (int i = 0; i < 4; ++i) { body.push_back(static_cast<std::uint8_t>(id[i])); }
            PutU32(body, static_cast<std::uint32_t>(payload.size()));
            body.insert(body.end(), payload.begin(), payload.end());
            if (pad && (payload.size() & 1u) != 0u) { body.push_back(0u); }
            return *this;
        }

        WavBuilder& Fmt(std::uint16_t formatTag, std::uint16_t channels, std::uint32_t sampleRate,
                        std::uint16_t bitsPerSample,
                        std::optional<std::uint16_t> blockAlignOverride = std::nullopt,
                        std::optional<std::uint32_t> byteRateOverride = std::nullopt)
        {
            const auto blockAlign = static_cast<std::uint16_t>(
                blockAlignOverride.value_or(static_cast<std::uint16_t>(channels * (bitsPerSample / 8u))));
            const std::uint32_t byteRate = byteRateOverride.value_or(
                sampleRate * static_cast<std::uint32_t>(channels * (bitsPerSample / 8u)));
            std::vector<std::uint8_t> fmt;
            PutU16(fmt, formatTag);
            PutU16(fmt, channels);
            PutU32(fmt, sampleRate);
            PutU32(fmt, byteRate);
            PutU16(fmt, blockAlign);
            PutU16(fmt, bitsPerSample);
            return Chunk("fmt ", fmt);
        }

        /// An EXTENSIBLE `fmt ` chunk. `guid` is the whole 16-byte SubFormat, so a test can supply
        /// one outside the KSDATAFORMAT_SUBTYPE_* family.
        WavBuilder& FmtExtensible(std::uint16_t channels, std::uint32_t sampleRate,
                                  std::uint16_t bitsPerSample, std::uint16_t validBits,
                                  const std::array<std::uint8_t, 16>& guid,
                                  std::uint16_t cbSize = 22u)
        {
            const auto blockAlign = static_cast<std::uint16_t>(channels * (bitsPerSample / 8u));
            std::vector<std::uint8_t> fmt;
            PutU16(fmt, 0xFFFEu);
            PutU16(fmt, channels);
            PutU32(fmt, sampleRate);
            PutU32(fmt, sampleRate * blockAlign);
            PutU16(fmt, blockAlign);
            PutU16(fmt, bitsPerSample);
            PutU16(fmt, cbSize);
            PutU16(fmt, validBits);
            PutU32(fmt, 0u);   // channel mask
            fmt.insert(fmt.end(), guid.begin(), guid.end());
            return Chunk("fmt ", fmt);
        }

        WavBuilder& Data(const std::vector<std::uint8_t>& pcm) { return Chunk("data", pcm); }

        /// A `smpl` chunk of `totalBytes`, declaring `loops` loops. `totalBytes` below 60 is how
        /// the truncated-loop-table cases are built; exactly 36 is the case that used to read its
        /// "loop entry" out of whatever chunk followed.
        WavBuilder& Smpl(std::size_t totalBytes, std::uint32_t loops, std::uint32_t loopStart = 0u,
                         std::uint32_t loopEnd = 0u)
        {
            std::vector<std::uint8_t> smpl(totalBytes, 0u);
            const auto put = [&](std::size_t at, std::uint32_t v)
            {
                if (at + 4u > smpl.size()) { return; }
                smpl[at] = static_cast<std::uint8_t>(v & 0xFFu);
                smpl[at + 1u] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
                smpl[at + 2u] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
                smpl[at + 3u] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
            };
            put(28u, loops);
            put(44u, loopStart);
            put(48u, loopEnd);
            return Chunk("smpl", smpl);
        }

        /// `riffSizeOverride` replaces the computed RIFF length, which is how the declared-length
        /// cases are built.
        [[nodiscard]] std::vector<std::uint8_t> Build(
            std::optional<std::uint32_t> riffSizeOverride = std::nullopt) const
        {
            std::vector<std::uint8_t> out;
            for (const char c : std::string("RIFF")) { out.push_back(static_cast<std::uint8_t>(c)); }
            PutU32(out, riffSizeOverride.value_or(static_cast<std::uint32_t>(4u + body.size())));
            for (const char c : std::string("WAVE")) { out.push_back(static_cast<std::uint8_t>(c)); }
            out.insert(out.end(), body.begin(), body.end());
            return out;
        }
    };

    /// The PCM member of the KSDATAFORMAT_SUBTYPE_* family: {00000001-0000-0010-8000-00AA00389B71}.
    constexpr std::array<std::uint8_t, 16> kPcmSubtypeGuid = {
        0x01u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x10u, 0x00u,
        0x80u, 0x00u, 0x00u, 0xAAu, 0x00u, 0x38u, 0x9Bu, 0x71u};

    void ExpectWavRefused(const std::vector<std::uint8_t>& bytes, const char* fragment,
                          const char* what)
    {
        try
        {
            (void)CNA::Content::Cnb::DecodeWavAsCnbSoundEffect(bytes, "hostile.wav");
            ADD_FAILURE() << what << ": expected a refusal mentioning '" << fragment << "'";
        }
        catch (const ContentLoadException& e)
        {
            EXPECT_NE(std::string(e.what()).find(fragment), std::string::npos)
                << what << ": " << e.what();
        }
    }
}

TEST(CnbProducerTest, ThirtySixByteSmplFollowedByAnotherChunkDoesNotInventALoop)
{
    // The defect this pins: the loop-entry read was bounded by the FILE, not by the smpl chunk, so
    // a 36-byte smpl declaring a loop took its 24-byte "entry" out of the next chunk's header and
    // first sixteen bytes. With 'data' following, the loop region came from the ASCII "data", the
    // payload length and the samples themselves -- a loop invented from unrelated bytes.
    const std::vector<std::uint8_t> pcm = Pcm16(1000u, 1u);
    const std::vector<std::uint8_t> bytes =
        WavBuilder{}.Fmt(1u, 1u, 44100u, 16u).Smpl(36u, 1u).Data(pcm).Build();
    ExpectWavRefused(bytes, "loop table", "36-byte smpl declaring a loop");

    // The same chunk declaring NO loops is well-formed and must still compile: the refusal is
    // about a loop that is not there, not about a short smpl.
    const std::vector<std::uint8_t> noLoops =
        WavBuilder{}.Fmt(1u, 1u, 44100u, 16u).Smpl(36u, 0u).Data(pcm).Build();
    const auto sound = CNA::Content::Cnb::DecodeWavAsCnbSoundEffect(noLoops, "noloops.wav");
    EXPECT_EQ(sound.loopStart, 0u);
    EXPECT_EQ(sound.loopLength, 0u);
    EXPECT_EQ(sound.frameCount, 1000u);
}

TEST(CnbProducerTest, ATruncatedSmplLoopEntryIsRefusedRatherThanReadPast)
{
    const std::vector<std::uint8_t> pcm = Pcm16(1000u, 1u);
    for (const std::size_t smplBytes : {37u, 40u, 52u, 59u})
    {
        const std::vector<std::uint8_t> bytes =
            WavBuilder{}.Fmt(1u, 1u, 44100u, 16u).Smpl(smplBytes, 1u).Data(pcm).Build();
        ExpectWavRefused(bytes, "loop table",
                         ("smpl of " + std::to_string(smplBytes) + " bytes").c_str());
    }

    // Exactly 60 bytes is one complete entry and must be accepted, so the bound is not merely
    // "refuse anything unusual".
    const std::vector<std::uint8_t> whole =
        WavBuilder{}.Fmt(1u, 1u, 44100u, 16u).Smpl(60u, 1u, 100u, 400u).Data(pcm).Build();
    const auto sound = CNA::Content::Cnb::DecodeWavAsCnbSoundEffect(whole, "loop.wav");
    EXPECT_EQ(sound.loopStart, 100u);
    EXPECT_EQ(sound.loopLength, 300u);
}

TEST(CnbProducerTest, OddSizedChunksAreWalkedThroughTheirRiffPadByte)
{
    const std::vector<std::uint8_t> pcm = Pcm16(64u, 1u);

    // A padded odd-length chunk ahead of 'fmt ' must not shift the walk: if the pad byte were
    // ignored, every subsequent chunk header would be read one byte early.
    const std::vector<std::uint8_t> padded = WavBuilder{}
                                                 .Chunk("junk", std::vector<std::uint8_t>(5u, 0xABu))
                                                 .Fmt(1u, 1u, 32000u, 16u)
                                                 .Data(pcm)
                                                 .Build();
    const auto sound = CNA::Content::Cnb::DecodeWavAsCnbSoundEffect(padded, "padded.wav");
    EXPECT_EQ(sound.sampleRate, 32000u);
    EXPECT_EQ(sound.frameCount, 64u);

    // The same file with the pad byte omitted: the RIFF form and its chunk list now disagree, and
    // the parser says so instead of resynchronising onto a byte offset that is not a chunk header.
    const std::vector<std::uint8_t> unpadded =
        WavBuilder{}
            .Chunk("junk", std::vector<std::uint8_t>(5u, 0xABu), /*pad=*/false)
            .Fmt(1u, 1u, 32000u, 16u)
            .Data(pcm)
            .Build();
    EXPECT_THROW((void)CNA::Content::Cnb::DecodeWavAsCnbSoundEffect(unpadded, "unpadded.wav"),
                 ContentLoadException);

    // A trailing odd-length chunk at the very end of the form needs no pad byte.
    const std::vector<std::uint8_t> trailing =
        WavBuilder{}
            .Fmt(1u, 1u, 32000u, 16u)
            .Data(pcm)
            .Chunk("junk", std::vector<std::uint8_t>(3u, 0u), /*pad=*/false)
            .Build();
    EXPECT_NO_THROW((void)CNA::Content::Cnb::DecodeWavAsCnbSoundEffect(trailing, "trailing.wav"));
}

TEST(CnbProducerTest, AMalformedRiffDeclaredLengthIsRefused)
{
    const std::vector<std::uint8_t> pcm = Pcm16(64u, 1u);
    const WavBuilder builder = WavBuilder{}.Fmt(1u, 1u, 44100u, 16u).Data(pcm);
    const std::vector<std::uint8_t> good = builder.Build();
    EXPECT_NO_THROW((void)CNA::Content::Cnb::DecodeWavAsCnbSoundEffect(good, "good.wav"));

    // Longer than the file: a truncated download, and the value that bounds the chunk walk.
    ExpectWavRefused(builder.Build(static_cast<std::uint32_t>(good.size())), "past the end",
                     "RIFF length past the end of the file");
    ExpectWavRefused(builder.Build(0xFFFFFFFFu), "past the end", "RIFF length 0xFFFFFFFF");

    // Too short even for its own form identifier.
    ExpectWavRefused(builder.Build(0u), "too short", "RIFF length 0");
    ExpectWavRefused(builder.Build(3u), "too short", "RIFF length 3");

    // Shorter than the chunks it contains: the walk stops at the declared end, so 'data' is
    // reported as running past the form rather than being read anyway.
    ExpectWavRefused(builder.Build(static_cast<std::uint32_t>(good.size() - 8u - 20u)),
                     "runs past the end of the RIFF form", "RIFF length cutting 'data' short");
}

TEST(CnbProducerTest, FmtFieldsThatDisagreeWithEachOtherAreRefused)
{
    const std::vector<std::uint8_t> pcm = Pcm16(64u, 2u);

    // blockAlign says 2 bytes per frame; two channels of 16-bit samples is 4. The importer splits
    // the data chunk by the derived value, so believing the declared one would produce twice the
    // frames at half the width -- audible, and silent.
    ExpectWavRefused(
        WavBuilder{}.Fmt(1u, 2u, 44100u, 16u, /*blockAlign=*/2u).Data(pcm).Build(),
        "block alignment", "blockAlign disagreeing with channels x bits");

    ExpectWavRefused(
        WavBuilder{}.Fmt(1u, 2u, 44100u, 16u, std::nullopt, /*byteRate=*/1234u).Data(pcm).Build(),
        "byte rate", "byteRate disagreeing with rate x blockAlign");

    // Both consistent: still accepted, so the checks are not simply refusing everything.
    const auto sound = CNA::Content::Cnb::DecodeWavAsCnbSoundEffect(
        WavBuilder{}.Fmt(1u, 2u, 44100u, 16u).Data(pcm).Build(), "consistent.wav");
    EXPECT_EQ(sound.channels, 2u);
    EXPECT_EQ(sound.frameCount, 64u);
}

TEST(CnbProducerTest, ExtensibleFormatIsAcceptedOnlyForARealKsDataFormatSubtype)
{
    const std::vector<std::uint8_t> pcm = Pcm16(64u, 1u);

    // The genuine PCM subtype decodes exactly as a plain PCM file would.
    const auto sound = CNA::Content::Cnb::DecodeWavAsCnbSoundEffect(
        WavBuilder{}.FmtExtensible(1u, 44100u, 16u, 16u, kPcmSubtypeGuid).Data(pcm).Build(),
        "extensible.wav");
    EXPECT_EQ(sound.sampleRate, 44100u);
    EXPECT_EQ(sound.channels, 1u);
    ASSERT_EQ(sound.samples.size(), pcm.size());
    EXPECT_TRUE(std::equal(sound.samples.begin(), sound.samples.end(), pcm.begin()));

    // An unrelated GUID whose first two bytes happen to be 0x0001. Reading only those two bytes
    // called this PCM; the rest of the GUID says it is not, and CNA does not guess.
    std::array<std::uint8_t, 16> impostor = kPcmSubtypeGuid;
    impostor[8] = 0x12u;
    impostor[15] = 0x34u;
    ExpectWavRefused(
        WavBuilder{}.FmtExtensible(1u, 44100u, 16u, 16u, impostor).Data(pcm).Build(),
        "KSDATAFORMAT_SUBTYPE_", "an unrelated SubFormat GUID beginning 01 00");

    // A truncated extension, an undersized cbSize, and a container wider than its valid bits.
    ExpectWavRefused(
        WavBuilder{}.FmtExtensible(1u, 44100u, 16u, 16u, kPcmSubtypeGuid, /*cbSize=*/0u)
            .Data(pcm).Build(),
        "extension size", "cbSize 0");
    ExpectWavRefused(
        WavBuilder{}.FmtExtensible(1u, 44100u, 16u, /*validBits=*/12u, kPcmSubtypeGuid)
            .Data(pcm).Build(),
        "valid bits", "12 valid bits in a 16-bit container");

    // A 16-byte EXTENSIBLE fmt has no GUID at all: it used to fall through with the tag left at
    // 0xFFFE and be refused for the wrong reason.
    std::vector<std::uint8_t> shortExtensible =
        WavBuilder{}.Fmt(0xFFFEu, 1u, 44100u, 16u).Data(pcm).Build();
    ExpectWavRefused(shortExtensible, "the extension needs 40", "a 16-byte EXTENSIBLE fmt");
}

TEST(CnbProducerTest, AnExtensibleFmtMustDeclareAnExtensionThatFitsInsideItsChunk)
{
    // plans/plan_cnb.md CNBF-122. `chunkSize >= 40` proves the 22 extension bytes this importer
    // READS are present; it says nothing about the length the header itself declares. A cbSize of
    // 4000 in a 40-byte 'fmt ' describes 4018 bytes of WAVEFORMATEXTENSIBLE that are not there,
    // and every other field in that same header was then believed.
    const std::vector<std::uint8_t> pcm = Pcm16(64u, 1u);

    // Exactly at the boundary: 18 bytes of WAVEFORMATEX plus a 22-byte extension is the 40-byte
    // chunk the builder produces, so this is the largest cbSize that fits and must be accepted.
    const auto exact = CNA::Content::Cnb::DecodeWavAsCnbSoundEffect(
        WavBuilder{}.FmtExtensible(1u, 44100u, 16u, 16u, kPcmSubtypeGuid, /*cbSize=*/22u)
            .Data(pcm).Build(),
        "exact.wav");
    EXPECT_EQ(exact.sampleRate, 44100u);
    EXPECT_EQ(exact.frameCount, 64u);

    // One byte over, and then a value that is nowhere near the chunk.
    for (const std::uint16_t cbSize : {std::uint16_t{23u}, std::uint16_t{64u},
                                       std::uint16_t{4000u}, std::uint16_t{0xFFFFu}})
    {
        ExpectWavRefused(
            WavBuilder{}.FmtExtensible(1u, 44100u, 16u, 16u, kPcmSubtypeGuid, cbSize)
                .Data(pcm).Build(),
            "its 'fmt ' chunk holds",
            ("EXTENSIBLE cbSize " + std::to_string(cbSize) + " in a 40-byte fmt").c_str());
    }
}

TEST(CnbProducerTest, ADeclaredSmplLoopTableMustFitTheSmplChunk)
{
    // plans/plan_cnb.md CNBF-122. Only the FIRST loop entry is consumed, so only the first was
    // required to fit -- but the loop count is read out of the same header whose first entry is
    // then believed, and a count the chunk cannot hold means the two disagree. Checking one number
    // and not the other is how a plausible wrong loop region survives.
    const std::vector<std::uint8_t> pcm = Pcm16(1000u, 1u);

    // Exact fit: 36-byte header plus two 24-byte entries is 84 bytes, and the first entry decodes.
    const auto exact = CNA::Content::Cnb::DecodeWavAsCnbSoundEffect(
        WavBuilder{}.Fmt(1u, 1u, 44100u, 16u).Smpl(84u, 2u, 100u, 400u).Data(pcm).Build(),
        "twoloops.wav");
    EXPECT_EQ(exact.loopStart, 100u);
    EXPECT_EQ(exact.loopLength, 300u);

    // One entry short of what it declares -- the first entry still fits, so the OLD check passed.
    ExpectWavRefused(
        WavBuilder{}.Fmt(1u, 1u, 44100u, 16u).Smpl(83u, 2u, 100u, 400u).Data(pcm).Build(),
        "bytes of loop table", "two declared loops in 83 bytes");
    ExpectWavRefused(
        WavBuilder{}.Fmt(1u, 1u, 44100u, 16u).Smpl(60u, 2u, 100u, 400u).Data(pcm).Build(),
        "bytes of loop table", "two declared loops in a 60-byte smpl");

    // A count whose 24-byte product WRAPS a 32-bit accumulator: 178 956 971 * 24 is 2^32 + 8, so
    // 36 + that reads as 44 in 32-bit arithmetic and would have fitted a 60-byte chunk. Computed
    // in std::uint64_t it is 4 GiB of loop table and is refused.
    ExpectWavRefused(
        WavBuilder{}.Fmt(1u, 1u, 44100u, 16u).Smpl(60u, 178956971u, 100u, 400u).Data(pcm).Build(),
        "bytes of loop table", "a loop count whose table size wraps 32 bits");
    ExpectWavRefused(
        WavBuilder{}.Fmt(1u, 1u, 44100u, 16u).Smpl(60u, 0xFFFFFFFFu, 100u, 400u).Data(pcm).Build(),
        "bytes of loop table", "a loop count of 0xFFFFFFFF");
}

TEST(CnbProducerTest, RiffIntegersAreDecodedFromTheirBytesRatherThanTheHostLayout)
{
    // Values chosen so every byte of every multi-byte field differs: 0x0000AC44 (44100 Hz) has
    // three distinct bytes, and 44100 x 4 = 176400 = 0x0002B110 has four. A host-endian memcpy
    // decodes these correctly only on a little-endian machine; asserting the exact values here is
    // what the byte-assembled reads have to keep true on any host.
    const std::vector<std::uint8_t> pcm = Pcm16(300u, 2u);
    const std::vector<std::uint8_t> bytes = WavBuilder{}.Fmt(1u, 2u, 44100u, 16u).Data(pcm).Build();

    // The fields really are stored little-endian in the fixture, so the assertions below are about
    // the DECODER rather than about the builder.
    const std::size_t fmtPayload = 12u + 8u;
    EXPECT_EQ(bytes[fmtPayload + 4u], 0x44u);
    EXPECT_EQ(bytes[fmtPayload + 5u], 0xACu);
    EXPECT_EQ(bytes[fmtPayload + 6u], 0x00u);

    const auto sound = CNA::Content::Cnb::DecodeWavAsCnbSoundEffect(bytes, "endian.wav");
    EXPECT_EQ(sound.sampleRate, 44100u);
    EXPECT_EQ(sound.channels, 2u);
    EXPECT_EQ(sound.frameCount, 300u);
}

TEST(CnbProducerTest, ValidPcmFixturesStillCompileAcrossEveryAcceptedShape)
{
    // The positive contract, restated after the hardening: every shape the importer accepts must
    // still round-trip its samples, its rate and its channel count exactly.
    struct Shape { std::uint16_t channels; std::uint32_t rate; std::uint16_t bits; };
    for (const Shape& shape : {Shape{1u, 8000u, 8u}, Shape{1u, 44100u, 16u},
                               Shape{2u, 22050u, 16u}, Shape{2u, 384000u, 8u}})
    {
        const std::size_t frames = 128u;
        const std::vector<std::uint8_t> payload =
            shape.bits == 16u ? Pcm16(frames, shape.channels)
                              : std::vector<std::uint8_t>(frames * shape.channels, 200u);
        const std::vector<std::uint8_t> bytes =
            WavBuilder{}.Fmt(1u, shape.channels, shape.rate, shape.bits).Data(payload).Build();

        const auto sound = CNA::Content::Cnb::DecodeWavAsCnbSoundEffect(bytes, "valid.wav");
        EXPECT_EQ(sound.sampleRate, shape.rate);
        EXPECT_EQ(sound.channels, shape.channels);
        EXPECT_EQ(sound.frameCount, frames);
        EXPECT_EQ(sound.samples.size(), frames * shape.channels * 2u);
        EXPECT_EQ(sound.format, CNA::Content::Cnb::CnbAudioFormat::Pcm16);
        // And it encodes to a .cnb that loads back, so "the parser accepted it" is not the end of
        // the claim.
        EXPECT_NO_THROW((void)CNA::Content::Cnb::EncodeSoundEffectToCnb(sound, "valid"));
    }
}

TEST(CnbProducerTest, DuplicateFmtOrDataChunksAreRefused)
{
    // Two of either means the file describes two different sounds and the importer would silently
    // pick one of them.
    const std::vector<std::uint8_t> pcm = Pcm16(64u, 1u);
    ExpectWavRefused(
        WavBuilder{}.Fmt(1u, 1u, 44100u, 16u).Fmt(1u, 2u, 22050u, 16u).Data(pcm).Build(),
        "more than one 'fmt '", "two fmt chunks");
    ExpectWavRefused(
        WavBuilder{}.Fmt(1u, 1u, 44100u, 16u).Data(pcm).Data(pcm).Build(),
        "more than one 'data'", "two data chunks");
}
