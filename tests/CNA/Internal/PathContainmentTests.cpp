// SPDX-License-Identifier: MS-PL
//
// REMED-CONTENT-002: unit tests for the shared containment helper used by
// StorageDevice::DeleteContainer, ContentReader::ReadExternalReference, and
// PlaylistParser::Parse.

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

#include "CNA/Internal/PathContainment.hpp"

using CNA::Internal::ResolveContainedPath;

namespace
{
    // A tests-only scratch directory tree, unique per test process run. Mirrors the convention
    // already used by CnjSourceFileSafetyTests.cpp/ContentManagerSkinnedModelTests.cpp.
    class ScratchDir
    {
    public:
        ScratchDir()
            : dir_(std::filesystem::temp_directory_path()
                   / ("cna_path_containment_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(dir_);
        }
        ~ScratchDir()
        {
            std::error_code ec;
            std::filesystem::remove_all(dir_, ec);
        }
        ScratchDir(const ScratchDir&) = delete;
        ScratchDir& operator=(const ScratchDir&) = delete;

        [[nodiscard]] const std::filesystem::path& path() const { return dir_; }

    private:
        std::filesystem::path dir_;
    };
}

TEST(PathContainmentTest, SimpleRelativeSubpathIsContained)
{
    const auto result = ResolveContainedPath("/base/dir", "sub/file.txt", /*canonicalize=*/false);
    EXPECT_TRUE(result.ok);
}

TEST(PathContainmentTest, AbsoluteRhsIsRejected)
{
    const auto result = ResolveContainedPath("/base/dir", "/etc/passwd", /*canonicalize=*/false);
    EXPECT_FALSE(result.ok);
}

TEST(PathContainmentTest, DotDotTraversalEscapingBaseIsRejected)
{
    const auto result = ResolveContainedPath("/base/dir", "../../etc/passwd", /*canonicalize=*/false);
    EXPECT_FALSE(result.ok);
}

TEST(PathContainmentTest, DotDotThatStaysWithinBaseIsContained)
{
    // "sub/../file.txt" normalizes to "file.txt" -- still within base, must not be rejected just
    // for containing a ".." component that never actually escapes.
    const auto result = ResolveContainedPath("/base/dir", "sub/../file.txt", /*canonicalize=*/false);
    EXPECT_TRUE(result.ok);
}

TEST(PathContainmentTest, DotOnlyResolvesToBaseItselfAndIsRejected)
{
    const auto result = ResolveContainedPath("/base/dir", ".", /*canonicalize=*/false);
    EXPECT_FALSE(result.ok) << "resolving to exactly the base directory is never a valid target";
}

TEST(PathContainmentTest, EmptyRhsIsRejected)
{
    const auto result = ResolveContainedPath("/base/dir", "", /*canonicalize=*/false);
    EXPECT_FALSE(result.ok);
}

TEST(PathContainmentTest, WindowsDriveLetterIsRejectedEvenOnPosix)
{
    const auto result = ResolveContainedPath("/base/dir", "C:/Windows/System32", /*canonicalize=*/false);
    EXPECT_FALSE(result.ok);
}

TEST(PathContainmentTest, WindowsBackslashDriveLetterIsRejected)
{
    const auto result = ResolveContainedPath("/base/dir", "C:\\Windows\\System32", /*canonicalize=*/false);
    EXPECT_FALSE(result.ok);
}

TEST(PathContainmentTest, UncPathIsRejected)
{
    const auto result = ResolveContainedPath("/base/dir", "\\\\server\\share\\file", /*canonicalize=*/false);
    EXPECT_FALSE(result.ok);
}

TEST(PathContainmentTest, EmptyBaseDirTreatsCurrentDirectoryAsBase)
{
    const auto result = ResolveContainedPath("", "sub/file.txt", /*canonicalize=*/false);
    EXPECT_TRUE(result.ok);
}

TEST(PathContainmentTest, EmptyBaseDirStillRejectsEscape)
{
    const auto result = ResolveContainedPath("", "../outside", /*canonicalize=*/false);
    EXPECT_FALSE(result.ok);
}

// Real-filesystem tests below (canonicalize=true, the default) -- exercise real directories/
// symlinks, unlike the purely-lexical cases above.

TEST(PathContainmentTest, RealNestedFileIsContainedWithCanonicalization)
{
    ScratchDir root;
    std::filesystem::create_directories(root.path() / "sub");
    std::ofstream(root.path() / "sub" / "file.txt") << "x";

    const auto result = ResolveContainedPath(root.path().string(), "sub/file.txt");
    EXPECT_TRUE(result.ok);
}

TEST(PathContainmentTest, SymlinkEscapingBaseIsRejected)
{
    ScratchDir root;
    ScratchDir outside;
    std::ofstream(outside.path() / "secret.txt") << "secret";

    std::error_code ec;
    std::filesystem::create_directory_symlink(outside.path(), root.path() / "escape", ec);
    if (ec)
    {
        GTEST_SKIP() << "symlink creation not permitted in this environment";
    }

    const auto result = ResolveContainedPath(root.path().string(), "escape/secret.txt");
    EXPECT_FALSE(result.ok) << "a real symlink under the base pointing outside it must be caught";
}

TEST(PathContainmentTest, ResultPathIsUsableAndCorrect)
{
    ScratchDir root;
    std::filesystem::create_directories(root.path() / "sub");
    std::ofstream(root.path() / "sub" / "file.txt") << "hello";

    const auto result = ResolveContainedPath(root.path().string(), "sub/file.txt");
    ASSERT_TRUE(result.ok);
    EXPECT_TRUE(std::filesystem::exists(result.resolvedPath));
}
