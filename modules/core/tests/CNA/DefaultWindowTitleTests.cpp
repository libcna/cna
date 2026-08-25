// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "CNA/AssemblyInfo.hpp"
#include "CNA/Internal/DefaultWindowTitle.hpp"

namespace
{
    // Each test restores whatever the process declared, so ordering cannot leak.
    class AssemblyTitleGuard
    {
    public:
        AssemblyTitleGuard() : previous_(CNA::GetAssemblyTitleEXT()) {}
        ~AssemblyTitleGuard() { CNA::SetAssemblyTitleEXT(previous_); }

    private:
        std::string previous_;
    };
}

TEST(DefaultWindowTitleTests, DeclaredAssemblyTitleWins)
{
    AssemblyTitleGuard guard;
    CNA::SetAssemblyTitleEXT("FuzzyLogic");
    EXPECT_EQ(CNA::GetAssemblyTitleEXT(), "FuzzyLogic");
    EXPECT_EQ(CNA::Internal::GetDefaultWindowTitle(), "FuzzyLogic");
}

TEST(DefaultWindowTitleTests, AttributeObjectDeclaresTheSameTitle)
{
    AssemblyTitleGuard guard;
    const CNA::AssemblyTitleAttributeEXT attribute{"AttributeDeclared"};
    (void)attribute;
    EXPECT_EQ(CNA::Internal::GetDefaultWindowTitle(), "AttributeDeclared");
}

TEST(DefaultWindowTitleTests, FallsBackToTheExecutableNameWithoutDirectoryOrExtension)
{
    AssemblyTitleGuard guard;
    CNA::SetAssemblyTitleEXT("");

    const std::string title = CNA::Internal::GetDefaultWindowTitle();
    EXPECT_FALSE(title.empty());
    EXPECT_EQ(title.find('/'), std::string::npos);
    EXPECT_EQ(title.find('\\'), std::string::npos);
    EXPECT_EQ(title.find(".exe"), std::string::npos);
#ifndef __EMSCRIPTEN__
    // This test binary is the running executable, so the fallback names it rather than
    // taking the last resort.
    EXPECT_NE(title, "Game");
#else
    // A wasm bundle has no executable file, so only the last resort is left.
    EXPECT_EQ(title, "Game");
#endif
}

TEST(DefaultWindowTitleTests, NeverReturnsAnEmptyTitle)
{
    AssemblyTitleGuard guard;
    CNA::SetAssemblyTitleEXT("");
    EXPECT_FALSE(CNA::Internal::GetDefaultWindowTitle().empty());
}

TEST(DefaultWindowTitleTests, AnEmptyDeclarationIsTreatedAsNoDeclaration)
{
    AssemblyTitleGuard guard;
    CNA::SetAssemblyTitleEXT("Declared");
    const std::string declared = CNA::Internal::GetDefaultWindowTitle();
    CNA::SetAssemblyTitleEXT("");
    EXPECT_NE(CNA::Internal::GetDefaultWindowTitle(), declared);
}
