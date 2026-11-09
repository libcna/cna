// SPDX-License-Identifier: MS-PL
//
// HND-TQ-16 (.NET 4 binary decoding): an .xnb's strings, characters and 7-bit encoded integers
// decode as .NET Framework 4.0's BinaryReader decodes them, because ContentReader is one
// (Microsoft.Xna.Framework.dll ContentReader..ctor IL_0000-0002 -> BinaryReader(Stream), mscorlib
// 4.0.30319.1 IL_0002 `new UTF8Encoding()`). sharp-runtime's BinaryReader carries that behaviour
// (docs/Migration-BinaryReaderNet4Decoding.md there); these tests pin it at Load<T>:
// Read7BitEncodedInt IL_0019-002b keeps only the low four bits of a fifth byte, and an invalid
// UTF-8 sequence in a String or Char reads as one U+FFFD (UTF8Encoding.GetChars IL_0100-0157).

#include "CNA/TestSupport/EmptyServiceProvider.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Internal/Xnb/XnbBuiltInReaders.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "System/IO/MemoryStream.hpp"

using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;

namespace
{
    using Bytes = std::vector<std::uint8_t>;

    void Append(Bytes& bytes, const std::string& text)
    {
        bytes.push_back(static_cast<std::uint8_t>(text.size()));
        bytes.insert(bytes.end(), text.begin(), text.end());
    }

    /// One reader @p readerName, then @p sharedCount and @p rootId spelled byte by byte, then
    /// @p payload, in an uncompressed container; @p readerCount spells the reader count.
    Bytes Xnb(const std::string& readerName, const Bytes& payload, const Bytes& readerCount = {0x01},
              const Bytes& sharedCount = {0x00}, const Bytes& rootId = {0x01})
    {
        Bytes body = readerCount;
        Append(body, readerName);
        body.insert(body.end(), {0, 0, 0, 0});
        body.insert(body.end(), sharedCount.begin(), sharedCount.end());
        body.insert(body.end(), rootId.begin(), rootId.end());
        body.insert(body.end(), payload.begin(), payload.end());
        Bytes file{'X', 'N', 'B', 'w', 5, 0, 0, 0, 0, 0};
        file.insert(file.end(), body.begin(), body.end());
        const auto total = static_cast<std::uint32_t>(file.size());
        for (int i = 0; i < 4; ++i) { file[6 + i] = static_cast<std::uint8_t>(total >> (8 * i)); }
        return file;
    }

    Bytes Int32Xnb(const Bytes& readerCount, const Bytes& sharedCount, const Bytes& rootId)
    {
        return Xnb("Microsoft.Xna.Framework.Content.Int32Reader", {7, 0, 0, 0}, readerCount,
                   sharedCount, rootId);
    }

    Bytes StringXnb(const Bytes& utf8)
    {
        Bytes payload{static_cast<std::uint8_t>(utf8.size())};
        payload.insert(payload.end(), utf8.begin(), utf8.end());
        return Xnb("Microsoft.Xna.Framework.Content.StringReader", payload);
    }

    class InMemoryContentManager final : public ContentManager
    {
    public:
        InMemoryContentManager() : ContentManager(CNA::TestSupport::EmptyServices(), "Content")
        {
            RouteLoadsThroughOpenStreamEXT(true);
        }

        std::map<std::string, Bytes> files;

    protected:
        std::unique_ptr<System::IO::Stream> OpenStream(const std::string& assetName) override
        {
            const auto found = files.find(assetName);
            if (found == files.end()) { throw ContentLoadException("no asset '" + assetName + "'"); }
            return std::make_unique<System::IO::MemoryStream>(
                found->second.data(), static_cast<std::int32_t>(found->second.size()), false);
        }
    };

    class ContentNet4BinaryDecodingTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            CNA::Internal::Xnb::RegisterAllBuiltInXnbReaders();
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }

        InMemoryContentManager manager;
    };

    const std::string Fffd = "\xEF\xBF\xBD";
}

TEST_F(ContentNet4BinaryDecodingTest, TheFifthByteOfAHeaderIntegerKeepsOnlyItsLowFourBits)
{
    // 81 80 80 80 70 is 1 under .NET 4 (0x70 << 28 leaves nothing in an Int32); the stricter
    // modern rule refused it with FormatException.
    const Bytes one{0x81, 0x80, 0x80, 0x80, 0x70};
    manager.files["reader-count"] = Int32Xnb(one, {0x00}, {0x01});
    manager.files["root-id"] = Int32Xnb({0x01}, {0x00}, one);
    // FF FF FF FF 7F is -1: no shared resources (Refresh ReadHeader IL_001b-001d).
    manager.files["shared-count"] = Int32Xnb({0x01}, {0xFF, 0xFF, 0xFF, 0xFF, 0x7F}, {0x01});
    EXPECT_EQ(manager.Load<std::int32_t>("reader-count"), 7);
    EXPECT_EQ(manager.Load<std::int32_t>("root-id"), 7);
    EXPECT_EQ(manager.Load<std::int32_t>("shared-count"), 7);
}

TEST_F(ContentNet4BinaryDecodingTest, AnInvalidUtf8StringAssetReadsWithReplacementCharacters)
{
    manager.files["lead"] = StringXnb({'A', 0xFF, 'B'});
    manager.files["non-shortest"] = StringXnb({0xE0, 0x80, 0x80});
    manager.files["ended"] = StringXnb({0xC3, 'A'});
    manager.files["valid"] = StringXnb({0xC3, 0xA9});
    EXPECT_EQ(manager.Load<std::string>("lead"), "A" + Fffd + "B");
    // .NET 4 groups E0 80 as one sequence: two U+FFFD, where modern .NET writes three.
    EXPECT_EQ(manager.Load<std::string>("non-shortest"), Fffd + Fffd);
    EXPECT_EQ(manager.Load<std::string>("ended"), Fffd + "A");
    EXPECT_EQ(manager.Load<std::string>("valid"), "\xC3\xA9");
}

TEST_F(ContentNet4BinaryDecodingTest, AnInvalidUtf8CharAssetReadsAsOneReplacementCharacter)
{
    manager.files["char"] = Xnb("Microsoft.Xna.Framework.Content.CharReader", {0xFF});
    EXPECT_EQ(manager.Load<char16_t>("char"), static_cast<char16_t>(0xFFFD));
}
