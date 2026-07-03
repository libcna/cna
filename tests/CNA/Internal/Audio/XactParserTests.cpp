// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "CNA/Internal/Audio/XactTypes.hpp"

#include <cstring>
#include <string>
#include <vector>

using CNA::Internal::Audio::ParseXgs;
using CNA::Internal::Audio::ParseXsb;
using CNA::Internal::Audio::ParseXwb;
using CNA::Internal::Audio::XgsData;
using CNA::Internal::Audio::XsbData;
using CNA::Internal::Audio::XwbData;
using CNA::Internal::Audio::XwbFormat;

namespace
{
    void AppendU8(std::vector<uint8_t>& buf, uint8_t value)
    {
        buf.push_back(value);
    }

    void AppendU16(std::vector<uint8_t>& buf, uint16_t value)
    {
        uint8_t bytes[2];
        std::memcpy(bytes, &value, 2);
        buf.insert(buf.end(), bytes, bytes + 2);
    }

    void AppendS16(std::vector<uint8_t>& buf, int16_t value)
    {
        uint8_t bytes[2];
        std::memcpy(bytes, &value, 2);
        buf.insert(buf.end(), bytes, bytes + 2);
    }

    void AppendU32(std::vector<uint8_t>& buf, uint32_t value)
    {
        uint8_t bytes[4];
        std::memcpy(bytes, &value, 4);
        buf.insert(buf.end(), bytes, bytes + 4);
    }

    void AppendS32(std::vector<uint8_t>& buf, int32_t value)
    {
        uint8_t bytes[4];
        std::memcpy(bytes, &value, 4);
        buf.insert(buf.end(), bytes, bytes + 4);
    }

    void AppendF32(std::vector<uint8_t>& buf, float value)
    {
        uint8_t bytes[4];
        std::memcpy(bytes, &value, 4);
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

    // Minimal .xgs with one category ("Default") and one variable ("Volume", initial 0.5) —
    // direct regression coverage for ParseXgs's category/variable parsing (T-2F cleanup safety net).
    std::vector<uint8_t> BuildXgsFixture()
    {
        constexpr uint32_t headerSize       = 65;
        constexpr uint32_t categoryDataSize = 10;
        constexpr uint32_t variableDataSize = 13;

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
        AppendU8(data, 255);
        AppendU16(data, 100);
        AppendU16(data, 200);
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

    // Minimal compact .xwb with 3 entries: two whose length must be derived from the gap to
    // the next entry's offset minus the compact deviation field, and one trailing entry whose
    // length comes from the remainder of the wave-data segment (regression fixture for T-2D).
    std::vector<uint8_t> BuildCompactXwbFixture()
    {
        constexpr uint32_t alignment          = 4;
        constexpr uint32_t headerSize         = 48;   // magic + version + 5 * {offset,length}
        constexpr uint32_t bankDataSize       = 96;   // flags+count+name[64]+metaSize+nameSize+align+fmt+buildTime
        constexpr uint32_t entryCount         = 3;
        constexpr uint32_t entryMetaDataSize  = 4;    // packed {offsetUnits:21, deviation:11}, no extra bytes
        constexpr uint32_t entryMetaSegSize   = entryCount * entryMetaDataSize;
        constexpr uint32_t waveDataLength     = 29;   // 12 (entry0 aligned) + 8 (entry1 aligned) + 9 (entry2)

        const uint32_t segOffset[5] = {
            headerSize,
            headerSize + bankDataSize,
            headerSize + bankDataSize + entryMetaSegSize,
            headerSize + bankDataSize + entryMetaSegSize,
            headerSize + bankDataSize + entryMetaSegSize,
        };
        const uint32_t segLength[5] = { bankDataSize, entryMetaSegSize, 0, 0, waveDataLength };

        std::vector<uint8_t> data;
        data.reserve(headerSize + bankDataSize + entryMetaSegSize + waveDataLength);

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
        AppendPadded(data, "T-2D", 64); // bank name
        AppendU32(data, entryMetaDataSize);
        AppendU32(data, 0); // entryNameElementSize
        AppendU32(data, alignment);
        const uint32_t compactFormat =
              (0u)          // format tag: PCM
            | (0u << 2)     // channels - 1 = 0 -> mono
            | (44100u << 5) // sample rate
            | (2u << 23)    // wBlockAlign
            | (1u << 31);   // bits-per-sample flag -> 16-bit
        AppendU32(data, compactFormat);
        for (int i = 0; i < 8; ++i) data.push_back(0); // buildTime

        // offset units are relative to `alignment`; deviation is the aligned-span slack in bytes.
        AppendU32(data, (0u) | (2u << 21)); // entry 0: offset=0*4=0,  aligned span=12, deviation=2 -> length 10
        AppendU32(data, (3u) | (2u << 21)); // entry 1: offset=3*4=12, aligned span=8,  deviation=2 -> length 6
        AppendU32(data, (5u));              // entry 2 (last): offset=5*4=20, length = remaining segment (9)

        for (uint32_t i = 0; i < waveDataLength; ++i)
            data.push_back(static_cast<uint8_t>(i));

        return data;
    }

    // Minimal non-compact .xwb with entryMetaDataSize == 12 (only dwFlagsAndDuration/Format/
    // PlayRegion.dwOffset present; dwLength/LoopRegion absent). The entry metadata segment ends
    // exactly at the file's end, so a parser that unconditionally reads all 24 bytes of
    // FACTWaveBankEntry before checking entryMetaDataSize would either read the following
    // entry's bytes as its own loop fields, or overrun the buffer entirely for the last entry
    // (regression fixture for IN-2).
    std::vector<uint8_t> BuildNonCompactXwbWithNarrowEntryMetaData()
    {
        constexpr uint32_t headerSize        = 48;  // magic + version + 5 * {offset,length}
        constexpr uint32_t bankDataSize      = 96;  // flags+count+name[64]+metaSize+nameSize+align+fmt+buildTime
        constexpr uint32_t entryCount        = 2;
        constexpr uint32_t entryMetaDataSize = 12;  // flagsAndDuration(4) + fmt(4) + playOffset(4)
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
        data.reserve(headerSize + bankDataSize + entryMetaSegSize);

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
        AppendPadded(data, "IN-2", 64); // bank name
        AppendU32(data, entryMetaDataSize);
        AppendU32(data, 0); // entryNameElementSize
        AppendU32(data, 4); // alignment (unused, non-compact)
        AppendU32(data, 0); // compactFormat (unused, non-compact)
        for (int i = 0; i < 8; ++i) data.push_back(0); // buildTime

        // Entry 0: flagsAndDuration=0x11111111, fmt=0 (PCM/mono/0Hz/8-bit), playOffset=100
        AppendU32(data, 0x11111111u);
        AppendU32(data, 0u);
        AppendU32(data, 100u);

        // Entry 1 (last, ends exactly at file end): flagsAndDuration=0x22222222, fmt=0, playOffset=200
        AppendU32(data, 0x22222222u);
        AppendU32(data, 0u);
        AppendU32(data, 200u);

        return data;
    }

    // Builds one PITCH-family track event using the "equation" (non-ramp) form — a stand-in
    // for any non-PlayWave event (PITCH/VOLUME/MARKER) the track-event walker must skip over.
    std::vector<uint8_t> BuildPitchEventBytes()
    {
        std::vector<uint8_t> e;
        AppendU32(e, 7u);       // evtInfo: type=FACTEVENT_PITCH (7), timestamp=0
        AppendU16(e, 0);        // randomOffset
        AppendU8(e, 0xFF);      // separator
        AppendU8(e, 0);         // settings: RAMP bit clear -> equation form
        AppendU8(e, 0x04);      // equation.flags (EVENT_EQUATION_VALUE)
        AppendF32(e, 0.0f);     // value1
        AppendF32(e, 0.0f);     // value2
        for (int i = 0; i < 5; ++i) e.push_back(0); // unknown
        return e;
    }

    // Builds one basic (non-variation) PlayWave track event.
    std::vector<uint8_t> BuildPlayWaveEventBytes(uint16_t waveIdx, uint8_t wbIdx, uint8_t loopCnt)
    {
        std::vector<uint8_t> e;
        AppendU32(e, 1u);       // evtInfo: type=FACTEVENT_PLAYWAVE (1), timestamp=0
        AppendU16(e, 0);        // randomOffset
        AppendU8(e, 0xFF);      // separator
        AppendU8(e, 0);         // flags
        AppendU16(e, waveIdx);
        AppendU8(e, wbIdx);
        AppendU8(e, loopCnt);
        AppendU16(e, 0);        // position
        AppendU16(e, 0);        // angle
        return e;
    }

    // Minimal .xsb with zero cues/wavebanks and one complex sound whose single track's event
    // list is exactly `events` (each pre-encoded via the Build*EventBytes helpers above).
    // Regression fixture for T-2E.
    std::vector<uint8_t> BuildXsbWithComplexTrack(const std::vector<std::vector<uint8_t>>& events)
    {
        constexpr uint32_t headerSize     = 74;
        constexpr uint32_t bankNameSize   = 64;
        constexpr uint32_t baseOffset     = headerSize + bankNameSize; // 138
        constexpr uint32_t soundEntrySize = 19; // flags+cat+vol+pitch+prio+len(9) + trackCount+track(10)

        const uint32_t soundOffset       = baseOffset;
        const uint32_t trackEventsOffset = soundOffset + soundEntrySize;

        std::vector<uint8_t> data;

        const char magic[4] = { 'S', 'D', 'B', 'K' };
        data.insert(data.end(), magic, magic + 4);
        AppendU16(data, 46); // contentVersion
        AppendU16(data, 0);  // toolVersion
        AppendU16(data, 0);  // CRC
        for (int i = 0; i < 8; ++i) data.push_back(0); // lastModified
        AppendU8(data, 0);   // platform

        AppendU16(data, 0); // cueSimpleCount
        AppendU16(data, 0); // cueComplexCount
        AppendU16(data, 0); // unknown
        AppendU16(data, 0); // cueTotalAlign
        AppendU8(data, 0);  // wavebankCount
        AppendU16(data, 1); // soundCount
        AppendU16(data, 0); // cueNameLength
        AppendU16(data, 0); // unknown

        AppendS32(data, -1); // cueSimpleOffset
        AppendS32(data, -1); // cueComplexOffset
        AppendS32(data, -1); // cueNameOffset (unused by the parser)
        AppendS32(data, 0);  // unknown
        AppendS32(data, -1); // variationOffset (must sit at header byte 0x32)
        AppendS32(data, 0);  // transitionOffset (unused)
        AppendS32(data, -1); // wavebankNameOffset
        AppendS32(data, 0);  // cueHashOffset (unused)
        AppendS32(data, -1); // cueNameIndexOffset (unused, totalCues == 0)
        AppendS32(data, static_cast<int32_t>(soundOffset));

        AppendPadded(data, "TestSoundBank", bankNameSize);

        // Sound: flags(COMPLEX), categoryIndex, volume, pitchCents, priority, soundLength(skip)
        AppendU8(data, 0x01); // SOUND_FLAG_COMPLEX
        AppendU16(data, 0);   // categoryIndex
        AppendU8(data, 0xFF); // volume byte
        AppendS16(data, 0);   // pitchCents
        AppendU8(data, 0);    // priority
        AppendU16(data, 0);   // soundLength (skipped)

        // One track: volume byte, code (absolute offset to its event array), filterData, frequency
        AppendU8(data, 1);
        AppendU8(data, 0xFF);
        AppendU32(data, trackEventsOffset);
        AppendU16(data, 0); // filterData
        AppendU16(data, 0); // frequency

        // Track event array: eventCount followed by each event's raw bytes
        AppendU8(data, static_cast<uint8_t>(events.size()));
        for (const auto& e : events)
            data.insert(data.end(), e.begin(), e.end());

        return data;
    }

    // Minimal .xsb with two simple (non-complex) sounds: sound 0 has SOUND_FLAG_HAS_DSP set with
    // a leading 2-byte field FAudio marks unused (FACT_internal.c) but this parser used to
    // (incorrectly) treat as a self-inclusive skip length. Sound 1 has a distinctive wave
    // reference; if sound 0's DSP block is mis-skipped, the cursor desyncs and sound 1's fields
    // come out wrong. Regression fixture for IN-1 (plan_audio.md Fáze 7).
    std::vector<uint8_t> BuildXsbWithDspThenSecondSound()
    {
        constexpr uint32_t headerSize   = 74;
        constexpr uint32_t bankNameSize = 64;
        constexpr uint32_t soundOffset  = headerSize + bankNameSize; // 138

        std::vector<uint8_t> data;

        const char magic[4] = { 'S', 'D', 'B', 'K' };
        data.insert(data.end(), magic, magic + 4);
        AppendU16(data, 46); // contentVersion
        AppendU16(data, 0);  // toolVersion
        AppendU16(data, 0);  // CRC
        for (int i = 0; i < 8; ++i) data.push_back(0); // lastModified
        AppendU8(data, 0);   // platform

        AppendU16(data, 0); // cueSimpleCount
        AppendU16(data, 0); // cueComplexCount
        AppendU16(data, 0); // unknown
        AppendU16(data, 0); // cueTotalAlign
        AppendU8(data, 0);  // wavebankCount
        AppendU16(data, 2); // soundCount
        AppendU16(data, 0); // cueNameLength
        AppendU16(data, 0); // unknown

        AppendS32(data, -1); // cueSimpleOffset
        AppendS32(data, -1); // cueComplexOffset
        AppendS32(data, -1); // cueNameOffset
        AppendS32(data, 0);  // unknown
        AppendS32(data, -1); // variationOffset
        AppendS32(data, 0);  // transitionOffset
        AppendS32(data, -1); // wavebankNameOffset
        AppendS32(data, 0);  // cueHashOffset
        AppendS32(data, -1); // cueNameIndexOffset
        AppendS32(data, static_cast<int32_t>(soundOffset));

        AppendPadded(data, "TestSoundBank", bankNameSize);

        // Sound 0: simple (non-complex), SOUND_FLAG_HAS_DSP (0x10) set.
        AppendU8(data, 0x10);  // flags
        AppendU16(data, 0);    // categoryIndex
        AppendU8(data, 0xFF);  // volume byte
        AppendS16(data, 0);    // pitchCents
        AppendU8(data, 0);     // priority
        AppendU16(data, 0);    // soundLength (skipped)
        AppendU16(data, 0);    // simple wave: waveIdx (unused by this test)
        AppendU8(data, 0);     // simple wave: wbIdx (unused by this test)

        // DSP block. Correct total size = 2 (unused length) + 1 (count) + count*4 (codes) = 11 B.
        // The leading field's value (5) would make the OLD buggy `skip(dspLen - 2)` logic consume
        // only 5 bytes total here -- 6 bytes short of sound 1's true start.
        AppendU16(data, 5);       // DSP presets length -- unused per FAudio
        AppendU8(data, 2);        // dspCodeCount
        AppendU32(data, 0xDEADBEEFu);
        AppendU32(data, 0xCAFEBABEu);

        // Sound 1: simple (non-complex), no flags, distinctive wave reference.
        AppendU8(data, 0x00);   // flags
        AppendU16(data, 0);     // categoryIndex
        AppendU8(data, 0xFF);   // volume byte
        AppendS16(data, 0);     // pitchCents
        AppendU8(data, 0);      // priority
        AppendU16(data, 0);     // soundLength (skipped)
        AppendU16(data, 99);    // waveIdx
        AppendU8(data, 5);      // wbIdx

        return data;
    }
}

TEST(XactParserTest, CompactWaveBankComputesLengthsFromConsecutiveOffsets)
{
    const XwbData wb = ParseXwb(BuildCompactXwbFixture());

    ASSERT_EQ(wb.entries.size(), 3u);

    EXPECT_EQ(wb.entries[0].dataOffset, 156u);
    EXPECT_EQ(wb.entries[0].dataLength, 10u);

    EXPECT_EQ(wb.entries[1].dataOffset, 168u);
    EXPECT_EQ(wb.entries[1].dataLength, 6u);

    EXPECT_EQ(wb.entries[2].dataOffset, 176u);
    EXPECT_EQ(wb.entries[2].dataLength, 9u);

    for (const auto& e : wb.entries)
    {
        EXPECT_EQ(e.format, XwbFormat::PCM);
        EXPECT_EQ(e.channels, 1);
        EXPECT_EQ(e.sampleRate, 44100u);
        EXPECT_EQ(e.bitsPerSample, 16);
    }

    // PCM sample count regression check (16-bit mono -> 2 bytes/sample).
    const auto& first = wb.entries[0];
    const uint32_t bytesPerSample = static_cast<uint32_t>(first.bitsPerSample / 8) * first.channels;
    EXPECT_EQ(first.dataLength / bytesPerSample, 5u);
}

TEST(XactParserTest, NonCompactWaveBankWithNarrowEntryMetaDataDoesNotReadForeignBytes)
{
    const XwbData wb = ParseXwb(BuildNonCompactXwbWithNarrowEntryMetaData());

    ASSERT_EQ(wb.entries.size(), 2u);

    // dataLength must come from the "use entire wave data" fallback (entryMetaDataSize < 24),
    // not from bytes belonging to the following entry.
    EXPECT_EQ(wb.entries[0].dataLength, 0u);
    EXPECT_EQ(wb.entries[0].loopStartSample, 0u);
    EXPECT_EQ(wb.entries[0].loopTotalSamples, 0u);

    EXPECT_EQ(wb.entries[1].dataLength, 0u);
    EXPECT_EQ(wb.entries[1].loopStartSample, 0u);
    EXPECT_EQ(wb.entries[1].loopTotalSamples, 0u);
}

TEST(XactParserTest, ComplexTrackSkipsNonPlayEventToFindPlayWave)
{
    const XsbData xsb = ParseXsb(
        BuildXsbWithComplexTrack({ BuildPitchEventBytes(), BuildPlayWaveEventBytes(42, 7, 3) }));

    ASSERT_EQ(xsb.sounds.size(), 1u);
    ASSERT_EQ(xsb.sounds[0].waves.size(), 1u);
    EXPECT_EQ(xsb.sounds[0].waves[0].wavebankIndex, 7);
    EXPECT_EQ(xsb.sounds[0].waves[0].waveIndex, 42u);
    EXPECT_EQ(xsb.sounds[0].waves[0].loopCount, 3);
}

TEST(XactParserTest, ComplexTrackWithOnlyPlayWaveEventStillResolves)
{
    const XsbData xsb = ParseXsb(BuildXsbWithComplexTrack({ BuildPlayWaveEventBytes(99, 2, 1) }));

    ASSERT_EQ(xsb.sounds.size(), 1u);
    ASSERT_EQ(xsb.sounds[0].waves.size(), 1u);
    EXPECT_EQ(xsb.sounds[0].waves[0].wavebankIndex, 2);
    EXPECT_EQ(xsb.sounds[0].waves[0].waveIndex, 99u);
    EXPECT_EQ(xsb.sounds[0].waves[0].loopCount, 1);
}

TEST(XactParserTest, DspBlockIsSkippedByCodeCountNotByLengthField)
{
    const XsbData xsb = ParseXsb(BuildXsbWithDspThenSecondSound());

    ASSERT_EQ(xsb.sounds.size(), 2u);
    ASSERT_EQ(xsb.sounds[1].waves.size(), 1u);
    EXPECT_EQ(xsb.sounds[1].waves[0].wavebankIndex, 5);
    EXPECT_EQ(xsb.sounds[1].waves[0].waveIndex, 99u);
}

TEST(XactParserTest, XgsParsesCategoryAndVariable)
{
    const XgsData xgs = ParseXgs(BuildXgsFixture());

    ASSERT_EQ(xgs.categories.size(), 1u);
    const auto& cat = xgs.categories[0];
    EXPECT_EQ(cat.name, "Default");
    EXPECT_EQ(cat.instanceLimit, 255);
    EXPECT_EQ(cat.fadeInMS, 100);
    EXPECT_EQ(cat.fadeOutMS, 200);
    EXPECT_EQ(cat.parentIndex, 0xFFFF);

    ASSERT_EQ(xgs.variables.size(), 1u);
    const auto& var = xgs.variables[0];
    EXPECT_EQ(var.name, "Volume");
    EXPECT_FLOAT_EQ(var.initialValue, 0.5f);
    EXPECT_FLOAT_EQ(var.minValue, 0.0f);
    EXPECT_FLOAT_EQ(var.maxValue, 1.0f);

    EXPECT_EQ(xgs.categoryNameMap.at("Default"), 0);
    EXPECT_EQ(xgs.variableNameMap.at("Volume"), 0);
}
