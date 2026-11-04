// SPDX-License-Identifier: MS-PL
//
// TQ-018: the 7-bit encoded integers of an .xnb body -- the type-reader count and the root type
// id -- at the ContentManager::Load<T> boundary.
//
// Oracles: Microsoft.Xna.Framework.dll ContentReader.ReadHeader IL_0001 and
// ReadObjectInternal<T> IL_0001 read BinaryReader.Read7BitEncodedInt, whose documented refusal of
// a corrupt encoding is FormatException; ContentReader.ReadAsset<T> IL_0000-002c catches
// IOException only, so a FormatException leaves XNA's Load<T> unwrapped. ReadObjectInternal<T>
// IL_0014-0036 compares typeId - 1 (conv.u8) with the type-reader table's length and throws
// ContentLoadException (FrameworkResources.BadXnb) for anything outside it, negative ids included.

#include <array>
#include <any>
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
#include "System/FormatException.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"

using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;

namespace
{
    using Bytes = std::vector<std::uint8_t>;
    using ObjectDictionary = std::map<std::string, std::any>;

    Bytes Xnb(const Bytes& body)
    {
        Bytes file{'X', 'N', 'B', 'w', 5, 0, 0, 0, 0, 0};
        const auto total = static_cast<std::uint32_t>(10 + body.size());
        for (int i = 0; i < 4; ++i) { file[6 + i] = static_cast<std::uint8_t>(total >> (8 * i)); }
        file.insert(file.end(), body.begin(), body.end());
        return file;
    }

    /// One Int32Reader, no shared resources, then @p rootId spelled byte by byte, then 7.
    Bytes Int32Asset(const Bytes& rootId)
    {
        System::IO::MemoryStream stream;
        System::IO::BinaryWriter writer(&stream, true);
        writer.Write7BitEncodedInt(1);
        writer.Write(std::string("Microsoft.Xna.Framework.Content.Int32Reader"));
        writer.Write(static_cast<std::int32_t>(0));
        writer.Write7BitEncodedInt(0);
        for (const std::uint8_t byte : rootId) { writer.Write(byte); }
        writer.Write(static_cast<std::int32_t>(7));
        writer.Flush();
        const auto body = stream.ToArray();
        return Xnb(Bytes(body.begin(), body.end()));
    }

    class InMemoryContentManager final : public ContentManager
    {
    public:
        InMemoryContentManager() : ContentManager(nullptr, "Content") { RouteLoadsThroughOpenStreamEXT(true); }

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

    class ContentSevenBitEncodingTest : public ::testing::Test
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
}

TEST_F(ContentSevenBitEncodingTest, PositiveControlTheInt32AssetLoads)
{
    manager.files["value"] = Int32Asset({0x01});
    EXPECT_EQ(manager.Load<std::int32_t>("value"), 7);
}

TEST_F(ContentSevenBitEncodingTest, TypeReaderCountWithAContinuationOnItsFifthByteEscapesAsFormatException)
{
    // 80 80 80 80 80 00: the fifth byte still sets the continuation bit, which Read7BitEncodedInt
    // refuses with FormatException, and XNA's ReadAsset (IOException only) lets it out of Load<T>.
    // A wider handler in ContentReader::ReadAsset -- turning it into ContentLoadException -- fails
    // this test.
    manager.files["overlong"] = Xnb({0x80, 0x80, 0x80, 0x80, 0x80, 0x00});
    EXPECT_THROW((void)manager.Load<ObjectDictionary>("overlong"), System::FormatException);
}

TEST_F(ContentSevenBitEncodingTest, NegativeRootTypeIdIsRefusedAsContentLoadException)
{
    // FF FF FF FF 0F is -1 and 80 80 80 80 08 is int.MinValue; typeId - 1 as an unsigned 64-bit
    // value is far outside a one-entry table either way.
    manager.files["minus-one"] = Int32Asset({0xFF, 0xFF, 0xFF, 0xFF, 0x0F});
    manager.files["min-value"] = Int32Asset({0x80, 0x80, 0x80, 0x80, 0x08});
    EXPECT_THROW((void)manager.Load<std::int32_t>("minus-one"), ContentLoadException);
    EXPECT_THROW((void)manager.Load<std::int32_t>("min-value"), ContentLoadException);
    // And the one id past the table, spelled over-long (81 80 80 80 00 is 1 in five bytes, 82 80
    // 80 80 00 is 2): the over-long 1 still names the Int32Reader, the 2 is out of range.
    manager.files["overlong-one"] = Int32Asset({0x81, 0x80, 0x80, 0x80, 0x00});
    manager.files["overlong-two"] = Int32Asset({0x82, 0x80, 0x80, 0x80, 0x00});
    EXPECT_EQ(manager.Load<std::int32_t>("overlong-one"), 7);
    EXPECT_THROW((void)manager.Load<std::int32_t>("overlong-two"), ContentLoadException);
}
