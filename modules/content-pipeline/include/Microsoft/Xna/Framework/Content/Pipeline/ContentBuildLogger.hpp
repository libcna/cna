// SPDX-License-Identifier: MS-PL
#pragma once

#include <format>
#include <string>
#include <string_view>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentIdentity.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    /**
     * @brief Provides methods for reporting informational messages or warnings from content
     *        importers and processors.
     *
     * The three logging members take a composite-format message and its arguments in XNA
     * (`params object[]`); here the variadic templates format with `std::format` -- indexed `{0}`
     * and sequential `{}` placeholders both work -- and forward to the non-template virtuals a
     * derived logger implements (docs/xna-content-pipeline-compat-api.md §2). A class that
     * overrides those virtuals hides the formatting overloads on its own type, as C++ name
     * lookup always does; add `using ContentBuildLogger::LogMessage;` (and the other two) to keep
     * them callable through the derived type. Calls through a `ContentBuildLogger&` -- which is
     * what every importer and processor receives -- are unaffected.
     */
    class ContentBuildLogger
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.ContentBuildLogger";

        /** @brief Destroys the logger. */
        virtual ~ContentBuildLogger() = default;

        /**
         * @brief Gets the base directory used to make reported file names relative.
         *
         * @return The root directory, or empty when file names are reported as given.
         */
        [[nodiscard]] const std::string& getLoggerRootDirectoryProperty() const noexcept;

        /**
         * @brief Sets the base directory used to make reported file names relative.
         *
         * @param value The root directory.
         */
        void setLoggerRootDirectoryProperty(std::string value);

        /**
         * @brief Outputs a high-priority status message from a content importer or processor.
         *
         * @param message The message text.
         */
        virtual void LogImportantMessage(const std::string& message) = 0;

        /**
         * @brief Outputs a low-priority status message from a content importer or processor.
         *
         * @param message The message text.
         */
        virtual void LogMessage(const std::string& message) = 0;

        /**
         * @brief Outputs a warning message from a content importer or processor.
         *
         * @param helpLink Link to an existing online description of the warning, or empty.
         * @param contentIdentity Identity of the content the warning is about; may be empty.
         * @param message The message text.
         */
        virtual void LogWarning(const std::string& helpLink, const ContentIdentity& contentIdentity,
                                const std::string& message) = 0;

        /**
         * @brief Outputs a high-priority composite-format status message.
         *
         * @tparam Args Argument types formattable by `std::format`.
         * @param message Format string.
         * @param messageArgs Values substituted into @p message.
         */
        template<typename... Args>
            requires(sizeof...(Args) > 0)
        void LogImportantMessage(std::string_view message, Args&&... messageArgs)
        {
            LogImportantMessage(std::vformat(message, std::make_format_args(messageArgs...)));
        }

        /**
         * @brief Outputs a low-priority composite-format status message.
         *
         * @tparam Args Argument types formattable by `std::format`.
         * @param message Format string.
         * @param messageArgs Values substituted into @p message.
         */
        template<typename... Args>
            requires(sizeof...(Args) > 0)
        void LogMessage(std::string_view message, Args&&... messageArgs)
        {
            LogMessage(std::vformat(message, std::make_format_args(messageArgs...)));
        }

        /**
         * @brief Outputs a composite-format warning.
         *
         * @tparam Args Argument types formattable by `std::format`.
         * @param helpLink Link to an existing online description of the warning, or empty.
         * @param contentIdentity Identity of the content the warning is about; may be empty.
         * @param message Format string.
         * @param messageArgs Values substituted into @p message.
         */
        template<typename... Args>
            requires(sizeof...(Args) > 0)
        void LogWarning(const std::string& helpLink, const ContentIdentity& contentIdentity,
                        std::string_view message, Args&&... messageArgs)
        {
            LogWarning(helpLink, contentIdentity,
                       std::vformat(message, std::make_format_args(messageArgs...)));
        }

        /**
         * @brief Outputs a message indicating that a content asset has completed processing;
         *        the file previously pushed becomes the current file again.
         *
         * @throws std::logic_error when no file is being processed.
         */
        void PopFile();

        /**
         * @brief Outputs a message indicating that a content asset has begun processing.
         *
         * @param filename Name of the file being processed.
         */
        void PushFile(std::string filename);

    protected:
        /** @brief Initializes a logger with no root directory and no file being processed. */
        ContentBuildLogger() = default;

        /**
         * @brief Gets the file name currently being processed, relative to the root directory
         *        when one is set.
         *
         * @param contentIdentity Identity whose source file wins over the file stack when it has
         *        one; may be empty.
         * @return The relative file name, or empty when nothing is being processed.
         */
        [[nodiscard]] std::string GetCurrentFilename(const ContentIdentity& contentIdentity) const;

    private:
        std::string loggerRootDirectory_;
        std::vector<std::string> files_;
    };
}
