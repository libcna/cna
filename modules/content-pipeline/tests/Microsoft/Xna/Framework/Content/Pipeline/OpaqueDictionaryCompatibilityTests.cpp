// SPDX-License-Identifier: MS-PL
// Genuine contracts: cna-demos/docs/repair-evidence/content/OpaqueDictionaryOracle.cs.
#include <gtest/gtest.h>
#include <cstdint>
#include <memory>
#include <string>
#include <typeinfo>
#include <vector>
#include "Microsoft/Xna/Framework/Content/Pipeline/OpaqueDataDictionary.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentObject.hpp"
#include "System/ArgumentNullException.hpp"

using namespace Microsoft::Xna::Framework::Content::Pipeline;
namespace
{
class ObservedDictionary final : public NamedValueDictionary<std::shared_ptr<int>>
{
public:
    int adds = 0;
    int sets = 0;
    void DirectSet(const std::string& key, const std::shared_ptr<int>& value)
    {
        NamedValueDictionary::SetItem(key, value);
    }
protected:
    void AddItem(const std::string& key, const std::shared_ptr<int>& value) override
    {
        ++adds;
        NamedValueDictionary::AddItem(key, value);
    }
    void SetItem(const std::string& key, const std::shared_ptr<int>& value) override
    {
        ++sets;
        NamedValueDictionary::SetItem(key, value);
    }
};

template<class Action> void ExpectNullArgument(Action action, const char* parameter)
{
    try { action(); ADD_FAILURE() << "Expected ArgumentNullException for " << parameter; }
    catch (const System::ArgumentException& error)
    {
        EXPECT_EQ(typeid(error), typeid(System::ArgumentNullException));
        EXPECT_EQ(error.getParamNameProperty(), parameter);
    }
    catch (const std::exception& error) { ADD_FAILURE() << error.what(); }
}
}

TEST(OpaqueDictionaryCompatibilityTest, EmptyValuesAreRejectedWithoutMutationOrXmlChanges)
{
    OpaqueDataDictionary data;
    data.Add("Seed", Box<std::int32_t>(1));
    data.Add("Text", Box<std::string>("retained"));
    const std::string original = data.GetContentAsXml();
    ExpectNullArgument([&] { data.Add("New", ContentObject{}); }, "value");
    ExpectNullArgument([&] { data.Add("Seed", ContentObject{}); }, "value");
    ExpectNullArgument([&] { data.Set("Seed", ContentObject{}); }, "value");
    ExpectNullArgument([&] { data.Set("New", ContentObject{}); }, "value");
    EXPECT_EQ(data.getCountProperty(), 2);
    EXPECT_EQ(data.GetValue<std::int32_t>("Seed", -1), 1);
    EXPECT_EQ(data.GetContentAsXml(), original);
}

TEST(OpaqueDictionaryCompatibilityTest, EmptyKeyPrecedesNullValueInPublicMutationDoors)
{
    OpaqueDataDictionary data;
    ExpectNullArgument([&] { data.Add("", ContentObject{}); }, "key");
    ExpectNullArgument([&] { data.Set("", ContentObject{}); }, "key");
    ExpectNullArgument([&] { (void)data.Remove(""); }, "key");
    EXPECT_EQ(data.getCountProperty(), 0);
    NamedValueDictionary<std::shared_ptr<int>> typed;
    ExpectNullArgument([&] { typed.Add("", nullptr); }, "key");
    ExpectNullArgument([&] { typed.Set("", nullptr); }, "key");
    ExpectNullArgument([&] { typed.Add("Value", nullptr); }, "value");
    EXPECT_EQ(typed.getCountProperty(), 0);
}

TEST(OpaqueDictionaryCompatibilityTest, SuccessfulSetInsertsAndPreservesReplacementOrder)
{
    OpaqueDataDictionary data;
    data.Add("Seed", Box<std::int32_t>(1));
    data.Add("Text", Box<std::string>("retained"));
    data.Set("New", Box<std::int32_t>(2));
    data.Set("Seed", Box<std::int32_t>(3));
    EXPECT_EQ(data.getCountProperty(), 3);
    EXPECT_EQ(data.GetValue<std::int32_t>("New", -1), 2);
    EXPECT_EQ(data.GetValue<std::int32_t>("Seed", -1), 3);
    EXPECT_EQ(data.getKeysProperty(), (std::vector<std::string>{"Seed", "Text", "New"}));
    NamedValueDictionary<std::shared_ptr<int>> typed;
    typed.Set("Value", std::make_shared<int>(4));
    ASSERT_EQ(typed.getCountProperty(), 1);
    EXPECT_EQ(*typed["Value"], 4);
}

TEST(OpaqueDictionaryCompatibilityTest, SetterDispatchesSetHookForNewAndExistingKeys)
{
    ObservedDictionary data;
    data.Set("New", std::make_shared<int>(1));
    EXPECT_EQ(data.sets, 1);
    EXPECT_EQ(data.adds, 0);
    EXPECT_EQ(*data["New"], 1);
    data.Set("New", std::make_shared<int>(2));
    EXPECT_EQ(data.sets, 2);
    EXPECT_EQ(data.adds, 0);
    EXPECT_EQ(*data["New"], 2);
    data.Add("Added", std::make_shared<int>(3));
    EXPECT_EQ(data.adds, 1);
    EXPECT_EQ(data.sets, 2);
}

TEST(OpaqueDictionaryCompatibilityTest, ProtectedSetInsertsAndValidatesBeforeMutation)
{
    ObservedDictionary data;
    data.Add("Seed", std::make_shared<int>(1));
    ASSERT_NO_THROW(data.DirectSet("Direct", std::make_shared<int>(4)));
    EXPECT_EQ(*data["Direct"], 4);
    ExpectNullArgument([&] { data.DirectSet("", nullptr); }, "key");
    ExpectNullArgument([&] { data.DirectSet("Missing", nullptr); }, "value");
    EXPECT_EQ(data.getCountProperty(), 2);
    EXPECT_EQ(data.getKeysProperty(), (std::vector<std::string>{"Seed", "Direct"}));
}
