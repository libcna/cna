// SPDX-License-Identifier: MS-PL
//
// plans/plan_gltf.md GLTF-392: a shared-importer defect may not be fixed inside a renderer.
//
// §6's rule exists because the opposite is so easy and so cheap in the moment. A model renders
// wrong on one renderer; the renderer is where the wrongness is visible; a two-line adjustment
// there makes it look right. What has actually happened is that the importer's defect is now
// compensated on one of N renderers, the other N-1 stay wrong, and the compensation is invisible to
// every layer of the oracle ladder below L7 -- because L1 through L6 never look at a renderer at
// all.
//
// The rule is written in `docs/gltf-conventions.md`. This file is the part a compiler can check:
// the dependency direction. A renderer that cannot even *see* the importer cannot be the place a
// glTF import decision is made.

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "GltfFixtureCorpus.hpp"

namespace
{
    std::filesystem::path RepositoryRoot()
    {
        const std::filesystem::path corpus = CnaTest::GltfOracle::CorpusDirectory();
        if (corpus.empty()) { return {}; }
        return corpus.parent_path().parent_path().parent_path();
    }

    bool IsSource(const std::filesystem::path& path)
    {
        const std::string extension = path.extension().string();
        return extension == ".cpp" || extension == ".hpp" || extension == ".h";
    }

    std::string ReadFile(const std::filesystem::path& path)
    {
        std::ifstream file(path);
        return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    }
}

TEST(GltfSharedDefectPolicy, NoRendererCanSeeTheGltfImporter)
{
    // The direction that matters. If a renderer includes the importer, then "fix it in the
    // renderer" is available as a shortcut, and the first time someone takes it the fix is
    // invisible to every rung of the ladder and absent from every other renderer.
    const std::filesystem::path renderers = RepositoryRoot() / "modules" / "renderers";
    ASSERT_TRUE(std::filesystem::is_directory(renderers)) << "cannot find " << renderers;

    int scanned = 0;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::recursive_directory_iterator(renderers))
    {
        if (!entry.is_regular_file() || !IsSource(entry.path())) { continue; }
        ++scanned;
        const std::string source = ReadFile(entry.path());
        EXPECT_EQ(std::string::npos, source.find("CNA/Internal/GltfImport/"))
            << entry.path().string()
            << " includes the glTF importer. plans/plan_gltf.md §6/GLTF-392: a renderer must not be able "
               "to make an import decision -- a fix applied there is invisible to L1-L6 (which "
               "never look at a renderer) and absent from every other renderer.";
    }
    EXPECT_GT(scanned, 100) << "too few renderer sources scanned -- the directory layout moved";
}

TEST(GltfSharedDefectPolicy, TheGltfImporterCanSeeNoParticularRenderer)
{
    // The converse, and the one that keeps the importer portable: it may depend on the renderer
    // *contract* (`IGraphicsRenderer`, `GpuDrawParams`) and on nothing that names a renderer. The
    // day it includes EasyGL's header is the day "correct" means "correct on EasyGL", which is
    // what makes GLTF-017's L1-L5 renderer-independence claim checkable at all.
    const std::filesystem::path importer =
        RepositoryRoot() / "modules" / "content" / "src" / "GltfImport";
    ASSERT_TRUE(std::filesystem::is_directory(importer)) << "cannot find " << importer;

    int scanned = 0;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::recursive_directory_iterator(importer))
    {
        if (!entry.is_regular_file() || !IsSource(entry.path())) { continue; }
        ++scanned;
        const std::string source = ReadFile(entry.path());
        // "Renderers/Common/" is the shared contract every renderer implements and is allowed;
        // any other renderer path names one implementation.
        for (std::string::size_type at = source.find("CNA/Internal/Renderers/");
             at != std::string::npos; at = source.find("CNA/Internal/Renderers/", at + 1))
        {
            const std::string tail = source.substr(at, 64);
            EXPECT_EQ(0u, tail.rfind("CNA/Internal/Renderers/Common/", 0))
                << entry.path().string() << " includes '" << tail.substr(0, tail.find('"'))
                << "', which names one renderer. The importer may depend on the renderer CONTRACT "
                   "and on no implementation of it (GLTF-392).";
        }
    }
    EXPECT_GT(scanned, 0);
}
