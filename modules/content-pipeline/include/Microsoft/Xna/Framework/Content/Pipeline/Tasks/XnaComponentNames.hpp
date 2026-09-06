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

        /**
         * @brief XNA's own obsolescence sentence for this component, or empty when it is current.
         *
         * XNA marks two of its processors obsolete and warns when a project names one, which is
         * something a project reads and acts on. Measured from the genuine build rather than
         * assumed: `SpriteTextureProcessor` and `ModelTextureProcessor` warn, the other ten do not
         * (tests/reference/xna40/differential-errors/, plans/plan_xnapipeline_parity.md
         * XNAPP-267).
         *
         * Second-to-last of the fields, before @ref known, because the tables are written as
         * aggregates and the two obsolete entries name their fields.
         */
        std::string obsoleteMessage;

        /**
         * @brief Whether XNA itself defines a component of this name.
         *
         * Not the same question as whether @ref canonicalName is empty, and the difference is a
         * build outcome rather than a nicety. `XmlImporter` and `PassThroughProcessor` are real XNA
         * components the canonical graph reaches without being named, so they map to no canonical
         * name and a project that writes them is right. A name XNA has never defined is a project
         * that will not build against a real XNA toolchain either, and XNA answers
         * `Cannot find importer "..."` rather than quietly building something else
         * (plans/plan_xnapipeline_parity.md XNAPP-267).
         *
         * Last of the fields because the tables above are written as aggregates without it; the
         * lookup sets it.
         */
        bool known = false;
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
