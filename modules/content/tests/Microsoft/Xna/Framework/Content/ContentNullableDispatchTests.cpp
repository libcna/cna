// SPDX-License-Identifier: MS-PL
// Genuine nullable boxing controls from ContentValueDispatchOracle.cs.
#include <gtest/gtest.h>
#include <optional>
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"
#include "System/ValueType.hpp"

using namespace Microsoft::Xna::Framework::Content;
namespace
{
    struct NullablePayload final : System::ValueType, System::IDisposable
    {
        int number = -1;
        NullablePayload() = default;
        explicit NullablePayload(int value) : number(value) { }
        void Dispose() override { }
    };
    using Payload = std::optional<NullablePayload>;
    class NullablePayloadReader final : public ContentTypeReader<Payload>
    {
    public:
        NullablePayloadReader() : ContentTypeReader<Payload>("CNA.Repair.NullablePayload") { }
    protected:
        Payload Read(ContentReader& reader, std::optional<Payload>) override
        { return NullablePayload(reader.ReadInt32()); }
    };
    class ContentNullableDispatchCompatibilityTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            Clear();
            ContentTypeReaderManager::AddTypeCreator("CNA.Repair.NullablePayload",
                [] { return std::make_unique<NullablePayloadReader>(); });
        }
        void TearDown() override { Clear(); }
        static void Clear()
        {
            ContentTypeReaderManager::ClearTypeCreators();
            ContentTypeReaderManager::ClearTargetTypeAssociationsEXT();
        }
        static void Check(int mode, bool hasValue)
        {
            System::IO::MemoryStream storage;
            System::IO::BinaryWriter writer(&storage, true);
            writer.Write7BitEncodedInt(1);
            writer.Write(std::string("CNA.Repair.NullablePayload"));
            writer.Write(int32_t{0});
            writer.Write7BitEncodedInt(0);
            if (mode == 0) writer.Write7BitEncodedInt(1);
            writer.Write(int32_t{73});
            writer.Flush();
            const auto bytes = storage.ToArray();
            System::IO::MemoryStream stream(bytes.data(), static_cast<int32_t>(bytes.size()));
            int recorded = 0;
            ContentReader input(nullptr, &stream, "nullable-dispatch", 5, 'w', [&](auto) { ++recorded; });
            input.InitializeTypeReaders();
            NullablePayloadReader reader;
            const Payload existing = hasValue ? Payload(NullablePayload(-1)) : std::nullopt;
            auto read = [&]() -> Payload
            {
                switch (mode)
                {
                case 0: return input.ReadObject<Payload>(existing);
                case 1: return input.ReadObject<Payload>(reader, existing);
                case 2: return input.ReadRawObject<Payload>(existing);
                case 3: return input.ReadRawObject<Payload>(reader, existing);
                default: throw System::InvalidOperationException("Unknown nullable mode");
                }
            };
            if (hasValue)
            {
                EXPECT_THROW((void)read(), System::InvalidOperationException);
                EXPECT_EQ(existing->number, -1);
            }
            else
            {
                const auto result = read();
                ASSERT_TRUE(result.has_value());
                EXPECT_EQ(result->number, 73);
                EXPECT_FALSE(existing.has_value());
            }
            EXPECT_EQ(recorded, 0);
            EXPECT_EQ(stream.getPositionProperty(), stream.getLengthProperty());
        }
    };
TEST_F(ContentNullableDispatchCompatibilityTest, IndexedNullExisting) { Check(0, false); }
TEST_F(ContentNullableDispatchCompatibilityTest, ExplicitNullExisting) { Check(1, false); }
TEST_F(ContentNullableDispatchCompatibilityTest, RawInferredNullExisting) { Check(2, false); }
TEST_F(ContentNullableDispatchCompatibilityTest, RawExplicitNullExisting) { Check(3, false); }
TEST_F(ContentNullableDispatchCompatibilityTest, IndexedExistingValueRejected) { Check(0, true); }
TEST_F(ContentNullableDispatchCompatibilityTest, ExplicitExistingValueRejected) { Check(1, true); }
TEST_F(ContentNullableDispatchCompatibilityTest, RawInferredExistingValueRejected) { Check(2, true); }
TEST_F(ContentNullableDispatchCompatibilityTest, RawExplicitExistingValueRejected) { Check(3, true); }
}
