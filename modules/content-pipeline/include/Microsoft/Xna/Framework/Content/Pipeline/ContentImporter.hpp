// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <type_traits>
#include <utility>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentObject.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentTypeName.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/IContentImporter.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    /**
     * @brief Implements a file format importer for use with game assets.
     *
     * Derive from this class, override `Import`, and register the class with a
     * `ContentImporterAttribute` descriptor (docs/xna-content-pipeline-compat-api.md §4). A
     * reference-typed @p T is returned as `std::shared_ptr<T>`, a value type by value; an
     * importer declared over `System::Object` returns an already boxed @ref ContentObject, which
     * is what `XmlImporter` does.
     *
     * @tparam T Type of the imported content.
     */
    template<typename T>
    class ContentImporter : public IContentImporter
    {
    public:
        /** @brief The carrier `Import` returns: `std::shared_ptr<T>`, `T`, or `ContentObject` for `object`. */
        using ImportResult = std::conditional_t<std::is_same_v<T, System::Object>, ContentObject, Carrier<T>>;

        /**
         * @brief Imports an asset from the specified file.
         *
         * @param filename Name of the game asset file.
         * @param context Contains any required custom importer logic.
         * @return The imported content.
         */
        [[nodiscard]] virtual ImportResult Import(const std::string& filename,
                                                  ContentImporterContext& context) = 0;

    protected:
        /** @brief Initializes an importer. */
        ContentImporter() = default;

        /**
         * @brief Boxes the typed result for the untyped interface.
         *
         * @param filename Name of the game asset file.
         * @param context Contains any required custom importer logic.
         * @return The imported content, boxed under `ContentTypeName<T>::Name()`.
         */
        [[nodiscard]] ContentObject ImportObject(const std::string& filename,
                                                 ContentImporterContext& context) override
        {
            if constexpr (std::is_same_v<T, System::Object>)
            {
                return Import(filename, context);
            }
            else
            {
                return Box<T>(Import(filename, context));
            }
        }
    };
}
