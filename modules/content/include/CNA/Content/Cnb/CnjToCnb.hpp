// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace CNA::Content::Cnb
{
    /** @brief What CompileCnjToCnb() produced, and what it did with the source's sidecar files. */
    struct CnjToCnbResult
    {
        /** @brief The complete compiled `.cnb` file bytes. */
        std::vector<std::uint8_t> bytes;

        /** @brief The asset type identifier written into the file's header. */
        std::uint32_t assetTypeId = 0u;

        /** @brief Human-readable name of that asset type, as recorded in the `CMET` chunk. */
        std::string assetTypeName;

        /**
         * @brief Files whose contents are now *inside* the `.cnb` and no longer need to ship.
         *
         * Paths as they were written in the source `.cnj`, not resolved filesystem paths, so a
         * build script can match them against what it generated. The source `.cnj` itself is
         * always the first entry.
         */
        std::vector<std::string> absorbedFiles;

        /**
         * @brief Logical asset names the compiled file still refers to from outside.
         *
         * A texture shared by a hundred models stays one shared asset; embedding a copy in each
         * would defeat `ContentManager`'s cache. These are the names recorded in the `XREF` chunk.
         */
        std::vector<std::string> externalReferences;
    };

    /**
     * @brief Compiles one `.cnj` document, and the binary sidecars it names, into a single `.cnb`
     *        file image (plans/plan_cnb.md `CNBF-060`).
     *
     * The source document is loaded through CNA's own existing `.cnj` readers rather than a second
     * parser written for the compiler, so a `.cnj` that loads at runtime compiles to a `.cnb` that
     * decodes to the same thing -- there is no second interpretation of the document to drift.
     * (The `Model` front end is the one exception, and it is pinned by an explicit
     * load-both-and-compare test; see `plans/plan_cnb.md` decision `D9`.)
     *
     * Supported `.cnj` `"type"` values: `Curve`, `AnimationClip`, `Model`. Any other type is
     * refused by name rather than silently producing an empty file.
     *
     * @param cnjPath     Filesystem path of the `.cnj` document to compile.
     * @param contentRoot Directory that sidecar references are resolved against. Defaults to
     *                    @p cnjPath's own parent directory, which is where every CNA content tool
     *                    writes them.
     * @param contentName Logical asset name recorded in the debug `CMET` chunk. Defaults to
     *                    @p cnjPath's stem.
     * @return The compiled image and a record of what was absorbed and what stayed external.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if the document cannot be
     *         read, is not a supported type, or names something the compiler cannot express.
     */
    [[nodiscard]] CnjToCnbResult CompileCnjToCnb(const std::string& cnjPath,
                                                  const std::string& contentRoot = {},
                                                  const std::string& contentName = {});
}
