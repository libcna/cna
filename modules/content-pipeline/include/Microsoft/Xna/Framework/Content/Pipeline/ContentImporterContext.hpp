// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <string_view>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentBuildLogger.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    /**
     * @brief Provides properties that define logging behavior for the importer.
     *
     * Abstract, as in XNA, so a test can subclass it; the pipeline supplies an implementation
     * over the canonical importer context (`CNA/Content/Pipeline/XnaPipelineBridge.hpp`).
     */
    class ContentImporterContext
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.ContentImporterContext";

        /** @brief Initializes a context. */
        ContentImporterContext() = default;

        /** @brief Destroys the context. */
        virtual ~ContentImporterContext() = default;

        /**
         * @brief Gets the name of the directory that contains intermediate build files.
         *
         * @return The intermediate directory, or empty when the host provides none.
         */
        [[nodiscard]] virtual std::string getIntermediateDirectoryProperty() const = 0;

        /**
         * @brief Gets the logger for an importer.
         *
         * @return The logger; valid for the duration of the import call.
         */
        [[nodiscard]] virtual ContentBuildLogger& getLoggerProperty() const = 0;

        /**
         * @brief Gets the name of the directory that contains the final build output.
         *
         * @return The output directory, or empty when the host provides none.
         */
        [[nodiscard]] virtual std::string getOutputDirectoryProperty() const = 0;

        /**
         * @brief Adds a dependency to the specified file: the asset is rebuilt when it changes.
         *
         * @param filename Name of the file the asset depends on.
         */
        virtual void AddDependency(const std::string& filename) = 0;
    };
}
