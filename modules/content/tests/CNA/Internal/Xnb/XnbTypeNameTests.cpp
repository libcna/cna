// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnb.md XNB-13/XNB-13A: unit tests for the .xnb type-reader name parser/normalizer.

#include <gtest/gtest.h>

#include "CNA/Internal/Xnb/XnbTypeName.hpp"

using CNA::Internal::Xnb::NormalizeXnbTypeReaderName;
using CNA::Internal::Xnb::ParseXnbTypeName;
using CNA::Internal::Xnb::XnbTypeName;

TEST(XnbTypeNameTest, PlainReaderNameStripsAssemblyQualification)
{
    const std::string raw =
        "Microsoft.Xna.Framework.Content.Texture2DReader, Microsoft.Xna.Framework.Graphics, "
        "Version=4.0.0.0, Culture=neutral, PublicKeyToken=842cf8be1de50553";

    EXPECT_EQ(NormalizeXnbTypeReaderName(raw), "Microsoft.Xna.Framework.Content.Texture2DReader");
}

TEST(XnbTypeNameTest, AlreadyBareNamePassesThroughUnchanged)
{
    EXPECT_EQ(NormalizeXnbTypeReaderName("Microsoft.Xna.Framework.Content.Texture2DReader"),
              "Microsoft.Xna.Framework.Content.Texture2DReader");
}

TEST(XnbTypeNameTest, OneLevelGenericStripsBothOuterAndArgumentAssemblyInfo)
{
    const std::string raw =
        "Microsoft.Xna.Framework.Content.ListReader`1[[Microsoft.Xna.Framework.Vector3, "
        "Microsoft.Xna.Framework, Version=4.0.0.0, Culture=neutral, "
        "PublicKeyToken=842cf8be1de50553]], Microsoft.Xna.Framework.Graphics, "
        "Version=4.0.0.0, Culture=neutral, PublicKeyToken=842cf8be1de50553";

    EXPECT_EQ(NormalizeXnbTypeReaderName(raw),
              "Microsoft.Xna.Framework.Content.ListReader`1[[Microsoft.Xna.Framework.Vector3]]");
}

TEST(XnbTypeNameTest, OneLevelGenericParsedFieldsAreCorrect)
{
    const std::string raw =
        "Microsoft.Xna.Framework.Content.ListReader`1[[Microsoft.Xna.Framework.Vector3, "
        "Microsoft.Xna.Framework, Version=4.0.0.0]], Microsoft.Xna.Framework.Graphics, Version=4.0.0.0";

    const auto parsed = ParseXnbTypeName(raw);

    EXPECT_EQ(parsed.baseName, "Microsoft.Xna.Framework.Content.ListReader`1");
    ASSERT_EQ(parsed.genericArguments.size(), 1u);
    EXPECT_EQ(parsed.genericArguments[0].baseName, "Microsoft.Xna.Framework.Vector3");
    EXPECT_TRUE(parsed.genericArguments[0].genericArguments.empty());
}

TEST(XnbTypeNameTest, DoublyNestedGenericDictionaryOfStringToListOfInt)
{
    const std::string raw =
        "Microsoft.Xna.Framework.Content.DictionaryReader`2"
        "[[System.String, mscorlib, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089],"
        "[Microsoft.Xna.Framework.Content.ListReader`1[[System.Int32, mscorlib, Version=4.0.0.0, "
        "Culture=neutral, PublicKeyToken=b77a5c561934e089]], Microsoft.Xna.Framework.Graphics, "
        "Version=4.0.0.0, Culture=neutral, PublicKeyToken=842cf8be1de50553]], "
        "Microsoft.Xna.Framework.Graphics, Version=4.0.0.0, Culture=neutral, "
        "PublicKeyToken=842cf8be1de50553";

    EXPECT_EQ(NormalizeXnbTypeReaderName(raw),
              "Microsoft.Xna.Framework.Content.DictionaryReader`2"
              "[[System.String],[Microsoft.Xna.Framework.Content.ListReader`1[[System.Int32]]]]");
}

TEST(XnbTypeNameTest, DoublyNestedGenericParsedFieldsAreCorrect)
{
    const std::string raw =
        "Microsoft.Xna.Framework.Content.DictionaryReader`2"
        "[[System.String, mscorlib],"
        "[Microsoft.Xna.Framework.Content.ListReader`1[[System.Int32, mscorlib]], "
        "Microsoft.Xna.Framework.Graphics]]";

    const auto parsed = ParseXnbTypeName(raw);

    EXPECT_EQ(parsed.baseName, "Microsoft.Xna.Framework.Content.DictionaryReader`2");
    ASSERT_EQ(parsed.genericArguments.size(), 2u);
    EXPECT_EQ(parsed.genericArguments[0].baseName, "System.String");
    EXPECT_EQ(parsed.genericArguments[1].baseName, "Microsoft.Xna.Framework.Content.ListReader`1");
    ASSERT_EQ(parsed.genericArguments[1].genericArguments.size(), 1u);
    EXPECT_EQ(parsed.genericArguments[1].genericArguments[0].baseName, "System.Int32");
}

TEST(XnbTypeNameTest, UnbalancedBracketsThrowsInvalidArgument)
{
    EXPECT_THROW(ParseXnbTypeName("Microsoft.Xna.Framework.Content.ListReader`1[[Microsoft.Xna.Framework.Vector3"),
                 std::invalid_argument);
}

TEST(XnbTypeNameTest, MissingOpenBracketForArgumentThrowsInvalidArgument)
{
    EXPECT_THROW(ParseXnbTypeName("Microsoft.Xna.Framework.Content.ListReader`1[Microsoft.Xna.Framework.Vector3]]"),
                 std::invalid_argument);
}

namespace
{
    // Builds a well-formed nested single-argument generic type name of the given depth, e.g.
    // depth=3 -> "X[[X[[X[[X]]]]]]" -- each level is exactly the shape a real nested generic like
    // ListReader`1[[ListReader`1[[...]]]] takes, just with a minimal base name.
    std::string BuildNestedTypeName(int depth)
    {
        std::string s;
        for (int i = 0; i < depth; ++i) s += "X[[";
        s += "X";
        for (int i = 0; i < depth; ++i) s += "]]";
        return s;
    }
}

// REMED-CONTENT-006: ParseOne() previously recursed once per nesting level with no limit at all --
// confirmed empirically (before this fix) to segfault the process via C++ call-stack exhaustion on
// a crafted, well-under-1MB type name. Reproduces that shape at the default limit's own boundary,
// proving it now throws cleanly instead of crashing.
TEST(XnbTypeNameTest, NestingDepthExceedingDefaultLimitThrowsInvalidArgumentNotCrash)
{
    const std::string deep = BuildNestedTypeName(300); // default maxObjectNestingDepth is 256
    EXPECT_THROW(ParseXnbTypeName(deep), std::invalid_argument);
}

TEST(XnbTypeNameTest, NestingDepthAtDefaultLimitDoesNotThrow)
{
    const std::string atLimit = BuildNestedTypeName(256); // exactly CNA::Internal::Xnb::XnbReadLimits{}.maxObjectNestingDepth
    EXPECT_NO_THROW(ParseXnbTypeName(atLimit));
}

TEST(XnbTypeNameTest, CustomLimitsRejectsNestingThatWouldBeAllowedByDefault)
{
    CNA::Internal::Xnb::XnbReadLimits tightLimits;
    tightLimits.maxObjectNestingDepth = 2;

    EXPECT_NO_THROW(ParseXnbTypeName(BuildNestedTypeName(2), tightLimits));
    EXPECT_THROW(ParseXnbTypeName(BuildNestedTypeName(3), tightLimits), std::invalid_argument);
}

// plans/plan_xnapipeline.md XNAP-9C: a .NET array type name. Found by auditing the *writer's*
// reader-identity model: a `List<int[]>` reader name genuinely contains `System.Int32[]` as a
// generic argument, and this parser read `System.Int32` followed by what it took to be a
// malformed generic-argument list and threw. A real `.xnb` whose type table names any array-typed
// generic argument therefore failed to load, before registry lookup was even reached.
TEST(XnbTypeNameTest, AnArrayTypeNameKeepsItsRankSpecifier)
{
    const XnbTypeName parsed = ParseXnbTypeName(
        "System.Int32[], mscorlib, Version=4.0.0.0, Culture=neutral, "
        "PublicKeyToken=b77a5c561934e089");
    EXPECT_EQ(parsed.baseName, "System.Int32");
    EXPECT_EQ(parsed.arraySuffix, "[]");
    EXPECT_TRUE(parsed.genericArguments.empty());
    EXPECT_EQ(parsed.ToCanonicalString(), "System.Int32[]");
}

TEST(XnbTypeNameTest, AnArrayTypedGenericArgumentParsesAndNormalizes)
{
    EXPECT_EQ(NormalizeXnbTypeReaderName(
                  "Microsoft.Xna.Framework.Content.ListReader`1[[System.Int32[], mscorlib, "
                  "Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089]], "
                  "Microsoft.Xna.Framework, Version=4.0.0.0"),
              "Microsoft.Xna.Framework.Content.ListReader`1[[System.Int32[]]]");
}

TEST(XnbTypeNameTest, AJaggedAndAMultidimensionalRankAreBothPreserved)
{
    EXPECT_EQ(NormalizeXnbTypeReaderName("System.Single[][], mscorlib"), "System.Single[][]");
    EXPECT_EQ(NormalizeXnbTypeReaderName("System.Single[,], mscorlib"), "System.Single[,]");
    EXPECT_EQ(NormalizeXnbTypeReaderName("System.Single[,,][], mscorlib"),
              "System.Single[,,][]");
}

TEST(XnbTypeNameTest, AnArrayOfAGenericTypeKeepsDotNetsOwnFieldOrder)
{
    // .NET spells `List<int>[]` as `List`1[[System.Int32, …]][]` -- rank *after* the argument
    // list -- so the canonical key must put it back in that order, not before the arguments.
    const XnbTypeName parsed = ParseXnbTypeName(
        "System.Collections.Generic.List`1[[System.Int32, mscorlib]][], mscorlib");
    EXPECT_EQ(parsed.baseName, "System.Collections.Generic.List`1");
    EXPECT_EQ(parsed.arraySuffix, "[]");
    ASSERT_EQ(parsed.genericArguments.size(), 1u);
    EXPECT_EQ(parsed.genericArguments[0].baseName, "System.Int32");
    EXPECT_EQ(parsed.ToCanonicalString(),
              "System.Collections.Generic.List`1[[System.Int32]][]");
}

TEST(XnbTypeNameTest, AGenericArgumentListIsStillNotMistakenForAnArrayRank)
{
    // The distinction is what is inside the bracket: only commas and spaces means a rank.
    EXPECT_EQ(NormalizeXnbTypeReaderName(
                  "Microsoft.Xna.Framework.Content.ArrayReader`1[[System.Int32, mscorlib]]"),
              "Microsoft.Xna.Framework.Content.ArrayReader`1[[System.Int32]]");
    EXPECT_THROW(ParseXnbTypeName("Some.Type[System.Int32]"), std::invalid_argument);
}
