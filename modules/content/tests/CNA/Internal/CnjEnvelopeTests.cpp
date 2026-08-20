// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnj.md CNB-3: unit tests for CNB-1 (ParseCnjEnvelope) and CNB-2 (ValidateCnjEnvelope).
// First gtest coverage of any .cnj JSON-envelope-level parsing, independent of any one reader.

#include <gtest/gtest.h>

#include "CNA/Internal/CnjEnvelope.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

using CNA::Internal::CnjEnvelope;
using CNA::Internal::ParseCnjEnvelope;
using CNA::Internal::ValidateCnjEnvelope;
using Microsoft::Xna::Framework::Content::ContentLoadException;

TEST(ParseCnjEnvelopeTest, ValidEnvelopeParsesAllFields)
{
    const std::string json = R"({
        "cnjVersion": 1,
        "type": "SpriteFont",
        "sourceFile": "ahoj.png",
        "lineSpacing": 24
    })";

    const CnjEnvelope env = ParseCnjEnvelope(json);

    EXPECT_TRUE(env.hasCnjVersion);
    EXPECT_EQ(env.cnjVersion, 1);
    EXPECT_TRUE(env.hasType);
    EXPECT_EQ(env.type, "SpriteFont");
    EXPECT_TRUE(env.hasSourceFile);
    EXPECT_EQ(env.sourceFile, "ahoj.png");
}

TEST(ParseCnjEnvelopeTest, MissingCnjVersionLeavesFlagFalse)
{
    const std::string json = R"({"type": "SpriteFont"})";

    const CnjEnvelope env = ParseCnjEnvelope(json);

    EXPECT_FALSE(env.hasCnjVersion);
    EXPECT_TRUE(env.hasType);
}

TEST(ParseCnjEnvelopeTest, MissingTypeLeavesFlagFalse)
{
    const std::string json = R"({"cnjVersion": 1})";

    const CnjEnvelope env = ParseCnjEnvelope(json);

    EXPECT_TRUE(env.hasCnjVersion);
    EXPECT_FALSE(env.hasType);
    EXPECT_EQ(env.type, "");
}

TEST(ParseCnjEnvelopeTest, SourceFileAbsentLeavesFlagFalse)
{
    const std::string json = R"({"cnjVersion": 1, "type": "Texture2D"})";

    const CnjEnvelope env = ParseCnjEnvelope(json);

    EXPECT_FALSE(env.hasSourceFile);
    EXPECT_EQ(env.sourceFile, "");
}

TEST(ParseCnjEnvelopeTest, SourceFilePresentIsParsed)
{
    const std::string json = R"({"cnjVersion": 1, "type": "Texture2D", "sourceFile": "ahoj.png"})";

    const CnjEnvelope env = ParseCnjEnvelope(json);

    EXPECT_TRUE(env.hasSourceFile);
    EXPECT_EQ(env.sourceFile, "ahoj.png");
}

TEST(ParseCnjEnvelopeTest, UnrelatedFieldsAreIgnoredNotErrors)
{
    const std::string json = R"({
        "cnjVersion": 1,
        "type": "Model",
        "bones": [{"name": "root"}],
        "nested": {"a": {"b": 1}}
    })";

    const CnjEnvelope env = ParseCnjEnvelope(json);

    EXPECT_TRUE(env.hasCnjVersion);
    EXPECT_TRUE(env.hasType);
    EXPECT_EQ(env.type, "Model");
}

TEST(ParseCnjEnvelopeTest, NestedTypeFieldIsNotMistakenForTopLevelType)
{
    // No top-level "type" -- only a same-named field nested inside "meshes". A naive
    // first-occurrence-anywhere scan would wrongly report hasType == true here.
    const std::string json = R"({
        "cnjVersion": 1,
        "meshes": [
            { "type": "NestedDecoy", "name": "part0" }
        ]
    })";

    const CnjEnvelope env = ParseCnjEnvelope(json);

    EXPECT_TRUE(env.hasCnjVersion);
    EXPECT_FALSE(env.hasType);
}

TEST(ParseCnjEnvelopeTest, NestedCnjVersionFieldIsNotMistakenForTopLevelCnjVersion)
{
    const std::string json = R"({
        "type": "Model",
        "meshes": [
            { "cnjVersion": 999, "name": "part0" }
        ]
    })";

    const CnjEnvelope env = ParseCnjEnvelope(json);

    EXPECT_FALSE(env.hasCnjVersion);
    EXPECT_TRUE(env.hasType);
    EXPECT_EQ(env.type, "Model");
}

TEST(ParseCnjEnvelopeTest, RealTopLevelFieldFoundEvenWhenNestedDecoyPrecedesItTextually)
{
    // The genuine top-level "type" appears AFTER a nested decoy with the same key name -- a
    // naive first-occurrence-anywhere scan would return "NestedDecoy" instead of "Model".
    const std::string json = R"({
        "meshes": [
            { "type": "NestedDecoy" }
        ],
        "type": "Model",
        "cnjVersion": 1
    })";

    const CnjEnvelope env = ParseCnjEnvelope(json);

    EXPECT_TRUE(env.hasType);
    EXPECT_EQ(env.type, "Model");
}

TEST(ParseCnjEnvelopeTest, EscapedQuoteInTypeIsDecodedNotTruncated)
{
    // The old hand-rolled scanner found the value by searching for the next '"' byte, which
    // would truncate at an escaped quote instead of decoding it. A real JSON parser must decode
    // \" correctly.
    const std::string json = R"({"cnjVersion": 1, "type": "Sprite\"Font"})";

    const CnjEnvelope env = ParseCnjEnvelope(json);

    EXPECT_TRUE(env.hasType);
    EXPECT_EQ(env.type, "Sprite\"Font");
}

TEST(ParseCnjEnvelopeTest, MalformedJsonSetsParseErrorDetail)
{
    const CnjEnvelope env = ParseCnjEnvelope("{not valid json at all");

    EXPECT_FALSE(env.hasCnjVersion);
    EXPECT_FALSE(env.hasType);
    EXPECT_FALSE(env.parseErrorDetail.empty());
}

TEST(ParseCnjEnvelopeTest, NonObjectRootSetsParseErrorDetail)
{
    const CnjEnvelope env = ParseCnjEnvelope(R"(["not", "an", "object"])");

    EXPECT_FALSE(env.hasCnjVersion);
    EXPECT_FALSE(env.hasType);
    EXPECT_FALSE(env.parseErrorDetail.empty());
}

TEST(ValidateCnjEnvelopeTest, MalformedJsonThrowsMentioningParseFailure)
{
    const CnjEnvelope env = ParseCnjEnvelope("not json { at all");

    try
    {
        ValidateCnjEnvelope(env, "SpriteFont", "broken.cnj");
        FAIL() << "expected ContentLoadException";
    }
    catch (const ContentLoadException& e)
    {
        const std::string message = e.what();
        EXPECT_NE(message.find("JSON"), std::string::npos);
    }
}

TEST(ValidateCnjEnvelopeTest, NonObjectRootThrows)
{
    const CnjEnvelope env = ParseCnjEnvelope("42");

    EXPECT_THROW(ValidateCnjEnvelope(env, "SpriteFont", "wrong.cnj"), ContentLoadException);
}

TEST(ValidateCnjEnvelopeTest, ZeroCnjVersionThrows)
{
    const CnjEnvelope env = ParseCnjEnvelope(R"({"cnjVersion": 0, "type": "SpriteFont"})");

    EXPECT_THROW(ValidateCnjEnvelope(env, "SpriteFont", "wrong.cnj"), ContentLoadException);
}

TEST(ValidateCnjEnvelopeTest, NegativeCnjVersionThrows)
{
    const CnjEnvelope env = ParseCnjEnvelope(R"({"cnjVersion": -1, "type": "SpriteFont"})");

    EXPECT_THROW(ValidateCnjEnvelope(env, "SpriteFont", "wrong.cnj"), ContentLoadException);
}

// plans/plan_gltf.md GLTF-129 (Phase 5): the version ceiling is a PER-TYPE argument, not a global
// constant. Model understands version 2 (the "bones" hierarchy and per-mesh "parentBone"); every
// other type still understands only version 1. The four cases below are the whole contract --
// raising the ceiling globally would have made SpriteFont silently accept a version whose meaning
// its own reader has never defined.

TEST(ValidateCnjEnvelopeTest, ModelAcceptsVersionOne)
{
    const CnjEnvelope env = ParseCnjEnvelope(R"({"cnjVersion": 1, "type": "Model"})");

    EXPECT_NO_THROW(ValidateCnjEnvelope(env, "Model", "v1.cnj", /*maxVersion=*/2));
}

TEST(ValidateCnjEnvelopeTest, ModelAcceptsVersionTwo)
{
    const CnjEnvelope env = ParseCnjEnvelope(R"({"cnjVersion": 2, "type": "Model"})");

    EXPECT_NO_THROW(ValidateCnjEnvelope(env, "Model", "v2.cnj", /*maxVersion=*/2));
}

TEST(ValidateCnjEnvelopeTest, ModelRejectsVersionThreeNamingTheSupportedRange)
{
    const CnjEnvelope env = ParseCnjEnvelope(R"({"cnjVersion": 3, "type": "Model"})");

    try
    {
        ValidateCnjEnvelope(env, "Model", "v3.cnj", /*maxVersion=*/2);
        FAIL() << "expected ContentLoadException";
    }
    catch (const ContentLoadException& e)
    {
        const std::string message = e.what();
        EXPECT_NE(std::string::npos, message.find("v3.cnj"));
        EXPECT_NE(std::string::npos, message.find("versions 1 to 2"))
            << "the message must name the range this type actually supports: " << message;
    }
}

TEST(ValidateCnjEnvelopeTest, ARaisedCeilingDoesNotLeakToOtherTypes)
{
    // The regression this guards: a Model-shaped version reaching a reader that never defined it.
    // SpriteFont's own ceiling stays 1 whatever Model's is, because the ceiling travels with the
    // call, not with the envelope.
    const CnjEnvelope env = ParseCnjEnvelope(R"({"cnjVersion": 2, "type": "SpriteFont"})");

    EXPECT_THROW(ValidateCnjEnvelope(env, "SpriteFont", "leak.cnj"), ContentLoadException);
    EXPECT_NO_THROW(ValidateCnjEnvelope(
        ParseCnjEnvelope(R"({"cnjVersion": 1, "type": "SpriteFont"})"), "SpriteFont", "ok.cnj"));
}

TEST(ValidateCnjEnvelopeTest, NonIntegerVersionIsRejectedEvenInsideARaisedRange)
{
    // cnjVersionRaw, not the truncating cnjVersion: 1.5 must not slip through as "1".
    const CnjEnvelope env = ParseCnjEnvelope(R"({"cnjVersion": 1.5, "type": "Model"})");

    EXPECT_THROW(ValidateCnjEnvelope(env, "Model", "fractional.cnj", /*maxVersion=*/2),
                 ContentLoadException);
}

TEST(ValidateCnjEnvelopeTest, FutureCnjVersionThrowsNamingActualValue)
{
    const CnjEnvelope env = ParseCnjEnvelope(R"({"cnjVersion": 2, "type": "SpriteFont"})");

    try
    {
        ValidateCnjEnvelope(env, "SpriteFont", "future.cnj");
        FAIL() << "expected ContentLoadException";
    }
    catch (const ContentLoadException& e)
    {
        const std::string message = e.what();
        EXPECT_NE(message.find('2'), std::string::npos);
        EXPECT_NE(message.find('1'), std::string::npos);
    }
}

TEST(ValidateCnjEnvelopeTest, DecimalCnjVersionThrows)
{
    const CnjEnvelope env = ParseCnjEnvelope(R"({"cnjVersion": 1.5, "type": "SpriteFont"})");

    EXPECT_THROW(ValidateCnjEnvelope(env, "SpriteFont", "wrong.cnj"), ContentLoadException);
}

TEST(ValidateCnjEnvelopeTest, TrailingGarbageCnjVersionIsAParseError)
{
    // "1abc" is not a valid JSON number token at all -- the whole document fails to parse,
    // rather than std::stoi silently truncating it to 1 (the old ad-hoc scanner's behavior).
    const CnjEnvelope env = ParseCnjEnvelope(R"({"cnjVersion": 1abc, "type": "SpriteFont"})");

    EXPECT_TRUE(env.parseErrorDetail.empty() == false || !env.hasCnjVersion);
    EXPECT_THROW(ValidateCnjEnvelope(env, "SpriteFont", "wrong.cnj"), ContentLoadException);
}

TEST(ValidateCnjEnvelopeTest, MatchingTypeDoesNotThrow)
{
    const CnjEnvelope env = ParseCnjEnvelope(R"({"cnjVersion": 1, "type": "SpriteFont"})");

    EXPECT_NO_THROW(ValidateCnjEnvelope(env, "SpriteFont", "Fonts/Consolas.cnj"));
}

TEST(ValidateCnjEnvelopeTest, MismatchedTypeThrowsNamingBothTypes)
{
    const CnjEnvelope env = ParseCnjEnvelope(R"({"cnjVersion": 1, "type": "Model"})");

    try
    {
        ValidateCnjEnvelope(env, "SpriteFont", "Fonts/Consolas.cnj");
        FAIL() << "expected ContentLoadException";
    }
    catch (const ContentLoadException& e)
    {
        const std::string message = e.what();
        EXPECT_NE(message.find("Model"), std::string::npos);
        EXPECT_NE(message.find("SpriteFont"), std::string::npos);
    }
}

TEST(ValidateCnjEnvelopeTest, MissingCnjVersionThrows)
{
    const CnjEnvelope env = ParseCnjEnvelope(R"({"type": "SpriteFont"})");

    EXPECT_THROW(ValidateCnjEnvelope(env, "SpriteFont", "Fonts/Consolas.cnj"), ContentLoadException);
}

TEST(ValidateCnjEnvelopeTest, MissingTypeThrows)
{
    const CnjEnvelope env = ParseCnjEnvelope(R"({"cnjVersion": 1})");

    EXPECT_THROW(ValidateCnjEnvelope(env, "SpriteFont", "Fonts/Consolas.cnj"), ContentLoadException);
}
