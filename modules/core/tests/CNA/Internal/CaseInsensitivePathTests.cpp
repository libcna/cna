// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "CNA/Internal/CaseInsensitivePath.hpp"
#include "CNA/Internal/PathUtf8.hpp"

// plans/plan_graphics_shared_cleanup.md GSC-0007 (formerly WINNATIVE-F26, two deliberately red tests on
// Windows). ResolveExistingXnaPath promises a spelling that OPENS the file XNA content named with any
// ASCII casing, not the casing stored on disk: on a case-insensitive filesystem the requested spelling
// already opens and is returned without a directory scan. So every case asserts what the function
// promises on the filesystem the scratch directory is actually on -- the resolved path opens the right
// file -- and pins the spelling each kind of filesystem produces, decided by probing that filesystem
// rather than by the compile target.

namespace
{
    class ScratchPath
    {
    public:
        ScratchPath()
        {
            const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
            path_ = std::filesystem::temp_directory_path() /
                    ("cna_case_insensitive_path_" + std::to_string(suffix));
            std::filesystem::create_directories(path_ / "Content" / "Audio");
            std::ofstream(path_ / "Content" / "Audio" / "SpaceWar.xgs") << "fixture";

            std::error_code ec;
            caseSensitive_ = !std::filesystem::exists(path_ / "content", ec);
        }

        ~ScratchPath()
        {
            std::error_code ec;
            std::filesystem::remove_all(path_, ec);
        }

        [[nodiscard]] const std::filesystem::path& getPath() const { return path_; }

        /** @brief Whether the scratch directory's filesystem tells "Content" from "content". */
        [[nodiscard]] bool isCaseSensitive() const { return caseSensitive_; }

    private:
        std::filesystem::path path_;
        bool caseSensitive_ = true;
    };

    std::string ReadAll(const std::string& utf8Path)
    {
        std::ifstream stream(CNA::Internal::PathFromUtf8(utf8Path), std::ios::binary);
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }

    void ExpectOpensTheOnDiskFile(const std::string& resolved, const std::filesystem::path& onDisk)
    {
        std::error_code ec;
        EXPECT_TRUE(std::filesystem::equivalent(CNA::Internal::PathFromUtf8(resolved), onDisk, ec))
            << resolved << " does not name " << CNA::Internal::PathToGenericUtf8(onDisk)
            << (ec ? " (" + ec.message() + ")" : std::string());
        EXPECT_EQ(ReadAll(resolved), "fixture") << resolved;
    }
}

TEST(CaseInsensitivePathTest, ExactExistingPathIsPreserved)
{
    ScratchPath scratch;
    const auto exact = scratch.getPath() / "Content" / "Audio" / "SpaceWar.xgs";

    // Compared in generic UTF-8, which is what ResolveExistingXnaPath documents itself as
    // returning. Comparing against path::string() asserted native separators the function never
    // promised -- invisible on POSIX, where the two spellings coincide, and a guaranteed failure
    // on Windows.
    EXPECT_EQ(CNA::Internal::ResolveExistingXnaPath(CNA::Internal::PathToGenericUtf8(exact)),
              CNA::Internal::PathToGenericUtf8(exact));
}

TEST(CaseInsensitivePathTest, ResolvesEveryCaseVariantComponent)
{
    ScratchPath scratch;
    const auto requested = scratch.getPath() / "content" / "audio" / "spacewar.xgs";
    const auto onDisk = scratch.getPath() / "Content" / "Audio" / "SpaceWar.xgs";

    const std::string resolved =
        CNA::Internal::ResolveExistingXnaPath(CNA::Internal::PathToGenericUtf8(requested));
    ExpectOpensTheOnDiskFile(resolved, onDisk);

    // Case-sensitive: only the on-disk spelling opens, so the walk must have found it. Case-insensitive:
    // the request already opens and comes back unchanged, with no directory scan.
    EXPECT_EQ(resolved, CNA::Internal::PathToGenericUtf8(scratch.isCaseSensitive() ? onDisk : requested));
}

TEST(CaseInsensitivePathTest, NormalizesWindowsSeparators)
{
    ScratchPath scratch;
    const auto requestedPath = scratch.getPath() / "content" / "audio" / "spacewar.xgs";
    std::string requested = CNA::Internal::PathToGenericUtf8(requestedPath);
    std::replace(requested.begin(), requested.end(), '/', '\\');
    const auto onDisk = scratch.getPath() / "Content" / "Audio" / "SpaceWar.xgs";

    const std::string resolved = CNA::Internal::ResolveExistingXnaPath(requested);
    EXPECT_EQ(resolved.find('\\'), std::string::npos) << resolved;
    ExpectOpensTheOnDiskFile(resolved, onDisk);
    EXPECT_EQ(resolved,
              CNA::Internal::PathToGenericUtf8(scratch.isCaseSensitive() ? onDisk : requestedPath));
}

TEST(CaseInsensitivePathTest, UnresolvedPathKeepsNormalizedRequestedSpelling)
{
    ScratchPath scratch;
    const auto requested = scratch.getPath() / "content" / "missing.xgs";

    EXPECT_EQ(CNA::Internal::ResolveExistingXnaPath(CNA::Internal::PathToGenericUtf8(requested)),
              CNA::Internal::PathToGenericUtf8(requested));
}
