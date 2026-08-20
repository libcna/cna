// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "CNA/Internal/Audio/XactTypes.hpp"

#include <chrono>
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
            | (1u << 2)     // channels: raw field IS the real channel count -> mono (IN-7)
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
            | (1u << 2)     // channels: raw field IS the real channel count -> mono (IN-7)
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

    // A-12 (2026-07-17 deep audit): compact .xwb with a SINGLE entry (so it is simultaneously the
    // first and last entry) carrying a nonzero deviation field. Verified against the real,
    // current, actively-maintained FAudio source (FACT_internal.c's compact-entry parsing loop,
    // `~line 3106-3124`): the last entry's PlayRegion.dwLength is computed as
    // `ENTRYWAVEDATA segment length - offset`, with NO deviation subtraction at all -- unlike the
    // non-last-entry loop just above it, which (due to what reads as a genuine, long-standing
    // FAudio bug -- it subtracts an entry's own just-computed offset from itself, always
    // yielding zero, present unchanged since at least a 2018-12-18 commit in the locally
    // available FAudio checkout) never produces a usable non-last-entry length at all. CNA
    // deliberately does not replicate that non-last-entry bug (see
    // CompactWaveBankComputesLengthsFromConsecutiveOffsets, which proves non-last entries get
    // real, non-zero lengths derived from the gap to the next entry's offset) but DOES correctly
    // match FAudio's last-entry behavior of never subtracting the deviation. This fixture proves
    // that specific behavior with a deviation value that would change the answer if it were
    // (incorrectly) subtracted.
    std::vector<uint8_t> BuildCompactXwbFixtureWithNonzeroLastEntryDeviation()
    {
        constexpr uint32_t alignment         = 4;
        constexpr uint32_t headerSize        = 48;
        constexpr uint32_t bankDataSize      = 96;
        constexpr uint32_t entryCount        = 1;
        constexpr uint32_t entryMetaDataSize = 4;
        constexpr uint32_t entryMetaSegSize  = entryCount * entryMetaDataSize;
        constexpr uint32_t waveDataLength    = 20;

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
        AppendU32(data, 1);
        for (int i = 0; i < 5; ++i)
        {
            AppendU32(data, segOffset[i]);
            AppendU32(data, segLength[i]);
        }

        AppendU32(data, 0x00020000u); // wbFlags: COMPACT only, no names
        AppendU32(data, entryCount);
        AppendPadded(data, "A-12", 64);
        AppendU32(data, entryMetaDataSize);
        AppendU32(data, 0);
        AppendU32(data, alignment);
        const uint32_t compactFormat =
              (0u) | (1u << 2) | (44100u << 5) | (2u << 23) | (1u << 31);
        AppendU32(data, compactFormat);
        for (int i = 0; i < 8; ++i) data.push_back(0);

        // Single (last) entry: offset=2*4=8, deviation=5 -- if (incorrectly) subtracted, length
        // would be (20-8)-5=7 instead of the correct 20-8=12.
        AppendU32(data, (2u) | (5u << 21));

        for (uint32_t i = 0; i < waveDataLength; ++i)
            data.push_back(static_cast<uint8_t>(i));

        return data;
    }

    // AUD-11-003 (2026-07-17 deep audit): compact .xwb with a caller-specified `alignment` field
    // (normally a small constant like 4) -- used to build both a zero-alignment fixture and an
    // alignment value large enough to overflow the `rawOffsetUnits[i] * alignment` multiplication
    // in 32-bit arithmetic.
    std::vector<uint8_t> BuildCompactXwbFixtureWithAlignment(uint32_t alignment, uint32_t rawOffsetUnits)
    {
        constexpr uint32_t headerSize        = 48;
        constexpr uint32_t bankDataSize      = 96;
        constexpr uint32_t entryCount        = 1;
        constexpr uint32_t entryMetaDataSize = 4;
        constexpr uint32_t entryMetaSegSize  = entryCount * entryMetaDataSize;
        constexpr uint32_t waveDataLength    = 20;

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
        AppendU32(data, 1);
        for (int i = 0; i < 5; ++i)
        {
            AppendU32(data, segOffset[i]);
            AppendU32(data, segLength[i]);
        }

        AppendU32(data, 0x00020000u); // wbFlags: COMPACT only, no names
        AppendU32(data, entryCount);
        AppendPadded(data, "AUD-11-003", 64);
        AppendU32(data, entryMetaDataSize);
        AppendU32(data, 0);
        AppendU32(data, alignment);
        const uint32_t compactFormat =
              (0u) | (1u << 2) | (44100u << 5) | (2u << 23) | (1u << 31);
        AppendU32(data, compactFormat);
        for (int i = 0; i < 8; ++i) data.push_back(0);

        AppendU32(data, rawOffsetUnits & 0x1FFFFFu); // single (last) entry, deviation=0

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
              (0u) | (1u << 2) | (44100u << 5) | (2u << 23) | (1u << 31);
        AppendU32(data, compactFormat);
        for (int i = 0; i < 8; ++i) data.push_back(0); // buildTime

        // entry 0 (only/last): offset=10*4=40, past the 8-byte wave-data segment: underflow.
        AppendU32(data, (10u));

        for (uint32_t i = 0; i < waveDataLength; ++i)
            data.push_back(static_cast<uint8_t>(i));

        return data;
    }

    // AUD-11-005: a corrupt/adversarial entryMetaDataSize smaller than 4 (the size of the
    // compact per-entry u32 the parser unconditionally reads) makes `entryMetaDataSize - 4`
    // (both uint32_t) underflow to a huge value passed straight to ctx.skip() -- unlike the
    // non-compact path's carefully graduated conditional reads (which structurally guarantee
    // `ctx.cur - entryPtr` never exceeds entryMetaDataSize), the compact path has no such
    // guard. Must fail cleanly, not with UB pointer arithmetic or a crash.
    std::vector<uint8_t> BuildCompactXwbFixtureWithEntryMetaDataSizeTooSmall()
    {
        constexpr uint32_t alignment         = 4;
        constexpr uint32_t headerSize        = 48;
        constexpr uint32_t bankDataSize      = 96;
        constexpr uint32_t entryCount        = 1;
        constexpr uint32_t entryMetaDataSize = 2; // too small: the compact entry itself is 4 bytes
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
        AppendPadded(data, "AUD-11-005", 64); // bank name
        AppendU32(data, entryMetaDataSize);
        AppendU32(data, 0); // entryNameElementSize
        AppendU32(data, alignment);
        const uint32_t compactFormat =
              (0u) | (1u << 2) | (44100u << 5) | (2u << 23) | (1u << 31);
        AppendU32(data, compactFormat);
        for (int i = 0; i < 8; ++i) data.push_back(0); // buildTime

        // The one compact entry: a plausible, in-range offset/deviation -- the defect is in
        // entryMetaDataSize itself, not this value.
        AppendU32(data, (0u) | (0u << 21));

        for (uint32_t i = 0; i < waveDataLength; ++i)
            data.push_back(static_cast<uint8_t>(i));

        return data;
    }

    // Compact .xwb with the channel field encoded as raw "2" (stereo) -- IN-7 regression
    // fixture, independent of any fixture that assumed the old (buggy) "field is channels
    // minus one" convention.
    std::vector<uint8_t> BuildCompactXwbFixtureStereo()
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
        AppendPadded(data, "IN-7-stereo", 64); // bank name
        AppendU32(data, entryMetaDataSize);
        AppendU32(data, 0); // entryNameElementSize
        AppendU32(data, alignment);
        const uint32_t compactFormat =
              (0u)          // format tag: PCM
            | (2u << 2)     // channels: raw field IS the real channel count -> stereo (IN-7)
            | (44100u << 5) // sample rate
            | (4u << 23)    // wBlockAlign: 4 bytes/frame (2ch * 16-bit)
            | (1u << 31);   // bits-per-sample flag -> 16-bit
        AppendU32(data, compactFormat);
        for (int i = 0; i < 8; ++i) data.push_back(0); // buildTime

        // single (and therefore last) entry: offset=0, length = remaining wave-data segment.
        AppendU32(data, 0u);

        for (uint32_t i = 0; i < waveDataLength; ++i)
            data.push_back(static_cast<uint8_t>(i));

        return data;
    }

    // Compact .xwb with a single ADPCM entry -- IN-10 regression fixture. The compact branch
    // used to never derive samplesPerBlock/blockAlign for ADPCM at all (only the non-compact
    // branch did), leaving them at their raw/default values even for a fully valid compact+ADPCM
    // combination.
    std::vector<uint8_t> BuildCompactXwbFixtureAdpcm()
    {
        constexpr uint32_t alignment         = 4;
        constexpr uint32_t headerSize        = 48;
        constexpr uint32_t bankDataSize      = 96;
        constexpr uint32_t entryCount        = 1;
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
        AppendPadded(data, "IN-10-adpcm", 64); // bank name
        AppendU32(data, entryMetaDataSize);
        AppendU32(data, 0); // entryNameElementSize
        AppendU32(data, alignment);
        constexpr uint32_t wBlockAlign = 8u;
        const uint32_t compactFormat =
              (2u)                // format tag: ADPCM
            | (1u << 2)           // channels: mono
            | (22050u << 5)       // sample rate
            | (wBlockAlign << 23)
            | (0u << 31);         // bps flag (unused for ADPCM)
        AppendU32(data, compactFormat);
        for (int i = 0; i < 8; ++i) data.push_back(0); // buildTime

        // single (and therefore last) entry: offset=0, length = remaining wave-data segment.
        AppendU32(data, 0u);

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

    // Builds a single track event with an unrecognized type (2 -- a genuine gap in FAudio's own
    // FACTEVENT_* enum: FACT_internal.h defines exactly {0,1,3,4,6,7,8,9,16,17,18}, so 2 can never
    // appear in real XACT-tool-built content, but the parser must still handle it safely rather
    // than misreading undefined bytes as a known event's fields).
    std::vector<uint8_t> BuildUnknownTypeEventBytes()
    {
        std::vector<uint8_t> e;
        AppendU32(e, 2u);  // evtInfo: type=2 (unrecognized), timestamp=0
        AppendU16(e, 0);   // randomOffset
        AppendU8(e, 0xFF); // separator
        return e;
    }

    // Minimal .xsb with zero cues/wavebanks and one complex sound whose single track's event
    // list is exactly `events` (each pre-encoded via the Build*EventBytes helpers above).
    // Regression fixture for T-2E. `filterData`/`frequency` default to 0 (no filter, matching
    // every pre-existing caller); P9-XACT-011's retention test passes real values.
    std::vector<uint8_t> BuildXsbWithComplexTrack(const std::vector<std::vector<uint8_t>>& events,
                                                   uint16_t filterData = 0, uint16_t frequency = 0)
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
        AppendU16(data, filterData);
        AppendU16(data, frequency);

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
    // come out wrong. Regression fixture for IN-1 (plans/plan_audio.md Fáze 7).
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
    // a real sound-level RPC code list (count:u8 + one code:u32, P9-XACT-006). Sound 1 has a
    // distinctive wave reference; if sound 0's RPC block is mis-parsed, the cursor desyncs and
    // sound 1's fields come out wrong. Coverage fixture for IN-6/P9-XACT-006.
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

        // RPC block: rpcDataLength (unused by the parser, informational only) + count:u8=1 +
        // one code:u32.
        AppendU16(data, 7);
        AppendU8(data, 1);
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

    // Minimal .xsb with a COMPLEX sound carrying SOUND_FLAG_HAS_RPC (sound 0, one track, one
    // PlayWave event) followed by a second, distinctly-identifiable simple sound. Per FACT
    // (FACT_internal.c's FACTSoundBank_Prepare), the real byte order for a COMPLEX+RPC sound is:
    // trackCount -> RPC block -> per-track metadata -> track events. This parser used to read
    // per-track metadata immediately after trackCount, before the RPC block, misinterpreting RPC
    // bytes as track metadata and leaving the cursor desynced for every sound parsed afterward.
    // Regression fixture for IN-8.
    //
    // Track event data is NOT contiguous with a sound's own header/metadata -- FACT's sequential
    // sound-list cursor never walks through it (each track's events are reached only via the
    // absolute `code` offset, from a *separate* region of the file); sound HEADERS are what must
    // be contiguous from one sound to the next. So sound 1's header follows immediately after
    // sound 0's per-track metadata, and sound 0's track-0 event array is placed after sound 1,
    // referenced purely by absolute offset.
    std::vector<uint8_t> BuildXsbWithComplexRpcThenSecondSound()
    {
        constexpr uint32_t headerSize   = 74;
        constexpr uint32_t bankNameSize = 64;
        constexpr uint32_t soundOffset  = headerSize + bankNameSize; // 138
        // Sound 0 fixed fields (flags+cat+vol+pitch+prio+len = 9) + trackCount(1) = 10 bytes.
        constexpr uint32_t rpcBlockOffset = soundOffset + 10;
        // RPC block: rpcDataLength(2, unused) + count(1) + one code(4) = 7 bytes.
        constexpr uint32_t trackMetaOffset = rpcBlockOffset + 7;
        // Per-track metadata (1 track): vol(1) + code(4) + filterData(2) + frequency(2) = 9 bytes.
        constexpr uint32_t sound1Offset = trackMetaOffset + 9;
        // Sound 1 (simple): flags+cat+vol+pitch+prio+len (9) + waveIdx(2) + wbIdx(1) = 12 bytes.
        constexpr uint32_t trackEventsOffset = sound1Offset + 12;

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

        // Sound 0: COMPLEX | HAS_RPC.
        AppendU8(data, 0x01 | 0x02); // flags
        AppendU16(data, 0);          // categoryIndex
        AppendU8(data, 0xFF);        // volume byte
        AppendS16(data, 0);          // pitchCents
        AppendU8(data, 0);           // priority
        AppendU16(data, 0);          // soundLength (skipped)
        AppendU8(data, 1);           // trackCount

        // RPC block: rpcDataLength(unused) + count:u8=1 + one code:u32.
        AppendU16(data, 7);
        AppendU8(data, 1);
        AppendU32(data, 0xAAAAAAAAu);

        // Per-track metadata (1 track): volume byte, code (absolute offset to event array,
        // placed after sound 1 below), filterData, frequency.
        AppendU8(data, 0xFF);
        AppendU32(data, trackEventsOffset);
        AppendU16(data, 0); // filterData
        AppendU16(data, 0); // frequency

        // Sound 1: simple (non-complex), no flags, distinctive wave reference. Immediately
        // follows sound 0's per-track metadata -- sound headers are contiguous.
        AppendU8(data, 0x00);   // flags
        AppendU16(data, 0);     // categoryIndex
        AppendU8(data, 0xFF);   // volume byte
        AppendS16(data, 0);     // pitchCents
        AppendU8(data, 0);      // priority
        AppendU16(data, 0);     // soundLength (skipped)
        AppendU16(data, 99);    // waveIdx
        AppendU8(data, 7);      // wbIdx

        // Sound 0's track-0 event array, out-of-line: eventCount followed by one PlayWave event.
        const auto event = BuildPlayWaveEventBytes(42, 3, 0);
        AppendU8(data, 1);
        data.insert(data.end(), event.begin(), event.end());

        return data;
    }

    // Same as BuildXsbWithComplexRpcThenSecondSound but with SOUND_FLAG_HAS_DSP instead of
    // SOUND_FLAG_HAS_RPC -- covers the DSP-block variant of the same IN-8 ordering bug
    // separately, since the DSP block's byte layout (and skip logic) differs from RPC's.
    std::vector<uint8_t> BuildXsbWithComplexDspThenSecondSound()
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

        // Sound 0: COMPLEX | HAS_DSP.
        AppendU8(data, 0x01 | 0x10); // flags
        AppendU16(data, 0);          // categoryIndex
        AppendU8(data, 0xFF);        // volume byte
        AppendS16(data, 0);          // pitchCents
        AppendU8(data, 0);           // priority
        AppendU16(data, 0);          // soundLength (skipped)
        AppendU8(data, 1);           // trackCount

        // DSP block: presetsLength(2, unused) + dspCodeCount(1) + codes(4 each) = 7 bytes.
        AppendU16(data, 5);       // DSP presets length -- unused
        AppendU8(data, 1);        // dspCodeCount
        AppendU32(data, 0xDEADBEEFu);

        // trackEventsOffset: soundOffset(138) + 9(fixed)+1(trackCount) + 7(DSP block)
        //                    + 9(per-track meta) + 12(sound 1 header) = 176.
        constexpr uint32_t trackEventsOffset = soundOffset + 9 + 1 + 7 + 9 + 12;

        // Per-track metadata (1 track): code points past sound 1, to the out-of-line event array.
        AppendU8(data, 0xFF);
        AppendU32(data, trackEventsOffset);
        AppendU16(data, 0); // filterData
        AppendU16(data, 0); // frequency

        // Sound 1: simple (non-complex), no flags, distinctive wave reference. Immediately
        // follows sound 0's per-track metadata -- sound headers are contiguous.
        AppendU8(data, 0x00);   // flags
        AppendU16(data, 0);     // categoryIndex
        AppendU8(data, 0xFF);   // volume byte
        AppendS16(data, 0);     // pitchCents
        AppendU8(data, 0);      // priority
        AppendU16(data, 0);     // soundLength (skipped)
        AppendU16(data, 88);    // waveIdx
        AppendU8(data, 9);      // wbIdx

        // Sound 0's track-0 event array, out-of-line: eventCount followed by one PlayWave event.
        const auto event = BuildPlayWaveEventBytes(55, 8, 0);
        AppendU8(data, 1);
        data.insert(data.end(), event.begin(), event.end());

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
            | (1u << 2)               // channels: raw field IS the real channel count -> mono (IN-7)
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

    // Minimal non-compact .xwb with a single PCM entry whose channel field is raw "2" (stereo) --
    // the non-compact counterpart to BuildCompactXwbFixtureStereo (IN-7).
    std::vector<uint8_t> BuildNonCompactXwbFixtureStereo()
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
        AppendPadded(data, "IN-7-nc-stereo", 64); // bank name
        AppendU32(data, entryMetaDataSize);
        AppendU32(data, 0); // entryNameElementSize
        AppendU32(data, 4); // alignment (unused, non-compact)
        AppendU32(data, 0); // compactFormat (unused, non-compact)
        for (int i = 0; i < 8; ++i) data.push_back(0); // buildTime

        const uint32_t fmt =
              (0u)            // fmtTag: PCM
            | (2u << 2)       // channels: raw field IS the real channel count -> stereo (IN-7)
            | (44100u << 5)   // sample rate
            | (4u << 23)      // wBlockAlign: 4 bytes/frame (2ch * 16-bit)
            | (1u << 31);     // 16-bit
        AppendU32(data, 0u);  // flagsAndDuration (unused by CNA)
        AppendU32(data, fmt);
        AppendU32(data, 0u);             // playOffset
        AppendU32(data, waveDataLength); // playLength
        AppendU32(data, 0u);             // loopStart
        AppendU32(data, 0u);             // loopTotal

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

// A-12 (2026-07-17 deep audit): confirmed NOT a defect after checking real FAudio source
// (FACT_internal.c) directly -- the last compact entry's length must NOT subtract its own
// deviation field, matching FAudio's own (verified) behavior exactly. See
// BuildCompactXwbFixtureWithNonzeroLastEntryDeviation's own comment for the full citation.
TEST(XactParserTest, CompactWaveBankLastEntryLengthIgnoresItsOwnDeviation)
{
    const XwbData wb = ParseXwb(BuildCompactXwbFixtureWithNonzeroLastEntryDeviation());
    ASSERT_EQ(wb.entries.size(), 1u);
    EXPECT_EQ(wb.entries[0].dataOffset, 148u + 8u); // segOffset[4] (48+96+4) + offset(8)
    EXPECT_EQ(wb.entries[0].dataLength, 12u)        // 20 - 8, deviation NOT subtracted
        << "last entry must not subtract its own deviation (matches real FAudio's "
           "FACT_internal.c compact-entry parsing exactly)";
}

TEST(XactParserTest, CompactWaveBankThrowsWhenDeviationExceedsGapToNextEntry)
{
    EXPECT_THROW(ParseXwb(BuildCompactXwbFixtureWithOversizedDeviation()), std::runtime_error);
}

TEST(XactParserTest, CompactWaveBankThrowsWhenLastEntryOffsetExceedsWaveDataSegment)
{
    EXPECT_THROW(ParseXwb(BuildCompactXwbFixtureWithOffsetPastSegment()), std::runtime_error);
}

// AUD-11-003 (2026-07-17 deep audit): a zero alignment would silently collapse every compact
// entry's offset to 0 (every entry claiming the same start) instead of failing loudly.
TEST(XactParserTest, CompactWaveBankThrowsOnZeroAlignment)
{
    EXPECT_THROW(ParseXwb(BuildCompactXwbFixtureWithAlignment(0, 1)), std::runtime_error);
}

// AUD-11-003: rawOffsetUnits (up to 21 bits) times an unconstrained alignment can overflow
// 32-bit arithmetic and silently wrap to a wrong-but-plausible offset instead of failing loudly.
// Chosen so the WRAPPED 32-bit product is exactly 0 (0x100000 * 0x1000 == 2^32, wraps to 0) --
// a small, entirely plausible-looking offset that would NOT trip the pre-existing "offset exceeds
// wave-data segment" check on its own, unlike a wrap that happens to still land on a huge value.
TEST(XactParserTest, CompactWaveBankThrowsWhenOffsetMultiplicationOverflows32Bits)
{
    EXPECT_THROW(
        ParseXwb(BuildCompactXwbFixtureWithAlignment(0x1000u, 0x100000u)),
        std::runtime_error);
}

// AUD-11-005: entryMetaDataSize < 4 must fail cleanly (a specific, diagnostic exception), not
// via UB pointer arithmetic inside ctx.skip() reached only by luck of the bounds check catching
// the resulting huge value after the fact.
TEST(XactParserTest, CompactWaveBankThrowsWhenEntryMetaDataSizeTooSmall)
{
    try
    {
        ParseXwb(BuildCompactXwbFixtureWithEntryMetaDataSizeTooSmall());
        FAIL() << "expected std::runtime_error";
    }
    catch (const std::runtime_error& ex)
    {
        // Checks the specific, diagnostic message naming the real problem -- not just any
        // exception (the pre-fix code already threw a generic "skip past end" from deep inside
        // Ctx::skip(), which would make a plain EXPECT_THROW pass even without this fix).
        const std::string what = ex.what();
        EXPECT_NE(what.find("entryMetaDataSize"), std::string::npos) << what;
    }
}

TEST(XactParserTest, CompactWaveBankChannelFieldIsRawChannelCountNotMinusOne)
{
    const XwbData wb = ParseXwb(BuildCompactXwbFixtureStereo());
    ASSERT_EQ(wb.entries.size(), 1u);
    EXPECT_EQ(wb.entries[0].channels, 2);
}

// AUD-11-021: every other fixture in this file uses version=1 (<=43, no headerVersion field) --
// the newer header format (version>43, WITH an extra 4-byte headerVersion field between version
// and the segment table) had zero test coverage anywhere despite ReadXwbSegmentTable explicitly
// branching on it. Confirms the segment table (and everything after it) is still read from the
// correct offset when that extra field is present -- a real desync risk if the branch were ever
// wrong, since every segOffset in the file is authored relative to this header's own true size.
std::vector<uint8_t> BuildCompactXwbFixtureWithNewerHeaderVersion()
{
    constexpr uint32_t headerSize        = 52; // magic+version+headerVersion+5*{offset,length}
    constexpr uint32_t bankDataSize      = 96;
    constexpr uint32_t entryCount        = 1;
    constexpr uint32_t entryMetaDataSize = 4;
    constexpr uint32_t entryMetaSegSize  = entryCount * entryMetaDataSize;
    constexpr uint32_t waveDataLength    = 20;

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
    AppendU32(data, 44); // version: >43, exercises the headerVersion branch (still <=46, no
                          // "may not be fully supported" warning)
    AppendU32(data, 1);  // headerVersion -- only present because version>43
    for (int i = 0; i < 5; ++i) { AppendU32(data, segOffset[i]); AppendU32(data, segLength[i]); }

    AppendU32(data, 0x00020000u); // wbFlags: COMPACT only, no names
    AppendU32(data, entryCount);
    AppendPadded(data, "AUD-11-021", 64);
    AppendU32(data, entryMetaDataSize);
    AppendU32(data, 0);
    AppendU32(data, 4);
    const uint32_t compactFormat = (0u) | (1u << 2) | (44100u << 5) | (2u << 23) | (1u << 31);
    AppendU32(data, compactFormat);
    for (int i = 0; i < 8; ++i) data.push_back(0);

    AppendU32(data, 0u); // entry 0: offset=0, deviation=0

    for (uint32_t i = 0; i < waveDataLength; ++i)
        data.push_back(static_cast<uint8_t>(i));

    return data;
}

TEST(XactParserTest, NewerHeaderVersionWithExtraFieldParsesSegmentsAtCorrectOffset)
{
    const XwbData wb = ParseXwb(BuildCompactXwbFixtureWithNewerHeaderVersion());
    ASSERT_EQ(wb.entries.size(), 1u);
    EXPECT_EQ(wb.bankName, "AUD-11-021");
    EXPECT_EQ(wb.entries[0].channels, 1);
    EXPECT_EQ(wb.entries[0].sampleRate, 44100u);
    EXPECT_EQ(wb.entries[0].dataLength, 20u);
}

TEST(XactParserTest, CompactAdpcmEntryComputesBlockAlignAndSamplesPerBlock)
{
    const XwbData wb = ParseXwb(BuildCompactXwbFixtureAdpcm());
    ASSERT_EQ(wb.entries.size(), 1u);
    const auto& e = wb.entries[0];
    EXPECT_EQ(e.format, XwbFormat::ADPCM);
    EXPECT_EQ(e.channels, 1);
    // wSamplesPerBlock = (wBlockAlign + 16) * 2 = (8+16)*2 = 48
    EXPECT_EQ(e.samplesPerBlock, 48);
    // nBlockAlign = (wBlockAlign + 22) * channels = (8+22)*1 = 30
    EXPECT_EQ(e.blockAlign, 30);
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

// P10-XACT-010: CNA's FACTEVENT_* constants match FAudio's real enum (FACT_internal.h) exactly --
// there is no XACT event type CNA fails to recognize among the ones that actually exist. The one
// remaining "unsupported" path is a genuinely unrecognized/malformed type (never emitted by real
// content); its already-documented behavior (stop scanning rather than misinterpret the remaining
// bytes as event headers) had no direct test proving it until now.
TEST(XactParserTest, ComplexTrackStopsScanningAtUnrecognizedEventType)
{
    const XsbData xsb = ParseXsb(BuildXsbWithComplexTrack(
        { BuildUnknownTypeEventBytes(), BuildPlayWaveEventBytes(42, 7, 3) }));

    ASSERT_EQ(xsb.sounds.size(), 1u);
    ASSERT_EQ(xsb.sounds[0].waves.size(), 1u);
    // The scan gave up at the unrecognized event and never reached the PlayWave event after it.
    EXPECT_EQ(xsb.sounds[0].waves[0].wavebankIndex, 0xFF);
    EXPECT_EQ(xsb.sounds[0].waves[0].waveIndex, 0xFFFFu);
}

// P9-XACT-010/011: a complex track's filterData/frequency used to be read-and-discarded. This
// covers the has-filter bit (bit0), FAudio's own filter-type bit-decode ((filterData>>1)&0x02,
// which structurally only ever yields 0/low-pass or 2/high-pass -- see plans/plan_audio.md's
// P9-XACT-010 note), the qfactor byte (upper byte of filterData), and the separate frequency u16.
// filterData = 0x0605: qfactor=0x06, bit0=1 (has filter), bits (>>1)&0x02 == 2 (high-pass).
TEST(XactParserTest, ComplexTrackFilterDataIsRetained)
{
    const XsbData xsb = ParseXsb(
        BuildXsbWithComplexTrack({ BuildPlayWaveEventBytes(1, 0, 0) }, 0x0605, 8000));

    ASSERT_EQ(xsb.sounds.size(), 1u);
    ASSERT_EQ(xsb.sounds[0].waves.size(), 1u);
    const auto& wave = xsb.sounds[0].waves[0];
    EXPECT_EQ(wave.filterType, 2);
    EXPECT_EQ(wave.filterFrequencyHz, 8000u);
    EXPECT_EQ(wave.filterQFactorRaw, 6);
}

// filterData's bit0 clear means "no filter" regardless of whatever happens to be in the qfactor/
// type bits or the frequency field -- filterType must come out as the 0xFF sentinel.
TEST(XactParserTest, ComplexTrackWithFilterBitClearHasNoFilterSentinel)
{
    const XsbData xsb = ParseXsb(
        BuildXsbWithComplexTrack({ BuildPlayWaveEventBytes(1, 0, 0) }, 0x0604, 8000));

    ASSERT_EQ(xsb.sounds.size(), 1u);
    ASSERT_EQ(xsb.sounds[0].waves.size(), 1u);
    EXPECT_EQ(xsb.sounds[0].waves[0].filterType, 0xFF);
}

// Low-pass case: bit0=1, bits (>>1)&0x02 == 0 -- filterData = 0x0301 (qfactor=3, bit0 set, bit2
// clear).
TEST(XactParserTest, ComplexTrackFilterDataDecodesLowPassType)
{
    const XsbData xsb = ParseXsb(
        BuildXsbWithComplexTrack({ BuildPlayWaveEventBytes(1, 0, 0) }, 0x0301, 4000));

    ASSERT_EQ(xsb.sounds.size(), 1u);
    ASSERT_EQ(xsb.sounds[0].waves.size(), 1u);
    const auto& wave = xsb.sounds[0].waves[0];
    EXPECT_EQ(wave.filterType, 0);
    EXPECT_EQ(wave.filterFrequencyHz, 4000u);
    EXPECT_EQ(wave.filterQFactorRaw, 3);
}

TEST(XactParserTest, DspBlockIsSkippedByCodeCountNotByLengthField)
{
    const XsbData xsb = ParseXsb(BuildXsbWithDspThenSecondSound());

    ASSERT_EQ(xsb.sounds.size(), 2u);
    ASSERT_EQ(xsb.sounds[1].waves.size(), 1u);
    EXPECT_EQ(xsb.sounds[1].waves[0].wavebankIndex, 5);
    EXPECT_EQ(xsb.sounds[1].waves[0].waveIndex, 99u);
}

// P9-XACT-014: a simple cue's sound code that doesn't match any parsed sound (corrupt/malformed
// data -- a real XACT-tool-built file's codes always resolve, since they're generated from the
// very same sound table) used to silently fall back to soundIndex 0, aliasing onto whichever
// sound happens to be first in the bank instead of resolving to nothing. `soundIndex` must come
// out of range (>= sounds.size()) so Cue::Play()'s existing bounds check treats it as
// unresolvable, matching the already-established "no sound found -> plays silently" behavior
// (BuildXsbFixtureBytes's own comment) instead of substituting the wrong sound.
TEST(XactParserTest, SimpleCueWithUnresolvableSoundCodeDoesNotAliasToSoundZero)
{
    constexpr uint32_t headerSize   = 74;
    constexpr uint32_t bankNameSize = 64;
    constexpr uint32_t soundOffset  = headerSize + bankNameSize; // 138
    constexpr uint32_t soundSize    = 12; // simple (non-complex) sound, no RPC/DSP block
    const uint32_t cueSimpleOffset  = soundOffset + 2 * soundSize;
    // Deliberately bogus: past both sound entries, matches no real soundCodes[i] value.
    const uint32_t bogusSoundCode   = soundOffset + 2 * soundSize + 1000;

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
    AppendU16(data, 2); // soundCount
    AppendU16(data, 0); // cueNameLength
    AppendU16(data, 0); // unknown

    AppendS32(data, static_cast<int32_t>(cueSimpleOffset));
    AppendS32(data, -1); // cueComplexOffset
    AppendS32(data, -1); // cueNameOffset
    AppendS32(data, 0);  // unknown
    AppendS32(data, -1); // variationOffset
    AppendS32(data, 0);  // transitionOffset
    AppendS32(data, -1); // wavebankNameOffset
    AppendS32(data, 0);  // cueHashOffset
    AppendS32(data, -1); // cueNameIndexOffset (unused, totalCues would need it, but we read the
                         // cue struct directly, not by name)
    AppendS32(data, static_cast<int32_t>(soundOffset));

    AppendPadded(data, "TestSoundBank", bankNameSize);

    // Sound 0: distinctive wave reference (so a test could tell if a cue wrongly resolved here).
    AppendU8(data, 0x00);  // flags
    AppendU16(data, 0);    // categoryIndex
    AppendU8(data, 0xFF);  // volume byte
    AppendS16(data, 0);    // pitchCents
    AppendU8(data, 0);     // priority
    AppendU16(data, 0);    // soundLength (skipped)
    AppendU16(data, 77);   // waveIdx
    AppendU8(data, 3);     // wbIdx

    // Sound 1: another distinctive wave reference.
    AppendU8(data, 0x00);
    AppendU16(data, 0);
    AppendU8(data, 0xFF);
    AppendS16(data, 0);
    AppendU8(data, 0);
    AppendU16(data, 0);
    AppendU16(data, 88);
    AppendU8(data, 4);

    // Simple cue: flags + bogus sound code.
    AppendU8(data, 0);
    AppendU32(data, bogusSoundCode);

    const XsbData xsb = ParseXsb(data);

    ASSERT_EQ(xsb.sounds.size(), 2u);
    ASSERT_EQ(xsb.cues.size(), 1u);
    EXPECT_TRUE(xsb.cues[0].isSingleSound);
    EXPECT_GE(xsb.cues[0].soundIndex, xsb.sounds.size())
        << "unresolvable sound code must not alias onto sound 0 (or any real sound)";
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

// P9-XACT-002: varMin/varMax used to be read and discarded; Cue::Play() now needs them to
// evaluate interactive-variation selection (P9-XACT-003), so the parser must retain them.
TEST(XactParserTest, VariationTypeInteractiveRetainsVarMinAndVarMax)
{
    const XsbData xsb = ParseXsb(BuildXsbWithVariationOfType(3));

    ASSERT_EQ(xsb.variations.size(), 1u);
    ASSERT_EQ(xsb.variations[0].entries.size(), 1u);
    EXPECT_FLOAT_EQ(xsb.variations[0].entries[0].varMin, 0.0f);
    EXPECT_FLOAT_EQ(xsb.variations[0].entries[0].varMax, 1.0f);
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

// P9-XACT-006: the sound-level RPC code list used to be read only far enough to skip past
// (rpcDataLength-driven), discarding the codes entirely; it's now retained on XsbSound::rpcCodes.
TEST(XactParserTest, SoundLevelRpcCodeIsRetained)
{
    const XsbData xsb = ParseXsb(BuildXsbWithRpcThenSecondSound());

    ASSERT_EQ(xsb.sounds.size(), 2u);
    ASSERT_EQ(xsb.sounds[0].rpcCodes.size(), 1u);
    EXPECT_EQ(xsb.sounds[0].rpcCodes[0], 0xAAAAAAAAu);
    EXPECT_TRUE(xsb.sounds[1].rpcCodes.empty());
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

TEST(XactParserTest, ComplexSoundWithRpcParsesTrackAndDoesNotDesyncSecondSound)
{
    const XsbData xsb = ParseXsb(BuildXsbWithComplexRpcThenSecondSound());

    ASSERT_EQ(xsb.sounds.size(), 2u);

    ASSERT_EQ(xsb.sounds[0].waves.size(), 1u);
    EXPECT_EQ(xsb.sounds[0].waves[0].wavebankIndex, 3);
    EXPECT_EQ(xsb.sounds[0].waves[0].waveIndex, 42u);
    ASSERT_EQ(xsb.sounds[0].rpcCodes.size(), 1u);
    EXPECT_EQ(xsb.sounds[0].rpcCodes[0], 0xAAAAAAAAu);

    ASSERT_EQ(xsb.sounds[1].waves.size(), 1u);
    EXPECT_EQ(xsb.sounds[1].waves[0].wavebankIndex, 7);
    EXPECT_EQ(xsb.sounds[1].waves[0].waveIndex, 99u);
}

TEST(XactParserTest, ComplexSoundWithDspParsesTrackAndDoesNotDesyncSecondSound)
{
    const XsbData xsb = ParseXsb(BuildXsbWithComplexDspThenSecondSound());

    ASSERT_EQ(xsb.sounds.size(), 2u);

    ASSERT_EQ(xsb.sounds[0].waves.size(), 1u);
    EXPECT_EQ(xsb.sounds[0].waves[0].wavebankIndex, 8);
    EXPECT_EQ(xsb.sounds[0].waves[0].waveIndex, 55u);

    ASSERT_EQ(xsb.sounds[1].waves.size(), 1u);
    EXPECT_EQ(xsb.sounds[1].waves[0].wavebankIndex, 9);
    EXPECT_EQ(xsb.sounds[1].waves[0].waveIndex, 88u);
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

TEST(XactParserTest, NonCompactWaveBankChannelFieldIsRawChannelCountNotMinusOne)
{
    const XwbData wb = ParseXwb(BuildNonCompactXwbFixtureStereo());
    ASSERT_EQ(wb.entries.size(), 1u);
    EXPECT_EQ(wb.entries[0].channels, 2);
}

namespace
{
    // AUD-11-012: distinctive, mutually-non-collidable values for every FACTWaveBankMiniWaveFormat
    // bitfield (real layout confirmed against FAudio's own FACT.h: wFormatTag:2, nChannels:3,
    // nSamplesPerSec:18, wBlockAlign:8, wBitsPerSample:1, packed LSB-first) -- unlike the
    // channels-only fixture above, a bit-shift/mask bug that swapped two fields or was off by one
    // bit would still be caught here, since no two chosen values could plausibly be confused for
    // each other. Uses ADPCM (fmtTag=2) specifically so the raw wBlockAlign field's own extraction
    // is verifiable through the derived blockAlign/samplesPerBlock formulas (for PCM, wBlockAlign
    // is read but overridden by a channels-only formula, so it wouldn't exercise this field at all).
    std::vector<uint8_t> BuildNonCompactXwbFixtureDistinctiveAdpcmFields()
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
        AppendU32(data, 1);
        for (int i = 0; i < 5; ++i)
        {
            AppendU32(data, segOffset[i]);
            AppendU32(data, segLength[i]);
        }

        AppendU32(data, 0u);
        AppendU32(data, entryCount);
        AppendPadded(data, "AUD-11-012-nc-distinctive", 64);
        AppendU32(data, entryMetaDataSize);
        AppendU32(data, 0);
        AppendU32(data, 4);
        AppendU32(data, 0);
        for (int i = 0; i < 8; ++i) data.push_back(0);

        const uint32_t fmt =
              (2u)              // fmtTag: ADPCM
            | (5u << 2)         // channels: 5 (distinctive, not a real speaker layout)
            | (12345u << 5)     // sample rate: distinctive, well within the 18-bit range
            | (77u << 23)       // wBlockAlign raw field: distinctive
            | (1u << 31);       // 16-bit
        AppendU32(data, 0u);
        AppendU32(data, fmt);
        AppendU32(data, 0u);
        AppendU32(data, waveDataLength);
        AppendU32(data, 0u);
        AppendU32(data, 0u);

        for (uint32_t i = 0; i < waveDataLength; ++i)
            data.push_back(static_cast<uint8_t>(i));

        return data;
    }
}

TEST(XactParserTest, NonCompactWaveBankMiniWaveFormatBitExtractionMatchesEveryFieldExactly)
{
    const XwbData wb = ParseXwb(BuildNonCompactXwbFixtureDistinctiveAdpcmFields());
    ASSERT_EQ(wb.entries.size(), 1u);
    const auto& e = wb.entries[0];

    EXPECT_EQ(e.channels, 5);
    EXPECT_EQ(e.sampleRate, 12345u);
    EXPECT_EQ(e.bitsPerSample, 16);
    // ADPCM-derived formulas (FAudio's own): wSamplesPerBlock = (wBlockAlign_raw + 16) * 2,
    // nBlockAlign = (wBlockAlign_raw + 22) * channels -- with wBlockAlign_raw=77, channels=5:
    EXPECT_EQ(e.samplesPerBlock, (77 + 16) * 2);
    EXPECT_EQ(e.blockAlign, (77 + 22) * 5);
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

// AUD-11-017/018: found via ASan, not just inspection -- a genuine heap-buffer-overflow, distinct
// from the generic "too small to even have a header" truncation above. `wbFlags`/`entryCount`
// (8 bytes total) are read via bounds-checked `ctx.u32()`, but the 64-byte bankName field right
// after them used a raw `strnlen(p, 64)` with no check that 64 real bytes actually remained --
// for a file truncated to end exactly there, this read past the buffer's real heap allocation
// before the (bounds-checked) `ctx.skip(64)` immediately after it ever got a chance to catch the
// truncation. A standalone repro against this exact fixture shape reproduced a real ASan
// heap-buffer-overflow report before the fix (capping the strnlen scan to the real remaining
// bytes first, mirroring cstr()'s own established AUDIO-PARSER-001 pattern); this test only
// checks the resulting exception, since ASan itself (not a plain gtest assertion) is what proves
// the absence of the OOB read -- see this session's ASan-verified investigation notes in
// plans/plan_audio.md's AUD-11-017 entry.
TEST(XactParserTest, ParseXwbTruncatedExactlyAtBankNameFieldThrowsNotOob)
{
    constexpr uint32_t headerSize = 48;
    std::vector<uint8_t> data;
    auto w32 = [&data](uint32_t v) {
        data.push_back(static_cast<uint8_t>(v)); data.push_back(static_cast<uint8_t>(v >> 8));
        data.push_back(static_cast<uint8_t>(v >> 16)); data.push_back(static_cast<uint8_t>(v >> 24));
    };

    const char magic[4] = { 'W', 'B', 'N', 'D' };
    data.insert(data.end(), magic, magic + 4);
    w32(1); // version
    for (int i = 0; i < 5; ++i)
    {
        w32(headerSize); // segOffset[i]: all point right after the header
        w32(0);          // segLength[i]
    }
    w32(0u); // wbFlags
    w32(1u); // entryCount
    // File ends here -- exactly 0 bytes remain for the 64-byte bankName field that would follow.

    EXPECT_THROW(ParseXwb(data), std::runtime_error);
}

// AUD-11-026 (found via fuzzing prep): entryCount is a full uint32_t (unlike every other count
// field this parser reads, all naturally bounded by an 8/16-bit field width) fed straight into
// `result.entries.resize(entryCount)` with no validation -- an implausibly large value (here,
// near UINT32_MAX against a file that's really only ~72 bytes) must be rejected with a clean,
// specific diagnostic instead of attempting a multi-hundred-gigabyte allocation. A plain
// EXPECT_THROW wouldn't distinguish "rejected cleanly and fast" from "eventually got an unrelated
// std::bad_alloc/std::length_error after actually starting a huge allocation" (both throw *some*
// std::runtime_error-derived-or-not exception), which is exactly the class of gap
// XnbContainerFuzzTests.cpp treats as a real failure, not an acceptable outcome -- so this checks
// both the specific exception type and that it returns fast, not after a slow/huge allocation
// attempt.
TEST(XactParserTest, ParseXwbRejectsImplausiblyLargeEntryCountWithoutAllocationAttempt)
{
    // A COMPLETE, well-formed BANKDATA segment (144 real bytes total) is required so the parse
    // genuinely reaches `result.entries.resize(entryCount)` -- a truncated file would already be
    // rejected earlier (by AUD-11-018's bankName-field truncation check) for an unrelated reason,
    // which would not actually exercise this guard at all.
    constexpr uint32_t headerSize   = 48;
    constexpr uint32_t bankDataSize = 96;
    std::vector<uint8_t> data;
    auto w32 = [&data](uint32_t v) {
        data.push_back(static_cast<uint8_t>(v)); data.push_back(static_cast<uint8_t>(v >> 8));
        data.push_back(static_cast<uint8_t>(v >> 16)); data.push_back(static_cast<uint8_t>(v >> 24));
    };

    const char magic[4] = { 'W', 'B', 'N', 'D' };
    data.insert(data.end(), magic, magic + 4);
    w32(1); // version
    const uint32_t segOffset1 = headerSize + bankDataSize; // 144: right after BANKDATA
    for (int i = 0; i < 5; ++i)
    {
        w32(i == 0 ? headerSize : segOffset1);
        w32(0);
    }

    w32(0u);           // wbFlags: not compact, no names
    // 5,000,000 entries against a real file that's really only 144 bytes total -- clearly
    // implausible (each entry needs at least 1 real byte to exist), but a moderate enough
    // magnitude (~a few hundred MB, not hundreds of GB) that even an *unguarded* resize would
    // complete quickly and safely if this test ever needs to run against pre-fix code, rather
    // than risking a genuinely dangerous multi-GB allocation attempt on a shared machine.
    constexpr uint32_t kImplausibleEntryCount = 5000000u;
    w32(kImplausibleEntryCount);
    for (int i = 0; i < 64; ++i) data.push_back(0); // bankName
    w32(24u); // entryMetaDataSize
    w32(0u);  // entryNameElementSize
    w32(4u);  // alignment
    w32(0u);  // compactFormat
    for (int i = 0; i < 8; ++i) data.push_back(0); // buildTime
    // File ends here -- segOffset[1] (144) is exactly at the file's real end, a valid seek
    // target, but there is no real per-entry data behind it at all.

    ASSERT_EQ(data.size(), headerSize + bankDataSize);

    const auto start = std::chrono::steady_clock::now();
    try
    {
        ParseXwb(data);
        FAIL() << "expected std::runtime_error";
    }
    catch (const std::runtime_error& ex)
    {
        EXPECT_NE(std::string(ex.what()).find("entryCount"), std::string::npos) << ex.what();
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 500)
        << "rejection must be fast -- a slow return here would mean an allocation was actually attempted";
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

// P10-XACT-004: a distinct corruption class from "too small to even have a header" (10 bytes)
// and "bad magic" above -- a real, valid magic and a size that clears the coarse minimum-size
// check, but truncated partway through a real record. Every field read in XactParser.cpp goes
// through Ctx::u8()/u16()/u32()/f32()/skip()/seek(), each of which bounds-checks against the
// buffer's actual end (not the declared header/segment sizes), so this must throw
// std::runtime_error rather than read out of bounds -- exercised here rather than just assumed.
TEST(XactParserTest, ParseXgsTruncatedMidRecordThrows)
{
    std::vector<uint8_t> full = BuildXgsFixture();
    ASSERT_GT(full.size(), 0x50u); // the real fixture must exceed the coarse minimum-size check
    std::vector<uint8_t> truncated(full.begin(), full.begin() + 0x50);
    EXPECT_THROW(ParseXgs(truncated), std::runtime_error);
}

TEST(XactParserTest, ParseXwbTruncatedMidRecordThrows)
{
    std::vector<uint8_t> full = BuildCompactXwbFixture();
    ASSERT_GT(full.size(), 52u);
    std::vector<uint8_t> truncated(full.begin(), full.begin() + 52);
    EXPECT_THROW(ParseXwb(truncated), std::runtime_error);
}

// AUDIO-PARSER-001 (external audit, 2026-07-16): a string with no null terminator anywhere before
// the buffer's end used to let Ctx::cstr() silently return the truncated content and advance its
// cursor one byte PAST the buffer's end (`cur += len + 1` where `len == maxlen`) instead of
// failing safely like every other Ctx accessor already does for an out-of-bounds condition. A
// single such overrun is bad enough on its own (forming an invalid pointer one past `end`); if
// this cursor were ever read from again (e.g. a category/variable list with more than one name),
// the next cstr() call's own `end - cur` would underflow to a huge std::size_t, turning strnlen()
// into a real out-of-bounds heap read over corrupt/truncated content -- exactly the class of bug
// this branch's own established "any Ctx accessor throws on out-of-bounds" convention exists to
// prevent. This fixture deliberately omits the category name's trailing null byte so the buffer
// ends immediately after "Default" with nothing left to find a terminator in.
TEST(XactParserTest, ParseXgsUnterminatedCategoryNameThrows)
{
    constexpr uint32_t headerSize       = 65;
    constexpr uint32_t categoryDataSize = 10;

    const uint32_t categoryOffset     = headerSize;
    const uint32_t variableOffset     = categoryOffset + categoryDataSize;
    const uint32_t categoryNameOffset = variableOffset; // no variable data segment needed (count=0)
    const std::string categoryName    = "Default"; // deliberately left unterminated below

    std::vector<uint8_t> data;
    const char magic[4] = { 'X', 'G', 'S', 'F' };
    data.insert(data.end(), magic, magic + 4);
    AppendU16(data, 46); AppendU16(data, 0); AppendU16(data, 0);
    for (int i = 0; i < 8; ++i) data.push_back(0);
    AppendU8(data, 3);

    AppendU16(data, 1); // categoryCount
    AppendU16(data, 0); // variableCount -- zero, so nothing ever reads past the missing terminator
    AppendU16(data, 0); AppendU16(data, 0); AppendU16(data, 0); AppendU16(data, 0); AppendU16(data, 0);

    AppendU32(data, categoryOffset);
    AppendU32(data, variableOffset);
    AppendU32(data, 0); AppendU32(data, 0); AppendU32(data, 0); AppendU32(data, 0);
    AppendU32(data, categoryNameOffset);
    AppendU32(data, categoryNameOffset); // variableNameOffset (unused, variableCount==0)

    AppendU8(data, 255); AppendU16(data, 0); AppendU16(data, 0); AppendU8(data, 0);
    AppendU16(data, 0xFFFF); AppendU8(data, 0xFF); AppendU8(data, 0); // 10-byte category record

    // Category name with NO trailing null terminator -- the buffer ends right after it.
    data.insert(data.end(), categoryName.begin(), categoryName.end());

    EXPECT_THROW(ParseXgs(data), std::runtime_error);
}

TEST(XactParserTest, ParseXsbTruncatedMidRecordThrows)
{
    std::vector<uint8_t> full = BuildXsbWithVariationOfType(3);
    ASSERT_GT(full.size(), 0x50u);
    std::vector<uint8_t> truncated(full.begin(), full.begin() + 0x50);
    EXPECT_THROW(ParseXsb(truncated), std::runtime_error);
}

// P10-XACT-007: two categories sharing the same name is content a real XACT authoring tool would
// never emit (names are validated identifiers at build time), but CNA must still not crash or
// misbehave on it if handed a hand-crafted/adversarial file: categoryNameMap is a plain
// `std::unordered_map<std::string, uint16_t>` built by a sequential `map[name] = i` loop
// (XactParser.cpp), so a duplicate name deterministically resolves to the LAST-declared index --
// well-defined, not UB -- rather than crashing or picking an arbitrary/unstable one.
TEST(XactParserTest, DuplicateCategoryNamesResolveDeterministicallyToLastDeclaredIndex)
{
    constexpr uint32_t headerSize       = 65;
    constexpr uint32_t categoryDataSize = 20; // 2 categories * 10 bytes
    constexpr uint32_t variableDataSize = 13;

    const uint32_t categoryOffset     = headerSize;
    const uint32_t variableOffset     = categoryOffset + categoryDataSize;
    const uint32_t categoryNameOffset = variableOffset + variableDataSize;
    const std::string categoryName    = "Default"; // same name for both categories
    const uint32_t variableNameOffset =
        categoryNameOffset + 2 * (static_cast<uint32_t>(categoryName.size()) + 1);
    const std::string variableName = "Volume";

    std::vector<uint8_t> data;
    const char magic[4] = { 'X', 'G', 'S', 'F' };
    data.insert(data.end(), magic, magic + 4);
    AppendU16(data, 46); AppendU16(data, 0); AppendU16(data, 0);
    for (int i = 0; i < 8; ++i) data.push_back(0);
    AppendU8(data, 3);

    AppendU16(data, 2); // categoryCount == 2
    AppendU16(data, 1); // variableCount
    AppendU16(data, 0); AppendU16(data, 0); AppendU16(data, 0); AppendU16(data, 0); AppendU16(data, 0);

    AppendU32(data, categoryOffset);
    AppendU32(data, variableOffset);
    AppendU32(data, 0); AppendU32(data, 0); AppendU32(data, 0); AppendU32(data, 0);
    AppendU32(data, categoryNameOffset);
    AppendU32(data, variableNameOffset);

    // Category 0: instanceLimit=1, distinguishable from category 1's instanceLimit=2.
    AppendU8(data, 1); AppendU16(data, 0); AppendU16(data, 0); AppendU8(data, 0);
    AppendU16(data, 0xFFFF); AppendU8(data, 0xFF); AppendU8(data, 0);
    // Category 1 (last-declared): instanceLimit=2.
    AppendU8(data, 2); AppendU16(data, 0); AppendU16(data, 0); AppendU8(data, 0);
    AppendU16(data, 0xFFFF); AppendU8(data, 0xFF); AppendU8(data, 0);

    // Variable: accessibility, initialValue, minValue, maxValue.
    AppendU8(data, 0x03); AppendF32(data, 0.5f); AppendF32(data, 0.0f); AppendF32(data, 1.0f);

    AppendCStr(data, categoryName); // category 0's name
    AppendCStr(data, categoryName); // category 1's name (duplicate)
    AppendCStr(data, variableName);

    const XgsData xgs = ParseXgs(data);
    ASSERT_EQ(xgs.categories.size(), 2u);
    ASSERT_TRUE(xgs.categoryNameMap.count("Default"));
    EXPECT_EQ(xgs.categoryNameMap.at("Default"), 1u); // last-declared (index 1) wins
    EXPECT_EQ(xgs.categories[1].instanceLimit, 2);
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
