// SPDX-License-Identifier: MS-PL
// Native counterpart of ContentValueDispatchOracle.cs, using the existing ValueType base.
#include <gtest/gtest.h>
#include <any>
#include <optional>
#include <vector>
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ValueType.hpp"

using namespace Microsoft::Xna::Framework::Content;

namespace
{
    struct DispatchValue final : System::ValueType, System::IDisposable
    {
        int number = -1;
        DispatchValue() = default;
        explicit DispatchValue(int value) : number(value) { }
        void Dispose() override { }
    };

    class DispatchValueReader final : public ContentTypeReader<DispatchValue>
    {
    public:
        DispatchValueReader() : ContentTypeReader<DispatchValue>("CNA.Repair.DispatchValue") { }
    protected:
        DispatchValue Read(ContentReader& input, std::optional<DispatchValue>) override
        { return DispatchValue(input.ReadInt32()); }
    };

    class ContentValueDispatchCompatibilityTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            Clear();
            ContentTypeReaderManager::AddTypeCreator("CNA.Repair.DispatchValue",
                [] { return std::make_unique<DispatchValueReader>(); });
        }
        void TearDown() override { Clear(); }
        static void Clear()
        {
            ContentTypeReaderManager::ClearTypeCreators();
            ContentTypeReaderManager::ClearTargetTypeAssociationsEXT();
        }
        static DispatchValue Read(ContentReader& input, int mode)
        {
            DispatchValueReader explicitReader;
            const DispatchValue existing(-1);
            switch (mode)
            {
            case 0: return input.ReadObject<DispatchValue>();
            case 1: return input.ReadObject<DispatchValue>(existing);
            case 2: return input.ReadObject<DispatchValue>(explicitReader);
            case 3: return input.ReadObject<DispatchValue>(explicitReader, existing);
            case 4: return input.ReadRawObject<DispatchValue>();
            case 5: return input.ReadRawObject<DispatchValue>(existing);
            case 6: return input.ReadRawObject<DispatchValue>(explicitReader);
            case 7: return input.ReadRawObject<DispatchValue>(explicitReader, existing);
            case 8: return std::any_cast<DispatchValue>(input.ReadObject());
            default: throw System::InvalidOperationException("Unknown value dispatch mode.");
            }
        }
        static void CheckMode(int mode)
        {
            System::IO::MemoryStream storage;
            System::IO::BinaryWriter writer(&storage, true);
            writer.Write7BitEncodedInt(1);
            writer.Write(std::string("CNA.Repair.DispatchValue"));
            writer.Write(int32_t{0});
            writer.Write7BitEncodedInt(0);
            if (mode == 0 || mode == 1 || mode == 8) writer.Write7BitEncodedInt(1);
            writer.Write(int32_t{73});
            writer.Flush();
            const auto bytes = storage.ToArray();
            System::IO::MemoryStream stream(bytes.data(), static_cast<int32_t>(bytes.size()));
            int recorded = 0;
            ContentReader input(nullptr, &stream, "value-dispatch", 5, 'w', [&](auto) { ++recorded; });
            input.InitializeTypeReaders();
            if (mode == 1 || mode == 3 || mode == 5 || mode == 7)
            {
                EXPECT_THROW((void)Read(input, mode), System::InvalidOperationException);
            }
            else
            {
                EXPECT_EQ(Read(input, mode).number, 73);
            }
            EXPECT_EQ(recorded, 0);
            EXPECT_EQ(stream.getPositionProperty(), stream.getLengthProperty());
        }
    };

TEST_F(ContentValueDispatchCompatibilityTest, IndexedNew) { CheckMode(0); }
TEST_F(ContentValueDispatchCompatibilityTest, IndexedExistingValueIsRejected) { CheckMode(1); }
TEST_F(ContentValueDispatchCompatibilityTest, ExplicitNewDoesNotConsumeIndex) { CheckMode(2); }
TEST_F(ContentValueDispatchCompatibilityTest, ExplicitExistingValueIsRejected) { CheckMode(3); }
TEST_F(ContentValueDispatchCompatibilityTest, RawInferredNew) { CheckMode(4); }
TEST_F(ContentValueDispatchCompatibilityTest, RawInferredExistingValueIsRejected) { CheckMode(5); }
TEST_F(ContentValueDispatchCompatibilityTest, RawExplicitNew) { CheckMode(6); }
TEST_F(ContentValueDispatchCompatibilityTest, RawExplicitExistingValueIsRejected) { CheckMode(7); }
TEST_F(ContentValueDispatchCompatibilityTest, ErasedValueIsNotRecorded) { CheckMode(8); }
}
