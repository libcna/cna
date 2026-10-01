// SPDX-License-Identifier: MS-PL
//
// CBIND-140: a relative content root is the title's, as XNA's ContentManager opens it through
// TitleContainer -- not the working directory, from which a game started anywhere else found none
// of its content.

#include "CNA/Internal/ContentRoot.hpp"
#include "CNA/Internal/PathUtf8.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <random>
#include <string>

namespace
{
    namespace fs = std::filesystem;

    class ContentRootTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            std::random_device device;
            title_ = fs::temp_directory_path() / ("cna-content-root-" + std::to_string(device()));
            fs::create_directories(title_ / "Content");
        }

        void TearDown() override
        {
            std::error_code ignored;
            fs::remove_all(title_, ignored);
        }

        [[nodiscard]] std::string Title() const { return CNA::Internal::PathToGenericUtf8(title_); }

        fs::path title_;
    };
}

TEST_F(ContentRootTest, ARelativeRootThatExistsUnderTheTitleIsReadFromThere)
{
    EXPECT_EQ(CNA::Internal::ResolveContentRoot("Content", Title()),
              CNA::Internal::PathToGenericUtf8((title_ / "Content").lexically_normal()));
}

TEST_F(ContentRootTest, ARelativeRootTheTitleDoesNotHaveKeepsTheWorkingDirectory)
{
    EXPECT_EQ(CNA::Internal::ResolveContentRoot("tests/assets", Title()), "tests/assets");
}

TEST_F(ContentRootTest, AnAbsoluteRootIsNotMoved)
{
    const std::string absolute = CNA::Internal::PathToGenericUtf8(title_ / "Content");
    EXPECT_EQ(CNA::Internal::ResolveContentRoot(absolute, "/elsewhere"), absolute);
}

TEST_F(ContentRootTest, NoTitleOrNoRootChangesNothing)
{
    EXPECT_EQ(CNA::Internal::ResolveContentRoot("Content", ""), "Content");
    EXPECT_EQ(CNA::Internal::ResolveContentRoot("", Title()), "");
}
