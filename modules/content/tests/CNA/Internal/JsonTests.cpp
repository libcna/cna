// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnj.md CNB-35: unit tests for the minimal JSON parser backing .cnj envelope parsing.

#include <gtest/gtest.h>

#include "CNA/Internal/Json.hpp"

using CNA::Internal::JsonParseException;
using CNA::Internal::JsonType;
using CNA::Internal::JsonValue;
using CNA::Internal::ParseJson;

TEST(JsonParseTest, ParsesEmptyObject)
{
    const JsonValue v = ParseJson("{}");
    EXPECT_TRUE(v.IsObject());
    EXPECT_TRUE(v.objectValue.empty());
}

TEST(JsonParseTest, ParsesFlatObject)
{
    const JsonValue v = ParseJson(R"({"a": 1, "b": "hello", "c": true, "d": null})");
    ASSERT_TRUE(v.IsObject());

    const JsonValue* a = v.FindMember("a");
    ASSERT_NE(a, nullptr);
    EXPECT_TRUE(a->IsNumber());
    EXPECT_DOUBLE_EQ(a->numberValue, 1.0);

    const JsonValue* b = v.FindMember("b");
    ASSERT_NE(b, nullptr);
    EXPECT_TRUE(b->IsString());
    EXPECT_EQ(b->stringValue, "hello");

    const JsonValue* c = v.FindMember("c");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->type, JsonType::Boolean);
    EXPECT_TRUE(c->boolValue);

    const JsonValue* d = v.FindMember("d");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, JsonType::Null);
}

TEST(JsonParseTest, ParsesNestedObjectsAndArrays)
{
    const JsonValue v = ParseJson(R"({
        "meshes": [
            {"name": "a", "vertices": [1, 2, 3]},
            {"name": "b", "vertices": []}
        ]
    })");
    ASSERT_TRUE(v.IsObject());
    const JsonValue* meshes = v.FindMember("meshes");
    ASSERT_NE(meshes, nullptr);
    ASSERT_EQ(meshes->type, JsonType::Array);
    ASSERT_EQ(meshes->arrayValue.size(), 2u);
    EXPECT_EQ(meshes->arrayValue[0].FindMember("name")->stringValue, "a");
    EXPECT_EQ(meshes->arrayValue[0].FindMember("vertices")->arrayValue.size(), 3u);
    EXPECT_EQ(meshes->arrayValue[1].FindMember("vertices")->arrayValue.size(), 0u);
}

TEST(JsonParseTest, DecodesBasicStringEscapes)
{
    const JsonValue v = ParseJson(R"({"s": "a\"b\\c\/d\be\ff\ng\rh\ti"})");
    EXPECT_EQ(v.FindMember("s")->stringValue, "a\"b\\c/d\be\ff\ng\rh\ti");
}

TEST(JsonParseTest, DecodesUnicodeEscape)
{
    // é = 'é' (U+00E9), UTF-8: 0xC3 0xA9.
    const JsonValue v = ParseJson(R"({"s": "café"})");
    const std::string& s = v.FindMember("s")->stringValue;
    ASSERT_EQ(s.size(), 5u); // "caf" (3) + 2-byte UTF-8 sequence
    EXPECT_EQ(static_cast<unsigned char>(s[3]), 0xC3);
    EXPECT_EQ(static_cast<unsigned char>(s[4]), 0xA9);
}

TEST(JsonParseTest, DecodesSurrogatePairEscape)
{
    // U+1F600 (grinning face) as a UTF-16 surrogate pair: 😀.
    const JsonValue v = ParseJson(R"({"s": "😀"})");
    const std::string& s = v.FindMember("s")->stringValue;
    ASSERT_EQ(s.size(), 4u); // 4-byte UTF-8 sequence for a codepoint > 0xFFFF
    EXPECT_EQ(static_cast<unsigned char>(s[0]), 0xF0);
}

TEST(JsonParseTest, ParsesNumberVariants)
{
    const JsonValue v = ParseJson(R"({"a": 1, "b": -1, "c": 1.5, "d": 1e2, "e": 1.5e-2, "f": 0})");
    EXPECT_DOUBLE_EQ(v.FindMember("a")->numberValue, 1.0);
    EXPECT_DOUBLE_EQ(v.FindMember("b")->numberValue, -1.0);
    EXPECT_DOUBLE_EQ(v.FindMember("c")->numberValue, 1.5);
    EXPECT_DOUBLE_EQ(v.FindMember("d")->numberValue, 100.0);
    EXPECT_DOUBLE_EQ(v.FindMember("e")->numberValue, 0.015);
    EXPECT_DOUBLE_EQ(v.FindMember("f")->numberValue, 0.0);
}

TEST(JsonParseTest, RejectsTrailingGarbageAfterNumber)
{
    EXPECT_THROW(ParseJson(R"({"cnjVersion": 1abc})"), JsonParseException);
}

TEST(JsonParseTest, RejectsUnterminatedString)
{
    EXPECT_THROW(ParseJson(R"({"a": "unterminated)"), JsonParseException);
}

TEST(JsonParseTest, RejectsInvalidEscape)
{
    EXPECT_THROW(ParseJson(R"({"a": "bad \q escape"})"), JsonParseException);
}

TEST(JsonParseTest, RejectsTrailingContentAfterRootValue)
{
    EXPECT_THROW(ParseJson(R"({"a": 1} garbage)"), JsonParseException);
}

TEST(JsonParseTest, RejectsEmptyDocument)
{
    EXPECT_THROW(ParseJson(""), JsonParseException);
}

TEST(JsonParseTest, RejectsMissingCommaInObject)
{
    EXPECT_THROW(ParseJson(R"({"a": 1 "b": 2})"), JsonParseException);
}

TEST(JsonParseTest, ParsesNonObjectRoots)
{
    EXPECT_EQ(ParseJson("[1,2,3]").type, JsonType::Array);
    EXPECT_EQ(ParseJson("\"hello\"").type, JsonType::String);
    EXPECT_EQ(ParseJson("42").type, JsonType::Number);
    EXPECT_EQ(ParseJson("true").type, JsonType::Boolean);
    EXPECT_EQ(ParseJson("null").type, JsonType::Null);
}
