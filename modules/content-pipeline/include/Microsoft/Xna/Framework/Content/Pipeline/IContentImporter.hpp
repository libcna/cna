// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentObject.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    class ContentImporterContext;

    /**
     * @brief Accesses a statically typed `ContentImporter<T>` through a common interface that
     *        works with untyped `object` payloads.
     *
     * `Import` here is deliberately **non-virtual**: it is the C++ spelling of the explicit
     * interface implementation `object IContentImporter.Import(...)`, reachable only through the
     * interface, while `ContentImporter<T>::Import` is the typed virtual a component overrides
     * (docs/xna-content-pipeline-compat-api.md §2).
     */
    class IContentImporter
    {
    public:
        /** @brief Destroys the importer. */
        virtual ~IContentImporter() = default;

        /**
         * @brief Imports an asset from the specified file.
         *
         * @param filename Name of the game asset file.
         * @param context Contains any required custom importer logic.
         * @return The imported content, boxed under its pipeline type name.
         */
        [[nodiscard]] ContentObject Import(const std::string& filename, ContentImporterContext& context)
        {
            return ImportObject(filename, context);
        }

    protected:
        /**
         * @brief The customization point behind the interface's `Import`; `ContentImporter<T>`
         *        implements it by boxing its typed result.
         *
         * @param filename Name of the game asset file.
         * @param context Contains any required custom importer logic.
         * @return The imported content, boxed under its pipeline type name.
         */
        CNAEXT [[nodiscard]] virtual ContentObject ImportObject(const std::string& filename,
                                                                ContentImporterContext& context) = 0;
    };
}
