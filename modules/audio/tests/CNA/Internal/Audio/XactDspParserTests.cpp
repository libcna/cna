// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Audio/XactTypes.hpp"
#include <gtest/gtest.h>
#include <bit>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace
{
using Bytes = std::vector<uint8_t>;
using namespace CNA::Internal::Audio;
void U16(Bytes &b, uint16_t v)
{
    b.push_back(v & 255);
    b.push_back(v >> 8);
}
void U32(Bytes &b, uint32_t v)
{
    U16(b, v & 65535);
    U16(b, v >> 16);
}
void F32(Bytes &b, float v) { U32(b, std::bit_cast<uint32_t>(v)); }
void Put16(Bytes &b, std::size_t at, uint16_t v)
{
    b.at(at) = v & 255;
    b.at(at + 1) = v >> 8;
}
void Put32(Bytes &b, std::size_t at, uint32_t v)
{
    Put16(b, at, v & 65535);
    Put16(b, at + 2, v >> 16);
}
Bytes Settings()
{
    // Oracle: FACT_internal.c:2073-2090,2166-2207. Full header = 19+14+44 = 77.
    // Deliberately separate tables: headers at 96,101; parameters at 128,143,158.
    // Literal dyadic floats make every parsed expected value exact in float32.
    Bytes b(96, 0);
    b[0] = 'X';
    b[1] = 'G';
    b[2] = 'S';
    b[3] = 'F';
    Put16(b, 4, 46);
    Put16(b, 6, 42);
    b[18] = 3;
    Put16(b, 29, 2);
    Put16(b, 31, 3);
    Put32(b, 69, 96);
    Put32(b, 73, 128);
    b.push_back(1);
    U16(b, 2);
    U16(b, 0x1234);
    b.push_back(5);
    U16(b, 1);
    U16(b, 0xABCD);
    b.resize(128, 0xA5);
    for (int i = 0; i < 3; ++i)
    {
        b.push_back(static_cast<uint8_t>(i + 2));
        F32(b, 1.25f + i);
        F32(b, -2.5f - i);
        F32(b, 8.0f + i);
        U16(b, 0x5500 + i);
    }
    return b;
}
TEST(XactDspParser, PreservesPresetCodesParameterPartitionAndRawFields)
{
    const auto x = ParseXgs(Settings());
    ASSERT_EQ(x.dspPresets.size(), 2u);
    ASSERT_EQ(x.dspCodeMap.size(), 2u);
    ASSERT_TRUE(x.dspCodeMap.contains(96));
    ASSERT_TRUE(x.dspCodeMap.contains(101));
    EXPECT_EQ(x.dspCodeMap.at(96), 0u);
    EXPECT_EQ(x.dspCodeMap.at(101), 1u);
    EXPECT_EQ(x.dspCodeMap.count(0), 0u); // index zero is not an absolute preset code
    EXPECT_EQ(x.dspPresets[0].accessibility, 1);
    EXPECT_EQ(x.dspPresets[0].unknown, 0x1234);
    EXPECT_EQ(x.dspPresets[1].accessibility, 5);
    EXPECT_EQ(x.dspPresets[1].unknown, 0xABCD);
    ASSERT_EQ(x.dspPresets[0].parameters.size(), 2u);
    ASSERT_EQ(x.dspPresets[1].parameters.size(), 1u);
    for (int i = 0; i < 3; ++i)
    {
        const auto &p = i < 2 ? x.dspPresets[0].parameters[i] : x.dspPresets[1].parameters[0];
        EXPECT_EQ(p.type, i + 2);
        EXPECT_FLOAT_EQ(p.value, 1.25f + i);
        EXPECT_FLOAT_EQ(p.minValue, -2.5f - i);
        EXPECT_FLOAT_EQ(p.maxValue, 8.0f + i);
        EXPECT_EQ(p.unknown, 0x5500 + i);
    }
}
TEST(XactDspParser, RejectsEveryTruncatedParameterAndPresetHeader)
{
    // CNA bounded-reader contract: no partial successful parse, including the last unknown byte.
    for (std::size_t n = 128; n < 173; ++n)
    {
        auto b = Settings();
        b.resize(n);
        EXPECT_THROW(ParseXgs(b), std::runtime_error) << n;
    }
    for (std::size_t n = 96; n < 106; ++n)
    {
        auto b = Settings();
        b.resize(n);
        EXPECT_THROW(ParseXgs(b), std::runtime_error) << n;
    }
}
TEST(XactDspParser, RejectsOutOfRangeOffsetsAndInconsistentCounts)
{
    for (auto offsetField : {69u, 73u})
    {
        auto b = Settings();
        Put32(b, offsetField, 0xFFFFFFFFu);
        EXPECT_THROW(ParseXgs(b), std::runtime_error);
    }
    for (uint16_t count : {uint16_t(2), uint16_t(4), uint16_t(65535)})
    {
        auto b = Settings();
        Put16(b, 31, count);
        EXPECT_THROW(ParseXgs(b), std::runtime_error);
    }
    auto b = Settings();
    Put16(b, 97, 65535);
    EXPECT_THROW(ParseXgs(b), std::runtime_error);
    b = Settings();
    Put16(b, 29, 0);
    EXPECT_THROW(ParseXgs(b), std::runtime_error);
}
TEST(XactDspParser, EmptyTablesDoNotDereferenceOffsets)
{
    auto b = Settings();
    Put16(b, 29, 0);
    Put16(b, 31, 0);
    Put32(b, 69, 0xFFFFFFFFu);
    Put32(b, 73, 0xFFFFFFFFu);
    const auto x = ParseXgs(b);
    EXPECT_TRUE(x.dspPresets.empty());
    EXPECT_TRUE(x.dspCodeMap.empty());
}
} // namespace

TEST(XactDspParser, EmptyFullHeaderIsExactlySeventySevenBytes)
{
    // FACT header: 19-byte preamble + seven u16 counts + eleven u32 offsets =77.
    // No table payload is required when every count is zero.
    auto b=Settings(); b.resize(77); Put16(b,29,0); Put16(b,31,0);
    CNA::Internal::Audio::XgsData x;
    ASSERT_NO_THROW(x=CNA::Internal::Audio::ParseXgs(b));
    EXPECT_TRUE(x.categories.empty()); EXPECT_TRUE(x.variables.empty()); EXPECT_TRUE(x.rpcs.empty());
    EXPECT_TRUE(x.dspPresets.empty()); EXPECT_TRUE(x.dspCodeMap.empty());
    b.resize(76); EXPECT_THROW(CNA::Internal::Audio::ParseXgs(b),std::runtime_error);
}
