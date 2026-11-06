// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "CNA/Internal/Audio/XactTypes.hpp"
#include "Internal/XactStreamingHeader.hpp"

#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
using Bytes = std::vector<std::uint8_t>;
using namespace CNA::Internal::Audio;

void Put(Bytes& b, std::size_t at, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i) b.at(at+i) = static_cast<std::uint8_t>(v >> (8*i));
}

Bytes MetadataBank(bool compact = false)
{
    // Metadata-only fixture. The wave bytes are deliberately NOT valid encoded audio.
    // Oracle: FACT.h mini-format layout; FACT_internal.c:3230-3288 seek offsets.
    // v46 preamble=52, BANKDATA=96, two entries=48 (or 8), seeks=40, data=32.
    const std::uint32_t seek = compact ? 156 : 196;
    const std::uint32_t wave = seek + 40;
    Bytes b(wave + 32, 0);
    Put(b,0,0x444e4257); Put(b,4,46); Put(b,8,44);
    Put(b,12,52); Put(b,16,96);
    Put(b,20,148); Put(b,24,compact ? 8 : 48);
    Put(b,28,seek); Put(b,32,40);
    Put(b,36,wave); Put(b,40,0); Put(b,44,wave); Put(b,48,32);
    Put(b,52,0x00080000u | (compact ? 0x00020000u : 0));
    Put(b,56,2); b[60]='G'; b[61]='0'; b[62]='5';
    Put(b,124,compact ? 4 : 24); Put(b,132,4);
    const auto wma2 = 3u | (2u<<2) | (44100u<<5) | (3u<<23);
    const auto pro = 3u | (6u<<2) | (48000u<<5) | (169u<<23) | (1u<<31);
    if (compact)
    {
        Put(b,136,pro); Put(b,148,0); Put(b,152,4); // 4 alignment units = 16 bytes
    }
    else
    {
        Put(b,148,(4097u<<4)|5); Put(b,152,wma2);
        Put(b,156,0); Put(b,160,16); Put(b,164,64); Put(b,168,128);
        Put(b,172,(8193u<<4)|10); Put(b,176,pro);
        Put(b,180,16); Put(b,184,16); Put(b,188,256); Put(b,192,512);
    }
    Put(b,seek,0); Put(b,seek+4,16); // record offsets after the 8-byte index
    Put(b,seek+8,3); Put(b,seek+12,0); Put(b,seek+16,8192); Put(b,seek+20,16388);
    Put(b,seek+24,3); Put(b,seek+28,1024); Put(b,seek+32,4096); Put(b,seek+36,8193);
    return b;
}

TEST(EncodedWaveBankMetadata, RetainsBothWmaSelectorsDurationAndLoopFrames)
{
    const auto wb = ParseXwb(MetadataBank());
    EXPECT_EQ(wb.version,46u); EXPECT_EQ(wb.headerVersion,44u);
    ASSERT_EQ(wb.entries.size(),2u);
    const auto& a = wb.entries[0]; const auto& b = wb.entries[1];
    // Low nibble is flags, not part of duration. Bit31 selects WMA Pro, not PCM depth.
    EXPECT_EQ(a.miniFormat,3u|(2u<<2)|(44100u<<5)|(3u<<23));
    EXPECT_EQ(b.miniFormat,3u|(6u<<2)|(48000u<<5)|(169u<<23)|(1u<<31));
    EXPECT_EQ(a.format,XwbFormat::WMA); EXPECT_EQ(b.format,XwbFormat::WMA);
    EXPECT_EQ(a.durationSamples,4097u); EXPECT_EQ(b.durationSamples,8193u);
    EXPECT_EQ(a.channels,2); EXPECT_EQ(b.channels,6);
    EXPECT_EQ(a.sampleRate,44100u); EXPECT_EQ(b.sampleRate,48000u);
    EXPECT_EQ(a.loopStartSample,64u); EXPECT_EQ(a.loopTotalSamples,128u);
    EXPECT_EQ(b.loopStartSample,256u); EXPECT_EQ(b.loopTotalSamples,512u);
    EXPECT_EQ(a.dataOffset,236u); EXPECT_EQ(b.dataOffset,252u);
    EXPECT_EQ(a.dataLength,16u); EXPECT_EQ(b.dataLength,16u);
}

TEST(EncodedWaveBankMetadata, KeepsSeekTablesDistinctAndKeepsUnitsRaw)
{
    const auto wb = ParseXwb(MetadataBank());
    ASSERT_EQ(wb.entries.size(),2u);
    EXPECT_EQ(wb.entries[0].seekTable,(std::vector<std::uint32_t>{0,8192,16388}));
    EXPECT_EQ(wb.entries[1].seekTable,(std::vector<std::uint32_t>{1024,4096,8193}));
}

TEST(EncodedWaveBankMetadata, LegacyPreambleRetainsVersionWithoutReadingAHeaderVersion)
{
    auto b=MetadataBank();
    Put(b,4,43); b.erase(b.begin()+8,b.begin()+12);
    // Removing headerVersion shifts the five segments and all stored bytes by four.
    for (auto at : {8u,16u,24u,32u,40u})
    {
        std::uint32_t offset=0;
        for (int i=0; i<4; ++i) offset|=std::uint32_t(b[at+i])<<(8*i);
        Put(b,at,offset-4);
    }
    const auto wb=ParseXwb(std::move(b));
    EXPECT_EQ(wb.version,43u); EXPECT_EQ(wb.headerVersion,0u);
    ASSERT_EQ(wb.entries.size(),2u);
    EXPECT_EQ(wb.entries[0].durationSamples,4097u);
    EXPECT_EQ(wb.entries[0].dataOffset,232u);
    EXPECT_EQ(wb.entries[0].seekTable,(std::vector<std::uint32_t>{0,8192,16388}));
}

TEST(EncodedWaveBankMetadata, CompactFormatAndXmaBitAreRetainedWithoutInventingDuration)
{
    auto bytes = MetadataBank(true);
    // Literal mini-format change only; this remains a metadata test, never a decode fixture.
    const auto xma = 1u|(2u<<2)|(32000u<<5)|(9u<<23)|(1u<<31);
    Put(bytes,136,xma);
    const auto wb = ParseXwb(std::move(bytes));
    ASSERT_EQ(wb.entries.size(),2u);
    for (const auto& e : wb.entries)
    {
        EXPECT_EQ(e.format,XwbFormat::XMA); EXPECT_EQ(e.miniFormat,xma);
        EXPECT_EQ(e.durationSamples,0u); // Compact entries have no flags/duration DWORD.
        EXPECT_EQ(e.dataLength,16u);
    }
    EXPECT_EQ(wb.entries[0].dataOffset,196u); EXPECT_EQ(wb.entries[1].dataOffset,212u);
    EXPECT_EQ(wb.entries[1].seekTable,(std::vector<std::uint32_t>{1024,4096,8193}));
}

TEST(EncodedWaveBankMetadata, MinusOneSeekSentinelBelongsOnlyToItsEntry)
{
    auto b=MetadataBank(); Put(b,196,0xffffffffu);
    const auto wb=ParseXwb(std::move(b));
    ASSERT_EQ(wb.entries.size(),2u);
    EXPECT_TRUE(wb.entries[0].seekTable.empty());
    EXPECT_EQ(wb.entries[1].seekTable,(std::vector<std::uint32_t>{1024,4096,8193}));
}

TEST(EncodedWaveBankMetadata, SeekFlagControlsInterpretationOfSegment)
{
    auto b=MetadataBank(); Put(b,52,0); Put(b,196,0xfffffff0u);
    const auto wb=ParseXwb(std::move(b));
    ASSERT_EQ(wb.entries.size(),2u);
    for (const auto& e: wb.entries) EXPECT_TRUE(e.seekTable.empty());
}

TEST(EncodedWaveBankMetadata, SeekOffsetsCannotBorrowBytesFromTheWaveSegment)
{
    for (auto offset : {28u,32u,0xfffffffeu,0xfffffff0u})
    {
        SCOPED_TRACE(offset);
        auto b=MetadataBank(); Put(b,196,offset); Put(b,236,1); Put(b,240,123);
        // record base=204, end=236: offset28 leaves only a count, offset32 is wave data.
        EXPECT_THROW(ParseXwb(std::move(b)),std::runtime_error);
    }
}

TEST(EncodedWaveBankMetadata, SeekCountsAreBoundedBeforeAllocation)
{
    for (auto count : {8u,0xffffffffu})
    {
        SCOPED_TRACE(count);
        auto b=MetadataBank(); Put(b,204,count);
        // At 204 there are only 28 bytes after the count within the segment.
        EXPECT_THROW(ParseXwb(std::move(b)),std::runtime_error);
    }
}

TEST(EncodedWaveBankMetadata, SeekSegmentBoundsDoNotUseTheWholeFileAsTheRecord)
{
    auto b=MetadataBank(); Put(b,32,4); // two indices need 8 bytes
    EXPECT_THROW(ParseXwb(b),std::runtime_error);
    b=MetadataBank(); Put(b,32,1000);
    EXPECT_THROW(ParseXwb(b),std::runtime_error);
}

TEST(EncodedWaveBankMetadata, StreamingMetadataAndEmbeddedOffsetsMatchResidentEntries)
{
    const auto bytes=MetadataBank();
    std::string embedded(2048,'\0');
    embedded.append(reinterpret_cast<const char*>(bytes.data()),bytes.size());
    std::istringstream source(embedded,std::ios::binary);
    const auto streamed=ParseXwbStreamingHeaderAtOffset(source,"literal.xwb",2048);
    ASSERT_TRUE(streamed.streaming); ASSERT_EQ(streamed.entries.size(),2u);
    EXPECT_EQ(streamed.version,46u); EXPECT_EQ(streamed.headerVersion,44u);
    EXPECT_EQ(streamed.fileData.size(),236u); // no wave bytes resident
    EXPECT_EQ(streamed.entries[0].dataOffset,2284u); // 2048+236
    EXPECT_EQ(streamed.entries[1].dataOffset,2300u); // 2048+252
    EXPECT_EQ(streamed.entries[0].miniFormat,3u|(2u<<2)|(44100u<<5)|(3u<<23));
    EXPECT_EQ(streamed.entries[1].durationSamples,8193u);
    EXPECT_EQ(streamed.entries[0].seekTable,(std::vector<std::uint32_t>{0,8192,16388}));
    EXPECT_EQ(streamed.entries[1].seekTable,(std::vector<std::uint32_t>{1024,4096,8193}));
}

TEST(EncodedWaveBankMetadata, EncodedPlayOffsetCannotWrapToTheBankHeader)
{
    for (bool compact : {false,true})
    {
        auto b=MetadataBank(compact);
        Put(b,44,0xfffffff0u);
        if (compact) { Put(b,48,1024); Put(b,132,256); Put(b,148,1); Put(b,152,2); }
        else Put(b,156,256);
        EXPECT_THROW(ParseXwb(std::move(b)),std::runtime_error);
    }
}

TEST(EncodedWaveBankMetadata, StreamingSeekExtentIsCheckedBeforePrefixAllocation)
{
    auto b=MetadataBank(); Put(b,32,4096);
    // Encoded seek metadata claims196+4096=4292 bytes in a268-byte source.
    // A small extent suffices to distinguish the pre-allocation guard from the
    // old allocate-then-read path, so its mutation does not risk a huge allocation.
    std::string bytes(reinterpret_cast<const char*>(b.data()),b.size());
    std::istringstream source(bytes,std::ios::binary);
    try
    {
        (void)ParseXwbStreamingHeaderAtOffset(source,"literal.xwb",0);
        FAIL()<<"accepted metadata beyond the source";
    }
    catch (const std::runtime_error& error)
    {
        EXPECT_NE(std::string(error.what()).find("metadata segments exceed the source file"),
                  std::string::npos) << error.what();
    }
}
} // namespace
