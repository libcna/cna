// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "CNA/Internal/CaseInsensitivePath.hpp"

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

    EXPECT_EQ(CNA::Internal::ResolveExistingXnaPath(exact.string()), exact.string());
}

TEST(CaseInsensitivePathTest, ResolvesEveryCaseVariantComponent)
{
    ScratchPath scratch;
    const auto requested = scratch.getPath() / "content" / "audio" / "spacewar.xgs";
    const auto expected = scratch.getPath() / "Content" / "Audio" / "SpaceWar.xgs";

    EXPECT_EQ(CNA::Internal::ResolveExistingXnaPath(requested.string()), expected.string());
}

TEST(CaseInsensitivePathTest, NormalizesWindowsSeparators)
{
    ScratchPath scratch;
    std::string requested = (scratch.getPath() / "content" / "audio" / "spacewar.xgs").string();
    std::replace(requested.begin(), requested.end(), '/', '\\');
    const auto expected = scratch.getPath() / "Content" / "Audio" / "SpaceWar.xgs";

    EXPECT_EQ(CNA::Internal::ResolveExistingXnaPath(requested), expected.string());
}

TEST(CaseInsensitivePathTest, UnresolvedPathKeepsNormalizedRequestedSpelling)
{
    ScratchPath scratch;
    const auto requested = scratch.getPath() / "content" / "missing.xgs";

    EXPECT_EQ(CNA::Internal::ResolveExistingXnaPath(requested.string()), requested.string());
}
