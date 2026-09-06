// SPDX-License-Identifier: MS-PL
#pragma once

#include <map>
#include <string>
#include <vector>

#include "CNA/CNAHelper.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Tasks
{
    /**
     * @brief What an XNA component name means to CNA's canonical pipeline.
     *
     * A `.contentproj` names its importer and processor by the class name of a Microsoft
     * component. The canonical engine names its own components `CNA.*`, so something has to
     * translate, and translating in one place is what keeps `BuildContent` and the
     * `.contentproj` reader from disagreeing.
     *
     * The translation is not only a name. Three of XNA's processors -- `TextureProcessor`,
     * `SpriteTextureProcessor` and `ModelTextureProcessor` -- are the same processor with
     * different defaults, and the difference is observable: a model texture is DXT-compressed
     * with a full mip chain where a sprite texture is not (measured, and recorded in the parity
     * report's processor-default table). So a mapping carries the parameters that make the
     * canonical processor behave as the named XNA one, and an explicit `ProcessorParameters`
     * entry in the project overrides them.
     */
    struct XnaComponentMapping
    {
        /** @brief The canonical component this XNA name selects, or empty when none does. */
        std::string canonicalName;

        /**
         * @brief Parameters that make the canonical component behave as the named XNA one.
         *
         * Applied before the project's own `ProcessorParameters`, so a project that names a value
         * explicitly still wins.
         */
        std::vector<std::pair<std::string, std::string>> defaults;

        /** @brief Why no canonical component matches, when @ref canonicalName is empty. */
        std::string reason;
    };

    /**
     * @brief Translates an XNA importer name.
     *
     * @param xnaName The class name a content project wrote, with or without its namespace.
     * @return The mapping; `canonicalName` is empty and `reason` says why when there is none.
     */
    CNAEXT [[nodiscard]] XnaComponentMapping MapXnaImporterName(const std::string& xnaName);

    /**
     * @brief Translates an XNA processor name.
     *
     * @param xnaName The class name a content project wrote, with or without its namespace.
     * @return The mapping; `canonicalName` is empty and `reason` says why when there is none.
     */
    CNAEXT [[nodiscard]] XnaComponentMapping MapXnaProcessorName(const std::string& xnaName);

    /**
     * @brief Every XNA importer name this build can route, in name order.
     *
     * @return The names.
     */
    CNAEXT [[nodiscard]] std::vector<std::string> KnownXnaImporterNames();

    /**
     * @brief Every XNA processor name this build can route, in name order.
     *
     * @return The names.
     */
    CNAEXT [[nodiscard]] std::vector<std::string> KnownXnaProcessorNames();
}
