// SPDX-License-Identifier: MS-PL
//
// XNB-21/XNB-22: unit tests for the generic collection .xnb readers (ArrayReader<T>,
// ListReader<T>, DictionaryReader<TKey,TValue>, NullableReader<T>), each instantiated with a
// representative element type and registered element reader, matching how a real consumer would
// use them.

#include <any>
#include <cstdint>
#include <map>
#include <optional>
#include <unordered_map>
#include <string>
#include <gtest/gtest.h>

#include "CNA/Internal/Xnb/CollectionContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/DecimalDateTimeContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/EffectMaterialContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/MathContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/PrimitiveContentTypeReaders.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/OverflowException.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"

using namespace Microsoft::Xna::Framework;
using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentReader;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;

namespace
{
    class CollectionReaderTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            CNA::Internal::Xnb::RegisterPrimitiveXnbReaders();
            CNA::Internal::Xnb::RegisterMathXnbReaders();
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }

        template <typename F>
        std::unique_ptr<ContentReader> MakeReader(F&& write)
        {
            auto ms = std::make_unique<System::IO::MemoryStream>();
            System::IO::BinaryWriter writer(ms.get(), true);
            write(writer);
            writer.Flush();
            auto buf = ms->ToArray();
            streams_.push_back(std::make_unique<System::IO::MemoryStream>(buf.data(), (int32_t)buf.size()));
            return std::make_unique<ContentReader>(nullptr, streams_.back().get(), "test", 5, 'w');
        }

    private:
        std::vector<std::unique_ptr<System::IO::MemoryStream>> streams_;
    };
}

TEST_F(CollectionReaderTest, ArrayReaderReadsElementsViaExplicitElementReader)
{
    CNA::Internal::Xnb::ArrayReader<int32_t> reader(
        "System.Int32[]", "Microsoft.Xna.Framework.Content.Int32Reader");

    auto contentReader = MakeReader([](auto& w) {
        w.Write((uint32_t)3);
        w.Write((int32_t)10); w.Write((int32_t)20); w.Write((int32_t)30);
    });

    const auto result = std::any_cast<std::vector<int32_t>>(
        reader.ReadUntyped(*contentReader, std::any{}));

    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0], 10);
    EXPECT_EQ(result[1], 20);
    EXPECT_EQ(result[2], 30);
}

TEST_F(CollectionReaderTest, ArrayReaderResizesAMismatchedExistingInstance)
{
    CNA::Internal::Xnb::ArrayReader<int32_t> reader(
        "System.Int32[]", "Microsoft.Xna.Framework.Content.Int32Reader");

    auto contentReader = MakeReader([](auto& w) {
        w.Write((uint32_t)2);
        w.Write((int32_t)1); w.Write((int32_t)2);
    });

    std::vector<int32_t> existing = {99, 99, 99, 99}; // wrong size on purpose
    const auto result = std::any_cast<std::vector<int32_t>>(
        reader.ReadUntyped(*contentReader, std::any(existing)));

    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0], 1);
    EXPECT_EQ(result[1], 2);
}

TEST_F(CollectionReaderTest, ArrayReaderConsumesTypeIndicesForSystemStringElements)
{
    CNA::Internal::Xnb::ArrayReader<std::string> reader(
        "System.String[]", "Microsoft.Xna.Framework.Content.StringReader");

    auto contentReader = MakeReader([](auto& w) {
        w.Write7BitEncodedInt(1);
        w.Write(std::string("Microsoft.Xna.Framework.Content.StringReader"));
        w.Write((int32_t)0);
        w.Write7BitEncodedInt(0);

        w.Write((uint32_t)2);
        w.Write7BitEncodedInt(1);
        w.Write(std::string("cat"));
        w.Write7BitEncodedInt(1);
        w.Write(std::string("glow1"));
    });
    contentReader->InitializeTypeReaders();

    const auto result = std::any_cast<std::vector<std::string>>(
        reader.ReadUntyped(*contentReader, std::any{}));

    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0], "cat");
    EXPECT_EQ(result[1], "glow1");
}

TEST_F(CollectionReaderTest, ListReaderAppendsToExistingInstanceWithoutClearing)
{
    CNA::Internal::Xnb::ListReader<Vector3> reader(
        "System.Collections.Generic.List`1[[Microsoft.Xna.Framework.Vector3]]",
        "Microsoft.Xna.Framework.Content.Vector3Reader");

    auto contentReader = MakeReader([](auto& w) {
        w.Write((int32_t)1);
        w.Write(4.0f); w.Write(5.0f); w.Write(6.0f); // one Vector3
    });

    std::vector<Vector3> existing = {Vector3(1.0f, 2.0f, 3.0f)};
    const auto result = std::any_cast<std::vector<Vector3>>(
        reader.ReadUntyped(*contentReader, std::any(existing)));

    // FNA's own ListReader never clears existingInstance -- appends after it.
    ASSERT_EQ(result.size(), 2u);
    EXPECT_FLOAT_EQ(result[0].X, 1.0f);
    EXPECT_FLOAT_EQ(result[1].X, 4.0f);
}

TEST_F(CollectionReaderTest, ListReaderCanDeserializeIntoExistingObjectIsTrue)
{
    CNA::Internal::Xnb::ListReader<Vector3> reader(
        "System.Collections.Generic.List`1[[Microsoft.Xna.Framework.Vector3]]",
        "Microsoft.Xna.Framework.Content.Vector3Reader");
    EXPECT_TRUE(reader.getCanDeserializeIntoExistingObjectProperty());
}

TEST_F(CollectionReaderTest, ListReaderConsumesTypeIndicesForSystemStringElements)
{
    CNA::Internal::Xnb::ListReader<std::string> reader(
        "System.Collections.Generic.List`1[[System.String]]",
        "Microsoft.Xna.Framework.Content.StringReader");

    auto contentReader = MakeReader([](auto& w) {
        w.Write7BitEncodedInt(1);
        w.Write(std::string("Microsoft.Xna.Framework.Content.StringReader"));
        w.Write((int32_t)0);
        w.Write7BitEncodedInt(0);

        w.Write((int32_t)2);
        w.Write7BitEncodedInt(1);
        w.Write(std::string("cat"));
        w.Write7BitEncodedInt(1);
        w.Write(std::string("glow1"));
    });
    contentReader->InitializeTypeReaders();

    const auto result = std::any_cast<std::vector<std::string>>(
        reader.ReadUntyped(*contentReader, std::any{}));

    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0], "cat");
    EXPECT_EQ(result[1], "glow1");
}

TEST_F(CollectionReaderTest, DictionaryReaderAddsToTheExistingInstanceWithoutClearingIt)
{
    CNA::Internal::Xnb::DictionaryReader<int32_t, int32_t> reader(
        "System.Collections.Generic.Dictionary`2[[System.Int32],[System.Int32]]",
        "Microsoft.Xna.Framework.Content.Int32Reader",
        "Microsoft.Xna.Framework.Content.Int32Reader");

    auto contentReader = MakeReader([](auto& w) {
        w.Write((int32_t)2); // count
        w.Write((int32_t)1); w.Write((int32_t)100); // key 1 -> value 100
        w.Write((int32_t)2); w.Write((int32_t)200); // key 2 -> value 200
    });

    // TQ-033, XNA over FNA: DictionaryReader`2.Read IL_0007-0013 uses the existing instance as is
    // (existingInstance ?? new Dictionary(count)) and only Adds (IL_0032); FNA clears it first.
    std::unordered_map<int32_t, int32_t> existing = {{999, 999}};
    const auto result = std::any_cast<std::unordered_map<int32_t, int32_t>>(
        reader.ReadUntyped(*contentReader, std::any(existing)));

    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result.at(1), 100);
    EXPECT_EQ(result.at(2), 200);
    EXPECT_EQ(result.at(999), 999);
}

TEST_F(CollectionReaderTest, DictionaryReaderDuplicateKeyIsArgumentException)
{
    CNA::Internal::Xnb::DictionaryReader<int32_t, int32_t> reader(
        "System.Collections.Generic.Dictionary`2[[System.Int32],[System.Int32]]",
        "Microsoft.Xna.Framework.Content.Int32Reader",
        "Microsoft.Xna.Framework.Content.Int32Reader");

    // DictionaryReader`2.Read IL_002f-0032: Dictionary.Add, which refuses a key it already holds
    // with ArgumentException -- within the stream, and against the existing instance.
    auto twice = MakeReader([](auto& w) {
        w.Write((int32_t)2);
        w.Write((int32_t)7); w.Write((int32_t)1);
        w.Write((int32_t)7); w.Write((int32_t)2);
    });
    EXPECT_THROW((void)reader.ReadUntyped(*twice, std::any{}), System::ArgumentException);

    auto clash = MakeReader([](auto& w) {
        w.Write((int32_t)1);
        w.Write((int32_t)999); w.Write((int32_t)5);
    });
    std::unordered_map<int32_t, int32_t> existing = {{999, 999}};
    EXPECT_THROW((void)reader.ReadUntyped(*clash, std::any(existing)), System::ArgumentException);
}

TEST_F(CollectionReaderTest, StringObjectDictionaryReaderMergesAndRefusesADuplicateKey)
{
    CNA::Internal::Xnb::StringObjectDictionaryReader reader;
    const auto table = [](auto& w) {
        w.Write7BitEncodedInt(2);
        w.Write(std::string("Microsoft.Xna.Framework.Content.StringReader"));
        w.Write((int32_t)0);
        w.Write(std::string("Microsoft.Xna.Framework.Content.Int32Reader"));
        w.Write((int32_t)0);
        w.Write7BitEncodedInt(0);
    };

    // Dictionary<string, object> is DictionaryReader`2 too: no Clear, Add per entry.
    auto merge = MakeReader([&](auto& w) {
        table(w);
        w.Write((int32_t)1);
        w.Write7BitEncodedInt(1); w.Write(std::string("b"));
        w.Write7BitEncodedInt(2); w.Write((int32_t)2);
    });
    merge->InitializeTypeReaders();
    std::map<std::string, std::any> existing;
    existing.emplace("a", std::any(std::int32_t{1}));
    const auto merged = std::any_cast<std::map<std::string, std::any>>(
        reader.ReadUntyped(*merge, std::any(existing)));
    ASSERT_EQ(merged.size(), 2u);
    EXPECT_EQ(std::any_cast<std::int32_t>(merged.at("a")), 1);
    EXPECT_EQ(std::any_cast<std::int32_t>(merged.at("b")), 2);

    auto twice = MakeReader([&](auto& w) {
        table(w);
        w.Write((int32_t)2);
        w.Write7BitEncodedInt(1); w.Write(std::string("k"));
        w.Write7BitEncodedInt(2); w.Write((int32_t)1);
        w.Write7BitEncodedInt(1); w.Write(std::string("k"));
        w.Write7BitEncodedInt(2); w.Write((int32_t)2);
    });
    twice->InitializeTypeReaders();
    EXPECT_THROW((void)reader.ReadUntyped(*twice, std::any{}), System::ArgumentException);
}

TEST_F(CollectionReaderTest, DictionaryReaderConsumesTypeIndicesForSystemStringKeys)
{
    CNA::Internal::Xnb::DictionaryReader<std::string, int32_t> reader(
        "System.Collections.Generic.Dictionary`2[[System.String],[System.Int32]]",
        "Microsoft.Xna.Framework.Content.StringReader",
        "Microsoft.Xna.Framework.Content.Int32Reader");

    auto contentReader = MakeReader([](auto& w) {
        w.Write7BitEncodedInt(1);
        w.Write(std::string("Microsoft.Xna.Framework.Content.StringReader"));
        w.Write((int32_t)0);
        w.Write7BitEncodedInt(0);

        w.Write((int32_t)2);
        w.Write7BitEncodedInt(1);
        w.Write(std::string("cat"));
        w.Write((int32_t)0);
        w.Write7BitEncodedInt(1);
        w.Write(std::string("glow1"));
        w.Write((int32_t)1);
    });
    contentReader->InitializeTypeReaders();

    const auto result =
        std::any_cast<std::unordered_map<std::string, int32_t>>(
            reader.ReadUntyped(*contentReader, std::any{}));

    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result.at("cat"), 0);
    EXPECT_EQ(result.at("glow1"), 1);
}

TEST_F(CollectionReaderTest, PrimitiveRegistrationProvidesStringInt32DictionaryReader)
{
    auto reader = ContentTypeReaderManager::CreateReader(
        "Microsoft.Xna.Framework.Content.DictionaryReader`2[[System.String],[System.Int32]]");
    ASSERT_NE(reader, nullptr);

    auto contentReader = MakeReader([](auto& w) {
        w.Write7BitEncodedInt(1);
        w.Write(std::string("Microsoft.Xna.Framework.Content.StringReader"));
        w.Write((int32_t)0);
        w.Write7BitEncodedInt(0);

        w.Write((int32_t)2);
        w.Write7BitEncodedInt(1);
        w.Write(std::string("Head"));
        w.Write((int32_t)4);
        w.Write7BitEncodedInt(1);
        w.Write(std::string("L_UpperArm"));
        w.Write((int32_t)17);
    });
    contentReader->InitializeTypeReaders();

    const auto result = std::any_cast<std::unordered_map<std::string, int32_t>>(
        reader->ReadUntyped(*contentReader, std::any{}));

    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result.at("Head"), 4);
    EXPECT_EQ(result.at("L_UpperArm"), 17);
}

TEST_F(CollectionReaderTest, NullableReaderReturnsNulloptWhenFlagIsFalse)
{
    CNA::Internal::Xnb::NullableReader<Vector3> reader(
        "System.Nullable`1[[Microsoft.Xna.Framework.Vector3]]",
        "Microsoft.Xna.Framework.Content.Vector3Reader");

    auto contentReader = MakeReader([](auto& w) { w.Write(false); });

    const auto result = std::any_cast<std::optional<Vector3>>(
        reader.ReadUntyped(*contentReader, std::any{}));

    EXPECT_FALSE(result.has_value());
}

TEST_F(CollectionReaderTest, NullableReaderReadsElementWhenFlagIsTrue)
{
    CNA::Internal::Xnb::NullableReader<Vector3> reader(
        "System.Nullable`1[[Microsoft.Xna.Framework.Vector3]]",
        "Microsoft.Xna.Framework.Content.Vector3Reader");

    auto contentReader = MakeReader([](auto& w) {
        w.Write(true);
        w.Write(7.0f); w.Write(8.0f); w.Write(9.0f);
    });

    const auto result = std::any_cast<std::optional<Vector3>>(
        reader.ReadUntyped(*contentReader, std::any{}));

    ASSERT_TRUE(result.has_value());
    EXPECT_FLOAT_EQ(result->X, 7.0f);
    EXPECT_FLOAT_EQ(result->Y, 8.0f);
}

TEST_F(CollectionReaderTest, ArrayReaderThrowsWhenElementReaderIsUnregistered)
{
    CNA::Internal::Xnb::ArrayReader<int32_t> reader("System.Int32[]", "Not.Registered.Reader");

    auto contentReader = MakeReader([](auto& w) { w.Write((uint32_t)1); w.Write((int32_t)1); });

    EXPECT_THROW(reader.ReadUntyped(*contentReader, std::any{}), ContentLoadException);
}

// XNB-43: an adversarial/corrupt declared element count must be rejected before any
// collection allocation is attempted, not discovered only once the stream itself runs out (which
// would already have triggered a huge allocation attempt for e.g. std::vector<T>(0xFFFFFFFF)).

TEST_F(CollectionReaderTest, ArrayReaderNegativeCountIsOverflowAndAnOverLimitCountIsRefused)
{
    CNA::Internal::Xnb::ArrayReader<int32_t> reader(
        "System.Int32[]", "Microsoft.Xna.Framework.Content.Int32Reader");

    // TQ-033, XNA: ArrayReader`1.Read IL_0001-0008 reads an Int32 count and `newarr`s it, which is
    // OverflowException for a negative size; 0xFFFFFFFF is -1.
    auto negative = MakeReader([](auto& w) { w.Write((uint32_t)0xFFFFFFFFu); });
    EXPECT_THROW(reader.ReadUntyped(*negative, std::any{}), System::OverflowException);

    // A positive count above CNA's limit is still refused before any allocation (retained
    // hardening: XNA would attempt the allocation).
    auto huge = MakeReader([](auto& w) {
        w.Write((int32_t)(CNA::Internal::Xnb::DefaultXnbReadLimits().maxCollectionElementCount + 1));
    });
    EXPECT_THROW(reader.ReadUntyped(*huge, std::any{}), ContentLoadException);
}

TEST_F(CollectionReaderTest, ListReaderNegativeCountAddsNothing)
{
    CNA::Internal::Xnb::ListReader<Vector3> reader(
        "System.Collections.Generic.List`1[[Microsoft.Xna.Framework.Vector3]]",
        "Microsoft.Xna.Framework.Content.Vector3Reader");

    // TQ-033, XNA: ListReader`1.Read IL_0026-002c loops `while (count-- > 0)`, so a negative count
    // reads nothing and returns the (new or existing) list as it is -- no exception, no reserve.
    auto fresh = MakeReader([](auto& w) { w.Write((int32_t)-1); });
    const auto empty = std::any_cast<std::vector<Vector3>>(reader.ReadUntyped(*fresh, std::any{}));
    EXPECT_TRUE(empty.empty());

    auto into = MakeReader([](auto& w) { w.Write((int32_t)-5); });
    const auto kept = std::any_cast<std::vector<Vector3>>(
        reader.ReadUntyped(*into, std::any(std::vector<Vector3>{Vector3(1, 2, 3)})));
    ASSERT_EQ(kept.size(), 1u);
    EXPECT_EQ(kept[0], Vector3(1, 2, 3));
}

TEST_F(CollectionReaderTest, ListReaderRejectsAnElementCountAboveTheConfiguredLimit)
{
    CNA::Internal::Xnb::ListReader<Vector3> reader(
        "System.Collections.Generic.List`1[[Microsoft.Xna.Framework.Vector3]]",
        "Microsoft.Xna.Framework.Content.Vector3Reader");

    auto contentReader = MakeReader([](auto& w) {
        w.Write((int32_t)(CNA::Internal::Xnb::DefaultXnbReadLimits().maxCollectionElementCount + 1));
    });

    EXPECT_THROW(reader.ReadUntyped(*contentReader, std::any{}), ContentLoadException);
}

TEST_F(CollectionReaderTest, DictionaryReaderNegativeCountIsArgumentOutOfRangeOnlyWithoutAnInstance)
{
    CNA::Internal::Xnb::DictionaryReader<int32_t, int32_t> reader(
        "System.Collections.Generic.Dictionary`2[[System.Int32],[System.Int32]]",
        "Microsoft.Xna.Framework.Content.Int32Reader",
        "Microsoft.Xna.Framework.Content.Int32Reader");

    // TQ-033, XNA: DictionaryReader`2.Read IL_000c-000d builds `new Dictionary(count)`, whose
    // capacity check refuses a negative count with ArgumentOutOfRangeException; into an existing
    // instance the `while (count-- > 0)` loop (IL_0037-003d) simply adds nothing.
    auto fresh = MakeReader([](auto& w) { w.Write((int32_t)-1); });
    EXPECT_THROW(reader.ReadUntyped(*fresh, std::any{}), System::ArgumentOutOfRangeException);

    auto into = MakeReader([](auto& w) { w.Write((int32_t)-1); });
    const auto kept = std::any_cast<std::unordered_map<int32_t, int32_t>>(
        reader.ReadUntyped(*into, std::any(std::unordered_map<int32_t, int32_t>{{4, 40}})));
    ASSERT_EQ(kept.size(), 1u);
    EXPECT_EQ(kept.at(4), 40);

    // Dictionary<string, object> is the same reader.
    CNA::Internal::Xnb::StringObjectDictionaryReader objects;
    auto objectsFresh = MakeReader([](auto& w) { w.Write((int32_t)-2); });
    EXPECT_THROW(objects.ReadUntyped(*objectsFresh, std::any{}), System::ArgumentOutOfRangeException);
}

TEST_F(CollectionReaderTest, NullableReadersAreRegisteredForEveryBuiltInValueType)
{
    // TQ-033, XNA: ContentTypeReaderManager instantiates NullableReader`1<T> for any value type T
    // on demand; CNA has no reflection, so the closed readers of its value types are registered.
    CNA::Internal::Xnb::RegisterDecimalDateTimeXnbReaders();
    for (const char* type : {
             "System.Boolean", "System.Byte", "System.SByte", "System.Int16", "System.UInt16",
             "System.Int32", "System.UInt32", "System.Int64", "System.UInt64", "System.Single",
             "System.Double", "System.Char", "System.TimeSpan", "System.DateTime",
             "Microsoft.Xna.Framework.Vector2", "Microsoft.Xna.Framework.Vector3",
             "Microsoft.Xna.Framework.Vector4", "Microsoft.Xna.Framework.Matrix",
             "Microsoft.Xna.Framework.Quaternion", "Microsoft.Xna.Framework.Color",
             "Microsoft.Xna.Framework.Point", "Microsoft.Xna.Framework.Rectangle",
             "Microsoft.Xna.Framework.BoundingBox", "Microsoft.Xna.Framework.BoundingSphere",
             "Microsoft.Xna.Framework.Ray", "Microsoft.Xna.Framework.Plane"})
    {
        EXPECT_TRUE(ContentTypeReaderManager::IsRegistered(
            std::string("Microsoft.Xna.Framework.Content.NullableReader`1[[") + type + "]]"))
            << type;
    }

    // NullableReader`1.Read IL_0000-0023: a Boolean, then the raw value when it is true.
    auto int32Reader = ContentTypeReaderManager::CreateReader(
        "Microsoft.Xna.Framework.Content.NullableReader`1[[System.Int32]]");
    ASSERT_NE(int32Reader, nullptr);
    auto present = MakeReader([](auto& w) { w.Write(true); w.Write((int32_t)42); });
    EXPECT_EQ(std::any_cast<std::optional<int32_t>>(int32Reader->ReadUntyped(*present, std::any{})),
              std::optional<int32_t>(42));
    auto absent = MakeReader([](auto& w) { w.Write(false); });
    EXPECT_EQ(std::any_cast<std::optional<int32_t>>(int32Reader->ReadUntyped(*absent, std::any{})),
              std::nullopt);

    auto vectorReader = ContentTypeReaderManager::CreateReader(
        "Microsoft.Xna.Framework.Content.NullableReader`1[[Microsoft.Xna.Framework.Vector3]]");
    ASSERT_NE(vectorReader, nullptr);
    auto vector = MakeReader([](auto& w) { w.Write(true); w.Write(1.5f); w.Write(-2.0f); w.Write(3.25f); });
    EXPECT_EQ(std::any_cast<std::optional<Vector3>>(vectorReader->ReadUntyped(*vector, std::any{})),
              std::optional<Vector3>(Vector3(1.5f, -2.0f, 3.25f)));
}
