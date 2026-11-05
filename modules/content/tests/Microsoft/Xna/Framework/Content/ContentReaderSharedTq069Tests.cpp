// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"

using namespace Microsoft::Xna::Framework::Content;
namespace
{
    struct SharedItem { int value; };
    using Ref = std::shared_ptr<SharedItem>;
    class SharedReader final : public ContentTypeReader<Ref>
    {
    public:
        SharedReader() : ContentTypeReader<Ref>("Tq069.SharedItem") {}
    protected:
        Ref Read(ContentReader& input, std::optional<Ref>) override
        {
            const auto value = input.ReadByte();
            return value == 0 ? Ref{} : std::make_shared<SharedItem>(SharedItem{value});
        }
    };
    class NumberReader final : public ContentTypeReader<int32_t>
    {
    public:
        NumberReader() : ContentTypeReader<int32_t>("System.Int32") {}
    protected:
        int32_t Read(ContentReader& input, std::optional<int32_t>) override { return input.ReadInt32(); }
    };
    template<class F> std::vector<uint8_t> Body(int shared, F write)
    {
        System::IO::MemoryStream stream;
        System::IO::BinaryWriter writer(&stream, true);
        writer.Write7BitEncodedInt(2);
        writer.Write(std::string("Tq069.SharedReader")); writer.Write(int32_t{0});
        writer.Write(std::string("Tq069.NumberReader")); writer.Write(int32_t{0});
        writer.Write7BitEncodedInt(shared); write(writer); writer.Flush(); return stream.ToArray();
    }
    template<class F> void ExpectBadXnb(F read)
    {
        try { read(); FAIL() << "expected BadXnb refusal"; }
        catch (const ContentLoadException& error)
        { EXPECT_EQ(std::string(error.what()), "Error loading \"shared-api\"."); }
    }
    class ContentReaderSharedTq069Test : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            ContentTypeReaderManager::ClearTargetTypeAssociationsEXT();
            ContentTypeReaderManager::AddTypeCreator("Tq069.SharedReader",
                [] { return std::make_unique<SharedReader>(); });
            ContentTypeReaderManager::AddTypeCreator("Tq069.NumberReader",
                [] { return std::make_unique<NumberReader>(); });
        }
        void TearDown() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            ContentTypeReaderManager::ClearTargetTypeAssociationsEXT();
        }
    };

    TEST_F(ContentReaderSharedTq069Test, ReferencedNullOrWrongTypeRefusesOnlyAfterEveryResourceIsRead)
    {
        // Refresh ReadSharedResources IL_000b-001e reads all resources before callbacks;
        // <ReadSharedResource>b__0 IL_0000-001e uses isinst: both null forms fail, as does Int32.
        for (int kind : {0, 1, 2})
        {
            SCOPED_TRACE(kind);
            const auto bytes = Body(2, [&](auto& w) {
                w.Write7BitEncodedInt(0); // null primary object
                w.Write7BitEncodedInt(1); w.Write7BitEncodedInt(1); w.Write7BitEncodedInt(2);
                if (kind == 0) w.Write7BitEncodedInt(0); // null serialized shared object
                if (kind == 1) { w.Write7BitEncodedInt(1); w.Write(uint8_t{0}); } // typed null result
                if (kind == 2) { w.Write7BitEncodedInt(2); w.Write(int32_t{73}); }
                w.Write7BitEncodedInt(1); w.Write(uint8_t{9}); // valid later resource
                w.Write(uint8_t{0x5a});
            });
            System::IO::MemoryStream stream(bytes.data(), static_cast<int32_t>(bytes.size()));
            ContentReader input(nullptr, &stream, "shared-api", 5, 'w');
            input.InitializeTypeReaders();
            EXPECT_EQ(input.ReadObject<Ref>(), nullptr);
            std::vector<int> calls;
            input.ReadSharedResource<Ref>([&](Ref) { calls.push_back(1); });
            input.ReadSharedResource<Ref>([&](Ref) { calls.push_back(2); });
            input.ReadSharedResource<Ref>([&](Ref) { calls.push_back(3); });
            EXPECT_TRUE(calls.empty());
            ExpectBadXnb([&] { input.ReadSharedResources(); });
            EXPECT_TRUE(calls.empty()); // invalid callback, its sibling and later index never execute
            EXPECT_EQ(stream.getPositionProperty(), static_cast<int64_t>(bytes.size() - 1));
            EXPECT_EQ(input.ReadByte(), 0x5a);
        }

        // Refresh ReadSharedResource<T> IL_002e-0031 ignores zero for any T; the closure's
        // IL_0000-001e refuses an unrelated Int32. Native move-only T must still instantiate.
        struct MoveOnly
        {
            explicit MoveOnly(int) {}
            MoveOnly(const MoveOnly&) = delete;
            MoveOnly(MoveOnly&&) = default;
        };
        for (int index : {0, 1})
        {
            SCOPED_TRACE(index);
            const auto bytes = Body(1, [&](auto& w) {
                w.Write7BitEncodedInt(index);
                w.Write7BitEncodedInt(2); w.Write(int32_t{73});
                w.Write(uint8_t{0x5a});
            });
            System::IO::MemoryStream stream(bytes.data(), static_cast<int32_t>(bytes.size()));
            ContentReader input(nullptr, &stream, "shared-api", 5, 'w');
            input.InitializeTypeReaders();
            int calls = 0;
            input.ReadSharedResource<MoveOnly>([&](MoveOnly) { ++calls; });
            EXPECT_EQ(calls, 0);
            if (index) ExpectBadXnb([&] { input.ReadSharedResources(); });
            else input.ReadSharedResources();
            EXPECT_EQ(calls, 0);
            EXPECT_EQ(input.ReadByte(), 0x5a);
        }
    }

    TEST_F(ContentReaderSharedTq069Test, FirstInvalidResourceKeepsEarlierFixupsAndStopsLaterFixups)
    {
        // Refresh ReadSharedResources IL_0024-005e invokes directly, without catch/continue.
        const auto bytes = Body(3, [](auto& w) {
            for (int index : {1, 2, 2, 3}) w.Write7BitEncodedInt(index);
            w.Write7BitEncodedInt(1); w.Write(uint8_t{3});
            w.Write7BitEncodedInt(0);
            w.Write7BitEncodedInt(1); w.Write(uint8_t{9});
            w.Write(uint8_t{0x5a});
        });
        System::IO::MemoryStream stream(bytes.data(), static_cast<int32_t>(bytes.size()));
        ContentReader input(nullptr, &stream, "shared-api", 5, 'w');
        input.InitializeTypeReaders();
        std::vector<int> calls;
        for (int i : {0, 1, 2, 3})
            input.ReadSharedResource<Ref>([&, i](Ref item) { calls.push_back(10 * i + item->value); });
        EXPECT_TRUE(calls.empty());
        ExpectBadXnb([&] { input.ReadSharedResources(); });
        EXPECT_EQ(calls, (std::vector<int>{3}));
        EXPECT_EQ(input.ReadByte(), 0x5a);
    }

    TEST_F(ContentReaderSharedTq069Test, BadObjectAndSharedIndicesUseBadXnbWithoutReadingThePayload)
    {
        // Refresh ReadObjectInternal IL_0014-0036 and ReadSharedResource IL_0032-0054:
        // unsigned (index - 1) range check; both paths use BadXnb, including negative indices.
        for (bool shared : {false, true}) for (bool erased : {false, true})
        for (int32_t index : {int32_t{-1}, int32_t{3}})
        {
            SCOPED_TRACE(shared);
            SCOPED_TRACE(erased);
            SCOPED_TRACE(index);
            const auto bytes = Body(2, [&](auto& w) {
                w.Write7BitEncodedInt(index); w.Write(uint8_t{0x5a});
            });
            System::IO::MemoryStream stream(bytes.data(), static_cast<int32_t>(bytes.size()));
            ContentReader input(nullptr, &stream, "shared-api", 5, 'w');
            input.InitializeTypeReaders();
            int calls = 0;
            ExpectBadXnb([&] {
                if (shared) input.ReadSharedResource<Ref>([&](Ref) { ++calls; });
                else if (erased) (void)input.ReadObject();
                else (void)input.ReadObject<Ref>();
            });
            EXPECT_EQ(calls, 0);
            EXPECT_EQ(stream.getPositionProperty(), static_cast<int64_t>(bytes.size() - 1));
            EXPECT_EQ(input.ReadByte(), 0x5a);
        }
    }
}
