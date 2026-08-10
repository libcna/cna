// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/LaunchParameters.hpp"

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

TEST(LaunchParametersTest, SkipsArgTooShort)
{
    // "-r:" trims to "r:" which is length 2 — below the minimum of 3
    LaunchParameters p(std::vector<std::string>{"-r:"});
    EXPECT_FALSE(p.ContainsKey("r"));
}

TEST(LaunchParametersTest, SkipsArgWithNoColon)
{
    LaunchParameters p(std::vector<std::string>{"-rFNA"});
    EXPECT_FALSE(p.ContainsKey("rFNA"));
    EXPECT_TRUE(p.empty());
}

TEST(LaunchParametersTest, SkipsArgWhereColonIsLastChar)
{
    // "rv:" — colon at last position means no value, should be skipped
    LaunchParameters p(std::vector<std::string>{"rv:"});
    EXPECT_FALSE(p.ContainsKey("rv"));
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
