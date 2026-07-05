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

    // Layout constants for the two RPC curves BuildXgsFixtureBytes appends after its existing
    // category/variable data -- shared with BuildXsbFixtureBytesWithRpc so the .xsb's
    // sound-level RPC code references (absolute file offsets) match these curves exactly
    // (P9-XACT-006/007/008/009). Both curves are bound to variable index 0 ("Volume", range
    // [0,1]): the VOLUME curve maps 0.0->-2000 centibels (10^(-2000/2000) = 0.1x amplitude) and
    // 1.0->0 centibels (unity gain); the PITCH curve maps 0.0->-600 cents and 1.0->+600 cents.
    constexpr uint32_t kXgsHeaderSize       = 69; // 65 (pre-RPC) + 4 (new rpcOffset header field)
    constexpr uint32_t kXgsCategoryDataSize = 10;
    constexpr uint32_t kXgsVariableDataSize = 13;
    constexpr uint32_t kXgsCategoryNameSize = 8; // "Default\0"
    constexpr uint32_t kXgsVariableNameSize = 7; // "Volume\0"
    constexpr uint32_t kXgsRpcOffset        = kXgsHeaderSize + kXgsCategoryDataSize
        + kXgsVariableDataSize + kXgsCategoryNameSize + kXgsVariableNameSize;
    constexpr uint32_t kXgsRpcEntrySize     = 23; // variable(2)+pointCount(1)+parameter(2)+2*(x(4)+y(4)+type(1))
    constexpr uint32_t kVolumeRpcCode       = kXgsRpcOffset;
    constexpr uint32_t kPitchRpcCode        = kXgsRpcOffset + kXgsRpcEntrySize;

    // Minimal .xgs fixture with one category ("Default"), one variable ("Volume", initial value
    // 0.5) -- gives Cue::GetVariable/SetVariable a real engine-known name -- and two RPC curves
    // (see the constants above) bound to that variable, for P9-XACT-006/007's one-shot RPC
    // volume/pitch evaluation.
    std::vector<uint8_t> BuildXgsFixtureBytes()
    {
        const uint32_t categoryOffset     = kXgsHeaderSize;
        const uint32_t variableOffset     = categoryOffset + kXgsCategoryDataSize;
        const uint32_t categoryNameOffset = variableOffset + kXgsVariableDataSize;
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
        AppendU16(data, 2); // rpcCount
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
        AppendU32(data, kXgsRpcOffset);

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

        // RPC 0 (VOLUME): variable=0, 2 points, linear.
        AppendU16(data, 0); // variable
        AppendU8(data, 2);  // pointCount
        AppendU16(data, 0); // parameter = VOLUME
        AppendF32(data, 0.0f); AppendF32(data, -2000.0f); AppendU8(data, 0); // point 0: linear
        AppendF32(data, 1.0f); AppendF32(data, 0.0f);     AppendU8(data, 0); // point 1: linear

        // RPC 1 (PITCH): variable=0, 2 points, linear.
        AppendU16(data, 0); // variable
        AppendU8(data, 2);  // pointCount
        AppendU16(data, 1); // parameter = PITCH
        AppendF32(data, 0.0f); AppendF32(data, -600.0f); AppendU8(data, 0); // point 0: linear
        AppendF32(data, 1.0f); AppendF32(data, 600.0f);  AppendU8(data, 0); // point 1: linear

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

    // .xsb with 2 simple sounds (distinguished only by categoryIndex: 0 and 1) and one complex
    // cue ("Interactive") referencing an INTERACTIVE-type (type==3) variation table with 2
    // entries whose [varMin, varMax] ranges are disjoint and don't cover the full [0, 1] range
    // -- regression fixture for P9-XACT-002/003. The table's `variable` field (0) indexes
    // fixture.xgs's sole declared variable, "Volume" (see BuildXgsFixtureBytes above), so
    // Cue::SetVariable("Volume", ...) before Play() deterministically selects an entry instead
    // of a weighted-random pick. Like BuildXsbFixtureBytesWithWeightedVariation, neither sound
    // references a real wavebank, so Cue::Play() cannot spawn a SoundEffectInstance -- but it
    // still resolves and stores the picked sound's category index (categoryIdx_) before
    // attempting to, which is enough to observe the pick without a working audio device.
    std::vector<uint8_t> BuildXsbFixtureBytesWithInteractiveVariation()
    {
        constexpr uint32_t headerSize   = 74;
        constexpr uint32_t bankNameSize = 64;
        constexpr uint32_t baseOffset   = headerSize + bankNameSize; // 138
        constexpr uint32_t soundSize    = 12; // simple sound: flags+cat+vol+pitch+prio+len+waveIdx+wbIdx
        constexpr uint32_t entrySize    = 16; // INTERACTIVE: soundCode(4)+varMin(4)+varMax(4)+linger(4)

        const uint32_t soundOffset        = baseOffset;                 // 138
        const uint32_t sound0Code         = soundOffset;                 // 138
        const uint32_t sound1Code         = soundOffset + soundSize;     // 150
        const uint32_t variationOffset    = soundOffset + 2 * soundSize; // 162
        const uint32_t tableSize          = 4 + 2 + 2 + 2 * entrySize;   // 40
        const uint32_t cueComplexOffset   = variationOffset + tableSize;
        const uint32_t cueNameIndexOffset = cueComplexOffset + 15;
        const uint32_t cueNameStrOffset   = cueNameIndexOffset + 6;
        const std::string cueName         = "Interactive";

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

        AppendPadded(data, "InteractiveTestBank", bankNameSize);

        // Sound 0: categoryIndex=0 ("low range" pick)
        AppendU8(data, 0);   // flags: simple, no RPC/DSP
        AppendU16(data, 0);  // categoryIndex
        AppendU8(data, 0xFF); // volume raw byte (unused by this test)
        AppendU16(data, 0);  // pitchCents
        AppendU8(data, 0);   // priority
        AppendU16(data, 0);  // soundLength (skipped)
        AppendU16(data, 0);  // waveIdx
        AppendU8(data, 0);   // wbIdx

        // Sound 1: categoryIndex=1 ("high range" pick)
        AppendU8(data, 0);
        AppendU16(data, 1);
        AppendU8(data, 0xFF);
        AppendU16(data, 0);
        AppendU8(data, 0);
        AppendU16(data, 0);
        AppendU16(data, 0);
        AppendU8(data, 0);

        // Variation table: type=INTERACTIVE(3), 2 entries, bound to variable index 0 ("Volume").
        const uint32_t entryCountAndFlags = 2u | (3u << 19); // entryCount=2, type=3 (INTERACTIVE)
        AppendU32(data, entryCountAndFlags);
        AppendU16(data, 0);  // unknown
        AppendU16(data, 0);  // variable = 0 ("Volume" in fixture.xgs)

        AppendU32(data, sound0Code);  // entry 0
        AppendF32(data, 0.0f);        // varMin
        AppendF32(data, 0.4f);        // varMax
        AppendU32(data, 0);           // linger (unused)

        AppendU32(data, sound1Code);  // entry 1
        AppendF32(data, 0.6f);        // varMin
        AppendF32(data, 1.0f);        // varMax
        AppendU32(data, 0);           // linger (unused)

        // Complex cue "Interactive": not single-sound, sbCode points at the variation table.
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

    const std::string& XsbInteractiveVariationFixturePath()
    {
        static const std::string path = WriteFixture(
            "cna_cue_test", "fixture_interactive.xsb", BuildXsbFixtureBytesWithInteractiveVariation());
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

    SoundBank& SharedInteractiveVariationBank()
    {
        static SoundBank bank(&SharedEngine(), XsbInteractiveVariationFixturePath());
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

    // .xsb with one simple cue ("FilterCue") pointing at a COMPLEX sound with a single track
    // (referencing LongWaveBank, so a real SoundEffectInstance/track gets spawned) whose
    // filterData/frequency encode a real high-pass filter (bit0=1 has-filter, (filterData>>1)&
    // 0x02==2 high-pass per FAudio's own bit-decode, qfactor=6, frequency=8000Hz). Regression
    // fixture for P9-XACT-011.
    std::vector<uint8_t> BuildFilterXsbFixtureBytes()
    {
        constexpr uint32_t headerSize   = 74;
        constexpr uint32_t bankNameSize = 64;
        constexpr uint32_t baseOffset   = headerSize + bankNameSize;

        const uint32_t wavebankNameOffset = baseOffset;
        const uint32_t soundOffset        = wavebankNameOffset + 64;
        // flags+cat+vol+pitch+prio+len(9) + trackCount(1) + track-meta vol+code+filterData+freq(9)
        constexpr uint32_t soundPrefixSize = 9 + 1 + 9;
        constexpr uint32_t eventSize       = 16; // one PlayWave event, see BuildPlayWaveEventBytes
        constexpr uint32_t soundSize       = soundPrefixSize + 1 /*eventCount*/ + eventSize;
        const uint32_t trackEventsOffset  = soundOffset + soundPrefixSize;
        const uint32_t cueSimpleOffset    = soundOffset + soundSize;
        const uint32_t cueNameIndexOffset = cueSimpleOffset + 5;
        const uint32_t cueNameStrOffset   = cueNameIndexOffset + 6;
        const std::string cueName = "FilterCue";

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

        AppendPadded(data, "FilterSoundBank", bankNameSize);
        AppendPadded(data, kLongWaveBankName, 64);

        // Sound: COMPLEX, one track.
        AppendU8(data, 0x01); // SOUND_FLAG_COMPLEX
        AppendU16(data, 0);   // categoryIndex
        AppendU8(data, 0xFF); // volume raw byte
        AppendU16(data, 0);   // pitchCents
        AppendU8(data, 0);    // priority
        AppendU16(data, 0);   // soundLength (skipped)
        AppendU8(data, 1);    // trackCount

        // Track metadata: volume byte, code (absolute offset to event array), filterData,
        // frequency. filterData = 0x0605: qfactor=6 (upper byte), bit0=1 (has filter),
        // (filterData>>1)&0x02==2 (high-pass, FAudio's own bit-decode -- P9-XACT-010).
        AppendU8(data, 0xFF); // track volume raw byte
        AppendU32(data, trackEventsOffset);
        AppendU16(data, 0x0605); // filterData
        AppendU16(data, 8000);   // frequency (Hz)

        // Track event array: one PlayWave event referencing LongWaveBank entry 0.
        AppendU8(data, 1); // eventCount
        AppendU32(data, 1u); // evtInfo: type=FACTEVENT_PLAYWAVE (1), timestamp=0
        AppendU16(data, 0);  // randomOffset
        AppendU8(data, 0xFF); // separator
        AppendU8(data, 0);   // flags
        AppendU16(data, 0);  // waveIdx
        AppendU8(data, 0);   // wbIdx
        AppendU8(data, 0);   // loopCount
        AppendU16(data, 0);  // position
        AppendU16(data, 0);  // angle

        // Simple cue.
        AppendU8(data, 0);
        AppendU32(data, soundOffset);

        AppendU32(data, cueNameStrOffset);
        AppendU16(data, 0);

        AppendCStr(data, cueName);

        return data;
    }

    // Same layout as BuildLongXsbFixtureBytes, but the simple cue's sound code deliberately
    // points past the sound table instead of at the one real sound -- simulates corrupt/
    // malformed content where a cue's sound reference can't resolve. Regression fixture for
    // P9-XACT-014 (the parser used to silently alias an unresolvable code onto sound index 0).
    std::vector<uint8_t> BuildUnresolvableSoundXsbFixtureBytes()
    {
        constexpr uint32_t headerSize   = 74;
        constexpr uint32_t bankNameSize = 64;
        constexpr uint32_t baseOffset   = headerSize + bankNameSize;

        const uint32_t wavebankNameOffset = baseOffset;
        const uint32_t soundOffset        = wavebankNameOffset + 64;
        const uint32_t bogusSoundCode     = soundOffset + 1000; // matches no real sound
        const uint32_t cueSimpleOffset    = soundOffset + 12;
        const uint32_t cueNameIndexOffset = cueSimpleOffset + 5;
        const uint32_t cueNameStrOffset   = cueNameIndexOffset + 6;
        const std::string cueName = "UnresolvableSoundCue";

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

        AppendPadded(data, "UnresolvableSoundBank", bankNameSize);
        AppendPadded(data, kLongWaveBankName, 64);

        // The one real sound in the bank -- if the bug were present, the cue below would
        // silently resolve to this sound instead of nothing.
        AppendU8(data, 0);    // flags
        AppendU16(data, 0);   // categoryIndex
        AppendU8(data, 0xFF); // volume raw byte
        AppendU16(data, 0);   // pitchCents
        AppendU8(data, 0);    // priority
        AppendU16(data, 0);   // soundLength (skipped)
        AppendU16(data, 0);   // waveIdx
        AppendU8(data, 0);    // wbIdx

        AppendU8(data, 0);
        AppendU32(data, bogusSoundCode);

        AppendU32(data, cueNameStrOffset);
        AppendU16(data, 0);

        AppendCStr(data, cueName);

        return data;
    }

    // Same layout as BuildLongXsbFixtureBytes, but the wave bank *name* the one real sound
    // references ("GhostWaveBank") is never actually registered with the AudioEngine (no
    // matching WaveBank object exists) -- simulates a SoundBank prepared/played before its
    // dependent WaveBank has been loaded, or one that failed to load entirely. Regression
    // fixture for P9-XACT-014/015.
    std::vector<uint8_t> BuildMissingWaveBankXsbFixtureBytes()
    {
        constexpr uint32_t headerSize   = 74;
        constexpr uint32_t bankNameSize = 64;
        constexpr uint32_t baseOffset   = headerSize + bankNameSize;

        const uint32_t wavebankNameOffset = baseOffset;
        const uint32_t soundOffset        = wavebankNameOffset + 64;
        const uint32_t cueSimpleOffset    = soundOffset + 12;
        const uint32_t cueNameIndexOffset = cueSimpleOffset + 5;
        const uint32_t cueNameStrOffset   = cueNameIndexOffset + 6;
        const std::string cueName = "MissingWaveBankCue";

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

        AppendPadded(data, "MissingWaveBankSoundBank", bankNameSize);
        AppendPadded(data, "GhostWaveBank", 64); // never registered with SharedEngine()

        AppendU8(data, 0);    // flags
        AppendU16(data, 0);   // categoryIndex
        AppendU8(data, 0xFF); // volume raw byte
        AppendU16(data, 0);   // pitchCents
        AppendU8(data, 0);    // priority
        AppendU16(data, 0);   // soundLength (skipped)
        AppendU16(data, 0);   // waveIdx
        AppendU8(data, 0);    // wbIdx (index 0 -> "GhostWaveBank")

        AppendU8(data, 0);
        AppendU32(data, soundOffset);

        AppendU32(data, cueNameStrOffset);
        AppendU16(data, 0);

        AppendCStr(data, cueName);

        return data;
    }

    // Same layout as BuildLongXsbFixtureBytes, but the one real sound's waveIdx is out of range
    // for LongWaveBank (which has exactly 1 entry, index 0) -- simulates a corrupt/malformed
    // wave reference within an otherwise-valid, registered wave bank. Regression fixture for
    // P9-XACT-014/015.
    std::vector<uint8_t> BuildMissingWaveIndexXsbFixtureBytes()
    {
        constexpr uint32_t headerSize   = 74;
        constexpr uint32_t bankNameSize = 64;
        constexpr uint32_t baseOffset   = headerSize + bankNameSize;

        const uint32_t wavebankNameOffset = baseOffset;
        const uint32_t soundOffset        = wavebankNameOffset + 64;
        const uint32_t cueSimpleOffset    = soundOffset + 12;
        const uint32_t cueNameIndexOffset = cueSimpleOffset + 5;
        const uint32_t cueNameStrOffset   = cueNameIndexOffset + 6;
        const std::string cueName = "MissingWaveIndexCue";

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

        AppendPadded(data, "MissingWaveIndexSoundBank", bankNameSize);
        AppendPadded(data, kLongWaveBankName, 64);

        AppendU8(data, 0);    // flags
        AppendU16(data, 0);   // categoryIndex
        AppendU8(data, 0xFF); // volume raw byte
        AppendU16(data, 0);   // pitchCents
        AppendU8(data, 0);    // priority
        AppendU16(data, 0);   // soundLength (skipped)
        AppendU16(data, 999); // waveIdx -- out of range, LongWaveBank has exactly 1 entry
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

    // .xsb with 2 simple cues ("VolumeRpcCue"/"PitchRpcCue"), both referencing LongWaveBank
    // (real 1-second audio, so a real SoundEffectInstance is spawned) but each sound has
    // SOUND_FLAG_HAS_RPC (0x02) set with a single sound-level RPC code -- kVolumeRpcCode for the
    // first, kPitchRpcCode for the second -- referencing the two curves BuildXgsFixtureBytes
    // appends to fixture.xgs. Regression fixture for P9-XACT-006/007/008/009.
    std::vector<uint8_t> BuildRpcXsbFixtureBytes()
    {
        constexpr uint32_t headerSize   = 74;
        constexpr uint32_t bankNameSize = 64;
        constexpr uint32_t baseOffset   = headerSize + bankNameSize; // 138
        constexpr uint32_t soundSize    = 12 + 7; // simple wave ref (12) + RPC block (2+1+4=7)

        const uint32_t wavebankNameOffset = baseOffset;             // 138
        const uint32_t soundOffset        = wavebankNameOffset + 64; // 202
        const uint32_t sound0Code         = soundOffset;             // 202
        const uint32_t sound1Code         = soundOffset + soundSize; // 221
        const uint32_t cueSimpleOffset    = soundOffset + 2 * soundSize; // 240
        const uint32_t cueNameIndexOffset = cueSimpleOffset + 2 * 5;     // 250 (2 entries, 5 bytes each)
        const uint32_t cueNameStrOffset   = cueNameIndexOffset + 2 * 6;  // 262 (2 entries, 6 bytes each)
        const std::string cueName0        = "VolumeRpcCue";
        const std::string cueName1        = "PitchRpcCue";
        const uint32_t cueName1StrOffset  = cueNameStrOffset + static_cast<uint32_t>(cueName0.size()) + 1;

        std::vector<uint8_t> data;
        const char magic[4] = { 'S', 'D', 'B', 'K' };
        data.insert(data.end(), magic, magic + 4);
        AppendU16(data, 46); // contentVersion
        AppendU16(data, 0);  // toolVersion
        AppendU16(data, 0);  // CRC
        for (int i = 0; i < 8; ++i) data.push_back(0); // lastModified
        AppendU8(data, 0);   // platform

        AppendU16(data, 2); // cueSimpleCount
        AppendU16(data, 0); // cueComplexCount
        AppendU16(data, 0); // unknown
        AppendU16(data, 0); // cueTotalAlign
        AppendU8(data, 1);  // wavebankCount
        AppendU16(data, 2); // soundCount
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

        AppendPadded(data, "RpcSoundBank", bankNameSize);
        AppendPadded(data, kLongWaveBankName, 64);

        // Sound 0: HAS_RPC, bound to the VOLUME curve. Volume raw byte 0x64 (~0.31x amplitude,
        // not 0xFF/~2.0x) so that even the RPC curve's unity-gain (var=1.0) end doesn't saturate
        // the final [0,1] clamp -- "Default" category's own volume is also ~2.0x (0xFF raw byte,
        // shared fixture.xgs), so 0xFF*0xFF*1.0 would clamp and hide the RPC curve's real effect.
        AppendU8(data, 0x02); // flags: SOUND_FLAG_HAS_RPC
        AppendU16(data, 0);   // categoryIndex
        AppendU8(data, 0x64); // volume raw byte
        AppendU16(data, 0);   // pitchCents
        AppendU8(data, 0);    // priority
        AppendU16(data, 0);   // soundLength (skipped)
        AppendU16(data, 0);   // waveIdx
        AppendU8(data, 0);    // wbIdx
        AppendU16(data, 7);   // rpcDataLength (informational, unused): 2+1(count)+4(code)
        AppendU8(data, 1);    // rpc code count
        AppendU32(data, kVolumeRpcCode);

        // Sound 1: HAS_RPC, bound to the PITCH curve.
        AppendU8(data, 0x02);
        AppendU16(data, 0);
        AppendU8(data, 0xFF);
        AppendU16(data, 0);
        AppendU8(data, 0);
        AppendU16(data, 0);
        AppendU16(data, 0);
        AppendU8(data, 0);
        AppendU16(data, 7);
        AppendU8(data, 1);
        AppendU32(data, kPitchRpcCode);

        // Simple cues.
        AppendU8(data, 0);
        AppendU32(data, sound0Code);
        AppendU8(data, 0);
        AppendU32(data, sound1Code);

        AppendU32(data, cueNameStrOffset);
        AppendU16(data, 0);
        AppendU32(data, cueName1StrOffset);
        AppendU16(data, 0);

        AppendCStr(data, cueName0);
        AppendCStr(data, cueName1);

        return data;
    }

    SoundBank& SharedLongBank()
    {
        (void)SharedLongWaveBank(); // must be registered with the engine before GetCue()/Play()
        static SoundBank bank(&SharedEngine(), WriteFixture(
            "cna_cue_test", "long.xsb", BuildLongXsbFixtureBytes()));
        return bank;
    }

    SoundBank& SharedRpcBank()
    {
        (void)SharedLongWaveBank(); // must be registered with the engine before GetCue()/Play()
        static SoundBank bank(&SharedEngine(), WriteFixture(
            "cna_cue_test", "rpc.xsb", BuildRpcXsbFixtureBytes()));
        return bank;
    }

    SoundBank& SharedFilterBank()
    {
        (void)SharedLongWaveBank(); // must be registered with the engine before GetCue()/Play()
        static SoundBank bank(&SharedEngine(), WriteFixture(
            "cna_cue_test", "filter.xsb", BuildFilterXsbFixtureBytes()));
        return bank;
    }

    SoundBank& SharedUnresolvableSoundBank()
    {
        (void)SharedLongWaveBank(); // must be registered with the engine before GetCue()/Play()
        static SoundBank bank(&SharedEngine(), WriteFixture(
            "cna_cue_test", "unresolvable.xsb", BuildUnresolvableSoundXsbFixtureBytes()));
        return bank;
    }

    // Deliberately does NOT depend on SharedLongWaveBank() (or any WaveBank) -- the whole point
    // is that "GhostWaveBank" is never registered with SharedEngine().
    SoundBank& SharedMissingWaveBankBank()
    {
        static SoundBank bank(&SharedEngine(), WriteFixture(
            "cna_cue_test", "missing_wavebank.xsb", BuildMissingWaveBankXsbFixtureBytes()));
        return bank;
    }

    SoundBank& SharedMissingWaveIndexBank()
    {
        (void)SharedLongWaveBank(); // must be registered with the engine before GetCue()/Play()
        static SoundBank bank(&SharedEngine(), WriteFixture(
            "cna_cue_test", "missing_waveindex.xsb", BuildMissingWaveIndexXsbFixtureBytes()));
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

// P9-LIFECYCLE-013: matches real FACT (FACTCue_Pause never clears FACT_STATE_PLAYING) --
// IsPlaying stays true while paused; IsPaused is an independent flag layered on top of it, not a
// separate mutually-exclusive state.
TEST(CueTest, IsPausedTrueAfterPause)
{
    auto cue = MakeCue();
    cue->Play();
    cue->Pause();
    EXPECT_TRUE(cue->getIsPausedProperty());
    EXPECT_TRUE(cue->getIsPlayingProperty());
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
    EXPECT_TRUE(cue->getIsPlayingProperty());
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

        // P9-STOP-001/002/003: matches real FACT (FACTCue_Stop, FACT.c) -- a non-immediate stop
        // with a real tail does NOT reach STOPPED synchronously, it's State::Stopping until the
        // tail actually finishes (see ReconcileState()). The old code set state_=Stopped here
        // unconditionally, which was inconsistent with active_ still being populated.
        EXPECT_TRUE(asAuthoredCue->getIsStoppingProperty());
        EXPECT_FALSE(asAuthoredCue->getIsStoppedProperty());
        EXPECT_FALSE(asAuthoredCue->getIsPlayingProperty());

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
        EXPECT_TRUE(immediateCue->getIsStoppedProperty());
        EXPECT_FALSE(immediateCue->getIsStoppingProperty());
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                        "could not exercise real playback";
    }
}

// P9-STOP-003/004: a State::Stopping cue must reconcile to Stopped on its own once the release
// tail actually finishes playing, the same way a Playing cue naturally reconciles
// (P9-LIFECYCLE-001) -- it must not get stuck in Stopping forever.
TEST(CueTest, StopAsAuthoredTransitionsFromStoppingToStoppedOnceTailFinishes)
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

        cue->Stop(AudioStopOptions::AsAuthored);
        ASSERT_TRUE(cue->getIsStoppingProperty());
        ASSERT_FALSE(cue->getIsStoppedProperty());

        // ~1.13ms of mono 16-bit silence at 44100Hz -- long enough to finish well within 50ms.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        EXPECT_TRUE(cue->getIsStoppedProperty());
        EXPECT_FALSE(cue->getIsStoppingProperty());
        EXPECT_EQ(CueTestAccess::ActiveInstance(*cue, 0), nullptr);
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

// P9-LIFECYCLE-015: GetVariable() had no disposed guard at all, unlike Play()/Apply3D() in this
// same class -- an inconsistency, since a disposed cue's bank_/engine_ pointers happening to
// still be valid was incidental, not a guaranteed contract.
TEST(CueTest, GetVariableAfterDisposeThrowsObjectDisposed)
{
    auto cue = MakeCue();
    cue->Dispose();
    EXPECT_THROW((void)cue->GetVariable("Volume"), System::ObjectDisposedException);
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

// P9-LIFECYCLE-015: see GetVariableAfterDisposeThrowsObjectDisposed above -- same rationale.
TEST(CueTest, SetVariableAfterDisposeThrowsObjectDisposed)
{
    auto cue = MakeCue();
    cue->Dispose();
    EXPECT_THROW(cue->SetVariable("Volume", 0.5f), System::ObjectDisposedException);
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

// P9-LIFECYCLE-013/014: unlike Play()/Apply3D()/GetVariable()/SetVariable(), Pause()/Resume()/
// Stop() deliberately do NOT throw when disposed -- this matches real FACT/FNA exactly:
// FACTCue_Pause/FACTCue_Stop both check `if (pCue == NULL) return <error code>;` (FACT.c), and a
// disposed FNA Cue's handle is IntPtr.Zero, so calling any of these after Dispose() is a silent
// no-op in FNA too, not an exception. Don't "fix" these to throw for consistency with
// GetVariable/SetVariable -- that would be a regression away from FNA, not toward it.
TEST(CueTest, PauseAfterDisposeIsANoOp)
{
    auto cue = MakeCue();
    cue->Play();
    cue->Dispose();
    EXPECT_NO_THROW(cue->Pause());
}

TEST(CueTest, ResumeAfterDisposeIsANoOp)
{
    auto cue = MakeCue();
    cue->Play();
    cue->Dispose();
    EXPECT_NO_THROW(cue->Resume());
}

TEST(CueTest, StopAfterDisposeIsANoOp)
{
    auto cue = MakeCue();
    cue->Play();
    cue->Dispose();
    EXPECT_NO_THROW(cue->Stop(AudioStopOptions::Immediate));
    EXPECT_TRUE(cue->getIsDisposedProperty());
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

// P9-XACT-002/003/004: interactive (type==3) variation tables select by a bound variable's
// current value falling inside an entry's [varMin, varMax] range (FAudio's
// get_active_variation_index, VARIATION_TABLE_TYPE_INTERACTIVE branch), not a weighted lottery.
// fixture_interactive.xsb binds entry 0 to [0.0, 0.4] -> categoryIndex 0, and entry 1 to
// [0.6, 1.0] -> categoryIndex 1, against fixture.xgs's "Volume" variable (index 0).

TEST(CueTest, PlayInteractiveVariationSelectsLowRangeEntryWhenVariableInLowRange)
{
    auto cue = std::unique_ptr<Cue>(SharedInteractiveVariationBank().GetCue("Interactive"));
    cue->SetVariable("Volume", 0.2f);
    cue->Play();
    EXPECT_EQ(CueTestAccess::CategoryIndex(*cue), 0);
}

TEST(CueTest, PlayInteractiveVariationSelectsHighRangeEntryWhenVariableInHighRange)
{
    auto cue = std::unique_ptr<Cue>(SharedInteractiveVariationBank().GetCue("Interactive"));
    cue->SetVariable("Volume", 0.8f);
    cue->Play();
    EXPECT_EQ(CueTestAccess::CategoryIndex(*cue), 1);
}

TEST(CueTest, PlayInteractiveVariationRespectsInclusiveRangeBoundaries)
{
    auto lowBoundaryCue = std::unique_ptr<Cue>(SharedInteractiveVariationBank().GetCue("Interactive"));
    lowBoundaryCue->SetVariable("Volume", 0.4f); // exactly entry 0's varMax
    lowBoundaryCue->Play();
    EXPECT_EQ(CueTestAccess::CategoryIndex(*lowBoundaryCue), 0);

    auto highBoundaryCue = std::unique_ptr<Cue>(SharedInteractiveVariationBank().GetCue("Interactive"));
    highBoundaryCue->SetVariable("Volume", 0.6f); // exactly entry 1's varMin
    highBoundaryCue->Play();
    EXPECT_EQ(CueTestAccess::CategoryIndex(*highBoundaryCue), 1);
}

// FAudio: no entry's range contains the value -> get_active_variation_index() returns false and
// create_sound() aborts entirely -- the cue stays Playing but produces no sound at all, rather
// than falling back to any entry. categoryIdx_'s 0xFFFF default (Cue.hpp) only ever changes once
// a sound is actually resolved, so it staying 0xFFFF proves no entry was picked.
TEST(CueTest, PlayInteractiveVariationWithValueOutsideAllRangesStaysPlayingButSilent)
{
    auto cue = std::unique_ptr<Cue>(SharedInteractiveVariationBank().GetCue("Interactive"));
    cue->SetVariable("Volume", 0.5f); // strictly between entry 0's and entry 1's ranges
    cue->Play();
    EXPECT_TRUE(cue->getIsPlayingProperty());
    EXPECT_EQ(CueTestAccess::CategoryIndex(*cue), 0xFFFF);
}

// ===================== RPC volume/pitch (P9-XACT-006/007/008/009) =====================
//
// fixture.xgs's two RPC curves (see BuildXgsFixtureBytes/kVolumeRpcCode/kPitchRpcCode) are both
// bound to the "Volume" variable: VOLUME maps [0,1] -> [-2000, 0] centibels (a 20dB/10x amplitude
// range), PITCH maps [0,1] -> [-600, +600] cents. "VolumeRpcCue"/"PitchRpcCue" (fixture_rpc's
// real LongWaveBank-backed sounds) reference exactly one of the two, so each test isolates one
// parameter.

TEST(CueTest, PlayScalesVolumeByRpcCurveEvaluatedAtCurrentVariableValue)
{
    auto lowCue = std::unique_ptr<Cue>(SharedRpcBank().GetCue("VolumeRpcCue"));
    lowCue->SetVariable("Volume", 0.0f); // curve -> -2000 centibels -> 0.1x amplitude multiplier
    lowCue->Play();
    auto* lowInst = CueTestAccess::ActiveInstance(*lowCue, 0);
    ASSERT_NE(lowInst, nullptr);
    const float lowVolume = lowInst->getVolumeProperty();
    ASSERT_GT(lowVolume, 0.0f);

    auto highCue = std::unique_ptr<Cue>(SharedRpcBank().GetCue("VolumeRpcCue"));
    highCue->SetVariable("Volume", 1.0f); // curve -> 0 centibels -> unity multiplier
    highCue->Play();
    auto* highInst = CueTestAccess::ActiveInstance(*highCue, 0);
    ASSERT_NE(highInst, nullptr);
    const float highVolume = highInst->getVolumeProperty();

    // -2000 vs 0 centibels is a 20dB (10x amplitude) difference; the ratio is checked (rather
    // than an absolute value) so the test doesn't depend on the unrelated 0xFF volume-byte ->
    // amplitude conversion's exact result.
    EXPECT_NEAR(highVolume / lowVolume, 10.0f, 0.5f);
}

TEST(CueTest, PlayShiftsPitchByRpcCurveEvaluatedAtCurrentVariableValue)
{
    auto lowCue = std::unique_ptr<Cue>(SharedRpcBank().GetCue("PitchRpcCue"));
    lowCue->SetVariable("Volume", 0.0f); // curve -> -600 cents
    lowCue->Play();
    auto* lowInst = CueTestAccess::ActiveInstance(*lowCue, 0);
    ASSERT_NE(lowInst, nullptr);
    EXPECT_NEAR(lowInst->getPitchProperty(), -0.5f, 0.001f); // -600/1200

    auto midCue = std::unique_ptr<Cue>(SharedRpcBank().GetCue("PitchRpcCue"));
    midCue->SetVariable("Volume", 0.5f); // curve -> 0 cents (midpoint)
    midCue->Play();
    auto* midInst = CueTestAccess::ActiveInstance(*midCue, 0);
    ASSERT_NE(midInst, nullptr);
    EXPECT_NEAR(midInst->getPitchProperty(), 0.0f, 0.001f);

    auto highCue = std::unique_ptr<Cue>(SharedRpcBank().GetCue("PitchRpcCue"));
    highCue->SetVariable("Volume", 1.0f); // curve -> +600 cents
    highCue->Play();
    auto* highInst = CueTestAccess::ActiveInstance(*highCue, 0);
    ASSERT_NE(highInst, nullptr);
    EXPECT_NEAR(highInst->getPitchProperty(), 0.5f, 0.001f); // +600/1200
}

// P9-XACT-006: a sound with no RPC codes at all must not be perturbed by unrelated RPC curves
// existing elsewhere in the engine's XGS data.
TEST(CueTest, PlaySoundWithNoRpcCodesIsUnaffectedByEngineRpcCurves)
{
    auto cue = std::unique_ptr<Cue>(SharedLongBank().GetCue("LongCue"));
    cue->Play();
    auto* inst = CueTestAccess::ActiveInstance(*cue, 0);
    ASSERT_NE(inst, nullptr);
    EXPECT_FLOAT_EQ(inst->getPitchProperty(), 0.0f);
}

// P9-XACT-011: end-to-end wiring test -- "FilterCue" (BuildFilterXsbFixtureBytes) is a real
// WaveBank-backed complex sound whose single track carries real parsed high-pass filter data
// (type=2, frequency=8000Hz, qfactor=6 -> oneOverQ=0.5). Play() must reach all the way from
// XactParser's retained XsbWaveRef fields through Cue::Play()'s new
// INTERNAL_applyXactTrackFilter() call into the spawned SoundEffectInstance's real filter state.
TEST(CueTest, PlayWiresRealXactTrackFilterIntoSpawnedInstance)
{
    auto cue = std::unique_ptr<Cue>(SharedFilterBank().GetCue("FilterCue"));
    cue->Play();
    auto* inst = CueTestAccess::ActiveInstance(*cue, 0);
    ASSERT_NE(inst, nullptr);

    int kind = -1; float frequency = -1.0f, oneOverQ = -1.0f;
    SoundEffectInstanceTestAccess::GetFilterState(*inst, kind, frequency, oneOverQ);
    EXPECT_EQ(kind, 2); // FilterState::Kind::HighPass
    EXPECT_NEAR(oneOverQ, 0.5f, 1e-6f); // qfactor=6 -> min(3/6,1)
    EXPECT_GT(frequency, 0.0f);
}

// A cue with no filter data at all ("LongCue") must not spuriously end up with an active filter.
TEST(CueTest, PlaySoundWithNoFilterDataHasNoActiveFilter)
{
    auto cue = std::unique_ptr<Cue>(SharedLongBank().GetCue("LongCue"));
    cue->Play();
    auto* inst = CueTestAccess::ActiveInstance(*cue, 0);
    ASSERT_NE(inst, nullptr);

    int kind = -1; float frequency = -1.0f, oneOverQ = -1.0f;
    SoundEffectInstanceTestAccess::GetFilterState(*inst, kind, frequency, oneOverQ);
    EXPECT_EQ(kind, 0); // FilterState::Kind::None
}

// P9-XACT-014: end-to-end regression for the fix that stopped an unresolvable sound code from
// silently aliasing onto sound index 0. "UnresolvableSoundCue" (BuildUnresolvableSoundXsbFixtureBytes)
// is a valid, name-resolvable cue -- GetCue() must succeed -- but its sound code doesn't match
// the bank's one real sound. Play() must reach the same "no sound found" branch a genuinely
// empty sound table takes (state becomes Playing, no SoundEffectInstance spawned), not silently
// play the unrelated real sound that happens to be sound index 0 in this bank.
TEST(CueTest, PlayWithUnresolvableSoundCodeSpawnsNoInstance)
{
    auto cue = std::unique_ptr<Cue>(SharedUnresolvableSoundBank().GetCue("UnresolvableSoundCue"));
    cue->Play();

    EXPECT_TRUE(cue->getIsPlayingProperty());
    EXPECT_EQ(CueTestAccess::ActiveInstance(*cue, 0), nullptr);
}

// P9-XACT-015: a sound referencing a wave bank name that isn't registered with the AudioEngine
// (e.g. loaded before its dependent WaveBank, or a WaveBank that failed to load) must not crash
// -- Cue::Play()'s FindWaveBank()==nullptr guard already handles this; this pins the behavior
// down with a test. Matches the "no sound found" shape: cue transitions to Playing, no instance
// spawned for the unresolvable wave.
TEST(CueTest, PlayWithUnregisteredWaveBankSpawnsNoInstance)
{
    auto cue = std::unique_ptr<Cue>(SharedMissingWaveBankBank().GetCue("MissingWaveBankCue"));
    cue->Play();

    EXPECT_TRUE(cue->getIsPlayingProperty());
    EXPECT_EQ(CueTestAccess::ActiveInstance(*cue, 0), nullptr);
}

// P9-XACT-015: a sound's waveIdx that's out of range for its (real, registered) wave bank must
// not crash -- WaveBank::GetSoundEffect()'s bounds check already returns nullptr for this; this
// pins the behavior down with a test.
TEST(CueTest, PlayWithOutOfRangeWaveIndexSpawnsNoInstance)
{
    auto cue = std::unique_ptr<Cue>(SharedMissingWaveIndexBank().GetCue("MissingWaveIndexCue"));
    cue->Play();

    EXPECT_TRUE(cue->getIsPlayingProperty());
    EXPECT_EQ(CueTestAccess::ActiveInstance(*cue, 0), nullptr);
}
