// SPDX-License-Identifier: MS-PL
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "CNA/Content/Cnb/CnbModelData.hpp"

namespace CNA::Content::Cnb
{
    /** @brief What BuildCnbModelFromCnj() produced. */
    struct CnbModelFromCnjResult
    {
        /** @brief The decoded model, ready for EncodeModelToCnb(). */
        CnbModelData model;

        /**
         * @brief Every source file whose contents are now inside @ref model, as named in the
         *        `.cnj` -- the vertex, index, morph, skeleton and clip sidecars.
         *
         * The `.cnj` document itself is not included; the caller adds it.
         */
        std::vector<std::string> absorbedFiles;

        /** @brief Logical names of the assets the model still refers to from outside. */
        std::vector<std::string> externalReferences;
    };

    /**
     * @brief Reads a `Model` `.cnj` document, and every binary sidecar it names, into the neutral
     *        CnbModelData the compiler then encodes (plans/plan_cnb.md `CNBF-073`).
     *
     * This is the compiler's own front end, and it is the one place in the CNB implementation that
     * re-reads a `.cnj` shape rather than going through the runtime reader that already
     * understands it. That is deliberate and its reasons are recorded in `plans/plan_cnb.md`
     * decision `D9`: `ModelTypeReader` builds real `VertexBuffer`/`IndexBuffer`/`Effect` objects as
     * it parses, so reusing it would require a `GraphicsDevice` -- and compiling content must not
     * need a window, a GPU or a renderer. The drift risk that creates is pinned by
     * `CnbModelEquivalenceTests.cpp`, which loads one asset through *both* paths and compares the
     * resulting models.
     *
     * Deliberately refused rather than silently dropped: material variants
     * (`materialVariantNames`/`variantOf`/`materialVariant`). See `plans/plan_cnb.md` decision `D9`
     * for why they are out of the v1 Model schema.
     *
     * @param cnjPath     Filesystem path of the `Model` `.cnj` document.
     * @param contentRoot Directory that sidecar and asset references resolve against.
     * @return The decoded model and the file lists.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if the document cannot be
     *         read, is not a `Model`, names a sidecar outside the content root, carries a field
     *         the v1 Model schema does not express, or is internally inconsistent.
     */
    [[nodiscard]] CnbModelFromCnjResult BuildCnbModelFromCnj(
        const std::filesystem::path& cnjPath, const std::filesystem::path& contentRoot);

    /**
     * @brief Compatibility overload for callers that already hold native narrow path strings.
     *
     * New filesystem-aware callers should use the native-path overload so Windows Unicode paths
     * are not narrowed before opening the CNJ document or its sidecars.
     *
     * @param cnjPath     Native narrow filesystem path of the Model CNJ document.
     * @param contentRoot Native narrow directory containing its sidecars.
     * @return The decoded model and file lists.
     */
    [[nodiscard]] CnbModelFromCnjResult BuildCnbModelFromCnj(const std::string& cnjPath,
                                                              const std::string& contentRoot);
}
