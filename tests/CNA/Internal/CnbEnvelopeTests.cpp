// SPDX-License-Identifier: MS-PL
//
// plan_cnb.md CNB-3: unit tests for CNB-1 (ParseCnbEnvelope) and CNB-2 (ValidateCnbEnvelope).
// First gtest coverage of any .cnb JSON-envelope-level parsing, independent of any one reader.

#include <gtest/gtest.h>

#include "CNA/Internal/CnbEnvelope.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

using CNA::Internal::CnbEnvelope;
using CNA::Internal::ParseCnbEnvelope;
using CNA::Internal::ValidateCnbEnvelope;
using Microsoft::Xna::Framework::Content::ContentLoadException;

TEST(ParseCnbEnvelopeTest, ValidEnvelopeParsesAllFields)
{
    const std::string json = R"({
        "cnbVersion": 1,
        "type": "SpriteFont",
        "sourceFile": "ahoj.png",
        "lineSpacing": 24
    })";

    const CnbEnvelope env = ParseCnbEnvelope(json);

    EXPECT_TRUE(env.hasCnbVersion);
    EXPECT_EQ(env.cnbVersion, 1);
    EXPECT_TRUE(env.hasType);
    EXPECT_EQ(env.type, "SpriteFont");
    EXPECT_TRUE(env.hasSourceFile);
    EXPECT_EQ(env.sourceFile, "ahoj.png");
}

TEST(ParseCnbEnvelopeTest, MissingCnbVersionLeavesFlagFalse)
{
    const std::string json = R"({"type": "SpriteFont"})";

    const CnbEnvelope env = ParseCnbEnvelope(json);

    EXPECT_FALSE(env.hasCnbVersion);
    EXPECT_TRUE(env.hasType);
}

TEST(ParseCnbEnvelopeTest, MissingTypeLeavesFlagFalse)
{
    const std::string json = R"({"cnbVersion": 1})";

    const CnbEnvelope env = ParseCnbEnvelope(json);

    EXPECT_TRUE(env.hasCnbVersion);
    EXPECT_FALSE(env.hasType);
    EXPECT_EQ(env.type, "");
}

TEST(ParseCnbEnvelopeTest, SourceFileAbsentLeavesFlagFalse)
{
    const std::string json = R"({"cnbVersion": 1, "type": "Texture2D"})";

    const CnbEnvelope env = ParseCnbEnvelope(json);

    EXPECT_FALSE(env.hasSourceFile);
    EXPECT_EQ(env.sourceFile, "");
}

TEST(ParseCnbEnvelopeTest, SourceFilePresentIsParsed)
{
    const std::string json = R"({"cnbVersion": 1, "type": "Texture2D", "sourceFile": "ahoj.png"})";

    const CnbEnvelope env = ParseCnbEnvelope(json);

    EXPECT_TRUE(env.hasSourceFile);
    EXPECT_EQ(env.sourceFile, "ahoj.png");
}

TEST(ParseCnbEnvelopeTest, UnrelatedFieldsAreIgnoredNotErrors)
{
    const std::string json = R"({
        "cnbVersion": 1,
        "type": "Model",
        "bones": [{"name": "root"}],
        "nested": {"a": {"b": 1}}
    })";

    const CnbEnvelope env = ParseCnbEnvelope(json);

    EXPECT_TRUE(env.hasCnbVersion);
    EXPECT_TRUE(env.hasType);
    EXPECT_EQ(env.type, "Model");
}

TEST(ValidateCnbEnvelopeTest, MatchingTypeDoesNotThrow)
{
    const CnbEnvelope env = ParseCnbEnvelope(R"({"cnbVersion": 1, "type": "SpriteFont"})");

    EXPECT_NO_THROW(ValidateCnbEnvelope(env, "SpriteFont", "Fonts/Consolas.cnb"));
}

TEST(ValidateCnbEnvelopeTest, MismatchedTypeThrowsNamingBothTypes)
{
    const CnbEnvelope env = ParseCnbEnvelope(R"({"cnbVersion": 1, "type": "Model"})");

    try
    {
        ValidateCnbEnvelope(env, "SpriteFont", "Fonts/Consolas.cnb");
        FAIL() << "expected ContentLoadException";
    }
    catch (const ContentLoadException& e)
    {
        const std::string message = e.what();
        EXPECT_NE(message.find("Model"), std::string::npos);
        EXPECT_NE(message.find("SpriteFont"), std::string::npos);
    }
}

TEST(ValidateCnbEnvelopeTest, MissingCnbVersionThrows)
{
    const CnbEnvelope env = ParseCnbEnvelope(R"({"type": "SpriteFont"})");

    EXPECT_THROW(ValidateCnbEnvelope(env, "SpriteFont", "Fonts/Consolas.cnb"), ContentLoadException);
}

TEST(ValidateCnbEnvelopeTest, MissingTypeThrows)
{
    const CnbEnvelope env = ParseCnbEnvelope(R"({"cnbVersion": 1})");

    EXPECT_THROW(ValidateCnbEnvelope(env, "SpriteFont", "Fonts/Consolas.cnb"), ContentLoadException);
}
