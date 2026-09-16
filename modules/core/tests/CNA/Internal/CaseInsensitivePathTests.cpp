// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "CNA/Internal/CaseInsensitivePath.hpp"
#include "CNA/Internal/PathUtf8.hpp"

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
        }

        ~ScratchPath()
        {
            std::error_code ec;
            std::filesystem::remove_all(path_, ec);
        }

        [[nodiscard]] const std::filesystem::path& getPath() const { return path_; }

    private:
        std::filesystem::path path_;
    };
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

// This one is expected to FAIL on Windows, and is deliberately left failing rather than guarded.
// It is WINNATIVE-F26: on a case-insensitive filesystem the early exists() check succeeds for the
// wrongly-cased path, so the walker returns the spelling it was ASKED for rather than the one on
// disk. The file opens either way; what differs is the returned string. Making it always walk
// would add a directory scan per path on the one platform that does not need it, and guarding the
// test would make it pass on Windows without checking what it claims -- which is worse than an
// honest red. See plans/plan_win32_native_validation.md WINNATIVE-F26.
TEST(CaseInsensitivePathTest, ResolvesEveryCaseVariantComponent)
{
    ScratchPath scratch;
    const auto requested = scratch.getPath() / "content" / "audio" / "spacewar.xgs";
    const auto expected = scratch.getPath() / "Content" / "Audio" / "SpaceWar.xgs";

    EXPECT_EQ(CNA::Internal::ResolveExistingXnaPath(CNA::Internal::PathToGenericUtf8(requested)),
              CNA::Internal::PathToGenericUtf8(expected));
}

// Also expected to FAIL on Windows, and for the same reason as ResolvesEveryCaseVariantComponent
// above: its input is lower-cased as well as backslash-spelled, so it asks for the on-disk casing
// too. WINNATIVE-F26 covers both. The separator half of what it checks does hold on Windows.
TEST(CaseInsensitivePathTest, NormalizesWindowsSeparators)
{
    ScratchPath scratch;
    std::string requested =
        CNA::Internal::PathToGenericUtf8(scratch.getPath() / "content" / "audio" / "spacewar.xgs");
    std::replace(requested.begin(), requested.end(), '/', '\\');
    const auto expected = scratch.getPath() / "Content" / "Audio" / "SpaceWar.xgs";

    EXPECT_EQ(CNA::Internal::ResolveExistingXnaPath(requested),
              CNA::Internal::PathToGenericUtf8(expected));
}

TEST(CaseInsensitivePathTest, UnresolvedPathKeepsNormalizedRequestedSpelling)
{
    ScratchPath scratch;
    const auto requested = scratch.getPath() / "content" / "missing.xgs";

    EXPECT_EQ(CNA::Internal::ResolveExistingXnaPath(CNA::Internal::PathToGenericUtf8(requested)),
              CNA::Internal::PathToGenericUtf8(requested));
}
