// SPDX-License-Identifier: MS-PL

#include <cstdint>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Internal/Xnb/XnbContainerBody.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

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

TEST(XnbContainerBodyTest, UncompressedBodyUsesDeclaredRangeAndIgnoresTrailingBytes)
{
    const std::vector<std::uint8_t> file{
        'X', 'N', 'B', 'w', 5, 0, 12, 0, 0, 0, 0xA5, 0x5A, 0xEE};

    const auto container = DecodeXnbContainerBody(
        std::span<const std::uint8_t>(file.data(), file.size()), "trailing.xnb");
    const auto body = container.Bytes();

    EXPECT_EQ(container.header.compression, XnbCompression::None);
    ASSERT_EQ(body.size(), 2u);
    EXPECT_EQ(body[0], 0xA5u);
    EXPECT_EQ(body[1], 0x5Au);
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
