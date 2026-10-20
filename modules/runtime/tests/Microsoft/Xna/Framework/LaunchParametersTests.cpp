// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/LaunchParameters.hpp"
#include "System/ArgumentException.hpp"

using namespace Microsoft::Xna::Framework;

TEST(LaunchParametersTest, ParsesNormalDashArg)
{
    LaunchParameters p(std::vector<std::string>{"-r:FNA.dll"});
    EXPECT_TRUE(p.ContainsKey("r"));
    EXPECT_EQ(p.at("r"), "FNA.dll");
}

TEST(LaunchParametersTest, ParsesDoubleDashArg)
{
    LaunchParameters p(std::vector<std::string>{"--key:value"});
    EXPECT_TRUE(p.ContainsKey("key"));
    EXPECT_EQ(p.at("key"), "value");
}

TEST(LaunchParametersTest, ParsesSlashArg)
{
    LaunchParameters p(std::vector<std::string>{"/key:value"});
    EXPECT_TRUE(p.ContainsKey("key"));
    EXPECT_EQ(p.at("key"), "value");
}

TEST(LaunchParametersTest, RetainsSingleCharacterKeyWithEmptyValue)
{
    // Genuine XNA retains the empty value; there is no minimum three-byte length.
    LaunchParameters p(std::vector<std::string>{"-r:"});
    ASSERT_TRUE(p.ContainsKey("r"));
    EXPECT_TRUE(p.at("r").empty());
}

TEST(LaunchParametersTest, RetainsArgWithNoColonAsEmptyValue)
{
    LaunchParameters p(std::vector<std::string>{"-rFNA"});
    ASSERT_TRUE(p.ContainsKey("rFNA"));
    EXPECT_TRUE(p.at("rFNA").empty());
    EXPECT_EQ(p.size(), 1U);
}

TEST(LaunchParametersTest, RetainsArgWhereColonIsLastChar)
{
    // Genuine XNA keeps a trailing-colon key with an empty string value.
    LaunchParameters p(std::vector<std::string>{"rv:"});
    ASSERT_TRUE(p.ContainsKey("rv"));
    EXPECT_TRUE(p.at("rv").empty());
}

TEST(LaunchParametersTest, DuplicateKeyKeepsFirst)
{
    LaunchParameters p(std::vector<std::string>{"-r:first", "-r:second"});
    ASSERT_TRUE(p.ContainsKey("r"));
    EXPECT_EQ(p.at("r"), "first");
}

TEST(LaunchParametersTest, MultipleColonsUsesFirst)
{
    // FNA: "You can have multiple :, only the first matters"
    LaunchParameters p(std::vector<std::string>{"-r:FNA:2"});
    ASSERT_TRUE(p.ContainsKey("r"));
    EXPECT_EQ(p.at("r"), "FNA:2");
}

TEST(LaunchParametersTest, ContainsKeyFalseForMissing)
{
    LaunchParameters p(std::vector<std::string>{});
    EXPECT_FALSE(p.ContainsKey("missing"));
}

TEST(LaunchParametersTest, AddStoresValue)
{
    LaunchParameters p(std::vector<std::string>{});
    p.Add("k", "v");
    ASSERT_TRUE(p.ContainsKey("k"));
    EXPECT_EQ(p.at("k"), "v");
}

TEST(LaunchParametersTest, MultipleDistinctArgs)
{
    LaunchParameters p(std::vector<std::string>{"-a:1", "-b:2", "-c:3"});
    EXPECT_EQ(p.at("a"), "1");
    EXPECT_EQ(p.at("b"), "2");
    EXPECT_EQ(p.at("c"), "3");
}

TEST(LaunchParametersTest, MinimalValidArg)
{
    // 3-char string after trim: "k:v"
    LaunchParameters p(std::vector<std::string>{"k:v"});
    ASSERT_TRUE(p.ContainsKey("k"));
    EXPECT_EQ(p.at("k"), "v");
}

// Genuine ComponentContractsOracle.cs: parsing preserves empty values and the
// first duplicate, whereas direct Dictionary.Add must reject a duplicate.
TEST(LaunchParametersTest, XnaEmptyValueArgumentsAreRetainedWithoutDroppingExistingControls)
{
    const LaunchParameters p(std::vector<std::string>{
        "/level:3", "-difficulty:hard", "--windowed", "/level:9", "plain:value",
        "/mixed:a:b", "-Case:Upper", "-case:lower", "/empty:"});
    ASSERT_EQ(p.size(), 8U);
    EXPECT_EQ(p.at("level"), "3");
    EXPECT_EQ(p.at("difficulty"), "hard");
    EXPECT_EQ(p.at("plain"), "value");
    EXPECT_EQ(p.at("mixed"), "a:b");
    EXPECT_EQ(p.at("Case"), "Upper");
    EXPECT_EQ(p.at("case"), "lower");
    EXPECT_TRUE(p.at("windowed").empty());
    EXPECT_TRUE(p.at("empty").empty());
}

TEST(LaunchParametersTest, XnaParsingRejectsEmptyKeysButAcceptsSingleCharacterKeys)
{
    const LaunchParameters p(std::vector<std::string>{"", "---", "/", ":ignored", ":",
                                                     "-x", "/y:", "-x:later"});
    ASSERT_EQ(p.size(), 2U);
    EXPECT_TRUE(p.at("x").empty());
    EXPECT_TRUE(p.at("y").empty());
    EXPECT_FALSE(p.ContainsKey(""));
    EXPECT_FALSE(p.ContainsKey(":ignored"));
}

TEST(LaunchParametersTest, XnaDirectDuplicateAddThrowsAndPreservesEveryStoredValue)
{
    LaunchParameters p(std::vector<std::string>{});
    p.Add("key", "first");
    p.Add("", "empty key");
    p.Add("blank", "");
    EXPECT_THROW(p.Add("key", "replacement"), System::ArgumentException);
    EXPECT_THROW(p.Add("", "replacement"), System::ArgumentException);
    EXPECT_EQ(p.size(), 3U);
    EXPECT_EQ(p.at("key"), "first");
    EXPECT_EQ(p.at(""), "empty key");
    EXPECT_TRUE(p.at("blank").empty());
}
