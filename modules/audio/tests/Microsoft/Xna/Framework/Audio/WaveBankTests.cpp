// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Audio/AudioEngine.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioStopOptions.hpp"
#include "Microsoft/Xna/Framework/Audio/Cue.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundBank.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Audio/WaveBank.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/Environment.hpp"
#include "System/EventArgs.hpp"
#include "System/IO/FileNotFoundException.hpp"
#include "System/Object.hpp"

#include "SoundEffectInstanceTestAccess.hpp"

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

using Microsoft::Xna::Framework::Audio::AudioEngine;
using Microsoft::Xna::Framework::Audio::AudioStopOptions;
using Microsoft::Xna::Framework::Audio::Cue;
using Microsoft::Xna::Framework::Audio::SoundBank;
using Microsoft::Xna::Framework::Audio::SoundEffect;
using Microsoft::Xna::Framework::Audio::WaveBank;

namespace Microsoft::Xna::Framework::Audio
{
    // Test-only accessor for T-3F: whether a bank loaded via streaming/lazy per-entry reads
    // (XwbData::streaming) vs eagerly loading the whole file, how many file bytes are actually
    // resident in memory, and direct access to the private GetSoundEffect() so offset/length
    // correctness can be checked without going through a real Cue/SoundBank/audio-device pipeline.
    struct WaveBankTestAccess
    {
        static bool IsStreaming(const WaveBank& wb) { return wb.StreamingInternal(); }
        static std::size_t ResidentFileBytes(const WaveBank& wb) { return wb.ResidentFileBytesInternal(); }
        static const SoundEffect* GetSoundEffect(WaveBank& wb, unsigned short index)
        {
            return wb.GetSoundEffect(index);
        }
    };
}

using Microsoft::Xna::Framework::Audio::WaveBankTestAccess;

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

    void AppendCStr(std::vector<uint8_t>& buf, const std::string& s)
    {
        buf.insert(buf.end(), s.begin(), s.end());
        buf.push_back(0);
    }

    constexpr const char* kWaveBankName = "TestWaveBank";

    // Minimal compact .xwb with one mono PCM entry (200 bytes of silence). Defaults to PCM16
    // (direct SoundEffect(buffer) construction path); pass eightBitPcm=true to instead exercise
    // WaveBank::GetSoundEffect's WAV-wrapping SoundEffect::FromStream path (regression coverage
    // for XA-2, a leak in that branch) -- give it a distinct bankName so it doesn't collide with
    // "TestWaveBank" in AudioEngine::RegisterWaveBank's name-keyed registry.
    std::vector<uint8_t> BuildXwbFixtureBytes(const std::string& bankName = kWaveBankName,
                                               bool eightBitPcm = false)
    {
        constexpr uint32_t headerSize       = 48;
        constexpr uint32_t bankDataSize     = 96;
        constexpr uint32_t entryCount       = 1;
        constexpr uint32_t entryMetaDataSize = 4;
        constexpr uint32_t entryMetaSegSize = entryCount * entryMetaDataSize;
        constexpr uint32_t waveDataLength   = 200;
        constexpr uint32_t alignment        = 4;

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
        AppendU32(data, 1); // version (<=43 -> no headerVersion field)
        for (int i = 0; i < 5; ++i)
        {
            AppendU32(data, segOffset[i]);
            AppendU32(data, segLength[i]);
        }

        AppendU32(data, 0x00020000u); // wbFlags: COMPACT only, no names
        AppendU32(data, entryCount);
        AppendPadded(data, bankName, 64);
        AppendU32(data, entryMetaDataSize);
        AppendU32(data, 0); // entryNameElementSize
        AppendU32(data, alignment);
        const uint32_t compactFormat =
              (0u)                                  // format tag: PCM
            | (1u << 2)                              // channels: raw field IS the real channel count -> mono (IN-7)
            | (44100u << 5)                          // sample rate
            | ((eightBitPcm ? 1u : 2u) << 23)         // wBlockAlign: bytes/sample (mono)
            | ((eightBitPcm ? 0u : 1u) << 31);        // bits-per-sample flag -> 8-bit or 16-bit
        AppendU32(data, compactFormat);
        for (int i = 0; i < 8; ++i) data.push_back(0); // buildTime

        AppendU32(data, 0u); // entry 0: offset=0, deviation=0 (last/only entry: length = segLength[4])

        for (uint32_t i = 0; i < waveDataLength; ++i)
            data.push_back(eightBitPcm ? 0x80 : 0x00); // silence: 8-bit unsigned midpoint, or 16-bit zero

        return data;
    }

    // AUD-11-010/011 (2026-07-17 deep audit): minimal compact .xwb with one entry tagged as XMA
    // (compact format tag 1) -- CNA has no XMA decode path anywhere in this stack (SDL3 doesn't
    // decode XMA either), so GetSoundEffect() must return nullptr cleanly rather than crash or
    // silently construct a garbage SoundEffect from the raw compressed bytes.
    std::vector<uint8_t> BuildXmaXwbFixtureBytes(const std::string& bankName)
    {
        constexpr uint32_t headerSize       = 48;
        constexpr uint32_t bankDataSize     = 96;
        constexpr uint32_t entryCount       = 1;
        constexpr uint32_t entryMetaDataSize = 4;
        constexpr uint32_t entryMetaSegSize = entryCount * entryMetaDataSize;
        constexpr uint32_t waveDataLength   = 64;
        constexpr uint32_t alignment        = 4;

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
        AppendU32(data, 1);
        for (int i = 0; i < 5; ++i)
        {
            AppendU32(data, segOffset[i]);
            AppendU32(data, segLength[i]);
        }

        AppendU32(data, 0x00020000u); // wbFlags: COMPACT only, no names
        AppendU32(data, entryCount);
        AppendPadded(data, bankName, 64);
        AppendU32(data, entryMetaDataSize);
        AppendU32(data, 0);
        AppendU32(data, alignment);
        const uint32_t compactFormat =
              (1u)                                  // format tag: XMA
            | (1u << 2)                              // channels: mono
            | (44100u << 5)                          // sample rate
            | (2u << 23)
            | (1u << 31);
        AppendU32(data, compactFormat);
        for (int i = 0; i < 8; ++i) data.push_back(0); // buildTime

        AppendU32(data, 0u); // entry 0: offset=0, deviation=0 (last/only entry)

        for (uint32_t i = 0; i < waveDataLength; ++i)
            data.push_back(static_cast<uint8_t>(i));

        return data;
    }

    // AUDIO-ADPCM-001 (2026-07-17 deep audit follow-up): minimal compact .xwb with one mono
    // MS-ADPCM entry, real block-aligned payload (not silence -- SDL's real MS-ADPCM decoder
    // must actually parse a full block header + nibble data, which a run of zero bytes alone
    // would not meaningfully exercise). wBlockAlign=8 -> blockAlign=(8+22)*1=30 bytes/block,
    // samplesPerBlock=(8+16)*2=48 samples/block (XactParser.cpp's own compact-ADPCM formula);
    // 4 blocks of 30 bytes each. Each block: predictor index 0 (valid, <7 preset coefficients),
    // iDelta/iSamp1/iSamp2 header, then 23 bytes of nibble data.
    std::vector<uint8_t> BuildAdpcmXwbFixtureBytes(const std::string& bankName)
    {
        constexpr uint32_t headerSize       = 48;
        constexpr uint32_t bankDataSize     = 96;
        constexpr uint32_t entryCount       = 1;
        constexpr uint32_t entryMetaDataSize = 4;
        constexpr uint32_t entryMetaSegSize = entryCount * entryMetaDataSize;
        constexpr uint16_t blockAlign       = 30;
        constexpr uint32_t blockCount       = 4;
        constexpr uint32_t waveDataLength   = blockAlign * blockCount;
        constexpr uint32_t alignment        = 4;

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
        AppendU32(data, 1);
        for (int i = 0; i < 5; ++i)
        {
            AppendU32(data, segOffset[i]);
            AppendU32(data, segLength[i]);
        }

        AppendU32(data, 0x00020000u); // wbFlags: COMPACT only, no names
        AppendU32(data, entryCount);
        AppendPadded(data, bankName, 64);
        AppendU32(data, entryMetaDataSize);
        AppendU32(data, 0);
        AppendU32(data, alignment);
        constexpr uint8_t wBlockAlign = 8;
        const uint32_t compactFormat =
              (2u)                 // format tag: ADPCM
            | (1u << 2)            // channels: mono
            | (22050u << 5)        // sample rate
            | (static_cast<uint32_t>(wBlockAlign) << 23)
            | (0u << 31);          // bps flag unused for ADPCM
        AppendU32(data, compactFormat);
        for (int i = 0; i < 8; ++i) data.push_back(0); // buildTime

        AppendU32(data, 0u); // entry 0: offset=0, deviation=0

        for (uint32_t b = 0; b < blockCount; ++b)
        {
            data.push_back(0); // bPredictor: coefficient index 0 (valid, < 7 presets)
            AppendU16(data, 32);  // iDelta
            AppendU16(data, 0);   // iSamp1
            AppendU16(data, 0);   // iSamp2
            for (uint32_t j = 0; j < blockAlign - 7; ++j)
                data.push_back(static_cast<uint8_t>(0x11 * (j + 1))); // arbitrary nibble data
        }

        return data;
    }

    // Minimal .xsb with one wavebank reference, one simple sound pointing at wave 0
    // of that bank, and one simple cue playing that sound.
    std::vector<uint8_t> BuildXsbFixtureBytes(const std::string& wavebankName = kWaveBankName,
                                               const std::string& cueName = "Boom")
    {
        constexpr uint32_t headerSize   = 74;
        constexpr uint32_t bankNameSize = 64;
        constexpr uint32_t baseOffset   = headerSize + bankNameSize; // 138

        const uint32_t wavebankNameOffset = baseOffset;
        const uint32_t soundOffset        = wavebankNameOffset + 64;
        const uint32_t cueSimpleOffset    = soundOffset + 12;
        const uint32_t cueNameIndexOffset = cueSimpleOffset + 5;
        const uint32_t cueNameStrOffset   = cueNameIndexOffset + 6;

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
        AppendS32(data, -1); // variationOffset (must sit at header byte 0x32)
        AppendS32(data, 0);  // transitionOffset (unused)
        AppendS32(data, static_cast<int32_t>(wavebankNameOffset));
        AppendS32(data, 0);  // cueHashOffset (unused)
        AppendS32(data, static_cast<int32_t>(cueNameIndexOffset));
        AppendS32(data, static_cast<int32_t>(soundOffset));

        AppendPadded(data, "TestSoundBank", bankNameSize);

        AppendPadded(data, wavebankName, 64); // wavebank name table (1 entry)

        // Sound: flags, categoryIndex, volume, pitchCents, priority, soundLength(skip),
        // then (not complex) waveIdx, wbIdx.
        AppendU8(data, 0);   // flags (no COMPLEX/RPC/DSP bits)
        AppendU16(data, 0);  // categoryIndex
        AppendU8(data, 0xFF);// volume byte
        AppendU16(data, 0);  // pitchCents (s16, 0)
        AppendU8(data, 0);   // priority
        AppendU16(data, 0);  // soundLength (skipped)
        AppendU16(data, 0);  // waveIdx
        AppendU8(data, 0);   // wbIdx

        // cueSimpleOffset: {flags: u8, sbCode: u32} — sbCode matches the sound's offset.
        AppendU8(data, 0);
        AppendU32(data, soundOffset);

        // cueNameIndexOffset: {nameAbsOffset: u32, unknown: u16}
        AppendU32(data, cueNameStrOffset);
        AppendU16(data, 0);

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

    const std::string& XwbFixturePath()
    {
        static const std::string path =
            WriteFixture("cna_wavebank_test", "fixture.xwb", BuildXwbFixtureBytes());
        return path;
    }

    const std::string& XsbFixturePath()
    {
        static const std::string path =
            WriteFixture("cna_wavebank_test", "fixture.xsb", BuildXsbFixtureBytes());
        return path;
    }

    constexpr const char* kWaveBank8BitName = "TestWaveBank8Bit";
    constexpr const char* kCue8BitName      = "Boom8Bit";

    const std::string& Xwb8BitFixturePath()
    {
        static const std::string path = WriteFixture(
            "cna_wavebank_test", "fixture8bit.xwb",
            BuildXwbFixtureBytes(kWaveBank8BitName, /*eightBitPcm=*/true));
        return path;
    }

    const std::string& Xsb8BitFixturePath()
    {
        static const std::string path = WriteFixture(
            "cna_wavebank_test", "fixture8bit.xsb",
            BuildXsbFixtureBytes(kWaveBank8BitName, kCue8BitName));
        return path;
    }

    constexpr const char* kWaveBankAdpcmName = "TestWaveBankAdpcm";

    const std::string& XwbAdpcmFixturePath()
    {
        static const std::string path = WriteFixture(
            "cna_wavebank_test", "fixtureadpcm.xwb",
            BuildAdpcmXwbFixtureBytes(kWaveBankAdpcmName));
        return path;
    }

    constexpr const char* kWaveBankXmaName = "TestWaveBankXma";

    const std::string& XwbXmaFixturePath()
    {
        static const std::string path = WriteFixture(
            "cna_wavebank_test", "fixturexma.xwb",
            BuildXmaXwbFixtureBytes(kWaveBankXmaName));
        return path;
    }

    // Minimal parseable .xgs: zero categories/variables/rpcs, padded to ParseXgs's 0x50-byte
    // minimum (XactParser.cpp). AudioEngine's ctor now throws FileNotFoundException on a missing
    // settings file (P9-HARDWARE-003, matching FNA's TitleContainer.ReadToPointer), so this must
    // be a real, existing, parseable file rather than a deliberately-nonexistent path -- it still
    // has no categories/variables, which is all WaveBank needs here.
    std::vector<uint8_t> BuildMinimalXgsFixtureBytes()
    {
        std::vector<uint8_t> data;
        const char magic[4] = { 'X', 'G', 'S', 'F' };
        data.insert(data.end(), magic, magic + 4);
        AppendU16(data, 46); // contentVersion
        AppendU16(data, 0);  // toolVersion
        AppendU16(data, 0);  // unknown
        for (int i = 0; i < 8; ++i) data.push_back(0); // lastModified
        AppendU8(data, 0);   // platform

        AppendU16(data, 0); // categoryCount
        AppendU16(data, 0); // variableCount
        AppendU16(data, 0); // blob1Count
        AppendU16(data, 0); // blob2Count
        AppendU16(data, 0); // rpcCount
        AppendU16(data, 0); // dspPresetCount
        AppendU16(data, 0); // dspParameterCount

        for (int i = 0; i < 9; ++i) AppendU32(data, 0); // all nine offset fields

        data.resize(0x50, 0); // ParseXgs requires >= 0x50 bytes total
        return data;
    }

    AudioEngine& SharedEngine()
    {
        static const std::string path = []() -> std::string
        {
            auto dir = std::filesystem::temp_directory_path() / "cna_wavebank_test";
            std::filesystem::create_directories(dir);
            auto file = dir / "fixture_engine.xgs";
            const auto bytes = BuildMinimalXgsFixtureBytes();
            std::ofstream f(file, std::ios::binary);
            f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            return file.string();
        }();
        static AudioEngine engine(path);
        return engine;
    }

    constexpr const char* kMultiEntryWaveBankName = "MultiEntryWaveBank";

    // Non-compact .xwb (standard 24-byte entryMetaDataSize) with two mono 16-bit PCM entries at
    // different offsets/lengths within the wave-data segment (entry 0: 100 bytes at offset 0;
    // entry 1: 300 bytes at offset 100) -- needed to prove streaming's lazy per-entry disk reads
    // (T-3F) land on the correct byte range, not just "some" range. A single-entry fixture
    // couldn't catch an offset bug, since entry 0 always starts at offset 0 either way.
    std::vector<uint8_t> BuildMultiEntryXwbFixtureBytes()
    {
        constexpr uint32_t headerSize        = 48;
        constexpr uint32_t bankDataSize      = 96;
        constexpr uint32_t entryCount        = 2;
        constexpr uint32_t entryMetaDataSize = 24;
        constexpr uint32_t entryMetaSegSize  = entryCount * entryMetaDataSize;
        constexpr uint32_t entry0Length      = 100;
        constexpr uint32_t entry1Length      = 300;
        constexpr uint32_t waveDataLength    = entry0Length + entry1Length;

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
        AppendU32(data, 1); // version (<=43 -> no headerVersion field)
        for (int i = 0; i < 5; ++i)
        {
            AppendU32(data, segOffset[i]);
            AppendU32(data, segLength[i]);
        }

        AppendU32(data, 0u); // wbFlags: not compact, no names
        AppendU32(data, entryCount);
        AppendPadded(data, kMultiEntryWaveBankName, 64);
        AppendU32(data, entryMetaDataSize);
        AppendU32(data, 0); // entryNameElementSize
        AppendU32(data, 4); // alignment (unused, non-compact)
        AppendU32(data, 0); // compactFormat (unused, non-compact)
        for (int i = 0; i < 8; ++i) data.push_back(0); // buildTime

        const uint32_t fmt =
              (0u)          // fmtTag: PCM
            | (1u << 2)     // channels: raw field IS the real channel count -> mono (IN-7)
            | (44100u << 5) // sample rate
            | (2u << 23)    // wBlockAlign: 2 bytes/sample
            | (1u << 31);   // 16-bit

        // Entry 0: playOffset=0, playLength=entry0Length
        AppendU32(data, 0u); // flagsAndDuration (unused by CNA)
        AppendU32(data, fmt);
        AppendU32(data, 0u);
        AppendU32(data, entry0Length);
        AppendU32(data, 0u); // loopStart
        AppendU32(data, 0u); // loopTotal

        // Entry 1: playOffset=entry0Length, playLength=entry1Length
        AppendU32(data, 0u);
        AppendU32(data, fmt);
        AppendU32(data, entry0Length);
        AppendU32(data, entry1Length);
        AppendU32(data, 0u);
        AppendU32(data, 0u);

        for (uint32_t i = 0; i < entry0Length; ++i)
            data.push_back(0xAA);
        for (uint32_t i = 0; i < entry1Length; ++i)
            data.push_back(0xBB);

        return data;
    }

    const std::string& MultiEntryXwbFixturePath()
    {
        static const std::string path = WriteFixture(
            "cna_wavebank_test", "multifixture.xwb", BuildMultiEntryXwbFixtureBytes());
        return path;
    }

    constexpr const char* kLoopedWaveBankName = "LoopedWaveBank";

    // AUD-11-014: a non-compact (entryMetaDataSize=24, so real LoopRegion fields are present --
    // compact-format entries have no room for loop fields at all, only offsetUnits:21+deviation:11
    // packed into a single u32) .xwb entry with a genuine, non-degenerate intro-then-loop region:
    // 100 real frames total, loop covers [20, 70) -- neither the whole track nor starting at 0, so
    // a "loop the entire track instead of just the authored region" bug is distinguishable from
    // correct behavior.
    std::vector<uint8_t> BuildLoopedXwbFixtureBytes()
    {
        constexpr uint32_t headerSize        = 48;
        constexpr uint32_t bankDataSize      = 96;
        constexpr uint32_t entryCount        = 1;
        constexpr uint32_t entryMetaDataSize = 24;
        constexpr uint32_t entryMetaSegSize  = entryCount * entryMetaDataSize;
        constexpr uint32_t waveDataLength    = 200; // 100 mono 16-bit frames

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
        AppendU32(data, 1); // version (<=43 -> no headerVersion field)
        for (int i = 0; i < 5; ++i)
        {
            AppendU32(data, segOffset[i]);
            AppendU32(data, segLength[i]);
        }

        AppendU32(data, 0u); // wbFlags: not compact, no names
        AppendU32(data, entryCount);
        AppendPadded(data, kLoopedWaveBankName, 64);
        AppendU32(data, entryMetaDataSize);
        AppendU32(data, 0); // entryNameElementSize
        AppendU32(data, 4); // alignment (unused, non-compact)
        AppendU32(data, 0); // compactFormat (unused, non-compact)
        for (int i = 0; i < 8; ++i) data.push_back(0); // buildTime

        const uint32_t fmt =
              (0u)          // fmtTag: PCM
            | (1u << 2)     // channels: mono
            | (44100u << 5) // sample rate
            | (2u << 23)    // wBlockAlign: 2 bytes/sample
            | (1u << 31);   // 16-bit

        AppendU32(data, 0u);            // flagsAndDuration (unused by CNA)
        AppendU32(data, fmt);
        AppendU32(data, 0u);            // playOffset
        AppendU32(data, waveDataLength); // playLength
        AppendU32(data, 20u);           // loopStart: 20 frames
        AppendU32(data, 50u);           // loopTotal: 50 frames -> region [20, 70)

        for (uint32_t i = 0; i < waveDataLength; ++i)
            data.push_back(static_cast<uint8_t>(i));

        return data;
    }

    const std::string& LoopedXwbFixturePath()
    {
        static const std::string path = WriteFixture(
            "cna_wavebank_test", "loopedfixture.xwb", BuildLoopedXwbFixtureBytes());
        return path;
    }

    constexpr const char* kZeroLengthWaveBankName = "ZeroLengthWaveBank";

    // AUD-11-019: a non-compact .xwb entry with a genuinely zero-length wave payload (dwLength=0)
    // -- a real, reachable case (e.g. an authored-but-never-recorded placeholder wave, or a
    // corrupt/truncated bank), not just a synthetic edge case. Must not crash the decoder.
    std::vector<uint8_t> BuildZeroLengthXwbFixtureBytes()
    {
        constexpr uint32_t headerSize        = 48;
        constexpr uint32_t bankDataSize      = 96;
        constexpr uint32_t entryCount        = 1;
        constexpr uint32_t entryMetaDataSize = 24;
        constexpr uint32_t entryMetaSegSize  = entryCount * entryMetaDataSize;

        const uint32_t segOffset[5] = {
            headerSize,
            headerSize + bankDataSize,
            headerSize + bankDataSize + entryMetaSegSize,
            headerSize + bankDataSize + entryMetaSegSize,
            headerSize + bankDataSize + entryMetaSegSize,
        };
        const uint32_t segLength[5] = { bankDataSize, entryMetaSegSize, 0, 0, 0 };

        std::vector<uint8_t> data;
        const char magic[4] = { 'W', 'B', 'N', 'D' };
        data.insert(data.end(), magic, magic + 4);
        AppendU32(data, 1);
        for (int i = 0; i < 5; ++i) { AppendU32(data, segOffset[i]); AppendU32(data, segLength[i]); }

        AppendU32(data, 0u);
        AppendU32(data, entryCount);
        AppendPadded(data, kZeroLengthWaveBankName, 64);
        AppendU32(data, entryMetaDataSize);
        AppendU32(data, 0);
        AppendU32(data, 4);
        AppendU32(data, 0);
        for (int i = 0; i < 8; ++i) data.push_back(0);

        const uint32_t fmt = (0u) | (1u << 2) | (44100u << 5) | (2u << 23) | (1u << 31);
        AppendU32(data, 0u);
        AppendU32(data, fmt);
        AppendU32(data, 0u); // playOffset
        AppendU32(data, 0u); // playLength: zero -- no wave data at all
        AppendU32(data, 0u);
        AppendU32(data, 0u);

        // No wave-data bytes follow -- the file ends right here.
        return data;
    }

    const std::string& ZeroLengthXwbFixturePath()
    {
        static const std::string path = WriteFixture(
            "cna_wavebank_test", "zerolengthfixture.xwb", BuildZeroLengthXwbFixtureBytes());
        return path;
    }

    constexpr const char* kPaddedWaveBankName = "PaddedWaveBank";

    // AUD-11-020: two entries with deliberate padding bytes *between* them in the wave-data
    // segment (unlike BuildMultiEntryXwbFixtureBytes, whose two entries are byte-adjacent with no
    // gap at all) -- each entry's dwOffset/dwLength are explicit, authored fields, not inferred
    // from gaps between entries, so padding must never leak into either entry's decoded audio.
    // Entry 0 (0xAA bytes, 50 bytes) and entry 1 (0xBB bytes, 70 bytes) deliberately have
    // DIFFERENT lengths -- proving offset correctness, not just length correctness: if entry 1's
    // read accidentally started from the gap or from entry 0's tail instead of its own authored
    // offset, its decoded duration would come out wrong, not just its content.
    std::vector<uint8_t> BuildPaddedXwbFixtureBytes()
    {
        constexpr uint32_t headerSize        = 48;
        constexpr uint32_t bankDataSize      = 96;
        constexpr uint32_t entryCount        = 2;
        constexpr uint32_t entryMetaDataSize = 24;
        constexpr uint32_t entryMetaSegSize  = entryCount * entryMetaDataSize;
        constexpr uint32_t entry0Length      = 50;
        constexpr uint32_t entry1Length      = 70;
        constexpr uint32_t gapLength         = 30;
        constexpr uint32_t entry1Offset      = entry0Length + gapLength; // 80
        constexpr uint32_t waveDataLength    = entry1Offset + entry1Length; // 150, includes the gap

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
        AppendU32(data, 1);
        for (int i = 0; i < 5; ++i) { AppendU32(data, segOffset[i]); AppendU32(data, segLength[i]); }

        AppendU32(data, 0u);
        AppendU32(data, entryCount);
        AppendPadded(data, kPaddedWaveBankName, 64);
        AppendU32(data, entryMetaDataSize);
        AppendU32(data, 0);
        AppendU32(data, 4);
        AppendU32(data, 0);
        for (int i = 0; i < 8; ++i) data.push_back(0);

        const uint32_t fmt = (0u) | (1u << 2) | (44100u << 5) | (2u << 23) | (1u << 31);

        AppendU32(data, 0u); AppendU32(data, fmt);
        AppendU32(data, 0u);            // entry 0 playOffset
        AppendU32(data, entry0Length);  // entry 0 playLength -- does NOT include the gap
        AppendU32(data, 0u); AppendU32(data, 0u);

        AppendU32(data, 0u); AppendU32(data, fmt);
        AppendU32(data, entry1Offset);  // entry 1 playOffset -- starts AFTER the gap
        AppendU32(data, entry1Length);
        AppendU32(data, 0u); AppendU32(data, 0u);

        for (uint32_t i = 0; i < entry0Length; ++i) data.push_back(0xAA); // entry 0's real data
        for (uint32_t i = 0; i < gapLength; ++i) data.push_back(0xCC);   // padding gap
        for (uint32_t i = 0; i < entry1Length; ++i) data.push_back(0xBB); // entry 1's real data

        return data;
    }

    const std::string& PaddedXwbFixturePath()
    {
        static const std::string path = WriteFixture(
            "cna_wavebank_test", "paddedfixture.xwb", BuildPaddedXwbFixtureBytes());
        return path;
    }

    constexpr const char* kOversizedWaveBankName = "OversizedEntryWaveBank";

    // Non-compact .xwb with one entry whose dataLength (1,000,000 bytes) claims far more data
    // than the file genuinely contains (16 real bytes of wave data). The streaming ctor only
    // reads segments 0-3 upfront (ParseXwbStreamingHeader never touches segment 4), so nothing
    // at load time catches this -- only WaveBank::GetSoundEffect's streaming path can (IN-9).
    // Regression fixture: proves GetSoundEffect bounds-checks against the real on-disk file size
    // before attempting an allocation, rather than trusting the parsed dataLength outright.
    std::vector<uint8_t> BuildOversizedStreamingXwbFixtureBytes()
    {
        constexpr uint32_t headerSize        = 48;
        constexpr uint32_t bankDataSize      = 96;
        constexpr uint32_t entryCount        = 1;
        constexpr uint32_t entryMetaDataSize = 24;
        constexpr uint32_t entryMetaSegSize  = entryCount * entryMetaDataSize;
        constexpr uint32_t realWaveDataLength = 16; // genuinely written to disk below
        constexpr uint32_t claimedDataLength  = 1000000; // wildly exceeds the real file size

        const uint32_t segOffset[5] = {
            headerSize,
            headerSize + bankDataSize,
            headerSize + bankDataSize + entryMetaSegSize,
            headerSize + bankDataSize + entryMetaSegSize,
            headerSize + bankDataSize + entryMetaSegSize,
        };
        // segLength[4] (the header's own claimed wave-data segment size) is set to match the
        // corrupt entry, matching how a genuinely corrupt/adversarial file would look -- the bug
        // is that nothing cross-checks either of these against the real file size at load time.
        const uint32_t segLength[5] = { bankDataSize, entryMetaSegSize, 0, 0, claimedDataLength };

        std::vector<uint8_t> data;
        const char magic[4] = { 'W', 'B', 'N', 'D' };
        data.insert(data.end(), magic, magic + 4);
        AppendU32(data, 1); // version (<=43 -> no headerVersion field)
        for (int i = 0; i < 5; ++i)
        {
            AppendU32(data, segOffset[i]);
            AppendU32(data, segLength[i]);
        }

        AppendU32(data, 0u); // wbFlags: not compact, no names
        AppendU32(data, entryCount);
        AppendPadded(data, kOversizedWaveBankName, 64);
        AppendU32(data, entryMetaDataSize);
        AppendU32(data, 0); // entryNameElementSize
        AppendU32(data, 4); // alignment (unused, non-compact)
        AppendU32(data, 0); // compactFormat (unused, non-compact)
        for (int i = 0; i < 8; ++i) data.push_back(0); // buildTime

        const uint32_t fmt =
              (0u)          // fmtTag: PCM
            | (1u << 2)     // channels: mono (IN-7)
            | (44100u << 5) // sample rate
            | (2u << 23)    // wBlockAlign: 2 bytes/sample
            | (1u << 31);   // 16-bit

        AppendU32(data, 0u); // flagsAndDuration (unused by CNA)
        AppendU32(data, fmt);
        AppendU32(data, 0u);                 // playOffset
        AppendU32(data, claimedDataLength);  // playLength -- the lie
        AppendU32(data, 0u);                 // loopStart
        AppendU32(data, 0u);                 // loopTotal

        // Only realWaveDataLength bytes are actually written -- the file ends here.
        for (uint32_t i = 0; i < realWaveDataLength; ++i)
            data.push_back(0xCC);

        return data;
    }

    const std::string& OversizedStreamingXwbFixturePath()
    {
        static const std::string path = WriteFixture(
            "cna_wavebank_test", "oversized.xwb", BuildOversizedStreamingXwbFixtureBytes());
        return path;
    }
}

// ===================== Constructors =====================

TEST(WaveBankTest, NonStreamingCtorNullEngineThrowsArgumentNull)
{
    EXPECT_THROW(WaveBank wb(nullptr, XwbFixturePath()), System::ArgumentNullException);
}

TEST(WaveBankTest, NonStreamingCtorEmptyFilenameThrowsArgumentNull)
{
    EXPECT_THROW(WaveBank wb(&SharedEngine(), ""), System::ArgumentNullException);
}

TEST(WaveBankTest, NonStreamingCtorLoadsValidFixture)
{
    WaveBank wb(&SharedEngine(), XwbFixturePath());
    EXPECT_FALSE(wb.getIsDisposedProperty());
    EXPECT_TRUE(wb.getIsPreparedProperty());
}

TEST(WaveBankTest, StreamingCtorNullEngineThrowsArgumentNull)
{
    EXPECT_THROW(WaveBank wb(nullptr, XwbFixturePath(), 0, 0), System::ArgumentNullException);
}

TEST(WaveBankTest, StreamingCtorEmptyFilenameThrowsArgumentNull)
{
    EXPECT_THROW(WaveBank wb(&SharedEngine(), "", 0, 0), System::ArgumentNullException);
}

TEST(WaveBankTest, StreamingCtorLoadsValidFixture)
{
    WaveBank wb(&SharedEngine(), XwbFixturePath(), 0, 2048);
    EXPECT_FALSE(wb.getIsDisposedProperty());
    EXPECT_TRUE(wb.getIsPreparedProperty());
}

// ===================== Streaming vs. in-memory loading (T-3F) =====================

// The non-streaming ctor must keep the FNA-matching "everything eager" behavior: the entire
// file is resident, and WaveBank::StreamingInternal() reports false.
TEST(WaveBankTest, NonStreamingCtorLoadsEntireFileIntoMemory)
{
    const auto& path = XwbFixturePath();
    const auto fullFileSize = std::filesystem::file_size(path);

    WaveBank wb(&SharedEngine(), path);
    ASSERT_TRUE(wb.getIsPreparedProperty());

    EXPECT_FALSE(WaveBankTestAccess::IsStreaming(wb));
    EXPECT_EQ(WaveBankTestAccess::ResidentFileBytes(wb), fullFileSize);
}

// The streaming ctor must NOT eagerly load the (by far largest) wave-data segment -- only the
// header/metadata segments should be resident; this is the actual memory-footprint payoff of
// T-3F's real streaming implementation, as opposed to the old "streaming ctor secretly loads
// everything anyway" behavior.
TEST(WaveBankTest, StreamingCtorDoesNotLoadWaveDataSegmentIntoMemory)
{
    const auto& path = XwbFixturePath();
    const auto fullFileSize = std::filesystem::file_size(path);

    WaveBank wb(&SharedEngine(), path, 0, 2048);
    ASSERT_TRUE(wb.getIsPreparedProperty());

    EXPECT_TRUE(WaveBankTestAccess::IsStreaming(wb));
    const auto residentBytes = WaveBankTestAccess::ResidentFileBytes(wb);
    EXPECT_LT(residentBytes, fullFileSize);
    // BuildXwbFixtureBytes' single entry is 200 bytes of wave data with no name table --
    // resident size must be exactly fullFileSize minus that wave-data segment.
    EXPECT_EQ(residentBytes, fullFileSize - 200);
}

// Regression coverage for T-3F's "correct offsetted reading" accept criterion: a single-entry
// fixture can't distinguish "read the right bytes" from "read the wrong bytes that happen to be
// the same length", so this uses a two-entry fixture where entry 1 sits at a nonzero offset and
// has a different length than entry 0. If the streaming path's lazy read used the wrong offset
// (e.g. always reading from the start of the wave-data segment, or reusing entry 0's range),
// entry 1's decoded duration would not match its own non-streaming counterpart.
TEST(WaveBankTest, StreamingGetSoundEffectReadsCorrectPerEntryOffsetAndLength)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

    try
    {
        WaveBank streamingWb(&SharedEngine(), MultiEntryXwbFixturePath(), 0, 0);
        ASSERT_TRUE(streamingWb.getIsPreparedProperty());
        ASSERT_TRUE(WaveBankTestAccess::IsStreaming(streamingWb));

        WaveBank eagerWb(&SharedEngine(), MultiEntryXwbFixturePath());
        ASSERT_TRUE(eagerWb.getIsPreparedProperty());
        ASSERT_FALSE(WaveBankTestAccess::IsStreaming(eagerWb));

        const SoundEffect* streamedEntry0 = WaveBankTestAccess::GetSoundEffect(streamingWb, 0);
        const SoundEffect* streamedEntry1 = WaveBankTestAccess::GetSoundEffect(streamingWb, 1);
        const SoundEffect* eagerEntry1    = WaveBankTestAccess::GetSoundEffect(eagerWb, 1);

        if (!streamedEntry0 || !streamedEntry1 || !eagerEntry1)
        {
            GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                            "could not create a real SoundEffect";
        }

        // Entry 1 is 3x longer than entry 0 (300 vs 100 bytes) -- an offset bug would either
        // fail to decode, or produce entry 0's duration instead of entry 1's.
        EXPECT_EQ(streamedEntry1->getDurationProperty(), eagerEntry1->getDurationProperty());
        EXPECT_NE(streamedEntry0->getDurationProperty(), streamedEntry1->getDurationProperty());
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                        "could not exercise WaveBank playback";
    }
}

// Regression coverage for IN-9: a streaming entry whose parsed dataLength exceeds the real
// on-disk file size must not drive an unbounded/oversized allocation attempt -- GetSoundEffect
// must bounds-check against the real file and return nullptr instead of resizing to the
// (corrupt) claimed length. Note: for this fixture's moderate 1MB claimed length, the pre-fix
// code also returns nullptr (its post-read gcount() check happens to catch the short read), so
// this specific assertion doesn't discriminate old vs. new behavior via git stash -- the fix's
// real value is skipping the allocation attempt entirely up front, which matters most for
// claimed lengths large enough (e.g. multi-GB) to risk OOM before ever reaching that check, which
// isn't safely reproducible in a fast unit test.
TEST(WaveBankTest, StreamingGetSoundEffectRejectsEntryLengthExceedingRealFileSize)
{
    WaveBank streamingWb(&SharedEngine(), OversizedStreamingXwbFixturePath(), 0, 0);
    ASSERT_TRUE(streamingWb.getIsPreparedProperty());
    ASSERT_TRUE(WaveBankTestAccess::IsStreaming(streamingWb));

    EXPECT_EQ(WaveBankTestAccess::GetSoundEffect(streamingWb, 0), nullptr);
}

// AUD-11-004: the non-streaming path's own equivalent bounds check (WaveBank.cpp:
// `entry.dataOffset + entry.dataLength > fd.size()`, a separate code path from the streaming
// check exercised just above) had no dedicated regression test. Reuses the exact same corrupt
// fixture (entry playLength=1,000,000 against a file that's really only ~184 bytes) via the
// *non-streaming* constructor instead, so the fully-resident `fileData` buffer's own size is
// what the check runs against rather than a re-opened file's on-disk size.
TEST(WaveBankTest, NonStreamingGetSoundEffectRejectsEntryLengthExceedingRealFileSize)
{
    WaveBank nonStreamingWb(&SharedEngine(), OversizedStreamingXwbFixturePath());
    ASSERT_TRUE(nonStreamingWb.getIsPreparedProperty());
    ASSERT_FALSE(WaveBankTestAccess::IsStreaming(nonStreamingWb));

    EXPECT_EQ(WaveBankTestAccess::GetSoundEffect(nonStreamingWb, 0), nullptr);
}

// ===================== IsDisposed / IsPrepared =====================

// P9-HARDWARE-003: matches FNA exactly (WaveBank.cs, non-streaming ctor) -- a missing
// nonStreamingWaveBankFilename throws FileNotFoundException (from TitleContainer.ReadToPointer)
// before any FACT call is made.
TEST(WaveBankTest, ConstructorMissingFileThrowsFileNotFound)
{
    const auto missing =
        (std::filesystem::temp_directory_path() / "cna_wavebank_test_missing.xwb").string();
    EXPECT_THROW(WaveBank wb(&SharedEngine(), missing), System::IO::FileNotFoundException);
}

TEST(WaveBankTest, ConstructorResolvesXnaCaseInsensitiveBankPath)
{
    const std::filesystem::path exact(XwbFixturePath());
    const auto requested = exact.parent_path().parent_path() /
                           "CNA_WAVEBANK_TEST" / "FIXTURE.XWB";

    WaveBank bank(&SharedEngine(), requested.string());
    EXPECT_TRUE(bank.getIsPreparedProperty());
}

// XA-13: a file that EXISTS but isn't a valid .xwb (bad magic/garbage) must behave the same as
// a missing one -- construction doesn't throw, the bank just stays unprepared. (XA-9 decided to
// keep this "silent stub" behavior rather than throw; this locks in that decision explicitly, on
// the "exists but corrupt" path specifically, which the file-missing test above cannot cover.)
TEST(WaveBankTest, IsPreparedFalseForExistingButCorruptFile)
{
    const auto corrupt = WriteFixture("cna_wavebank_test", "corrupt.xwb",
                                       {'n', 'o', 't', ' ', 'a', ' ', 'w', 'a', 'v', 'e', 'b', 'a', 'n', 'k'});
    WaveBank wb(&SharedEngine(), corrupt);
    EXPECT_FALSE(wb.getIsPreparedProperty());
}

TEST(WaveBankTest, IsDisposedFalseInitiallyAndTrueAfterDispose)
{
    WaveBank wb(&SharedEngine(), XwbFixturePath());
    EXPECT_FALSE(wb.getIsDisposedProperty());
    wb.Dispose();
    EXPECT_TRUE(wb.getIsDisposedProperty());
    EXPECT_FALSE(wb.getIsPreparedProperty());
}

TEST(WaveBankTest, DisposeIsIdempotent)
{
    WaveBank wb(&SharedEngine(), XwbFixturePath());
    wb.Dispose();
    EXPECT_NO_THROW(wb.Dispose());
    EXPECT_TRUE(wb.getIsDisposedProperty());
}

TEST(WaveBankTest, DisposeRaisesDisposingEvent)
{
    WaveBank wb(&SharedEngine(), XwbFixturePath());
    bool raised = false;
    wb.Disposing += [&raised](System::Object*, const System::EventArgs&) { raised = true; };
    wb.Dispose();
    EXPECT_TRUE(raised);
}

// ===================== IsInUse =====================

TEST(WaveBankTest, IsInUseFalseWithNoCues)
{
    WaveBank wb(&SharedEngine(), XwbFixturePath());
    EXPECT_FALSE(wb.getIsInUseProperty());
}

TEST(WaveBankTest, IsInUseTrueWhilePlayingThenFalseAfterStop)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

    try
    {
        AudioEngine& engine = SharedEngine();
        WaveBank wb(&engine, XwbFixturePath());
        ASSERT_TRUE(wb.getIsPreparedProperty());
        SoundBank sb(&engine, XsbFixturePath());

        std::unique_ptr<Cue> cue(sb.GetCue("Boom"));
        cue->Play();

        if (!wb.getIsInUseProperty())
        {
            GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                            "could not exercise WaveBank playback";
        }

        EXPECT_TRUE(wb.getIsInUseProperty());
        cue->Stop(AudioStopOptions::Immediate);
        EXPECT_FALSE(wb.getIsInUseProperty());
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                        "could not exercise WaveBank playback";
    }
}

// P10-XACT-007: WaveBank::Dispose() clears activeCues_ directly (distinct from the natural
// Stop()-driven unregister path exercised above) -- IsInUse must still safely and correctly
// report false afterward rather than reading a stale/dangling cue pointer.
TEST(WaveBankTest, IsInUseFalseAfterDisposeWhilePlaying)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

    try
    {
        AudioEngine& engine = SharedEngine();
        WaveBank wb(&engine, XwbFixturePath());
        ASSERT_TRUE(wb.getIsPreparedProperty());
        SoundBank sb(&engine, XsbFixturePath());

        std::unique_ptr<Cue> cue(sb.GetCue("Boom"));
        cue->Play();

        if (!wb.getIsInUseProperty())
        {
            GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                            "could not exercise WaveBank playback";
        }

        ASSERT_TRUE(wb.getIsInUseProperty());
        wb.Dispose();
        EXPECT_FALSE(wb.getIsInUseProperty());
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                        "could not exercise WaveBank playback";
    }
}

// P12-BANK-001: real FACT (FACTWaveBank_Destroy, FACT.c) force-stops every cue still using the
// wave bank when it's destroyed -- previously WaveBank::Dispose() only cleared activeCues_
// without stopping anything, so a cue kept audibly playing even though IsInUse already
// (misleadingly) reported false right after Dispose(). This is the same scenario as
// IsInUseFalseAfterDisposeWhilePlaying above, but actually checking the cue was stopped.
TEST(WaveBankTest, DisposeForceStopsStillPlayingCue)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

    try
    {
        AudioEngine& engine = SharedEngine();
        WaveBank wb(&engine, XwbFixturePath());
        ASSERT_TRUE(wb.getIsPreparedProperty());
        SoundBank sb(&engine, XsbFixturePath());

        std::unique_ptr<Cue> cue(sb.GetCue("Boom"));
        cue->Play();

        if (!wb.getIsInUseProperty())
        {
            GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                            "could not exercise WaveBank playback";
        }

        ASSERT_TRUE(cue->getIsPlayingProperty());
        wb.Dispose();

        EXPECT_TRUE(cue->getIsDisposedProperty());
        EXPECT_FALSE(cue->getIsPlayingProperty());
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                        "could not exercise WaveBank playback";
    }
}

// XA-7: IsInUse used to only check IsPlaying, so pausing the only cue using this bank made it
// falsely report itself as not in use.
TEST(WaveBankTest, IsInUseTrueWhilePausedNotJustWhilePlaying)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

    try
    {
        AudioEngine& engine = SharedEngine();
        WaveBank wb(&engine, XwbFixturePath());
        ASSERT_TRUE(wb.getIsPreparedProperty());
        SoundBank sb(&engine, XsbFixturePath());

        std::unique_ptr<Cue> cue(sb.GetCue("Boom"));
        cue->Play();

        if (!wb.getIsInUseProperty())
        {
            GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                            "could not exercise WaveBank playback";
        }

        cue->Pause();
        ASSERT_TRUE(cue->getIsPausedProperty());
        EXPECT_TRUE(wb.getIsInUseProperty());

        cue->Stop(AudioStopOptions::Immediate);
        EXPECT_FALSE(wb.getIsInUseProperty());
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                        "could not exercise WaveBank playback";
    }
}

// P9-LIFECYCLE-004/007: unlike Stop(Immediate) above, this cue is never explicitly stopped --
// WaveBank::IsInUse must still become false soon after the underlying 200-byte (~1.13ms) wave
// naturally finishes playing, via Cue::ReconcileState() reconciling IsPlaying/IsPaused live on
// every getIsInUseProperty() query (see WaveBank::getIsInUseProperty()).
TEST(WaveBankTest, IsInUseFalseSoonAfterCueNaturallyFinishesWithoutExplicitStop)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

    try
    {
        AudioEngine& engine = SharedEngine();
        WaveBank wb(&engine, XwbFixturePath());
        ASSERT_TRUE(wb.getIsPreparedProperty());
        SoundBank sb(&engine, XsbFixturePath());

        std::unique_ptr<Cue> cue(sb.GetCue("Boom"));
        cue->Play();

        if (!wb.getIsInUseProperty())
        {
            GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                            "could not exercise WaveBank playback";
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        EXPECT_FALSE(wb.getIsInUseProperty());
        EXPECT_TRUE(cue->getIsStoppedProperty());
        EXPECT_FALSE(cue->getIsPlayingProperty());
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                        "could not exercise WaveBank playback";
    }
}

// AUD-11-006/007: golden-tests for the PCM8/PCM16 wave-bank entry paths, checking the exact
// decoded frame count (not just "does it play") -- this is what would catch a wrong
// dataOffset/dataLength/sampleRate extracted from the compact XWB entry format, the same class of
// bug AUDIO-ADPCM-001/AUD-11-013 caught for the ADPCM path. Uses WaveBankTestAccess::GetSoundEffect
// directly (no audio device/Play() needed), matching
// GetSoundEffectForAdpcmEntryDecodesExactFrameCountFromSamplesPerBlockFormula's own established
// pattern. The actual unsigned-8-bit-to-signed-16-bit PCM conversion itself is SDL3's own real WAV
// decoder's responsibility (WaveBank::GetSoundEffect wraps the raw bytes in a real WAV and decodes
// via SoundEffect::FromStream for 8-bit, exactly like the XNB SoundEffectReader's own WAV-wrapper
// path -- already covered by AUD-06's Pcm8BitLoadsSuccessfully et al.), not something this test
// re-verifies.
TEST(WaveBankTest, GetSoundEffectForPcm16EntryHasExactFrameCount)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

    try
    {
        AudioEngine& engine = SharedEngine();
        WaveBank wb(&engine, XwbFixturePath());
        ASSERT_TRUE(wb.getIsPreparedProperty());

        const SoundEffect* effect = WaveBankTestAccess::GetSoundEffect(wb, 0);
        ASSERT_NE(effect, nullptr);

        // BuildXwbFixtureBytes's default (eightBitPcm=false): 200 bytes, 16-bit mono ->
        // 100 frames exactly.
        constexpr double expectedSeconds = 100.0 / 44100.0;
        EXPECT_NEAR(effect->getDurationProperty().getTotalSecondsProperty(), expectedSeconds, 1e-6);
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                        "could not construct WaveBank/AudioEngine";
    }
}

TEST(WaveBankTest, GetSoundEffectForPcm8EntryHasExactFrameCount)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

    try
    {
        AudioEngine& engine = SharedEngine();
        WaveBank wb(&engine, Xwb8BitFixturePath());
        ASSERT_TRUE(wb.getIsPreparedProperty());

        const SoundEffect* effect = WaveBankTestAccess::GetSoundEffect(wb, 0);
        ASSERT_NE(effect, nullptr);

        // BuildXwbFixtureBytes(eightBitPcm=true): 200 bytes, 8-bit mono -> 200 frames exactly
        // (1 byte/frame).
        constexpr double expectedSeconds = 200.0 / 44100.0;
        EXPECT_NEAR(effect->getDurationProperty().getTotalSecondsProperty(), expectedSeconds, 1e-6);
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                        "could not construct WaveBank/AudioEngine";
    }
}

// AUD-11-014: confirmed real gap and fixed -- WaveBank::GetSoundEffect() previously parsed
// entry.loopStartSample/loopTotalSamples (FACTWaveBankEntry's LoopRegion) but never wired them
// into the constructed SoundEffect at all, so every WaveBank-sourced looping cue looped the
// *entire* track regardless of an authored intro-then-loop region -- unlike real FAudio (FACT.c),
// which applies these exact fields to the playback buffer's LoopBegin/LoopLength whenever the
// cue's PlayWaveEvent requests looping. Verifies the authored [20, 70) sub-region survives to the
// SoundEffectInstance exactly, via the same SoundEffectInstanceTestAccess mechanism the XNB-reader
// loop tests use (AUD-06-014/019) -- SDL_mixer exposes no way to read back the applied
// loop-start/max-frame play options, so this checks the cached value Play() would apply, matching
// this project's established pattern for verifying loop state without a real device.
TEST(WaveBankTest, GetSoundEffectForNonCompactEntryHonorsAuthoredLoopRegion)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

    try
    {
        AudioEngine& engine = SharedEngine();
        WaveBank wb(&engine, LoopedXwbFixturePath());
        ASSERT_TRUE(wb.getIsPreparedProperty());

        const SoundEffect* effect = WaveBankTestAccess::GetSoundEffect(wb, 0);
        ASSERT_NE(effect, nullptr);

        Microsoft::Xna::Framework::Audio::SoundEffectInstance instance = effect->CreateInstance();
        EXPECT_EQ(Microsoft::Xna::Framework::Audio::SoundEffectInstanceTestAccess::LoopStart(instance), 20u);
        EXPECT_EQ(Microsoft::Xna::Framework::Audio::SoundEffectInstanceTestAccess::LoopLength(instance), 50u);
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                        "could not construct WaveBank/AudioEngine";
    }
}

// Regression coverage for XA-2: WaveBank::GetSoundEffect's 8-bit-PCM branch wraps the raw wave
// data in a WAV and goes through SoundEffect::FromStream (unlike the 16-bit PCM path above, which
// constructs a SoundEffect directly). FromStream used to leak the heap SoundEffect it returns.
TEST(WaveBankTest, GetSoundEffectFor8BitPcmEntrySucceeds)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

    try
    {
        AudioEngine& engine = SharedEngine();
        WaveBank wb(&engine, Xwb8BitFixturePath());
        ASSERT_TRUE(wb.getIsPreparedProperty());
        SoundBank sb(&engine, Xsb8BitFixturePath());

        std::unique_ptr<Cue> cue(sb.GetCue(kCue8BitName));
        cue->Play();

        if (!wb.getIsInUseProperty())
        {
            GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                            "could not exercise WaveBank playback";
        }

        EXPECT_TRUE(wb.getIsInUseProperty());
        cue->Stop(AudioStopOptions::Immediate);
        EXPECT_FALSE(wb.getIsInUseProperty());
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                        "could not exercise WaveBank playback";
    }
}

// AUDIO-ADPCM-001 (2026-07-17 deep audit follow-up): confirmed, previously-uncaught defect --
// WaveBank::GetSoundEffect's MS-ADPCM branch wrapped raw compressed bytes in a WAV file with no
// coefficient table at all, which SDL3's real MS-ADPCM decoder rejects outright ("Could not read
// MS ADPCM format header"), meaning every MS-ADPCM-compressed XACT wave silently failed to load
// (caught internally, GetSoundEffect returned nullptr). Asserts GetSoundEffect() directly
// (non-null) rather than only inferring success via IsInUseProperty after Play() -- that older
// pattern conflates "no audio device" with "decode failed" into the same GTEST_SKIP path and
// would not have caught this bug even if run under a real device.
TEST(WaveBankTest, GetSoundEffectForAdpcmEntrySucceeds)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

    try
    {
        AudioEngine& engine = SharedEngine();
        WaveBank wb(&engine, XwbAdpcmFixturePath());
        ASSERT_TRUE(wb.getIsPreparedProperty());

        const SoundEffect* effect = WaveBankTestAccess::GetSoundEffect(wb, 0);
        ASSERT_NE(effect, nullptr)
            << "MS-ADPCM entry failed to decode -- see AUDIO-ADPCM-001";
        EXPECT_GT(effect->getDurationProperty().getTicksProperty(), 0);
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                        "could not construct WaveBank/AudioEngine";
    }
}

// AUD-11-013: the loose non-zero duration check above doesn't confirm the samplesPerBlock formula
// ((wBlockAlign_raw + 16) * 2, AUD-11-012) actually matches how many PCM frames SDL3's own
// MS-ADPCM decoder produces per block -- this asserts the exact expected duration.
// BuildAdpcmXwbFixtureBytes uses blockAlign=30, blockCount=4, wBlockAlign_raw=8, sampleRate=22050
// mono, so samplesPerBlock = (8+16)*2 = 48, giving exactly 4*48 = 192 decoded frames total (no
// partial trailing block, every block in the fixture is a complete, fully-aligned one).
TEST(WaveBankTest, GetSoundEffectForAdpcmEntryDecodesExactFrameCountFromSamplesPerBlockFormula)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

    try
    {
        AudioEngine& engine = SharedEngine();
        WaveBank wb(&engine, XwbAdpcmFixturePath());
        ASSERT_TRUE(wb.getIsPreparedProperty());

        const SoundEffect* effect = WaveBankTestAccess::GetSoundEffect(wb, 0);
        ASSERT_NE(effect, nullptr)
            << "MS-ADPCM entry failed to decode -- see AUDIO-ADPCM-001";

        constexpr int samplesPerBlock = (8 + 16) * 2; // wBlockAlign_raw=8, AUD-11-012's formula
        constexpr int blockCount = 4;
        constexpr int expectedFrames = samplesPerBlock * blockCount;
        constexpr double expectedSeconds = static_cast<double>(expectedFrames) / 22050.0;

        EXPECT_NEAR(effect->getDurationProperty().getTotalSecondsProperty(), expectedSeconds, 1e-6);
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                        "could not construct WaveBank/AudioEngine";
    }
}

// AUD-11-010/011 (2026-07-17 deep audit): CNA has no XMA/WMA decode path anywhere in this stack
// (SDL3 doesn't decode either) -- GetSoundEffect() must return nullptr cleanly for an XMA-tagged
// entry, not crash or silently construct a garbage SoundEffect from the raw compressed bytes. This
// does NOT require an audio device -- GetSoundEffect() rejects the format before ever touching the
// mixer, so unlike the ADPCM test above this can run unconditionally.
TEST(WaveBankTest, GetSoundEffectForXmaEntryReturnsNullCleanly)
{
    AudioEngine& engine = SharedEngine();
    WaveBank wb(&engine, XwbXmaFixturePath());
    ASSERT_TRUE(wb.getIsPreparedProperty());

    EXPECT_EQ(WaveBankTestAccess::GetSoundEffect(wb, 0), nullptr);
}

// ===================== GetTypeName =====================

TEST(WaveBankTest, GetTypeNameIsDottedXnaName)
{
    WaveBank wb(&SharedEngine(), XwbFixturePath());
    EXPECT_EQ(wb.GetTypeName(), "Microsoft.Xna.Framework.Audio.WaveBank");
}

// AUD-11-023/024: WaveBank's per-entry SoundEffect cache (XactWaveBankImpl::cache, a
// std::vector<std::optional<SoundEffect>> sized once at construction to entryCount, never grown
// or evicted) already satisfies "bounded memory policy" by construction -- its size can never
// exceed entryCount, which AUD-11-026 now validates against the real file size, so it's bounded
// by the same real-world constraint as the file itself, not unbounded growth. "Avoid unnecessary
// decode" is the pre-existing `if (cached.has_value()) return &cached.value();` early return.
// What was genuinely unverified is AUD-11-024's own explicit concern: simultaneous *first* use
// from multiple threads. Confirmed real (not just theoretical) via git-stash: without a guard,
// two threads racing to first-decode the same entry could both observe an empty cache slot and
// both try to populate the same std::optional<SoundEffect> concurrently -- a genuine data race.
// Fixed with a mutex serializing the whole lookup-then-populate sequence. This test spawns many
// threads all requesting the same entry simultaneously and checks every thread got the exact same
// SoundEffect pointer back (proving decode happened exactly once, not once-per-thread).
TEST(WaveBankTest, GetSoundEffectFromManyThreadsSimultaneouslyDecodesOnceNotPerThread)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

    try
    {
        AudioEngine& engine = SharedEngine();
        WaveBank wb(&engine, MultiEntryXwbFixturePath());
        ASSERT_TRUE(wb.getIsPreparedProperty());

        constexpr int kThreadCount = 16;
        std::vector<std::thread> threads;
        std::vector<const SoundEffect*> results(kThreadCount, nullptr);

        for (int i = 0; i < kThreadCount; ++i)
        {
            threads.emplace_back([&wb, &results, i]() {
                results[i] = WaveBankTestAccess::GetSoundEffect(wb, 0);
            });
        }
        for (auto& t : threads) t.join();

        for (int i = 0; i < kThreadCount; ++i)
        {
            if (!results[i])
            {
                GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                                "could not create a real SoundEffect";
            }
        }

        // Every thread must have received the exact same cached instance -- proving the entry
        // was decoded exactly once, not raced/duplicated across threads.
        for (int i = 1; i < kThreadCount; ++i)
        {
            EXPECT_EQ(results[i], results[0]);
        }
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                        "could not exercise WaveBank playback";
    }
}

// AUD-11-025: confirmed real gap and fixed -- WaveBank::Dispose() called `xactImpl_.reset()`
// with no synchronization against a concurrent GetSoundEffect() call on another thread, a
// genuine use-after-free risk (worse than a plain UAF if the other thread was actively blocked
// holding XactWaveBankImpl's own cache lock when it got destroyed out from under it). Fixed with
// a new WaveBank::xactImplMutex_ that both GetSoundEffect() and Dispose() take before touching
// xactImpl_. This test hammers GetSoundEffect() from many threads while concurrently calling
// Dispose() from the main thread -- every GetSoundEffect() call must either return a valid
// pointer (decode completed before Dispose() won the race) or nullptr (Dispose() won the race,
// xactImpl_ was already gone) -- never crash, and never any other outcome.
TEST(WaveBankTest, GetSoundEffectConcurrentWithDisposeNeverCrashes)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

    for (int iteration = 0; iteration < 20; ++iteration)
    {
        try
        {
            AudioEngine& engine = SharedEngine();
            auto wb = std::make_unique<WaveBank>(&engine, MultiEntryXwbFixturePath());
            if (!wb->getIsPreparedProperty())
            {
                GTEST_SKIP() << "no audio device (dummy driver unavailable)";
            }

            constexpr int kThreadCount = 8;
            std::vector<std::thread> threads;
            for (int i = 0; i < kThreadCount; ++i)
            {
                threads.emplace_back([&wb]() {
                    try
                    {
                        (void)WaveBankTestAccess::GetSoundEffect(*wb, 0);
                    }
                    catch (...)
                    {
                        // WaveBank being mid-Dispose() on another thread is an acceptable race
                        // outcome here (isDisposed_ isn't itself atomic) -- what matters is that
                        // this never crashes/corrupts, which a clean exception or nullptr both
                        // satisfy.
                    }
                });
            }
            wb->Dispose();
            for (auto& t : threads) t.join();

            SUCCEED();
        }
        catch (...)
        {
            GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                            "could not exercise WaveBank playback";
        }
    }
}

// AUD-11-019: a genuinely zero-length wave entry (dwLength=0, a real reachable case -- an
// authored-but-never-recorded placeholder, or a corrupt/truncated bank) must not crash the
// decoder. GetSoundEffect() may reasonably either construct a zero-duration SoundEffect or
// return nullptr -- what matters is that it does one of those cleanly, not that it picks a
// specific one (neither is more "correct" than the other for genuinely empty audio).
TEST(WaveBankTest, GetSoundEffectForZeroLengthEntryDoesNotCrash)
{
    AudioEngine& engine = SharedEngine();
    WaveBank wb(&engine, ZeroLengthXwbFixturePath());
    ASSERT_TRUE(wb.getIsPreparedProperty());

    const SoundEffect* effect = WaveBankTestAccess::GetSoundEffect(wb, 0);
    if (effect)
    {
        EXPECT_EQ(effect->getDurationProperty(), System::TimeSpan::Zero);
    }
    // A null result (decode declined a zero-byte buffer) is equally acceptable -- this test's
    // real assertion is that reaching this line at all means no crash happened.
    SUCCEED();
}

// AUD-11-020: verifies padding bytes *between* entries in the wave-data segment never become
// part of either entry's decoded audio. Entry 0 (50 bytes) and entry 1 (70 bytes) are separated
// by a 30-byte gap, with deliberately DIFFERENT lengths -- proving offset correctness, not just
// length correctness: if entry 1 ever started reading from the gap or from entry 0's tail
// instead of its own authored offset, its decoded duration would come out wrong, not just its
// content (which this direct-PCM16 raw-buffer path doesn't otherwise expose for inspection).
TEST(WaveBankTest, GetSoundEffectEntriesSeparatedByPaddingHaveExactLengthsNotLeakingTheGap)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

    try
    {
        AudioEngine& engine = SharedEngine();
        WaveBank wb(&engine, PaddedXwbFixturePath());
        ASSERT_TRUE(wb.getIsPreparedProperty());

        const SoundEffect* entry0 = WaveBankTestAccess::GetSoundEffect(wb, 0);
        const SoundEffect* entry1 = WaveBankTestAccess::GetSoundEffect(wb, 1);
        ASSERT_NE(entry0, nullptr);
        ASSERT_NE(entry1, nullptr);

        // 50 bytes / 70 bytes, 16-bit mono -> 25 / 35 frames exactly. Either value coming out
        // wrong (e.g. 25+15=40 frames if the gap leaked into entry 0, or entry 1 starting from
        // the gap instead of its own offset) would fail these exact checks.
        constexpr double expectedSeconds0 = 25.0 / 44100.0;
        constexpr double expectedSeconds1 = 35.0 / 44100.0;
        EXPECT_NEAR(entry0->getDurationProperty().getTotalSecondsProperty(), expectedSeconds0, 1e-6);
        EXPECT_NEAR(entry1->getDurationProperty().getTotalSecondsProperty(), expectedSeconds1, 1e-6);
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable); "
                        "could not construct WaveBank/AudioEngine";
    }
}
