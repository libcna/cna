// SPDX-License-Identifier: MS-PL
//
// REMED-CONTENT-002/-007/-008: unit tests for the shared containment helpers.

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

#include "CNA/Internal/PathContainment.hpp"

using CNA::Internal::ResolveContainedPath;
using CNA::Internal::ResolveContainedPathFromBase;
using CNA::Internal::ResolveContainedPathRelativeToFile;
using CNA::Internal::ResolveContainedUtf8Path;
using CNA::Internal::ValidateContainedNativePath;

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
    EXPECT_EQ(result.resolvedPath, "/base/dir/sub/file.txt");
}

TEST(PathContainmentTest, NestedRelativeSubpathIsContained)
{
    const auto result = ResolveContainedPath(
        "/base/dir", "music/album/song.ext", /*canonicalize=*/false);
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.resolvedPath, "/base/dir/music/album/song.ext");
}

TEST(PathContainmentTest, CurrentDirectorySegmentIsNormalizedWithinBase)
{
    const auto result = ResolveContainedPath(
        "/base/dir", "./textures/a.png", /*canonicalize=*/false);
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.resolvedPath, "/base/dir/textures/a.png");
}

TEST(PathContainmentTest, InternalParentSegmentIsNormalizedWithinBase)
{
    const auto result = ResolveContainedPath(
        "/base/dir", "textures/../textures/a.png", /*canonicalize=*/false);
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.resolvedPath, "/base/dir/textures/a.png");
}

TEST(PathContainmentTest, AbsoluteRhsIsRejected)
{
    const auto result = ResolveContainedPath("/base/dir", "/etc/passwd", /*canonicalize=*/false);
    EXPECT_FALSE(result.ok);
}

TEST(PathContainmentTest, AbsolutePathInsideBaseIsStillRejectedAsUntrustedInput)
{
    const auto result = ResolveContainedPath(
        "/base/dir", "/base/dir/textures/a.png", /*canonicalize=*/false);
    EXPECT_FALSE(result.ok);
}

TEST(PathContainmentTest, DotDotTraversalEscapingBaseIsRejected)
{
    const auto result = ResolveContainedPath("/base/dir", "../../etc/passwd", /*canonicalize=*/false);
    EXPECT_FALSE(result.ok);
}

TEST(PathContainmentTest, DeepDotDotTraversalEscapingBaseIsRejected)
{
    const auto result = ResolveContainedPath(
        "/base/dir", "a/b/../../../outside", /*canonicalize=*/false);
    EXPECT_FALSE(result.ok);
}

TEST(PathContainmentTest, SiblingPrefixIsNotMistakenForContainedDirectory)
{
    const auto result = ResolveContainedPath(
        "/base/content", "../content-evil/secret.bin", /*canonicalize=*/false);
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

TEST(PathContainmentTest, WindowsDriveRelativeFormIsRejectedEvenOnPosix)
{
    const auto result = ResolveContainedPath(
        "/base/dir", "C:Windows\\System32", /*canonicalize=*/false);
    EXPECT_FALSE(result.ok);
}

TEST(PathContainmentTest, WindowsRootedBackslashFormIsRejectedEvenOnPosix)
{
    const auto result = ResolveContainedPath(
        "/base/dir", "\\Windows\\System32", /*canonicalize=*/false);
    EXPECT_FALSE(result.ok);
}

TEST(PathContainmentTest, UncPathIsRejected)
{
    const auto result = ResolveContainedPath("/base/dir", "\\\\server\\share\\file", /*canonicalize=*/false);
    EXPECT_FALSE(result.ok);
}

TEST(PathContainmentTest, RepeatedAndMixedSeparatorsNormalizeWithinBase)
{
    const auto result = ResolveContainedPath(
        "/base/dir", "textures//nested\\a.png", /*canonicalize=*/false);
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.resolvedPath, "/base/dir/textures/nested/a.png");
}

TEST(PathContainmentTest, TrailingSeparatorBelowBaseRemainsContained)
{
    const auto result = ResolveContainedPath(
        "/base/dir", "textures/", /*canonicalize=*/false);
    EXPECT_TRUE(result.ok);
}

TEST(PathContainmentTest, DotDotCharactersInsideFilenameAreNotTraversal)
{
    const auto result = ResolveContainedPath(
        "/base/dir", "music/track..mix.ogg", /*canonicalize=*/false);
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.resolvedPath, "/base/dir/music/track..mix.ogg");
}

TEST(PathContainmentTest, NestedJoinBaseMayClimbWithinAuthorizedRoot)
{
    const auto result = ResolveContainedPathFromBase(
        "/content", "/content/music/album", "../shared/song.ogg", /*canonicalize=*/false);
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.resolvedPath, "/content/music/shared/song.ogg");
}

TEST(PathContainmentTest, NestedJoinBaseCannotClimbPastAuthorizedRoot)
{
    const auto result = ResolveContainedPathFromBase(
        "/content", "/content/music/album", "../../../content-evil/song.ogg",
        /*canonicalize=*/false);
    EXPECT_FALSE(result.ok);
}

TEST(PathContainmentTest, ExplicitExternalReferringFileIsConfinedToItsBundle)
{
    const auto child = ResolveContainedPathRelativeToFile(
        "/content", "/external/bundle/media.xnb", "audio/song.ogg", /*canonicalize=*/false);
    ASSERT_TRUE(child.ok);
    EXPECT_EQ(child.resolvedPath, "/external/bundle/audio/song.ogg");

    const auto escape = ResolveContainedPathRelativeToFile(
        "/content", "/external/bundle/media.xnb", "../secret.ogg", /*canonicalize=*/false);
    EXPECT_FALSE(escape.ok);
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

TEST(PathContainmentTest, NativeValidationPreservesANonAsciiFilesystemPath)
{
    ScratchDir root;
    const std::filesystem::path nested =
        root.path() / CNA::Internal::ContentPathFromUtf8("vnoření_内");
    const std::filesystem::path file =
        nested / CNA::Internal::ContentPathFromUtf8("obrázek_壁.bin");
    std::filesystem::create_directories(nested);
    std::ofstream(file) << "data";

    const auto result = ValidateContainedNativePath(root.path(), file);
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.resolvedPath, file.lexically_normal());
    EXPECT_EQ(CNA::Internal::ContentPathToUtf8(result.resolvedPath.filename()),
              "obrázek_壁.bin");
}

TEST(PathContainmentTest, Utf8ResolverUsesNativePathsAndRetainsContainmentPolicy)
{
    ScratchDir root;
    const std::filesystem::path file =
        root.path() / CNA::Internal::ContentPathFromUtf8("geometrie_形/údaje_ß.bin");
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file) << "data";

    const auto result = ResolveContainedUtf8Path(root.path(), "geometrie_形/údaje_ß.bin");
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.resolvedPath, file.lexically_normal());
    EXPECT_FALSE(ResolveContainedUtf8Path(root.path(), "../outside.bin").ok);
    EXPECT_FALSE(ResolveContainedUtf8Path(root.path(), "C:/outside.bin").ok);
    EXPECT_FALSE(ResolveContainedUtf8Path(root.path(), "\\\\server\\share\\outside.bin").ok);
}
