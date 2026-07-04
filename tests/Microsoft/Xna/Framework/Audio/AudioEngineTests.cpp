// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Audio/AudioCategory.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioEngine.hpp"
#include "Microsoft/Xna/Framework/Audio/Cue.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundBank.hpp"
#include "Microsoft/Xna/Framework/Audio/WaveBank.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/EventArgs.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/Object.hpp"
#include "System/ObjectDisposedException.hpp"
#include "System/TimeSpan.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using Microsoft::Xna::Framework::Audio::AudioCategory;
using Microsoft::Xna::Framework::Audio::AudioEngine;
using Microsoft::Xna::Framework::Audio::Cue;
using Microsoft::Xna::Framework::Audio::SoundBank;
using Microsoft::Xna::Framework::Audio::WaveBank;

namespace
{
    void AppendU8(std::vector<uint8_t>& buf, uint8_t v) { buf.push_back(v); }

    void AppendU16(std::vector<uint8_t>& buf, uint16_t v)
    {
        uint8_t bytes[2];
        std::memcpy(bytes, &v, 2);
        buf.insert(buf.end(), bytes, bytes + 2);
    }

    void AppendU32(std::vector<uint8_t>& buf, uint32_t v)
    {
        uint8_t bytes[4];
        std::memcpy(bytes, &v, 4);
        buf.insert(buf.end(), bytes, bytes + 4);
    }

    void AppendS32(std::vector<uint8_t>& buf, int32_t v)
    {
        uint8_t bytes[4];
        std::memcpy(bytes, &v, 4);
        buf.insert(buf.end(), bytes, bytes + 4);
    }

    void AppendPadded(std::vector<uint8_t>& buf, const std::string& text, std::size_t totalSize)
    {
        const std::size_t start = buf.size();
        buf.resize(start + totalSize, 0);
        std::memcpy(buf.data() + start, text.data(), text.size());
    }

    void AppendF32(std::vector<uint8_t>& buf, float v)
    {
        uint8_t bytes[4];
        std::memcpy(bytes, &v, 4);
        buf.insert(buf.end(), bytes, bytes + 4);
    }

    void AppendCStr(std::vector<uint8_t>& buf, const std::string& s)
    {
        buf.insert(buf.end(), s.begin(), s.end());
        buf.push_back(0);
    }

    // Minimal .xgs fixture with one category ("Default") and one variable ("Volume",
    // initial value 0.5) — enough to exercise AudioEngine's valid-name lookup paths.
    std::vector<uint8_t> BuildXgsFixtureBytes()
    {
        constexpr uint32_t headerSize        = 65;
        constexpr uint32_t categoryDataSize  = 10;
        constexpr uint32_t variableDataSize  = 13;

        const uint32_t categoryOffset     = headerSize;
        const uint32_t variableOffset     = categoryOffset + categoryDataSize;
        const uint32_t categoryNameOffset = variableOffset + variableDataSize;
        const std::string categoryName    = "Default";
        const uint32_t variableNameOffset = categoryNameOffset + static_cast<uint32_t>(categoryName.size()) + 1;
        const std::string variableName    = "Volume";

        std::vector<uint8_t> data;

        const char magic[4] = { 'X', 'G', 'S', 'F' };
        data.insert(data.end(), magic, magic + 4);
        AppendU16(data, 46); // contentVersion
        AppendU16(data, 0);  // toolVersion
        AppendU16(data, 0);  // unknown
        for (int i = 0; i < 8; ++i) data.push_back(0); // lastModified
        AppendU8(data, 3);   // platform

        AppendU16(data, 1); // categoryCount
        AppendU16(data, 1); // variableCount
        AppendU16(data, 0); // blob1Count
        AppendU16(data, 0); // blob2Count
        AppendU16(data, 0); // rpcCount
        AppendU16(data, 0); // dspPresetCount
        AppendU16(data, 0); // dspParameterCount

        AppendU32(data, categoryOffset);
        AppendU32(data, variableOffset);
        AppendU32(data, 0); // blob1Offset
        AppendU32(data, 0); // categoryNameIndexOffset
        AppendU32(data, 0); // blob2Offset
        AppendU32(data, 0); // variableNameIndexOffset
        AppendU32(data, categoryNameOffset);
        AppendU32(data, variableNameOffset);

        // Category: instanceLimit, fadeInMS, fadeOutMS, maxInstanceBehavior(skip), parentIndex, volume, visibility
        AppendU8(data, 0xFF);
        AppendU16(data, 0);
        AppendU16(data, 0);
        AppendU8(data, 0);
        AppendU16(data, 0xFFFF);
        AppendU8(data, 0xFF);
        AppendU8(data, 0);

        // Variable: accessibility, initialValue, minValue, maxValue
        AppendU8(data, 0x03);
        AppendF32(data, 0.5f);
        AppendF32(data, 0.0f);
        AppendF32(data, 1.0f);

        AppendCStr(data, categoryName);
        AppendCStr(data, variableName);

        return data;
    }

    const std::string& XgsFixturePath()
    {
        static const std::string path = []() -> std::string
        {
            auto dir = std::filesystem::temp_directory_path() / "cna_audio_engine_test";
            std::filesystem::create_directories(dir);
            auto file = dir / "fixture.xgs";
            const auto bytes = BuildXgsFixtureBytes();
            std::ofstream f(file, std::ios::binary);
            f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            return file.string();
        }();
        return path;
    }

    std::string WriteFixture(const std::string& fileName, const std::vector<uint8_t>& bytes)
    {
        auto dir = std::filesystem::temp_directory_path() / "cna_audio_engine_test";
        std::filesystem::create_directories(dir);
        auto file = dir / fileName;
        std::ofstream f(file, std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return file.string();
    }

    constexpr const char* kXA8WaveBankName = "XA8WaveBank";

    // Minimal compact .xwb with one mono 16-bit PCM entry (200 bytes of silence) -- for XA-8's
    // AudioEngine::Dispose() cascade test, which needs its own real WaveBank/SoundBank/Cue
    // registered with a NEW (non-shared) AudioEngine instance, since the test disposes it.
    std::vector<uint8_t> BuildXA8XwbFixtureBytes()
    {
        constexpr uint32_t headerSize        = 48;
        constexpr uint32_t bankDataSize      = 96;
        constexpr uint32_t entryCount        = 1;
        constexpr uint32_t entryMetaDataSize = 4;
        constexpr uint32_t entryMetaSegSize  = entryCount * entryMetaDataSize;
        constexpr uint32_t waveDataLength    = 200;
        constexpr uint32_t alignment         = 4;

        const uint32_t segOffset[5] = {
            headerSize,
            headerSize + bankDataSize,
            headerSize + bankDataSize + entryMetaSegSize,
            headerSize + bankDataSize + entryMetaSegSize,
            headerSize + bankDataSize + entryMetaSegSize,
        };
        const uint32_t segLength[5] = { bankDataSize, entryMetaSegSize, 0, 0, waveDataLength };

        std::vector<uint8_t> data;
        const char magic[4] = { 'W', 'B', 'N', 'D' };
        data.insert(data.end(), magic, magic + 4);
        AppendU32(data, 1); // version
        for (int i = 0; i < 5; ++i)
        {
            AppendU32(data, segOffset[i]);
            AppendU32(data, segLength[i]);
        }

        AppendU32(data, 0x00020000u); // wbFlags: COMPACT only, no names
        AppendU32(data, entryCount);
        AppendPadded(data, kXA8WaveBankName, 64);
        AppendU32(data, entryMetaDataSize);
        AppendU32(data, 0); // entryNameElementSize
        AppendU32(data, alignment);
        const uint32_t compactFormat =
              (0u)            // format tag: PCM
            | (1u << 2)       // channels: mono
            | (44100u << 5)   // sample rate
            | (2u << 23)      // wBlockAlign: 2 bytes/sample
            | (1u << 31);     // 16-bit
        AppendU32(data, compactFormat);
        for (int i = 0; i < 8; ++i) data.push_back(0); // buildTime

        AppendU32(data, 0u); // entry 0: offset=0, deviation=0 (last/only entry)

        for (uint32_t i = 0; i < waveDataLength; ++i)
            data.push_back(0x00); // 16-bit silence

        return data;
    }

    // Minimal .xsb with one wavebank reference, one simple sound (categoryIndex=0), and one
    // simple cue "XA8Cue" playing that sound.
    std::vector<uint8_t> BuildXA8XsbFixtureBytes()
    {
        constexpr uint32_t headerSize   = 74;
        constexpr uint32_t bankNameSize = 64;
        constexpr uint32_t baseOffset   = headerSize + bankNameSize;

        const uint32_t wavebankNameOffset = baseOffset;
        const uint32_t soundOffset        = wavebankNameOffset + 64;
        const uint32_t cueSimpleOffset    = soundOffset + 12;
        const uint32_t cueNameIndexOffset = cueSimpleOffset + 5;
        const uint32_t cueNameStrOffset   = cueNameIndexOffset + 6;
        const std::string cueName = "XA8Cue";

        std::vector<uint8_t> data;
        const char magic[4] = { 'S', 'D', 'B', 'K' };
        data.insert(data.end(), magic, magic + 4);
        AppendU16(data, 46); // contentVersion
        AppendU16(data, 0);  // toolVersion
        AppendU16(data, 0);  // CRC
        for (int i = 0; i < 8; ++i) data.push_back(0); // lastModified
        AppendU8(data, 0);   // platform

        AppendU16(data, 1); // cueSimpleCount
        AppendU16(data, 0); // cueComplexCount
        AppendU16(data, 0); // unknown
        AppendU16(data, 0); // cueTotalAlign
        AppendU8(data, 1);  // wavebankCount
        AppendU16(data, 1); // soundCount
        AppendU16(data, 0); // cueNameLength
        AppendU16(data, 0); // unknown

        AppendS32(data, static_cast<int32_t>(cueSimpleOffset));
        AppendS32(data, -1); // cueComplexOffset
        AppendS32(data, -1); // cueNameOffset (unused by the parser)
        AppendS32(data, 0);  // unknown
        AppendS32(data, -1); // variationOffset
        AppendS32(data, 0);  // transitionOffset (unused)
        AppendS32(data, static_cast<int32_t>(wavebankNameOffset));
        AppendS32(data, 0);  // cueHashOffset (unused)
        AppendS32(data, static_cast<int32_t>(cueNameIndexOffset));
        AppendS32(data, static_cast<int32_t>(soundOffset));

        AppendPadded(data, "XA8SoundBank", bankNameSize);
        AppendPadded(data, kXA8WaveBankName, 64);

        AppendU8(data, 0);    // flags
        AppendU16(data, 0);   // categoryIndex
        AppendU8(data, 0xFF); // volume raw byte
        AppendU16(data, 0);   // pitchCents
        AppendU8(data, 0);    // priority
        AppendU16(data, 0);   // soundLength (skipped)
        AppendU16(data, 0);   // waveIdx
        AppendU8(data, 0);    // wbIdx

        AppendU8(data, 0);
        AppendU32(data, soundOffset);

        AppendU32(data, cueNameStrOffset);
        AppendU16(data, 0);

        AppendCStr(data, cueName);

        return data;
    }
}

// ===================== ContentVersion =====================

TEST(AudioEngineTest, ContentVersionIs46)
{
    EXPECT_EQ(AudioEngine::ContentVersion, 46);
}

// ===================== Constructors =====================

TEST(AudioEngineTest, SingleArgConstructorEmptyPathThrowsArgumentNull)
{
    EXPECT_THROW(AudioEngine engine(""), System::ArgumentNullException);
}

TEST(AudioEngineTest, TwoArgConstructorEmptyPathThrowsArgumentNull)
{
    EXPECT_THROW(AudioEngine engine("", System::TimeSpan::Zero, "renderer"), System::ArgumentNullException);
}

TEST(AudioEngineTest, SingleArgConstructorLoadsFixture)
{
    AudioEngine engine(XgsFixturePath());
    EXPECT_FALSE(engine.getIsDisposedProperty());
}

TEST(AudioEngineTest, TwoArgConstructorLoadsFixtureWithRendererAndLookAhead)
{
    AudioEngine engine(XgsFixturePath(), System::TimeSpan::Zero, "SDL3_mixer");
    EXPECT_FALSE(engine.getIsDisposedProperty());
}

TEST(AudioEngineTest, ThreeArgConstructorWithArbitraryRendererAndLookAheadBehavesLikeSingleArg)
{
    // lookAheadTime/rendererId have no effect (plan_audio.md XA-4): even a nonzero look-ahead
    // and a renderer ID that doesn't name any real backend must not throw, and the resulting
    // engine must behave identically to the single-argument constructor.
    AudioEngine engine(XgsFixturePath(),
                       System::TimeSpan::FromMilliseconds(500),
                       "TotallyBogusRendererThatDoesNotExist");

    EXPECT_FALSE(engine.getIsDisposedProperty());
    EXPECT_FALSE(engine.getRendererDetailsProperty().empty());
    EXPECT_NO_THROW((void)engine.GetCategory("Default"));
}

// ===================== IsDisposed / Dispose =====================

TEST(AudioEngineTest, IsDisposedFalseInitiallyAndTrueAfterDispose)
{
    AudioEngine engine(XgsFixturePath());
    EXPECT_FALSE(engine.getIsDisposedProperty());
    engine.Dispose();
    EXPECT_TRUE(engine.getIsDisposedProperty());
}

TEST(AudioEngineTest, DisposeIsIdempotent)
{
    AudioEngine engine(XgsFixturePath());
    engine.Dispose();
    EXPECT_NO_THROW(engine.Dispose());
    EXPECT_TRUE(engine.getIsDisposedProperty());
}

TEST(AudioEngineTest, DisposeRaisesDisposingEvent)
{
    AudioEngine engine(XgsFixturePath());
    bool raised = false;
    engine.Disposing += [&raised](System::Object*, const System::EventArgs&) { raised = true; };
    engine.Dispose();
    EXPECT_TRUE(raised);
}

// XA-8: AudioEngine::Dispose() used to only reset its own xactImpl_ -- every WaveBank/SoundBank/
// Cue it had created stayed IsDisposed==false forever, unlike FNA's native OnXACTNotification
// (WAVEBANKDESTROYED/SOUNDBANKDESTROYED/CUEDESTROYED), which immediately flips IsDisposed on
// every dependent wrapper. Uses a dedicated (non-shared) AudioEngine since this test disposes it.
TEST(AudioEngineTest, DisposeCascadesToWaveBankSoundBankAndCue)
{
    AudioEngine engine(XgsFixturePath());

    WaveBank wb(&engine, WriteFixture("xa8.xwb", BuildXA8XwbFixtureBytes()));
    ASSERT_TRUE(wb.getIsPreparedProperty());
    SoundBank sb(&engine, WriteFixture("xa8.xsb", BuildXA8XsbFixtureBytes()));

    std::unique_ptr<Cue> cue(sb.GetCue("XA8Cue"));
    cue->Play(); // registers with the engine's active-cue list

    ASSERT_FALSE(wb.getIsDisposedProperty());
    ASSERT_FALSE(sb.getIsDisposedProperty());
    ASSERT_FALSE(cue->getIsDisposedProperty());

    engine.Dispose();

    EXPECT_TRUE(wb.getIsDisposedProperty());
    EXPECT_TRUE(sb.getIsDisposedProperty());
    EXPECT_TRUE(cue->getIsDisposedProperty());
}

// ===================== RendererDetails =====================

TEST(AudioEngineTest, RendererDetailsNonEmpty)
{
    AudioEngine engine(XgsFixturePath());
    EXPECT_FALSE(engine.getRendererDetailsProperty().empty());
}

// ===================== GetCategory =====================

TEST(AudioEngineTest, GetCategoryValidReturnsMatchingName)
{
    AudioEngine engine(XgsFixturePath());
    const AudioCategory cat = engine.GetCategory("Default");
    EXPECT_EQ(cat.getNameProperty(), "Default");
}

TEST(AudioEngineTest, GetCategoryEmptyNameThrowsArgumentNull)
{
    AudioEngine engine(XgsFixturePath());
    EXPECT_THROW((void)engine.GetCategory(""), System::ArgumentNullException);
}

TEST(AudioEngineTest, GetCategoryInvalidNameThrowsInvalidOperation)
{
    AudioEngine engine(XgsFixturePath());
    EXPECT_THROW((void)engine.GetCategory("NoSuchCategory"), System::InvalidOperationException);
}

TEST(AudioEngineTest, GetCategoryAfterDisposeThrowsObjectDisposed)
{
    AudioEngine engine(XgsFixturePath());
    engine.Dispose();
    EXPECT_THROW((void)engine.GetCategory("Default"), System::ObjectDisposedException);
}

// XA-13: a file that EXISTS but isn't a valid .xgs (bad magic/garbage) must behave the same as
// a missing one -- construction doesn't throw, GetCategory() on any name then throws
// InvalidOperationException (matching GetCategoryInvalidNameThrowsInvalidOperation's error), not
// some other unhandled exception. (XA-9 decided to keep this "silent stub" behavior rather than
// throw; this locks in that decision explicitly, on the "exists but corrupt" path specifically.)
TEST(AudioEngineTest, ConstructorWithExistingButCorruptFileStaysInStubState)
{
    const auto corrupt = WriteFixture(
        "corrupt.xgs", {'n', 'o', 't', ' ', 'a', ' ', 's', 'e', 't', 't', 'i', 'n', 'g', 's'});
    AudioEngine engine(corrupt);
    EXPECT_FALSE(engine.getIsDisposedProperty());
    EXPECT_THROW((void)engine.GetCategory("Default"), System::InvalidOperationException);
}

// ===================== GetGlobalVariable =====================

TEST(AudioEngineTest, GetGlobalVariableValidReturnsInitialValue)
{
    AudioEngine engine(XgsFixturePath());
    EXPECT_FLOAT_EQ(engine.GetGlobalVariable("Volume"), 0.5f);
}

TEST(AudioEngineTest, GetGlobalVariableEmptyNameThrowsArgumentNull)
{
    AudioEngine engine(XgsFixturePath());
    EXPECT_THROW((void)engine.GetGlobalVariable(""), System::ArgumentNullException);
}

TEST(AudioEngineTest, GetGlobalVariableInvalidNameThrowsInvalidOperation)
{
    AudioEngine engine(XgsFixturePath());
    EXPECT_THROW((void)engine.GetGlobalVariable("NoSuchVariable"), System::InvalidOperationException);
}

TEST(AudioEngineTest, GetGlobalVariableAfterDisposeThrowsObjectDisposed)
{
    AudioEngine engine(XgsFixturePath());
    engine.Dispose();
    EXPECT_THROW((void)engine.GetGlobalVariable("Volume"), System::ObjectDisposedException);
}

// ===================== SetGlobalVariable =====================

TEST(AudioEngineTest, SetGlobalVariableValidUpdatesValue)
{
    AudioEngine engine(XgsFixturePath());
    engine.SetGlobalVariable("Volume", 0.75f);
    EXPECT_FLOAT_EQ(engine.GetGlobalVariable("Volume"), 0.75f);
}

TEST(AudioEngineTest, SetGlobalVariableEmptyNameThrowsArgumentNull)
{
    AudioEngine engine(XgsFixturePath());
    EXPECT_THROW(engine.SetGlobalVariable("", 1.0f), System::ArgumentNullException);
}

TEST(AudioEngineTest, SetGlobalVariableInvalidNameThrowsInvalidOperation)
{
    AudioEngine engine(XgsFixturePath());
    EXPECT_THROW(engine.SetGlobalVariable("NoSuchVariable", 1.0f), System::InvalidOperationException);
}

TEST(AudioEngineTest, SetGlobalVariableAfterDisposeThrowsObjectDisposed)
{
    AudioEngine engine(XgsFixturePath());
    engine.Dispose();
    EXPECT_THROW(engine.SetGlobalVariable("Volume", 1.0f), System::ObjectDisposedException);
}

// ===================== Update =====================

TEST(AudioEngineTest, UpdateDoesNotThrow)
{
    AudioEngine engine(XgsFixturePath());
    EXPECT_NO_THROW(engine.Update());
}

// ===================== GetTypeName =====================

TEST(AudioEngineTest, GetTypeNameIsDottedXnaName)
{
    AudioEngine engine(XgsFixturePath());
    EXPECT_EQ(engine.GetTypeName(), "Microsoft.Xna.Framework.Audio.AudioEngine");
}
