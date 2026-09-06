// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Tasks/BuildContent.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Tasks
{
    /**
     * @brief An XNA Game Studio 4.0 content project (`.contentproj`), read for what it builds.
     *
     * A `.contentproj` is an MSBuild project, and CNA does not host MSBuild. What it *is*, for the
     * purpose of building content, is a list of source assets with their importer, processor and
     * parameters, plus a handful of project-level properties -- which is exactly a `BuildContent`
     * task's inputs. So this reads one into a task, and the task runs the canonical coordinator;
     * there is no second build engine and no second project format. CNA's own
     * `.cna-content.json` remains its native project file and is unaffected.
     *
     * What is read, from the schema the shipped `.targets` file and the public samples establish:
     *
     * | Item or property | Meaning |
     * |---|---|
     * | `<Compile Include="…">` | a source asset to build |
     * | `<Name>` | the logical asset name |
     * | `<Importer>`, `<Processor>` | the XNA component names, translated by `XnaComponentNames` |
     * | `<ProcessorParameters_X>` | one processor parameter, `X` being its property name |
     * | `<Link>` | where the asset belongs when the file lives outside the project |
     * | `<Content>`, `<None>` | copied rather than built; `<CopyToOutputDirectory>` says whether |
     * | `<ContentRootDirectory>` | the directory the output is written under |
     * | `<XnaPlatform>` | `Windows`, `Xbox 360` or `Windows Phone`, normalized |
     * | `<XnaProfile>` | `Reach` or `HiDef` |
     * | `<XnaCompressContent>` | whether the compiled assets are compressed |
     * | `<XnaFrameworkVersion>`, `<ProjectGuid>` | carried through to the task |
     * | `<Reference>`, `<ProjectReference>` | recorded, and refused at build time when they name a pipeline assembly |
     *
     * An `<ItemGroup>` or `<PropertyGroup>` carrying a `Condition` is read only when the condition
     * is one this reader can decide; anything else is refused by name rather than guessed at,
     * because silently dropping a conditional item group would silently drop assets.
     */
    class ContentProject
    {
    public:
        /** @brief One thing the project says to build or to copy. */
        struct Item
        {
            /** @brief The path, as the project wrote it, relative to the project's directory. */
            std::string include;
            /** @brief The item's element name: `Compile`, `Content`, `None` or `Reference`. */
            std::string kind;
            /** @brief The metadata elements inside it, by name. */
            std::vector<std::pair<std::string, std::string>> metadata;

            /**
             * @brief One metadata value, or an empty string.
             *
             * @param name The metadata name; matching is case-insensitive, as MSBuild's is.
             * @return The value.
             */
            CNAEXT [[nodiscard]] std::string Get(const std::string& name) const;
        };

        /** @brief Reads a project file.
         *
         * @param filename Path to the `.contentproj`.
         * @return The project.
         * @throws InvalidContentException when the file is not there, is not XML, is not an
         *         MSBuild project, or carries a construct this reader will not guess at.
         */
        CNAEXT [[nodiscard]] static ContentProject Load(const std::string& filename);

        /**
         * @brief Reads a project from text already in memory.
         *
         * @param text The project's XML.
         * @param origin A name for diagnostics, typically the file it came from.
         * @return The project.
         * @throws InvalidContentException as `Load` does.
         */
        CNAEXT [[nodiscard]] static ContentProject Parse(const std::string& text,
                                                         const std::string& origin);

        /** @brief The directory the project file lives in, which its items are relative to. */
        CNAEXT [[nodiscard]] const std::string& DirectoryEXT() const noexcept;

        /** @brief One project-level property, or an empty string. */
        CNAEXT [[nodiscard]] std::string Property(const std::string& name) const;

        /** @brief Every item the project declares, in document order. */
        CNAEXT [[nodiscard]] const std::vector<Item>& Items() const noexcept;

        /** @brief The items that are built: the `Compile` ones. */
        CNAEXT [[nodiscard]] std::vector<Item> SourceAssets() const;

        /** @brief The items that are copied rather than built: `Content` and `None`. */
        CNAEXT [[nodiscard]] std::vector<Item> CopiedFiles() const;

        /**
         * @brief Fills a `BuildContent` task with everything this project says.
         *
         * The root directory is the project's own directory; the output and intermediate
         * directories are the caller's, because a project file does not say where a particular
         * build should put its results.
         *
         * @param outputDirectory Where compiled assets are to be written.
         * @param intermediateDirectory Where the build may keep its state.
         * @return A task ready to `Execute()`.
         */
        CNAEXT [[nodiscard]] BuildContent ToBuildContentEXT(const std::string& outputDirectory,
                                                            const std::string& intermediateDirectory) const;

        /**
         * @brief Every reason this project cannot be built as it stands, in the order found.
         *
         * Naming them all at once beats failing on the first: a project that uses three custom
         * processors should say so once rather than three times.
         *
         * @return The reasons; empty when everything routes.
         */
        CNAEXT [[nodiscard]] std::vector<std::string> UnroutableEXT() const;

    private:
        std::string directory_;
        std::vector<std::pair<std::string, std::string>> properties_;
        std::vector<Item> items_;
    };
}
