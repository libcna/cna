// SPDX-License-Identifier: MS-PL

#ifndef CNA_TEST_SUPPORT_TEST_PATHS_HPP
#define CNA_TEST_SUPPORT_TEST_PATHS_HPP

#include <filesystem>
#include <system_error>

namespace CNA::TestSupport
{
    /**
     * @brief Locates the repository root by walking up from the working directory.
     *
     * Test fixtures used to be found through `std::filesystem::path(__FILE__)`, which held while
     * `__FILE__` was absolute. It stopped being absolute when the build began normalizing source
     * paths for ccache: the compiler is now handed a path relative to the **build** directory,
     * while the tests run from the **repository root**, so every such lookup silently pointed one
     * level beside the tree and opened nothing. Twenty-four tests failed on empty fixtures rather
     * than on a missing file, which is the shape of failure that costs the most to read.
     *
     * Walking up is what makes this independent of both: nothing reaches the compiler's command
     * line, so there is nothing for path normalization to rewrite, and the answer is the same
     * whether a binary is run from the repository root, from a build directory or from anywhere
     * below either.
     *
     * @return The repository root, or an empty path when the working directory is outside a CNA
     *         checkout -- in which case the fixture accessors below answer empty too and a caller
     *         sees the same "fixture is missing" it would see for a deleted file.
     */
    [[nodiscard]] inline std::filesystem::path RepositoryRoot()
    {
        std::error_code error;
        std::filesystem::path directory = std::filesystem::current_path(error);
        if (error)
        {
            return {};
        }
        for (;;)
        {
            // Two markers rather than one: `modules/` alone appears in plenty of trees, and
            // CHECKLIST.md alone could be carried by a copied file.
            if (std::filesystem::exists(directory / "CHECKLIST.md", error) &&
                std::filesystem::is_directory(directory / "modules", error))
            {
                return directory;
            }
            const std::filesystem::path parent = directory.parent_path();
            if (parent == directory)
            {
                return {};
            }
            directory = parent;
        }
    }

    /**
     * @brief The directory holding the committed compiled-effect binaries (`.fxb`).
     * @return The directory, or an empty path when the repository root cannot be located.
     */
    [[nodiscard]] inline std::filesystem::path CompiledEffectDirectory()
    {
        const std::filesystem::path root = RepositoryRoot();
        return root.empty() ? std::filesystem::path{} : root / "modules/renderers/fna3d/effects";
    }

    /**
     * @brief The directory holding the compiled-effect test fixtures (oracles, reflection dumps).
     * @return The directory, or an empty path when the repository root cannot be located.
     */
    [[nodiscard]] inline std::filesystem::path CompiledEffectFixtureDirectory()
    {
        const std::filesystem::path root = RepositoryRoot();
        return root.empty() ? std::filesystem::path{} : root / "tests/fixtures/compiled-effects";
    }
}

#endif
