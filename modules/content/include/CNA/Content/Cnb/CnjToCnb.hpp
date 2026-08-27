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
         * build script can match them against what it generated -- including a document's
         * authored subdirectory, so `art/ui/hero.png` and `art/world/hero.png` remain two
         * distinguishable entries (plans/plan_cnb.md `CNBF-118`). The source `.cnj` itself is
         * always the first entry, under its own file name.
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
     * **Supported `.cnj` `"type"` values: `Curve`, `AnimationClip`, `Model`, `Texture2D`,
     * `Texture3D`, `TextureCube`, `SpriteFont` and `SoundEffect`** -- all eight. Any other type is
     * refused by name rather than silently producing an empty file.
     *
     * Those eight reach the compiler by three different routes, and the difference matters to
     * anyone reasoning about whether a compiled asset can disagree with a loaded one:
     *
     * - **Through CNA's own runtime `.cnj` reader** -- `Curve` and `AnimationClip`. The compiler
     *   hands `ContentManager` the literal file name and encodes whatever it gets back, so there
     *   is no second interpretation of the document at all.
     * - **Through a headless importer, over the shared canonical document reader** --
     *   `Texture2D`, `Texture3D`, `TextureCube`, `SpriteFont` and `SoundEffect`. A runtime reader
     *   cannot be used for these: each of them constructs a `Texture2D`, a `SpriteFont` or a
     *   `SoundEffect`, which needs a `GraphicsDevice` or an audio device a build machine does not
     *   have. What is shared instead is the *reading of the document*
     *   (`CNA::Internal::CnjCanonicalRead`, plans/plan_cnb.md `CNBF-118`) and the containment rule
     *   for the files it names (`ResolveCnjSourceFileSafely`), so the compiler accepts and rejects
     *   exactly what the runtime does; only the construction of the runtime object differs, and
     *   the compiler does not perform it.
     * - **Through a compiler-side model builder** -- `Model`, whose runtime reader is 1100 lines
     *   with JSON parsing and `Model` construction fully interleaved. That one is a genuine second
     *   front end, and it is pinned by an explicit load-both-and-compare test; see
     *   `plans/plan_cnb.md` decision `D9`.
     *
     * The per-type `"cnjVersion"` ceiling is the same one that type's runtime reader applies:
     * version 2 for `Model`, version 1 for every other type (`CNBF-118`).
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
