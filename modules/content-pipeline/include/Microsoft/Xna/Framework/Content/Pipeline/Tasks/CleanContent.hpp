// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <string_view>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Tasks/ContentTask.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Tasks
{
    /**
     * @brief Removes what a previous `BuildContent` left behind.
     *
     * Only files a valid output manifest proves the pipeline owns are removed, which is the
     * canonical cleaner's own rule and the safe reading of what "clean the output directory"
     * should mean. A directory with no manifest is left alone rather than emptied.
     */
    class CleanContent final : public ContentTask
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Tasks.CleanContent";

        /** @brief Initializes a task with every property empty. */
        CleanContent() = default;

        /**
         * @brief Gets the configuration being cleaned.
         *
         * @return The configuration name.
         */
        [[nodiscard]] const std::string& getBuildConfigurationProperty() const noexcept;

        /**
         * @brief Sets the configuration being cleaned.
         *
         * @param value The configuration name.
         */
        void setBuildConfigurationProperty(std::string value);

        /**
         * @brief Gets the content project's identity.
         *
         * @return The project GUID.
         */
        [[nodiscard]] const std::string& getContentProjectGUIDProperty() const noexcept;

        /**
         * @brief Sets the content project's identity.
         *
         * @param value The project GUID.
         */
        void setContentProjectGUIDProperty(std::string value);

        /**
         * @brief Gets the intermediate directory to clean.
         *
         * @return The intermediate directory.
         */
        [[nodiscard]] const std::string& getIntermediateDirectoryProperty() const noexcept;

        /**
         * @brief Sets the intermediate directory to clean.
         *
         * @param value The intermediate directory.
         */
        void setIntermediateDirectoryProperty(std::string value);

        /**
         * @brief Gets the output directory to clean.
         *
         * @return The output directory.
         */
        [[nodiscard]] const std::string& getOutputDirectoryProperty() const noexcept;

        /**
         * @brief Sets the output directory to clean.
         *
         * @param value The output directory.
         */
        void setOutputDirectoryProperty(std::string value);

        /**
         * @brief Gets the directory source assets were named relative to.
         *
         * @return The root directory.
         */
        [[nodiscard]] const std::string& getRootDirectoryProperty() const noexcept;

        /**
         * @brief Sets the directory source assets were named relative to.
         *
         * @param value The root directory.
         */
        void setRootDirectoryProperty(std::string value);

        /**
         * @brief Gets the platform whose output is being cleaned.
         *
         * @return The platform name.
         */
        [[nodiscard]] const std::string& getTargetPlatformProperty() const noexcept;

        /**
         * @brief Sets the platform whose output is being cleaned.
         *
         * @param value The platform name.
         */
        void setTargetPlatformProperty(std::string value);

        /**
         * @brief Gets the profile whose output is being cleaned.
         *
         * @return The profile name.
         */
        [[nodiscard]] const std::string& getTargetProfileProperty() const noexcept;

        /**
         * @brief Sets the profile whose output is being cleaned.
         *
         * @param value The profile name.
         */
        void setTargetProfileProperty(std::string value);

        /**
         * @brief Removes the pipeline-owned files under the output directory.
         *
         * @return true when the clean completed; false when the output directory could not be
         *         inspected, with the reason in `ErrorsEXT()`.
         */
        [[nodiscard]] bool Execute() override;

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        std::string buildConfiguration_;
        std::string contentProjectGuid_;
        std::string intermediateDirectory_;
        std::string outputDirectory_;
        std::string rootDirectory_;
        std::string targetPlatform_;
        std::string targetProfile_;
    };

    /**
     * @brief Answers what the previous build left in the output directory.
     *
     * The task an incremental project uses to know what a clean would remove and what an installer
     * would deploy, without running a build. It reads the output manifest a previous
     * `BuildContent` wrote and nothing else, so it is cheap and never rebuilds anything.
     */
    class GetLastOutputs final : public ContentTask
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Tasks.GetLastOutputs";

        /** @brief Initializes a task with every property empty. */
        GetLastOutputs() = default;

        /**
         * @brief Gets the content project's identity.
         *
         * @return The project GUID.
         */
        [[nodiscard]] const std::string& getContentProjectGUIDProperty() const noexcept;

        /**
         * @brief Sets the content project's identity.
         *
         * @param value The project GUID.
         */
        void setContentProjectGUIDProperty(std::string value);

        /**
         * @brief Gets the directory holding the previous build's state.
         *
         * @return The intermediate directory.
         */
        [[nodiscard]] const std::string& getIntermediateDirectoryProperty() const noexcept;

        /**
         * @brief Sets the directory holding the previous build's state.
         *
         * XNA keeps its incremental state under the intermediate directory; CNA's manifest lives
         * beside the compiled output, so this is also read as the place to look when nothing else
         * names one.
         *
         * @param value The intermediate directory.
         */
        void setIntermediateDirectoryProperty(std::string value);

        /**
         * @brief Gets what the previous build left behind.
         *
         * @return The items; empty when there was no previous build.
         */
        [[nodiscard]] const std::vector<TaskItem>& getOutputContentFilesProperty() const noexcept;

        /**
         * @brief Sets where to look for the previous build's manifest.
         *
         * Not an XNA property: XNA finds the state under `IntermediateDirectory`, and CNA's
         * manifest lives beside the compiled output, so the caller can name that directly.
         *
         * @param value The output directory.
         */
        CNAEXT void setOutputDirectoryEXT(std::string value);

        /**
         * @brief Reads the previous build's manifest into `OutputContentFiles`.
         *
         * @return true whether or not a manifest was found -- no previous build is not a failure,
         *         and answers an empty list, which is what a project asking "what is there?"
         *         needs.
         */
        [[nodiscard]] bool Execute() override;

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        std::string contentProjectGuid_;
        std::string intermediateDirectory_;
        std::string outputDirectory_;
        std::vector<TaskItem> outputContentFiles_;
    };
}
