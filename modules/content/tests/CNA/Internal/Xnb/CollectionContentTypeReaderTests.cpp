// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnb.md XNB-21/XNB-22: unit tests for the generic collection .xnb readers (ArrayReader<T>,
// ListReader<T>, DictionaryReader<TKey,TValue>, NullableReader<T>), each instantiated with a
// representative element type and registered element reader, matching how a real consumer would
// use them.

#include <any>
#include <gtest/gtest.h>

#include "CNA/Internal/Xnb/CollectionContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/MathContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/PrimitiveContentTypeReaders.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
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

TEST_F(CollectionReaderTest, DictionaryReaderClearsExistingInstanceThenRepopulates)
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

    std::unordered_map<int32_t, int32_t> existing = {{999, 999}}; // must be cleared, not merged
    const auto result = std::any_cast<std::unordered_map<int32_t, int32_t>>(
        reader.ReadUntyped(*contentReader, std::any(existing)));

    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result.at(1), 100);
    EXPECT_EQ(result.at(2), 200);
    EXPECT_EQ(result.find(999), result.end());
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

// plans/plan_xnb.md XNB-43: an adversarial/corrupt declared element count must be rejected before any
// collection allocation is attempted, not discovered only once the stream itself runs out (which
// would already have triggered a huge allocation attempt for e.g. std::vector<T>(0xFFFFFFFF)).

TEST_F(CollectionReaderTest, ArrayReaderRejectsAnAdversarialElementCountBeforeAllocating)
{
    CNA::Internal::Xnb::ArrayReader<int32_t> reader(
        "System.Int32[]", "Microsoft.Xna.Framework.Content.Int32Reader");

    auto contentReader = MakeReader([](auto& w) { w.Write((uint32_t)0xFFFFFFFFu); });

    EXPECT_THROW(reader.ReadUntyped(*contentReader, std::any{}), ContentLoadException);
}

TEST_F(CollectionReaderTest, ListReaderRejectsANegativeElementCountBeforeReserving)
{
    CNA::Internal::Xnb::ListReader<Vector3> reader(
        "System.Collections.Generic.List`1[[Microsoft.Xna.Framework.Vector3]]",
        "Microsoft.Xna.Framework.Content.Vector3Reader");

    // A negative int32 count, if cast unchecked to size_t for vector::reserve(), would become an
    // enormous allocation request instead of a clean, catchable error.
    auto contentReader = MakeReader([](auto& w) { w.Write((int32_t)-1); });

    EXPECT_THROW(reader.ReadUntyped(*contentReader, std::any{}), ContentLoadException);
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

TEST_F(CollectionReaderTest, DictionaryReaderRejectsANegativeElementCount)
{
    CNA::Internal::Xnb::DictionaryReader<int32_t, int32_t> reader(
        "System.Collections.Generic.Dictionary`2[[System.Int32],[System.Int32]]",
        "Microsoft.Xna.Framework.Content.Int32Reader",
        "Microsoft.Xna.Framework.Content.Int32Reader");

    auto contentReader = MakeReader([](auto& w) { w.Write((int32_t)-1); });

    EXPECT_THROW(reader.ReadUntyped(*contentReader, std::any{}), ContentLoadException);
}
