// SPDX-License-Identifier: MS-PL
// ISSUE-043 target-to-factory association and registration lifetime regressions.
#include <gtest/gtest.h>
#include <type_traits>
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "System/InvalidOperationException.hpp"

using namespace Microsoft::Xna::Framework::Content;

namespace
{
    struct LookupValue { int value = 0; };
    struct OtherValue { int value = 0; };
    template<class T> class LookupReader final : public ContentTypeReader<T>
    {
    public:
        LookupReader() : ContentTypeReader<T>(std::is_same_v<T, LookupValue>
            ? "CNA.Repair.Target" : "CNA.Repair.OtherTarget") { }
    protected:
        T Read(ContentReader&, std::optional<T> existing) override
        { return existing.value_or(T{}); }
    };
    class ReaderTargetLookupCompatibilityTest : public ::testing::Test
    {
    protected:
        static constexpr const char* key = "CNA.Repair.TargetReader";
        void SetUp() override { Clear(); }
        void TearDown() override { Clear(); }
        static void Clear()
        {
            ContentTypeReaderManager::ClearTypeCreators();
            ContentTypeReaderManager::ClearTargetTypeAssociationsEXT();
        }
        static void Register()
        {
            ContentTypeReaderManager::AddTypeCreator(key,
                [] { return std::make_unique<LookupReader<LookupValue>>(); });
        }
        static std::unique_ptr<ContentTypeReaderBase> Resolve()
        { return ContentTypeReaderManager{}.GetTypeReader(System::Type::From<LookupValue>()); }
    };
TEST_F(ReaderTargetLookupCompatibilityTest, DistinctTargetAndFactoryNamesResolveFreshReaders)
{
    Register();
    auto tableReader = ContentTypeReaderManager::CreateReader(key);
    ASSERT_NE(tableReader, nullptr);
    auto first = Resolve();
    auto second = Resolve();
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_NE(dynamic_cast<LookupReader<LookupValue>*>(first.get()), nullptr);
    EXPECT_NE(first.get(), tableReader.get());
    EXPECT_NE(first.get(), second.get());
}

TEST_F(ReaderTargetLookupCompatibilityTest, RegistrationDoesNotEagerlyInvokeFactories)
{
    int calls = 0;
    ContentTypeReaderManager::AddTypeCreator(key, [&] {
        ++calls;
        return std::make_unique<LookupReader<LookupValue>>();
    });
    EXPECT_EQ(calls, 0);
    EXPECT_EQ(Resolve(), nullptr);
    EXPECT_EQ(calls, 0);
    ASSERT_NE(ContentTypeReaderManager::CreateReader(key), nullptr);
    EXPECT_EQ(calls, 1);
    EXPECT_NE(Resolve(), nullptr);
    EXPECT_EQ(calls, 2);
}

TEST_F(ReaderTargetLookupCompatibilityTest, RemovingFactoryWithdrawsTargetLookup)
{
    Register();
    ASSERT_NE(ContentTypeReaderManager::CreateReader(key), nullptr);
    ASSERT_NE(Resolve(), nullptr);
    EXPECT_TRUE(ContentTypeReaderManager::RemoveTypeCreatorEXT(key));
    EXPECT_EQ(Resolve(), nullptr);
}

TEST_F(ReaderTargetLookupCompatibilityTest, ClearingFactoriesWithdrawsTargetLookup)
{
    Register();
    ASSERT_NE(ContentTypeReaderManager::CreateReader(key), nullptr);
    ASSERT_NE(Resolve(), nullptr);
    ContentTypeReaderManager::ClearTypeCreators();
    EXPECT_EQ(Resolve(), nullptr);
}

TEST_F(ReaderTargetLookupCompatibilityTest, ReusedFactoryNameDoesNotRetainOldTarget)
{
    Register();
    ASSERT_NE(ContentTypeReaderManager::CreateReader(key), nullptr);
    ASSERT_TRUE(ContentTypeReaderManager::RemoveTypeCreatorEXT(key));
    ContentTypeReaderManager::AddTypeCreator(key,
        [] { return std::make_unique<LookupReader<OtherValue>>(); });
    ASSERT_NE(ContentTypeReaderManager::CreateReader(key), nullptr);
    EXPECT_EQ(Resolve(), nullptr);
    auto other = ContentTypeReaderManager{}.GetTypeReader(System::Type::From<OtherValue>());
    EXPECT_NE(dynamic_cast<LookupReader<OtherValue>*>(other.get()), nullptr);
}

TEST_F(ReaderTargetLookupCompatibilityTest, RemovedDuringFactoryDoesNotPublishStaleAssociation)
{
    ContentTypeReaderManager::AddTypeCreator(key, [] {
        EXPECT_TRUE(ContentTypeReaderManager::RemoveTypeCreatorEXT(key));
        return std::make_unique<LookupReader<LookupValue>>();
    });
    ASSERT_NE(ContentTypeReaderManager::CreateReader(key), nullptr);
    EXPECT_EQ(Resolve(), nullptr);
}

TEST_F(ReaderTargetLookupCompatibilityTest, ReplacedDuringFactoryDoesNotPublishOldTarget)
{
    ContentTypeReaderManager::AddTypeCreator(key, [] {
        EXPECT_TRUE(ContentTypeReaderManager::RemoveTypeCreatorEXT(key));
        ContentTypeReaderManager::AddTypeCreator(key,
            [] { return std::make_unique<LookupReader<OtherValue>>(); });
        return std::make_unique<LookupReader<LookupValue>>();
    });
    ASSERT_NE(ContentTypeReaderManager::CreateReader(key), nullptr);
    EXPECT_EQ(Resolve(), nullptr);
    ASSERT_NE(ContentTypeReaderManager::CreateReader(key), nullptr);
    auto other = ContentTypeReaderManager{}.GetTypeReader(System::Type::From<OtherValue>());
    EXPECT_NE(dynamic_cast<LookupReader<OtherValue>*>(other.get()), nullptr);
}

TEST_F(ReaderTargetLookupCompatibilityTest, UnreturnedConstructedReaderDoesNotUseTheWrongFactory)
{
    ContentTypeReaderManager::AddTypeCreator(key, [] {
        const LookupReader<OtherValue> unrelated;
        return std::make_unique<LookupReader<LookupValue>>();
    });
    ASSERT_NE(ContentTypeReaderManager::CreateReader(key), nullptr);
    EXPECT_NE(Resolve(), nullptr);
    EXPECT_EQ(ContentTypeReaderManager{}.GetTypeReader(System::Type::From<OtherValue>()), nullptr);
}

TEST_F(ReaderTargetLookupCompatibilityTest, DirectCanonicalNameFactoryAssociationRemainsSupported)
{
    ContentTypeReaderManager::AddTypeCreator("CNA.Repair.Target",
        [] { return std::make_unique<LookupReader<LookupValue>>(); });
    const LookupReader<LookupValue> association;
    auto reader = Resolve();
    EXPECT_NE(dynamic_cast<LookupReader<LookupValue>*>(reader.get()), nullptr);
}

TEST_F(ReaderTargetLookupCompatibilityTest, ManualFactoryKeyAssociationRemainsSupported)
{
    Register();
    ContentTypeReaderManager::AssociateTargetType(System::Type::From<LookupValue>(), key);
    auto reader = Resolve();
    EXPECT_NE(dynamic_cast<LookupReader<LookupValue>*>(reader.get()), nullptr);
}

TEST_F(ReaderTargetLookupCompatibilityTest, ThrowingFactoryDoesNotPublishTarget)
{
    ContentTypeReaderManager::AddTypeCreator(key,
        []() -> std::unique_ptr<ContentTypeReaderBase> {
            const LookupReader<LookupValue> unreturned;
            throw System::InvalidOperationException("factory refusal");
        });
    EXPECT_THROW((void)ContentTypeReaderManager::CreateReader(key), System::InvalidOperationException);
    EXPECT_EQ(Resolve(), nullptr);
}
}
