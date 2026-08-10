#include <gtest/gtest.h>

#include "CNA/Internal/Renderers/Glide/GlideExtensionCapabilities.hpp"

using CNA::Internal::Renderers::Glide::GlideExtensionListContains;
using CNA::Internal::Renderers::Glide::ParseGlideExtensionList;

TEST(GlideExtensionCapabilitiesTest, ParsesASpaceSeparatedExtensionList)
{
    const auto extensions = ParseGlideExtensionList("CHROMARANGE TEXCHROMA TEXMIRROR");

    ASSERT_EQ(extensions.size(), 3u);
    EXPECT_EQ(extensions[0], "CHROMARANGE");
    EXPECT_EQ(extensions[1], "TEXCHROMA");
    EXPECT_EQ(extensions[2], "TEXMIRROR");
}

TEST(GlideExtensionCapabilitiesTest, ASingleSpaceMeansNoExtensionsPerTheSpec)
{
    EXPECT_TRUE(ParseGlideExtensionList(" ").empty());
}

TEST(GlideExtensionCapabilitiesTest, HandlesEmptyStringAndNullDefensively)
{
    EXPECT_TRUE(ParseGlideExtensionList("").empty());
    EXPECT_TRUE(ParseGlideExtensionList(nullptr).empty());
}

TEST(GlideExtensionCapabilitiesTest, ToleratesRepeatedOrSurroundingSpaces)
{
    const auto extensions = ParseGlideExtensionList("  TEXMIRROR   PALETTE6666  ");

    ASSERT_EQ(extensions.size(), 2u);
    EXPECT_EQ(extensions[0], "TEXMIRROR");
    EXPECT_EQ(extensions[1], "PALETTE6666");
}

TEST(GlideExtensionCapabilitiesTest, SingleExtensionWithNoSurroundingSpaces)
{
    const auto extensions = ParseGlideExtensionList("FOGCOORD");

    ASSERT_EQ(extensions.size(), 1u);
    EXPECT_EQ(extensions[0], "FOGCOORD");
}

TEST(GlideExtensionCapabilitiesTest, ContainsChecksExactNameMatch)
{
    const std::vector<std::string> extensions{"CHROMARANGE", "TEXCHROMA"};

    EXPECT_TRUE(GlideExtensionListContains(extensions, "TEXCHROMA"));
    EXPECT_FALSE(GlideExtensionListContains(extensions, "TEXMIRROR"));
    EXPECT_FALSE(GlideExtensionListContains(extensions, "CHROMA"));
    EXPECT_FALSE(GlideExtensionListContains({}, "CHROMARANGE"));
}
