// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Tasks/ContentTask.hpp"

#include <algorithm>
#include <cctype>

#include "Microsoft/Xna/Framework/Content/Pipeline/ContentIdentity.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Tasks
{
    namespace
    {
        /** @brief MSBuild metadata names are case-insensitive; this is the key they share. */
        [[nodiscard]] std::string Fold(const std::string& name)
        {
            std::string folded(name);
            std::transform(folded.begin(), folded.end(), folded.begin(), [](const unsigned char c)
                           { return static_cast<char>(std::tolower(c)); });
            return folded;
        }
    }

    TaskItem::TaskItem(std::string itemSpec) : itemSpec_(std::move(itemSpec)) {}

    const std::string& TaskItem::getItemSpecProperty() const noexcept { return itemSpec_; }

    void TaskItem::setItemSpecProperty(std::string value) { itemSpec_ = std::move(value); }

    std::string TaskItem::GetMetadata(const std::string& name) const
    {
        const auto found = metadata_.find(Fold(name));
        return found == metadata_.end() ? std::string() : found->second;
    }

    void TaskItem::SetMetadata(const std::string& name, std::string value)
    {
        metadata_[Fold(name)] = std::move(value);
    }

    bool TaskItem::HasMetadata(const std::string& name) const
    {
        return metadata_.find(Fold(name)) != metadata_.end();
    }

    std::vector<std::string> TaskItem::MetadataNames() const
    {
        std::vector<std::string> names;
        names.reserve(metadata_.size());
        for (const auto& [name, value] : metadata_)
        {
            (void)value;
            names.push_back(name);
        }
        return names;
    }

    const std::string& TaskItem::GetTypeName() const
    {
        static const std::string name("Microsoft.Xna.Framework.Content.Pipeline.Tasks.TaskItem");
        return name;
    }

    void ContentTask::SetLoggerEXT(ContentBuildLogger* const logger) noexcept { logger_ = logger; }

    const std::vector<std::string>& ContentTask::MessagesEXT() const noexcept { return messages_; }

    const std::vector<std::string>& ContentTask::ErrorsEXT() const noexcept { return errors_; }

    void ContentTask::LogMessage(const std::string& message)
    {
        messages_.push_back(message);
        if (logger_ != nullptr)
        {
            logger_->LogMessage(message);
        }
    }

    void ContentTask::LogError(const std::string& message)
    {
        errors_.push_back(message);
        if (logger_ != nullptr)
        {
            // A content build logger has no error level of its own; an error is an important
            // message the task also keeps, and what makes Execute answer false.
            logger_->LogImportantMessage(message);
        }
    }
}
