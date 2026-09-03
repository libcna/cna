// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline.md XNAP-9B: there must be exactly one XNB writer architecture.
//
// For a while there were two. Two branches independently implemented the same subsystem in
// different directories (§0, §0.5), the histories were reconciled, and one implementation
// survived. Nothing in a compiler stops the other one being reintroduced -- a well-meaning
// merge, a revert, a copy of an old branch -- and the second time it happened nobody would be
// surprised by the first place to look, so the shape of the resolution is asserted here rather
// than only described in a document.
//
// This test reads the source tree, not the build. It assumes the repository root is the working
// directory, which is how every other content test locates tests/assets.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace
{
    /** @brief Skips the whole suite when the tests were not started from the repository root. */
    [[nodiscard]] bool RepositoryRootIsCurrentDirectory()
    {
        return std::filesystem::exists("modules/content/include/CNA/Internal/Xnb/XnbWriter.hpp");
    }

    /**
     * @brief Every source, header and CMake file that could plausibly name a content path.
     *
     * Scoped to the content modules, the tools and the build scripts rather than the whole tree:
     * a reference to an XNB writer header cannot appear in a renderer without also appearing in
     * one of these, and scanning every renderer costs seconds for nothing.
     */
    [[nodiscard]] std::vector<std::filesystem::path> SourceFiles()
    {
        std::vector<std::filesystem::path> files;
        for (const char* root : {"modules/content", "modules/content-pipeline", "modules/graphics",
                                 "tools", "cmake", "tests/interop"})
        {
            if (!std::filesystem::is_directory(root)) { continue; }
            for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
            {
                if (!entry.is_regular_file()) { continue; }
                const std::string extension = entry.path().extension().string();
                if (extension == ".cpp" || extension == ".hpp" || extension == ".h" ||
                    extension == ".cc" || extension == ".txt" || extension == ".cmake")
                {
                    files.push_back(entry.path());
                }
            }
        }
        std::sort(files.begin(), files.end());
        return files;
    }

    [[nodiscard]] std::string ReadText(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(stream),
                           std::istreambuf_iterator<char>());
    }
}

TEST(XnbArchitectureUniquenessTest, TheSurvivingWriterArchitectureIsWhereItSaysItIs)
{
    if (!RepositoryRootIsCurrentDirectory()) { GTEST_SKIP() << "not run from the repository root"; }

    // The writer stack, its output-pipeline adapter, and the input pipeline it must not be
    // confused with.
    for (const char* path : {
             "modules/content/include/CNA/Internal/Xnb/XnbWriter.hpp",
             "modules/content/include/CNA/Internal/Xnb/XnbAssetWriter.hpp",
             "modules/content/include/CNA/Internal/Xnb/XnbTypeWriter.hpp",
             "modules/content/include/CNA/Internal/Xnb/XnbBuiltInWriters.hpp",
             "modules/content/src/Xnb/XnbWriter.cpp",
             "modules/content/include/CNA/Content/Pipeline/XnbOutputContentPipeline.hpp",
             "modules/content/include/CNA/Content/Pipeline/XnbContentPipeline.hpp",
             "tools/xnb/xnb_conformance.py",
         })
    {
        EXPECT_TRUE(std::filesystem::exists(path)) << path << " is missing";
    }
}

TEST(XnbArchitectureUniquenessTest, TheSupersededWriterArchitecturesPathsDoNotExist)
{
    if (!RepositoryRootIsCurrentDirectory()) { GTEST_SKIP() << "not run from the repository root"; }

    // plans/plan_xnapipeline.md §0.5.1's removal table, asserted. A directory reappearing here
    // means a second writer architecture is back in the tree.
    for (const char* path : {
             "modules/content/include/CNA/Content/Xnb",
             "modules/content/src/XnbWrite",
             "modules/content/src/Pipeline/XnbOutput.cpp",
             "modules/content/src/Pipeline/XnbModelOutput.cpp",
             "modules/content/include/CNA/Content/Pipeline/XnbOutput.hpp",
             "scripts/xnb_conformance.py",
         })
    {
        EXPECT_FALSE(std::filesystem::exists(path))
            << path << " is a path of the superseded XNB writer architecture "
            << "(plans/plan_xnapipeline.md §0.5.1). One writer, not two.";
    }
}

TEST(XnbArchitectureUniquenessTest, NoSourceFileIncludesTheSupersededWriterHeaders)
{
    if (!RepositoryRootIsCurrentDirectory()) { GTEST_SKIP() << "not run from the repository root"; }

    // A stale include is how a removed architecture comes back: the header is restored to fix a
    // build error rather than the include being deleted.
    const std::vector<std::string> forbidden = {
        "CNA/Content/Xnb/",
        "CNA/Content/Pipeline/XnbOutput.hpp",
    };

    const std::filesystem::path self =
        "modules/content/tests/CNA/Internal/Xnb/XnbArchitectureUniquenessTests.cpp";
    for (const std::filesystem::path& file : SourceFiles())
    {
        if (file == self) { continue; }
        const std::string text = ReadText(file);
        for (const std::string& needle : forbidden)
        {
            EXPECT_EQ(text.find(needle), std::string::npos)
                << file.string() << " references '" << needle
                << "', a path of the superseded XNB writer architecture.";
        }
    }
}
