// SPDX-License-Identifier: MS-PL
//
// The conversion layer's own tests. These are deliberately written so that they would have FAILED
// on Windows before plans/plan_windows_portability.md WINPORT-0006: every case that touches the
// filesystem creates a real file under a real non-ASCII directory and reads a known payload back
// out of it, rather than comparing strings the test itself produced.
//
// Path text is spelled with hex escapes rather than literal characters so that the encoding of
// this source file cannot be what is under test. The concatenation breaks ("\xc5\xbe" "lu") are
// required: a hex escape in C++ is maximal-munch, so "\xc5\xbelu" would lex as one enormous escape.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "CNA/Internal/ContentPath.hpp"
#include "CNA/Internal/PathUtf8.hpp"

namespace fs = std::filesystem;

using CNA::Internal::ContentPathFromUtf8;
using CNA::Internal::ContentPathToUtf8;
using CNA::Internal::IsWellFormedUtf8;
using CNA::Internal::PathFromUtf8;
using CNA::Internal::PathToGenericUtf8;
using CNA::Internal::PathToUtf8;

namespace
{
    /// The path classes the Windows probe measured. The labels match the spike's output so a
    /// failure here can be read against spikes/windows-unicode-path-spike/probe_windows_cp1252.txt.
    struct PathClass
    {
        const char* label;
        const char* utf8;
    };

    const std::vector<PathClass>& Classes()
    {
        static const std::vector<PathClass> classes = {
            {"ascii",       "plain"},
            {"spaces",      "CNA test dir"},
            {"latin1",      "caf\xc3\xa9"},
            {"czech",       "\xc5\xbe" "lu\xc5\xa5" "ou\xc4\x8d" "k\xc3\xbd"},
            {"combining",   "e\xcc\x81" "tude"},
            {"precomposed", "\xc3\xa9" "tude"},
            {"cyrillic",    "\xd0\xba\xd0\xb8\xd1\x80\xd0\xb8\xd0\xbb\xd0\xbb\xd0\xb8\xd1\x86\xd0\xb0"},
            {"cjk-jp",      "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"},
            {"cjk-zh",      "\xe4\xb8\xad\xe6\x96\x87"},
            {"emoji",       "emoji-\xf0\x9f\x98\x80"},
            {"mixed",       "m\xc3\xad" "ch-\xc4\x8d" "esky-\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e-\xf0\x9f\x98\x80"},
        };
        return classes;
    }

    /// A temporary directory that removes itself, named so concurrent suites cannot collide.
    class ScopedDirectory
    {
    public:
        explicit ScopedDirectory(const std::string& tag)
        {
            std::error_code ec;
            root_ = fs::temp_directory_path() / ("cna-pathutf8-" + tag + "-"
                                                 + std::to_string(::testing::UnitTest::GetInstance()
                                                                      ->random_seed())
                                                 + "-" + std::to_string(counter_++));
            fs::remove_all(root_, ec);
            fs::create_directories(root_, ec);
        }

        ~ScopedDirectory()
        {
            std::error_code ec;
            fs::remove_all(root_, ec);
        }

        ScopedDirectory(const ScopedDirectory&) = delete;
        ScopedDirectory& operator=(const ScopedDirectory&) = delete;

        [[nodiscard]] const fs::path& Path() const { return root_; }

    private:
        fs::path root_;
        static inline int counter_ = 0;
    };
}

// ---------------------------------------------------------------------------------------------
// Round trips. The property that matters is not that the text looks right but that the path
// converted to UTF-8 and back still names the same file.
// ---------------------------------------------------------------------------------------------

TEST(PathUtf8Test, RoundTripsEveryPathClassThroughUtf8AndBack)
{
    for (const PathClass& item : Classes())
    {
        const fs::path original = PathFromUtf8(item.utf8);
        const std::string text = PathToUtf8(original);
        EXPECT_EQ(text, item.utf8) << "class " << item.label;
        EXPECT_EQ(PathFromUtf8(text), original) << "class " << item.label;
    }
}

TEST(PathUtf8Test, RoundTripsEveryPathClassThroughGenericUtf8AndBack)
{
    for (const PathClass& item : Classes())
    {
        const fs::path original = PathFromUtf8(item.utf8);
        const std::string text = PathToGenericUtf8(original);
        EXPECT_EQ(text, item.utf8) << "class " << item.label;
        EXPECT_EQ(PathFromUtf8(text), original) << "class " << item.label;
    }
}

TEST(PathUtf8Test, AFileCreatedUnderEveryPathClassIsFoundAgainThroughItsUtf8Text)
{
    // The whole point of the layer, asserted the only way that proves anything: write a payload,
    // convert the path to text, convert it back, and read the payload out of the reconstructed
    // path. On Windows before this layer existed, PathToUtf8's predecessor threw here.
    ScopedDirectory scope("roundtrip");
    constexpr const char* kPayload = "CNA-PATHUTF8-OK";

    for (const PathClass& item : Classes())
    {
        const fs::path directory = scope.Path() / PathFromUtf8(item.utf8);
        std::error_code ec;
        fs::create_directories(directory, ec);
        ASSERT_FALSE(ec) << "class " << item.label << ": " << ec.message();

        const fs::path file = directory / PathFromUtf8(std::string(item.utf8) + ".txt");
        {
            std::ofstream out(file, std::ios::binary);
            ASSERT_TRUE(out.is_open()) << "class " << item.label;
            out << kPayload;
        }

        const std::string text = PathToUtf8(file);
        const fs::path reconstructed = PathFromUtf8(text);

        EXPECT_TRUE(fs::exists(reconstructed, ec)) << "class " << item.label;
        std::ifstream in(reconstructed, std::ios::binary);
        ASSERT_TRUE(in.is_open()) << "class " << item.label;
        std::string read((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        EXPECT_EQ(read, kPayload) << "class " << item.label;
    }
}

TEST(PathUtf8Test, DirectoryEnumerationAgreesWithTheUtf8TextOfWhatWasWritten)
{
    // Guards the map-key half of the model: a name discovered by enumerating a directory must
    // produce the same UTF-8 identity as the name that created it, or caches and album trees
    // silently miss.
    ScopedDirectory scope("enumerate");
    std::vector<std::string> expected;

    for (const PathClass& item : Classes())
    {
        const fs::path file = scope.Path() / PathFromUtf8(std::string(item.utf8) + ".bin");
        std::ofstream out(file, std::ios::binary);
        ASSERT_TRUE(out.is_open()) << "class " << item.label;
        expected.push_back(PathToUtf8(file.filename()));
    }

    std::vector<std::string> found;
    std::error_code ec;
    for (const fs::directory_entry& entry : fs::directory_iterator(scope.Path(), ec))
    {
        found.push_back(PathToUtf8(entry.path().filename()));
    }
    ASSERT_FALSE(ec) << ec.message();

    std::sort(expected.begin(), expected.end());
    std::sort(found.begin(), found.end());
    EXPECT_EQ(found, expected);
}

// ---------------------------------------------------------------------------------------------
// Separator handling. PathToGenericUtf8 normalises separators and PathToUtf8 does not, and
// neither of them normalises anything else.
// ---------------------------------------------------------------------------------------------

TEST(PathUtf8Test, GenericFormSpellsSeparatorsWithForwardSlashOnEveryPlatform)
{
    const fs::path joined = PathFromUtf8("content") / PathFromUtf8("textures") / PathFromUtf8("a.png");
    EXPECT_EQ(PathToGenericUtf8(joined), "content/textures/a.png");
}

TEST(PathUtf8Test, ConversionDoesNotResolveDotOrDotDot)
{
    // Encoding and lexical normalisation are separate concerns; bundling them would silently
    // change what a stored path means.
    EXPECT_EQ(PathToGenericUtf8(PathFromUtf8("a/./b/../c")), "a/./b/../c");
}

TEST(PathUtf8Test, ConversionDoesNotChangeCase)
{
    EXPECT_EQ(PathToGenericUtf8(PathFromUtf8("Content/Textures/Asset.PNG")),
              "Content/Textures/Asset.PNG");
}

TEST(PathUtf8Test, ConversionDoesNotApplyUnicodeNormalisation)
{
    // "e" + U+0301 and precomposed U+00E9 look identical and are different filenames. A
    // conversion that folded them would make one asset shadow another.
    const std::string combining = "e\xcc\x81";
    const std::string precomposed = "\xc3\xa9";
    EXPECT_NE(combining, precomposed);
    EXPECT_EQ(PathToUtf8(PathFromUtf8(combining)), combining);
    EXPECT_EQ(PathToUtf8(PathFromUtf8(precomposed)), precomposed);
    EXPECT_NE(PathToUtf8(PathFromUtf8(combining)), PathToUtf8(PathFromUtf8(precomposed)));
}

TEST(PathUtf8Test, TrailingSeparatorSurvivesConversion)
{
    EXPECT_EQ(PathToGenericUtf8(PathFromUtf8("content/textures/")), "content/textures/");
}

TEST(PathUtf8Test, AnEmptyPathConvertsToEmptyTextAndBack)
{
    EXPECT_EQ(PathToUtf8(fs::path()), "");
    EXPECT_EQ(PathToGenericUtf8(fs::path()), "");
    EXPECT_TRUE(PathFromUtf8("").empty());
}

// ---------------------------------------------------------------------------------------------
// UTF-8 validation. The policy is that PathFromUtf8 does not validate -- POSIX filenames need not
// be UTF-8 at all -- and that a caller wanting a deterministic answer asks for one.
// ---------------------------------------------------------------------------------------------

TEST(IsWellFormedUtf8Test, AcceptsEveryPathClassUsedByTheseTests)
{
    for (const PathClass& item : Classes())
    {
        EXPECT_TRUE(IsWellFormedUtf8(item.utf8)) << "class " << item.label;
    }
}

TEST(IsWellFormedUtf8Test, AcceptsEmptyAndPlainAscii)
{
    EXPECT_TRUE(IsWellFormedUtf8(""));
    EXPECT_TRUE(IsWellFormedUtf8("content/textures/asset.png"));
}

TEST(IsWellFormedUtf8Test, AcceptsAnEmbeddedNulBecauseItIsAValidCodePoint)
{
    EXPECT_TRUE(IsWellFormedUtf8(std::string("a\0b", 3)));
}

TEST(IsWellFormedUtf8Test, AcceptsTheBoundaryCodePoints)
{
    EXPECT_TRUE(IsWellFormedUtf8("\x7f"));                  // U+007F, last one-byte
    EXPECT_TRUE(IsWellFormedUtf8("\xc2\x80"));              // U+0080, first two-byte
    EXPECT_TRUE(IsWellFormedUtf8("\xdf\xbf"));              // U+07FF, last two-byte
    EXPECT_TRUE(IsWellFormedUtf8("\xe0\xa0\x80"));          // U+0800, first three-byte
    EXPECT_TRUE(IsWellFormedUtf8("\xef\xbf\xbf"));          // U+FFFF, last three-byte
    EXPECT_TRUE(IsWellFormedUtf8("\xf0\x90\x80\x80"));      // U+10000, first four-byte
    EXPECT_TRUE(IsWellFormedUtf8("\xf4\x8f\xbf\xbf"));      // U+10FFFF, last valid code point
}

TEST(IsWellFormedUtf8Test, RejectsAStrayContinuationByte)
{
    EXPECT_FALSE(IsWellFormedUtf8("\x80"));
    EXPECT_FALSE(IsWellFormedUtf8("a\xbf" "b"));
}

TEST(IsWellFormedUtf8Test, RejectsATruncatedSequence)
{
    EXPECT_FALSE(IsWellFormedUtf8("\xc3"));
    EXPECT_FALSE(IsWellFormedUtf8("\xe6\x97"));
    EXPECT_FALSE(IsWellFormedUtf8("\xf0\x9f\x98"));
}

TEST(IsWellFormedUtf8Test, RejectsAnOverlongEncoding)
{
    EXPECT_FALSE(IsWellFormedUtf8("\xc0\xaf"));             // U+002F as two bytes
    EXPECT_FALSE(IsWellFormedUtf8("\xe0\x80\xaf"));         // U+002F as three
    EXPECT_FALSE(IsWellFormedUtf8("\xf0\x80\x80\xaf"));     // U+002F as four
    EXPECT_FALSE(IsWellFormedUtf8("\xc1\xbf"));             // U+007F as two bytes
}

TEST(IsWellFormedUtf8Test, RejectsTheSurrogateRange)
{
    EXPECT_FALSE(IsWellFormedUtf8("\xed\xa0\x80"));         // U+D800
    EXPECT_FALSE(IsWellFormedUtf8("\xed\xbf\xbf"));         // U+DFFF
    EXPECT_TRUE(IsWellFormedUtf8("\xed\x9f\xbf"));          // U+D7FF, just below
    EXPECT_TRUE(IsWellFormedUtf8("\xee\x80\x80"));          // U+E000, just above
}

TEST(IsWellFormedUtf8Test, RejectsCodePointsAboveTheUnicodeRange)
{
    EXPECT_FALSE(IsWellFormedUtf8("\xf4\x90\x80\x80"));     // U+110000
    EXPECT_FALSE(IsWellFormedUtf8("\xf5\x80\x80\x80"));
    EXPECT_FALSE(IsWellFormedUtf8("\xf8\x88\x80\x80\x80")); // 5-byte form, never valid
    EXPECT_FALSE(IsWellFormedUtf8("\xff"));
}

TEST(IsWellFormedUtf8Test, RejectsLatin1BytesThatAreNotUtf8)
{
    // The case that makes a mechanical migration wrong: 0xE9 is a perfectly good CP1252 'é' and
    // is not UTF-8 at all.
    EXPECT_FALSE(IsWellFormedUtf8("caf\xe9"));
}

// ---------------------------------------------------------------------------------------------
// The content-pipeline spellings must stay exactly what they were, because ~100 sites use them.
// ---------------------------------------------------------------------------------------------

TEST(ContentPathCompatibilityTest, ContentPathToUtf8StillProducesGenericForm)
{
    const fs::path joined = PathFromUtf8("content") / PathFromUtf8("a") / PathFromUtf8("b.png");
    EXPECT_EQ(ContentPathToUtf8(joined), "content/a/b.png");
    EXPECT_EQ(ContentPathToUtf8(joined), PathToGenericUtf8(joined));
}

TEST(ContentPathCompatibilityTest, ContentPathFromUtf8StillMatchesPathFromUtf8)
{
    for (const PathClass& item : Classes())
    {
        EXPECT_EQ(ContentPathFromUtf8(item.utf8), PathFromUtf8(item.utf8)) << "class " << item.label;
    }
}
