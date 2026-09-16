// SPDX-License-Identifier: MS-PL
//
// plans/plan_win32_native_validation.md WINNATIVE-0024: the oracle corpus reader that replaced a
// std::regex MSVC could not run.
//
// The reader is shared by sixteen fixtures and decides what every one of them compares against, so
// a defect here does not fail -- it silently changes the expected value. These cases pin the
// grammar at the edges the shipped corpora never exercise: escapes, adjacent quotation marks,
// surrounding text, and the malformed lines a reader must decline rather than half-read.
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "CNA/TestSupport/OracleCorpus.hpp"

using CNA::TestSupport::ReadOracleCase;
using CNA::TestSupport::ReadOracleFields;

namespace
{
    /** @brief The name and raw result of a line, or two empty strings when it is not a record. */
    std::pair<std::string, std::string> Read(const std::string& line)
    {
        std::string name;
        std::string result;
        return ReadOracleCase(line, name, result) ? std::pair{name, result}
                                                  : std::pair{std::string{}, std::string{}};
    }
}

TEST(OracleCorpus, ReadsThePlainRecord)
{
    const auto [name, result] = Read(R"({"case": "alpha", "result": "beta"})");
    EXPECT_EQ(name, "alpha");
    EXPECT_EQ(result, "beta");
}

TEST(OracleCorpus, ReturnsTheResultStillEscaped)
{
    // Unescaping belongs to the caller: the corpora do not all spell it the same way, so the
    // reader hands back exactly the bytes between the quotation marks.
    const auto [name, result] = Read(R"({"case": "c", "result": "a\nb\\c\"d"})");
    EXPECT_EQ(name, "c");
    EXPECT_EQ(result, R"(a\nb\\c\"d)");
}

TEST(OracleCorpus, AnEscapedQuotationMarkDoesNotEndTheResult)
{
    // This is the case the regex existed for and the one a naive scan gets wrong.
    const auto [name, result] = Read(R"({"case": "c", "result": "he said \"no\" twice"})");
    EXPECT_EQ(name, "c");
    EXPECT_EQ(result, R"(he said \"no\" twice)");
}

TEST(OracleCorpus, AnEscapedBackslashBeforeTheClosingQuoteEndsTheResult)
{
    // "a\\" is a result of a single escaped backslash, immediately followed by the real closing
    // quotation mark -- not an escaped quote. Getting this backwards swallows the rest of the line.
    const auto [name, result] = Read(R"({"case": "c", "result": "a\\"})");
    EXPECT_EQ(name, "c");
    EXPECT_EQ(result, R"(a\\)");
}

TEST(OracleCorpus, EmptyValuesAreRecords)
{
    const auto [name, result] = Read(R"({"case": "", "result": ""})");
    EXPECT_EQ(name, "");
    EXPECT_EQ(result, "");
    std::string n;
    std::string r;
    EXPECT_TRUE(ReadOracleCase(R"({"case": "", "result": ""})", n, r));
}

TEST(OracleCorpus, TextAroundTheRecordIsIgnored)
{
    // The readers searched rather than matched, so a record inside an array line still counts.
    const auto [name, result] = Read(R"(  [ {"case": "x", "result": "y"} ],)");
    EXPECT_EQ(name, "x");
    EXPECT_EQ(result, "y");
}

TEST(OracleCorpus, TheLeftmostRecordWins)
{
    const auto [name, result] =
        Read(R"({"case": "first", "result": "1"} {"case": "second", "result": "2"})");
    EXPECT_EQ(name, "first");
    EXPECT_EQ(result, "1");
}

TEST(OracleCorpus, MalformedLinesAreDeclined)
{
    std::string name;
    std::string result;
    EXPECT_FALSE(ReadOracleCase("", name, result));
    EXPECT_FALSE(ReadOracleCase("[]", name, result));
    EXPECT_FALSE(ReadOracleCase(R"({"case": "a"})", name, result));
    EXPECT_FALSE(ReadOracleCase(R"({"result": "a", "case": "b"})", name, result));  // wrong order
    EXPECT_FALSE(ReadOracleCase(R"({"case": "a", "result": "b")", name, result));   // no brace
    EXPECT_FALSE(ReadOracleCase(R"({"case": "a", "result": "b\")", name, result));  // never closed
    EXPECT_FALSE(ReadOracleCase(R"({"case": "a","result": "b"})", name, result));   // no space
}

TEST(OracleCorpus, ADeclinedLineLeavesTheOutputsAlone)
{
    std::string name = "kept";
    std::string result = "kept too";
    EXPECT_FALSE(ReadOracleCase("nonsense", name, result));
    EXPECT_EQ(name, "kept");
    EXPECT_EQ(result, "kept too");
}

TEST(OracleCorpus, ReadsTheFourFieldManifestRecord)
{
    std::vector<std::string> fields;
    ASSERT_TRUE(ReadOracleFields(
        R"({"case": "c", "rootType": "System.Int32", "status": "ok", "note": "a\nb"})",
        {{"case", false}, {"rootType", true}, {"status", false}, {"note", true}}, fields));
    ASSERT_EQ(fields.size(), 4u);
    EXPECT_EQ(fields[0], "c");
    EXPECT_EQ(fields[1], "System.Int32");
    EXPECT_EQ(fields[2], "ok");
    EXPECT_EQ(fields[3], R"(a\nb)");
}

TEST(OracleCorpus, AnUnescapedFieldStopsAtTheFirstQuotationMark)
{
    // `case` was written [^"]*, which cannot contain a quotation mark at all -- so a name that
    // carries one is not a record, rather than a name with an escape in it.
    std::string name;
    std::string result;
    EXPECT_FALSE(ReadOracleCase(R"({"case": "a\"b", "result": "c"})", name, result));
}

TEST(OracleCorpus, NoFieldsIsNotARecord)
{
    std::vector<std::string> fields;
    EXPECT_FALSE(ReadOracleFields(R"({"case": "a"})", {}, fields));
}

TEST(OracleCorpus, ALongResultIsReadRatherThanRefused)
{
    // The whole point. MSVC's <regex> matches (?:[^"\\]|\\.)* by recursing once per repetition, so
    // a result of this length threw regex_error(error_stack) on Windows and left the corpus empty.
    const std::string payload(200000, 'x');
    const auto [name, result] = Read(R"({"case": "big", "result": ")" + payload + R"("})");
    EXPECT_EQ(name, "big");
    EXPECT_EQ(result.size(), payload.size());
}
