// SPDX-License-Identifier: MS-PL
#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentBuildLogger.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Tasks
{
    /**
     * @brief One item a task takes in or answers back: a path and the metadata attached to it.
     *
     * XNA's four tasks are MSBuild tasks, and their item-valued properties are
     * `Microsoft.Build.Framework.ITaskItem[]`. `ITaskItem`'s developer-visible contract is exactly
     * this -- an `ItemSpec` and a string-to-string metadata table -- so that is what it becomes
     * here. What does not come across is MSBuild's own hosting: item creation, batching, property
     * functions and the engine that calls `Execute()`. `CNA::Content::Pipeline`'s own build
     * configuration and the `cna-content` command line take that role, which is why the four
     * tasks are `HOST_SUBSTITUTION` in the parity map and not `EXACT_EQUIVALENT`.
     *
     * The metadata names are MSBuild's own and are the ones a `.contentproj` writes: `Importer`,
     * `Processor`, `Name`, `Link` and `ProcessorParameters` (see
     * `plans/plan_xnapipeline_parity.md` §18).
     */
    class TaskItem final : public System::Object
    {
    public:
        /** @brief Creates an empty item. */
        CNAEXT TaskItem() = default;

        /**
         * @brief Creates an item naming a path.
         *
         * @param itemSpec The path, as the project wrote it.
         */
        CNAEXT explicit TaskItem(std::string itemSpec);

        /**
         * @brief Gets the path this item names.
         *
         * @return The item specification.
         */
        CNAEXT [[nodiscard]] const std::string& getItemSpecProperty() const noexcept;

        /**
         * @brief Sets the path this item names.
         *
         * @param value The item specification.
         */
        CNAEXT void setItemSpecProperty(std::string value);

        /**
         * @brief Gets one metadata value.
         *
         * @param name The metadata name; MSBuild's own names are case-insensitive, and so are
         *        these.
         * @return The value, or an empty string when the item does not carry that metadata --
         *        which is what MSBuild answers too.
         */
        CNAEXT [[nodiscard]] std::string GetMetadata(const std::string& name) const;

        /**
         * @brief Sets one metadata value.
         *
         * @param name The metadata name.
         * @param value The value to record.
         */
        CNAEXT void SetMetadata(const std::string& name, std::string value);

        /**
         * @brief Tells whether the item carries a metadata name at all.
         *
         * @param name The metadata name.
         * @return true when the name is present, even with an empty value.
         */
        CNAEXT [[nodiscard]] bool HasMetadata(const std::string& name) const;

        /**
         * @brief Every metadata name this item carries, in name order.
         *
         * @return The names.
         */
        CNAEXT [[nodiscard]] std::vector<std::string> MetadataNames() const;

        /** @brief Compares the path and every metadata entry. */
        CNAEXT [[nodiscard]] bool operator==(const TaskItem& other) const = default;

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        std::string itemSpec_;
        // Case-insensitive, as MSBuild metadata is; the comparator lowers both sides.
        std::map<std::string, std::string> metadata_;
    };

    /**
     * @brief What every content task shares: a log, and the answer `Execute()` gives.
     *
     * MSBuild's `Microsoft.Build.Utilities.Task` is the base of all four, and what a task author
     * uses from it is the logging helper and the `bool Execute()` contract. Both are here; the
     * engine that discovers and drives tasks is not, and is not meant to be.
     */
    class ContentTask : public System::Object
    {
    public:
        /** @brief Initializes a task with no log sink. */
        CNAEXT ContentTask() = default;

        /** @brief Releases the task. */
        CNAEXT ~ContentTask() override = default;

        /**
         * @brief Runs the task.
         *
         * @return true when the task succeeded; false when it failed, having logged why. A task
         *         does not throw for a build failure, as MSBuild's do not.
         */
        CNAEXT [[nodiscard]] virtual bool Execute() = 0;

        /**
         * @brief Sets where the task writes its messages.
         *
         * MSBuild gives a task its logger through the engine; there is no engine here, so the
         * caller supplies one. A null sink discards the messages.
         *
         * @param logger The sink, which must outlive the task, or null.
         */
        CNAEXT void SetLoggerEXT(ContentBuildLogger* logger) noexcept;

        /**
         * @brief The messages the task recorded, newest last.
         *
         * @return The lines, whatever the sink did with them.
         */
        CNAEXT [[nodiscard]] const std::vector<std::string>& MessagesEXT() const noexcept;

        /**
         * @brief The errors the task recorded, newest last.
         *
         * A task that answers false has at least one.
         *
         * @return The lines.
         */
        CNAEXT [[nodiscard]] const std::vector<std::string>& ErrorsEXT() const noexcept;

    protected:
        /**
         * @brief Records a message.
         *
         * @param message The text.
         */
        void LogMessage(const std::string& message);

        /**
         * @brief Records an error, which is what makes a task answer false.
         *
         * @param message The text.
         */
        void LogError(const std::string& message);

    private:
        ContentBuildLogger* logger_ = nullptr;
        std::vector<std::string> messages_;
        std::vector<std::string> errors_;
    };
}
