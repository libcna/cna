// SPDX-License-Identifier: MS-PL
// Contracts measured by cna-demos/docs/repair-evidence/storage/TitlePathsOracle.cs.
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <typeinfo>
#include "Microsoft/Xna/Framework/TitleContainer.hpp"
#include "Microsoft/Xna/Framework/TitleLocation.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/IO/FileNotFoundException.hpp"

using namespace Microsoft::Xna::Framework;
namespace fs = std::filesystem;
namespace
{
class TitleContainerPathTest : public ::testing::Test
{
protected:
    fs::path root;
    fs::path previousDirectory;
    std::string previousTitle;
    bool ownsFixture = false;

    void SetUp() override
    {
        previousDirectory = fs::current_path();
        previousTitle = TitleLocation::Path();
        std::random_device entropy;
        root = fs::temp_directory_path() / ("CnaTitlePaths-" + std::to_string(entropy()) +
                                             "-" + std::to_string(entropy()));
        ASSERT_TRUE(fs::create_directory(root));
        ownsFixture = true;
        ASSERT_TRUE(fs::create_directory(root / "child"));
        std::ofstream seed(root / "seed.bin", std::ios::binary);
        seed.write("\1\2\3\4", 4);
        ASSERT_TRUE(seed.good());
        TitleLocation::setPathProperty(root.string());
        fs::current_path(root / "child");
    }

    void TearDown() override
    {
        fs::current_path(previousDirectory);
        TitleLocation::setPathProperty(previousTitle);
        if (!ownsFixture) return;
        // Remove only the files/directories created by this exact fixture.
        std::error_code ignored;
        fs::remove(root / "seed.bin", ignored);
        fs::remove(root / "child", ignored);
        fs::remove(root, ignored);
    }

    template<class Exception> void Reject(const std::string& name)
    {
        SCOPED_TRACE(name.size() <= 80 ? name : name.substr(0, 64) + "...[bytes=" + std::to_string(name.size()) + "]");
        try
        {
            auto stream = TitleContainer::OpenStream(name);
            ADD_FAILURE() << "Expected " << typeid(Exception).name();
        }
        catch (const std::exception& error)
        {
            EXPECT_EQ(typeid(error), typeid(Exception));
        }
    }
};
}

TEST_F(TitleContainerPathTest, RelativePathsRemainTitleRelativeAfterChangingWorkingDirectory)
{
    for (const char* path : {"seed.bin", ".\\seed.bin", "./seed.bin", "child/../seed.bin"})
    {
        SCOPED_TRACE(path);
        auto stream = TitleContainer::OpenStream(path);
        ASSERT_NE(stream, nullptr);
        EXPECT_EQ(stream->getLengthProperty(), 4);
        EXPECT_EQ(stream->ReadByte(), 1);
        EXPECT_TRUE(stream->getCanReadProperty());
        EXPECT_TRUE(stream->getCanSeekProperty());
        EXPECT_FALSE(stream->getCanWriteProperty());
    }
}

TEST_F(TitleContainerPathTest, EmptyRootedEscapingAndReservedNamesHaveExactArgumentCategories)
{
    Reject<System::ArgumentNullException>("");
    Reject<System::ArgumentException>((root / "seed.bin").string());
    for (const char* path : {"/rooted.bin", "\\rooted.bin", "C:drive-relative.bin", "../outside.bin"})
        Reject<System::ArgumentException>(path);
    for (char bad : {':', '*', '?', '"', '<', '>', '|'})
        Reject<System::ArgumentException>(std::string("bad") + bad + ".bin");
}

TEST_F(TitleContainerPathTest, MissingFileAndMissingParentBothReportFileNotFound)
{
    Reject<System::IO::FileNotFoundException>("missing.bin");
    Reject<System::IO::FileNotFoundException>("missing/child.bin");
}

TEST_F(TitleContainerPathTest, ExistingDirectoryIsAnOpenFailureRatherThanAMissingFile)
{
    Reject<System::InvalidOperationException>("child");
    // The empty-input guard runs before cleaning, not after it.
    for (const char* path : {".", "./", "child/..", "child/../"})
        Reject<System::InvalidOperationException>(path);
}

TEST_F(TitleContainerPathTest, CleanBeforeValidationPreservesTheGenuineEmptyComponentRules)
{
    // A removed segment's reserved characters are not part of the validated name.
    for (const char* path : {"missing/../seed.bin", "bad*/../seed.bin", "seed.bin/."})
    {
        SCOPED_TRACE(path);
        try
        {
            auto stream = TitleContainer::OpenStream(path);
            ASSERT_NE(stream, nullptr);
            EXPECT_EQ(stream->ReadByte(), 1);
        }
        catch (const std::exception& error)
        {
            ADD_FAILURE() << error.what();
        }
    }
    // XNA removes the empty/dot component before '..', not the 'child' component.
    Reject<System::IO::FileNotFoundException>("child//../seed.bin");
    Reject<System::IO::FileNotFoundException>("child/././../seed.bin");
}

TEST_F(TitleContainerPathTest, FrameworkFourUriLengthLimitCountsUtf16RatherThanUtf8Bytes)
{
    Reject<System::ArgumentException>(std::string(65520, 'a'));
    Reject<System::InvalidOperationException>(std::string(65519, 'a'));
    std::string astral;
    for (int index = 0; index < 32760; ++index) astral += "\xF0\x9F\x98\x80";
    Reject<System::ArgumentException>(astral);
    astral.resize(astral.size() - 4);
    Reject<System::InvalidOperationException>(astral);
}

TEST_F(TitleContainerPathTest, EmbeddedNulIsAnOpenFailureEvenIfThePrefixIsMissing)
{
    Reject<System::InvalidOperationException>(std::string("seed.bin") + std::string("\0suffix", 7));
    Reject<System::InvalidOperationException>(std::string("missing.bin") + std::string("\0suffix", 7));
    auto seed = TitleContainer::OpenStream("seed.bin");
    EXPECT_EQ(seed->getLengthProperty(), 4);
    EXPECT_EQ(seed->ReadByte(), 1);
}
