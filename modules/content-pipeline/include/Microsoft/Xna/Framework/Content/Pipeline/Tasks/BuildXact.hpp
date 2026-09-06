// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Tasks/ContentTask.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Tasks
{
    /**
     * @brief Compiles the XACT projects a content project references.
     *
     * XACT audio projects (`.xap`) are compiled by Microsoft's own `XactBld3.exe`, which ships
     * with the XNA Game Studio tools and is not something CNA can reimplement or vendor. Every
     * part of this task that does not need that tool is here and works: the properties, the
     * validation, the project plumbing, the incremental decision, and the outputs it fills.
     *
     * The tool itself is discovered rather than assumed: `SetXactCompilerEXT()` names it, and
     * failing that the `CNA_XACTBLD` environment variable does. Where neither finds one,
     * `Execute()` answers false with a message naming the tool, and that -- the final external
     * compilation, and nothing else about this task -- is the one thing in this plan marked
     * `EXTERNAL_BLOCKED`.
     */
    class BuildXact final : public ContentTask
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Tasks.BuildXact";

        /** @brief Initializes a task with every property empty. */
        BuildXact() = default;

        /**
         * @brief Gets the configuration the projects are being built in.
         *
         * @return The configuration name.
         */
        [[nodiscard]] const std::string& getBuildConfigurationProperty() const noexcept;

        /**
         * @brief Sets the configuration the projects are being built in.
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
         * @brief Gets where the build keeps its intermediate state.
         *
         * @return The intermediate directory.
         */
        [[nodiscard]] const std::string& getIntermediateDirectoryProperty() const noexcept;

        /**
         * @brief Sets where the build keeps its intermediate state.
         *
         * @param value The intermediate directory.
         */
        void setIntermediateDirectoryProperty(std::string value);

        /**
         * @brief Gets the intermediate files the last `Execute()` produced.
         *
         * @return The items; empty before a build.
         */
        [[nodiscard]] const std::vector<TaskItem>& getIntermediateFilesProperty() const noexcept;

        /**
         * @brief Gets the directory build messages are reported relative to.
         *
         * @return The logger root directory.
         */
        [[nodiscard]] const std::string& getLoggerRootDirectoryProperty() const noexcept;

        /**
         * @brief Sets the directory build messages are reported relative to.
         *
         * @param value The logger root directory.
         */
        void setLoggerRootDirectoryProperty(std::string value);

        /**
         * @brief Gets where compiled wave and sound banks are written.
         *
         * @return The output directory.
         */
        [[nodiscard]] const std::string& getOutputDirectoryProperty() const noexcept;

        /**
         * @brief Sets where compiled wave and sound banks are written.
         *
         * @param value The output directory.
         */
        void setOutputDirectoryProperty(std::string value);

        /**
         * @brief Gets every file the last `Execute()` left in the output directory.
         *
         * @return The items; empty before a build.
         */
        [[nodiscard]] const std::vector<TaskItem>& getOutputXactFilesProperty() const noexcept;

        /**
         * @brief Gets whether every project is rebuilt regardless of what is current.
         *
         * @return true to ignore the incremental state.
         */
        [[nodiscard]] bool getRebuildAllProperty() const noexcept;

        /**
         * @brief Sets whether every project is rebuilt regardless of what is current.
         *
         * @param value true to ignore the incremental state.
         */
        void setRebuildAllProperty(bool value) noexcept;

        /**
         * @brief Gets the files the last `Execute()` actually rebuilt.
         *
         * @return The items; empty before a build.
         */
        [[nodiscard]] const std::vector<TaskItem>& getRebuiltXactFilesProperty() const noexcept;

        /**
         * @brief Gets the directory the projects are named relative to.
         *
         * @return The root directory.
         */
        [[nodiscard]] const std::string& getRootDirectoryProperty() const noexcept;

        /**
         * @brief Sets the directory the projects are named relative to.
         *
         * @param value The root directory.
         */
        void setRootDirectoryProperty(std::string value);

        /**
         * @brief Gets the platform the banks are built for.
         *
         * @return The platform name.
         */
        [[nodiscard]] const std::string& getTargetPlatformProperty() const noexcept;

        /**
         * @brief Sets the platform the banks are built for.
         *
         * @param value The platform name.
         */
        void setTargetPlatformProperty(std::string value);

        /**
         * @brief Gets the profile the banks are built for.
         *
         * @return The profile name.
         */
        [[nodiscard]] const std::string& getTargetProfileProperty() const noexcept;

        /**
         * @brief Sets the profile the banks are built for.
         *
         * @param value The profile name.
         */
        void setTargetProfileProperty(std::string value);

        /**
         * @brief Gets the XACT projects to compile.
         *
         * @return The items.
         */
        [[nodiscard]] const std::vector<TaskItem>& getXactProjectsProperty() const noexcept;

        /**
         * @brief Sets the XACT projects to compile.
         *
         * @param value The items.
         */
        void setXactProjectsProperty(std::vector<TaskItem> value);

        /**
         * @brief Gets the framework version the banks target.
         *
         * @return The version, as the project writes it.
         */
        [[nodiscard]] const std::string& getXnaFrameworkVersionProperty() const noexcept;

        /**
         * @brief Sets the framework version the banks target.
         *
         * @param value The version.
         */
        void setXnaFrameworkVersionProperty(std::string value);

        /**
         * @brief Names the XACT compiler to invoke.
         *
         * Not an XNA property: XNA finds `XactBld3.exe` through its own installation, and there is
         * no such installation to find here.
         *
         * @param executable Path to the compiler, or empty to fall back to `CNA_XACTBLD`.
         * @param launcher Program to run it through (`wine`), or empty to run it directly.
         */
        CNAEXT void SetXactCompilerEXT(std::string executable, std::string launcher);

        /**
         * @brief Tells whether an XACT compiler can be found at all.
         *
         * @return true when one was named or discovered.
         */
        CNAEXT [[nodiscard]] bool HasXactCompilerEXT() const;

        /**
         * @brief Validates the projects and compiles each through the XACT compiler.
         *
         * Validation runs whether or not a compiler is present, so a project with a missing or
         * unreadable `.xap` is told so rather than being told the tool is missing.
         *
         * @return true when every project compiled; false when validation failed or no compiler
         *         could be found, with the reason in `ErrorsEXT()`.
         */
        [[nodiscard]] bool Execute() override;

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        std::string buildConfiguration_;
        std::string contentProjectGuid_;
        std::string intermediateDirectory_;
        std::vector<TaskItem> intermediateFiles_;
        std::string loggerRootDirectory_;
        std::string outputDirectory_;
        std::vector<TaskItem> outputXactFiles_;
        bool rebuildAll_ = false;
        std::vector<TaskItem> rebuiltXactFiles_;
        std::string rootDirectory_;
        std::string targetPlatform_;
        std::string targetProfile_;
        std::vector<TaskItem> xactProjects_;
        std::string xnaFrameworkVersion_;
        std::string xactExecutable_;
        std::string xactLauncher_;
    };
}
