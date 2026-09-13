// SPDX-License-Identifier: MS-PL
//
// plans/plan_win32.md WIN32-0011: UTF-16 <-> UTF-8 at the Win32 boundary.
//
// The surrogate cases are the reason this file exists. `WM_CHAR` delivers one UTF-16 *code unit*
// per message, so a character outside the basic multilingual plane -- an emoji, most of the
// historic scripts, a CJK extension ideograph -- arrives as two messages. Encoding either half
// alone produces invalid UTF-8 that a consumer either rejects or silently corrupts, and the bug
// is invisible on every ASCII keystroke.

#include "Win32/Win32Utf.hpp"

#include <gtest/gtest.h>

#include <string>

namespace {

using namespace CNA::Platform::Win32;

// --- scalar encoding ---------------------------------------------------------------------------

TEST(Win32Utf, EncodesEachUtf8Length)
{
    EXPECT_EQ(EncodeUtf8(U'A'), "A");                       // 1 byte
    EXPECT_EQ(EncodeUtf8(U'é'), "\xC3\xA9");           // 2 bytes, e-acute
    EXPECT_EQ(EncodeUtf8(U'€'), "\xE2\x82\xAC");       // 3 bytes, euro sign
    EXPECT_EQ(EncodeUtf8(U'\U0001F600'), "\xF0\x9F\x98\x80"); // 4 bytes, grinning face
}

TEST(Win32Utf, RefusesValuesThatAreNotScalarValues)
{
    // A lone surrogate has no UTF-8 encoding. Producing one anyway is CESU-8, which is the
    // classic way a "working" conversion emits bytes a strict decoder rejects.
    EXPECT_TRUE(EncodeUtf8(static_cast<char32_t>(0xD800)).empty());
    EXPECT_TRUE(EncodeUtf8(static_cast<char32_t>(0xDFFF)).empty());
    EXPECT_TRUE(EncodeUtf8(static_cast<char32_t>(0x110000)).empty());
}

// --- round trips -------------------------------------------------------------------------------

TEST(Win32Utf, RoundTripsThroughUtf16)
{
    for (const std::string& text :
         {std::string("plain ascii"), std::string("p\xC5\x99\xC3\xADli\xC5\xA1"),
          std::string("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E"),
          std::string("emoji \xF0\x9F\x8E\xAE here")})
    {
        EXPECT_EQ(ToUtf8(ToWide(text)), text) << text;
    }
}

TEST(Win32Utf, EmptyInputsStayEmpty)
{
    EXPECT_TRUE(ToWide("").empty());
    EXPECT_TRUE(ToUtf8(std::wstring()).empty());
    EXPECT_TRUE(ToUtf8(static_cast<const wchar_t*>(nullptr)).empty());
}

TEST(Win32Utf, InvalidUtf8ConvertsToNothingRatherThanGarbage)
{
    // 0x80 is a continuation byte with nothing to continue. MB_ERR_INVALID_CHARS makes this a
    // rejection instead of a silent substitution with U+FFFD, which would put a replacement
    // character into a window title.
    EXPECT_TRUE(ToWide(std::string("\x80\x80")).empty());
}

// --- surrogate assembly ------------------------------------------------------------------------

TEST(Win32Utf, AssemblesASurrogatePairIntoOneCharacter)
{
    SurrogateAssembler assembler;
    std::string text;

    // U+1F600 encodes as the pair D83D DE00.
    EXPECT_FALSE(assembler.Feed(u'\xD83D', text)) << "a high surrogate alone is not a character";
    EXPECT_TRUE(assembler.HasPending());
    ASSERT_TRUE(assembler.Feed(u'\xDE00', text));
    EXPECT_EQ(text, "\xF0\x9F\x98\x80");
    EXPECT_FALSE(assembler.HasPending());
}

TEST(Win32Utf, PassesBasicMultilingualPlaneCharactersStraightThrough)
{
    SurrogateAssembler assembler;
    std::string text;
    ASSERT_TRUE(assembler.Feed(u'é', text));
    EXPECT_EQ(text, "\xC3\xA9");
}

TEST(Win32Utf, DropsAnUnpairedLowSurrogate)
{
    SurrogateAssembler assembler;
    std::string text = "untouched";
    EXPECT_FALSE(assembler.Feed(u'\xDE00', text));
    EXPECT_EQ(text, "untouched");
}

TEST(Win32Utf, ASecondHighSurrogateReplacesTheAbandonedOne)
{
    SurrogateAssembler assembler;
    std::string text;
    EXPECT_FALSE(assembler.Feed(u'\xD800', text));
    EXPECT_FALSE(assembler.Feed(u'\xD83D', text));
    ASSERT_TRUE(assembler.Feed(u'\xDE00', text));
    // The second high surrogate is the live one, so the completed character is U+1F600 and the
    // abandoned first half contributes nothing.
    EXPECT_EQ(text, "\xF0\x9F\x98\x80");
}

TEST(Win32Utf, ResetDiscardsAPendingHalf)
{
    SurrogateAssembler assembler;
    std::string text;
    EXPECT_FALSE(assembler.Feed(u'\xD83D', text));
    assembler.Reset();
    EXPECT_FALSE(assembler.HasPending());
    // Without the reset this low surrogate would complete the stale pair and emit a character the
    // user never typed.
    EXPECT_FALSE(assembler.Feed(u'\xDE00', text));
}

} // namespace
