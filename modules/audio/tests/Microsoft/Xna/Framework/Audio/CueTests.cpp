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
#include "CNA/Internal/Audio/AudioMixer.hpp"
#include "CueTestAccess.hpp"
#include "SoundEffectInstanceTestAccess.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/Environment.hpp"
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
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <SDL3_mixer/SDL_mixer.h>
#include "System/Environment.hpp"

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

        // Variable: accessibility, initialValue, minValue, maxValue.
        // P12-VAR-001: PUBLIC|CUE (0x5) -- this "Volume" variable is exercised via Cue-level
        // GetVariable/SetVariable AND per-cue RPC curves throughout this file, both of which
        // require the CUE accessibility bit set (matches FACTCue_GetVariableIndex, FACT.c) --
        // an engine-global (PUBLIC, non-CUE) variable is a different, mutually-exclusive domain,
        // never reachable via Cue::GetVariable in real XNA/FACT. Previously 0x03 (PUBLIC|
        // READONLY, CUE clear), an arbitrary nonzero byte chosen before this project enforced
        // accessibility semantics at all.
        AppendU8(data, 0x05);
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

    // P10-VAR-005: one entry of an N-entry non-interactive (SOUND-type) weighted variation
    // table, generalizing BuildXsbFixtureBytesWithWeightedVariation above (which is hardcoded to
    // exactly 2 entries) to any number of entries with independently configurable weights --
    // needed to test 3+ entries, zero-weight entries, and deterministic-by-construction/by-seed
    // edge cases (P9-XACT-001, `plans/plan_audio.md`'s Phase 10 audit).
    struct WeightedVariationEntrySpec
    {
        uint16_t categoryIndex;
        uint8_t  weightMin;
        uint8_t  weightMax;
    };

    std::vector<uint8_t> BuildXsbFixtureBytesWithWeightedVariationN(
        const std::string& cueName, const std::vector<WeightedVariationEntrySpec>& entries)
    {
        constexpr uint32_t headerSize   = 74;
        constexpr uint32_t bankNameSize = 64;
        constexpr uint32_t baseOffset   = headerSize + bankNameSize;
        constexpr uint32_t soundSize    = 12;
        constexpr uint32_t tableHeaderSize = 8;
        constexpr uint32_t tableEntrySize  = 6; // sbCode(4) + weightMin(1) + weightMax(1)

        const auto soundCount = static_cast<uint32_t>(entries.size());
        const uint32_t soundOffset        = baseOffset;
        const uint32_t variationOffset    = soundOffset + soundCount * soundSize;
        const uint32_t tableSize          = tableHeaderSize + soundCount * tableEntrySize;
        const uint32_t cueComplexOffset   = variationOffset + tableSize;
        const uint32_t cueNameIndexOffset = cueComplexOffset + 15;
        const uint32_t cueNameStrOffset   = cueNameIndexOffset + 6;

        std::vector<uint32_t> soundCodes(soundCount);
        for (uint32_t i = 0; i < soundCount; ++i)
            soundCodes[i] = soundOffset + i * soundSize;

        std::vector<uint8_t> data;
        const char magic[4] = { 'S', 'D', 'B', 'K' };
        data.insert(data.end(), magic, magic + 4);
        AppendU16(data, 46); // contentVersion
        AppendU16(data, 0);  // toolVersion
        AppendU16(data, 0);  // CRC
        for (int i = 0; i < 8; ++i) data.push_back(0); // lastModified
        AppendU8(data, 0);   // platform

        AppendU16(data, 0);          // cueSimpleCount
        AppendU16(data, 1);          // cueComplexCount
        AppendU16(data, 0);          // unknown
        AppendU16(data, 0);          // cueTotalAlign
        AppendU8(data, 0);           // wavebankCount
        AppendU16(data, static_cast<uint16_t>(soundCount)); // soundCount
        AppendU16(data, 0);          // cueNameLength
        AppendU16(data, 0);          // unknown

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

        AppendPadded(data, "WeightedNTestBank", bankNameSize);

        for (const auto& e : entries)
        {
            AppendU8(data, 0);              // flags: simple, no RPC/DSP
            AppendU16(data, e.categoryIndex);
            AppendU8(data, 0xFF);           // volume raw byte (unused by this test)
            AppendU16(data, 0);             // pitchCents
            AppendU8(data, 0);              // priority
            AppendU16(data, 0);             // soundLength (skipped)
            AppendU16(data, 0);             // waveIdx
            AppendU8(data, 0);              // wbIdx
        }

        // Variation table: type=SOUND(1), N entries.
        const uint32_t entryCountAndFlags = soundCount | (1u << 19);
        AppendU32(data, entryCountAndFlags);
        AppendU16(data, 0);                          // unknown
        AppendU16(data, static_cast<uint16_t>(-1));   // variable (unused, not interactive)

        for (uint32_t i = 0; i < soundCount; ++i)
        {
            AppendU32(data, soundCodes[i]);
            AppendU8(data, entries[i].weightMin);
            AppendU8(data, entries[i].weightMax);
        }

        // Complex cue: not single-sound, sbCode points at the variation table.
        AppendU8(data, 0);                 // flags (CUE_FLAG_SINGLE_SOUND clear)
        AppendU32(data, variationOffset);  // sbCode
        AppendU32(data, 0);                // transitionOffset
        AppendU8(data, 0xFF);              // instanceLimit
        AppendU16(data, 0);                // fadeInMS
        AppendU16(data, 0);                // fadeOutMS
        AppendU8(data, 0);                 // maxInstanceBehavior

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

    // P10-VAR-005: 4 entries, all weight concentrated on entry 0 (categoryIndex 0), the other
    // three carrying zero weight. Mathematically guaranteed to always select entry 0 regardless
    // of the RNG's drawn value -- entries 1..3 (the only ones the reverse-index loop explicitly
    // checks; see Cue.cpp's comment on FAudio's get_active_variation_index) all contribute zero
    // weight, so `remaining` never shrinks and the `value > (remaining - weight)` check can never
    // pass for them (value is always < remaining, which stays equal to the untouched total
    // weight); the loop always falls through to the implicit default of entry 0. Zero flake risk,
    // no RNG seeding needed.
    const std::string& XsbAllWeightOnFirstEntryFixturePath()
    {
        static const std::string path = WriteFixture(
            "cna_cue_test", "fixture_weighted_first.xsb",
            BuildXsbFixtureBytesWithWeightedVariationN("WeightedFirst", {
                {0, 0, 255}, {1, 0, 0}, {2, 0, 0}, {3, 0, 0},
            }));
        return path;
    }

    SoundBank& SharedAllWeightOnFirstEntryBank()
    {
        static SoundBank bank(&SharedEngine(), XsbAllWeightOnFirstEntryFixturePath());
        return bank;
    }

    // P10-VAR-005: 4 entries -- two zero-weight (categoryIndex 0/1), then weight 30
    // (categoryIndex 2) and weight 70 (categoryIndex 3), total 100. Used with
    // CueTestAccess::SeedRng() for deterministic-by-replica tests: a test seeds the RNG to a
    // fixed value, independently computes the expected pick using the identical
    // std::mt19937 + std::uniform_int_distribution(0, 99) draw and the same reverse-index
    // selection loop as Cue::Play() (Cue.cpp), then asserts Play() actually picked that entry.
    const std::string& XsbFourWeightedEntriesFixturePath()
    {
        static const std::string path = WriteFixture(
            "cna_cue_test", "fixture_weighted_four.xsb",
            BuildXsbFixtureBytesWithWeightedVariationN("WeightedFour", {
                {0, 0, 0}, {1, 0, 0}, {2, 0, 30}, {3, 30, 100},
            }));
        return path;
    }

    SoundBank& SharedFourWeightedEntriesBank()
    {
        static SoundBank bank(&SharedEngine(), XsbFourWeightedEntriesFixturePath());
        return bank;
    }

    // P11-XACT-004: 2 entries of EQUAL weight (1 and 1) -- the discretization case where the
    // pre-fix `>` boundary comparison collapsed to a *total* bias (entry 1 could never be picked
    // at all, see Cue.cpp's comment), unlike every other fixture on this page, which only ever
    // uses skewed weights that mask the same bug as a small statistical error instead of a hard
    // impossibility.
    const std::string& XsbTwoEqualWeightEntriesFixturePath()
    {
        static const std::string path = WriteFixture(
            "cna_cue_test", "fixture_weighted_equal.xsb",
            BuildXsbFixtureBytesWithWeightedVariationN("WeightedEqual", {
                {0, 0, 1}, {1, 0, 1},
            }));
        return path;
    }

    SoundBank& SharedTwoEqualWeightEntriesBank()
    {
        static SoundBank bank(&SharedEngine(), XsbTwoEqualWeightEntriesFixturePath());
        return bank;
    }

    // Independently replicates Cue::Play()'s non-interactive weighted-variation selection
    // (Cue.cpp), for cross-checking against a seeded Cue::Play() outcome in a test. Deliberately
    // separate code, not a call into production -- mirrors FAudio's own get_active_variation_index
    // algorithm (see Cue.cpp's citation), transcribed independently here as a controlled oracle.
    // P11-XACT-004: uses `>=` (not `>`), matching Cue.cpp's own fix -- see its comment for why a
    // strict `>` (FAudio's own boundary check, correct only for FAudio's continuous float draw)
    // is wrong for this discrete integer draw.
    uint16_t PredictWeightedPick(unsigned int seed, const std::vector<uint32_t>& weights)
    {
        uint32_t total = 0;
        for (uint32_t w : weights) total += w;
        if (total == 0) return 0;

        std::mt19937 rng(seed);
        std::uniform_int_distribution<uint32_t> dist(0, total - 1);
        const uint32_t value = dist(rng);

        uint16_t pick = 0;
        uint32_t remaining = total;
        for (int32_t i = static_cast<int32_t>(weights.size()) - 1; i > 0; --i)
        {
            const uint32_t weight = weights[static_cast<std::size_t>(i)];
            if (value >= (remaining - weight))
            {
                pick = static_cast<uint16_t>(i);
                break;
            }
            remaining -= weight;
        }
        return pick;
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

    // AUD-11-016: same layout as BuildApply3DXsbFixtureBytes, but referencing a wave bank name
    // that is deliberately never registered with any engine anywhere in this test binary
    // (guaranteed-unique -- no other fixture in this file uses this name), so
    // Cue::Play()'s `AudioEngine::FindWaveBank()` call is guaranteed to fail and take the
    // "!wb" diagnostic path, regardless of what other tests have already run and registered
    // their own wave banks as persistent function-local statics.
    constexpr const char* kNeverRegisteredWaveBankName = "AUD11016NeverRegisteredWaveBank";

    std::vector<uint8_t> BuildUnresolvableWaveBankXsbFixtureBytes()
    {
        constexpr uint32_t headerSize   = 74;
        constexpr uint32_t bankNameSize = 64;
        constexpr uint32_t baseOffset   = headerSize + bankNameSize;

        const uint32_t wavebankNameOffset = baseOffset;
        const uint32_t soundOffset        = wavebankNameOffset + 64;
        const uint32_t cueSimpleOffset    = soundOffset + 12;
        const uint32_t cueNameIndexOffset = cueSimpleOffset + 5;
        const uint32_t cueNameStrOffset   = cueNameIndexOffset + 6;
        const std::string cueName = "UnresolvableWaveBankCue";

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

        AppendPadded(data, "UnresolvableWaveBankSoundBank", bankNameSize);
        AppendPadded(data, kNeverRegisteredWaveBankName, 64);

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

    SoundBank& SharedUnresolvableWaveBankBank()
    {
        static SoundBank sb(&SharedEngine(), WriteFixture(
            "cna_cue_test", "unresolvable_wavebank.xsb", BuildUnresolvableWaveBankXsbFixtureBytes()));
        return sb;
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

    // Fade duration authored on "LongCue" below (P9-STOP-010) -- deliberately far shorter than
    // LongWaveBank's 1-second wave, so reaching Stopped this fast can only be the authored fade
    // timer elapsing, never the wave naturally finishing. 300ms (not a "round" short value) gives
    // enough margin for a mid-fade checkpoint to sleep well clear of both endpoints under
    // scheduling jitter.
    constexpr uint16_t kLongCueFadeOutMS = 300;

    // Same layout as BuildApply3DXsbFixtureBytes, but referencing LongWaveBank and named "LongCue".
    // Unlike a simple cue, this is a COMPLEX cue (CUE_FLAG_SINGLE_SOUND set) authoring a real
    // fadeOutMS (P9-STOP-010) -- a simple cue's format has no such field at all (always 0,
    // matching FAudio's own hardcoded value), so it can never exercise real Stop(AsAuthored)
    // fade timing.
    std::vector<uint8_t> BuildLongXsbFixtureBytes()
    {
        constexpr uint32_t headerSize   = 74;
        constexpr uint32_t bankNameSize = 64;
        constexpr uint32_t baseOffset   = headerSize + bankNameSize;

        const uint32_t wavebankNameOffset = baseOffset;
        const uint32_t soundOffset        = wavebankNameOffset + 64;
        const uint32_t cueComplexOffset   = soundOffset + 12;
        const uint32_t cueNameIndexOffset = cueComplexOffset + 15;
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

        // Complex cue "LongCue": single-sound (CUE_FLAG_SINGLE_SOUND), sbCode points directly at
        // the one real sound above, authoring a real fadeOutMS.
        AppendU8(data, 0x04); // flags: CUE_FLAG_SINGLE_SOUND
        AppendU32(data, soundOffset); // sbCode
        AppendU32(data, 0);   // transitionOffset
        AppendU8(data, 0xFF); // instanceLimit
        AppendU16(data, 0);   // fadeInMS
        AppendU16(data, kLongCueFadeOutMS); // fadeOutMS
        AppendU8(data, 0);    // maxInstanceBehavior

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

    // ── P10-RPC-003: dedicated "AttackTime"-bound RPC fixture ─────────────────────────────────
    //
    // A separate engine/xgs (not SharedEngine()/BuildXgsFixtureBytes(), whose "Volume" variable
    // and two RPC curves have carefully laid-out, widely-shared byte offsets) with a single
    // variable literally named "AttackTime" and one VOLUME-parameter RPC curve bound to it, x
    // values in raw milliseconds (0ms -> 0 centibels/unity gain, 150ms -> -2000 centibels/0.1x
    // amplitude, linear) -- matches FAudio's FACT_INTERNAL_UpdateRPCs (FACT_internal.c), which
    // feeds elapsed milliseconds directly as an "AttackTime"-bound curve's x-domain value.
    constexpr uint32_t kAttackXgsHeaderSize       = 69;
    constexpr uint32_t kAttackXgsCategoryDataSize = 10;
    constexpr uint32_t kAttackXgsVariableDataSize = 13;
    constexpr uint32_t kAttackXgsCategoryNameSize = 8;  // "Default\0"
    constexpr uint32_t kAttackXgsVariableNameSize = 11; // "AttackTime\0"
    constexpr uint32_t kAttackXgsRpcOffset = kAttackXgsHeaderSize + kAttackXgsCategoryDataSize
        + kAttackXgsVariableDataSize + kAttackXgsCategoryNameSize + kAttackXgsVariableNameSize;
    constexpr uint32_t kAttackVolumeRpcCode = kAttackXgsRpcOffset;

    std::vector<uint8_t> BuildAttackTimeXgsFixtureBytes()
    {
        const uint32_t categoryOffset     = kAttackXgsHeaderSize;
        const uint32_t variableOffset     = categoryOffset + kAttackXgsCategoryDataSize;
        const uint32_t categoryNameOffset = variableOffset + kAttackXgsVariableDataSize;
        const std::string categoryName    = "Default";
        const uint32_t variableNameOffset =
            categoryNameOffset + static_cast<uint32_t>(categoryName.size()) + 1;
        const std::string variableName    = "AttackTime";

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
        AppendU16(data, 1); // rpcCount
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
        AppendU32(data, kAttackXgsRpcOffset);

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
        AppendF32(data, 0.0f);
        AppendF32(data, 0.0f);
        AppendF32(data, 100000.0f);

        AppendCStr(data, categoryName);
        AppendCStr(data, variableName);

        // RPC 0 (VOLUME): variable=0 ("AttackTime"), 2 points, linear, x in milliseconds.
        AppendU16(data, 0); // variable
        AppendU8(data, 2);  // pointCount
        AppendU16(data, 0); // parameter = VOLUME
        AppendF32(data, 0.0f);   AppendF32(data, 0.0f);     AppendU8(data, 0); // point 0: linear
        AppendF32(data, 150.0f); AppendF32(data, -2000.0f); AppendU8(data, 0); // point 1: linear

        return data;
    }

    // .xsb with one simple cue ("AttackTimeCue") referencing LongWaveBank (real 1-second audio,
    // so a real SoundEffectInstance is spawned), SOUND_FLAG_HAS_RPC set with a single sound-level
    // RPC code (kAttackVolumeRpcCode).
    std::vector<uint8_t> BuildAttackTimeXsbFixtureBytes()
    {
        constexpr uint32_t headerSize   = 74;
        constexpr uint32_t bankNameSize = 64;
        constexpr uint32_t baseOffset   = headerSize + bankNameSize; // 138
        constexpr uint32_t soundSize    = 12 + 7; // simple wave ref (12) + RPC block (2+1+4=7)

        const uint32_t wavebankNameOffset = baseOffset;              // 138
        const uint32_t soundOffset        = wavebankNameOffset + 64; // 202
        const uint32_t soundCode          = soundOffset;             // 202
        const uint32_t cueSimpleOffset    = soundOffset + soundSize;     // 221
        const uint32_t cueNameIndexOffset = cueSimpleOffset + 5;         // 226 (1 entry, 5 bytes)
        const uint32_t cueNameStrOffset   = cueNameIndexOffset + 6;      // 232 (1 entry, 6 bytes)
        const std::string cueName         = "AttackTimeCue";

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

        AppendPadded(data, "AttackTimeSoundBank", bankNameSize);
        AppendPadded(data, kLongWaveBankName, 64);

        // Sound 0: HAS_RPC, bound to the AttackTime-driven VOLUME curve. Volume raw byte 0x64
        // (~0.31x amplitude) so the curve's unity-gain (t=0ms) end doesn't saturate the final
        // [0,1] clamp, same reasoning as BuildRpcXsbFixtureBytes's sound 0.
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
        AppendU32(data, kAttackVolumeRpcCode);

        // Simple cue.
        AppendU8(data, 0);
        AppendU32(data, soundCode);

        AppendU32(data, cueNameStrOffset);
        AppendU16(data, 0);

        AppendCStr(data, cueName);

        return data;
    }

    AudioEngine& AttackTimeEngine()
    {
        static AudioEngine engine(WriteFixture(
            "cna_cue_test", "attacktime.xgs", BuildAttackTimeXgsFixtureBytes()));
        return engine;
    }

    WaveBank& AttackTimeWaveBank()
    {
        static WaveBank wb(&AttackTimeEngine(), WriteFixture(
            "cna_cue_test", "attacktime_long.xwb", BuildLongXwbFixtureBytes()));
        return wb;
    }

    SoundBank& AttackTimeBank()
    {
        (void)AttackTimeWaveBank(); // must be registered with the engine before GetCue()/Play()
        static SoundBank bank(&AttackTimeEngine(), WriteFixture(
            "cna_cue_test", "attacktime.xsb", BuildAttackTimeXsbFixtureBytes()));
        return bank;
    }

    // ── P10-RPC-004: dedicated "ReleaseTime"-bound RPC fixture ────────────────────────────────
    //
    // Same shape as the "AttackTime" fixture above, but the single variable/RPC curve is bound
    // to "ReleaseTime" instead: 0ms -> 0 centibels/unity gain, 150ms -> -2000 centibels/0.1x
    // amplitude, linear. Also used to derive maxRpcReleaseTime_ (the curve's last point x, 150)
    // at Play() time -- StopInternal() consults it when this cue's fadeOutMS is 0 (always true
    // for a "simple" cue's format, see BuildAttackTimeXsbFixtureBytes's own comment).
    constexpr uint32_t kReleaseXgsHeaderSize       = 69;
    constexpr uint32_t kReleaseXgsCategoryDataSize = 10;
    constexpr uint32_t kReleaseXgsVariableDataSize = 13;
    constexpr uint32_t kReleaseXgsCategoryNameSize = 8;  // "Default\0"
    constexpr uint32_t kReleaseXgsVariableNameSize = 12; // "ReleaseTime\0"
    constexpr uint32_t kReleaseXgsRpcOffset = kReleaseXgsHeaderSize + kReleaseXgsCategoryDataSize
        + kReleaseXgsVariableDataSize + kReleaseXgsCategoryNameSize + kReleaseXgsVariableNameSize;
    constexpr uint32_t kReleaseVolumeRpcCode = kReleaseXgsRpcOffset;
    // The curve saturates well before the release duration ends (rather than exactly at it) so a
    // test can sleep into a window that is BOTH past full saturation AND safely before the cue
    // reconciles Stopping -> Stopped (which would clear active_ and dangle any instance pointer
    // taken beforehand) -- a single shared saturation point would leave no such safe window.
    constexpr float    kReleaseRpcSaturateX = 30.0f;  // curve reaches its floor (-2000cB) here
    constexpr float    kReleaseRpcMaxX      = 150.0f; // last point x == maxRpcReleaseTime_ (release duration)

    std::vector<uint8_t> BuildReleaseTimeXgsFixtureBytes()
    {
        const uint32_t categoryOffset     = kReleaseXgsHeaderSize;
        const uint32_t variableOffset     = categoryOffset + kReleaseXgsCategoryDataSize;
        const uint32_t categoryNameOffset = variableOffset + kReleaseXgsVariableDataSize;
        const std::string categoryName    = "Default";
        const uint32_t variableNameOffset =
            categoryNameOffset + static_cast<uint32_t>(categoryName.size()) + 1;
        const std::string variableName    = "ReleaseTime";

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
        AppendU16(data, 1); // rpcCount
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
        AppendU32(data, kReleaseXgsRpcOffset);

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
        AppendF32(data, 0.0f);
        AppendF32(data, 0.0f);
        AppendF32(data, 100000.0f);

        AppendCStr(data, categoryName);
        AppendCStr(data, variableName);

        // RPC 0 (VOLUME): variable=0 ("ReleaseTime"), 3 points, linear, x in milliseconds. Ramps
        // from unity gain to -2000cB by kReleaseRpcSaturateX, then holds flat at -2000cB out to
        // kReleaseRpcMaxX (the release duration) -- see kReleaseRpcSaturateX's comment above.
        AppendU16(data, 0); // variable
        AppendU8(data, 3);  // pointCount
        AppendU16(data, 0); // parameter = VOLUME
        AppendF32(data, 0.0f);                AppendF32(data, 0.0f);     AppendU8(data, 0); // point 0: linear
        AppendF32(data, kReleaseRpcSaturateX); AppendF32(data, -2000.0f); AppendU8(data, 0); // point 1: linear
        AppendF32(data, kReleaseRpcMaxX);      AppendF32(data, -2000.0f); AppendU8(data, 0); // point 2: linear

        return data;
    }

    // .xsb with one simple cue ("ReleaseTimeCue") referencing LongWaveBank, SOUND_FLAG_HAS_RPC
    // set with a single sound-level RPC code (kReleaseVolumeRpcCode). A simple cue's format has
    // no fadeOutMS field at all (always 0), so Stop(AsAuthored) on this cue can only get a real
    // tail via the new RPC-only release path (P10-RPC-004), never the authored-fade one --
    // isolating the new behavior from the pre-existing fadeOutMS_ tail.
    std::vector<uint8_t> BuildReleaseTimeXsbFixtureBytes()
    {
        constexpr uint32_t headerSize   = 74;
        constexpr uint32_t bankNameSize = 64;
        constexpr uint32_t baseOffset   = headerSize + bankNameSize; // 138
        constexpr uint32_t soundSize    = 12 + 7; // simple wave ref (12) + RPC block (2+1+4=7)

        const uint32_t wavebankNameOffset = baseOffset;              // 138
        const uint32_t soundOffset        = wavebankNameOffset + 64; // 202
        const uint32_t soundCode          = soundOffset;             // 202
        const uint32_t cueSimpleOffset    = soundOffset + soundSize;     // 221
        const uint32_t cueNameIndexOffset = cueSimpleOffset + 5;         // 226 (1 entry, 5 bytes)
        const uint32_t cueNameStrOffset   = cueNameIndexOffset + 6;      // 232 (1 entry, 6 bytes)
        const std::string cueName         = "ReleaseTimeCue";

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

        AppendPadded(data, "ReleaseTimeSoundBank", bankNameSize);
        AppendPadded(data, kLongWaveBankName, 64);

        // Sound 0: HAS_RPC, bound to the ReleaseTime-driven VOLUME curve. Volume raw byte 0x64
        // (~0.31x amplitude) so the curve's unity-gain (t=0ms) end doesn't saturate the final
        // [0,1] clamp, same reasoning as BuildRpcXsbFixtureBytes's sound 0.
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
        AppendU32(data, kReleaseVolumeRpcCode);

        // Simple cue.
        AppendU8(data, 0);
        AppendU32(data, soundCode);

        AppendU32(data, cueNameStrOffset);
        AppendU16(data, 0);

        AppendCStr(data, cueName);

        return data;
    }

    // .xsb with one COMPLEX cue ("ReleaseTimePrecedenceCue") authoring BOTH a real fadeOutMS
    // (like BuildLongXsbFixtureBytes) AND a sound-level RPC code bound to the same "ReleaseTime"
    // curve (like BuildReleaseTimeXsbFixtureBytes's sound) -- reproduces FAudio's FACTCue_Stop
    // if/else-if between fadeOutMS and maxRpcReleaseTime (FACT.c:2434-2448): whichever this cue
    // authors, the authored fadeOutMS must always win when both are present.
    constexpr uint16_t kReleaseTimePrecedenceFadeOutMS = 300;

    std::vector<uint8_t> BuildReleaseTimePrecedenceXsbFixtureBytes()
    {
        constexpr uint32_t headerSize   = 74;
        constexpr uint32_t bankNameSize = 64;
        constexpr uint32_t baseOffset   = headerSize + bankNameSize; // 138
        constexpr uint32_t soundSize    = 12 + 7; // simple wave ref (12) + RPC block (2+1+4=7)

        const uint32_t wavebankNameOffset = baseOffset;                  // 138
        const uint32_t soundOffset        = wavebankNameOffset + 64;     // 202
        const uint32_t soundCode          = soundOffset;                 // 202
        const uint32_t cueComplexOffset   = soundOffset + soundSize;     // 221
        const uint32_t cueNameIndexOffset = cueComplexOffset + 15;       // 236
        const uint32_t cueNameStrOffset   = cueNameIndexOffset + 6;      // 242
        const std::string cueName         = "ReleaseTimePrecedenceCue";

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

        AppendPadded(data, "ReleaseTimePrecedenceSoundBank", bankNameSize);
        AppendPadded(data, kLongWaveBankName, 64);

        // Sound 0: HAS_RPC (simple, not complex), bound to the same ReleaseTime-driven VOLUME
        // curve as BuildReleaseTimeXsbFixtureBytes's sound.
        AppendU8(data, 0x02); // flags: SOUND_FLAG_HAS_RPC
        AppendU16(data, 0);   // categoryIndex
        AppendU8(data, 0xFF); // volume raw byte
        AppendU16(data, 0);   // pitchCents
        AppendU8(data, 0);    // priority
        AppendU16(data, 0);   // soundLength (skipped)
        AppendU16(data, 0);   // waveIdx
        AppendU8(data, 0);    // wbIdx
        AppendU16(data, 7);   // rpcDataLength (informational, unused): 2+1(count)+4(code)
        AppendU8(data, 1);    // rpc code count
        AppendU32(data, kReleaseVolumeRpcCode);

        // Complex cue: single-sound (CUE_FLAG_SINGLE_SOUND), sbCode points directly at the one
        // real sound above, authoring a real, nonzero fadeOutMS alongside the sound's RPC code.
        AppendU8(data, 0x04); // flags: CUE_FLAG_SINGLE_SOUND
        AppendU32(data, soundCode); // sbCode
        AppendU32(data, 0);   // transitionOffset
        AppendU8(data, 0xFF); // instanceLimit
        AppendU16(data, 0);   // fadeInMS
        AppendU16(data, kReleaseTimePrecedenceFadeOutMS); // fadeOutMS
        AppendU8(data, 0);    // maxInstanceBehavior

        AppendU32(data, cueNameStrOffset);
        AppendU16(data, 0);

        AppendCStr(data, cueName);

        return data;
    }

    AudioEngine& ReleaseTimeEngine()
    {
        static AudioEngine engine(WriteFixture(
            "cna_cue_test", "releasetime.xgs", BuildReleaseTimeXgsFixtureBytes()));
        return engine;
    }

    WaveBank& ReleaseTimeWaveBank()
    {
        static WaveBank wb(&ReleaseTimeEngine(), WriteFixture(
            "cna_cue_test", "releasetime_long.xwb", BuildLongXwbFixtureBytes()));
        return wb;
    }

    SoundBank& ReleaseTimeBank()
    {
        (void)ReleaseTimeWaveBank(); // must be registered with the engine before GetCue()/Play()
        static SoundBank bank(&ReleaseTimeEngine(), WriteFixture(
            "cna_cue_test", "releasetime.xsb", BuildReleaseTimeXsbFixtureBytes()));
        return bank;
    }

    SoundBank& ReleaseTimePrecedenceBank()
    {
        (void)ReleaseTimeWaveBank(); // must be registered with the engine before GetCue()/Play()
        static SoundBank bank(&ReleaseTimeEngine(), WriteFixture(
            "cna_cue_test", "releasetime_precedence.xsb", BuildReleaseTimePrecedenceXsbFixtureBytes()));
        return bank;
    }

    SoundBank& SharedFilterBank()
    {
        (void)SharedLongWaveBank(); // must be registered with the engine before GetCue()/Play()
        static SoundBank bank(&SharedEngine(), WriteFixture(
            "cna_cue_test", "filter.xsb", BuildFilterXsbFixtureBytes()));
        return bank;
    }

    // ── P10-FILTER-002/003/006: dedicated FILTERFREQUENCY-bound RPC fixture ──────────────────
    //
    // Own dedicated engine/xgs (not SharedEngine(), same precedent as AttackTimeBank()) with a
    // single variable literally named "FilterFreq" and one RPC_PARAMETER_FILTERFREQUENCY-
    // targeting curve bound to it: variable value 0.0 -> 2000Hz, 1.0 -> 12000Hz, linear -- the
    // curve's y-axis is itself the desired frequency in Hz, matching FAudio's
    // FACT_INTERNAL_UpdateRPCs feeding the raw curve result straight into
    // FACT_INTERNAL_CalculateFilterFrequency (FACT_internal.c).
    constexpr uint32_t kFilterFreqXgsHeaderSize       = 69;
    constexpr uint32_t kFilterFreqXgsCategoryDataSize = 10;
    constexpr uint32_t kFilterFreqXgsVariableDataSize = 13;
    constexpr uint32_t kFilterFreqXgsCategoryNameSize = 8;  // "Default\0"
    constexpr uint32_t kFilterFreqXgsVariableNameSize = 11; // "FilterFreq\0"
    constexpr uint32_t kFilterFreqXgsRpcOffset = kFilterFreqXgsHeaderSize + kFilterFreqXgsCategoryDataSize
        + kFilterFreqXgsVariableDataSize + kFilterFreqXgsCategoryNameSize + kFilterFreqXgsVariableNameSize;
    constexpr uint32_t kFilterFreqRpcCode = kFilterFreqXgsRpcOffset;
    // The track's own authored (base) filter frequency -- deliberately far from both curve
    // endpoints above, so a test can tell "the RPC override took effect" apart from "the base
    // value is still in play" unambiguously.
    constexpr uint16_t kFilterFreqTrackBaseHz = 500;

    std::vector<uint8_t> BuildFilterFreqRpcXgsFixtureBytes()
    {
        const uint32_t categoryOffset     = kFilterFreqXgsHeaderSize;
        const uint32_t variableOffset     = categoryOffset + kFilterFreqXgsCategoryDataSize;
        const uint32_t categoryNameOffset = variableOffset + kFilterFreqXgsVariableDataSize;
        const std::string categoryName    = "Default";
        const uint32_t variableNameOffset =
            categoryNameOffset + static_cast<uint32_t>(categoryName.size()) + 1;
        const std::string variableName    = "FilterFreq";

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
        AppendU16(data, 1); // rpcCount
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
        AppendU32(data, kFilterFreqXgsRpcOffset);

        // Category: instanceLimit, fadeInMS, fadeOutMS, maxInstanceBehavior(skip), parentIndex, volume, visibility
        AppendU8(data, 0xFF);
        AppendU16(data, 0);
        AppendU16(data, 0);
        AppendU8(data, 0);
        AppendU16(data, 0xFFFF);
        AppendU8(data, 0xFF);
        AppendU8(data, 0);

        // Variable: accessibility, initialValue, minValue, maxValue.
        // P12-VAR-001: PUBLIC|CUE (0x5) -- see BuildXgsFixtureBytes's identical "Volume" fix
        // above for the full rationale; this "FilterFreq" variable is exercised the same way
        // (Cue::SetVariable + a per-cue RPC curve).
        AppendU8(data, 0x05);
        AppendF32(data, 0.0f);
        AppendF32(data, 0.0f);
        AppendF32(data, 1.0f);

        AppendCStr(data, categoryName);
        AppendCStr(data, variableName);

        // RPC 0 (FILTERFREQUENCY): variable=0 ("FilterFreq"), 2 points, linear.
        AppendU16(data, 0); // variable
        AppendU8(data, 2);  // pointCount
        AppendU16(data, 3); // parameter = FILTERFREQUENCY
        AppendF32(data, 0.0f); AppendF32(data, 2000.0f);  AppendU8(data, 0); // point 0: linear
        AppendF32(data, 1.0f); AppendF32(data, 12000.0f); AppendU8(data, 0); // point 1: linear

        return data;
    }

    // .xsb with one simple cue ("FilterFreqRpcCue") pointing at a COMPLEX sound with a single
    // track (referencing LongWaveBank), SOUND_FLAG_HAS_RPC set with a sound-level RPC code
    // (kFilterFreqRpcCode) alongside the track's own real, authored base filter (same
    // filterData=0x0605 high-pass/qfactor=6 encoding as BuildFilterXsbFixtureBytes, but
    // kFilterFreqTrackBaseHz instead of 8000Hz) -- same layout as BuildFilterXsbFixtureBytes with
    // the RPC block (parsed right after trackCount, before per-track metadata, per
    // XactParser.cpp's IN-8-fixed ordering) inserted.
    std::vector<uint8_t> BuildFilterFreqRpcXsbFixtureBytes()
    {
        constexpr uint32_t headerSize   = 74;
        constexpr uint32_t bankNameSize = 64;
        constexpr uint32_t baseOffset   = headerSize + bankNameSize;

        const uint32_t wavebankNameOffset = baseOffset;
        const uint32_t soundOffset        = wavebankNameOffset + 64;
        // flags+cat+vol+pitch+prio+len(9) + trackCount(1) + RPC block(2+1+4=7)
        // + track-meta vol+code+filterData+freq(9)
        constexpr uint32_t soundPrefixSize = 9 + 1 + 7 + 9;
        constexpr uint32_t eventSize       = 16; // one PlayWave event, see BuildPlayWaveEventBytes
        constexpr uint32_t soundSize       = soundPrefixSize + 1 /*eventCount*/ + eventSize;
        const uint32_t trackEventsOffset  = soundOffset + soundPrefixSize;
        const uint32_t cueSimpleOffset    = soundOffset + soundSize;
        const uint32_t cueNameIndexOffset = cueSimpleOffset + 5;
        const uint32_t cueNameStrOffset   = cueNameIndexOffset + 6;
        const std::string cueName = "FilterFreqRpcCue";

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

        AppendPadded(data, "FilterFreqRpcSoundBank", bankNameSize);
        AppendPadded(data, kLongWaveBankName, 64);

        // Sound: COMPLEX | HAS_RPC, one track.
        AppendU8(data, 0x03); // SOUND_FLAG_COMPLEX | SOUND_FLAG_HAS_RPC
        AppendU16(data, 0);   // categoryIndex
        AppendU8(data, 0xFF); // volume raw byte
        AppendU16(data, 0);   // pitchCents
        AppendU8(data, 0);    // priority
        AppendU16(data, 0);   // soundLength (skipped)
        AppendU8(data, 1);    // trackCount

        // Sound-level RPC block (parsed before per-track metadata, XactParser.cpp's IN-8 order).
        AppendU16(data, 7);   // rpcDataLength (informational, unused): 2+1(count)+4(code)
        AppendU8(data, 1);    // rpc code count
        AppendU32(data, kFilterFreqRpcCode);

        // Track metadata: volume byte, code (absolute offset to event array), filterData,
        // frequency. filterData = 0x0605: qfactor=6 (upper byte), bit0=1 (has filter),
        // (filterData>>1)&0x02==2 (high-pass, FAudio's own bit-decode -- P9-XACT-010).
        AppendU8(data, 0xFF); // track volume raw byte
        AppendU32(data, trackEventsOffset);
        AppendU16(data, 0x0605);            // filterData
        AppendU16(data, kFilterFreqTrackBaseHz); // frequency (Hz), the fallback base value

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

    AudioEngine& FilterFreqRpcEngine()
    {
        static AudioEngine engine(WriteFixture(
            "cna_cue_test", "filterfreqrpc.xgs", BuildFilterFreqRpcXgsFixtureBytes()));
        return engine;
    }

    WaveBank& FilterFreqRpcWaveBank()
    {
        static WaveBank wb(&FilterFreqRpcEngine(), WriteFixture(
            "cna_cue_test", "filterfreqrpc_long.xwb", BuildLongXwbFixtureBytes()));
        return wb;
    }

    SoundBank& FilterFreqRpcBank()
    {
        (void)FilterFreqRpcWaveBank(); // must be registered with the engine before GetCue()/Play()
        static SoundBank bank(&FilterFreqRpcEngine(), WriteFixture(
            "cna_cue_test", "filterfreqrpc.xsb", BuildFilterFreqRpcXsbFixtureBytes()));
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

    // ── P9-CATEGORY-011: cue-level instanceLimit/maxInstanceBehavior/fade fixture ─────────────
    //
    // Reuses SharedEngine() (category 0 "Default" has instanceLimit=0xFF, i.e. unlimited --
    // see BuildXgsFixtureBytes above -- so the category-level check never triggers here,
    // isolating cue-level behavior). A dedicated WaveBank/SoundBank with three COMPLEX cue
    // definitions (a simple cue's format has no instanceLimit/fadeInMS/maxInstanceBehavior
    // fields at all, always 0xFF/0/FAIL, matching FACT_internal.c's hardcoded simple-cue
    // defaults):
    //   "FailCue"    instanceLimit=1, FAIL           -- a second instance is rejected outright.
    //   "VictimCue"  instanceLimit=0xFF (unlimited)   -- an unrelated, otherwise-ordinary cue.
    //   "TriggerCue" instanceLimit=1, REPLACE_OLDEST, fadeInMS=fadeOutMS=kCueLimitFadeMS -- a
    //                second instance must evict the OLDEST active cue in the whole SoundBank
    //                (matching FACT_internal.c's handle_instance_limit(cue, NULL), which applies
    //                NO category or same-cue-definition filter when the *cue-level* limit is
    //                what triggered) -- proven by playing VictimCue before TriggerCue's first
    //                instance, so VictimCue (not TriggerCue's own first instance) is the one
    //                that gets evicted.

    constexpr const char* kCueLimitWaveBankName = "CueLimitWaveBank";

    // Same 1-second mono 16-bit silence pattern as BuildLongXwbFixtureBytes -- long enough that
    // natural completion can never race the short fade windows exercised below.
    std::vector<uint8_t> BuildCueLimitXwbFixtureBytes()
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
        AppendPadded(data, kCueLimitWaveBankName, 64);
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

    constexpr uint16_t kCueLimitFadeMS = 60;

    struct CueLimitCueDef
    {
        std::string cueName;
        uint8_t     instanceLimit;
        uint16_t    fadeInMS;
        uint16_t    fadeOutMS;
        uint8_t     maxInstanceBehavior;
    };

    const std::vector<CueLimitCueDef>& CueLimitCueDefs()
    {
        static const std::vector<CueLimitCueDef> defs = {
            {"FailCue",    1,    0,               0,                0}, // FAIL
            {"VictimCue",  0xFF, 0,               0,                0}, // unlimited, unrelated
            {"TriggerCue", 1,    kCueLimitFadeMS, kCueLimitFadeMS,  2}, // REPLACE_OLDEST
        };
        return defs;
    }

    // One simple sound + one COMPLEX cue per CueLimitCueDefs() entry, all referencing wave index
    // 0 of the single wavebank above and category 0 ("Default", unlimited -- see fixture note
    // above). Same overall shape as BuildLongXsbFixtureBytes, generalized to N complex cues.
    std::vector<uint8_t> BuildCueLimitXsbFixtureBytes()
    {
        const auto& defs = CueLimitCueDefs();
        const uint16_t cueCount = static_cast<uint16_t>(defs.size());

        constexpr uint32_t headerSize            = 74;
        constexpr uint32_t bankNameSize           = 64;
        constexpr uint32_t baseOffset             = headerSize + bankNameSize;
        constexpr uint32_t soundSize              = 12;
        constexpr uint32_t cueComplexEntrySize    = 15;
        constexpr uint32_t cueNameIndexEntrySize  = 6;

        const uint32_t wavebankNameOffset = baseOffset;
        const uint32_t soundOffset        = wavebankNameOffset + 64;
        const uint32_t cueComplexOffset   = soundOffset + cueCount * soundSize;
        const uint32_t cueNameIndexOffset = cueComplexOffset + cueCount * cueComplexEntrySize;
        const uint32_t cueNameStrOffset   = cueNameIndexOffset + cueCount * cueNameIndexEntrySize;

        std::vector<uint32_t> nameAbsOffsets(cueCount);
        {
            uint32_t running = cueNameStrOffset;
            for (uint16_t i = 0; i < cueCount; ++i)
            {
                nameAbsOffsets[i] = running;
                running += static_cast<uint32_t>(defs[i].cueName.size()) + 1;
            }
        }

        std::vector<uint32_t> soundAbsOffsets(cueCount);
        for (uint16_t i = 0; i < cueCount; ++i)
            soundAbsOffsets[i] = soundOffset + i * soundSize;

        std::vector<uint8_t> data;
        const char magic[4] = { 'S', 'D', 'B', 'K' };
        data.insert(data.end(), magic, magic + 4);
        AppendU16(data, 46); // contentVersion
        AppendU16(data, 0);  // toolVersion
        AppendU16(data, 0);  // CRC
        for (int i = 0; i < 8; ++i) data.push_back(0); // lastModified
        AppendU8(data, 0);   // platform

        AppendU16(data, 0);        // cueSimpleCount
        AppendU16(data, cueCount); // cueComplexCount
        AppendU16(data, 0);        // unknown
        AppendU16(data, 0);        // cueTotalAlign
        AppendU8(data, 1);         // wavebankCount
        AppendU16(data, cueCount); // soundCount
        AppendU16(data, 0);        // cueNameLength
        AppendU16(data, 0);        // unknown

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

        AppendPadded(data, "CueLimitSoundBank", bankNameSize);
        AppendPadded(data, kCueLimitWaveBankName, 64);

        for (const auto& def : defs)
        {
            (void)def;
            AppendU8(data, 0);    // flags
            AppendU16(data, 0);   // categoryIndex (0 == "Default", unlimited)
            AppendU8(data, 0xFF); // volume raw byte
            AppendU16(data, 0);   // pitchCents
            AppendU8(data, 0);    // priority
            AppendU16(data, 0);   // soundLength (skipped)
            AppendU16(data, 0);   // waveIdx
            AppendU8(data, 0);    // wbIdx
        }

        for (uint16_t i = 0; i < cueCount; ++i)
        {
            const auto& def = defs[i];
            AppendU8(data, 0x04); // flags: CUE_FLAG_SINGLE_SOUND
            AppendU32(data, soundAbsOffsets[i]); // sbCode
            AppendU32(data, 0);   // transitionOffset
            AppendU8(data, def.instanceLimit);
            AppendU16(data, def.fadeInMS);
            AppendU16(data, def.fadeOutMS);
            AppendU8(data, static_cast<uint8_t>(def.maxInstanceBehavior << 3));
        }

        for (uint16_t i = 0; i < cueCount; ++i)
        {
            AppendU32(data, nameAbsOffsets[i]);
            AppendU16(data, 0); // unknown
        }

        for (const auto& def : defs)
            AppendCStr(data, def.cueName);

        return data;
    }

    WaveBank& CueLimitWaveBank()
    {
        static WaveBank wb(&SharedEngine(), WriteFixture(
            "cna_cue_test", "cuelimit.xwb", BuildCueLimitXwbFixtureBytes()));
        return wb;
    }

    SoundBank& CueLimitBank()
    {
        (void)CueLimitWaveBank(); // must be registered with the engine before GetCue()/Play()
        static SoundBank bank(&SharedEngine(), WriteFixture(
            "cna_cue_test", "cuelimit.xsb", BuildCueLimitXsbFixtureBytes()));
        return bank;
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

// P10-AUDIT-002/003: renamed from the stale "IsStoppingIsAlwaysFalse" -- IsStopping is NOT always
// false in general (a real Stopping tail exists for both an authored fadeOutMS_, P9-STOP-010, and
// an RPC-only release, P10-RPC-004; see the many getIsStoppingProperty()==true assertions
// elsewhere in this file, e.g. StopAsAuthoredEntersRpcOnlyReleasePhaseWhenMaxRpcReleaseTimeIsPositive).
// This test only covers the specific case where it IS immediately false: an immediate Stop() on a
// cue whose sound has no authored fade and no RPC-release time at all (MakeCue()'s "Explosion"
// fixture), which hard-stops synchronously with no tail to enter.
TEST(CueTest, IsStoppingIsFalseAfterImmediateStopWithNoTailToRelease)
{
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
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

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
    catch (const std::exception& error)
    {
        // CABI-32: names what it caught. A bare catch(...) reporting "no audio device" is how a
        // contract exception hides as an environment skip -- it happened in this file, to the
        // Apply3D attenuation case below, and cost a real regression its visibility.
        GTEST_SKIP() << "could not exercise real playback (expected: no audio device): "
                     << error.what();
    }
}

// P9-LIFECYCLE-013: a naturally-finished cue must not be resurrected into Paused by a stray
// Pause() call (mirrors FACTCue_Pause rejecting STOPPING/STOPPED cues in FACT.c).
TEST(CueTest, PauseAfterNaturalCompletionIsANoOp)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

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
    catch (const std::exception& error)
    {
        // CABI-32: names what it caught. A bare catch(...) reporting "no audio device" is how a
        // contract exception hides as an environment skip -- it happened in this file, to the
        // Apply3D attenuation case below, and cost a real regression its visibility.
        GTEST_SKIP() << "could not exercise real playback (expected: no audio device): "
                     << error.what();
    }
}

// P9-LIFECYCLE-010/011: FACTCue_Play (FACT.c) silently rejects a cue whose state already has
// PLAYING/STOPPING/STOPPED set; FNA's Cue.Play() discards the return value, so this is a no-op
// from the caller's perspective, not an exception -- and it must not spawn a second, overlapping
// SoundEffectInstance for the same wave reference.
TEST(CueTest, PlayCalledTwiceWhileAlreadyPlayingIsANoOpAndDoesNotDuplicateInstances)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

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
    catch (const std::exception& error)
    {
        // CABI-32: names what it caught. A bare catch(...) reporting "no audio device" is how a
        // contract exception hides as an environment skip -- it happened in this file, to the
        // Apply3D attenuation case below, and cost a real regression its visibility.
        GTEST_SKIP() << "could not exercise real playback (expected: no audio device): "
                     << error.what();
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
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

    // CABI-32: only the fixture acquisition may skip. This whole test used to sit inside one
    // try/catch that turned *any* exception into "no audio device (dummy driver unavailable)",
    // which is how CABI-25 hid here: the is3D gate made Apply3D throw, the catch-all swallowed it,
    // and the test reported a missing device on a machine that has one. Narrowing the guard to the
    // part that genuinely depends on the environment is what makes a contract exception below fail
    // the test instead of quietly excusing it.
    std::unique_ptr<Cue> cue;
    try
    {
        cue.reset(SharedApply3DBank().GetCue("Apply3DCue"));
    }
    catch (const std::exception& error)
    {
        GTEST_SKIP() << "no usable sound bank in this environment: " << error.what();
    }

    // Everything from here down is contract. An exception is a failure, not a skip.
    //
    // Aim before playing: starting playback fixes the choice between 3D and pan, so an instance
    // played first and aimed afterwards refuses -- XNA's own rule. Cue's pending-3D cache exists
    // precisely so this order works before there is an active instance to forward to (see the test
    // below it).
    AudioListener listener; // default position: origin
    AudioEmitter nearEmitter;
    nearEmitter.setPositionProperty({1.0f, 0.0f, 0.0f});
    cue->Apply3D(listener, nearEmitter);

    cue->Play();

    SoundEffectInstance* inst = CueTestAccess::ActiveInstance(*cue, 0);
    if (!inst)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                        "could not create a real SoundEffectInstance";
    }

    MIX_Track* track = SoundEffectInstanceTestAccess::GetTrack(*inst);
    ASSERT_NE(track, nullptr);

    const float nearGain = MIX_GetTrackGain(track);

    AudioEmitter farEmitter;
    farEmitter.setPositionProperty({10000.0f, 0.0f, 0.0f});
    cue->Apply3D(listener, farEmitter);
    const float farGain = MIX_GetTrackGain(track);

    EXPECT_LT(farGain, nearGain);

    cue->Stop(AudioStopOptions::Immediate);
}

// AUDIO-001 finding 4: Cue::Apply3D() called BEFORE the cue's first Play() has nothing in
// active_ to forward to yet, so without Cue's own pending-3D cache this would be silently lost
// -- Play() would create a brand-new instance with no spatial state at all until the next real
// Apply3D() call.
TEST(CueTest, Apply3DBeforePlaySeedsSpatialStateOntoNewlyCreatedInstance)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

    try
    {
        std::unique_ptr<Cue> cue(SharedApply3DBank().GetCue("Apply3DCue"));

        AudioListener listener; // default position: origin
        AudioEmitter farEmitter;
        farEmitter.setPositionProperty({10000.0f, 0.0f, 0.0f}); // far -> strong attenuation
        cue->Apply3D(listener, farEmitter);

        cue->Play();

        SoundEffectInstance* inst = CueTestAccess::ActiveInstance(*cue, 0);
        if (!inst)
        {
            GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                            "could not create a real SoundEffectInstance";
        }
        MIX_Track* track = SoundEffectInstanceTestAccess::GetTrack(*inst);
        ASSERT_NE(track, nullptr);

        // The wave's authored volume clamps to a full-scale 1.0f gain with no Apply3D() in play
        // (same fixture as AttenuatesActiveInstanceTrackGainWithDistance above) -- so a gain
        // measurably below 1.0f here proves the pre-Play() Apply3D() call actually reached this
        // brand-new instance's track, not just that Play() itself succeeded.
        EXPECT_LT(MIX_GetTrackGain(track), 1.0f);

        cue->Stop(AudioStopOptions::Immediate);
    }
    catch (const std::exception& error)
    {
        // CABI-32: names what it caught. A bare catch(...) reporting "no audio device" is how a
        // contract exception hides as an environment skip -- it happened in this file, to the
        // Apply3D attenuation case below, and cost a real regression its visibility.
        GTEST_SKIP() << "could not exercise real playback (expected: no audio device): "
                     << error.what();
    }
}

// XA-6: Stop(AsAuthored) must let the track keep playing (SoundEffectInstance::Stop(false) just
// exits any loop) instead of hard-stopping it -- the old code called active_.clear() right after
// pi.instance->Stop(immediate) unconditionally, destroying every instance (and thus hard-stopping
// its track via ~SoundEffectInstance()'s Dispose() cascade) regardless of `immediate`. Contrasted
// directly against Stop(Immediate), which must still hard-stop right away. LongCue authors a real
// fadeOutMS (P9-STOP-010), which is what makes AsAuthored get a real tail here at all -- see
// StopAsAuthoredOnCueWithNoAuthoredFadeIsImmediate for the no-fade-authored case.
TEST(CueTest, StopAsAuthoredLeavesTrackPlayingButStopImmediateHardStopsRightAway)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

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
    catch (const std::exception& error)
    {
        // CABI-32: names what it caught. A bare catch(...) reporting "no audio device" is how a
        // contract exception hides as an environment skip -- it happened in this file, to the
        // Apply3D attenuation case below, and cost a real regression its visibility.
        GTEST_SKIP() << "could not exercise real playback (expected: no audio device): "
                     << error.what();
    }
}

// P9-STOP-010: a State::Stopping cue with a real authored fadeOutMS must reconcile to Stopped
// once that authored fade *timer* elapses -- driven by real wall-clock time, not by the
// underlying wave naturally finishing. LongCue's wave is a full second long, far longer than its
// ~300ms authored fade, so reaching Stopped this fast can only be the timer, never natural
// completion (the opposite of what this test used to exercise, before P9-STOP-010: FACT actually
// treats a cue with no authored fade -- like the old Apply3DCue-based version of this test -- as
// an *immediate* stop, so it never even reaches Stopping at all; see
// StopAsAuthoredOnCueWithNoAuthoredFadeIsImmediate below).
TEST(CueTest, StopAsAuthoredTransitionsFromStoppingToStoppedOnceFadeTimerElapses)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

    try
    {
        std::unique_ptr<Cue> cue(SharedLongBank().GetCue("LongCue"));
        cue->Play();

        if (!CueTestAccess::ActiveInstance(*cue, 0))
        {
            GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                            "could not create a real SoundEffectInstance";
        }

        cue->Stop(AudioStopOptions::AsAuthored);
        ASSERT_TRUE(cue->getIsStoppingProperty());
        ASSERT_FALSE(cue->getIsStoppedProperty());

        // Comfortably past kLongCueFadeOutMS, nowhere near LongWaveBank's real 1-second wave
        // naturally finishing.
        std::this_thread::sleep_for(std::chrono::milliseconds(kLongCueFadeOutMS + 150));

        EXPECT_TRUE(cue->getIsStoppedProperty());
        EXPECT_FALSE(cue->getIsStoppingProperty());
        EXPECT_EQ(CueTestAccess::ActiveInstance(*cue, 0), nullptr);
    }
    catch (const std::exception& error)
    {
        // CABI-32: names what it caught. A bare catch(...) reporting "no audio device" is how a
        // contract exception hides as an environment skip -- it happened in this file, to the
        // Apply3D attenuation case below, and cost a real regression its visibility.
        GTEST_SKIP() << "could not exercise real playback (expected: no audio device): "
                     << error.what();
    }
}

// P9-STOP-010: real FACT (FACTCue_Stop, FACT.c) treats Stop(AsAuthored) as immediate when the cue
// has no authored fadeOutMS (and no RPC-release timing, which CNA doesn't resolve at all) -- a
// "simple" cue like Apply3DCue can never carry a fadeOutMS at all (the format has no such field
// for it), so AsAuthored must behave exactly like Stop(Immediate) for one.
TEST(CueTest, StopAsAuthoredOnCueWithNoAuthoredFadeIsImmediate)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

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

        EXPECT_TRUE(cue->getIsStoppedProperty());
        EXPECT_FALSE(cue->getIsStoppingProperty());
        EXPECT_EQ(CueTestAccess::ActiveInstance(*cue, 0), nullptr);
    }
    catch (const std::exception& error)
    {
        // CABI-32: names what it caught. A bare catch(...) reporting "no audio device" is how a
        // contract exception hides as an environment skip -- it happened in this file, to the
        // Apply3D attenuation case below, and cost a real regression its visibility.
        GTEST_SKIP() << "could not exercise real playback (expected: no audio device): "
                     << error.what();
    }
}

// P9-STOP-010: a real authored fadeOutMS ramps volume down linearly over that duration, matching
// FACT_INTERNAL_UpdateSound's SOUND_STATE_FADE_OUT handling (FACT_internal.c) -- not an instant
// volume cut, and not tied to the wave's own natural length. Checked at 80% elapsed (not 50%):
// LongCue's sound-level volume byte and category 0's volume byte are both 0xFF, which
// ReadVolByteAsAmplitude (XactParser.cpp) converts to ~1.998 each (XACT volume bytes are a
// log-centibel scale, not a linear 0-255 -> 0-1 one) -- their product (~3.99) is well above the
// [0,1] clamp Cue::Play()/ReconcileState() both apply, so anything above ~25% remaining still
// clamps to a flat 1.0 and wouldn't show a difference at all; 20% remaining (80% elapsed) clamps
// to ~0.8, comfortably observable, with 60ms of margin against the 300ms deadline.
TEST(CueTest, StopAsAuthoredRampsVolumeDownOverAuthoredFadeDuration)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

    try
    {
        std::unique_ptr<Cue> cue(SharedLongBank().GetCue("LongCue"));
        cue->Play();

        SoundEffectInstance* inst = CueTestAccess::ActiveInstance(*cue, 0);
        if (!inst)
        {
            GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                            "could not create a real SoundEffectInstance";
        }
        const float startVolume = inst->getVolumeProperty();
        // P11-TEST-001: exact value -- LongCue's sound volume byte (0xFF, amplitude ~1.9977)
        // combined with the "Default" category's own authored volume byte (also 0xFF) clamps
        // to exactly 1.0, matching this test's own comment above about the clamp.
        ASSERT_FLOAT_EQ(startVolume, 1.0f);

        cue->Stop(AudioStopOptions::AsAuthored);
        std::this_thread::sleep_for(std::chrono::milliseconds(kLongCueFadeOutMS * 4 / 5));
        ASSERT_TRUE(cue->getIsStoppingProperty()); // triggers ReconcileState()'s fade tick

        SoundEffectInstance* stillFading = CueTestAccess::ActiveInstance(*cue, 0);
        ASSERT_NE(stillFading, nullptr);
        const float midVolume = stillFading->getVolumeProperty();
        EXPECT_LT(midVolume, startVolume);
        EXPECT_GT(midVolume, 0.0f);

        cue->Stop(AudioStopOptions::Immediate); // clean up rather than leaking a Stopping cue
    }
    catch (const std::exception& error)
    {
        // CABI-32: names what it caught. A bare catch(...) reporting "no audio device" is how a
        // contract exception hides as an environment skip -- it happened in this file, to the
        // Apply3D attenuation case below, and cost a real regression its visibility.
        GTEST_SKIP() << "could not exercise real playback (expected: no audio device): "
                     << error.what();
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

// P10-VAR-001/002 (2026-07-06 audit): this test previously reused a single `Cue` object across
// all 200 loop iterations, calling `Play()` on it repeatedly. That's not a real bug in the
// selection algorithm itself (see the line-by-line comparison against FAudio's
// get_active_variation_index below, and CHECKLIST.md/plans/plan_audio.md's Phase 10 audit note) -- it's
// a bug in *this test*: neither sound in `fixture_weighted.xsb` references a real WaveBank, so
// `Cue::active_` stays empty after the first `Play()`, `ReconcileState()`'s
// `... || active_.empty()) return;` guard means `state_` can never reconcile back from
// `Playing`, and `Play()`'s own `state_ == State::Playing` guard (P9-LIFECYCLE-010/011, "a Cue
// models exactly one playthrough, not a restartable voice") then silently no-ops every
// subsequent call in the loop. Confirmed by direct instrumentation: all 200 "iterations" read
// back the *identical* categoryIndex from iteration 1 -- this was really a single weighted draw,
// not 200 independent trials, which is what actually made the test's pass/fail outcome depend on
// one random draw's ~1% chance of landing on the low-weight entry (matching NEXT.md's "un-seeded
// RNG" flake description) rather than a smooth statistical average across genuinely independent
// samples. Fixed by creating a fresh `Cue` (a fresh "playthrough", matching how a real game would
// call `SoundBank::PlayCue()` repeatedly, never replaying one `Cue` object) each iteration.
TEST(CueTest, PlayWeightedVariationFavorsHigherWeightEntryStatistically)
{
    constexpr int kIterations = 200;
    int highWeightPicks = 0;
    for (int i = 0; i < kIterations; ++i)
    {
        auto cue = std::unique_ptr<Cue>(SharedWeightedVariationBank().GetCue("Weighted"));
        cue->Play();
        if (CueTestAccess::CategoryIndex(*cue) == 1)
            ++highWeightPicks;
    }

    // Entry 1 carries weight 99 of a total 100 -- it should be picked ~99% of the time.
    // Uniform (unweighted) selection between the two entries would land close to 50%, so an
    // 80% threshold cleanly distinguishes weighted selection from the old uniform pick.
    EXPECT_GT(highWeightPicks, kIterations * 0.8);
}

// P10-VAR-002/005 (2026-07-06 audit): line-by-line comparison of Cue::Play()'s non-interactive
// weighted-lottery loop against FAudio's get_active_variation_index (FACT_internal.c, the
// `else` / "Random" branch) confirms it is a byte-for-byte port: same reverse-index loop bound
// (`i = entryCount-1` down to `i > 0`, entry 0 is an implicit fallback never explicitly checked),
// same single `remaining -= weight` per non-matching iteration (not applied twice in any branch
// -- no such bug exists). **Correction (P11-XACT-004):** the original audit's "same strict `>`
// (not `>=`) boundary comparison, no fix was needed" conclusion was wrong -- it verified the
// comparison *character* matched FAudio's C source, but never accounted for FAudio's own `next`
// being a *continuous* float draw (`FACT_INTERNAL_rng() * max`) where a strict `>` is correct,
// versus this code's *discrete* integer draw, where it isn't: `>` silently steals one unit of
// probability mass from every explicitly-checked entry (transferred to whichever entry is checked
// right after it, ultimately piling up on index 0's implicit fallback), invisible for the skewed
// weights every test below originally used (1-2% relative error) but a *total* bias for
// small/equal weights -- found while adding an equivalent regression test for `P11-XACT-002`'s own
// copy of this exact pattern. Fixed to `>=`, which reproduces FAudio's continuous per-entry
// probability mass exactly (worked out by hand: entry `j`'s discrete slot becomes exactly
// `weight[j]` consecutive integers out of `total`, for any weight distribution). `PredictWeightedPick`
// below was updated to match, and `PlayWeightedVariationWithTwoEqualWeightEntriesSelectsBoth` adds
// the small/equal-weight coverage this file never had. The tests below still exist to lock this
// (corrected) proof down and to give this branch real, deterministic-by-
// construction and deterministic-by-seed coverage (multiple entries, zero-weight entries) instead
// of only the single pre-existing statistical test above.

// 4 entries, all weight on entry 0 -- see SharedAllWeightOnFirstEntryBank()'s comment for why
// this is mathematically guaranteed, not just statistically likely: entries 1..3 (the only ones
// explicitly checked by the reverse loop) all carry zero weight, so the loop can never break out
// of its default fallback to entry 0. Every one of many fresh-cue trials must pick entry 0.
TEST(CueTest, PlayWeightedVariationWithAllWeightOnFirstEntryAlwaysSelectsItAcrossManyTrials)
{
    constexpr int kIterations = 50;
    for (int i = 0; i < kIterations; ++i)
    {
        auto cue = std::unique_ptr<Cue>(SharedAllWeightOnFirstEntryBank().GetCue("WeightedFirst"));
        cue->Play();
        EXPECT_EQ(CueTestAccess::CategoryIndex(*cue), 0) << "iteration " << i;
    }
}

// 4-entry table (two zero-weight, then weight 30 and weight 70, total 100) cross-checked against
// an independent replica (PredictWeightedPick) of the same selection algorithm, for several fixed
// RNG seeds. Deterministic and reproducible on every run (std::mt19937 is a fully deterministic
// PRNG given a fixed seed -- unlike the un-seeded std::random_device the production code falls
// back to by default), and exercises whatever specific boundary values each seed happens to draw
// against the real `>` comparison in Cue.cpp, not just the two extremes a hand-picked value would
// hit. Covers 3+ entries, zero-weight entries, and non-extreme (30/70, not 1/99) weight ratios in
// one fixture.
TEST(CueTest, PlayWeightedVariationWithFourEntriesMatchesIndependentReplicaForSeededRng)
{
    const std::vector<uint32_t> weights = {0, 0, 30, 70};
    for (unsigned int seed : {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u})
    {
        CueTestAccess::SeedRng(seed);
        const uint16_t expected = PredictWeightedPick(seed, weights);

        auto cue = std::unique_ptr<Cue>(SharedFourWeightedEntriesBank().GetCue("WeightedFour"));
        cue->Play();

        EXPECT_EQ(CueTestAccess::CategoryIndex(*cue), expected) << "seed " << seed;
    }
}

// P11-XACT-004: regression test for the discretization bug the pre-fix `>` boundary comparison
// had -- with 2 equal-weight-1 entries (total=2, draw in {0,1}), the old code could NEVER select
// entry 1 (both draws resolved to entry 0), a total, not statistical, bias. 60 fresh-cue trials
// give a (1/2)^60 false-negative probability under the fix, effectively zero.
TEST(CueTest, PlayWeightedVariationWithTwoEqualWeightEntriesSelectsBoth)
{
    CueTestAccess::SeedRng(9001);
    bool sawEntry0 = false, sawEntry1 = false;
    for (int i = 0; i < 60; ++i)
    {
        auto cue = std::unique_ptr<Cue>(SharedTwoEqualWeightEntriesBank().GetCue("WeightedEqual"));
        cue->Play();
        if (CueTestAccess::CategoryIndex(*cue) == 1) sawEntry1 = true; else sawEntry0 = true;
    }
    EXPECT_TRUE(sawEntry0);
    EXPECT_TRUE(sawEntry1);
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

constexpr const char* kTrackVariationWaveBankName = "TrackVariationWaveBank";

// P11-XACT-002: two-entry COMPACT WaveBank backing the PlayWaveTrackVariation-family end-to-end
// test below -- entry 0 ("short") is 200 frames (400 bytes) so a selection of it finishes almost
// immediately, entry 1 ("long") is 1600 frames (3200 bytes, 8x longer) so a selection of it is
// still clearly playing well after the short wave would have ended. Observing
// MIX_GetTrackRemaining() against that gap is how the test infers which candidate got picked
// without needing any other introspection hook.
constexpr uint32_t kTrackVariationShortFrameCount = 200;
constexpr uint32_t kTrackVariationLongFrameCount   = 1600;

std::vector<uint8_t> BuildTrackVariationXwbFixtureBytes()
{
    constexpr uint32_t headerSize        = 48;
    constexpr uint32_t bankDataSize      = 96;
    constexpr uint32_t entryCount        = 2;
    constexpr uint32_t entryMetaDataSize = 4;
    constexpr uint32_t entryMetaSegSize  = entryCount * entryMetaDataSize;
    constexpr uint32_t shortBytes        = kTrackVariationShortFrameCount * 2; // mono 16-bit
    constexpr uint32_t longBytes         = kTrackVariationLongFrameCount * 2;
    constexpr uint32_t waveDataLength    = shortBytes + longBytes;
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
    AppendPadded(data, kTrackVariationWaveBankName, 64);
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

    AppendU32(data, 0u);                        // entry 0 ("short"): offset=0, deviation=0
    AppendU32(data, shortBytes / alignment);     // entry 1 ("long"): offset=100 units, deviation=0

    for (uint32_t i = 0; i < waveDataLength; ++i)
        data.push_back(0x00); // 16-bit silence

    return data;
}

// .xsb with one simple cue ("TrackVarCue") pointing at a COMPLEX sound with a single track whose
// only event is a PlayWaveTrackVariation (FACTEVENT_PLAYWAVETRACKVARIATION) referencing both
// TrackVariationWaveBank entries with equal weight and variation_type=Random (2) -- so
// Cue::Play() must run the real selection algorithm (SelectTrackVariationIndex) rather than the
// parser's always-entry-0 fallback. Regression fixture for P11-XACT-002.
std::vector<uint8_t> BuildTrackVariationXsbFixtureBytes()
{
    constexpr uint32_t headerSize   = 74;
    constexpr uint32_t bankNameSize = 64;
    constexpr uint32_t baseOffset   = headerSize + bankNameSize;

    const uint32_t wavebankNameOffset = baseOffset;
    const uint32_t soundOffset        = wavebankNameOffset + 64;
    // flags+cat+vol+pitch+prio+len(9) + trackCount(1) + track-meta vol+code+filterData+freq(9)
    constexpr uint32_t soundPrefixSize = 9 + 1 + 9;
    // header(evtInfo+randomOffset+separator=7) + flags+loopCount+position+angle(6) +
    // evtInfoInner+unknown(8) + 2 entries * (waveIdx+wbIdx+minWeight+maxWeight=5)
    constexpr uint32_t eventSize       = 7 + 6 + 8 + 2 * 5;
    constexpr uint32_t soundSize       = soundPrefixSize + 1 /*eventCount*/ + eventSize;
    const uint32_t trackEventsOffset  = soundOffset + soundPrefixSize;
    const uint32_t cueSimpleOffset    = soundOffset + soundSize;
    const uint32_t cueNameIndexOffset = cueSimpleOffset + 5;
    const uint32_t cueNameStrOffset   = cueNameIndexOffset + 6;
    const std::string cueName = "TrackVarCue";

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

    AppendPadded(data, "TrackVariationSoundBank", bankNameSize);
    AppendPadded(data, kTrackVariationWaveBankName, 64);

    // Sound: COMPLEX, one track, no filter/RPC.
    AppendU8(data, 0x01); // SOUND_FLAG_COMPLEX
    AppendU16(data, 0);   // categoryIndex
    AppendU8(data, 0xFF); // volume raw byte
    AppendU16(data, 0);   // pitchCents
    AppendU8(data, 0);    // priority
    AppendU16(data, 0);   // soundLength (skipped)
    AppendU8(data, 1);    // trackCount

    AppendU8(data, 0xFF); // track volume raw byte
    AppendU32(data, trackEventsOffset);
    AppendU16(data, 0);   // filterData: bit0=0, no filter
    AppendU16(data, 0);   // frequency

    // Track event array: one PlayWaveTrackVariation event with 2 candidate entries.
    AppendU8(data, 1); // eventCount
    AppendU32(data, 3u); // evtInfo: type=FACTEVENT_PLAYWAVETRACKVARIATION (3), timestamp=0
    AppendU16(data, 0);   // randomOffset
    AppendU8(data, 0xFF); // separator
    AppendU8(data, 0);    // flags
    AppendU8(data, 0);    // loopCount
    AppendU16(data, 0);   // position
    AppendU16(data, 0);   // angle
    // evtInfoInner: waveCount=2 (low16), variation_type=Random (2, bits16-18).
    AppendU32(data, 2u | (2u << 16));
    AppendU32(data, 0); // unknown
    // Entry 0: TrackVariationWaveBank entry 0 ("short"), weight = maxWeight-minWeight = 1.
    AppendU16(data, 0); // waveIdx
    AppendU8(data, 0);  // wbIdx
    AppendU8(data, 0);  // minWeight
    AppendU8(data, 1);  // maxWeight
    // Entry 1: TrackVariationWaveBank entry 1 ("long"), weight = 1.
    AppendU16(data, 1); // waveIdx
    AppendU8(data, 0);  // wbIdx
    AppendU8(data, 0);  // minWeight
    AppendU8(data, 1);  // maxWeight

    // Simple cue.
    AppendU8(data, 0);
    AppendU32(data, soundOffset);

    AppendU32(data, cueNameStrOffset);
    AppendU16(data, 0);

    AppendCStr(data, cueName);

    return data;
}

WaveBank& TrackVariationWaveBank()
{
    static WaveBank wb(&SharedEngine(), WriteFixture(
        "cna_cue_test", "trackvariation.xwb", BuildTrackVariationXwbFixtureBytes()));
    return wb;
}

SoundBank& TrackVariationBank()
{
    (void)TrackVariationWaveBank(); // must be registered with the engine before GetCue()/Play()
    static SoundBank bank(&SharedEngine(), WriteFixture(
        "cna_cue_test", "trackvariation.xsb", BuildTrackVariationXsbFixtureBytes()));
    return bank;
}

// ===================== PlayWaveTrackVariation-family selection (P11-XACT-002) =====================
//
// FAudio's real per-instance selection (FACT_internal.c:730-762's one-time `valuei` init,
// immediately followed by FACT_internal.c:190-247's `FACT_INTERNAL_GetNextWave` unconditional
// re-selection) is a genuine two-step composite, not a single pick. These tests exercise
// `Cue::INTERNAL_selectTrackVariationIndexForTest`'s exact reproduction of that first-activation
// value directly, without needing a full XACT fixture for every algorithm. CNA resolves a
// track-variation event once per fresh `Cue::Play()` (matching the existing "first PlayWave
// event" simplification, not true per-loop-iteration re-selection, which would need a much larger
// per-frame XACT event-scheduling rewrite CNA doesn't have) -- there is no persistent cross-
// `Play()` selection state, so every case here is a "first activation" outcome.

TEST(CueTest, SelectTrackVariationIndexOrderedAlwaysStartsAtEntryZero)
{
    using CNA::Internal::Audio::XsbTrackVariationEntry;
    using CNA::Internal::Audio::XsbTrackVariationType;
    const std::vector<XsbTrackVariationEntry> entries = {
        {0, 0, 10}, {1, 0, 10}, {2, 0, 10},
    };
    // Deterministic -- no RNG involved for pure Ordered's one-time init (-1, wrapping to 0 via
    // GetNextWave's own +1), so every fresh call must land on entry 0, not just "usually".
    for (int i = 0; i < 5; ++i)
        EXPECT_EQ(CueTestAccess::SelectTrackVariationIndex(entries, XsbTrackVariationType::Ordered), 0u);
}

TEST(CueTest, SelectTrackVariationIndexOrderedWithSingleEntryStaysAtZero)
{
    using CNA::Internal::Audio::XsbTrackVariationEntry;
    using CNA::Internal::Audio::XsbTrackVariationType;
    const std::vector<XsbTrackVariationEntry> entries = { {0, 0, 10} };
    EXPECT_EQ(CueTestAccess::SelectTrackVariationIndex(entries, XsbTrackVariationType::Ordered), 0u);
}

TEST(CueTest, SelectTrackVariationIndexOrderedFromRandomIsNotAlwaysEntryZero)
{
    // Distinguishes real randomness (the one-time weighted-random initial offset) from a stuck/
    // regressed implementation that silently behaves like plain Ordered (always 0) -- over 50
    // independent fresh "instances", a real random offset must eventually land somewhere else.
    using CNA::Internal::Audio::XsbTrackVariationEntry;
    using CNA::Internal::Audio::XsbTrackVariationType;
    const std::vector<XsbTrackVariationEntry> entries = {
        {0, 0, 10}, {1, 0, 10}, {2, 0, 10},
    };
    CueTestAccess::SeedRng(12345);
    bool sawNonZero = false;
    for (int i = 0; i < 50; ++i)
    {
        const auto picked =
            CueTestAccess::SelectTrackVariationIndex(entries, XsbTrackVariationType::OrderedFromRandom);
        ASSERT_LT(picked, entries.size());
        if (picked != 0) sawNonZero = true;
    }
    EXPECT_TRUE(sawNonZero);
}

TEST(CueTest, SelectTrackVariationIndexRandomFavorsHigherWeightEntryStatistically)
{
    // Same 80%-threshold style as PlayWeightedVariationFavorsHigherWeightEntryStatistically
    // above, for the equivalent per-track selection algorithm.
    using CNA::Internal::Audio::XsbTrackVariationEntry;
    using CNA::Internal::Audio::XsbTrackVariationType;
    const std::vector<XsbTrackVariationEntry> entries = { {0, 0, 1}, {1, 0, 99} };
    CueTestAccess::SeedRng(777);
    constexpr int kIterations = 200;
    int highWeightPicks = 0;
    for (int i = 0; i < kIterations; ++i)
        if (CueTestAccess::SelectTrackVariationIndex(entries, XsbTrackVariationType::Random) == 1)
            ++highWeightPicks;
    EXPECT_GT(highWeightPicks, kIterations * 0.8);
}

// RandomNoRepeats/Shuffle's real "exclude the previous pick" semantic only ever excludes the
// *step-1* throwaway draw here (there is no genuine cross-Play() previous pick to exclude, per
// this task's own documented scope) -- with only 2 entries, excluding step 1's (heavily
// weight-favored) draw *inverts* the naive weight expectation for the final pick, so this
// deliberately does not assert a specific ratio, only that the algorithm explores more than one
// value and never goes out of bounds (a stuck-on-one-value or crashing regression would fail this).
TEST(CueTest, SelectTrackVariationIndexRandomNoRepeatsAndShuffleStayInBoundsAndVary)
{
    using CNA::Internal::Audio::XsbTrackVariationEntry;
    using CNA::Internal::Audio::XsbTrackVariationType;
    const std::vector<XsbTrackVariationEntry> entries = { {0, 0, 1}, {1, 0, 1}, {2, 0, 98} };
    CueTestAccess::SeedRng(4242);
    constexpr int kIterations = 200;
    bool sawNoRepeatsVariety = false, sawShuffleVariety = false;
    std::size_t firstNoRepeats = entries.size(), firstShuffle = entries.size();
    for (int i = 0; i < kIterations; ++i)
    {
        const auto noRepeatsPick =
            CueTestAccess::SelectTrackVariationIndex(entries, XsbTrackVariationType::RandomNoRepeats);
        const auto shufflePick =
            CueTestAccess::SelectTrackVariationIndex(entries, XsbTrackVariationType::Shuffle);
        ASSERT_LT(noRepeatsPick, entries.size());
        ASSERT_LT(shufflePick, entries.size());
        if (firstNoRepeats == entries.size()) firstNoRepeats = noRepeatsPick;
        if (firstShuffle == entries.size()) firstShuffle = shufflePick;
        if (noRepeatsPick != firstNoRepeats) sawNoRepeatsVariety = true;
        if (shufflePick != firstShuffle) sawShuffleVariety = true;
    }
    EXPECT_TRUE(sawNoRepeatsVariety);
    EXPECT_TRUE(sawShuffleVariety);
}

// End-to-end: proves Cue::Play() actually wires the new selection into wave resolution, not just
// that the algorithm itself (tested directly above) is correct in isolation. "TrackVarCue"
// references a 2-entry PlayWaveTrackVariation event pointing at two DISTINCT wave bank entries of
// very different lengths (see BuildTrackVariationXwbFixtureBytes) -- which underlying wave got
// resolved is observable via MIX_GetTrackRemaining() (frames left to play, ~the whole wave's
// length immediately after Play()), since the two entries' lengths differ by a wide margin.
TEST(CueTest, PlayResolvesTrackVariationEventToOneOfTheAuthoredCandidates)
{
    CueTestAccess::SeedRng(2024);
    bool sawShort = false, sawLong = false;
    for (int i = 0; i < 40; ++i)
    {
        auto cue = std::unique_ptr<Cue>(TrackVariationBank().GetCue("TrackVarCue"));
        cue->Play();
        auto* inst = CueTestAccess::ActiveInstance(*cue, 0);
        if (!inst)
        {
            GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                            "could not create a real SoundEffectInstance";
        }
        MIX_Track* track = SoundEffectInstanceTestAccess::GetTrack(*inst);
        ASSERT_NE(track, nullptr);
        const Sint64 remaining = MIX_GetTrackRemaining(track);
        // Long entry is 8x the short entry's frame count (see the fixture builder) -- comfortably
        // distinguishable by a simple midpoint threshold regardless of exact mixer startup slop.
        if (remaining > kTrackVariationShortFrameCount * 4) sawLong = true; else sawShort = true;
        cue->Stop(AudioStopOptions::Immediate);
    }
    EXPECT_TRUE(sawShort);
    EXPECT_TRUE(sawLong);
}

// ===================== PlayWaveEffectVariation-family randomization (P11-XACT-003) =====================
//
// Mirrors FAudio's real per-play pitch/volume/filter randomization (FACT_internal.c:309-425)
// exactly for the "Initial Variation" branch (activeWave.wave == NULL) -- CNA's per-track single-
// resolution model (see plans/plan_audio.md's P11-XACT-003 note) only ever reaches that branch, so the
// NEW_ON_LOOP/_ADD combination logic for a later loop iteration re-triggering the same event is
// out of scope, not simplified away. These tests exercise
// CueTestAccess::ApplyEffectVariation's exact reproduction of that one-time draw directly, without
// needing a full XACT fixture for every flag combination.

// FAudio's own VARIATION_FLAG_* bit values (FACT_internal.h) -- not exposed by CNA's public/test
// headers (Cue.cpp keeps its own private copy), so tests build synthetic flags the same way
// fixture builders elsewhere in this file hardcode other protocol-level constants directly.
constexpr uint16_t kEffectVarFlagPitch      = 0x1000;
constexpr uint16_t kEffectVarFlagVolume     = 0x2000;
constexpr uint16_t kEffectVarFlagFrequencyQ = 0xC000;

CNA::Internal::Audio::XsbWaveRef MakeEffectVariationWaveRef()
{
    CNA::Internal::Audio::XsbWaveRef wr{};
    wr.wavebankIndex = 0;
    wr.waveIndex     = 0;
    wr.loopCount     = 0;
    wr.volume        = 1.0f;
    return wr;
}

TEST(CueTest, ApplyEffectVariationWithNoFlagsIsANoOp)
{
    auto wr = MakeEffectVariationWaveRef();
    const auto result = CueTestAccess::ApplyEffectVariation(wr);
    EXPECT_FLOAT_EQ(result.pitchCentsDelta, 0.0f);
    EXPECT_FLOAT_EQ(result.volumeAmplitudeMultiplier, 1.0f);
    EXPECT_FALSE(result.hasFilterOverride);
}

TEST(CueTest, ApplyEffectVariationPitchStaysWithinAuthoredRangeAndVaries)
{
    auto wr = MakeEffectVariationWaveRef();
    wr.effectVariationFlags = kEffectVarFlagPitch;
    wr.effectMinPitch = -300;
    wr.effectMaxPitch = 300;

    CueTestAccess::SeedRng(55);
    bool sawNegative = false, sawPositive = false;
    for (int i = 0; i < 60; ++i)
    {
        const auto result = CueTestAccess::ApplyEffectVariation(wr);
        ASSERT_GE(result.pitchCentsDelta, -300.0f);
        ASSERT_LE(result.pitchCentsDelta, 300.0f);
        EXPECT_FLOAT_EQ(result.volumeAmplitudeMultiplier, 1.0f); // untouched -- volume flag unset
        EXPECT_FALSE(result.hasFilterOverride);
        if (result.pitchCentsDelta < 0.0f) sawNegative = true;
        if (result.pitchCentsDelta > 0.0f) sawPositive = true;
    }
    EXPECT_TRUE(sawNegative);
    EXPECT_TRUE(sawPositive);
}

TEST(CueTest, ApplyEffectVariationPitchWithDegenerateRangeIsExact)
{
    // min == max collapses the draw to one deterministic value regardless of the RNG --
    // int16(rng() * (600-600)) + 600 == 600 for every possible rng() output.
    auto wr = MakeEffectVariationWaveRef();
    wr.effectVariationFlags = kEffectVarFlagPitch;
    wr.effectMinPitch = 600;
    wr.effectMaxPitch = 600;

    const auto result = CueTestAccess::ApplyEffectVariation(wr);
    EXPECT_FLOAT_EQ(result.pitchCentsDelta, 600.0f);
}

TEST(CueTest, ApplyEffectVariationVolumeStaysWithinAuthoredAmplitudeRangeAndVaries)
{
    // Amplitude is monotonically increasing in centibels (pow(10, x/2000)), so the min/max
    // centibel range maps directly to a min/max amplitude range with no inversion.
    auto wr = MakeEffectVariationWaveRef();
    wr.effectVariationFlags = kEffectVarFlagVolume;
    wr.effectMinVolume = -2000.0f; // centibels
    wr.effectMaxVolume = 0.0f;
    const float minAmp = std::pow(10.0f, -2000.0f / 2000.0f); // 0.1
    const float maxAmp = std::pow(10.0f, 0.0f / 2000.0f);     // 1.0

    CueTestAccess::SeedRng(66);
    bool sawLow = false, sawHigh = false;
    for (int i = 0; i < 60; ++i)
    {
        const auto result = CueTestAccess::ApplyEffectVariation(wr);
        ASSERT_GE(result.volumeAmplitudeMultiplier, minAmp - 1e-5f);
        ASSERT_LE(result.volumeAmplitudeMultiplier, maxAmp + 1e-5f);
        EXPECT_FLOAT_EQ(result.pitchCentsDelta, 0.0f); // untouched -- pitch flag unset
        EXPECT_FALSE(result.hasFilterOverride);
        if (result.volumeAmplitudeMultiplier < (minAmp + maxAmp) / 2.0f) sawLow = true;
        else sawHigh = true;
    }
    EXPECT_TRUE(sawLow);
    EXPECT_TRUE(sawHigh);
}

TEST(CueTest, ApplyEffectVariationFrequencyQStaysWithinAuthoredRangeAndVaries)
{
    // The Q axis takes a reciprocal (matching FAudio's own `1.0f / rngQFactor`), which inverts
    // the ordering: authored [minQ,maxQ] maps to a final oneOverQ range of [1/maxQ, 1/minQ].
    auto wr = MakeEffectVariationWaveRef();
    wr.effectVariationFlags = kEffectVarFlagFrequencyQ;
    wr.effectMinFrequency = 1000.0f;
    wr.effectMaxFrequency = 9000.0f;
    wr.effectMinQFactor = 2.0f;
    wr.effectMaxQFactor = 4.0f;
    const float minOneOverQ = 1.0f / wr.effectMaxQFactor; // 0.25
    const float maxOneOverQ = 1.0f / wr.effectMinQFactor; // 0.5

    CueTestAccess::SeedRng(77);
    bool sawLowFreq = false, sawHighFreq = false;
    for (int i = 0; i < 60; ++i)
    {
        const auto result = CueTestAccess::ApplyEffectVariation(wr);
        ASSERT_TRUE(result.hasFilterOverride);
        ASSERT_GE(result.filterFrequencyHz, 1000.0f);
        ASSERT_LE(result.filterFrequencyHz, 9000.0f);
        ASSERT_GE(result.filterQFactor, minOneOverQ - 1e-5f);
        ASSERT_LE(result.filterQFactor, maxOneOverQ + 1e-5f);
        EXPECT_FLOAT_EQ(result.pitchCentsDelta, 0.0f); // untouched -- pitch flag unset
        EXPECT_FLOAT_EQ(result.volumeAmplitudeMultiplier, 1.0f); // untouched -- volume flag unset
        if (result.filterFrequencyHz < 5000.0f) sawLowFreq = true; else sawHighFreq = true;
    }
    EXPECT_TRUE(sawLowFreq);
    EXPECT_TRUE(sawHighFreq);
}

// End-to-end: proves Cue::Play() actually wires ApplyEffectVariation's draw into the spawned
// SoundEffectInstance for all three axes, not just that the algorithm itself (tested directly
// above) is correct in isolation. "EffectVarCue" authors a PlayWaveEffectVariation event with
// degenerate (min == max) ranges on every axis, so the expected outcome is fully deterministic
// without needing to seed/replicate the RNG at all.
constexpr uint8_t kEffectVarVolByte = 50;

std::vector<uint8_t> BuildEffectVarXsbFixtureBytes()
{
    constexpr uint32_t headerSize   = 74;
    constexpr uint32_t bankNameSize = 64;
    constexpr uint32_t baseOffset   = headerSize + bankNameSize;

    const uint32_t wavebankNameOffset = baseOffset;
    const uint32_t soundOffset        = wavebankNameOffset + 64;
    // flags+cat+vol+pitch+prio+len(9) + trackCount(1) + track-meta vol+code+filterData+freq(9)
    constexpr uint32_t soundPrefixSize = 9 + 1 + 9;
    // header(evtInfo+randomOffset+separator=7) + flags+waveIdx+wbIdx+loopCnt+position+angle(9) +
    // minPitch+maxPitch(4) + minVol+maxVol(2) + minFreq+maxFreq+minQ+maxQ(16) + variationFlags(2)
    constexpr uint32_t eventSize       = 7 + 9 + 4 + 2 + 16 + 2;
    constexpr uint32_t soundSize       = soundPrefixSize + 1 /*eventCount*/ + eventSize;
    const uint32_t trackEventsOffset  = soundOffset + soundPrefixSize;
    const uint32_t cueSimpleOffset    = soundOffset + soundSize;
    const uint32_t cueNameIndexOffset = cueSimpleOffset + 5;
    const uint32_t cueNameStrOffset   = cueNameIndexOffset + 6;
    const std::string cueName = "EffectVarCue";

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

    AppendPadded(data, "EffectVarSoundBank", bankNameSize);
    AppendPadded(data, kLongWaveBankName, 64);

    // Sound: COMPLEX, one track, no RPC.
    AppendU8(data, 0x01); // SOUND_FLAG_COMPLEX
    AppendU16(data, 0);   // categoryIndex
    AppendU8(data, 0xFF); // sound volume raw byte
    AppendU16(data, 0);   // pitchCents (0 -- effect variation's pitch delta is the only source)
    AppendU8(data, 0);    // priority
    AppendU16(data, 0);   // soundLength (skipped)
    AppendU8(data, 1);    // trackCount

    // Track metadata: high-pass filter, qfactor=6/frequency=8000Hz -- both deliberately different
    // from the event's own randomized frequency/Q (5000Hz/qfactor=4 below), so the test can prove
    // the event's values *replaced* these plain authored ones rather than only reading them.
    AppendU8(data, 0xFF); // track volume raw byte
    AppendU32(data, trackEventsOffset);
    AppendU16(data, 0x0605); // filterData: qfactor=6 (upper byte), bit0=1, high-pass bit set
    AppendU16(data, 8000);   // frequency (Hz), plain base (expected to be overridden)

    // Track event array: one PlayWaveEffectVariation event, degenerate (min==max) on every axis.
    AppendU8(data, 1); // eventCount
    AppendU32(data, 4u); // evtInfo: type=FACTEVENT_PLAYWAVEEFFECTVARIATION (4), timestamp=0
    AppendU16(data, 0);    // randomOffset
    AppendU8(data, 0xFF);  // separator
    AppendU8(data, 0);     // flags
    AppendU16(data, 0);    // waveIdx
    AppendU8(data, 0);     // wbIdx
    AppendU8(data, 0);     // loopCount
    AppendU16(data, 0);    // position
    AppendU16(data, 0);    // angle
    AppendU16(data, static_cast<uint16_t>(600));  // minPitch (int16, +600 cents)
    AppendU16(data, static_cast<uint16_t>(600));  // maxPitch
    AppendU8(data, kEffectVarVolByte);            // minVol (raw byte -> centibels)
    AppendU8(data, kEffectVarVolByte);            // maxVol
    AppendF32(data, 5000.0f);                     // minFreq (Hz)
    AppendF32(data, 5000.0f);                     // maxFreq
    AppendF32(data, 4.0f);                        // minQFactor
    AppendF32(data, 4.0f);                        // maxQFactor
    AppendU16(data, kEffectVarFlagPitch | kEffectVarFlagVolume | kEffectVarFlagFrequencyQ);

    // Simple cue.
    AppendU8(data, 0);
    AppendU32(data, soundOffset);

    AppendU32(data, cueNameStrOffset);
    AppendU16(data, 0);

    AppendCStr(data, cueName);

    return data;
}

SoundBank& SharedEffectVariationBank()
{
    (void)SharedLongWaveBank(); // must be registered with the engine before GetCue()/Play()
    static SoundBank bank(&SharedEngine(), WriteFixture(
        "cna_cue_test", "effectvar.xsb", BuildEffectVarXsbFixtureBytes()));
    return bank;
}

TEST(CueTest, PlayWiresEffectVariationPitchIntoSpawnedInstance)
{
    auto cue = std::unique_ptr<Cue>(SharedEffectVariationBank().GetCue("EffectVarCue"));
    cue->Play();
    auto* inst = CueTestAccess::ActiveInstance(*cue, 0);
    ASSERT_NE(inst, nullptr);

    // sound.pitchCents == 0 and no RPC codes -> the only pitch contribution is the event's own
    // degenerate +600-cent draw: CentsToPitch(0 + 600) == 0.5 exactly.
    EXPECT_FLOAT_EQ(inst->getPitchProperty(), 0.5f);
}

TEST(CueTest, PlayWiresEffectVariationVolumeIntoSpawnedInstance)
{
    auto cue = std::unique_ptr<Cue>(SharedEffectVariationBank().GetCue("EffectVarCue"));
    cue->Play();
    auto* inst = CueTestAccess::ActiveInstance(*cue, 0);
    ASSERT_NE(inst, nullptr);

    // Independent oracle (deliberately separate code, not a call into production, same
    // convention as PredictWeightedPick above): replicates XactParser.cpp's ReadVolByte/
    // CentibelsToAmplitude formulas by hand to compute the expected combined amplitude --
    // soundVolByte(0xFF) * trackVolByte(0xFF) * eventVolByte(50)'s three independent conversions,
    // multiplied together (matches XactParser.cpp's `wr.volume *= sound.volume` plus this task's
    // own CentibelsToAmplitude(rngVolume) multiplier -- see Cue.cpp's comment on why that's
    // exactly equivalent to FAudio's additive-centibel-then-single-convert combination), times
    // SharedEngine()'s own "Default" category volume byte (also 0xFF, BuildXgsFixtureBytes).
    auto readVolByteCentibels = [](int b) {
        return static_cast<float>(3969.0 * std::log10(b / 28240.0) + 8715.0);
    };
    auto centibelsToAmplitude = [](float cb) {
        return static_cast<float>(std::pow(10.0, cb / 2000.0));
    };
    const float soundAmp = centibelsToAmplitude(readVolByteCentibels(0xFF));
    const float trackAmp = centibelsToAmplitude(readVolByteCentibels(0xFF));
    const float catAmp   = centibelsToAmplitude(readVolByteCentibels(0xFF));
    const float eventMultiplier = centibelsToAmplitude(readVolByteCentibels(kEffectVarVolByte));
    const float expected = std::clamp(soundAmp * trackAmp * catAmp * eventMultiplier, 0.0f, 1.0f);

    EXPECT_NEAR(inst->getVolumeProperty(), expected, 1e-4f);
}

// P14-ORDER-002: Cue::Play()'s per-wave loop now calls INTERNAL_applyEffectVariationFilter()
// BEFORE inst->Play() rather than after (the filter no longer requires a live track_ to establish
// its pending state, only to register the real SDL3_mixer callback) -- this test's assertions
// only pass if that pending-state path actually works, since by the time it runs here the base
// filter/RPC-override calls above it already exercised the exact same no-track-yet code path this
// task added.
TEST(CueTest, PlayWiresEffectVariationFilterFrequencyAndQIntoSpawnedInstance)
{
    auto cue = std::unique_ptr<Cue>(SharedEffectVariationBank().GetCue("EffectVarCue"));
    cue->Play();
    auto* inst = CueTestAccess::ActiveInstance(*cue, 0);
    ASSERT_NE(inst, nullptr);

    int kind = -1; float frequency = -1.0f, oneOverQ = -1.0f;
    SoundEffectInstanceTestAccess::GetFilterState(*inst, kind, frequency, oneOverQ);
    EXPECT_EQ(kind, 2); // FilterState::Kind::HighPass -- from the track's own filterData bits
    // qfactor=4 -> 1/4 = 0.25, NOT the track's plain authored qfactor=6 (which would give 0.5) --
    // proves the event's own randomized Q *replaced* the plain base, not just that a filter of
    // some kind got applied.
    EXPECT_NEAR(oneOverQ, 0.25f, 1e-6f);
    // 5000Hz (the event's own degenerate draw), NOT the track's plain authored 8000Hz.
    SDL_AudioSpec spec{};
    ASSERT_TRUE(MIX_GetMixerFormat(CNA::Internal::Audio::GetMixer(), &spec));
    EXPECT_NEAR(frequency,
                SoundEffectInstanceTestAccess::CalculateFilterCutoff(5000.0f, static_cast<float>(spec.freq)),
                1e-4f);
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

// P9-XACT-016: RPC volume/pitch are now re-evaluated continuously, not just once at Play() time
// (the narrowing P9-XACT-006/007 originally documented). Changing the bound variable AFTER
// Play() must be reflected on the very next tick -- getIsPlayingProperty() calls
// ReconcileState() internally, which is exactly the mechanism AudioEngine::Update() also drives
// every frame, so this doesn't need a real sleep/wait.
TEST(CueTest, ChangingBoundVariableAfterPlayContinuouslyUpdatesVolume)
{
    auto cue = std::unique_ptr<Cue>(SharedRpcBank().GetCue("VolumeRpcCue"));
    cue->SetVariable("Volume", 0.0f); // curve -> -2000 centibels -> ~0.1x amplitude multiplier
    cue->Play();
    auto* inst = CueTestAccess::ActiveInstance(*cue, 0);
    ASSERT_NE(inst, nullptr);
    const float quietVolume = inst->getVolumeProperty();
    ASSERT_GT(quietVolume, 0.0f);

    cue->SetVariable("Volume", 1.0f); // curve -> 0 centibels -> unity multiplier
    ASSERT_TRUE(cue->getIsPlayingProperty()); // ticks ReconcileState()'s continuous RPC re-eval

    const float loudVolume = inst->getVolumeProperty();
    // Same ~20dB (10x amplitude) ratio PlayScalesVolumeByRpcCurveEvaluatedAtCurrentVariableValue
    // checks for the Play()-time snapshot -- this proves the SAME cue's volume tracks a variable
    // change made after it was already playing, not just at the moment Play() was called.
    EXPECT_NEAR(loudVolume / quietVolume, 10.0f, 0.5f);

    cue->Stop(AudioStopOptions::Immediate);
}

TEST(CueTest, ChangingBoundVariableAfterPlayContinuouslyUpdatesPitch)
{
    auto cue = std::unique_ptr<Cue>(SharedRpcBank().GetCue("PitchRpcCue"));
    cue->SetVariable("Volume", 0.0f); // curve -> -600 cents
    cue->Play();
    auto* inst = CueTestAccess::ActiveInstance(*cue, 0);
    ASSERT_NE(inst, nullptr);
    EXPECT_NEAR(inst->getPitchProperty(), -0.5f, 0.001f); // -600/1200

    cue->SetVariable("Volume", 1.0f); // curve -> +600 cents
    ASSERT_TRUE(cue->getIsPlayingProperty()); // ticks ReconcileState()'s continuous RPC re-eval

    EXPECT_NEAR(inst->getPitchProperty(), 0.5f, 0.001f); // +600/1200

    cue->Stop(AudioStopOptions::Immediate);
}

// AUD-10-013 (2026-07-17 deep audit): "Verify pitch does not get reapplied on repeated Update
// calls -- stable parameter values cannot accumulate ratio exponentially." Confirmed by reading
// Cue.cpp that ReconcileState()'s per-tick RPC re-evaluation always recomputes
// `pitchCentsSum = basePitchCents_ + rpcPitchCents` fresh from the constant, once-per-Play()
// `basePitchCents_` member (a plain assignment, never `+=`) plus a freshly re-evaluated RPC curve
// value -- not an incremental delta applied on top of the previous tick's already-converted
// result, so there is no code path that could compound. This test locks that down empirically:
// many ReconcileState() ticks (via repeated getIsPlayingProperty() calls, matching how
// AudioEngine::Update() drives it every real frame) with the bound variable held CONSTANT must
// leave the pitch bit-for-bit identical across every tick, not drifting toward +-1.0 or beyond.
TEST(CueTest, RepeatedReconcileStateTicksWithConstantVariableDoNotDriftPitch)
{
    auto cue = std::unique_ptr<Cue>(SharedRpcBank().GetCue("PitchRpcCue"));
    cue->SetVariable("Volume", 0.75f); // curve -> +300 cents (some non-edge value)
    cue->Play();
    auto* inst = CueTestAccess::ActiveInstance(*cue, 0);
    ASSERT_NE(inst, nullptr);

    const float firstTickPitch = inst->getPitchProperty();
    EXPECT_NEAR(firstTickPitch, 0.25f, 0.001f); // +300/1200

    for (int i = 0; i < 50; ++i)
    {
        ASSERT_TRUE(cue->getIsPlayingProperty()); // ticks ReconcileState() each iteration
        EXPECT_FLOAT_EQ(inst->getPitchProperty(), firstTickPitch)
            << "pitch drifted after " << (i + 1) << " ReconcileState() tick(s) with no variable "
               "change -- possible cents/ratio accumulation bug";
    }

    cue->Stop(AudioStopOptions::Immediate);
}

// ===================== AttackTime/ReleaseTime built-in RPC variables (P10-RPC-003) =====================
//
// AttackTimeBank()'s "AttackTimeCue" (its own dedicated engine/xgs, see BuildAttackTimeXgsFixtureBytes)
// has a single VOLUME-parameter RPC curve bound to the built-in "AttackTime" variable: 0ms -> 0
// centibels (unity gain), 150ms -> -2000 centibels (0.1x amplitude), linear, clamped beyond 150ms.

TEST(CueTest, PlayAttackTimeRpcCurveTracksElapsedTimeSincePlay)
{
    auto cue = std::unique_ptr<Cue>(AttackTimeBank().GetCue("AttackTimeCue"));
    cue->Play();
    auto* inst = CueTestAccess::ActiveInstance(*cue, 0);
    ASSERT_NE(inst, nullptr);
    const float initialVolume = inst->getVolumeProperty(); // t≈0ms -> curve's unity-gain end
    ASSERT_GT(initialVolume, 0.0f);

    std::this_thread::sleep_for(std::chrono::milliseconds(250)); // past the curve's 150ms domain
    ASSERT_TRUE(cue->getIsPlayingProperty()); // ticks ReconcileState()'s continuous RPC re-eval
    const float laterVolume = inst->getVolumeProperty();

    // -2000 vs 0 centibels is a 20dB (10x amplitude) difference, same ratio check
    // PlayScalesVolumeByRpcCurveEvaluatedAtCurrentVariableValue uses for an explicitly-set
    // variable -- here the "variable" is real elapsed wall-clock time instead.
    EXPECT_NEAR(initialVolume / laterVolume, 10.0f, 1.5f);

    cue->Stop(AudioStopOptions::Immediate);
}

// P10-RPC-003: matches real FACTCue_GetVariable (FACT.c), which reads variableValues[] directly
// and has no "AttackTime"/"ReleaseTime" special case at all -- only RPC curve evaluation
// (FACT_INTERNAL_UpdateRPCs) ever substitutes the live value. A direct GetVariable() call (with
// no prior SetVariable()) always reads back the built-in default, never live elapsed time, unlike
// P10-RPC-002's three 3D variables (which Apply3D() explicitly writes into variables_).
TEST(CueTest, GetVariableAttackTimeDoesNotReflectLiveElapsedTime)
{
    auto cue = std::unique_ptr<Cue>(AttackTimeBank().GetCue("AttackTimeCue"));
    EXPECT_FLOAT_EQ(cue->GetVariable("AttackTime"), 0.0f);

    cue->Play();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_TRUE(cue->getIsPlayingProperty());
    EXPECT_FLOAT_EQ(cue->GetVariable("AttackTime"), 0.0f); // still the default, not the live value

    cue->Stop(AudioStopOptions::Immediate);
}

// "ReleaseTime" is recognized as a valid built-in variable name (matching XACT's always-present
// default variable set) and behaves like an ordinary variable through GetVariable/SetVariable --
// GetVariable() never live-substitutes it (P10-RPC-003's documented asymmetry vs. the three 3D
// variables), regardless of whether a real RPC-only release phase (P10-RPC-004) is active.
TEST(CueTest, ReleaseTimeIsRecognizedAsBuiltInVariable)
{
    auto cue = MakeCue();
    EXPECT_FLOAT_EQ(cue->GetVariable("ReleaseTime"), 0.0f);
    EXPECT_NO_THROW(cue->SetVariable("ReleaseTime", 42.0f));
    EXPECT_FLOAT_EQ(cue->GetVariable("ReleaseTime"), 42.0f);
}

// ===================== RPC-only release timing (P10-RPC-004/007) =====================
//
// ReleaseTimeBank()'s "ReleaseTimeCue" (its own dedicated engine/xgs/xsb, see
// BuildReleaseTimeXgsFixtureBytes/BuildReleaseTimeXsbFixtureBytes) has a single VOLUME-parameter
// RPC curve bound to the built-in "ReleaseTime" variable, matching FAudio's
// FACT_internal.c:790-815's maxRpcReleaseTime scan (variable name "ReleaseTime",
// RPC_PARAMETER_VOLUME): 0ms -> 0 centibels (unity gain), 150ms -> -2000 centibels (0.1x
// amplitude), linear, clamped beyond 150ms. This "simple" cue's format has no fadeOutMS field at
// all (always 0), so any real Stopping tail it gets can only come from the new RPC-only release
// path, never the pre-existing authored-fade one -- isolating the two mechanisms.

// A cue whose resolved sound had a "ReleaseTime"-bound VOLUME RPC curve must NOT stop immediately
// on Stop(AsAuthored) -- matches FAudio's FACTCue_Stop (FACT.c:2434-2448), which enters
// SOUND_STATE_RELEASE_RPC (a real Stopping tail) whenever maxRpcReleaseTime > 0, even with no
// authored fadeOutMS at all.
TEST(CueTest, StopAsAuthoredEntersRpcOnlyReleasePhaseWhenMaxRpcReleaseTimeIsPositive)
{
    auto cue = std::unique_ptr<Cue>(ReleaseTimeBank().GetCue("ReleaseTimeCue"));
    cue->Play();
    ASSERT_TRUE(cue->getIsPlayingProperty());

    cue->Stop(AudioStopOptions::AsAuthored);
    EXPECT_TRUE(cue->getIsStoppingProperty());
    EXPECT_FALSE(cue->getIsStoppedProperty());

    cue->Stop(AudioStopOptions::Immediate); // clean up the still-ringing tail
}

// Once the RPC-only release duration (maxRpcReleaseTime_, derived from the curve's last point x
// -- 150ms here) elapses, the cue must reconcile Stopping -> Stopped on its own, the same way an
// authored fadeOutMS_ tail does (P9-STOP-010) -- matches FACT_INTERNAL_UpdateSound's
// SOUND_STATE_RELEASE_RPC branch (FACT_internal.c), which finishes once
// `timestamp - fadeStart >= fadeTarget`.
TEST(CueTest, StopAsAuthoredReconcilesToStoppedOnceRpcReleaseTimeElapses)
{
    auto cue = std::unique_ptr<Cue>(ReleaseTimeBank().GetCue("ReleaseTimeCue"));
    cue->Play();
    cue->Stop(AudioStopOptions::AsAuthored);
    ASSERT_TRUE(cue->getIsStoppingProperty());

    std::this_thread::sleep_for(std::chrono::milliseconds(
        static_cast<int>(kReleaseRpcMaxX) + 100)); // past the 150ms release duration
    EXPECT_TRUE(cue->getIsStoppedProperty()); // ticks ReconcileState()
    EXPECT_FALSE(cue->getIsPlayingProperty());
    EXPECT_FALSE(cue->getIsStoppingProperty());
}

// While inside the RPC-only release phase, EvaluateRpc()'s "ReleaseTime" substitution feeds real
// elapsed-since-release-started milliseconds into the bound curve (matches
// FACT_INTERNAL_UpdateRPCs, FACT_internal.c:1042-1053) -- so the cue's live volume keeps tracking
// the curve as the release progresses, the same continuous re-evaluation
// ChangingBoundVariableAfterPlayContinuouslyUpdatesVolume/PlayAttackTimeRpcCurveTracksElapsedTimeSincePlay
// already prove for other bindings. Before Stop() is called, "ReleaseTime" always evaluates to
// 0.0f (not yet releasing), landing on the curve's unity-gain (t=0ms) end regardless of how long
// the cue has actually been playing.
TEST(CueTest, ReleaseTimeRpcCurveTracksLiveElapsedTimeDuringReleasePhase)
{
    auto cue = std::unique_ptr<Cue>(ReleaseTimeBank().GetCue("ReleaseTimeCue"));
    cue->Play();
    auto* inst = CueTestAccess::ActiveInstance(*cue, 0);
    ASSERT_NE(inst, nullptr);
    const float preReleaseVolume = inst->getVolumeProperty(); // "ReleaseTime" == 0 -> unity gain
    ASSERT_GT(preReleaseVolume, 0.0f);

    cue->Stop(AudioStopOptions::AsAuthored);
    ASSERT_TRUE(cue->getIsStoppingProperty());

    // Between kReleaseRpcSaturateX (30ms, curve already at its -2000cB floor) and kReleaseRpcMaxX
    // (150ms, when the cue reconciles Stopping -> Stopped and active_ is cleared) -- reading
    // `inst` after that point would dangle, so this window must stay safely inside both bounds.
    std::this_thread::sleep_for(std::chrono::milliseconds(90));
    ASSERT_TRUE(cue->getIsStoppingProperty()); // still releasing; ticks the continuous RPC re-eval

    // -2000 vs 0 centibels is a 20dB (10x amplitude) difference, same ratio check
    // PlayAttackTimeRpcCurveTracksElapsedTimeSincePlay uses for its own elapsed-time-driven curve.
    EXPECT_NEAR(preReleaseVolume / inst->getVolumeProperty(), 10.0f, 1.5f);

    cue->Stop(AudioStopOptions::Immediate);
}

// FAudio's FACTCue_Stop chooses between an authored fadeOutMS tail and an RPC-only release tail
// with an if/else-if (FACT.c:2434-2448): a real, nonzero, authored fadeOutMS always wins even
// when the cue's sound ALSO has a positive maxRpcReleaseTime_. ReleaseTimePrecedenceBank()'s cue
// authors both; this locks down that StopInternal() preserves FAudio's exact precedence rather
// than, say, preferring whichever tail is longer or combining them.
TEST(CueTest, AuthoredFadeOutTakesPrecedenceOverRpcOnlyReleaseWhenBothAreAuthored)
{
    auto cue = std::unique_ptr<Cue>(ReleaseTimePrecedenceBank().GetCue("ReleaseTimePrecedenceCue"));
    cue->Play();
    ASSERT_TRUE(cue->getIsPlayingProperty());

    cue->Stop(AudioStopOptions::AsAuthored);
    ASSERT_TRUE(cue->getIsStoppingProperty());

    // The authored fadeOutMS (300ms) is strictly longer than the RPC release duration (150ms) --
    // if the RPC-only path had won (or the two were combined), the cue would already be Stopped
    // by 200ms. Still Stopping here proves the authored fade, not the RPC release, is driving.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_TRUE(cue->getIsStoppingProperty());
    EXPECT_FALSE(cue->getIsStoppedProperty());

    std::this_thread::sleep_for(std::chrono::milliseconds(
        kReleaseTimePrecedenceFadeOutMS - 200 + 100)); // past the full 300ms authored fade
    EXPECT_TRUE(cue->getIsStoppedProperty());
}

// ===================== Built-in 3D cue variables (P10-RPC-002) =====================
//
// Cue::Apply3D() forwards to SoundEffectInstance::Apply3D() for actual playback (pan/
// attenuation/Doppler), but previously never wrote its own computed distance/angle/Doppler back
// into variables_, so GetVariable("Distance")/"OrientationAngle"/"DopplerPitchScalar" only ever
// reflected a manual SetVariable() call (or the hardcoded 0.0f default) -- never Apply3D's own
// live 3D state, unlike real FAudio's FACT3DApply (FACT3D.c), which writes these three built-in
// variables on every Apply3D call. None of these need cue->Play() first: Apply3D() computes and
// stores them regardless of whether any wave reference is currently active.

TEST(CueTest, Apply3DUpdatesDistanceVariableToReflectLiveComputedDistance)
{
    auto cue = std::unique_ptr<Cue>(SharedLongBank().GetCue("LongCue"));

    AudioListener listener; // default position: origin
    AudioEmitter emitter;
    emitter.setPositionProperty({3.0f, 4.0f, 0.0f}); // 3-4-5 triangle -> distance 5
    cue->Apply3D(listener, emitter);
    EXPECT_FLOAT_EQ(cue->GetVariable("Distance"), 5.0f);

    emitter.setPositionProperty({6.0f, 8.0f, 0.0f}); // same ratio, distance 10
    cue->Apply3D(listener, emitter);
    EXPECT_FLOAT_EQ(cue->GetVariable("Distance"), 10.0f); // proves this tracks the LATEST call, not a one-time snapshot
}

// Same geometry as SoundEffectInstanceTest.Apply3DAppliesDopplerPitchDownWhenEmitterRecedes/
// ...ApproachesL listener at origin, emitter at (10,0,0), moving along X at half the default
// speed of sound (343.5/2 = 171.75) -- proven there to produce dopplerFactor 2/3 (receding) or
// 2.0 (approaching). Reused here to cross-check GetVariable("DopplerPitchScalar") reports the
// exact same raw FACT3DApply-equivalent ratio SoundEffectInstance::Apply3D applies to the track.
TEST(CueTest, Apply3DUpdatesDopplerPitchScalarVariableToReflectLiveRelativeVelocity)
{
    auto cue = std::unique_ptr<Cue>(SharedLongBank().GetCue("LongCue"));

    AudioListener listener; // default position/velocity: origin, stationary
    AudioEmitter emitter;
    emitter.setPositionProperty({10.0f, 0.0f, 0.0f});

    emitter.setVelocityProperty({171.75f, 0.0f, 0.0f}); // moving away from the listener
    cue->Apply3D(listener, emitter);
    EXPECT_NEAR(cue->GetVariable("DopplerPitchScalar"), 2.0f / 3.0f, 1e-4f);

    emitter.setVelocityProperty({-171.75f, 0.0f, 0.0f}); // moving toward the listener
    cue->Apply3D(listener, emitter);
    EXPECT_NEAR(cue->GetVariable("DopplerPitchScalar"), 2.0f, 1e-4f); // proves liveness, not a snapshot
}

// FAudio's F3DAudioCalculate EMITTER_ANGLE branch: angle between the emitter-to-listener
// direction and the emitter's own OrientFront. Default Forward is (0,0,-1) (XNA's
// Vector3.Forward) -- an emitter positioned at +Z relative to the listener faces directly at it
// (angle 0); positioned at -Z, it faces directly away (angle 180).
TEST(CueTest, Apply3DUpdatesOrientationAngleVariableToReflectLiveRelativeFacing)
{
    auto cue = std::unique_ptr<Cue>(SharedLongBank().GetCue("LongCue"));

    AudioListener listener; // default position: origin
    AudioEmitter emitter;   // default forward: (0,0,-1)

    emitter.setPositionProperty({0.0f, 0.0f, 5.0f}); // listener directly ahead of the emitter
    cue->Apply3D(listener, emitter);
    EXPECT_NEAR(cue->GetVariable("OrientationAngle"), 0.0f, 1e-3f);

    emitter.setPositionProperty({0.0f, 0.0f, -5.0f}); // listener directly behind the emitter
    cue->Apply3D(listener, emitter);
    EXPECT_NEAR(cue->GetVariable("OrientationAngle"), 180.0f, 1e-3f); // proves liveness, not a snapshot
}

// P9-XACT-011: end-to-end wiring test -- "FilterCue" (BuildFilterXsbFixtureBytes) is a real
// WaveBank-backed complex sound whose single track carries real parsed high-pass filter data
// (type=2, frequency=8000Hz, qfactor=6 -> oneOverQ=0.5). Play() must reach all the way from
// XactParser's retained XsbWaveRef fields through Cue::Play()'s new
// INTERNAL_applyXactTrackFilter() call into the spawned SoundEffectInstance's real filter state.
// P14-ORDER-002: this call now happens BEFORE the spawned instance's own inst->Play() (see
// Cue::Play()'s per-wave loop) instead of after -- this test's assertions only pass because
// INTERNAL_applyXactTrackFilter() establishes the pending filter state in filterState_ regardless
// of whether a live track exists yet, then Play() (via EnsureTrackDspState()) attaches the real
// SDL3_mixer callback to that already-populated state. Reverting just the SoundEffectInstance-side
// fix while keeping this ordering would make this filter call a no-op (its old `if (!track_)
// return;` guard would trip, since track_ is still null at that point), so this test doubles as a
// regression guard for the pending-state path, not just "the final result is correct".
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
    // P11-TEST-001: exact value, not just non-zero -- BuildFilterXsbFixtureBytes' authored
    // 8000Hz, compared against the real mixer's own sample rate.
    SDL_AudioSpec spec{};
    ASSERT_TRUE(MIX_GetMixerFormat(CNA::Internal::Audio::GetMixer(), &spec));
    EXPECT_NEAR(frequency,
                SoundEffectInstanceTestAccess::CalculateFilterCutoff(8000.0f, static_cast<float>(spec.freq)),
                1e-4f);
}

// ===================== RPC-driven live filter frequency (P10-FILTER-002/003/006) =====================
//
// FilterFreqRpcBank()'s "FilterFreqRpcCue" (its own dedicated engine/xgs/xsb, see
// BuildFilterFreqRpcXgsFixtureBytes/BuildFilterFreqRpcXsbFixtureBytes) has a real per-track
// high-pass filter authored at kFilterFreqTrackBaseHz (500Hz) AND a RPC_PARAMETER_FILTERFREQUENCY
// curve bound to the "FilterFreq" variable: 0.0 -> 2000Hz, 1.0 -> 12000Hz, linear.

// Play() must apply the RPC's initial evaluation (variable defaults to 0.0 -> curve's 2000Hz
// endpoint) on top of the track's own authored base filter -- proving the wiring reaches all the
// way from Cue::Play()'s new INTERNAL_applyRpcFilterOverride() call into the spawned instance's
// real filter state, not just the one-shot XACT-authored value. P14-ORDER-002: both the base
// filter and this RPC override now run BEFORE the spawned instance's inst->Play() (see
// Cue::Play()'s per-wave loop) -- same pending-state-path regression coverage rationale as
// PlayWiresRealXactTrackFilterIntoSpawnedInstance above, for the RPC-override entry point
// specifically.
TEST(CueTest, PlayAppliesInitialFilterFrequencyRpcEvaluationOverridingTrackBaseValue)
{
    auto cue = std::unique_ptr<Cue>(FilterFreqRpcBank().GetCue("FilterFreqRpcCue"));
    cue->Play();
    auto* inst = CueTestAccess::ActiveInstance(*cue, 0);
    ASSERT_NE(inst, nullptr);

    int kind = -1; float frequency = -1.0f, oneOverQ = -1.0f;
    SoundEffectInstanceTestAccess::GetFilterState(*inst, kind, frequency, oneOverQ);
    EXPECT_EQ(kind, 2); // FilterState::Kind::HighPass, unchanged by the RPC override

    // The RPC curve's 2000Hz endpoint (variable == 0.0 by default) must win over the track's own
    // authored 500Hz base -- compare against the exact same Hz->cutoff conversion the production
    // code uses (against the real mixer's own sample rate, not a hardcoded assumption), rather
    // than a bare "greater than" check.
    SDL_AudioSpec spec{};
    ASSERT_TRUE(MIX_GetMixerFormat(CNA::Internal::Audio::GetMixer(), &spec));
    const float expectedCutoff =
        SoundEffectInstanceTestAccess::CalculateFilterCutoff(2000.0f, static_cast<float>(spec.freq));
    EXPECT_NEAR(frequency, expectedCutoff, 1e-4f);

    cue->Stop(AudioStopOptions::Immediate);
}

// The continuous-tick infra (P9-XACT-016's pattern, now extended to filter frequency) must keep
// re-evaluating the bound curve as the variable changes after Play(), not just once at Play()
// time -- same "change the variable, tick, observe the new value" shape as
// ChangingBoundVariableAfterPlayContinuouslyUpdatesVolume/Pitch.
TEST(CueTest, ChangingBoundVariableAfterPlayContinuouslyUpdatesFilterFrequency)
{
    auto cue = std::unique_ptr<Cue>(FilterFreqRpcBank().GetCue("FilterFreqRpcCue"));
    cue->Play();
    auto* inst = CueTestAccess::ActiveInstance(*cue, 0);
    ASSERT_NE(inst, nullptr);

    int kind = -1; float lowFrequency = -1.0f, oneOverQ = -1.0f;
    SoundEffectInstanceTestAccess::GetFilterState(*inst, kind, lowFrequency, oneOverQ);

    cue->SetVariable("FilterFreq", 1.0f); // curve -> 12000Hz
    ASSERT_TRUE(cue->getIsPlayingProperty()); // ticks ReconcileState()'s continuous RPC re-eval

    float highFrequency = -1.0f;
    SoundEffectInstanceTestAccess::GetFilterState(*inst, kind, highFrequency, oneOverQ);

    // P11-TEST-001: exact values, not just "increased" -- the curve's two endpoints (variable
    // 0.0 -> 2000Hz, 1.0 -> 12000Hz, per BuildFilterFreqRpcXgsFixtureBytes) map through the same
    // Hz->cutoff conversion the production code uses, against the real mixer's own sample rate.
    SDL_AudioSpec spec{};
    ASSERT_TRUE(MIX_GetMixerFormat(CNA::Internal::Audio::GetMixer(), &spec));
    EXPECT_NEAR(lowFrequency,
                SoundEffectInstanceTestAccess::CalculateFilterCutoff(2000.0f, static_cast<float>(spec.freq)),
                1e-4f);
    EXPECT_NEAR(highFrequency,
                SoundEffectInstanceTestAccess::CalculateFilterCutoff(12000.0f, static_cast<float>(spec.freq)),
                1e-4f);

    cue->Stop(AudioStopOptions::Immediate);
}

// Q-factor RPC targeting is a distinct axis from frequency -- verified independently by directly
// wiring an RPC curve override at the SoundEffectInstance level in
// ApplyRpcFilterOverrideOverridesQFactorOnlyWhenFrequencySentinelIsNegative
// (SoundEffectInstanceTests.cpp); this Cue-level pass only needs to confirm the frequency-axis
// wiring reaches Cue::ReconcileState() correctly, which the two tests above already do.

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

// ===================== P9-CATEGORY-011: cue-level instanceLimit =====================

// FAIL (0): "FailCue" has instanceLimit=1. Once one instance is playing, a second Play() call on
// a different Cue object for the SAME cue definition must be rejected outright -- matches
// FACT_internal.c's play_sound() checking cue->data->instanceLimit and calling
// FACTCue_Stop(cue, IMMEDIATE) on the *new* cue when maxInstanceBehavior is FAIL.
TEST(CueTest, CueInstanceLimitFailRejectsSecondInstanceOfSameCueDefinition)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

    try
    {
        std::unique_ptr<Cue> cueA(CueLimitBank().GetCue("FailCue"));
        cueA->Play();
        if (!CueTestAccess::ActiveInstance(*cueA, 0))
        {
            GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                            "could not create a real SoundEffectInstance";
        }
        ASSERT_TRUE(cueA->getIsPlayingProperty());

        std::unique_ptr<Cue> cueB(CueLimitBank().GetCue("FailCue"));
        cueB->Play();

        EXPECT_TRUE(cueB->getIsStoppedProperty());
        EXPECT_FALSE(cueB->getIsPlayingProperty());
        EXPECT_EQ(CueTestAccess::ActiveInstance(*cueB, 0), nullptr);

        // cueA must be completely unaffected by cueB's rejected Play().
        EXPECT_TRUE(cueA->getIsPlayingProperty());
        EXPECT_NE(CueTestAccess::ActiveInstance(*cueA, 0), nullptr);

        cueA->Stop(AudioStopOptions::Immediate);
    }
    catch (const std::exception& error)
    {
        // CABI-32: names what it caught. A bare catch(...) reporting "no audio device" is how a
        // contract exception hides as an environment skip -- it happened in this file, to the
        // Apply3D attenuation case below, and cost a real regression its visibility.
        GTEST_SKIP() << "could not exercise real playback (expected: no audio device): "
                     << error.what();
    }
}

// REPLACE_OLDEST (2), and the bank-wide-unfiltered victim search: "TriggerCue" has
// instanceLimit=1. Playing an unrelated cue ("VictimCue", unlimited, a different definition) THEN
// one instance of "TriggerCue" THEN a second instance of "TriggerCue" must evict "VictimCue" --
// NOT the first "TriggerCue" instance, even though that's the "same definition" one might expect
// to compete against. This matches FACT_internal.c's handle_instance_limit(cue, NULL): its
// `if (category && ...)` victim filter is unconditionally false for a cue-level check (category
// is NULL), so the search scans every live cue in the whole SoundBank with no category or
// same-definition filter at all -- a real FAudio quirk (a cue-level instanceLimit conceptually
// ought to only compete against other instances of the same named cue), replicated here exactly
// rather than "fixed". Also proves the *count* itself IS correctly scoped to the same cue
// definition: the first "TriggerCue" instance must play completely normally (full volume, no
// fade) despite "VictimCue" already being active, and the second "TriggerCue" instance fades in
// via TriggerCue's own fadeInMS.
TEST(CueTest, CueInstanceLimitReplaceOldestEvictsOldestBankWideCueNotSameDefinitionSibling)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

    try
    {
        std::unique_ptr<Cue> victim(CueLimitBank().GetCue("VictimCue"));
        victim->Play();
        if (!CueTestAccess::ActiveInstance(*victim, 0))
        {
            GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                            "could not create a real SoundEffectInstance";
        }

        std::unique_ptr<Cue> triggerA(CueLimitBank().GetCue("TriggerCue"));
        triggerA->Play();
        ASSERT_TRUE(triggerA->getIsPlayingProperty());
        // Proves the instance-limit *count* excludes "VictimCue" (a different cue definition):
        // triggerA is this definition's first-ever instance, so it must play at full volume with
        // no fade-in, even though an unrelated cue is already active in the bank.
        const auto triggerAVolAtPlay = CueTestAccess::ActiveInstanceVolumes(*triggerA);
        ASSERT_FALSE(triggerAVolAtPlay.empty());
        // P11-TEST-001: exact value, not just ">0.9" -- TriggerCue's sound volume byte (0xFF,
        // amplitude ~1.9977) combined with the "Default" category's own authored volume byte
        // (also 0xFF, ~1.9977) is ~3.99 pre-clamp, so full (no-fade) volume clamps to exactly 1.0.
        EXPECT_FLOAT_EQ(triggerAVolAtPlay[0], 1.0f);

        std::unique_ptr<Cue> triggerB(CueLimitBank().GetCue("TriggerCue"));
        triggerB->Play();

        // victim (oldest in the whole bank) is evicted; triggerA (same definition as triggerB,
        // but not the oldest bank-wide) is untouched; triggerB fades in from silence.
        EXPECT_TRUE(victim->getIsStoppingProperty());
        EXPECT_FALSE(victim->getIsStoppedProperty());

        EXPECT_TRUE(triggerA->getIsPlayingProperty());
        const auto triggerAVolAfter = CueTestAccess::ActiveInstanceVolumes(*triggerA);
        ASSERT_FALSE(triggerAVolAfter.empty());
        EXPECT_FLOAT_EQ(triggerAVolAfter[0], 1.0f); // still untouched, no fade applied to it

        const auto triggerBVolAtPlay = CueTestAccess::ActiveInstanceVolumes(*triggerB);
        ASSERT_FALSE(triggerBVolAtPlay.empty());
        EXPECT_FLOAT_EQ(triggerBVolAtPlay[0], 0.0f);

        std::this_thread::sleep_for(std::chrono::milliseconds(kCueLimitFadeMS + 150));

        EXPECT_TRUE(victim->getIsStoppedProperty());
        EXPECT_FALSE(victim->getIsStoppingProperty());
        EXPECT_EQ(CueTestAccess::ActiveInstance(*victim, 0), nullptr);

        EXPECT_TRUE(triggerA->getIsPlayingProperty()); // still untouched throughout

        ASSERT_TRUE(triggerB->getIsPlayingProperty());
        const auto triggerBVolAfter = CueTestAccess::ActiveInstanceVolumes(*triggerB);
        ASSERT_FALSE(triggerBVolAfter.empty());
        EXPECT_FLOAT_EQ(triggerBVolAfter[0], 1.0f); // fully faded in after kCueLimitFadeMS elapses

        triggerA->Stop(AudioStopOptions::Immediate);
        triggerB->Stop(AudioStopOptions::Immediate);
    }
    catch (const std::exception& error)
    {
        // CABI-32: names what it caught. A bare catch(...) reporting "no audio device" is how a
        // contract exception hides as an environment skip -- it happened in this file, to the
        // Apply3D attenuation case below, and cost a real regression its visibility.
        GTEST_SKIP() << "could not exercise real playback (expected: no audio device): "
                     << error.what();
    }
}

// AUD-11-016: confirmed real gap and fixed -- Cue::Play() previously `continue`d silently when
// a wave reference's wave bank couldn't be found (or its SoundEffect failed to load), with zero
// cue-level diagnostic naming which cue was affected. Verifies the new diagnostic actually names
// the cue when its sole wave reference points at a wave bank that was never registered with the
// engine.
TEST(CueTest, PlayWithUnresolvableWaveBankLogsCueNameAndDoesNotCrash)
{
    std::unique_ptr<Cue> cue(SharedUnresolvableWaveBankBank().GetCue("UnresolvableWaveBankCue"));
    ASSERT_NE(cue, nullptr);

    testing::internal::CaptureStderr();
    cue->Play();
    std::string captured = testing::internal::GetCapturedStderr();

    EXPECT_NE(captured.find("UnresolvableWaveBankCue"), std::string::npos) << captured;
    EXPECT_NE(captured.find(kNeverRegisteredWaveBankName), std::string::npos) << captured;
}
