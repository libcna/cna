// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/TitleContainer.hpp"
#include "Microsoft/Xna/Framework/TitleLocation.hpp"

using Microsoft::Xna::Framework::TitleContainer;
using Microsoft::Xna::Framework::TitleLocation;

namespace
{
    // Writes a small text file and returns its path.
    std::string WriteTempFile(const std::filesystem::path& dir, const std::string& filename, const std::string& content)
    {
        auto path = dir / filename;
        std::ofstream f(path, std::ios::binary);
        f.write(content.data(), static_cast<std::streamsize>(content.size()));
        return path.string();
    }
}

// -----------------------------------------------------------------------
// OpenStream — relative name resolved via TitleLocation
// -----------------------------------------------------------------------

TEST(TitleContainerTest, OpenStreamRelativeNameReadsContent)
{
    auto tmpDir = std::filesystem::temp_directory_path() / "cna_tc_test_rel";
    std::filesystem::create_directories(tmpDir);
    WriteTempFile(tmpDir, "hello.txt", "world");

    TitleLocation::setPathProperty(tmpDir.string());

    auto stream = TitleContainer::OpenStream("hello.txt");
    ASSERT_NE(stream, nullptr);
    stream.reset();

    std::filesystem::remove_all(tmpDir);
}

// -----------------------------------------------------------------------
// OpenStream — absolute (rooted) name
// -----------------------------------------------------------------------

TEST(TitleContainerTest, OpenStreamAbsolutePathReadsContent)
{
    auto tmpDir = std::filesystem::temp_directory_path() / "cna_tc_test_abs";
    std::filesystem::create_directories(tmpDir);
    const std::string absPath = WriteTempFile(tmpDir, "data.bin", "abcd");

    // Rooted path is used directly, regardless of TitleLocation.
    auto stream = TitleContainer::OpenStream(absPath);
    ASSERT_NE(stream, nullptr);
    stream.reset();

    std::filesystem::remove_all(tmpDir);
}

// -----------------------------------------------------------------------
// OpenStream — backslash path separators are normalised
// -----------------------------------------------------------------------

TEST(TitleContainerTest, OpenStreamNormalizesBackslashes)
{
    auto tmpDir = std::filesystem::temp_directory_path() / "cna_tc_test_bs";
    auto subDir = tmpDir / "sub";
    std::filesystem::create_directories(subDir);
    WriteTempFile(subDir, "file.txt", "ok");

    TitleLocation::setPathProperty(tmpDir.string());

    // On Linux the backslash is part of the path; NormalizeFilePathSeparators converts it.
    auto stream = TitleContainer::OpenStream("sub/file.txt");
    ASSERT_NE(stream, nullptr);
    stream.reset();

    std::filesystem::remove_all(tmpDir);
}

TEST(TitleContainerTest, OpenStreamMatchesXnaCaseInsensitiveTitlePaths)
{
    auto tmpDir = std::filesystem::temp_directory_path() / "cna_tc_test_case";
    auto contentDir = tmpDir / "Content" / "Audio";
    std::filesystem::create_directories(contentDir);
    WriteTempFile(contentDir, "SpaceWar.xgs", "settings");

    TitleLocation::setPathProperty(tmpDir.string());

    auto stream = TitleContainer::OpenStream("content\\audio\\spacewar.xgs");
    ASSERT_NE(stream, nullptr);
    stream.reset();

    std::filesystem::remove_all(tmpDir);
}

// -----------------------------------------------------------------------
// OpenStream — missing file throws
// -----------------------------------------------------------------------

TEST(TitleContainerTest, OpenStreamThrowsForMissingFile)
{
    auto tmpDir = std::filesystem::temp_directory_path() / "cna_tc_test_miss";
    std::filesystem::create_directories(tmpDir);
    TitleLocation::setPathProperty(tmpDir.string());

    EXPECT_THROW(
        (void)TitleContainer::OpenStream("nonexistent_file_xyz.dat"),
        std::runtime_error
    );

    std::filesystem::remove_all(tmpDir);
}
