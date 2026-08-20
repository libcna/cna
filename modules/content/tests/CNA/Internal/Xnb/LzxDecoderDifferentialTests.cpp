// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnb.md XNB-30A: differential tests against a reference LZX implementation. Compares
// CNA::Internal::Xnb::DecompressXnbPayload()'s output, byte-for-byte, against reference output
// produced by FNA's own, unmodified src/Content/LzxDecoder.cs run under Mono -- see
// tests/assets/xnb/monogame/windows/lzx/reference-decompressed/README.md for exactly how those
// reference files were generated and how to reproduce them. This is a genuinely independent
// cross-implementation check: the reference bytes come from executing the actual C# source this
// port was based on, not from re-deriving expected output by reasoning about the algorithm again.

#include <fstream>
#include <gtest/gtest.h>
#include <sstream>

#include "CNA/Internal/Xnb/XnbDecompression.hpp"
#include "CNA/Internal/Xnb/XnbHeader.hpp"
#include "System/IO/BinaryReader.hpp"
#include "System/IO/MemoryStream.hpp"

namespace
{
    std::string ReadWholeFile(const std::string& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return {};
        std::ostringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }

    void ExpectMatchesReference(const std::string& xnbPath, const std::string& referencePath)
    {
        const std::string fileBytes = ReadWholeFile(xnbPath);
        ASSERT_FALSE(fileBytes.empty()) << "Real compressed .xnb fixture not found relative to CWD: " << xnbPath;
        const std::string referenceBytes = ReadWholeFile(referencePath);
        ASSERT_FALSE(referenceBytes.empty()) << "Reference decompressed fixture not found relative to CWD: " << referencePath;

        System::IO::MemoryStream headerStream(
            reinterpret_cast<const uint8_t*>(fileBytes.data()), static_cast<int32_t>(fileBytes.size()));
        System::IO::BinaryReader headerReader(&headerStream, true);
        const auto header = CNA::Internal::Xnb::ParseXnbHeader(headerReader, xnbPath);
        ASSERT_EQ(header.compression, CNA::Internal::Xnb::XnbCompression::Lzx);

        System::IO::MemoryStream sizeStream(reinterpret_cast<const uint8_t*>(fileBytes.data()) + 10, 4);
        System::IO::BinaryReader sizeReader(&sizeStream, true);
        const int32_t decompressedSize = sizeReader.ReadInt32();
        const int32_t compressedSize = header.totalLength - 14;

        const auto decompressed = CNA::Internal::Xnb::DecompressXnbPayload(
            reinterpret_cast<const uint8_t*>(fileBytes.data()) + 14, compressedSize, decompressedSize, xnbPath);

        ASSERT_EQ(decompressed.size(), referenceBytes.size());
        EXPECT_EQ(
            std::string(reinterpret_cast<const char*>(decompressed.data()), decompressed.size()),
            referenceBytes)
            << "CNA's decompressed output differs from the reference LzxDecoder.cs (via Mono) output "
               "for " << xnbPath;
    }
}

TEST(LzxDecoderDifferentialTest, SingleBlockFixtureMatchesReferenceDecoderByteForByte)
{
    ExpectMatchesReference(
        "tests/assets/xnb/monogame/windows/lzx/Explosion.xnb",
        "tests/assets/xnb/monogame/windows/lzx/reference-decompressed/Explosion.decompressed.bin");
}

TEST(LzxDecoderDifferentialTest, MultiBlockFixtureMatchesReferenceDecoderByteForByte)
{
    ExpectMatchesReference(
        "tests/assets/xnb/monogame/windows/lzx/FontCalibri14.xnb",
        "tests/assets/xnb/monogame/windows/lzx/reference-decompressed/FontCalibri14.decompressed.bin");
}
