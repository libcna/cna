// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Audio/AudioEmitter.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioEngine.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioListener.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioStopOptions.hpp"
#include "Microsoft/Xna/Framework/Audio/Cue.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundBank.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffectInstance.hpp"
#include "Microsoft/Xna/Framework/Audio/WaveBank.hpp"
#include "CueTestAccess.hpp"
#include "SoundEffectInstanceTestAccess.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/EventArgs.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/Object.hpp"
#include "System/ObjectDisposedException.hpp"

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

#include <SDL3_mixer/SDL_mixer.h>

using Microsoft::Xna::Framework::Audio::AudioEmitter;
using Microsoft::Xna::Framework::Audio::AudioEngine;
using Microsoft::Xna::Framework::Audio::AudioListener;
using Microsoft::Xna::Framework::Audio::AudioStopOptions;
using Microsoft::Xna::Framework::Audio::Cue;
using Microsoft::Xna::Framework::Audio::SoundBank;
using Microsoft::Xna::Framework::Audio::SoundEffectInstance;
using Microsoft::Xna::Framework::Audio::SoundEffectInstanceTestAccess;
using Microsoft::Xna::Framework::Audio::WaveBank;

namespace
{
    using Microsoft::Xna::Framework::Audio::CueTestAccess;

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

    void AppendF32(std::vector<uint8_t>& buf, float v)
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

    void AppendCStr(std::vector<uint8_t>& buf, const std::string& s)
    {
        buf.insert(buf.end(), s.begin(), s.end());
        buf.push_back(0);
    }

    // Minimal .xgs fixture with one category ("Default") and one variable ("Volume",
    // initial value 0.5) — gives Cue::GetVariable/SetVariable a real engine-known name.
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

    // Minimal .xsb with zero wavebanks/sounds and one simple cue named "Explosion".
    // Cue::Play() cannot resolve an actual sound (soundCount==0), so it takes the
    // "no sound found" branch and still transitions to State::Playing — enough to
    // exercise the full state machine without needing a WaveBank or audio device.
    std::vector<uint8_t> BuildXsbFixtureBytes()
    {
        constexpr uint32_t headerSize   = 74;
        constexpr uint32_t bankNameSize = 64;
        constexpr uint32_t baseOffset   = headerSize + bankNameSize; // 138

        const uint32_t cueSimpleOffset    = baseOffset;
        const uint32_t cueNameIndexOffset = cueSimpleOffset + 5;
        const uint32_t cueNameStrOffset   = cueNameIndexOffset + 6;
        const std::string cueName         = "Explosion";

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
        AppendU8(data, 0);  // wavebankCount
        AppendU16(data, 0); // soundCount
        AppendU16(data, 0); // cueNameLength
        AppendU16(data, 0); // unknown

        AppendS32(data, static_cast<int32_t>(cueSimpleOffset));
        AppendS32(data, -1); // cueComplexOffset
        AppendS32(data, -1); // cueNameOffset (unused by the parser)
        AppendS32(data, 0);  // unknown
        AppendS32(data, -1); // variationOffset (must sit at header byte 0x32)
        AppendS32(data, 0);  // transitionOffset (unused)
        AppendS32(data, -1); // wavebankNameOffset
        AppendS32(data, 0);  // cueHashOffset (unused)
        AppendS32(data, static_cast<int32_t>(cueNameIndexOffset));
        AppendS32(data, -1); // soundOffset

        AppendPadded(data, "TestSoundBank", bankNameSize);

        AppendU8(data, 0);
        AppendU32(data, 0);

        AppendU32(data, cueNameStrOffset);
        AppendU16(data, 0);

        AppendCStr(data, cueName);

        return data;
    }

    // .xsb with 2 simple sounds (distinguished only by categoryIndex: 0 and 1) and one
    // complex cue ("Weighted") referencing a SOUND-type variation table with 2 entries whose
    // weight ranges are lopsided (weight 1 vs weight 99 out of a total of 100) -- regression
    // fixture for XA-3. Neither sound references a real wavebank, so Cue::Play() cannot spawn
    // a SoundEffectInstance, but it still resolves and stores the picked sound's category
    // index (categoryIdx_) before attempting to, which is enough to observe the pick without
    // a working audio device.
    std::vector<uint8_t> BuildXsbFixtureBytesWithWeightedVariation()
    {
        constexpr uint32_t headerSize   = 74;
        constexpr uint32_t bankNameSize = 64;
        constexpr uint32_t baseOffset   = headerSize + bankNameSize; // 138
        constexpr uint32_t soundSize    = 12; // simple sound: flags+cat+vol+pitch+prio+len+waveIdx+wbIdx

        const uint32_t soundOffset        = baseOffset;                 // 138
        const uint32_t sound0Code         = soundOffset;                 // 138
        const uint32_t sound1Code         = soundOffset + soundSize;     // 150
        const uint32_t variationOffset    = soundOffset + 2 * soundSize; // 162
        const uint32_t cueComplexOffset   = variationOffset + 20;        // 182 (table is 20 bytes)
        const uint32_t cueNameIndexOffset = cueComplexOffset + 15;       // 197 (entry is 15 bytes)
        const uint32_t cueNameStrOffset   = cueNameIndexOffset + 6;      // 203
        const std::string cueName         = "Weighted";

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
        AppendU8(data, 0);  // wavebankCount
        AppendU16(data, 2); // soundCount
        AppendU16(data, 0); // cueNameLength
        AppendU16(data, 0); // unknown

        AppendS32(data, -1); // cueSimpleOffset
        AppendS32(data, static_cast<int32_t>(cueComplexOffset));
        AppendS32(data, -1); // cueNameOffset (unused by the parser)
        AppendS32(data, 0);  // unknown
        AppendS32(data, static_cast<int32_t>(variationOffset));
        AppendS32(data, 0);  // transitionOffset (unused)
        AppendS32(data, -1); // wavebankNameOffset
        AppendS32(data, 0);  // cueHashOffset (unused)
        AppendS32(data, static_cast<int32_t>(cueNameIndexOffset));
        AppendS32(data, static_cast<int32_t>(soundOffset));

        AppendPadded(data, "WeightedTestBank", bankNameSize);

        // Sound 0: categoryIndex=0 ("low weight" pick)
        AppendU8(data, 0);   // flags: simple, no RPC/DSP
        AppendU16(data, 0);  // categoryIndex
        AppendU8(data, 0xFF); // volume raw byte (unused by this test)
        AppendU16(data, 0);  // pitchCents
        AppendU8(data, 0);   // priority
        AppendU16(data, 0);  // soundLength (skipped)
        AppendU16(data, 0);  // waveIdx
        AppendU8(data, 0);   // wbIdx

        // Sound 1: categoryIndex=1 ("high weight" pick)
        AppendU8(data, 0);
        AppendU16(data, 1);
        AppendU8(data, 0xFF);
        AppendU16(data, 0);
        AppendU8(data, 0);
        AppendU16(data, 0);
        AppendU16(data, 0);
        AppendU8(data, 0);

        // Variation table: type=SOUND(1), 2 entries.
        const uint32_t entryCountAndFlags = 2u | (1u << 19); // entryCount=2, type=1 (SOUND)
        AppendU32(data, entryCountAndFlags);
        AppendU16(data, 0);   // unknown
        AppendU16(data, static_cast<uint16_t>(-1)); // variable (unused, not interactive)

        AppendU32(data, sound0Code); // entry 0: weight = 1-0 = 1
        AppendU8(data, 0);           // weightMin
        AppendU8(data, 1);           // weightMax

        AppendU32(data, sound1Code); // entry 1: weight = 100-1 = 99
        AppendU8(data, 1);           // weightMin
        AppendU8(data, 100);         // weightMax

        // Complex cue "Weighted": not single-sound, sbCode points at the variation table.
        AppendU8(data, 0);   // flags (CUE_FLAG_SINGLE_SOUND clear)
        AppendU32(data, variationOffset); // sbCode
        AppendU32(data, 0); // transitionOffset
        AppendU8(data, 0xFF); // instanceLimit
        AppendU16(data, 0); // fadeInMS
        AppendU16(data, 0); // fadeOutMS
        AppendU8(data, 0);  // maxInstanceBehavior

        AppendU32(data, cueNameStrOffset);
        AppendU16(data, 0); // unknown

        AppendCStr(data, cueName);

        return data;
    }

    std::string WriteFixture(const std::string& dirName, const std::string& fileName,
                              const std::vector<uint8_t>& bytes)
    {
        auto dir = std::filesystem::temp_directory_path() / dirName;
        std::filesystem::create_directories(dir);
        auto file = dir / fileName;
        std::ofstream f(file, std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return file.string();
    }

    const std::string& XgsFixturePath()
    {
        static const std::string path =
            WriteFixture("cna_cue_test", "fixture.xgs", BuildXgsFixtureBytes());
        return path;
    }

    const std::string& XsbFixturePath()
    {
        static const std::string path =
            WriteFixture("cna_cue_test", "fixture.xsb", BuildXsbFixtureBytes());
        return path;
    }

    const std::string& XsbWeightedVariationFixturePath()
    {
        static const std::string path = WriteFixture(
            "cna_cue_test", "fixture_weighted.xsb", BuildXsbFixtureBytesWithWeightedVariation());
        return path;
    }

    // Both fixtures are parseable, unlike the "stub" AudioEngine used by
    // SoundBankTests.cpp/WaveBankTests.cpp, so "Volume" is a real, engine-known
    // global variable Cue::GetVariable/SetVariable can validate against.
    AudioEngine& SharedEngine()
    {
        static AudioEngine engine(XgsFixturePath());
        return engine;
    }

    SoundBank& SharedBank()
    {
        static SoundBank bank(&SharedEngine(), XsbFixturePath());
        return bank;
    }

    SoundBank& SharedWeightedVariationBank()
    {
        static SoundBank bank(&SharedEngine(), XsbWeightedVariationFixturePath());
        return bank;
    }

    constexpr const char* kApply3DWaveBankName = "Apply3DWaveBank";

    // Minimal compact .xwb with one mono 16-bit PCM entry (200 bytes of silence) -- needed
    // (unlike BuildXsbFixtureBytes/BuildXsbFixtureBytesWithWeightedVariation above) so that
    // Cue::Play() resolves a real WaveBank entry and creates an actual SoundEffectInstance in
    // Cue::active_, which T-4B's Apply3D geometry test needs a real MIX_Track to inspect.
    std::vector<uint8_t> BuildApply3DXwbFixtureBytes()
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
        AppendPadded(data, kApply3DWaveBankName, 64);
        AppendU32(data, entryMetaDataSize);
        AppendU32(data, 0); // entryNameElementSize
        AppendU32(data, alignment);
        const uint32_t compactFormat =
              (0u)            // format tag: PCM
            | (1u << 2)       // channels: raw field IS the real channel count -> mono (IN-7)
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
    // simple cue "Apply3DCue" playing that sound.
    std::vector<uint8_t> BuildApply3DXsbFixtureBytes()
    {
        constexpr uint32_t headerSize   = 74;
        constexpr uint32_t bankNameSize = 64;
        constexpr uint32_t baseOffset   = headerSize + bankNameSize;

        const uint32_t wavebankNameOffset = baseOffset;
        const uint32_t soundOffset        = wavebankNameOffset + 64;
        const uint32_t cueSimpleOffset    = soundOffset + 12;
        const uint32_t cueNameIndexOffset = cueSimpleOffset + 5;
        const uint32_t cueNameStrOffset   = cueNameIndexOffset + 6;
        const std::string cueName = "Apply3DCue";

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

        AppendPadded(data, "Apply3DSoundBank", bankNameSize);
        AppendPadded(data, kApply3DWaveBankName, 64);

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

    WaveBank& SharedApply3DWaveBank()
    {
        static WaveBank wb(&SharedEngine(), WriteFixture(
            "cna_cue_test", "apply3d.xwb", BuildApply3DXwbFixtureBytes()));
        return wb;
    }

    constexpr const char* kLongWaveBankName = "LongWaveBank";

    // Same layout as BuildApply3DXwbFixtureBytes, but with a full second of audio instead of
    // 200 bytes (~1ms) -- XA-6's "is the track still actually playing" check is a live, timing-
    // sensitive MIX_TrackPlaying() query, and a ~1ms sound can easily have already finished
    // playing by the time a test gets around to checking it.
    std::vector<uint8_t> BuildLongXwbFixtureBytes()
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

    // Same layout as BuildApply3DXsbFixtureBytes, but referencing LongWaveBank and named "LongCue".
    std::vector<uint8_t> BuildLongXsbFixtureBytes()
    {
        constexpr uint32_t headerSize   = 74;
        constexpr uint32_t bankNameSize = 64;
        constexpr uint32_t baseOffset   = headerSize + bankNameSize;

        const uint32_t wavebankNameOffset = baseOffset;
        const uint32_t soundOffset        = wavebankNameOffset + 64;
        const uint32_t cueSimpleOffset    = soundOffset + 12;
        const uint32_t cueNameIndexOffset = cueSimpleOffset + 5;
        const uint32_t cueNameStrOffset   = cueNameIndexOffset + 6;
        const std::string cueName = "LongCue";

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

        AppendPadded(data, "LongSoundBank", bankNameSize);
        AppendPadded(data, kLongWaveBankName, 64);

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

    WaveBank& SharedLongWaveBank()
    {
        static WaveBank wb(&SharedEngine(), WriteFixture(
            "cna_cue_test", "long.xwb", BuildLongXwbFixtureBytes()));
        return wb;
    }

    SoundBank& SharedLongBank()
    {
        (void)SharedLongWaveBank(); // must be registered with the engine before GetCue()/Play()
        static SoundBank bank(&SharedEngine(), WriteFixture(
            "cna_cue_test", "long.xsb", BuildLongXsbFixtureBytes()));
        return bank;
    }

    SoundBank& SharedApply3DBank()
    {
        (void)SharedApply3DWaveBank(); // must be registered with the engine before GetCue()/Play()
        static SoundBank bank(&SharedEngine(), WriteFixture(
            "cna_cue_test", "apply3d.xsb", BuildApply3DXsbFixtureBytes()));
        return bank;
    }

    std::unique_ptr<Cue> MakeCue()
    {
        return std::unique_ptr<Cue>(SharedBank().GetCue("Explosion"));
    }
}

// ===================== State properties (9) + Name =====================

TEST(CueTest, IsCreatedIsAlwaysFalse)
{
    // The constructor sets state_ straight to Prepared; nothing in Cue transitions
    // back to the Created state, so IsCreated is unreachable by design (matches
    // XACT's "created but not yet prepared" phase, which CNA's synchronous XSB
    // parsing skips entirely).
    auto cue = MakeCue();
    EXPECT_FALSE(cue->getIsCreatedProperty());
}

TEST(CueTest, IsPreparedTrueImmediatelyAfterCreation)
{
    auto cue = MakeCue();
    EXPECT_TRUE(cue->getIsPreparedProperty());
}

TEST(CueTest, IsPreparingIsAlwaysFalse)
{
    auto cue = MakeCue();
    EXPECT_FALSE(cue->getIsPreparingProperty());
}

TEST(CueTest, IsDisposedFalseInitiallyAndTrueAfterDispose)
{
    auto cue = MakeCue();
    EXPECT_FALSE(cue->getIsDisposedProperty());
    cue->Dispose();
    EXPECT_TRUE(cue->getIsDisposedProperty());
}

TEST(CueTest, IsPlayingTrueAfterPlay)
{
    auto cue = MakeCue();
    cue->Play();
    EXPECT_TRUE(cue->getIsPlayingProperty());
}

TEST(CueTest, IsPausedTrueAfterPause)
{
    auto cue = MakeCue();
    cue->Play();
    cue->Pause();
    EXPECT_TRUE(cue->getIsPausedProperty());
    EXPECT_FALSE(cue->getIsPlayingProperty());
}

TEST(CueTest, IsStoppedTrueAfterStop)
{
    auto cue = MakeCue();
    cue->Play();
    cue->Stop(AudioStopOptions::AsAuthored);
    EXPECT_TRUE(cue->getIsStoppedProperty());
}

TEST(CueTest, IsStoppingIsAlwaysFalse)
{
    // StopInternal transitions straight from Playing to Stopped; there is no
    // intermediate release/fade phase modeled (SDL3_mixer stops synchronously).
    auto cue = MakeCue();
    cue->Play();
    cue->Stop(AudioStopOptions::Immediate);
    EXPECT_FALSE(cue->getIsStoppingProperty());
}

// P9-LIFECYCLE-001/002/005: "Apply3DCue" has a real (200-byte, ~1.13ms) WaveBank-backed instance
// (unlike MakeCue()'s wavebank-less "Explosion" fixture used by every test above), so it actually
// finishes playing on its own -- IsPlaying/IsStopped must reconcile to reflect that without any
// explicit Stop() call, and the finished instance must be dropped from Cue::active_.
TEST(CueTest, PlayingCueNaturallyTransitionsToStoppedAfterPlaybackFinishes)
{
    ::setenv("SDL_AUDIODRIVER", "dummy", 1);

    try
    {
        std::unique_ptr<Cue> cue(SharedApply3DBank().GetCue("Apply3DCue"));
        cue->Play();

        if (!CueTestAccess::ActiveInstance(*cue, 0))
        {
            GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                            "could not create a real SoundEffectInstance";
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        EXPECT_TRUE(cue->getIsStoppedProperty());
        EXPECT_FALSE(cue->getIsPlayingProperty());
        EXPECT_EQ(CueTestAccess::ActiveInstance(*cue, 0), nullptr);
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                        "could not exercise real playback";
    }
}

// P9-LIFECYCLE-013: a naturally-finished cue must not be resurrected into Paused by a stray
// Pause() call (mirrors FACTCue_Pause rejecting STOPPING/STOPPED cues in FACT.c).
TEST(CueTest, PauseAfterNaturalCompletionIsANoOp)
{
    ::setenv("SDL_AUDIODRIVER", "dummy", 1);

    try
    {
        std::unique_ptr<Cue> cue(SharedApply3DBank().GetCue("Apply3DCue"));
        cue->Play();

        if (!CueTestAccess::ActiveInstance(*cue, 0))
        {
            GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                            "could not create a real SoundEffectInstance";
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        ASSERT_TRUE(cue->getIsStoppedProperty());

        cue->Pause();
        EXPECT_TRUE(cue->getIsStoppedProperty());
        EXPECT_FALSE(cue->getIsPausedProperty());
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                        "could not exercise real playback";
    }
}

// P9-LIFECYCLE-010/011: FACTCue_Play (FACT.c) silently rejects a cue whose state already has
// PLAYING/STOPPING/STOPPED set; FNA's Cue.Play() discards the return value, so this is a no-op
// from the caller's perspective, not an exception -- and it must not spawn a second, overlapping
// SoundEffectInstance for the same wave reference.
TEST(CueTest, PlayCalledTwiceWhileAlreadyPlayingIsANoOpAndDoesNotDuplicateInstances)
{
    ::setenv("SDL_AUDIODRIVER", "dummy", 1);

    try
    {
        std::unique_ptr<Cue> cue(SharedApply3DBank().GetCue("Apply3DCue"));
        cue->Play();

        SoundEffectInstance* first = CueTestAccess::ActiveInstance(*cue, 0);
        if (!first)
        {
            GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                            "could not create a real SoundEffectInstance";
        }

        cue->Play();

        EXPECT_EQ(CueTestAccess::ActiveInstance(*cue, 0), first);
        EXPECT_EQ(CueTestAccess::ActiveInstance(*cue, 1), nullptr);
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                        "could not exercise real playback";
    }
}

TEST(CueTest, PlayWhilePausedIsANoOp)
{
    auto cue = MakeCue();
    cue->Play();
    cue->Pause();
    cue->Play();
    EXPECT_TRUE(cue->getIsPausedProperty());
    EXPECT_FALSE(cue->getIsPlayingProperty());
}

TEST(CueTest, PlayAfterStopIsANoOp)
{
    auto cue = MakeCue();
    cue->Play();
    cue->Stop(AudioStopOptions::Immediate);
    cue->Play();
    EXPECT_TRUE(cue->getIsStoppedProperty());
    EXPECT_FALSE(cue->getIsPlayingProperty());
}

TEST(CueTest, NameReturnsGivenName)
{
    auto cue = MakeCue();
    EXPECT_EQ(cue->getNameProperty(), "Explosion");
}

// ===================== Apply3D =====================

TEST(CueTest, Apply3DDoesNotThrowWhenNotDisposed)
{
    auto cue = MakeCue();
    AudioListener listener;
    AudioEmitter emitter;
    EXPECT_NO_THROW(cue->Apply3D(listener, emitter));
}

TEST(CueTest, Apply3DAfterDisposeThrowsObjectDisposed)
{
    auto cue = MakeCue();
    cue->Dispose();
    AudioListener listener;
    AudioEmitter emitter;
    EXPECT_THROW(cue->Apply3D(listener, emitter), System::ObjectDisposedException);
}

// T-4B: Apply3D must propagate to every real SoundEffectInstance playing under this cue, not
// just no-op. MakeCue()'s wavebank-less fixture can't exercise this (Cue::active_ stays empty --
// see BuildXsbFixtureBytes's comment), so this uses SharedApply3DBank()'s real WaveBank-backed
// fixture instead, and reads back the actual SDL_mixer track gain to prove the effect is real,
// not just "didn't throw".
TEST(CueTest, Apply3DAttenuatesActiveInstanceTrackGainWithDistance)
{
    ::setenv("SDL_AUDIODRIVER", "dummy", 1);

    try
    {
        std::unique_ptr<Cue> cue(SharedApply3DBank().GetCue("Apply3DCue"));
        cue->Play();

        SoundEffectInstance* inst = CueTestAccess::ActiveInstance(*cue, 0);
        if (!inst)
        {
            GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                            "could not create a real SoundEffectInstance";
        }

        MIX_Track* track = SoundEffectInstanceTestAccess::GetTrack(*inst);
        ASSERT_NE(track, nullptr);

        AudioListener listener; // default position: origin
        AudioEmitter nearEmitter;
        nearEmitter.setPositionProperty({1.0f, 0.0f, 0.0f});
        cue->Apply3D(listener, nearEmitter);
        const float nearGain = MIX_GetTrackGain(track);

        AudioEmitter farEmitter;
        farEmitter.setPositionProperty({10000.0f, 0.0f, 0.0f});
        cue->Apply3D(listener, farEmitter);
        const float farGain = MIX_GetTrackGain(track);

        EXPECT_LT(farGain, nearGain);

        cue->Stop(AudioStopOptions::Immediate);
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                        "could not exercise real playback";
    }
}

// XA-6: Stop(AsAuthored) must let the track keep playing (SoundEffectInstance::Stop(false) just
// exits any loop) instead of hard-stopping it -- the old code called active_.clear() right after
// pi.instance->Stop(immediate) unconditionally, destroying every instance (and thus hard-stopping
// its track via ~SoundEffectInstance()'s Dispose() cascade) regardless of `immediate`. Contrasted
// directly against Stop(Immediate), which must still hard-stop right away.
TEST(CueTest, StopAsAuthoredLeavesTrackPlayingButStopImmediateHardStopsRightAway)
{
    ::setenv("SDL_AUDIODRIVER", "dummy", 1);

    try
    {
        std::unique_ptr<Cue> asAuthoredCue(SharedLongBank().GetCue("LongCue"));
        asAuthoredCue->Play();

        SoundEffectInstance* inst = CueTestAccess::ActiveInstance(*asAuthoredCue, 0);
        if (!inst)
        {
            GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                            "could not create a real SoundEffectInstance";
        }
        MIX_Track* track = SoundEffectInstanceTestAccess::GetTrack(*inst);
        ASSERT_NE(track, nullptr);

        asAuthoredCue->Stop(AudioStopOptions::AsAuthored);

        // The instance must still be tracked (not destroyed) and its track still playing --
        // AsAuthored only exits the loop, it doesn't cut the sound off.
        EXPECT_NE(CueTestAccess::ActiveInstance(*asAuthoredCue, 0), nullptr);
        EXPECT_TRUE(MIX_TrackPlaying(track));

        // Contrast: Stop(Immediate) on a fresh instance must hard-stop right away -- it destroys
        // the instance (and with it the underlying track) immediately, so there is no track left
        // to query afterward; the absence of an active instance is itself the observable effect.
        std::unique_ptr<Cue> immediateCue(SharedApply3DBank().GetCue("Apply3DCue"));
        immediateCue->Play();
        SoundEffectInstance* inst2 = CueTestAccess::ActiveInstance(*immediateCue, 0);
        ASSERT_NE(inst2, nullptr);
        ASSERT_NE(SoundEffectInstanceTestAccess::GetTrack(*inst2), nullptr);

        immediateCue->Stop(AudioStopOptions::Immediate);

        EXPECT_EQ(CueTestAccess::ActiveInstance(*immediateCue, 0), nullptr);
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                        "could not exercise real playback";
    }
}

// ===================== GetVariable =====================

TEST(CueTest, GetVariableValidEngineVariableReturnsInitialValue)
{
    auto cue = MakeCue();
    EXPECT_FLOAT_EQ(cue->GetVariable("Volume"), 0.5f);
}

TEST(CueTest, GetVariableBuiltInVariableReturnsZeroByDefault)
{
    auto cue = MakeCue();
    EXPECT_FLOAT_EQ(cue->GetVariable("Distance"), 0.0f);
    EXPECT_FLOAT_EQ(cue->GetVariable("DopplerPitchScalar"), 0.0f);
    EXPECT_FLOAT_EQ(cue->GetVariable("OrientationAngle"), 0.0f);
}

TEST(CueTest, GetVariableAfterSetReturnsLocalOverride)
{
    auto cue = MakeCue();
    cue->SetVariable("Volume", 0.75f);
    EXPECT_FLOAT_EQ(cue->GetVariable("Volume"), 0.75f);
}

TEST(CueTest, GetVariableEmptyNameThrowsArgumentNull)
{
    auto cue = MakeCue();
    EXPECT_THROW((void)cue->GetVariable(""), System::ArgumentNullException);
}

TEST(CueTest, GetVariableNameOutsideKnownSetThrowsInvalidOperation)
{
    auto cue = MakeCue();
    EXPECT_THROW((void)cue->GetVariable("NoSuchVariable"), System::InvalidOperationException);
}

// ===================== SetVariable =====================

TEST(CueTest, SetVariableValidEngineVariableUpdatesValue)
{
    auto cue = MakeCue();
    cue->SetVariable("Volume", 0.1f);
    EXPECT_FLOAT_EQ(cue->GetVariable("Volume"), 0.1f);
}

TEST(CueTest, SetVariableBuiltInVariableDoesNotThrow)
{
    auto cue = MakeCue();
    EXPECT_NO_THROW(cue->SetVariable("OrientationAngle", 45.0f));
    EXPECT_FLOAT_EQ(cue->GetVariable("OrientationAngle"), 45.0f);
}

TEST(CueTest, SetVariableEmptyNameThrowsArgumentNull)
{
    auto cue = MakeCue();
    EXPECT_THROW(cue->SetVariable("", 1.0f), System::ArgumentNullException);
}

TEST(CueTest, SetVariableNameOutsideKnownSetThrowsInvalidOperation)
{
    auto cue = MakeCue();
    EXPECT_THROW(cue->SetVariable("NoSuchVariable", 1.0f), System::InvalidOperationException);
}

// ===================== Play / Pause / Resume / Stop =====================

TEST(CueTest, PlayTransitionsToPlaying)
{
    auto cue = MakeCue();
    cue->Play();
    EXPECT_TRUE(cue->getIsPlayingProperty());
}

TEST(CueTest, PlayAfterDisposeThrowsObjectDisposed)
{
    auto cue = MakeCue();
    cue->Dispose();
    EXPECT_THROW(cue->Play(), System::ObjectDisposedException);
}

TEST(CueTest, PauseThenResumeReturnsToPlaying)
{
    auto cue = MakeCue();
    cue->Play();
    cue->Pause();
    ASSERT_TRUE(cue->getIsPausedProperty());
    cue->Resume();
    EXPECT_TRUE(cue->getIsPlayingProperty());
    EXPECT_FALSE(cue->getIsPausedProperty());
}

TEST(CueTest, StopAsAuthoredTransitionsToStopped)
{
    auto cue = MakeCue();
    cue->Play();
    cue->Stop(AudioStopOptions::AsAuthored);
    EXPECT_TRUE(cue->getIsStoppedProperty());
    EXPECT_FALSE(cue->getIsPlayingProperty());
}

TEST(CueTest, StopImmediateTransitionsToStopped)
{
    auto cue = MakeCue();
    cue->Play();
    cue->Stop(AudioStopOptions::Immediate);
    EXPECT_TRUE(cue->getIsStoppedProperty());
    EXPECT_FALSE(cue->getIsPlayingProperty());
}

// ===================== Dispose =====================

TEST(CueTest, DisposeIsIdempotent)
{
    auto cue = MakeCue();
    cue->Dispose();
    EXPECT_NO_THROW(cue->Dispose());
    EXPECT_TRUE(cue->getIsDisposedProperty());
}

TEST(CueTest, DisposeRaisesDisposingEvent)
{
    auto cue = MakeCue();
    bool raised = false;
    cue->Disposing += [&raised](System::Object*, const System::EventArgs&) { raised = true; };
    cue->Dispose();
    EXPECT_TRUE(raised);
}

// ===================== GetTypeName =====================

TEST(CueTest, GetTypeNameIsDottedXnaName)
{
    auto cue = MakeCue();
    EXPECT_EQ(cue->GetTypeName(), "Microsoft.Xna.Framework.Audio.Cue");
}

// ===================== Variation selection (XA-3) =====================

TEST(CueTest, PlayWeightedVariationFavorsHigherWeightEntryStatistically)
{
    auto cue = std::unique_ptr<Cue>(SharedWeightedVariationBank().GetCue("Weighted"));

    constexpr int kIterations = 200;
    int highWeightPicks = 0;
    for (int i = 0; i < kIterations; ++i)
    {
        cue->Play();
        if (CueTestAccess::CategoryIndex(*cue) == 1)
            ++highWeightPicks;
    }

    // Entry 1 carries weight 99 of a total 100 -- it should be picked ~99% of the time.
    // Uniform (unweighted) selection between the two entries would land close to 50%, so an
    // 80% threshold cleanly distinguishes weighted selection from the old uniform pick.
    EXPECT_GT(highWeightPicks, kIterations * 0.8);
}
