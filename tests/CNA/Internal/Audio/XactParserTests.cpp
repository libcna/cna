// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "CNA/Internal/Audio/XactTypes.hpp"

#include <cstring>
#include <string>
#include <vector>

using CNA::Internal::Audio::ParseXwb;
using CNA::Internal::Audio::XwbData;
using CNA::Internal::Audio::XwbFormat;

namespace
{
    void AppendU32(std::vector<uint8_t>& buf, uint32_t value)
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
