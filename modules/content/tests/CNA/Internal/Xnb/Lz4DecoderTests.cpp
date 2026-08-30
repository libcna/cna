// SPDX-License-Identifier: MS-PL

#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Internal/Xnb/XnbDecompression.hpp"
#include "CNA/Internal/Xnb/XnbHeader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "System/IO/BinaryReader.hpp"
#include "System/IO/MemoryStream.hpp"

using Microsoft::Xna::Framework::Content::ContentLoadException;

namespace
{
    std::vector<std::uint8_t> ReadBytes(const std::string& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }

    void ExpectRejected(const std::vector<std::uint8_t>& compressed,
                        std::int32_t decompressedSize)
    {
        EXPECT_THROW(
            static_cast<void>(CNA::Internal::Xnb::DecompressXnbLz4Payload(
                compressed.data(), static_cast<std::int32_t>(compressed.size()),
                decompressedSize, "malformed-lz4.xnb")),
            ContentLoadException);
    }
}

TEST(Lz4DecoderTest, UpstreamCompressedFixtureMatchesTheExternalMonoGameBodyExactly)
{
    const std::vector<std::uint8_t> compressed =
        ReadBytes("tests/assets/xnb/monogame/windows/lz4/white-1.xnb");
    const std::vector<std::uint8_t> uncompressed =
        ReadBytes("tests/assets/xnb/monogame/windows/uncompressed/white-1.xnb");
    ASSERT_GE(compressed.size(), 14u);
    ASSERT_GE(uncompressed.size(), 10u);

    System::IO::MemoryStream headerStream(
        compressed.data(), static_cast<std::int32_t>(compressed.size()));
    System::IO::BinaryReader headerReader(&headerStream, true);
    const CNA::Internal::Xnb::XnbHeader header =
        CNA::Internal::Xnb::ParseXnbHeader(headerReader, "white-1.xnb");
    ASSERT_EQ(header.compression, CNA::Internal::Xnb::XnbCompression::Lz4);

    System::IO::MemoryStream sizeStream(compressed.data() + 10u, 4);
    System::IO::BinaryReader sizeReader(&sizeStream, true);
    const std::int32_t decompressedSize = sizeReader.ReadInt32();
    ASSERT_EQ(decompressedSize, static_cast<std::int32_t>(uncompressed.size() - 10u));

    const std::vector<std::uint8_t> body =
        CNA::Internal::Xnb::DecompressXnbLz4Payload(
            compressed.data() + 14u,
            static_cast<std::int32_t>(compressed.size() - 14u),
            decompressedSize, "white-1.xnb");
    EXPECT_EQ(body, std::vector<std::uint8_t>(uncompressed.begin() + 10u,
                                              uncompressed.end()));
}

TEST(Lz4DecoderTest, RejectsEveryMalformedRawBlockBoundary)
{
    ExpectRejected({0xF0u}, 0);                         // truncated literal extension
    ExpectRejected({0x20u, 'a'}, 2);                    // literal exceeds input
    ExpectRejected({0x10u, 'a', 0x01u}, 5);             // truncated offset
    ExpectRejected({0x10u, 'a', 0x00u, 0x00u}, 5);      // zero offset
    ExpectRejected({0x10u, 'a', 0x02u, 0x00u}, 5);      // offset before history
    ExpectRejected({0x1Fu, 'a', 0x01u, 0x00u}, 20);     // truncated match extension
    ExpectRejected({0x20u, 'a', 'b'}, 1);               // literal exceeds output
    ExpectRejected({0x10u, 'a', 0x01u, 0x00u}, 4);      // match exceeds output
    ExpectRejected({0x10u, 'a'}, 2);                    // final size mismatch
}

TEST(Lz4DecoderTest, ReadLimitsRejectBombSizesBeforeAllocation)
{
    const std::vector<std::uint8_t> compressed{0x00u};
    CNA::Internal::Xnb::XnbReadLimits limits;
    limits.maxFileSize = 0;
    EXPECT_THROW(
        static_cast<void>(CNA::Internal::Xnb::DecompressXnbLz4Payload(
            compressed.data(), 1, 0, "compressed-limit.xnb", limits)),
        ContentLoadException);

    limits.maxFileSize = 1;
    limits.maxDecompressedSize = 3;
    EXPECT_THROW(
        static_cast<void>(CNA::Internal::Xnb::DecompressXnbLz4Payload(
            compressed.data(), 1, 4, "output-limit.xnb", limits)),
        ContentLoadException);
    EXPECT_THROW(
        static_cast<void>(CNA::Internal::Xnb::DecompressXnbLz4Payload(
            nullptr, 1, 0, "null-input.xnb", limits)),
        ContentLoadException);
}
