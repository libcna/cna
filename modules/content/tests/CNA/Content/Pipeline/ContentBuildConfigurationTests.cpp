// SPDX-License-Identifier: MS-PL

#include <cstdint>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include "CNA/Content/Pipeline/ContentBuildConfiguration.hpp"

namespace Pipeline = CNA::Content::Pipeline;

TEST(ContentBuildConfigurationTest, ParsesEveryOverrideAndStableParameterType)
{
    const Pipeline::ContentBuildConfiguration configuration =
        Pipeline::ContentBuildConfiguration::Parse(
            R"json({
                "format":"CNA.ContentPipeline.Config",
                "version":1,
                "assets":{
                    "Textures/wall.png":{
                        "logicalName":"Environment/stone",
                        "importer":"Game.ImageImporter",
                        "processor":"Game.TextureProcessor",
                        "writer":"Game.TextureWriter",
                        "parameters":{
                            "enabled":{"type":"bool","value":true},
                            "offset":{"type":"i64","value":"-7"},
                            "size":{"type":"u64","value":"18446744073709551615"},
                            "scale":{"type":"f64","value":"1.25"},
                            "label":{"type":"string","value":"wall"}
                        }
                    }
                }
            })json",
            "ContentSource/.cna-content.json");

    ASSERT_EQ(configuration.Entries().size(), 1u);
    const Pipeline::ContentAssetBuildConfiguration* asset =
        configuration.Find("Textures/wall.png");
    ASSERT_NE(asset, nullptr);
    EXPECT_EQ(asset->source, "Textures/wall.png");
    EXPECT_EQ(asset->logicalName, "Environment/stone");
    EXPECT_EQ(asset->importer, "Game.ImageImporter");
    EXPECT_EQ(asset->processor, "Game.TextureProcessor");
    EXPECT_EQ(asset->writer, "Game.TextureWriter");
    ASSERT_NE(asset->parameters.Find("enabled"), nullptr);
    EXPECT_EQ(std::get<bool>(*asset->parameters.Find("enabled")), true);
    EXPECT_EQ(std::get<std::int64_t>(*asset->parameters.Find("offset")), -7);
    EXPECT_EQ(std::get<std::uint64_t>(*asset->parameters.Find("size")),
              std::numeric_limits<std::uint64_t>::max());
    EXPECT_DOUBLE_EQ(std::get<double>(*asset->parameters.Find("scale")), 1.25);
    EXPECT_EQ(std::get<std::string>(*asset->parameters.Find("label")), "wall");
    EXPECT_EQ(configuration.Find("Textures/other.png"), nullptr);
}

TEST(ContentBuildConfigurationTest, EmptyAssetMapIsValidForConventionOnlyBuilds)
{
    const Pipeline::ContentBuildConfiguration configuration =
        Pipeline::ContentBuildConfiguration::Parse(
            R"json({"format":"CNA.ContentPipeline.Config","version":1,"assets":{}})json");
    EXPECT_TRUE(configuration.Entries().empty());
}

TEST(ContentBuildConfigurationTest, RejectsUnknownAndRepeatedFieldsWithAssetContext)
{
    const auto parse = [](const std::string& json)
    {
        return Pipeline::ContentBuildConfiguration::Parse(json, "bad-config.json");
    };

    EXPECT_THROW(
        (void)parse(
            R"json({"format":"CNA.ContentPipeline.Config","version":1,"extra":0,"assets":{}})json"),
        std::runtime_error);
    EXPECT_THROW(
        (void)parse(
            R"json({"format":"CNA.ContentPipeline.Config","version":1,"assets":{"a.png":{"processor":"A","processor":"B"}}})json"),
        std::runtime_error);
    EXPECT_THROW(
        (void)parse(
            R"json({"format":"CNA.ContentPipeline.Config","version":1,"assets":{"a.png":{},"a.png":{}}})json"),
        std::runtime_error);
    EXPECT_THROW(
        (void)parse(
            R"json({"format":"CNA.ContentPipeline.Config","version":1,"assets":{"a.png":{"mystery":true}}})json"),
        std::runtime_error);

    try
    {
        (void)parse(
            R"json({"format":"CNA.ContentPipeline.Config","version":1,"assets":{"a.png":{"mystery":true}}})json");
        FAIL() << "unknown asset field was accepted";
    }
    catch (const std::runtime_error& error)
    {
        const std::string message = error.what();
        EXPECT_NE(message.find("bad-config.json"), std::string::npos);
        EXPECT_NE(message.find("asset entry 'a.png'"), std::string::npos);
        EXPECT_NE(message.find("mystery"), std::string::npos);
    }
}

TEST(ContentBuildConfigurationTest, RejectsUnsafeSourceAndLogicalNames)
{
    EXPECT_THROW(
        (void)Pipeline::ContentBuildConfiguration::Parse(
            R"json({"format":"CNA.ContentPipeline.Config","version":1,"assets":{"../a.png":{}}})json"),
        std::runtime_error);
    EXPECT_THROW(
        (void)Pipeline::ContentBuildConfiguration::Parse(
            R"json({"format":"CNA.ContentPipeline.Config","version":1,"assets":{"./a.png":{}}})json"),
        std::runtime_error);
    EXPECT_THROW(
        (void)Pipeline::ContentBuildConfiguration::Parse(
            R"json({"format":"CNA.ContentPipeline.Config","version":1,"assets":{"a.png":{"logicalName":"../escape"}}})json"),
        std::runtime_error);
}

TEST(ContentBuildConfigurationTest, RejectsUnknownOrMistypedParameterValues)
{
    const auto withParameter = [](const std::string& parameter)
    {
        return Pipeline::ContentBuildConfiguration::Parse(
            "{\"format\":\"CNA.ContentPipeline.Config\",\"version\":1,\"assets\":{"
            "\"a.png\":{\"parameters\":{\"p\":" + parameter + "}}}}}");
    };

    EXPECT_THROW((void)withParameter(R"json({"type":"mystery","value":"1"})json"),
                 std::runtime_error);
    EXPECT_THROW((void)withParameter(R"json({"type":"bool","value":"true"})json"),
                 std::runtime_error);
    EXPECT_THROW((void)withParameter(R"json({"type":"u64","value":7})json"),
                 std::runtime_error);
    EXPECT_THROW((void)withParameter(R"json({"type":"i64","value":"7x"})json"),
                 std::runtime_error);
    EXPECT_THROW((void)withParameter(R"json({"type":"f64","value":"nan"})json"),
                 std::runtime_error);
    EXPECT_THROW((void)withParameter(R"json({"type":"string","value":false})json"),
                 std::runtime_error);
    EXPECT_THROW((void)withParameter(R"json({"type":"string","value":"x","extra":1})json"),
                 std::runtime_error);
}
