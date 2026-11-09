// SPDX-License-Identifier: MS-PL

#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Internal/Xnb/XnbContainerBody.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "System/IO/Stream.hpp"
#include "System/InvalidOperationException.hpp"

using CNA::Internal::Xnb::DecodeXnbContainerBody;
using CNA::Internal::Xnb::XnbCompression;
using CNA::Internal::Xnb::XnbReadLimits;
using Microsoft::Xna::Framework::Content::ContentLoadException;

namespace
{
    std::vector<std::uint8_t> ReadBytes(const std::string& path)
    {
        std::ifstream stream(path, std::ios::binary);
        EXPECT_TRUE(stream.is_open()) << path;
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }

    std::vector<std::uint8_t> DecodeBody(const std::string& path)
    {
        const auto file = ReadBytes(path);
        const auto container = DecodeXnbContainerBody(
            std::span<const std::uint8_t>(file.data(), file.size()), path);
        const auto body = container.Bytes();
        return {body.begin(), body.end()};
    }
}

TEST(XnbContainerBodyTest, UncompressedBodyRunsToTheEndOfTheInputWhateverTheDeclaredLength)
{
    // HND-TQ-16, Refresh ContentReader.PrepareStream IL_00ed: an uncompressed reader reads the
    // input stream itself, so the bytes after a smaller declared totalLength are body too.
    const std::vector<std::uint8_t> file{
        'X', 'N', 'B', 'w', 5, 0, 12, 0, 0, 0, 0xA5, 0x5A, 0xEE};

    const auto container = DecodeXnbContainerBody(
        std::span<const std::uint8_t>(file.data(), file.size()), "trailing.xnb");
    const auto body = container.Bytes();

    EXPECT_EQ(container.header.compression, XnbCompression::None);
    ASSERT_EQ(body.size(), 3u);
    EXPECT_EQ(body[0], 0xA5u);
    EXPECT_EQ(body[1], 0x5Au);
    EXPECT_EQ(body[2], 0xEEu);
    EXPECT_FALSE(container.deferredReadFailure);
}

TEST(XnbContainerBodyTest, FileLimitIsEnforcedBeforeContainerParsing)
{
    const std::vector<std::uint8_t> file{
        'X', 'N', 'B', 'w', 5, 0, 10, 0, 0, 0};
    XnbReadLimits limits;
    limits.maxFileSize = 9;

    EXPECT_THROW(
        (void)DecodeXnbContainerBody(
            std::span<const std::uint8_t>(file.data(), file.size()), "limited.xnb", limits),
        ContentLoadException);
}

TEST(XnbContainerBodyTest, LzxAndLz4DecodeToTheirUncompressedTwins)
{
    EXPECT_EQ(
        DecodeBody("tests/assets/xnb/cna/windows/lzx/texture2d_color_mips.xnb"),
        DecodeBody("tests/assets/xnb/cna/windows/uncompressed/texture2d_color_mips.xnb"));
    EXPECT_EQ(
        DecodeBody("tests/assets/xnb/monogame/windows/lz4/white-1.xnb"),
        DecodeBody("tests/assets/xnb/monogame/windows/uncompressed/white-1.xnb"));
}

TEST(XnbContainerBodyTest, Safe002CompressedSizesAndFrameBoundariesRefuseAndRecover)
{
    const auto write = [](std::vector<std::uint8_t>& bytes, std::size_t offset, int32_t value) {
        std::memcpy(bytes.data() + offset, &value, sizeof(value));
    };
    for (const char* path : {"tests/assets/xnb/cna/windows/lzx/texture2d_color_mips.xnb",
                             "tests/assets/xnb/monogame/windows/lz4/white-1.xnb"})
    {
        SCOPED_TRACE(path);
        const bool lzx = std::string(path).find("/lzx/") != std::string::npos;
        const auto seed = ReadBytes(path);
        ASSERT_GE(seed.size(), 14u);
        int32_t outputSize = 0;
        std::memcpy(&outputSize, seed.data() + 10, sizeof(outputSize));
        const auto control = DecodeXnbContainerBody(seed, path);
        const std::vector<std::uint8_t> expected(control.Bytes().begin(), control.Bytes().end());
        EXPECT_FALSE(control.deferredReadFailure);
        const auto recover = [&] {
            const auto decoded = DecodeXnbContainerBody(seed, path);
            EXPECT_EQ(expected, (std::vector<std::uint8_t>(decoded.Bytes().begin(), decoded.Bytes().end())));
        };
        for (const int32_t size : {-1, INT32_MAX})
        {
            auto mutant = seed;
            write(mutant, 10, size);
            if (!lzx)
            {
                // MonoGame's LZ4 body has no Refresh counterpart and stays an exact decode.
                EXPECT_THROW((void)DecodeXnbContainerBody(mutant, path), ContentLoadException);
            }
            else if (size < 0)
            {
                // HND-TQ-16, DecompressNextBuffer IL_0000-000a: nothing to decompress.
                const auto empty = DecodeXnbContainerBody(mutant, path);
                EXPECT_TRUE(empty.Bytes().empty());
                EXPECT_FALSE(empty.deferredReadFailure);
            }
            else
            {
                // Every frame decodes; the read past them meets DecompressionError.
                const auto generous = DecodeXnbContainerBody(mutant, path);
                EXPECT_EQ(expected, (std::vector<std::uint8_t>(generous.Bytes().begin(), generous.Bytes().end())));
                ASSERT_TRUE(generous.deferredReadFailure);
                EXPECT_THROW(std::rethrow_exception(generous.deferredReadFailure),
                             System::InvalidOperationException);
            }
            recover();
        }
        for (const std::size_t length : {10u, 11u, 13u})
        {
            auto mutant = seed;
            mutant.resize(length);
            write(mutant, 6, static_cast<int32_t>(length));
            EXPECT_THROW((void)DecodeXnbContainerBody(mutant, path), ContentLoadException);
            recover();
        }
        XnbReadLimits limits;
        limits.maxDecompressedSize = outputSize - 1;
        if (lzx)
        {
            // CNA's own bound, kept for allocation safety: decoding stops before the frame that
            // would exceed it, and the read that needs that frame meets ContentLoadException.
            const auto bounded = DecodeXnbContainerBody(seed, path, limits);
            EXPECT_LT(bounded.Bytes().size(), expected.size());
            ASSERT_TRUE(bounded.deferredReadFailure);
            EXPECT_THROW(std::rethrow_exception(bounded.deferredReadFailure), ContentLoadException);
        }
        else
        {
            EXPECT_THROW((void)DecodeXnbContainerBody(seed, path, limits), ContentLoadException);
        }
        recover();
        limits.maxDecompressedSize = outputSize;
        const auto exact = DecodeXnbContainerBody(seed, path, limits);
        EXPECT_EQ(expected, (std::vector<std::uint8_t>(exact.Bytes().begin(), exact.Bytes().end())));
    }
}

TEST(XnbContainerBodyTest, Safe002LzxFramesDecodeOnlyWhileTheDeclaredSizeIsUnspent)
{
    const auto seed = ReadBytes("tests/assets/xnb/cna/windows/lzx/texture2d_color_mips.xnb");
    ASSERT_GT(seed.size(), 19u);
    // HND-TQ-16, DecompressNextBuffer IL_0000-000a: a declared size of zero decodes nothing.
    auto noOutput = seed;
    const int32_t zero = 0;
    std::memcpy(noOutput.data() + 10, &zero, sizeof(zero));
    const auto empty = DecodeXnbContainerBody(noOutput, "no-output");
    EXPECT_TRUE(empty.Bytes().empty());
    EXPECT_FALSE(empty.deferredReadFailure);
    EXPECT_FALSE(DecodeXnbContainerBody(seed, "valid").Bytes().empty());
    // Truncated or oversized frame headers, and a zero block: the decoder has nothing it can
    // decode, which Refresh reports as DecompressionError when the reader asks for the bytes.
    for (const std::vector<std::uint8_t>& frame : {
             std::vector<std::uint8_t>{0xFF}, {0xFF, 0, 1}, {0xFF, 0, 1, 0},
             {0xFF, 0, 1, 0xFF, 0xFF}, {0, 1}, {0, 0}})
    {
        auto mutant = std::vector<std::uint8_t>(seed.begin(), seed.begin() + 14);
        mutant.insert(mutant.end(), frame.begin(), frame.end());
        const int32_t length = static_cast<int32_t>(mutant.size());
        const int32_t output = 1;
        std::memcpy(mutant.data() + 6, &length, sizeof(length));
        std::memcpy(mutant.data() + 10, &output, sizeof(output));
        const auto refused = DecodeXnbContainerBody(mutant, "Safe002 frame");
        EXPECT_TRUE(refused.Bytes().empty());
        ASSERT_TRUE(refused.deferredReadFailure);
        std::uint8_t byte = 0;
        EXPECT_THROW((void)refused.OpenBodyStream()->Read(&byte, 0, 1), System::InvalidOperationException);
        EXPECT_FALSE(DecodeXnbContainerBody(seed, "valid").Bytes().empty());
    }
}
