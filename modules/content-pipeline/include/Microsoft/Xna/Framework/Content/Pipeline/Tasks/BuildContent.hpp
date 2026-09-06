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
     * @brief Builds a content project's source assets into compiled assets.
     *
     * The task XNA's `.targets` file drives to build a `.contentproj`. Every input property is a
     * value MSBuild sets from the project, `Execute()` runs the build, and the three item-valued
     * output properties are what the project reads back afterwards.
     *
     * What is reproduced is the developer-visible contract: the properties, their meanings, the
     * `bool Execute()` answer, and the outputs it fills. What is replaced is the hosting -- there
     * is no MSBuild engine, so nothing sets these properties for you and nothing calls `Execute()`
     * for you. The build itself is the one canonical coordinator (`cna-content`'s own
     * `RunContentCompiler`), so this is a translation from MSBuild's item model to that
     * coordinator's, not a second build engine.
     *
     * Source-asset metadata this reads, which is what a `.contentproj` writes:
     * `Name` (the logical asset name), `Importer`, `Processor`, `ProcessorParameters` (or a
     * `ProcessorParameters_<Name>` metadata per parameter, as XNA's project system writes them)
     * and `Link`.
     */
    class BuildContent final : public ContentTask
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Tasks.BuildContent";

        /**
         * @brief The name of the event a host signals to cancel a running build.
         *
         * XNA's own value, verbatim: a `string.Format` template whose one argument is the content
         * project's GUID. There is no Windows named event here, and nothing waits on one; the
         * constant is the observable part and is what a host that used it would format.
         */
        static const std::string CancelEventNameFormat;

        /** @brief Initializes a task with every property empty. */
        BuildContent() = default;

        /**
         * @brief Gets the configuration the project is being built in.
         *
         * @return The configuration name, typically `Debug` or `Release`.
         */
        [[nodiscard]] const std::string& getBuildConfigurationProperty() const noexcept;

        /**
         * @brief Sets the configuration the project is being built in.
         *
         * @param value The configuration name.
         */
        void setBuildConfigurationProperty(std::string value);

        /**
         * @brief Gets whether compiled assets are compressed.
         *
         * @return true when the build compresses its output.
         */
        [[nodiscard]] bool getCompressContentProperty() const noexcept;

        /**
         * @brief Sets whether compiled assets are compressed.
         *
         * @param value true to compress.
         */
        void setCompressContentProperty(bool value) noexcept;

        /**
         * @brief Gets the content project's identity.
         *
         * @return The project GUID, as the project file writes it.
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
         * @brief Gets every compiled asset the last `Execute()` left in the output directory.
         *
         * @return The items; empty before a build.
         */
        [[nodiscard]] const std::vector<TaskItem>& getOutputContentFilesProperty() const noexcept;

        /**
         * @brief Gets where compiled assets are written.
         *
         * @return The output directory.
         */
        [[nodiscard]] const std::string& getOutputDirectoryProperty() const noexcept;

        /**
         * @brief Sets where compiled assets are written.
         *
         * @param value The output directory.
         */
        void setOutputDirectoryProperty(std::string value);

        /**
         * @brief Gets the assemblies carrying custom pipeline components.
         *
         * @return The items.
         */
        [[nodiscard]] const std::vector<TaskItem>& getPipelineAssembliesProperty() const noexcept;

        /**
         * @brief Sets the assemblies carrying custom pipeline components.
         *
         * C++ has no assembly loading, so a non-empty list is refused by `Execute()` with a
         * message saying where a custom component is registered instead. Accepting it silently
         * would let a project believe its own importers were running.
         *
         * @param value The items.
         */
        void setPipelineAssembliesProperty(std::vector<TaskItem> value);

        /**
         * @brief Gets what the pipeline assemblies themselves depend on.
         *
         * @return The items.
         */
        [[nodiscard]] const std::vector<TaskItem>& getPipelineAssemblyDependenciesProperty() const noexcept;

        /**
         * @brief Sets what the pipeline assemblies themselves depend on.
         *
         * @param value The items.
         */
        void setPipelineAssemblyDependenciesProperty(std::vector<TaskItem> value);

        /**
         * @brief Gets whether every asset is rebuilt regardless of what is current.
         *
         * @return true to ignore the incremental state.
         */
        [[nodiscard]] bool getRebuildAllProperty() const noexcept;

        /**
         * @brief Sets whether every asset is rebuilt regardless of what is current.
         *
         * @param value true to ignore the incremental state.
         */
        void setRebuildAllProperty(bool value) noexcept;

        /**
         * @brief Gets the compiled assets the last `Execute()` actually rebuilt.
         *
         * A subset of `OutputContentFiles`: the ones that were not already current.
         *
         * @return The items; empty before a build.
         */
        [[nodiscard]] const std::vector<TaskItem>& getRebuiltContentFilesProperty() const noexcept;

        /**
         * @brief Gets the directory source assets are named relative to.
         *
         * @return The root directory.
         */
        [[nodiscard]] const std::string& getRootDirectoryProperty() const noexcept;

        /**
         * @brief Sets the directory source assets are named relative to.
         *
         * @param value The root directory.
         */
        void setRootDirectoryProperty(std::string value);

        /**
         * @brief Gets the source assets to build.
         *
         * @return The items and their metadata.
         */
        [[nodiscard]] const std::vector<TaskItem>& getSourceAssetsProperty() const noexcept;

        /**
         * @brief Sets the source assets to build.
         *
         * @param value The items and their metadata.
         */
        void setSourceAssetsProperty(std::vector<TaskItem> value);

        /**
         * @brief Gets the platform the assets are built for.
         *
         * @return `Windows`, `Xbox360` or `WindowsPhone`.
         */
        [[nodiscard]] const std::string& getTargetPlatformProperty() const noexcept;

        /**
         * @brief Sets the platform the assets are built for.
         *
         * @param value The platform name.
         */
        void setTargetPlatformProperty(std::string value);

        /**
         * @brief Gets the graphics profile the assets are built for.
         *
         * @return `Reach` or `HiDef`.
         */
        [[nodiscard]] const std::string& getTargetProfileProperty() const noexcept;

        /**
         * @brief Sets the graphics profile the assets are built for.
         *
         * @param value The profile name.
         */
        void setTargetProfileProperty(std::string value);

        /**
         * @brief Builds every source asset, filling the three output properties.
         *
         * @return true when every asset built; false when one failed, with the reason in
         *         `ErrorsEXT()`.
         */
        [[nodiscard]] bool Execute() override;

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        std::string buildConfiguration_;
        bool compressContent_ = false;
        std::string contentProjectGuid_;
        std::string intermediateDirectory_;
        std::vector<TaskItem> intermediateFiles_;
        std::string loggerRootDirectory_;
        std::vector<TaskItem> outputContentFiles_;
        std::string outputDirectory_;
        std::vector<TaskItem> pipelineAssemblies_;
        std::vector<TaskItem> pipelineAssemblyDependencies_;
        bool rebuildAll_ = false;
        std::vector<TaskItem> rebuiltContentFiles_;
        std::string rootDirectory_;
        std::vector<TaskItem> sourceAssets_;
        std::string targetPlatform_;
        std::string targetProfile_;
    };
}
