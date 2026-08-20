// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnb.md XNB-14/XNB-14A/XNB-14B: unit tests for ContentTypeReaderManager's registration
// surface. Does not call ReadUntyped()/Read() on any reader -- that needs a real ContentReader&
// (plans/plan_xnb.md XNB-15/16), out of scope for this task; these tests cover only registration,
// per-call instance freshness, and concrete EffectReader registration.

#include <gtest/gtest.h>

#include "CNA/Internal/Xnb/EffectContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Content/KnownUnsupportedContentTypeReader.hpp"

using Microsoft::Xna::Framework::Content::ContentReader;
using Microsoft::Xna::Framework::Content::ContentTypeReader;
using Microsoft::Xna::Framework::Content::ContentTypeReaderBase;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
using CNA::Internal::Xnb::EffectReader;
using CNA::Internal::Xnb::RegisterEffectXnbReader;

namespace
{
    // Minimal concrete reader for registry-mechanics tests. Its Read() body is never invoked --
    // only the registry's ability to construct instances of the right concrete type is tested.
    class TestOnlyInt32Reader : public ContentTypeReader<int32_t>
    {
    public:
        TestOnlyInt32Reader() : ContentTypeReader<int32_t>("CNA.Test.Int32") {}

    protected:
        int32_t Read(ContentReader& /*input*/, std::optional<int32_t> existingInstance) override
        {
            return existingInstance.value_or(0);
        }
    };

    class ContentTypeReaderManagerTest : public ::testing::Test
    {
    protected:
        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }
    };
}

TEST_F(ContentTypeReaderManagerTest, CreateReaderReturnsNullForUnregisteredName)
{
    EXPECT_EQ(ContentTypeReaderManager::CreateReader("Nothing.Registered.Here"), nullptr);
}

TEST_F(ContentTypeReaderManagerTest, RegisteredFactoryProducesTheRightConcreteType)
{
    ContentTypeReaderManager::AddTypeCreator(
        "CNA.Test.Int32Reader", [] { return std::make_unique<TestOnlyInt32Reader>(); });

    auto reader = ContentTypeReaderManager::CreateReader("CNA.Test.Int32Reader");

    ASSERT_NE(reader, nullptr);
    EXPECT_NE(dynamic_cast<TestOnlyInt32Reader*>(reader.get()), nullptr);
    EXPECT_EQ(reader->getTargetTypeNameProperty(), "CNA.Test.Int32");
}

TEST_F(ContentTypeReaderManagerTest, EachCreateReaderCallReturnsAFreshInstance)
{
    ContentTypeReaderManager::AddTypeCreator(
        "CNA.Test.Int32Reader", [] { return std::make_unique<TestOnlyInt32Reader>(); });

    auto first = ContentTypeReaderManager::CreateReader("CNA.Test.Int32Reader");
    auto second = ContentTypeReaderManager::CreateReader("CNA.Test.Int32Reader");

    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_NE(first.get(), second.get());
}

TEST_F(ContentTypeReaderManagerTest, RepeatRegistrationOfSameNameIsIgnoredNotReplaced)
{
    bool firstFactoryCalled = false;
    bool secondFactoryCalled = false;

    ContentTypeReaderManager::AddTypeCreator("CNA.Test.Int32Reader", [&] {
        firstFactoryCalled = true;
        return std::make_unique<TestOnlyInt32Reader>();
    });
    ContentTypeReaderManager::AddTypeCreator("CNA.Test.Int32Reader", [&] {
        secondFactoryCalled = true;
        return std::make_unique<TestOnlyInt32Reader>();
    });

    auto reader = ContentTypeReaderManager::CreateReader("CNA.Test.Int32Reader");

    EXPECT_TRUE(firstFactoryCalled);
    EXPECT_FALSE(secondFactoryCalled);
    ASSERT_NE(reader, nullptr);
}

TEST_F(ContentTypeReaderManagerTest, ClearTypeCreatorsRemovesRegistration)
{
    ContentTypeReaderManager::AddTypeCreator(
        "CNA.Test.Int32Reader", [] { return std::make_unique<TestOnlyInt32Reader>(); });
    ContentTypeReaderManager::ClearTypeCreators();

    EXPECT_EQ(ContentTypeReaderManager::CreateReader("CNA.Test.Int32Reader"), nullptr);
}

TEST_F(ContentTypeReaderManagerTest, RegisterEffectXnbReaderRegistersConcreteReader)
{
    RegisterEffectXnbReader();

    auto reader = ContentTypeReaderManager::CreateReader("Microsoft.Xna.Framework.Content.EffectReader");

    ASSERT_NE(reader, nullptr);
    EXPECT_NE(dynamic_cast<EffectReader*>(reader.get()), nullptr);
    EXPECT_EQ(reader->getTargetTypeNameProperty(), "Microsoft.Xna.Framework.Graphics.Effect");
}

TEST_F(ContentTypeReaderManagerTest, RegisterEffectXnbReaderIsIdempotent)
{
    RegisterEffectXnbReader();
    RegisterEffectXnbReader();

    auto reader = ContentTypeReaderManager::CreateReader("Microsoft.Xna.Framework.Content.EffectReader");
    ASSERT_NE(reader, nullptr);
}

// -----------------------------------------------------------------------
// RemoveTypeCreatorEXT (CNAEXT)
//
// The registry could previously only be emptied wholesale, which is no help to a registration
// that belongs to something with a lifetime -- withdrawing one caller's reader must not take the
// built-in readers with it.
// -----------------------------------------------------------------------

TEST_F(ContentTypeReaderManagerTest, RemoveTypeCreatorWithdrawsOneRegistration)
{
    ContentTypeReaderManager::AddTypeCreator(
        "CNA.Test.Int32Reader", [] { return std::make_unique<TestOnlyInt32Reader>(); });
    ASSERT_TRUE(ContentTypeReaderManager::IsRegistered("CNA.Test.Int32Reader"));

    EXPECT_TRUE(ContentTypeReaderManager::RemoveTypeCreatorEXT("CNA.Test.Int32Reader"));

    EXPECT_FALSE(ContentTypeReaderManager::IsRegistered("CNA.Test.Int32Reader"));
    EXPECT_EQ(ContentTypeReaderManager::CreateReader("CNA.Test.Int32Reader"), nullptr);
}

TEST_F(ContentTypeReaderManagerTest, RemoveTypeCreatorLeavesEveryOtherRegistrationAlone)
{
    RegisterEffectXnbReader();
    ContentTypeReaderManager::AddTypeCreator(
        "CNA.Test.Int32Reader", [] { return std::make_unique<TestOnlyInt32Reader>(); });

    EXPECT_TRUE(ContentTypeReaderManager::RemoveTypeCreatorEXT("CNA.Test.Int32Reader"));

    EXPECT_TRUE(ContentTypeReaderManager::IsRegistered(
        "Microsoft.Xna.Framework.Content.EffectReader"));
}

TEST_F(ContentTypeReaderManagerTest, RemoveTypeCreatorReportsFalseForAnUnregisteredName)
{
    EXPECT_FALSE(ContentTypeReaderManager::RemoveTypeCreatorEXT("CNA.Test.NeverRegistered"));
}

TEST_F(ContentTypeReaderManagerTest, RemoveTypeCreatorFreesTheNameForANewRegistration)
{
    ContentTypeReaderManager::AddTypeCreator(
        "CNA.Test.Int32Reader", [] { return std::make_unique<TestOnlyInt32Reader>(); });
    ASSERT_TRUE(ContentTypeReaderManager::RemoveTypeCreatorEXT("CNA.Test.Int32Reader"));

    // AddTypeCreator ignores a repeat registration of a live name, so the only way a second
    // factory can take the name at all is if the first was really removed rather than shadowed.
    bool secondFactoryRan = false;
    ContentTypeReaderManager::AddTypeCreator(
        "CNA.Test.Int32Reader",
        [&secondFactoryRan]
        {
            secondFactoryRan = true;
            return std::make_unique<TestOnlyInt32Reader>();
        });

    EXPECT_NE(ContentTypeReaderManager::CreateReader("CNA.Test.Int32Reader"), nullptr);
    EXPECT_TRUE(secondFactoryRan);
}
