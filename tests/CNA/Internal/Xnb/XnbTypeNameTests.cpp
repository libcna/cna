// SPDX-License-Identifier: MS-PL
//
// plan_xnb.md XNB-13/XNB-13A: unit tests for the .xnb type-reader name parser/normalizer.

#include <gtest/gtest.h>

#include "CNA/Internal/Xnb/XnbTypeName.hpp"

using CNA::Internal::Xnb::NormalizeXnbTypeReaderName;
using CNA::Internal::Xnb::ParseXnbTypeName;

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
