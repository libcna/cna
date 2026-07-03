// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "CNA/Internal/Audio/XactTypes.hpp"

#include <cstring>
#include <stdexcept>
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

    // Compact .xwb with an adversarial deviation field larger than the gap to the next entry's
    // offset -- the dataLength computation must throw rather than silently underflow to a huge
    // uint32_t value that could later bypass WaveBank's bounds check (regression fixture for IN-3).
    std::vector<uint8_t> BuildCompactXwbFixtureWithOversizedDeviation()
    {
        constexpr uint32_t alignment         = 4;
        constexpr uint32_t headerSize        = 48;
        constexpr uint32_t bankDataSize      = 96;
        constexpr uint32_t entryCount        = 2;
        constexpr uint32_t entryMetaDataSize = 4;
        constexpr uint32_t entryMetaSegSize  = entryCount * entryMetaDataSize;
        constexpr uint32_t waveDataLength    = 16;

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
        AppendPadded(data, "IN-3", 64); // bank name
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

        // entry 0: offset=0*4=0, deviation=15 -- exceeds entry 1's offset (3*4=12): underflow.
        AppendU32(data, (0u) | (15u << 21));
        // entry 1 (last): offset=3*4=12
        AppendU32(data, (3u));

        for (uint32_t i = 0; i < waveDataLength; ++i)
            data.push_back(static_cast<uint8_t>(i));

        return data;
    }

    // Compact .xwb whose single (and therefore last) entry's offset lies past the end of the
    // wave-data segment -- the "remainder of segment" length computation must throw rather than
    // underflow (regression fixture for IN-3's second underflow site).
    std::vector<uint8_t> BuildCompactXwbFixtureWithOffsetPastSegment()
    {
        constexpr uint32_t alignment         = 4;
        constexpr uint32_t headerSize        = 48;
        constexpr uint32_t bankDataSize      = 96;
        constexpr uint32_t entryCount        = 1;
        constexpr uint32_t entryMetaDataSize = 4;
        constexpr uint32_t entryMetaSegSize  = entryCount * entryMetaDataSize;
        constexpr uint32_t waveDataLength    = 8;

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
        AppendPadded(data, "IN-3b", 64); // bank name
        AppendU32(data, entryMetaDataSize);
        AppendU32(data, 0); // entryNameElementSize
        AppendU32(data, alignment);
        const uint32_t compactFormat =
              (0u) | (0u << 2) | (44100u << 5) | (2u << 23) | (1u << 31);
        AppendU32(data, compactFormat);
        for (int i = 0; i < 8; ++i) data.push_back(0); // buildTime

        // entry 0 (only/last): offset=10*4=40, past the 8-byte wave-data segment: underflow.
        AppendU32(data, (10u));

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

    // Builds one PITCH-family track event using the "ramp" form (settings' RAMP bit set) --
    // the counterpart to BuildPitchEventBytes's "equation" form (IN-6).
    std::vector<uint8_t> BuildPitchRampEventBytes()
    {
        std::vector<uint8_t> e;
        AppendU32(e, 7u);       // evtInfo: type=FACTEVENT_PITCH (7), timestamp=0
        AppendU16(e, 0);        // randomOffset
        AppendU8(e, 0xFF);      // separator
        AppendU8(e, 0x01);      // settings: RAMP bit set
        AppendF32(e, 0.0f);     // initialValue
        AppendF32(e, 0.0f);     // initialSlope
        AppendF32(e, 0.0f);     // slopeDelta
        AppendU16(e, 0);        // duration
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

    // Minimal .xsb with two simple (non-complex) sounds: sound 0 has SOUND_FLAG_HAS_RPC set with
    // a self-inclusive 2-byte rpcDataLength field (unlike the DSP block's leading field, this one
    // genuinely IS a self-inclusive skip length per the parser). Sound 1 has a distinctive wave
    // reference; if sound 0's RPC block is mis-skipped, the cursor desyncs and sound 1's fields
    // come out wrong. Coverage fixture for IN-6.
    std::vector<uint8_t> BuildXsbWithRpcThenSecondSound()
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

        // Sound 0: simple (non-complex), SOUND_FLAG_HAS_RPC (0x02) set.
        AppendU8(data, 0x02);  // flags
        AppendU16(data, 0);    // categoryIndex
        AppendU8(data, 0xFF);  // volume byte
        AppendS16(data, 0);    // pitchCents
        AppendU8(data, 0);     // priority
        AppendU16(data, 0);    // soundLength (skipped)
        AppendU16(data, 0);    // simple wave: waveIdx (unused by this test)
        AppendU8(data, 0);     // simple wave: wbIdx (unused by this test)

        // RPC block: rpcDataLength is self-inclusive (includes its own 2 bytes) -> 6 means
        // 4 bytes of RPC codes follow.
        AppendU16(data, 6);
        AppendU32(data, 0xAAAAAAAAu);

        // Sound 1: simple (non-complex), no flags, distinctive wave reference.
        AppendU8(data, 0x00);   // flags
        AppendU16(data, 0);     // categoryIndex
        AppendU8(data, 0xFF);   // volume byte
        AppendS16(data, 0);     // pitchCents
        AppendU8(data, 0);      // priority
        AppendU16(data, 0);     // soundLength (skipped)
        AppendU16(data, 77);    // waveIdx
        AppendU8(data, 4);      // wbIdx

        return data;
    }

    // Minimal non-compact .xwb with a single, full-size (entryMetaDataSize==24) ADPCM entry --
    // covers both the "standard" (non-narrow) non-compact entry layout and ADPCM's
    // samplesPerBlock/blockAlign derivation from wBlockAlign (IN-6).
    std::vector<uint8_t> BuildNonCompactAdpcmXwbFixture()
    {
        constexpr uint32_t headerSize        = 48;
        constexpr uint32_t bankDataSize      = 96;
        constexpr uint32_t entryCount        = 1;
        constexpr uint32_t entryMetaDataSize = 24;
        constexpr uint32_t entryMetaSegSize  = entryCount * entryMetaDataSize;
        constexpr uint32_t waveDataLength    = 16;

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

        AppendU32(data, 0u); // wbFlags: not compact, no names
        AppendU32(data, entryCount);
        AppendPadded(data, "IN-6-ADPCM", 64); // bank name
        AppendU32(data, entryMetaDataSize);
        AppendU32(data, 0); // entryNameElementSize
        AppendU32(data, 4); // alignment (unused, non-compact)
        AppendU32(data, 0); // compactFormat (unused, non-compact)
        for (int i = 0; i < 8; ++i) data.push_back(0); // buildTime

        // Entry 0: ADPCM, mono, 22050Hz, wBlockAlign=8 -> blockAlign=(8+22)*1=30,
        // samplesPerBlock=(8+16)*2=48.
        constexpr uint32_t wBlockAlign = 8u;
        const uint32_t fmt =
              (2u)                    // fmtTag: ADPCM
            | (0u << 2)               // channels-1 = 0 -> mono
            | (22050u << 5)           // sample rate
            | (wBlockAlign << 23)
            | (0u << 31);             // bps flag (unused for ADPCM)
        AppendU32(data, 0x12345678u); // flagsAndDuration (unused by CNA)
        AppendU32(data, fmt);
        AppendU32(data, 0u);             // playOffset (relative to wave-data segment)
        AppendU32(data, waveDataLength); // playLength
        AppendU32(data, 100u);           // loopStart
        AppendU32(data, 200u);           // loopTotal

        for (uint32_t i = 0; i < waveDataLength; ++i)
            data.push_back(static_cast<uint8_t>(i));

        return data;
    }

    // Minimal .xsb with one simple sound and one complex cue referencing a variation table of
    // the given `type`. For type==3 (INTERACTIVE) the table gets one valid 16-byte entry
    // (soundCode + var_min + var_max + linger); every other type (e.g. 2 == CLIP) gets an
    // entryCount of 1 with no entry bytes at all, since the parser must reject an unknown type
    // before attempting to read any type-specific fields. Regression fixture for IN-4.
    std::vector<uint8_t> BuildXsbWithVariationOfType(uint8_t type)
    {
        constexpr uint32_t headerSize   = 74;
        constexpr uint32_t bankNameSize = 64;
        constexpr uint32_t baseOffset   = headerSize + bankNameSize; // 138
        constexpr uint32_t soundSize    = 12; // simple sound: flags+cat+vol+pitch+prio+len+waveIdx+wbIdx

        const uint32_t soundOffset        = baseOffset;             // 138
        const uint32_t sound0Code         = soundOffset;             // 138
        const uint32_t variationOffset    = soundOffset + soundSize; // 150
        const uint32_t entrySize =
              (type == 0) ? 5u   // WAVE: waveIndex(2)+wavebankIndex(1)+weightMin(1)+weightMax(1)
            : (type == 1) ? 6u   // SOUND: soundCode(4)+weightMin(1)+weightMax(1)
            : (type == 3) ? 16u  // INTERACTIVE: soundCode(4)+var_min(4)+var_max(4)+linger(4)
            : (type == 4) ? 3u   // COMPACT_WAVE: waveIndex(2)+wavebankIndex(1)
            : 0u;                // anything else -- the parser must throw before reading fields
        const uint32_t tableSize          = 4 + 2 + 2 + entrySize;
        const uint32_t cueComplexOffset   = variationOffset + tableSize;
        const uint32_t cueNameIndexOffset = cueComplexOffset + 15;
        const uint32_t cueNameStrOffset   = cueNameIndexOffset + 6;
        const std::string cueName         = "VariType";

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
        AppendU16(data, 1); // soundCount
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

        AppendPadded(data, "VariTypeTestBank", bankNameSize);

        // Sound 0: simple, categoryIndex=0.
        AppendU8(data, 0);   // flags
        AppendU16(data, 0);  // categoryIndex
        AppendU8(data, 0xFF); // volume raw byte (unused by this test)
        AppendU16(data, 0);  // pitchCents
        AppendU8(data, 0);   // priority
        AppendU16(data, 0);  // soundLength (skipped)
        AppendU16(data, 0);  // waveIdx
        AppendU8(data, 0);   // wbIdx

        // Variation table.
        const uint32_t entryCountAndFlags = 1u | (static_cast<uint32_t>(type) << 19); // entryCount=1
        AppendU32(data, entryCountAndFlags);
        AppendU16(data, 0);   // unknown
        AppendU16(data, static_cast<uint16_t>(-1)); // variable

        if (type == 0) // WAVE: waveIndex + wavebankIndex + weightMin + weightMax
        {
            AppendU16(data, 7u);   // waveIndex
            AppendU8(data, 2u);    // wavebankIndex
            AppendU8(data, 0u);    // weightMin
            AppendU8(data, 100u);  // weightMax
        }
        else if (type == 1) // SOUND: soundCode + weightMin + weightMax
        {
            AppendU32(data, sound0Code);
            AppendU8(data, 0u);   // weightMin
            AppendU8(data, 50u);  // weightMax
        }
        else if (type == 3) // INTERACTIVE: soundCode + var_min + var_max + linger
        {
            AppendU32(data, sound0Code);
            AppendF32(data, 0.0f);
            AppendF32(data, 1.0f);
            AppendU32(data, 0);
        }
        else if (type == 4) // COMPACT_WAVE: waveIndex + wavebankIndex (weight hardcoded 0..255)
        {
            AppendU16(data, 9u);  // waveIndex
            AppendU8(data, 3u);   // wavebankIndex
        }
        // else: no entry bytes -- the parser must throw before reading any type-specific fields.

        // Complex cue: not single-sound, sbCode points at the variation table.
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

TEST(XactParserTest, CompactWaveBankThrowsWhenDeviationExceedsGapToNextEntry)
{
    EXPECT_THROW(ParseXwb(BuildCompactXwbFixtureWithOversizedDeviation()), std::runtime_error);
}

TEST(XactParserTest, CompactWaveBankThrowsWhenLastEntryOffsetExceedsWaveDataSegment)
{
    EXPECT_THROW(ParseXwb(BuildCompactXwbFixtureWithOffsetPastSegment()), std::runtime_error);
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

TEST(XactParserTest, VariationTypeInteractiveParsesSixteenByteEntry)
{
    const XsbData xsb = ParseXsb(BuildXsbWithVariationOfType(3));

    ASSERT_EQ(xsb.variations.size(), 1u);
    EXPECT_EQ(xsb.variations[0].type, 3);
    ASSERT_EQ(xsb.variations[0].entries.size(), 1u);
    EXPECT_TRUE(xsb.variations[0].entries[0].isSoundEntry);
    EXPECT_EQ(xsb.variations[0].entries[0].soundIndex, 0u);
}

TEST(XactParserTest, VariationTypeClipThrows)
{
    EXPECT_THROW(ParseXsb(BuildXsbWithVariationOfType(2)), std::runtime_error);
}

TEST(XactParserTest, VariationTypeWaveParsesFiveByteEntry)
{
    const XsbData xsb = ParseXsb(BuildXsbWithVariationOfType(0));

    ASSERT_EQ(xsb.variations.size(), 1u);
    EXPECT_EQ(xsb.variations[0].type, 0);
    ASSERT_EQ(xsb.variations[0].entries.size(), 1u);
    const auto& e = xsb.variations[0].entries[0];
    EXPECT_FALSE(e.isSoundEntry);
    EXPECT_EQ(e.waveIndex, 7u);
    EXPECT_EQ(e.wavebankIndex, 2u);
    EXPECT_EQ(e.weightMin, 0u);
    EXPECT_EQ(e.weightMax, 100u);
}

TEST(XactParserTest, VariationTypeSoundParsesSixByteEntry)
{
    const XsbData xsb = ParseXsb(BuildXsbWithVariationOfType(1));

    ASSERT_EQ(xsb.variations.size(), 1u);
    EXPECT_EQ(xsb.variations[0].type, 1);
    ASSERT_EQ(xsb.variations[0].entries.size(), 1u);
    const auto& e = xsb.variations[0].entries[0];
    EXPECT_TRUE(e.isSoundEntry);
    EXPECT_EQ(e.soundIndex, 0u);
    EXPECT_EQ(e.weightMin, 0u);
    EXPECT_EQ(e.weightMax, 50u);
}

TEST(XactParserTest, VariationTypeCompactWaveParsesThreeByteEntryWithHardcodedWeight)
{
    const XsbData xsb = ParseXsb(BuildXsbWithVariationOfType(4));

    ASSERT_EQ(xsb.variations.size(), 1u);
    EXPECT_EQ(xsb.variations[0].type, 4);
    ASSERT_EQ(xsb.variations[0].entries.size(), 1u);
    const auto& e = xsb.variations[0].entries[0];
    EXPECT_FALSE(e.isSoundEntry);
    EXPECT_EQ(e.waveIndex, 9u);
    EXPECT_EQ(e.wavebankIndex, 3u);
    EXPECT_EQ(e.weightMin, 0u);
    EXPECT_EQ(e.weightMax, 255u);
}

TEST(XactParserTest, RpcBlockIsSkippedCorrectly)
{
    const XsbData xsb = ParseXsb(BuildXsbWithRpcThenSecondSound());

    ASSERT_EQ(xsb.sounds.size(), 2u);
    ASSERT_EQ(xsb.sounds[1].waves.size(), 1u);
    EXPECT_EQ(xsb.sounds[1].waves[0].wavebankIndex, 4);
    EXPECT_EQ(xsb.sounds[1].waves[0].waveIndex, 77u);
}

TEST(XactParserTest, ComplexTrackSkipsRampPitchEventToFindPlayWave)
{
    const XsbData xsb = ParseXsb(
        BuildXsbWithComplexTrack({ BuildPitchRampEventBytes(), BuildPlayWaveEventBytes(55, 6, 2) }));

    ASSERT_EQ(xsb.sounds.size(), 1u);
    ASSERT_EQ(xsb.sounds[0].waves.size(), 1u);
    EXPECT_EQ(xsb.sounds[0].waves[0].wavebankIndex, 6);
    EXPECT_EQ(xsb.sounds[0].waves[0].waveIndex, 55u);
    EXPECT_EQ(xsb.sounds[0].waves[0].loopCount, 2);
}

TEST(XactParserTest, NonCompactAdpcmEntryComputesBlockAlignAndSamplesPerBlock)
{
    const XwbData wb = ParseXwb(BuildNonCompactAdpcmXwbFixture());

    ASSERT_EQ(wb.entries.size(), 1u);
    const auto& e = wb.entries[0];
    EXPECT_EQ(e.format, XwbFormat::ADPCM);
    EXPECT_EQ(e.channels, 1);
    EXPECT_EQ(e.sampleRate, 22050u);
    EXPECT_EQ(e.blockAlign, 30);
    EXPECT_EQ(e.samplesPerBlock, 48);
    EXPECT_EQ(e.dataLength, 16u);
    EXPECT_EQ(e.loopStartSample, 100u);
    EXPECT_EQ(e.loopTotalSamples, 200u);
}

// ===================== Truncated files / bad magic (IN-6) =====================

TEST(XactParserTest, ParseXgsTruncatedFileThrows)
{
    std::vector<uint8_t> tiny(10, 0);
    EXPECT_THROW(ParseXgs(tiny), std::runtime_error);
}

TEST(XactParserTest, ParseXwbTruncatedFileThrows)
{
    std::vector<uint8_t> tiny(10, 0);
    EXPECT_THROW(ParseXwb(tiny), std::runtime_error);
}

TEST(XactParserTest, ParseXsbTruncatedFileThrows)
{
    std::vector<uint8_t> tiny(10, 0);
    EXPECT_THROW(ParseXsb(tiny), std::runtime_error);
}

TEST(XactParserTest, ParseXgsBadMagicThrows)
{
    std::vector<uint8_t> data(0x50, 0); // large enough to pass the size check; magic is all-zero
    EXPECT_THROW(ParseXgs(data), std::runtime_error);
}

TEST(XactParserTest, ParseXwbBadMagicThrows)
{
    std::vector<uint8_t> data(52, 0);
    EXPECT_THROW(ParseXwb(data), std::runtime_error);
}

TEST(XactParserTest, ParseXsbBadMagicThrows)
{
    std::vector<uint8_t> data(0x50, 0);
    EXPECT_THROW(ParseXsb(data), std::runtime_error);
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
