// SPDX-License-Identifier: MS-PL
//
// Regression coverage for plans/plan_windows_portability.md WINPORT-0007: the core path helpers
// against real non-ASCII directory trees. Each of these exercises a code path that, before the
// migration, narrowed a path through the process ANSI code page -- so on a CP1252 Windows host it
// threw or silently matched the wrong entry, while on Linux it was a byte copy and passed.
//
// These tests therefore pass on Linux both before and after the change. Their value is on Windows,
// and the Linux run's job is only to prove the migration did not move anything here.
//
// Path text is spelled with hex escapes so the encoding of this source file is not under test; the
// concatenation breaks are required because a C++ hex escape is maximal-munch.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "CNA/Internal/CaseInsensitivePath.hpp"
#include "CNA/Internal/PathContainment.hpp"
#include "CNA/Internal/PathUtf8.hpp"

namespace fs = std::filesystem;

using CNA::Internal::IsDisallowedAbsolutePath;
using CNA::Internal::PathFromUtf8;
using CNA::Internal::PathToGenericUtf8;
using CNA::Internal::PathToUtf8;
using CNA::Internal::ResolveContainedNativePathFromBase;
using CNA::Internal::ResolveContainedPath;
using CNA::Internal::ResolveContainedPathRelativeToFile;
using CNA::Internal::ResolveExistingNativePath;
using CNA::Internal::ResolveExistingXnaPath;
using CNA::Internal::ValidateContainedPath;

namespace
{
    // žluťoučký, 日本語, кириллица, 😀 -- none of which exist in CP1252.
    constexpr const char* kCzech = "\xc5\xbe" "lu\xc5\xa5" "ou\xc4\x8d" "k\xc3\xbd";
    constexpr const char* kJapanese = "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e";
    constexpr const char* kCyrillic = "\xd0\xba\xd0\xb8\xd1\x80\xd0\xb8\xd0\xbb\xd0\xbb\xd0\xb8\xd1\x86\xd0\xb0";
    constexpr const char* kEmoji = "emoji-\xf0\x9f\x98\x80";

    class UnicodeTree : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            std::error_code ec;
            root_ = fs::temp_directory_path()
                    / ("cna-unicode-tree-" + std::to_string(::testing::UnitTest::GetInstance()
                                                                ->random_seed())
                       + "-" + std::to_string(counter_++));
            fs::remove_all(root_, ec);
            ASSERT_TRUE(fs::create_directories(root_, ec)) << ec.message();
        }

        void TearDown() override
        {
            std::error_code ec;
            fs::remove_all(root_, ec);
        }

        /// Writes @p payload into root/<relativeUtf8>, creating parents. Stays native throughout.
        fs::path Write(const std::string& relativeUtf8, std::string_view payload = "payload")
        {
            const fs::path file = root_ / PathFromUtf8(relativeUtf8);
            std::error_code ec;
            fs::create_directories(file.parent_path(), ec);
            std::ofstream out(file, std::ios::binary);
            EXPECT_TRUE(out.is_open()) << relativeUtf8;
            out << payload;
            return file;
        }

        [[nodiscard]] static std::string Read(const fs::path& file)
        {
            std::ifstream in(file, std::ios::binary);
            if (!in.is_open()) { return {}; }
            return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        }

        fs::path root_;

    private:
        static inline int counter_ = 0;
    };
}

// ---------------------------------------------------------------------------------------------
// Containment, with non-ASCII on both sides of the join.
// ---------------------------------------------------------------------------------------------

TEST_F(UnicodeTree, ResolvesANonAsciiRelativePathUnderANonAsciiRoot)
{
    const std::string relative = std::string(kCzech) + "/" + kJapanese + "/asset.png";
    Write(relative, "contained-ok");

    const CNA::Internal::ContainedPathResult result =
        ResolveContainedPath(PathToGenericUtf8(root_), relative);

    ASSERT_TRUE(result.ok);
    // The returned text is UTF-8 and names the file that was actually written -- asserted by
    // reading the payload back out, not by comparing strings.
    EXPECT_EQ(Read(PathFromUtf8(result.resolvedPath)), "contained-ok");
}

TEST_F(UnicodeTree, ResolvedTextIsGenericFormSoItMatchesKeysProducedElsewhere)
{
    const std::string relative = std::string(kCyrillic) + "/track.ogg";
    Write(relative);

    const CNA::Internal::ContainedPathResult result =
        ResolveContainedPath(PathToGenericUtf8(root_), relative);

    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.resolvedPath.find('\\'), std::string::npos)
        << "a containment result is a map key; it must be spelled with '/' on every platform";
    EXPECT_NE(result.resolvedPath.find(kCyrillic), std::string::npos);
}

TEST_F(UnicodeTree, StillRejectsAnEscapeWhenEveryComponentIsNonAscii)
{
    // Encoding correctness must not have loosened containment.
    const std::string escaping = std::string("../") + kJapanese + "/secret.bin";
    EXPECT_FALSE(ResolveContainedPath(PathToGenericUtf8(root_), escaping).ok);
}

TEST_F(UnicodeTree, StillRejectsAnAbsoluteSpellingWhenTheRootIsNonAscii)
{
    EXPECT_FALSE(ResolveContainedPath(PathToGenericUtf8(root_), "/etc/passwd").ok);
    EXPECT_FALSE(ResolveContainedPath(PathToGenericUtf8(root_), "C:/Windows/win.ini").ok);
    EXPECT_FALSE(ResolveContainedPath(PathToGenericUtf8(root_), "//server/share/x").ok);
}

TEST_F(UnicodeTree, ADirectoryWhoseNameBeginsWithTwoDotsIsNotAnEscape)
{
    // The WINNATIVE-F9 case, re-asserted with a non-ASCII sibling present so that the walk has to
    // survive enumerating it.
    Write(std::string(kEmoji) + "/decoy.txt");
    const std::string relative = std::string("..") + kCzech + "/settings.cfg";
    Write(relative, "dotdot-ok");

    const CNA::Internal::ContainedPathResult result =
        ResolveContainedPath(PathToGenericUtf8(root_), relative);
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(Read(PathFromUtf8(result.resolvedPath)), "dotdot-ok");
}

TEST_F(UnicodeTree, ResolvesARelativeReferenceAgainstANonAsciiReferringFile)
{
    const fs::path referring = Write(std::string(kJapanese) + "/scene.xnb");
    Write(std::string(kJapanese) + "/" + kCzech + ".png", "sibling-ok");

    const CNA::Internal::ContainedPathResult result = ResolveContainedPathRelativeToFile(
        PathToGenericUtf8(root_), PathToGenericUtf8(referring), std::string(kCzech) + ".png");

    ASSERT_TRUE(result.ok);
    EXPECT_EQ(Read(PathFromUtf8(result.resolvedPath)), "sibling-ok");
}

TEST_F(UnicodeTree, ValidateContainedPathAcceptsANonAsciiCandidateBelowANonAsciiRoot)
{
    const fs::path file = Write(std::string(kEmoji) + "/" + kCyrillic + ".dat", "validate-ok");

    const CNA::Internal::ContainedPathResult result =
        ValidateContainedPath(PathToGenericUtf8(root_), PathToGenericUtf8(file));

    ASSERT_TRUE(result.ok);
    EXPECT_EQ(Read(PathFromUtf8(result.resolvedPath)), "validate-ok");
}

TEST_F(UnicodeTree, TheNativeCoreNeedsNoNarrowingAtAll)
{
    Write(std::string(kCzech) + "/" + kEmoji + "/mesh.cnb", "native-ok");

    const CNA::Internal::ContainedNativePathResult result = ResolveContainedNativePathFromBase(
        root_, root_, std::string(kCzech) + "/" + kEmoji + "/mesh.cnb");

    ASSERT_TRUE(result.ok);
    EXPECT_EQ(Read(result.resolvedPath), "native-ok");
}

TEST(IsDisallowedAbsolutePathUnicodeTest, IsTotalOnTextThatIsNotUtf8)
{
    // It takes untrusted text straight out of a playlist or an XNB. Answering, rather than
    // throwing, is the contract: it builds no path precisely so that it cannot.
    EXPECT_FALSE(IsDisallowedAbsolutePath("caf\xe9/track.ogg"));   // CP1252 bytes, not UTF-8
    EXPECT_FALSE(IsDisallowedAbsolutePath("\xff\xfe"));
    EXPECT_TRUE(IsDisallowedAbsolutePath("/\xff\xfe"));
    EXPECT_TRUE(IsDisallowedAbsolutePath("C:/\xff"));
}

TEST(IsDisallowedAbsolutePathUnicodeTest, StillAnswersTheRootedSpellingsItAlwaysDid)
{
    EXPECT_TRUE(IsDisallowedAbsolutePath("/etc/passwd"));
    EXPECT_TRUE(IsDisallowedAbsolutePath("C:/Windows"));
    EXPECT_TRUE(IsDisallowedAbsolutePath("c:/windows"));
    EXPECT_TRUE(IsDisallowedAbsolutePath("//server/share"));
    EXPECT_FALSE(IsDisallowedAbsolutePath("textures/a.png"));
    EXPECT_FALSE(IsDisallowedAbsolutePath("../textures/a.png"));
    EXPECT_FALSE(IsDisallowedAbsolutePath(""));
}

// ---------------------------------------------------------------------------------------------
// The case-insensitive walker. Its Windows defect was that it converted every entry it
// enumerated, so one non-ASCII sibling broke the lookup of an ordinary ASCII file.
// ---------------------------------------------------------------------------------------------

TEST_F(UnicodeTree, ResolvesAnAsciiNameInADirectoryThatAlsoHoldsNonAsciiSiblings)
{
    Write("Assets/Texture.png", "ascii-target");
    Write(std::string("Assets/") + kJapanese + ".png");
    Write(std::string("Assets/") + kEmoji + ".png");

    const fs::path wrongCase = root_ / "assets" / "texture.png";
    const fs::path resolved = ResolveExistingNativePath(wrongCase);

    EXPECT_EQ(Read(resolved), "ascii-target");
}

TEST_F(UnicodeTree, ResolvesANonAsciiNameSpelledWithTheWrongAsciiCase)
{
    // Only the ASCII letters fold; the non-ASCII part must match exactly.
    Write(std::string("Data/") + kCzech + "_LEVEL.cnb", "mixed-case-ok");

    const fs::path wrongCase = root_ / "data" / (std::string(kCzech) + "_level.cnb");
    const fs::path resolved = ResolveExistingNativePath(wrongCase);

    EXPECT_EQ(Read(resolved), "mixed-case-ok");
}

TEST_F(UnicodeTree, DoesNotFoldNonAsciiCaseWhenResolving)
{
    // A locale-aware tolower() over UTF-8 bytes could rewrite continuation bytes and match the
    // wrong entry. Ž (U+017D) and ž (U+017E) must stay distinct.
    Write("\xc5\xbd" "ID.bin", "upper");            // ŽID.bin
    const fs::path lower = root_ / "\xc5\xbe" "id.bin";   // žid.bin -- different file

    const fs::path resolved = ResolveExistingNativePath(lower);
    EXPECT_FALSE(fs::exists(resolved)) << "non-ASCII case must not fold";
}

TEST_F(UnicodeTree, TheUtf8SpellingReturnsGenericFormAndStillNamesTheFile)
{
    Write(std::string("Level/") + kCyrillic + ".map", "utf8-walker-ok");

    const std::string resolved =
        ResolveExistingXnaPath(PathToGenericUtf8(root_) + "/level/" + kCyrillic + ".map");

    EXPECT_EQ(resolved.find('\\'), std::string::npos);
    EXPECT_EQ(Read(PathFromUtf8(resolved)), "utf8-walker-ok");
}

TEST_F(UnicodeTree, AnUnresolvableNonAsciiPathIsReturnedUnchangedRatherThanThrowing)
{
    const std::string missing = PathToGenericUtf8(root_) + "/" + kEmoji + "/nothing/here.bin";
    EXPECT_EQ(ResolveExistingXnaPath(missing), missing);
}

TEST_F(UnicodeTree, BackslashSpellingsAreNormalisedEvenWithNonAsciiComponents)
{
    Write(std::string(kJapanese) + "/" + kCzech + "/a.txt", "sep-ok");

    const std::string spelled =
        PathToGenericUtf8(root_) + "\\" + kJapanese + "\\" + kCzech + "\\a.txt";
    const std::string resolved = ResolveExistingXnaPath(spelled);

    EXPECT_EQ(Read(PathFromUtf8(resolved)), "sep-ok");
}
