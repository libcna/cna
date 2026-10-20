// SPDX-License-Identifier: MS-PL
// Regression for the genuine XNA4 ContentDispatchOracle.cs contract (cna-demos).
#include <gtest/gtest.h>
#include <any>
#include <memory>
#include <optional>
#include <vector>
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"
#include "System/InvalidOperationException.hpp"

using namespace Microsoft::Xna::Framework::Content;

namespace
{
    struct DispatchItem final : System::IDisposable
    {
        int value = -1;
        void Dispose() override { }
    };
    using Item = std::shared_ptr<DispatchItem>;

    class DispatchItemReader final : public ContentTypeReader<Item>
    {
    public:
        // Matching factory/target names isolate dispatch from the separate ISSUE-043.
        DispatchItemReader() : ContentTypeReader<Item>("CNA.Repair.DispatchItem") { }
        static inline bool replaceExisting = false;
        static inline bool returnNull = false;
    protected:
        Item Read(ContentReader& input, std::optional<Item> existing) override
        {
            const auto value = input.ReadInt32();
            if (returnNull) return {};
            Item result = existing && *existing && !replaceExisting
                ? *existing : std::make_shared<DispatchItem>();
            result->value = value;
            return result;
        }
    };

    class ContentDispatchCompatibilityTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            ContentTypeReaderManager::ClearTargetTypeAssociationsEXT();
            ContentTypeReaderManager::AddTypeCreator("CNA.Repair.DispatchItem",
                [] { return std::make_unique<DispatchItemReader>(); });
            DispatchItemReader::replaceExisting = false;
            DispatchItemReader::returnNull = false;
        }
        void TearDown() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            ContentTypeReaderManager::ClearTargetTypeAssociationsEXT();
        }
        static bool HasExisting(int mode)
        {
            return mode == 1 || mode == 4 || mode == 5 || mode == 8;
        }
        static std::vector<uint8_t> Body(int mode, bool shared = false)
        {
            System::IO::MemoryStream stream;
            System::IO::BinaryWriter writer(&stream, true);
            writer.Write7BitEncodedInt(1);
            writer.Write(std::string("CNA.Repair.DispatchItem"));
            writer.Write(int32_t{0});
            writer.Write7BitEncodedInt(shared ? 1 : 0);
            if (shared) writer.Write7BitEncodedInt(0); // Null root, one new shared object.
            if (mode < 2 || mode >= 6) writer.Write7BitEncodedInt(mode == 9 ? 0 : 1);
            if (mode != 9) writer.Write(int32_t{73});
            writer.Flush();
            const auto bytes = stream.ToArray();
            return {bytes.begin(), bytes.end()};
        }
        static Item Read(ContentReader& input, int mode, const Item& existing)
        {
            DispatchItemReader explicitReader;
            switch (mode)
            {
            case 0: return input.ReadObject<Item>();
            case 1: return input.ReadObject<Item>(existing);
            case 2: return input.ReadRawObject<Item>();
            case 3: return input.ReadRawObject<Item>(explicitReader);
            case 4: return input.ReadRawObject<Item>(existing);
            case 5: return input.ReadRawObject<Item>(explicitReader, existing);
            case 6: return std::any_cast<Item>(input.ReadObject());
            case 7: return input.ReadObject<Item>(explicitReader);
            case 8: return input.ReadObject<Item>(explicitReader, existing);
            case 9: return input.ReadObject<Item>(existing);
            default: throw System::InvalidOperationException("Unknown dispatch mode.");
            }
        }
        static void CheckMode(int mode)
        {
            SCOPED_TRACE(mode);
            const auto bytes = Body(mode);
            System::IO::MemoryStream stream(bytes.data(), static_cast<int32_t>(bytes.size()));
            std::vector<std::shared_ptr<System::IDisposable>> recorded;
            ContentReader input(nullptr, &stream, "dispatch", 5, 'w',
                [&](auto value) { recorded.push_back(std::move(value)); });
            input.InitializeTypeReaders();
            const auto existing = std::make_shared<DispatchItem>();
            const Item result = Read(input, mode, existing);
            ASSERT_NE(result, nullptr);
            EXPECT_EQ(result->value, 73);
            EXPECT_EQ(result == existing, HasExisting(mode));
            if (HasExisting(mode)) EXPECT_TRUE(recorded.empty());
            else
            {
                ASSERT_EQ(recorded.size(), 1U);
                EXPECT_EQ(recorded.front().get(), result.get());
            }
            EXPECT_EQ(stream.getPositionProperty(), stream.getLengthProperty());
        }
        static void CheckReplacement(int mode)
        {
            const auto bytes = Body(mode);
            System::IO::MemoryStream stream(bytes.data(), static_cast<int32_t>(bytes.size()));
            int recorded = 0;
            ContentReader input(nullptr, &stream, "replacement", 5, 'w',
                [&](auto) { ++recorded; });
            input.InitializeTypeReaders();
            DispatchItemReader::replaceExisting = true;
            EXPECT_THROW((void)Read(input, mode, std::make_shared<DispatchItem>()),
                         System::InvalidOperationException);
            EXPECT_EQ(recorded, 0);
        }
    };
TEST_F(ContentDispatchCompatibilityTest, TypedNew) { CheckMode(0); }
TEST_F(ContentDispatchCompatibilityTest, TypedExisting) { CheckMode(1); }
TEST_F(ContentDispatchCompatibilityTest, RawInferredNew) { CheckMode(2); }
TEST_F(ContentDispatchCompatibilityTest, RawExplicitNew) { CheckMode(3); }
TEST_F(ContentDispatchCompatibilityTest, RawInferredExisting) { CheckMode(4); }
TEST_F(ContentDispatchCompatibilityTest, RawExplicitExisting) { CheckMode(5); }
TEST_F(ContentDispatchCompatibilityTest, ErasedNew) { CheckMode(6); }
TEST_F(ContentDispatchCompatibilityTest, ExplicitNewConsumesDispatch) { CheckMode(7); }
TEST_F(ContentDispatchCompatibilityTest, ExplicitExistingConsumesDispatch) { CheckMode(8); }
TEST_F(ContentDispatchCompatibilityTest, TypedReplacementRejected) { CheckReplacement(1); }
TEST_F(ContentDispatchCompatibilityTest, RawInferredReplacementRejected) { CheckReplacement(4); }
TEST_F(ContentDispatchCompatibilityTest, RawExplicitReplacementRejected) { CheckReplacement(5); }
TEST_F(ContentDispatchCompatibilityTest, ExplicitReplacementRejected) { CheckReplacement(8); }

TEST_F(ContentDispatchCompatibilityTest, NullTypeIndexDoesNotReturnExistingInstance)
{
    const auto bytes = Body(9);
    System::IO::MemoryStream stream(bytes.data(), static_cast<int32_t>(bytes.size()));
    int recorded = 0;
    ContentReader input(nullptr, &stream, "null-index", 5, 'w', [&](auto) { ++recorded; });
    input.InitializeTypeReaders();
    EXPECT_EQ(Read(input, 9, std::make_shared<DispatchItem>()), nullptr);
    EXPECT_EQ(recorded, 0);
}

TEST_F(ContentDispatchCompatibilityTest, NullReaderResultIsNotRecorded)
{
    const auto bytes = Body(0);
    System::IO::MemoryStream stream(bytes.data(), static_cast<int32_t>(bytes.size()));
    int recorded = 0;
    ContentReader input(nullptr, &stream, "null-reader", 5, 'w', [&](auto) { ++recorded; });
    input.InitializeTypeReaders();
    DispatchItemReader::returnNull = true;
    EXPECT_EQ(Read(input, 0, {}), nullptr);
    EXPECT_EQ(recorded, 0);
}

TEST_F(ContentDispatchCompatibilityTest, ErasedSharedResourceIsRecorded)
{
    const auto bytes = Body(0, true);
    System::IO::MemoryStream stream(bytes.data(), static_cast<int32_t>(bytes.size()));
    std::vector<std::shared_ptr<System::IDisposable>> recorded;
    ContentReader input(nullptr, &stream, "shared", 5, 'w',
        [&](auto value) { recorded.push_back(std::move(value)); });
    EXPECT_EQ(input.ReadAsset<Item>(), nullptr);
    ASSERT_EQ(recorded.size(), 1U);
    auto item = std::dynamic_pointer_cast<DispatchItem>(recorded.front());
    ASSERT_NE(item, nullptr);
    EXPECT_EQ(item->value, 73);
}

TEST_F(ContentDispatchCompatibilityTest, RecorderExceptionRemainsUnwrapped)
{
    const auto bytes = Body(0);
    System::IO::MemoryStream stream(bytes.data(), static_cast<int32_t>(bytes.size()));
    ContentReader input(nullptr, &stream, "throwing-recorder", 5, 'w',
        [](auto) { throw System::InvalidOperationException("recorder failure"); });
    EXPECT_THROW((void)input.ReadAsset<Item>(), System::InvalidOperationException);
}

TEST_F(ContentDispatchCompatibilityTest, NullExistingIsNewInEveryExistingInstanceOverload)
{
    for (const int mode : {1, 4, 5, 8})
    {
        SCOPED_TRACE(mode);
        const auto bytes = Body(mode);
        System::IO::MemoryStream stream(bytes.data(), static_cast<int32_t>(bytes.size()));
        std::vector<std::shared_ptr<System::IDisposable>> recorded;
        ContentReader input(nullptr, &stream, "null-existing", 5, 'w',
            [&](auto value) { recorded.push_back(std::move(value)); });
        input.InitializeTypeReaders();
        const auto result = Read(input, mode, {});
        ASSERT_NE(result, nullptr);
        EXPECT_EQ(result->value, 73);
        EXPECT_EQ(recorded.size(), 1U);
        if (recorded.size() == 1U)
        {
            EXPECT_EQ(recorded.front().get(), result.get());
        }
        EXPECT_EQ(stream.getPositionProperty(), stream.getLengthProperty());
    }
}

TEST_F(ContentDispatchCompatibilityTest, RequestedTypeIsCheckedBeforeRecordingOwnership)
{
    for (const int mode : {0, 3, 7})
    {
        SCOPED_TRACE(mode);
        const auto bytes = Body(mode);
        System::IO::MemoryStream stream(bytes.data(), static_cast<int32_t>(bytes.size()));
        int recorded = 0;
        ContentReader input(nullptr, &stream, "wrong-requested-type", 5, 'w',
            [&](auto) { ++recorded; });
        input.InitializeTypeReaders();
        DispatchItemReader explicitReader;
        if (mode == 0)
        {
            EXPECT_THROW((void)input.ReadObject<std::string>(), ContentLoadException);
        }
        else if (mode == 3)
        {
            EXPECT_THROW((void)input.ReadRawObject<std::string>(explicitReader), ContentLoadException);
        }
        else
        {
            EXPECT_THROW((void)input.ReadObject<std::string>(explicitReader), ContentLoadException);
        }
        EXPECT_EQ(recorded, 0);
        EXPECT_EQ(stream.getPositionProperty(), stream.getLengthProperty());
    }
}
}
