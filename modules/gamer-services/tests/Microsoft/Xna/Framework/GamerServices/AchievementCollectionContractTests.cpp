// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <tuple>
#include <vector>
#include "Microsoft/Xna/Framework/GamerServices/AchievementCollection.hpp"
#include "System/Collections/Generic/KeyNotFoundException.hpp"
#include "System/NotSupportedException.hpp"

using namespace Microsoft::Xna::Framework::GamerServices;

namespace {
    Achievement Value(const char* key) {
        return Achievement::CreateInternal(key, key, "description", true, false, System::DateTime{});
    }

    enum class Mutation { Add, Insert, InsertNegative, Remove, RemoveMissing, RemoveAt, RemoveAtNegative, Clear };

    class AchievementReadOnlyContractTest : public testing::TestWithParam<std::tuple<bool, Mutation>> { };

    // Genuine XNA ICollection/IList mutation refuses before inspecting the item or index.
    // This also protects populated collections against partial mutation on the refusal path.
    TEST_P(AchievementReadOnlyContractTest, RefusesModificationAndPreservesEveryValue) {
        const auto [populated, operation] = GetParam();
        const std::vector<Achievement> original = populated
            ? std::vector<Achievement>{Value("a"), Value("b")} : std::vector<Achievement>{};
        auto collection = AchievementCollection::CreateInternal(original);
        ASSERT_TRUE(collection.getIsReadOnlyProperty());
        auto mutate = [&] {
            switch (operation) {
                case Mutation::Add: collection.Add(Value("c")); break;
                case Mutation::Insert: collection.Insert(0, Value("c")); break;
                case Mutation::InsertNegative: collection.Insert(-1, Value("c")); break;
                case Mutation::Remove: (void)collection.Remove(Value("a")); break;
                case Mutation::RemoveMissing: (void)collection.Remove(Value("missing")); break;
                case Mutation::RemoveAt: collection.RemoveAt(0); break;
                case Mutation::RemoveAtNegative: collection.RemoveAt(-1); break;
                case Mutation::Clear: collection.Clear(); break;
            }
        };
        EXPECT_THROW(mutate(), System::NotSupportedException);
        EXPECT_EQ(original, std::vector<Achievement>(collection.begin(), collection.end()));
        EXPECT_TRUE(collection.getIsReadOnlyProperty());
        EXPECT_FALSE(collection.getIsDisposedProperty());
    }

    INSTANTIATE_TEST_SUITE_P(XnaCollectionMutation, AchievementReadOnlyContractTest,
        testing::Combine(testing::Bool(), testing::Values(Mutation::Add, Mutation::Insert,
            Mutation::InsertNegative, Mutation::Remove, Mutation::RemoveMissing,
            Mutation::RemoveAt, Mutation::RemoveAtNegative, Mutation::Clear)));

    class AchievementKeyContractTest : public testing::TestWithParam<bool> { };

    TEST_P(AchievementKeyContractTest, MissingAndEmptyKeysUseDictionaryException) {
        const std::vector<Achievement> original = GetParam()
            ? std::vector<Achievement>{Value("a")} : std::vector<Achievement>{};
        auto collection = AchievementCollection::CreateInternal(original);
        EXPECT_THROW((void)collection["missing"], System::Collections::Generic::KeyNotFoundException);
        EXPECT_THROW((void)collection[""], System::Collections::Generic::KeyNotFoundException);
        EXPECT_EQ(original, std::vector<Achievement>(collection.begin(), collection.end()));
    }

    INSTANTIATE_TEST_SUITE_P(XnaCollectionLookup, AchievementKeyContractTest, testing::Bool());
}
