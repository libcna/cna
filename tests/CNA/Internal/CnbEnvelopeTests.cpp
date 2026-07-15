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

TEST(ParseCnbEnvelopeTest, NestedTypeFieldIsNotMistakenForTopLevelType)
{
    // No top-level "type" -- only a same-named field nested inside "meshes". A naive
    // first-occurrence-anywhere scan would wrongly report hasType == true here.
    const std::string json = R"({
        "cnbVersion": 1,
        "meshes": [
            { "type": "NestedDecoy", "name": "part0" }
        ]
    })";

    const CnbEnvelope env = ParseCnbEnvelope(json);

    EXPECT_TRUE(env.hasCnbVersion);
    EXPECT_FALSE(env.hasType);
}

TEST(ParseCnbEnvelopeTest, NestedCnbVersionFieldIsNotMistakenForTopLevelCnbVersion)
{
    const std::string json = R"({
        "type": "Model",
        "meshes": [
            { "cnbVersion": 999, "name": "part0" }
        ]
    })";

    const CnbEnvelope env = ParseCnbEnvelope(json);

    EXPECT_FALSE(env.hasCnbVersion);
    EXPECT_TRUE(env.hasType);
    EXPECT_EQ(env.type, "Model");
}

TEST(ParseCnbEnvelopeTest, RealTopLevelFieldFoundEvenWhenNestedDecoyPrecedesItTextually)
{
    // The genuine top-level "type" appears AFTER a nested decoy with the same key name -- a
    // naive first-occurrence-anywhere scan would return "NestedDecoy" instead of "Model".
    const std::string json = R"({
        "meshes": [
            { "type": "NestedDecoy" }
        ],
        "type": "Model",
        "cnbVersion": 1
    })";

    const CnbEnvelope env = ParseCnbEnvelope(json);

    EXPECT_TRUE(env.hasType);
    EXPECT_EQ(env.type, "Model");
}

TEST(ParseCnbEnvelopeTest, EscapedQuoteInTypeIsDecodedNotTruncated)
{
    // The old hand-rolled scanner found the value by searching for the next '"' byte, which
    // would truncate at an escaped quote instead of decoding it. A real JSON parser must decode
    // \" correctly.
    const std::string json = R"({"cnbVersion": 1, "type": "Sprite\"Font"})";

    const CnbEnvelope env = ParseCnbEnvelope(json);

    EXPECT_TRUE(env.hasType);
    EXPECT_EQ(env.type, "Sprite\"Font");
}

TEST(ParseCnbEnvelopeTest, MalformedJsonSetsParseErrorDetail)
{
    const CnbEnvelope env = ParseCnbEnvelope("{not valid json at all");

    EXPECT_FALSE(env.hasCnbVersion);
    EXPECT_FALSE(env.hasType);
    EXPECT_FALSE(env.parseErrorDetail.empty());
}

TEST(ParseCnbEnvelopeTest, NonObjectRootSetsParseErrorDetail)
{
    const CnbEnvelope env = ParseCnbEnvelope(R"(["not", "an", "object"])");

    EXPECT_FALSE(env.hasCnbVersion);
    EXPECT_FALSE(env.hasType);
    EXPECT_FALSE(env.parseErrorDetail.empty());
}

TEST(ValidateCnbEnvelopeTest, MalformedJsonThrowsMentioningParseFailure)
{
    const CnbEnvelope env = ParseCnbEnvelope("not json { at all");

    try
    {
        ValidateCnbEnvelope(env, "SpriteFont", "broken.cnb");
        FAIL() << "expected ContentLoadException";
    }
    catch (const ContentLoadException& e)
    {
        const std::string message = e.what();
        EXPECT_NE(message.find("JSON"), std::string::npos);
    }
}

TEST(ValidateCnbEnvelopeTest, NonObjectRootThrows)
{
    const CnbEnvelope env = ParseCnbEnvelope("42");

    EXPECT_THROW(ValidateCnbEnvelope(env, "SpriteFont", "wrong.cnb"), ContentLoadException);
}

TEST(ValidateCnbEnvelopeTest, ZeroCnbVersionThrows)
{
    const CnbEnvelope env = ParseCnbEnvelope(R"({"cnbVersion": 0, "type": "SpriteFont"})");

    EXPECT_THROW(ValidateCnbEnvelope(env, "SpriteFont", "wrong.cnb"), ContentLoadException);
}

TEST(ValidateCnbEnvelopeTest, NegativeCnbVersionThrows)
{
    const CnbEnvelope env = ParseCnbEnvelope(R"({"cnbVersion": -1, "type": "SpriteFont"})");

    EXPECT_THROW(ValidateCnbEnvelope(env, "SpriteFont", "wrong.cnb"), ContentLoadException);
}

TEST(ValidateCnbEnvelopeTest, FutureCnbVersionThrowsNamingActualValue)
{
    const CnbEnvelope env = ParseCnbEnvelope(R"({"cnbVersion": 2, "type": "SpriteFont"})");

    try
    {
        ValidateCnbEnvelope(env, "SpriteFont", "future.cnb");
        FAIL() << "expected ContentLoadException";
    }
    catch (const ContentLoadException& e)
    {
        const std::string message = e.what();
        EXPECT_NE(message.find('2'), std::string::npos);
        EXPECT_NE(message.find('1'), std::string::npos);
    }
}

TEST(ValidateCnbEnvelopeTest, DecimalCnbVersionThrows)
{
    const CnbEnvelope env = ParseCnbEnvelope(R"({"cnbVersion": 1.5, "type": "SpriteFont"})");

    EXPECT_THROW(ValidateCnbEnvelope(env, "SpriteFont", "wrong.cnb"), ContentLoadException);
}

TEST(ValidateCnbEnvelopeTest, TrailingGarbageCnbVersionIsAParseError)
{
    // "1abc" is not a valid JSON number token at all -- the whole document fails to parse,
    // rather than std::stoi silently truncating it to 1 (the old ad-hoc scanner's behavior).
    const CnbEnvelope env = ParseCnbEnvelope(R"({"cnbVersion": 1abc, "type": "SpriteFont"})");

    EXPECT_TRUE(env.parseErrorDetail.empty() == false || !env.hasCnbVersion);
    EXPECT_THROW(ValidateCnbEnvelope(env, "SpriteFont", "wrong.cnb"), ContentLoadException);
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
