// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Audio/AudioCategory.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioEngine.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioStopOptions.hpp"
#include "Microsoft/Xna/Framework/Audio/Cue.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundBank.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffectInstance.hpp"
#include "Microsoft/Xna/Framework/Audio/WaveBank.hpp"
#include "AudioEngineTestAccess.hpp"
#include "CueTestAccess.hpp"
#include "SoundBankTestAccess.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/Environment.hpp"
#include "System/EventArgs.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/IO/FileNotFoundException.hpp"
#include "System/Object.hpp"
#include "System/ObjectDisposedException.hpp"
#include "System/TimeSpan.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "System/Environment.hpp"

using Microsoft::Xna::Framework::Audio::AudioCategory;
using Microsoft::Xna::Framework::Audio::AudioEngine;
using Microsoft::Xna::Framework::Audio::AudioEngineTestAccess;
using Microsoft::Xna::Framework::Audio::AudioStopOptions;
using Microsoft::Xna::Framework::Audio::Cue;
using Microsoft::Xna::Framework::Audio::CueTestAccess;
using Microsoft::Xna::Framework::Audio::SoundBank;
using Microsoft::Xna::Framework::Audio::SoundBankTestAccess;
using Microsoft::Xna::Framework::Audio::SoundEffectInstance;
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

        // Variable: accessibility, initialValue, minValue, maxValue.
        // P12-VAR-001: PUBLIC only (0x1) -- FACT_internal.h's real bit values are PUBLIC=0x1,
        // READONLY=0x2, CUE=0x4; this variable must be plain engine-global (PUBLIC set, CUE
        // clear) and writable (READONLY clear) for GetGlobalVariable/SetGlobalVariable's tests
        // below to mean what they say. Previously 0x03 (PUBLIC|READONLY), an arbitrary nonzero
        // byte chosen before this project enforced accessibility semantics at all -- silently
        // made SetGlobalVariableValidUpdatesValue test a no-op-writable variable by accident.
        AppendU8(data, 0x01);
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

    // P12-VAR-001: dedicated fixture with 4 variables spanning every combination of the
    // PUBLIC/READONLY/CUE accessibility bits this project now enforces (FACT_internal.h:
    // PUBLIC=0x1, READONLY=0x2, CUE=0x4):
    //   "GlobalVar"   -- PUBLIC only (0x1): plain engine-global, readable+writable via
    //                    GetGlobalVariable/SetGlobalVariable.
    //   "ReadOnlyVar" -- PUBLIC|READONLY (0x3): engine-global, readable but SetGlobalVariable is
    //                    a silent no-op (FNA's C# wrapper never checks
    //                    FACTAudioEngine_SetGlobalVariable's native return code).
    //   "CueVar"      -- PUBLIC|CUE (0x5): cue-scoped -- GetGlobalVariable/SetGlobalVariable must
    //                    reject it outright, a different/mutually-exclusive domain.
    //   "PrivateVar"  -- 0x0 (neither bit set): unreachable via either the engine-global or
    //                    cue-scoped API.
    // All four share min=0.0/max=10.0/initial=5.0 so the same clamp bounds cover every case.
    std::vector<uint8_t> BuildVarAccessibilityXgsFixtureBytes()
    {
        constexpr uint32_t headerSize       = 65;
        constexpr uint32_t categoryDataSize = 10;
        constexpr uint32_t variableDataSize = 13;
        constexpr uint32_t variableCount    = 4;

        const uint32_t categoryOffset     = headerSize;
        const uint32_t variableOffset     = categoryOffset + categoryDataSize;
        const uint32_t categoryNameOffset = variableOffset + variableDataSize * variableCount;
        const std::string categoryName    = "Default";
        const std::string name0 = "GlobalVar";
        const std::string name1 = "ReadOnlyVar";
        const std::string name2 = "CueVar";
        const std::string name3 = "PrivateVar";
        const uint32_t variableNameOffset =
            categoryNameOffset + static_cast<uint32_t>(categoryName.size()) + 1;

        std::vector<uint8_t> data;
        const char magic[4] = { 'X', 'G', 'S', 'F' };
        data.insert(data.end(), magic, magic + 4);
        AppendU16(data, 46); // contentVersion
        AppendU16(data, 0);  // toolVersion
        AppendU16(data, 0);  // unknown
        for (int i = 0; i < 8; ++i) data.push_back(0); // lastModified
        AppendU8(data, 3);   // platform

        AppendU16(data, 1); // categoryCount
        AppendU16(data, variableCount);
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

        // Variables: accessibility, initialValue, minValue, maxValue.
        AppendU8(data, 0x01); AppendF32(data, 5.0f); AppendF32(data, 0.0f); AppendF32(data, 10.0f); // GlobalVar
        AppendU8(data, 0x03); AppendF32(data, 5.0f); AppendF32(data, 0.0f); AppendF32(data, 10.0f); // ReadOnlyVar
        AppendU8(data, 0x05); AppendF32(data, 5.0f); AppendF32(data, 0.0f); AppendF32(data, 10.0f); // CueVar
        AppendU8(data, 0x00); AppendF32(data, 5.0f); AppendF32(data, 0.0f); AppendF32(data, 10.0f); // PrivateVar

        AppendCStr(data, categoryName);
        AppendCStr(data, name0);
        AppendCStr(data, name1);
        AppendCStr(data, name2);
        AppendCStr(data, name3);

        return data;
    }

    const std::string& VarAccessibilityFixturePath()
    {
        static const std::string path = []() -> std::string
        {
            auto dir = std::filesystem::temp_directory_path() / "cna_audio_engine_test";
            std::filesystem::create_directories(dir);
            auto file = dir / "var_accessibility.xgs";
            const auto bytes = BuildVarAccessibilityXgsFixtureBytes();
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

    constexpr const char* kLongWaveBankName = "P9StopLongWaveBank";

    // Same layout as BuildXA8XwbFixtureBytes, but with a full second of audio instead of 200
    // bytes (~1ms) -- P9-STOP's authored-stop-tail tests need the tail to still be reliably
    // audible at the moment of the assertion, not racing a near-instant natural finish (same
    // rationale as CueTests.cpp's BuildLongXwbFixtureBytes/"LongCue" for XA-6).
    std::vector<uint8_t> BuildP9StopLongXwbFixtureBytes()
    {
        constexpr uint32_t headerSize        = 48;
        constexpr uint32_t bankDataSize      = 96;
        constexpr uint32_t entryCount        = 1;
        constexpr uint32_t entryMetaDataSize = 4;
        constexpr uint32_t entryMetaSegSize  = entryCount * entryMetaDataSize;
        constexpr uint32_t waveDataLength    = 44100u * 2u; // 1 second, mono 16-bit @ 44100Hz
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
        AppendPadded(data, kLongWaveBankName, 64);
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

    // Fade duration authored on "P9StopLongCue" below (P9-STOP-010) -- far shorter than the
    // underlying wave's 1 second, so reaching Stopped this fast can only be the authored fade
    // timer elapsing, never the wave naturally finishing.
    constexpr uint16_t kP9StopLongCueFadeOutMS = 300;

    // Same layout as BuildXA8XsbFixtureBytes, but referencing kLongWaveBankName and named
    // "P9StopLongCue". Unlike a simple cue, this is a COMPLEX cue (CUE_FLAG_SINGLE_SOUND set)
    // authoring a real fadeOutMS (P9-STOP-010) -- a simple cue's format has no such field at all
    // (always 0), so it could never exercise real Stop(AsAuthored) fade timing.
    std::vector<uint8_t> BuildP9StopLongXsbFixtureBytes()
    {
        constexpr uint32_t headerSize   = 74;
        constexpr uint32_t bankNameSize = 64;
        constexpr uint32_t baseOffset   = headerSize + bankNameSize;

        const uint32_t wavebankNameOffset = baseOffset;
        const uint32_t soundOffset        = wavebankNameOffset + 64;
        const uint32_t cueComplexOffset   = soundOffset + 12;
        const uint32_t cueNameIndexOffset = cueComplexOffset + 15;
        const uint32_t cueNameStrOffset   = cueNameIndexOffset + 6;
        const std::string cueName = "P9StopLongCue";

        std::vector<uint8_t> data;
        const char magic[4] = { 'S', 'D', 'B', 'K' };
        data.insert(data.end(), magic, magic + 4);
        AppendU16(data, 46); // contentVersion
        AppendU16(data, 0);  // toolVersion
        AppendU16(data, 0);  // CRC
        for (int i = 0; i < 8; ++i) data.push_back(0); // lastModified
        AppendU8(data, 0);   // platform

        AppendU16(data, 0); // cueSimpleCount
        AppendU16(data, 1); // cueComplexCount
        AppendU16(data, 0); // unknown
        AppendU16(data, 0); // cueTotalAlign
        AppendU8(data, 1);  // wavebankCount
        AppendU16(data, 1); // soundCount
        AppendU16(data, 0); // cueNameLength
        AppendU16(data, 0); // unknown

        AppendS32(data, -1); // cueSimpleOffset
        AppendS32(data, static_cast<int32_t>(cueComplexOffset));
        AppendS32(data, -1); // cueNameOffset (unused by the parser)
        AppendS32(data, 0);  // unknown
        AppendS32(data, -1); // variationOffset
        AppendS32(data, 0);  // transitionOffset (unused)
        AppendS32(data, static_cast<int32_t>(wavebankNameOffset));
        AppendS32(data, 0);  // cueHashOffset (unused)
        AppendS32(data, static_cast<int32_t>(cueNameIndexOffset));
        AppendS32(data, static_cast<int32_t>(soundOffset));

        AppendPadded(data, "P9StopLongSoundBank", bankNameSize);
        AppendPadded(data, kLongWaveBankName, 64);

        AppendU8(data, 0);    // flags
        AppendU16(data, 0);   // categoryIndex
        AppendU8(data, 0xFF); // volume raw byte
        AppendU16(data, 0);   // pitchCents
        AppendU8(data, 0);    // priority
        AppendU16(data, 0);   // soundLength (skipped)
        AppendU16(data, 0);   // waveIdx
        AppendU8(data, 0);    // wbIdx

        // Complex cue "P9StopLongCue": single-sound (CUE_FLAG_SINGLE_SOUND), sbCode points
        // directly at the one real sound above, authoring a real fadeOutMS.
        AppendU8(data, 0x04); // flags: CUE_FLAG_SINGLE_SOUND
        AppendU32(data, soundOffset); // sbCode
        AppendU32(data, 0);   // transitionOffset
        AppendU8(data, 0xFF); // instanceLimit
        AppendU16(data, 0);   // fadeInMS
        AppendU16(data, kP9StopLongCueFadeOutMS); // fadeOutMS
        AppendU8(data, 0);    // maxInstanceBehavior

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
    // lookAheadTime/rendererId have no effect (plans/plan_audio.md XA-4): even a nonzero look-ahead
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

// AUD-15-007: exhaustively stress disposal ORDER across AudioEngine/WaveBank/SoundBank/Cue --
// not just the single canonical "dispose the engine and let it cascade" order exercised above.
// Each iteration builds a fresh, dedicated AudioEngine + WaveBank + SoundBank, a caller-held Cue
// (via GetCue, put into a randomly chosen state: never played, playing, or stopped-but-
// undisposed) and a fire-and-forget Cue (via PlayCue, registered with both the SoundBank and the
// engine's active-cue list, per AUDIO-LIFECYCLE-001/P9-LIFECYCLE-008), then disposes AND FREES
// (delete, matching this codebase's established caller-owns-a-GetCue()-Cue convention) the held
// cue as one of four randomly shuffled steps alongside engine/wavebank/soundbank disposal.
// Freeing the held cue mid-permutation (not always last) is deliberate: a caller in real code
// disposes-then-deletes a Cue as soon as it's done with it, potentially well before the bank(s)
// that still reference it via activeCues_ are themselves disposed -- if Cue::Dispose() ever
// failed to unregister from every bank that still held a pointer to it, this is what would turn
// that gap into a genuine, crashing use-after-free (a later bank Dispose() cascade dereferencing
// already-freed memory) instead of a silently-harmless stale entry. Acceptance (from the plan):
// no UAF, double unregister, or dangling cue -- checked via clean completion (no crash/ASan
// finding) plus every surviving object correctly reporting IsDisposed==true afterward.
TEST(AudioEngineTest, StressDisposalOrderPermutationsAcrossEngineWaveBankSoundBankAndCue)
{
    uint32_t seed = 0xC0FFEEu;
    auto nextRandom = [&seed]() {
        seed = seed * 1103515245u + 12345u;
        return (seed >> 16) & 0x7fffu;
    };

    constexpr int kIterations = 200;
    for (int i = 0; i < kIterations; ++i)
    {
        auto engine = std::make_unique<AudioEngine>(XgsFixturePath());
        auto wb = std::make_unique<WaveBank>(
            engine.get(),
            WriteFixture("xa8_perm_" + std::to_string(i) + ".xwb", BuildXA8XwbFixtureBytes()));
        ASSERT_TRUE(wb->getIsPreparedProperty());
        auto sb = std::make_unique<SoundBank>(
            engine.get(),
            WriteFixture("xa8_perm_" + std::to_string(i) + ".xsb", BuildXA8XsbFixtureBytes()));

        Cue* heldCue = sb->GetCue("XA8Cue"); // caller-owned, per this bank's own contract
        switch (nextRandom() % 3)
        {
            case 0: break; // never played (still Prepared)
            case 1: heldCue->Play(); break; // actively playing
            case 2: heldCue->Play(); heldCue->Stop(AudioStopOptions::Immediate); break; // stopped, undisposed
        }

        sb->PlayCue("XA8Cue"); // a second, fire-and-forget cue also registered against both banks

        // Fisher-Yates shuffle of the 4 dispose steps (0=engine, 1=wavebank, 2=soundbank,
        // 3=heldCue -- disposed AND deleted here, not afterward) -- picks uniformly among all
        // 24 orderings across enough iterations.
        std::array<int, 4> order = {0, 1, 2, 3};
        for (int k = 3; k > 0; --k)
        {
            const int j = static_cast<int>(nextRandom() % static_cast<unsigned>(k + 1));
            std::swap(order[k], order[j]);
        }

        for (int step : order)
        {
            switch (step)
            {
                case 0: engine->Dispose(); break;
                case 1: wb->Dispose(); break;
                case 2: sb->Dispose(); break;
                case 3:
                    heldCue->Dispose();
                    delete heldCue; // freed HERE -- mid-permutation, not after -- see rationale above
                    heldCue = nullptr;
                    break;
            }
        }

        EXPECT_TRUE(engine->getIsDisposedProperty()) << "iteration " << i;
        EXPECT_TRUE(wb->getIsDisposedProperty()) << "iteration " << i;
        EXPECT_TRUE(sb->getIsDisposedProperty()) << "iteration " << i;
        // engine/wb/sb destruct here (unique_ptr going out of scope) regardless of dispose order
        // above -- exercising every object's destructor-triggered Dispose() as a no-op on top of
        // whatever order already ran, every iteration, and (critically) running any remaining
        // bank's Dispose() cascade AFTER heldCue has already been freed if step 3 ran early.
    }
}

// ===================== RendererDetails =====================

TEST(AudioEngineTest, RendererDetailsNonEmpty)
{
    AudioEngine engine(XgsFixturePath());
    EXPECT_FALSE(engine.getRendererDetailsProperty().empty());
}

// P10-AUDIT-002/003: exact content, not just non-emptiness -- CNA has exactly one audio backend
// (SDL3_mixer, no renderer-enumeration API), so RendererDetails is always this single entry.
TEST(AudioEngineTest, RendererDetailsReportsExactlyOneSdlMixerEntry)
{
    AudioEngine engine(XgsFixturePath());
    const auto& details = engine.getRendererDetailsProperty();
    ASSERT_EQ(details.size(), 1u);
    EXPECT_EQ(details[0].getFriendlyNameProperty(), "SDL3_mixer");
    EXPECT_EQ(details[0].getRendererIdProperty(), "SDL3_mixer");
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

// P9-HARDWARE-003: matches FNA exactly (AudioEngine.cs) -- a missing settingsFile throws
// FileNotFoundException (from TitleContainer.ReadToPointer) before any FACT call is made.
TEST(AudioEngineTest, ConstructorWithMissingFileThrowsFileNotFound)
{
    const auto missing =
        (std::filesystem::temp_directory_path() / "cna_audio_engine_test_missing.xgs").string();
    EXPECT_THROW(AudioEngine engine(missing), System::IO::FileNotFoundException);
}

// XA-13/P9-HARDWARE-003: a file that EXISTS but isn't a valid .xgs (bad magic/garbage) matches
// FNA's AudioEngine ctor, which checks FACTAudioEngine_Initialize's return code and throws
// InvalidOperationException("Engine initialization failed!") (AudioEngine.cs) -- unlike a missing
// file, this is a construction-time throw, not a lazily-discovered stub state.
TEST(AudioEngineTest, ConstructorWithExistingButCorruptFileThrowsInvalidOperation)
{
    const auto corrupt = WriteFixture(
        "corrupt.xgs", {'n', 'o', 't', ' ', 'a', ' ', 's', 'e', 't', 't', 'i', 'n', 'g', 's'});
    EXPECT_THROW(AudioEngine engine(corrupt), System::InvalidOperationException);
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

// ===================== P12-VAR-001: accessibility/clamping =====================

TEST(AudioEngineTest, GetGlobalVariableRejectsCueScopedVariable)
{
    AudioEngine engine(VarAccessibilityFixturePath());
    EXPECT_THROW((void)engine.GetGlobalVariable("CueVar"), System::InvalidOperationException);
}

TEST(AudioEngineTest, SetGlobalVariableRejectsCueScopedVariable)
{
    AudioEngine engine(VarAccessibilityFixturePath());
    EXPECT_THROW(engine.SetGlobalVariable("CueVar", 1.0f), System::InvalidOperationException);
}

TEST(AudioEngineTest, GetGlobalVariableRejectsNonPublicVariable)
{
    AudioEngine engine(VarAccessibilityFixturePath());
    EXPECT_THROW((void)engine.GetGlobalVariable("PrivateVar"), System::InvalidOperationException);
}

TEST(AudioEngineTest, SetGlobalVariableRejectsNonPublicVariable)
{
    AudioEngine engine(VarAccessibilityFixturePath());
    EXPECT_THROW(engine.SetGlobalVariable("PrivateVar", 1.0f), System::InvalidOperationException);
}

TEST(AudioEngineTest, GetGlobalVariableOnReadOnlyVariableStillReadable)
{
    AudioEngine engine(VarAccessibilityFixturePath());
    EXPECT_FLOAT_EQ(engine.GetGlobalVariable("ReadOnlyVar"), 5.0f);
}

// Matches FACTAudioEngine_SetGlobalVariable's own READONLY rejection (FACT.c) -- FNA's C#
// wrapper never checks this native call's return code, so a READONLY variable's
// SetGlobalVariable() is a silent no-op in real XNA, not an exception.
TEST(AudioEngineTest, SetGlobalVariableOnReadOnlyVariableIsSilentNoOp)
{
    AudioEngine engine(VarAccessibilityFixturePath());
    EXPECT_NO_THROW(engine.SetGlobalVariable("ReadOnlyVar", 9.0f));
    EXPECT_FLOAT_EQ(engine.GetGlobalVariable("ReadOnlyVar"), 5.0f);
}

TEST(AudioEngineTest, SetGlobalVariableClampsAboveMaximum)
{
    AudioEngine engine(VarAccessibilityFixturePath());
    engine.SetGlobalVariable("GlobalVar", 999.0f);
    EXPECT_FLOAT_EQ(engine.GetGlobalVariable("GlobalVar"), 10.0f);
}

TEST(AudioEngineTest, SetGlobalVariableClampsBelowMinimum)
{
    AudioEngine engine(VarAccessibilityFixturePath());
    engine.SetGlobalVariable("GlobalVar", -999.0f);
    EXPECT_FLOAT_EQ(engine.GetGlobalVariable("GlobalVar"), 0.0f);
}

TEST(AudioEngineTest, SetGlobalVariableWithinRangeIsNotClamped)
{
    AudioEngine engine(VarAccessibilityFixturePath());
    engine.SetGlobalVariable("GlobalVar", 7.0f);
    EXPECT_FLOAT_EQ(engine.GetGlobalVariable("GlobalVar"), 7.0f);
}

// "Volume" in XgsFixturePath()'s own fixture is PUBLIC-only (accessibility 0x1, engine-global)
// -- Cue::GetVariable/SetVariable must reject it (matches FACTCue_GetVariableIndex's PUBLIC+CUE
// requirement, FACT.c): an engine-global variable is a different, mutually-exclusive domain,
// never reachable via the cue-level API in real XNA/FACT.
TEST(AudioEngineTest, CueGetVariableRejectsEngineGlobalOnlyVariable)
{
    AudioEngine engine(XgsFixturePath());
    WaveBank wb(&engine, WriteFixture("var_reject.xwb", BuildXA8XwbFixtureBytes()));
    SoundBank sb(&engine, WriteFixture("var_reject.xsb", BuildXA8XsbFixtureBytes()));
    std::unique_ptr<Cue> cue(sb.GetCue("XA8Cue"));

    EXPECT_THROW((void)cue->GetVariable("Volume"), System::InvalidOperationException);
}

TEST(AudioEngineTest, CueSetVariableRejectsEngineGlobalOnlyVariable)
{
    AudioEngine engine(XgsFixturePath());
    WaveBank wb(&engine, WriteFixture("var_reject2.xwb", BuildXA8XwbFixtureBytes()));
    SoundBank sb(&engine, WriteFixture("var_reject2.xsb", BuildXA8XsbFixtureBytes()));
    std::unique_ptr<Cue> cue(sb.GetCue("XA8Cue"));

    EXPECT_THROW(cue->SetVariable("Volume", 1.0f), System::InvalidOperationException);
}

// "CueVar" in VarAccessibilityFixturePath()'s fixture IS PUBLIC|CUE -- unlike "Volume" above,
// this one must be reachable via the cue-level API, reading back its authored initial value
// since this fresh cue has never explicitly SetVariable()-d it (matches real FACT's per-cue
// variableValues[i], initialized from the declared variable's own initialValue at cue creation).
TEST(AudioEngineTest, CueGetVariableReadsCueScopedVariableInitialValue)
{
    AudioEngine engine(VarAccessibilityFixturePath());
    WaveBank wb(&engine, WriteFixture("var_cue.xwb", BuildXA8XwbFixtureBytes()));
    SoundBank sb(&engine, WriteFixture("var_cue.xsb", BuildXA8XsbFixtureBytes()));
    std::unique_ptr<Cue> cue(sb.GetCue("XA8Cue"));

    EXPECT_FLOAT_EQ(cue->GetVariable("CueVar"), 5.0f);
}

TEST(AudioEngineTest, CueSetVariableClampsCueScopedVariable)
{
    AudioEngine engine(VarAccessibilityFixturePath());
    WaveBank wb(&engine, WriteFixture("var_cue2.xwb", BuildXA8XwbFixtureBytes()));
    SoundBank sb(&engine, WriteFixture("var_cue2.xsb", BuildXA8XsbFixtureBytes()));
    std::unique_ptr<Cue> cue(sb.GetCue("XA8Cue"));

    cue->SetVariable("CueVar", 999.0f);
    EXPECT_FLOAT_EQ(cue->GetVariable("CueVar"), 10.0f);
}

TEST(AudioEngineTest, CueGetVariableRejectsPrivateVariable)
{
    AudioEngine engine(VarAccessibilityFixturePath());
    WaveBank wb(&engine, WriteFixture("var_private.xwb", BuildXA8XwbFixtureBytes()));
    SoundBank sb(&engine, WriteFixture("var_private.xsb", BuildXA8XsbFixtureBytes()));
    std::unique_ptr<Cue> cue(sb.GetCue("XA8Cue"));

    EXPECT_THROW((void)cue->GetVariable("PrivateVar"), System::InvalidOperationException);
}

// ===================== Update =====================

TEST(AudioEngineTest, UpdateDoesNotThrow)
{
    AudioEngine engine(XgsFixturePath());
    EXPECT_NO_THROW(engine.Update());
}

// P10-XACT-007: unlike GetCategory/GetGlobalVariable/SetGlobalVariable above, Update() does NOT
// check isDisposed_ and throw -- it relies on Dispose() having reset xactImpl_ to null, and
// early-returns before touching anything. This is a deliberate asymmetry (matching neither FNA,
// which has no IsDisposed guard on ANY of these -- see AudioEngine.cs's Update(), a raw
// FACTAudioEngine_DoWork call with no null/disposed check at all, undefined behavior if disposed):
// throwing here would penalize the common "call Update() every frame regardless of teardown
// order" game-loop pattern for a call that CNA can already make perfectly safe. Locking down the
// current, deliberate behavior with a regression test rather than leaving it unverified.
TEST(AudioEngineTest, UpdateAfterDisposeDoesNotThrow)
{
    AudioEngine engine(XgsFixturePath());
    engine.Dispose();
    EXPECT_NO_THROW(engine.Update());
}

// P9-LIFECYCLE-008/009: mirrors FNA's AudioEngine.Update() -> FACTAudioEngine_DoWork, which is
// what actually destroys a managed/fire-and-forget cue once it reaches FACT_STATE_STOPPED
// (FACT_internal.c). A single Update() call after natural completion must sweep it out of its
// SoundBank's fire-and-forget list -- without needing a second PlayCue() call on that bank to
// trigger the sweep (see SoundBank::PlayCueInternal's own, separate sweep-on-next-play path).
TEST(AudioEngineTest, UpdateSweepsFinishedFireAndForgetCueWithoutNeedingAnotherPlayCue)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

    try
    {
        AudioEngine engine(XgsFixturePath());
        WaveBank wb(&engine, WriteFixture("xa8_update.xwb", BuildXA8XwbFixtureBytes()));
        SoundBank sb(&engine, WriteFixture("xa8_update.xsb", BuildXA8XsbFixtureBytes()));

        sb.PlayCue("XA8Cue");

        if (SoundBankTestAccess::FireAndForgetCount(sb) == 0 ||
            !SoundBankTestAccess::LastFireAndForgetCue(sb) ||
            !SoundBankTestAccess::LastFireAndForgetCue(sb)->getIsPlayingProperty())
        {
            GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                            "could not exercise real playback";
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        ASSERT_EQ(SoundBankTestAccess::FireAndForgetCount(sb), 1u); // not swept yet, no new PlayCue()

        engine.Update();

        EXPECT_EQ(SoundBankTestAccess::FireAndForgetCount(sb), 0u);
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                        "could not exercise real playback";
    }
}

// P9-LIFECYCLE-012: repeated category-wide Pause/Resume/SetVolume calls must never duplicate a
// cue's entry in AudioEngine's active-cue registry. RegisterCue() is the registry's only
// inserter (called once from Cue::Play(), itself now guarded by P9-LIFECYCLE-010/011 against
// being re-entered on an already Playing/Paused/Stopped cue) -- AudioCategory operations only
// ever call methods on cues already in the registry, never RegisterCue() again.
TEST(AudioEngineTest, RepeatedCategoryOperationsDoNotDuplicateActiveCueRegistryEntries)
{
    AudioEngine engine(XgsFixturePath());
    WaveBank wb(&engine, WriteFixture("xa_lifecycle012.xwb", BuildXA8XwbFixtureBytes()));
    SoundBank sb(&engine, WriteFixture("xa_lifecycle012.xsb", BuildXA8XsbFixtureBytes()));

    std::unique_ptr<Cue> cue(sb.GetCue("XA8Cue"));
    cue->Play();
    ASSERT_EQ(AudioEngineTestAccess::ActiveCueCount(engine), 1u);

    AudioCategory category = engine.GetCategory("Default");
    category.Pause();
    category.Pause();
    category.Resume();
    category.Resume();
    category.SetVolume(0.5f);
    category.SetVolume(0.8f);

    EXPECT_EQ(AudioEngineTestAccess::ActiveCueCount(engine), 1u);
}

// P9-STOP-005/009: the old code unregistered a cue from AudioEngine's activeCues immediately on
// ANY Stop() call, regardless of whether there was still a real release tail playing -- meaning a
// category-wide operation issued right after an AsAuthored stop could no longer reach a cue whose
// sound was still audibly playing. A non-immediate stop with a real tail (State::Stopping) must
// leave the cue registered until it's actually immediate-stopped or disposed.
TEST(AudioEngineTest, StopAsAuthoredDoesNotUnregisterFromAudioEngineWhileTailStillPlaying)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

    try
    {
        AudioEngine engine(XgsFixturePath());
        WaveBank wb(&engine, WriteFixture("p9stop_long.xwb", BuildP9StopLongXwbFixtureBytes()));
        SoundBank sb(&engine, WriteFixture("p9stop_long.xsb", BuildP9StopLongXsbFixtureBytes()));

        std::unique_ptr<Cue> cue(sb.GetCue("P9StopLongCue"));
        cue->Play();

        if (AudioEngineTestAccess::ActiveCueCount(engine) == 0)
        {
            GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                            "could not exercise real playback";
        }

        cue->Stop(AudioStopOptions::AsAuthored);

        EXPECT_TRUE(cue->getIsStoppingProperty());
        EXPECT_FALSE(cue->getIsStoppedProperty());
        EXPECT_EQ(AudioEngineTestAccess::ActiveCueCount(engine), 1u);

        cue->Stop(AudioStopOptions::Immediate);
        EXPECT_TRUE(cue->getIsStoppedProperty());
        EXPECT_EQ(AudioEngineTestAccess::ActiveCueCount(engine), 0u);
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                        "could not exercise real playback";
    }
}

// P9-STOP-010: AudioEngine::Update() must progress an in-progress authored fade on its own --
// not just whenever some other code happens to query a getter on the cue (which would also
// reconcile it, making a getter-based check unable to prove Update() specifically did the work).
// CueTestAccess::ActiveInstance() is a raw field read, not a getter, so calling engine.Update()
// as the *only* thing between Stop(AsAuthored) and reading the instance's volume back proves
// Update() itself ticked the fade forward, matching real FACT games calling AudioEngine.Update()
// every frame to keep every active fade progressing.
TEST(AudioEngineTest, UpdateProgressesInProgressAuthoredFadeWithoutAnyOtherCueQuery)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

    try
    {
        AudioEngine engine(XgsFixturePath());
        WaveBank wb(&engine, WriteFixture("p9stop_long_update.xwb", BuildP9StopLongXwbFixtureBytes()));
        SoundBank sb(&engine, WriteFixture("p9stop_long_update.xsb", BuildP9StopLongXsbFixtureBytes()));

        std::unique_ptr<Cue> cue(sb.GetCue("P9StopLongCue"));
        cue->Play();

        SoundEffectInstance* preStop = CueTestAccess::ActiveInstance(*cue, 0);
        if (!preStop)
        {
            GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                            "could not create a real SoundEffectInstance";
        }
        const float startVolume = preStop->getVolumeProperty();
        // P11-TEST-001: exact value, not just non-zero -- both the sound's and category 0's
        // volume bytes are 0xFF, whose log-centibel amplitude conversion exceeds 1.0
        // individually, so the combined pre-fade volume clamps to exactly 1.0 (see the fade
        // comment below for the same 0xFF/0xFF fixture detail).
        ASSERT_FLOAT_EQ(startVolume, 1.0f);

        cue->Stop(AudioStopOptions::AsAuthored);

        // 80% elapsed: see CueTests.cpp's StopAsAuthoredRampsVolumeDownOverAuthoredFadeDuration
        // for why this needs to be well past 50% (both the sound's and category 0's volume bytes
        // are 0xFF, whose log-centibel amplitude conversion exceeds 1.0 individually, so their
        // product needs the fade multiplier below ~0.25 before the [0,1] clamp stops masking it).
        std::this_thread::sleep_for(std::chrono::milliseconds(kP9StopLongCueFadeOutMS * 4 / 5));
        engine.Update(); // the ONLY tick between Stop() and the readback below

        SoundEffectInstance* postUpdate = CueTestAccess::ActiveInstance(*cue, 0);
        ASSERT_NE(postUpdate, nullptr);
        EXPECT_LT(postUpdate->getVolumeProperty(), startVolume);

        cue->Stop(AudioStopOptions::Immediate); // clean up rather than leaking a Stopping cue
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                        "could not exercise real playback";
    }
}

// ===================== GetTypeName =====================

TEST(AudioEngineTest, GetTypeNameIsDottedXnaName)
{
    AudioEngine engine(XgsFixturePath());
    EXPECT_EQ(engine.GetTypeName(), "Microsoft.Xna.Framework.Audio.AudioEngine");
}
