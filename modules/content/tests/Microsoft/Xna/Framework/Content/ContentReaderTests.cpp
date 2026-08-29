// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnb.md XNB-15/XNB-16/XNB-16B: unit tests for ContentReader's root-object dispatch
// (1-based type-reader index), shared-resource fixups, and reader-version enforcement.

#include <gtest/gtest.h>
#include <fstream>
#include <sstream>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "CNA/Internal/Xnb/XnbHeader.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/EndOfStreamException.hpp"
#include "System/IO/MemoryStream.hpp"

using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentReader;
using Microsoft::Xna::Framework::Content::ContentTypeReader;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;

namespace
{
    struct TestPayload
    {
        int32_t value = 0;
    };

    class TestOnlyPayloadReader : public ContentTypeReader<TestPayload>
    {
    public:
        TestOnlyPayloadReader() : ContentTypeReader<TestPayload>("CNA.Test.TestPayload") {}

    protected:
        TestPayload Read(ContentReader& input, std::optional<TestPayload> existingInstance) override
        {
            TestPayload result = existingInstance.value_or(TestPayload{});
            result.value = input.ReadInt32();
            return result;
        }
    };

    struct TestNodeResult
    {
        int32_t sharedValue = -1;
        bool fixupRan = false;
    };

    class TestOnlyNodeReader : public ContentTypeReader<std::shared_ptr<TestNodeResult>>
    {
    public:
        TestOnlyNodeReader() : ContentTypeReader<std::shared_ptr<TestNodeResult>>("CNA.Test.TestNode") {}

    protected:
        std::shared_ptr<TestNodeResult> Read(
            ContentReader& input, std::optional<std::shared_ptr<TestNodeResult>> existingInstance) override
        {
            auto node = (existingInstance && *existingInstance) ? *existingInstance : std::make_shared<TestNodeResult>();
            TestNodeResult* raw = node.get();
            input.ReadSharedResource<TestPayload>([raw](TestPayload p) {
                raw->sharedValue = p.value;
                raw->fixupRan = true;
            });
            return node;
        }
    };

    // REMED-CONTENT-006: a self-referential node type, used to drive InnerReadObject<T>()'s own
    // recursion (each node's Read() may call input.ReadObject<T>() again for a "child") -- the
    // same class of unbounded recursion as XnbTypeName::ParseOne()'s (see that file's own fix),
    // just triggered by nested object dispatch instead of nested generic-argument brackets.
    struct RecursiveNode
    {
        int32_t depth = 0;
        std::shared_ptr<RecursiveNode> child;
    };

    class RecursiveNodeReader : public ContentTypeReader<std::shared_ptr<RecursiveNode>>
    {
    public:
        RecursiveNodeReader() : ContentTypeReader<std::shared_ptr<RecursiveNode>>("CNA.Test.RecursiveNode") {}

    protected:
        std::shared_ptr<RecursiveNode> Read(
            ContentReader& input, std::optional<std::shared_ptr<RecursiveNode>> existingInstance) override
        {
            auto node = (existingInstance && *existingInstance) ? *existingInstance : std::make_shared<RecursiveNode>();
            node->depth = input.ReadInt32();
            const bool hasChild = input.ReadInt32() != 0;
            if (hasChild)
            {
                node->child = input.ReadObject<std::shared_ptr<RecursiveNode>>();
            }
            return node;
        }
    };

    // Writes remainingDepth+1 nested RecursiveNode dispatch calls (root plus remainingDepth
    // children), each preceded by its own 1-based type-reader index -- matching the object
    // dispatch protocol InnerReadObject<T>() expects at every nesting level, not just the root.
    void WriteRecursiveNode(System::IO::BinaryWriter& w, int remainingDepth)
    {
        w.Write7BitEncodedInt(1); // -> table entry 0, CNA.Test.RecursiveNodeReader
        w.Write(static_cast<int32_t>(remainingDepth));
        if (remainingDepth > 0)
        {
            w.Write(static_cast<int32_t>(1)); // has child
            WriteRecursiveNode(w, remainingDepth - 1);
        }
        else
        {
            w.Write(static_cast<int32_t>(0)); // no child
        }
    }

    class ContentReaderTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            ContentTypeReaderManager::AddTypeCreator(
                "CNA.Test.TestPayloadReader", [] { return std::make_unique<TestOnlyPayloadReader>(); });
            ContentTypeReaderManager::AddTypeCreator(
                "CNA.Test.TestNodeReader", [] { return std::make_unique<TestOnlyNodeReader>(); });
            ContentTypeReaderManager::AddTypeCreator(
                "CNA.Test.RecursiveNodeReader", [] { return std::make_unique<RecursiveNodeReader>(); });
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }

        // Builds [type-reader table][sharedResourceCount][payload bytes] into a byte buffer,
        // then returns a ContentReader positioned at the start of it.
        static std::vector<uint8_t> BuildBody(
            const std::vector<std::pair<std::string, int32_t>>& tableEntries,
            int32_t sharedResourceCount,
            const std::function<void(System::IO::BinaryWriter&)>& writePayload)
        {
            System::IO::MemoryStream ms;
            System::IO::BinaryWriter writer(&ms, true);

            writer.Write7BitEncodedInt(static_cast<int32_t>(tableEntries.size()));
            for (const auto& [name, version] : tableEntries)
            {
                writer.Write(name);
                writer.Write(version);
            }
            writer.Write7BitEncodedInt(sharedResourceCount);
            writePayload(writer);
            writer.Flush();

            auto buf = ms.ToArray();
            return std::vector<uint8_t>(buf.begin(), buf.end());
        }
    };
}

TEST_F(ContentReaderTest, RootObjectDispatchesToTheCorrectReader)
{
    const auto bytes = BuildBody(
        {{"CNA.Test.TestPayloadReader", 0}}, 0,
        [](System::IO::BinaryWriter& w) {
            w.Write7BitEncodedInt(1); // root index 1 -> table entry 0
            w.Write((int32_t)42);
        });

    System::IO::MemoryStream ms(bytes.data(), (int32_t)bytes.size());
    ContentReader reader(nullptr, &ms, "test", 5, 'w');

    const TestPayload result = reader.ReadAsset<TestPayload>();

    EXPECT_EQ(result.value, 42);
}

TEST_F(ContentReaderTest, RootIndexZeroReturnsDefaultInstance)
{
    const auto bytes = BuildBody(
        {}, 0,
        [](System::IO::BinaryWriter& w) {
            w.Write7BitEncodedInt(0); // null root object
        });

    System::IO::MemoryStream ms(bytes.data(), (int32_t)bytes.size());
    ContentReader reader(nullptr, &ms, "test", 5, 'w');

    const TestPayload result = reader.ReadAsset<TestPayload>();

    EXPECT_EQ(result.value, 0); // TestPayload{} default
}

TEST_F(ContentReaderTest, RootIndexZeroWithNoDefaultConstructorAndNoExistingInstanceThrows)
{
    // A type genuinely lacking a default constructor and no existing instance passed in has no
    // C++ representation for FNA's default(T)/null root object -- see InnerReadObject<T>()'s docs.
    struct NoDefaultCtor
    {
        explicit NoDefaultCtor(int v) : value(v) {}
        int value;
    };

    const auto bytes = BuildBody(
        {}, 0,
        [](System::IO::BinaryWriter& w) { w.Write7BitEncodedInt(0); });

    System::IO::MemoryStream ms(bytes.data(), (int32_t)bytes.size());
    ContentReader reader(nullptr, &ms, "test", 5, 'w');

    EXPECT_THROW(reader.ReadAsset<NoDefaultCtor>(), ContentLoadException);
}

TEST_F(ContentReaderTest, UnregisteredReaderNameThrowsContentLoadException)
{
    const auto bytes = BuildBody(
        {{"CNA.Test.NotRegistered", 0}}, 0,
        [](System::IO::BinaryWriter& w) { w.Write7BitEncodedInt(0); });

    System::IO::MemoryStream ms(bytes.data(), (int32_t)bytes.size());
    ContentReader reader(nullptr, &ms, "test", 5, 'w');

    EXPECT_THROW(reader.ReadAsset<TestPayload>(), ContentLoadException);
}

TEST_F(ContentReaderTest, ReaderVersionMismatchThrowsContentLoadException)
{
    // TestOnlyPayloadReader's TypeVersion defaults to 0; the table claims version 99.
    const auto bytes = BuildBody(
        {{"CNA.Test.TestPayloadReader", 99}}, 0,
        [](System::IO::BinaryWriter& w) { w.Write7BitEncodedInt(0); });

    System::IO::MemoryStream ms(bytes.data(), (int32_t)bytes.size());
    ContentReader reader(nullptr, &ms, "test", 5, 'w');

    EXPECT_THROW(reader.ReadAsset<TestPayload>(), ContentLoadException);
}

TEST_F(ContentReaderTest, OutOfRangeRootIndexThrowsContentLoadException)
{
    const auto bytes = BuildBody(
        {{"CNA.Test.TestPayloadReader", 0}}, 0,
        [](System::IO::BinaryWriter& w) {
            w.Write7BitEncodedInt(5); // only 1 reader registered
        });

    System::IO::MemoryStream ms(bytes.data(), (int32_t)bytes.size());
    ContentReader reader(nullptr, &ms, "test", 5, 'w');

    EXPECT_THROW(reader.ReadAsset<TestPayload>(), ContentLoadException);
}

// REMED-CONTENT-006: InnerReadObject<T>() previously recursed with no depth limit whenever a type
// reader called back into ReadObject<U>() for a nested member -- the same unbounded-recursion
// class as XnbTypeName::ParseOne()'s. A tiny custom limit keeps this test fast and deterministic
// rather than constructing thousands of real nested dispatch calls.
TEST_F(ContentReaderTest, ObjectNestingDepthExceedingLimitThrowsContentLoadException)
{
    CNA::Internal::Xnb::XnbReadLimits tightLimits;
    tightLimits.maxObjectNestingDepth = 3;

    const auto bytes = BuildBody(
        {{"CNA.Test.RecursiveNodeReader", 0}}, 0,
        [](System::IO::BinaryWriter& w) { WriteRecursiveNode(w, 4); }); // 5 nested dispatch calls: 0..4

    System::IO::MemoryStream ms(bytes.data(), (int32_t)bytes.size());
    ContentReader reader(nullptr, &ms, "test", 5, 'w', nullptr, tightLimits);

    EXPECT_THROW(reader.ReadAsset<std::shared_ptr<RecursiveNode>>(), ContentLoadException);
}

TEST_F(ContentReaderTest, ObjectNestingDepthAtLimitDoesNotThrow)
{
    CNA::Internal::Xnb::XnbReadLimits tightLimits;
    tightLimits.maxObjectNestingDepth = 3;

    const auto bytes = BuildBody(
        {{"CNA.Test.RecursiveNodeReader", 0}}, 0,
        [](System::IO::BinaryWriter& w) { WriteRecursiveNode(w, 3); }); // 4 nested dispatch calls: 0..3

    System::IO::MemoryStream ms(bytes.data(), (int32_t)bytes.size());
    ContentReader reader(nullptr, &ms, "test", 5, 'w', nullptr, tightLimits);

    const auto root = reader.ReadAsset<std::shared_ptr<RecursiveNode>>();
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->depth, 3);
    ASSERT_NE(root->child, nullptr);
    EXPECT_EQ(root->child->child->child->depth, 0);
    EXPECT_EQ(root->child->child->child->child, nullptr);
}

TEST_F(ContentReaderTest, SharedResourceFixupRunsAfterRootObjectWithResolvedValue)
{
    const auto bytes = BuildBody(
        {{"CNA.Test.TestNodeReader", 0}, {"CNA.Test.TestPayloadReader", 0}},
        1, // one shared resource
        [](System::IO::BinaryWriter& w) {
            // Root object: dispatch to TestNodeReader (table entry 1), whose Read() consumes
            // a shared-resource index referencing shared resource #1.
            w.Write7BitEncodedInt(1);
            w.Write7BitEncodedInt(1); // ReadSharedResource's own index read, inside TestNodeReader::Read()
            // Shared resources section: one entry, dispatched to TestPayloadReader (table entry 2).
            w.Write7BitEncodedInt(2);
            w.Write((int32_t)777);
        });

    System::IO::MemoryStream ms(bytes.data(), (int32_t)bytes.size());
    ContentReader reader(nullptr, &ms, "test", 5, 'w');

    const auto result = reader.ReadAsset<std::shared_ptr<TestNodeResult>>();

    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->fixupRan);
    EXPECT_EQ(result->sharedValue, 777);
}

TEST_F(ContentReaderTest, SharedResourceIndexZeroNeverRunsFixup)
{
    const auto bytes = BuildBody(
        {{"CNA.Test.TestNodeReader", 0}}, 0,
        [](System::IO::BinaryWriter& w) {
            w.Write7BitEncodedInt(1); // root -> TestNodeReader
            w.Write7BitEncodedInt(0); // ReadSharedResource's index: null reference
        });

    System::IO::MemoryStream ms(bytes.data(), (int32_t)bytes.size());
    ContentReader reader(nullptr, &ms, "test", 5, 'w');

    const auto result = reader.ReadAsset<std::shared_ptr<TestNodeResult>>();

    ASSERT_NE(result, nullptr);
    EXPECT_FALSE(result->fixupRan);
    EXPECT_EQ(result->sharedValue, -1);
}

TEST_F(ContentReaderTest, MathReadHelpersReadFieldsInFnaOrder)
{
    System::IO::MemoryStream ms;
    System::IO::BinaryWriter writer(&ms, true);
    // Vector4-shaped payload used for ReadVector4/ReadQuaternion (both are 4 floats in the same order).
    writer.Write(1.0f); writer.Write(2.0f); writer.Write(3.0f); writer.Write(4.0f);
    writer.Flush();
    auto buf = ms.ToArray();

    System::IO::MemoryStream ms2(buf.data(), (int32_t)buf.size());
    ContentReader reader(nullptr, &ms2, "test", 5, 'w');

    const auto v = reader.ReadVector4();
    EXPECT_FLOAT_EQ(v.X, 1.0f);
    EXPECT_FLOAT_EQ(v.Y, 2.0f);
    EXPECT_FLOAT_EQ(v.Z, 3.0f);
    EXPECT_FLOAT_EQ(v.W, 4.0f);
}

TEST_F(ContentReaderTest, ReadColorReadsRgbaBytesInOrder)
{
    std::vector<uint8_t> bytes{10, 20, 30, 40};
    System::IO::MemoryStream ms(bytes.data(), (int32_t)bytes.size());
    ContentReader reader(nullptr, &ms, "test", 5, 'w');

    const auto color = reader.ReadColor();

    EXPECT_EQ(color.getRProperty(), 10);
    EXPECT_EQ(color.getGProperty(), 20);
    EXPECT_EQ(color.getBProperty(), 30);
    EXPECT_EQ(color.getAProperty(), 40);
}

TEST_F(ContentReaderTest, RealMonoGameFixtureLoadsEndToEndThroughGenericDispatch)
{
    // Real, externally-produced fixture (MonoGame's own Tests/Assets/Textures/white-1.xnb,
    // vendored into tests/assets/xnb/monogame/windows/uncompressed/) -- proves the full
    // header + type-reader-table + root-object-dispatch + shared-resource-count pipeline
    // against genuine content, not just hand-built bytes. Uses a test-only reader that reads
    // just the Texture2DReader's leading SurfaceFormat/width/height/mipCount fields (a real
    // Texture2DReader is Phase C/XNB-23, out of scope here).
    struct RawTextureHeader { int32_t surfaceFormat = -1; int32_t width = -1; int32_t height = -1; int32_t mipLevelCount = -1; };

    class RawTextureHeaderReader : public ContentTypeReader<RawTextureHeader>
    {
    public:
        RawTextureHeaderReader() : ContentTypeReader<RawTextureHeader>("CNA.Test.RawTextureHeader") {}
    protected:
        RawTextureHeader Read(ContentReader& input, std::optional<RawTextureHeader> existingInstance) override
        {
            RawTextureHeader existing = existingInstance.value_or(RawTextureHeader{});
            existing.surfaceFormat = input.ReadInt32();
            existing.width = input.ReadInt32();
            existing.height = input.ReadInt32();
            existing.mipLevelCount = input.ReadInt32();
            return existing;
        }
    };

    ContentTypeReaderManager::AddTypeCreator(
        "Microsoft.Xna.Framework.Content.Texture2DReader",
        [] { return std::make_unique<RawTextureHeaderReader>(); });

    std::ifstream file(
        "tests/assets/xnb/monogame/windows/uncompressed/white-1.xnb", std::ios::binary);
    ASSERT_TRUE(file.is_open()) << "Real .xnb fixture not found relative to CWD";
    std::ostringstream ss;
    ss << file.rdbuf();
    const std::string fileBytes = ss.str();

    System::IO::MemoryStream headerStream(
        reinterpret_cast<const uint8_t*>(fileBytes.data()), (int32_t)fileBytes.size());
    System::IO::BinaryReader headerReader(&headerStream, true);
    const auto header = CNA::Internal::Xnb::ParseXnbHeader(headerReader, "white-1.xnb");
    EXPECT_EQ(header.platform, 'w');
    EXPECT_EQ(header.version, 5);
    EXPECT_EQ(header.compression, CNA::Internal::Xnb::XnbCompression::None);

    // ContentReader takes over immediately after the 10-byte container header.
    System::IO::MemoryStream bodyStream(
        reinterpret_cast<const uint8_t*>(fileBytes.data()) + 10, (int32_t)fileBytes.size() - 10);
    ContentReader reader(nullptr, &bodyStream, "white-1.xnb", header.version, header.platform);

    const RawTextureHeader result = reader.ReadAsset<RawTextureHeader>();

    EXPECT_EQ(result.surfaceFormat, 0); // SurfaceFormat.Color
    EXPECT_EQ(result.width, 1);
    EXPECT_EQ(result.height, 1);
    EXPECT_EQ(result.mipLevelCount, 1);
}

// plans/plan_xnb.md XNB-43: ReadBytesExactOrThrow() must reject a negative count, throw a clean
// EndOfStreamException instead of silently returning a short/mismatched buffer when the stream
// is truncated, and otherwise behave exactly like a normal full-length read.

TEST_F(ContentReaderTest, ReadBytesExactOrThrowReturnsExactlyTheRequestedBytesOnSuccess)
{
    System::IO::MemoryStream ms;
    System::IO::BinaryWriter writer(&ms, true);
    writer.Write((uint8_t)1); writer.Write((uint8_t)2); writer.Write((uint8_t)3);
    writer.Flush();
    auto buf = ms.ToArray();
    System::IO::MemoryStream body(buf.data(), (int32_t)buf.size());
    ContentReader reader(nullptr, &body, "test", 5, 'w');

    const auto result = reader.ReadBytesExactOrThrow(3, "TestReader");

    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0], 1);
    EXPECT_EQ(result[1], 2);
    EXPECT_EQ(result[2], 3);
}

TEST_F(ContentReaderTest, ReadBytesExactOrThrowThrowsEndOfStreamExceptionWhenStreamIsTruncated)
{
    System::IO::MemoryStream ms;
    System::IO::BinaryWriter writer(&ms, true);
    writer.Write((uint8_t)1); // only 1 byte available
    writer.Flush();
    auto buf = ms.ToArray();
    System::IO::MemoryStream body(buf.data(), (int32_t)buf.size());
    ContentReader reader(nullptr, &body, "test", 5, 'w');

    EXPECT_THROW(reader.ReadBytesExactOrThrow(10, "TestReader"), System::IO::EndOfStreamException);
}

TEST_F(ContentReaderTest, ReadBytesExactOrThrowRejectsANegativeCount)
{
    System::IO::MemoryStream ms;
    ContentReader reader(nullptr, &ms, "test", 5, 'w');

    EXPECT_THROW(reader.ReadBytesExactOrThrow(-1, "TestReader"), ContentLoadException);
}
