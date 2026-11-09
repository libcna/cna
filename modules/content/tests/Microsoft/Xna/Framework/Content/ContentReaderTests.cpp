// SPDX-License-Identifier: MS-PL
//
// XNB-15/XNB-16/XNB-16B: unit tests for ContentReader's root-object dispatch
// (1-based type-reader index), shared-resource fixups, and reader-version enforcement.

#include "CNA/TestSupport/EmptyServiceProvider.hpp"
#include <gtest/gtest.h>
#include <any>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "CNA/Internal/Xnb/XnbHeader.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/EndOfStreamException.hpp"
#include "System/IO/MemoryStream.hpp"
#include "System/NullReferenceException.hpp"

using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentReader;
using Microsoft::Xna::Framework::Content::ContentTypeReader;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;

namespace
{
    // Little-endian IEEE-754 binary32: 1, -2.5, 0.25, 8. Independent of BinaryWriter.
    constexpr std::array<uint8_t, 16> MathBytes{
        0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x20, 0xc0,
        0x00, 0x00, 0x80, 0x3e, 0x00, 0x00, 0x00, 0x41};

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

    // A self-referential node type, used to drive InnerReadObject<T>()'s own
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

    // The same self-referential shape as RecursiveNodeReader, but following its child through
    // the type-erased `object ReadObject<object>()` form -- the path the stock
    // Dictionary<string, object> reader and ModelReader's Tag fields take.
    class UntypedRecursiveNodeReader : public ContentTypeReader<std::shared_ptr<RecursiveNode>>
    {
    public:
        UntypedRecursiveNodeReader()
            : ContentTypeReader<std::shared_ptr<RecursiveNode>>("CNA.Test.UntypedRecursiveNode") {}

    protected:
        std::shared_ptr<RecursiveNode> Read(
            ContentReader& input, std::optional<std::shared_ptr<RecursiveNode>>) override
        {
            auto node = std::make_shared<RecursiveNode>();
            node->depth = input.ReadInt32();
            if (input.ReadInt32() != 0)
            {
                node->child = std::any_cast<std::shared_ptr<RecursiveNode>>(input.ReadObject());
            }
            return node;
        }
    };

    // Shared-resource ordering probe: every read and every fixup appends to one trace.
    std::vector<std::string>& OrderTrace()
    {
        static std::vector<std::string> trace;
        return trace;
    }

    class TracingPayloadReader : public ContentTypeReader<TestPayload>
    {
    public:
        TracingPayloadReader() : ContentTypeReader<TestPayload>("CNA.Test.TracingPayload") {}

    protected:
        TestPayload Read(ContentReader& input, std::optional<TestPayload>) override
        {
            TestPayload payload;
            payload.value = input.ReadInt32();
            OrderTrace().push_back("read " + std::to_string(payload.value));
            return payload;
        }
    };

    class TracingRootReader : public ContentTypeReader<std::shared_ptr<TestNodeResult>>
    {
    public:
        TracingRootReader() : ContentTypeReader<std::shared_ptr<TestNodeResult>>("CNA.Test.TracingRoot") {}

    protected:
        std::shared_ptr<TestNodeResult> Read(
            ContentReader& input, std::optional<std::shared_ptr<TestNodeResult>>) override
        {
            // Three references, in this order: resource #2 ("a"), resource #1 ("b"), #2 ("c").
            for (const char* label : {"a", "b", "c"})
            {
                input.ReadSharedResource<TestPayload>([label](TestPayload payload) {
                    OrderTrace().push_back(std::string("fixup ") + label + " " +
                                           std::to_string(payload.value));
                });
            }
            OrderTrace().push_back("root returned");
            return std::make_shared<TestNodeResult>();
        }
    };

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
            ContentTypeReaderManager::AddTypeCreator(
                "CNA.Test.UntypedRecursiveNodeReader",
                [] { return std::make_unique<UntypedRecursiveNodeReader>(); });
            ContentTypeReaderManager::AddTypeCreator(
                "CNA.Test.TracingPayloadReader", [] { return std::make_unique<TracingPayloadReader>(); });
            ContentTypeReaderManager::AddTypeCreator(
                "CNA.Test.TracingRootReader", [] { return std::make_unique<TracingRootReader>(); });
            OrderTrace().clear();
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

// InnerReadObject<T>() previously recursed with no depth limit whenever a type
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
    // Refresh ReadVector4 IL_0003-002f: little-endian Singles, X/Y/Z/W, exactly 16 bytes.
    // IEEE-754 literals 1,2,3,4 (no arithmetic/normalization); 0x5a is not part of the vector.
    const uint8_t bytes[]{0,0,0x80,0x3f, 0,0,0,0x40, 0,0,0x40,0x40, 0,0,0x80,0x40, 0x5a};
    System::IO::MemoryStream ms2(bytes, sizeof(bytes));
    ContentReader reader(nullptr, &ms2, "test", 5, 'w');

    const auto v = reader.ReadVector4();
    EXPECT_FLOAT_EQ(v.X, 1.0f);
    EXPECT_FLOAT_EQ(v.Y, 2.0f);
    EXPECT_FLOAT_EQ(v.Z, 3.0f);
    EXPECT_FLOAT_EQ(v.W, 4.0f);
    EXPECT_EQ(ms2.getPositionProperty(), 16);
    EXPECT_EQ(reader.ReadByte(), 0x5a);
}

TEST_F(ContentReaderTest, PropertiesPreserveOwnerAssetNameAndContainerMetadata)
{
    Microsoft::Xna::Framework::Content::ContentManager manager(CNA::TestSupport::EmptyServices(), "unused-root");
    System::IO::MemoryStream stream;
    ContentReader reader(&manager, &stream, "models/Ship", 4, 'x');
    EXPECT_EQ(reader.getContentManagerProperty(), &manager);
    EXPECT_EQ(reader.getAssetNameProperty(), "models/Ship");
    EXPECT_EQ(reader.getVersionProperty(), 4);
    EXPECT_EQ(reader.getPlatformProperty(), 'x');

    ContentReader detached(nullptr, &stream, "", 5, 'w');
    EXPECT_EQ(detached.getContentManagerProperty(), nullptr);
    EXPECT_TRUE(detached.getAssetNameProperty().empty());
    EXPECT_EQ(detached.getVersionProperty(), 5);
    EXPECT_EQ(detached.getPlatformProperty(), 'w');
    EXPECT_EQ(stream.getPositionProperty(), 0);
}

TEST_F(ContentReaderTest, ReadSingleDecodesLiteralIeeeBytesAndPreservesSignedZero)
{
    const uint8_t bytes[]{0x00, 0x00, 0x20, 0xc0, 0x00, 0x00, 0x00, 0x80, 0x5a};
    System::IO::MemoryStream stream(bytes, sizeof(bytes));
    ContentReader reader(nullptr, &stream, "single", 5, 'w');
    EXPECT_FLOAT_EQ(reader.ReadSingle(), -2.5f);
    const float zero = reader.ReadSingle();
    EXPECT_EQ(zero, 0.0f);
    EXPECT_TRUE(std::signbit(zero));
    EXPECT_EQ(stream.getPositionProperty(), 8);
    EXPECT_EQ(reader.ReadByte(), 0x5a);
}

TEST_F(ContentReaderTest, ReadDoubleDecodesLiteralIeeeBytesAndPreservesSignedZero)
{
    // binary64 -2.5 = 0xc004000000000000; negative zero = 0x8000000000000000.
    const uint8_t bytes[]{
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0xc0,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x5a};
    System::IO::MemoryStream stream(bytes, sizeof(bytes));
    ContentReader reader(nullptr, &stream, "double", 5, 'w');
    EXPECT_DOUBLE_EQ(reader.ReadDouble(), -2.5);
    const double zero = reader.ReadDouble();
    EXPECT_EQ(zero, 0.0);
    EXPECT_TRUE(std::signbit(zero));
    EXPECT_EQ(stream.getPositionProperty(), 16);
    EXPECT_EQ(reader.ReadByte(), 0x5a);
}

TEST_F(ContentReaderTest, ReadVector2ConsumesTwoLiteralComponents)
{
    System::IO::MemoryStream stream(MathBytes.data(), static_cast<int32_t>(MathBytes.size()));
    ContentReader reader(nullptr, &stream, "vector2", 5, 'w');
    const auto value = reader.ReadVector2();
    EXPECT_FLOAT_EQ(value.X, 1.0f);
    EXPECT_FLOAT_EQ(value.Y, -2.5f);
    EXPECT_EQ(stream.getPositionProperty(), 8);
    EXPECT_FLOAT_EQ(reader.ReadSingle(), 0.25f);
}

TEST_F(ContentReaderTest, ReadVector3ConsumesThreeLiteralComponents)
{
    System::IO::MemoryStream stream(MathBytes.data(), static_cast<int32_t>(MathBytes.size()));
    ContentReader reader(nullptr, &stream, "vector3", 5, 'w');
    const auto value = reader.ReadVector3();
    EXPECT_FLOAT_EQ(value.X, 1.0f);
    EXPECT_FLOAT_EQ(value.Y, -2.5f);
    EXPECT_FLOAT_EQ(value.Z, 0.25f);
    EXPECT_EQ(stream.getPositionProperty(), 12);
    EXPECT_FLOAT_EQ(reader.ReadSingle(), 8.0f);
}

TEST_F(ContentReaderTest, ReadQuaternionPreservesLiteralXyzwWithoutNormalizing)
{
    System::IO::MemoryStream stream(MathBytes.data(), static_cast<int32_t>(MathBytes.size()));
    ContentReader reader(nullptr, &stream, "quaternion", 5, 'w');
    const auto value = reader.ReadQuaternion();
    EXPECT_FLOAT_EQ(value.X, 1.0f);
    EXPECT_FLOAT_EQ(value.Y, -2.5f);
    EXPECT_FLOAT_EQ(value.Z, 0.25f);
    EXPECT_FLOAT_EQ(value.W, 8.0f);
    EXPECT_EQ(stream.getPositionProperty(), 16);
}

TEST_F(ContentReaderTest, ReadMatrixDecodesLiteralRowMajorFields)
{
    // binary32 integers 1 through 16, serialized M11..M14, M21..M24, M31..M34, M41..M44.
    const uint8_t bytes[]{
        0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x00, 0x40,
        0x00, 0x00, 0x40, 0x40, 0x00, 0x00, 0x80, 0x40,
        0x00, 0x00, 0xa0, 0x40, 0x00, 0x00, 0xc0, 0x40,
        0x00, 0x00, 0xe0, 0x40, 0x00, 0x00, 0x00, 0x41,
        0x00, 0x00, 0x10, 0x41, 0x00, 0x00, 0x20, 0x41,
        0x00, 0x00, 0x30, 0x41, 0x00, 0x00, 0x40, 0x41,
        0x00, 0x00, 0x50, 0x41, 0x00, 0x00, 0x60, 0x41,
        0x00, 0x00, 0x70, 0x41, 0x00, 0x00, 0x80, 0x41};
    System::IO::MemoryStream stream(bytes, sizeof(bytes));
    ContentReader reader(nullptr, &stream, "matrix", 5, 'w');
    const auto value = reader.ReadMatrix();
    EXPECT_FLOAT_EQ(value.M11, 1);  EXPECT_FLOAT_EQ(value.M12, 2);
    EXPECT_FLOAT_EQ(value.M13, 3);  EXPECT_FLOAT_EQ(value.M14, 4);
    EXPECT_FLOAT_EQ(value.M21, 5);  EXPECT_FLOAT_EQ(value.M22, 6);
    EXPECT_FLOAT_EQ(value.M23, 7);  EXPECT_FLOAT_EQ(value.M24, 8);
    EXPECT_FLOAT_EQ(value.M31, 9);  EXPECT_FLOAT_EQ(value.M32, 10);
    EXPECT_FLOAT_EQ(value.M33, 11); EXPECT_FLOAT_EQ(value.M34, 12);
    EXPECT_FLOAT_EQ(value.M41, 13); EXPECT_FLOAT_EQ(value.M42, 14);
    EXPECT_FLOAT_EQ(value.M43, 15); EXPECT_FLOAT_EQ(value.M44, 16);
    EXPECT_EQ(stream.getPositionProperty(), 64);
}

TEST_F(ContentReaderTest, ReadBoundingSphereConsumesCenterThenRadius)
{
    System::IO::MemoryStream stream(MathBytes.data(), static_cast<int32_t>(MathBytes.size()));
    ContentReader reader(nullptr, &stream, "sphere", 5, 'w');
    const auto value = reader.ReadBoundingSphere();
    EXPECT_FLOAT_EQ(value.Center.X, 1.0f);
    EXPECT_FLOAT_EQ(value.Center.Y, -2.5f);
    EXPECT_FLOAT_EQ(value.Center.Z, 0.25f);
    EXPECT_FLOAT_EQ(value.Radius, 8.0f);
    EXPECT_EQ(stream.getPositionProperty(), 16);
}

TEST_F(ContentReaderTest, CollectionCountLimitIsInclusiveAndDoesNotNarrowInt64)
{
    CNA::Internal::Xnb::XnbReadLimits limits;
    limits.maxCollectionElementCount = 3;
    System::IO::MemoryStream stream;
    ContentReader reader(nullptr, &stream, "counts", 5, 'w', nullptr, limits);
    EXPECT_NO_THROW(reader.CheckCollectionElementCount(0, "ListReader"));
    EXPECT_NO_THROW(reader.CheckCollectionElementCount(3, "ListReader"));
    for (const int64_t count : {-1LL, 4LL, 0x100000000LL, std::numeric_limits<long long>::max()})
    {
        SCOPED_TRACE(count);
        EXPECT_THROW(reader.CheckCollectionElementCount(count, "ListReader"), ContentLoadException);
    }
    EXPECT_EQ(stream.getPositionProperty(), 0);
}

TEST_F(ContentReaderTest, DecodedByteLimitIsInclusiveAndDoesNotNarrowInt64)
{
    CNA::Internal::Xnb::XnbReadLimits limits;
    limits.maxDecompressedSize = 16;
    System::IO::MemoryStream stream;
    ContentReader reader(nullptr, &stream, "sizes", 5, 'w', nullptr, limits);
    EXPECT_NO_THROW(reader.CheckDecodedByteSize(0, "TextureReader"));
    EXPECT_NO_THROW(reader.CheckDecodedByteSize(16, "TextureReader"));
    for (const int64_t size : {-1LL, 17LL, 0x100000000LL, std::numeric_limits<long long>::max()})
    {
        SCOPED_TRACE(size);
        EXPECT_THROW(reader.CheckDecodedByteSize(size, "TextureReader"), ContentLoadException);
    }
    EXPECT_EQ(stream.getPositionProperty(), 0);
}

TEST_F(ContentReaderTest, ReadColorReadsRgbaBytesInOrder)
{
    // Refresh ReadColor IL_000b-0010 reads packed UInt32 (AABBGGRR); the following byte belongs to the next field.
    std::vector<uint8_t> bytes{10, 20, 30, 40, 0x5a};
    System::IO::MemoryStream ms(bytes.data(), (int32_t)bytes.size());
    ContentReader reader(nullptr, &ms, "test", 5, 'w');

    const auto color = reader.ReadColor();

    EXPECT_EQ(color.getRProperty(), 10);
    EXPECT_EQ(color.getGProperty(), 20);
    EXPECT_EQ(color.getBProperty(), 30);
    EXPECT_EQ(color.getAProperty(), 40);
    EXPECT_EQ(ms.getPositionProperty(), 4);
    EXPECT_EQ(reader.ReadByte(), 0x5a);
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

// XNB-43: ReadBytesExactOrThrow() must reject a negative count, throw a clean
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

TEST_F(ContentReaderTest, Safe002SharedResourceDispatchCombinationsRefuseAndRecover)
{
    const auto make = [&](int32_t sharedCount, int32_t reference, int32_t resourceReader,
                          bool truncate) {
        return BuildBody({{"CNA.Test.TestNodeReader", 0}, {"CNA.Test.TestPayloadReader", 0}},
            sharedCount, [&](System::IO::BinaryWriter& writer) {
                writer.Write7BitEncodedInt(1);
                writer.Write7BitEncodedInt(reference);
                for (int32_t i = 0; i < sharedCount && i < 2; ++i)
                {
                    writer.Write7BitEncodedInt(resourceReader);
                    if (!truncate)
                    {
                        if (resourceReader == 1) { writer.Write7BitEncodedInt(0); }
                        else if (resourceReader == 2) { writer.Write(int32_t{777 + i}); }
                    }
                }
            });
    };
    const auto read = [](const std::vector<uint8_t>& bytes) {
        System::IO::MemoryStream stream(bytes.data(), static_cast<int32_t>(bytes.size()));
        CNA::Internal::Xnb::XnbReadLimits limits;
        limits.maxSharedResourceCount = 2;
        ContentReader reader(nullptr, &stream, "Safe002 shared", 5, 'w', nullptr, limits);
        return reader.ReadAsset<std::shared_ptr<TestNodeResult>>();
    };
    struct Mutation { int32_t count, reference, reader; bool truncate; };
    for (const Mutation& mutation : {
             Mutation{3, 1, 2, false}, {INT32_MAX, 1, 2, false}, {-1, 1, 2, false},
             {0, 1, 2, false}, {1, 2, 2, false}, {2, INT32_MAX, 2, false},
             {1, 1, 1, false}, {1, 1, 3, false}, {1, 1, 2, true}})
    {
        SCOPED_TRACE(::testing::Message() << mutation.count << "/" << mutation.reference
                                        << "/" << mutation.reader << "/" << mutation.truncate);
        const auto bytes = make(mutation.count, mutation.reference, mutation.reader, mutation.truncate);
        if (mutation.count <= 0)
        {
            // HND-TQ-16, Refresh ReadHeader IL_001b-001d / ReadSharedResource<T> IL_0039: no
            // fixup table, so the reference is a NullReferenceException.
            EXPECT_THROW((void)read(bytes), System::NullReferenceException);
        }
        else
        {
            EXPECT_THROW((void)read(bytes), ContentLoadException);
        }
        const auto valid = read(make(2, 2, 2, false));
        ASSERT_NE(nullptr, valid);
        EXPECT_TRUE(valid->fixupRan);
        EXPECT_EQ(778, valid->sharedValue);
    }
    const auto null = read(make(2, 0, 2, false));
    ASSERT_NE(nullptr, null);
    EXPECT_FALSE(null->fixupRan);
}

// TQ-004: the CNAEXT type-erased ReadAsset() is the root entry ContentManager's untyped external
// references use. Like XNA's ContentReader.ReadAsset<T> (IL_0000-IL_0010: type table, root object,
// then every shared resource), it must return the object the file's own root reader produced and
// must have run the shared-resource fixups before it returns.
TEST_F(ContentReaderTest, TypeErasedReadAssetReturnsTheRootReadersObjectAfterRunningFixups)
{
    const auto bytes = BuildBody(
        {{"CNA.Test.TestNodeReader", 0}, {"CNA.Test.TestPayloadReader", 0}}, 1,
        [](System::IO::BinaryWriter& w) {
            w.Write7BitEncodedInt(1); // root -> TestNodeReader
            w.Write7BitEncodedInt(1); // its shared-resource reference -> resource #1
            w.Write7BitEncodedInt(2); // resource #1 -> TestPayloadReader
            w.Write(int32_t{-31});
        });
    System::IO::MemoryStream ms(bytes.data(), static_cast<int32_t>(bytes.size()));
    ContentReader reader(nullptr, &ms, "erased", 5, 'w');

    const std::any root = reader.ReadAsset();

    const auto* node = std::any_cast<std::shared_ptr<TestNodeResult>>(&root);
    ASSERT_NE(node, nullptr) << root.type().name();
    ASSERT_NE(*node, nullptr);
    EXPECT_TRUE((*node)->fixupRan);
    EXPECT_EQ((*node)->sharedValue, -31);
    EXPECT_EQ(ms.getPositionProperty(), ms.getLengthProperty());
}

// TQ-004: a null root (index 0) is C#'s null, i.e. an empty std::any, and the shared-resource
// section that follows it is still consumed (XNA ReadAsset<T> reads it unconditionally).
TEST_F(ContentReaderTest, TypeErasedReadAssetOfANullRootIsEmptyAndStillReadsSharedResources)
{
    const auto bytes = BuildBody(
        {{"CNA.Test.TestPayloadReader", 0}}, 1,
        [](System::IO::BinaryWriter& w) {
            w.Write7BitEncodedInt(0); // null root
            w.Write7BitEncodedInt(1); // resource #1 -> TestPayloadReader
            w.Write(int32_t{5});
        });
    System::IO::MemoryStream ms(bytes.data(), static_cast<int32_t>(bytes.size()));
    ContentReader reader(nullptr, &ms, "erased-null", 5, 'w');

    const std::any root = reader.ReadAsset();

    EXPECT_FALSE(root.has_value());
    EXPECT_EQ(ms.getPositionProperty(), ms.getLengthProperty());
}

// TQ-004: XNA ReadAsset<T> catches IOException (EndOfStreamException included) and rethrows it as
// ContentLoadException (the catch handler after IL_0017); the type-erased entry must do the same.
TEST_F(ContentReaderTest, TypeErasedReadAssetWrapsATruncatedBodyAsContentLoadException)
{
    auto bytes = BuildBody(
        {{"CNA.Test.TestPayloadReader", 0}}, 0,
        [](System::IO::BinaryWriter& w) {
            w.Write7BitEncodedInt(1);
            w.Write(int32_t{0x01020304});
        });
    bytes.resize(bytes.size() - 2); // the root's Int32 is cut in half
    System::IO::MemoryStream ms(bytes.data(), static_cast<int32_t>(bytes.size()));
    ContentReader reader(nullptr, &ms, "erased-truncated", 5, 'w');

    EXPECT_THROW((void)reader.ReadAsset(), ContentLoadException);
}

// TQ-004 regression: XnbReadLimits::maxObjectNestingDepth bounds "the deepest nested-object graph
// this reader will follow". InnerReadObject<T> enforced it, but the type-erased
// `object ReadObject<object>()` form -- the one the stock Dictionary<string, object> reader and
// ModelReader's Tag fields use -- did not count nesting at all, so a file of nested dictionaries
// recursed without bound. Same limits, same expectations as the typed tests above.
TEST_F(ContentReaderTest, UntypedObjectNestingDepthExceedingLimitThrowsContentLoadException)
{
    CNA::Internal::Xnb::XnbReadLimits tightLimits;
    tightLimits.maxObjectNestingDepth = 3;
    const auto bytes = BuildBody(
        {{"CNA.Test.UntypedRecursiveNodeReader", 0}}, 0,
        [](System::IO::BinaryWriter& w) { WriteRecursiveNode(w, 4); }); // 5 nested dispatch calls

    {
        System::IO::MemoryStream ms(bytes.data(), static_cast<int32_t>(bytes.size()));
        ContentReader reader(nullptr, &ms, "untyped-deep", 5, 'w', nullptr, tightLimits);
        EXPECT_THROW((void)reader.ReadAsset<std::shared_ptr<RecursiveNode>>(), ContentLoadException);
    }
    {
        System::IO::MemoryStream ms(bytes.data(), static_cast<int32_t>(bytes.size()));
        ContentReader reader(nullptr, &ms, "untyped-deep-erased", 5, 'w', nullptr, tightLimits);
        EXPECT_THROW((void)reader.ReadAsset(), ContentLoadException);
    }
}

TEST_F(ContentReaderTest, UntypedObjectNestingDepthAtLimitDoesNotThrow)
{
    CNA::Internal::Xnb::XnbReadLimits tightLimits;
    tightLimits.maxObjectNestingDepth = 3;
    const auto bytes = BuildBody(
        {{"CNA.Test.UntypedRecursiveNodeReader", 0}}, 0,
        [](System::IO::BinaryWriter& w) { WriteRecursiveNode(w, 3); }); // 4 nested dispatch calls

    System::IO::MemoryStream ms(bytes.data(), static_cast<int32_t>(bytes.size()));
    ContentReader reader(nullptr, &ms, "untyped-at-limit", 5, 'w', nullptr, tightLimits);
    const std::any erased = reader.ReadAsset();
    const auto root = std::any_cast<std::shared_ptr<RecursiveNode>>(erased);
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->depth, 3);
    ASSERT_NE(root->child, nullptr);
    ASSERT_NE(root->child->child, nullptr);
    ASSERT_NE(root->child->child->child, nullptr);
    EXPECT_EQ(root->child->child->child->depth, 0);
    EXPECT_EQ(root->child->child->child->child, nullptr);
    EXPECT_EQ(ms.getPositionProperty(), ms.getLengthProperty());
}

// TQ-004 regression: XNA ContentReader.ReadSharedResource<T> (IL_0014-IL_0026) throws
// ArgumentNullException("fixup") for a null fixup before IL_0028 reads the index, whatever that
// index is; FNA's ContentReader.cs does the same. An empty std::function is that null.
TEST_F(ContentReaderTest, ReadSharedResourceRefusesAnEmptyFixupBeforeReadingTheIndex)
{
    for (const uint8_t index : {uint8_t{0}, uint8_t{1}})
    {
        SCOPED_TRACE(static_cast<int>(index));
        const auto bytes = BuildBody({}, 1, [index](System::IO::BinaryWriter& w) { w.Write(index); });
        System::IO::MemoryStream ms(bytes.data(), static_cast<int32_t>(bytes.size()));
        ContentReader reader(nullptr, &ms, "fixup", 5, 'w');
        reader.InitializeTypeReaders();
        const auto before = ms.getPositionProperty();

        EXPECT_THROW(reader.ReadSharedResource<TestPayload>(nullptr), System::ArgumentNullException);
        EXPECT_EQ(ms.getPositionProperty(), before);
    }
}

// TQ-004 regression: XNA ReadSharedResource<T> treats every nonzero index as a reference
// (IL_002f brtrue), subtracts one and compares it UNSIGNED (IL_0037 conv.u8) with the fixup table,
// so a negative 7-bit index (here 0xFFFFFFFF = -1) is refused with ContentLoadException (BadXnb,
// IL_0043-IL_0054) rather than silently ignored.
TEST_F(ContentReaderTest, ReadSharedResourceRefusesANegativeIndex)
{
    const auto bytes = BuildBody({}, 1, [](System::IO::BinaryWriter& w) {
        for (const uint8_t b : {0xff, 0xff, 0xff, 0xff, 0x0f}) w.Write(b);
    });
    System::IO::MemoryStream ms(bytes.data(), static_cast<int32_t>(bytes.size()));
    ContentReader reader(nullptr, &ms, "negative-shared", 5, 'w');
    reader.InitializeTypeReaders();
    bool ran = false;

    EXPECT_THROW(reader.ReadSharedResource<TestPayload>([&ran](TestPayload) { ran = true; }),
                 ContentLoadException);
    EXPECT_FALSE(ran);
}

// TQ-004: XNA ContentReader.ReadSharedResources(int) first reads EVERY shared resource
// (IL_000b-IL_001e), then walks the resources in index order, invoking each one's fixups in the
// order they were registered (IL_0020-IL_0065). The root reader registers references to
// resources #2, #1, #2 (fixups "a", "b", "c").
TEST_F(ContentReaderTest, SharedResourceFixupsRunAfterAllResourcesInIndexThenRegistrationOrder)
{
    const auto bytes = BuildBody(
        {{"CNA.Test.TracingRootReader", 0}, {"CNA.Test.TracingPayloadReader", 0}}, 2,
        [](System::IO::BinaryWriter& w) {
            w.Write7BitEncodedInt(1); // root -> TracingRootReader
            w.Write7BitEncodedInt(2); // fixup a -> resource #2
            w.Write7BitEncodedInt(1); // fixup b -> resource #1
            w.Write7BitEncodedInt(2); // fixup c -> resource #2
            w.Write7BitEncodedInt(2); // resource #1 -> TracingPayloadReader
            w.Write(int32_t{10});
            w.Write7BitEncodedInt(2); // resource #2 -> TracingPayloadReader
            w.Write(int32_t{20});
        });
    System::IO::MemoryStream ms(bytes.data(), static_cast<int32_t>(bytes.size()));
    ContentReader reader(nullptr, &ms, "fixup-order", 5, 'w');

    const auto root = reader.ReadAsset<std::shared_ptr<TestNodeResult>>();

    ASSERT_NE(root, nullptr);
    EXPECT_EQ(OrderTrace(), (std::vector<std::string>{
                                "root returned", "read 10", "read 20",
                                "fixup b 10", "fixup a 20", "fixup c 20"}));
    EXPECT_EQ(ms.getPositionProperty(), ms.getLengthProperty());
}
