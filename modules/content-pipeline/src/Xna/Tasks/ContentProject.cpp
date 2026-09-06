// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Tasks/ContentProject.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "CNA/Internal/Xml.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Tasks/XnaComponentNames.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Tasks
{
    namespace
    {
        /** @brief MSBuild element and metadata names are case-insensitive; this is the key. */
        [[nodiscard]] std::string Fold(const std::string& name)
        {
            std::string folded(name);
            std::transform(folded.begin(), folded.end(), folded.begin(), [](const unsigned char c)
                           { return static_cast<char>(std::tolower(c)); });
            return folded;
        }

        /** @brief The element's name without the XML namespace prefix a project may carry. */
        [[nodiscard]] std::string Local(const std::string& name)
        {
            const std::size_t colon = name.rfind(':');
            return colon == std::string::npos ? name : name.substr(colon + 1u);
        }

        [[nodiscard]] std::string Trim(const std::string& text)
        {
            const std::size_t first = text.find_first_not_of(" \t\r\n");
            if (first == std::string::npos)
            {
                return {};
            }
            const std::size_t last = text.find_last_not_of(" \t\r\n");
            return text.substr(first, last - first + 1u);
        }

        /** @brief The one attribute an item may carry. */
        [[nodiscard]] std::string Attribute(const CNA::Internal::XmlElement& element,
                                            const std::string& name)
        {
            for (const auto& [key, value] : element.attributes)
            {
                if (Fold(Local(key)) == Fold(name))
                {
                    return value;
                }
            }
            return {};
        }

        /**
         * @brief `$(Name)` replaced by what the properties read so far say, as MSBuild does.
         *
         * @param text The condition or value to expand.
         * @param properties The properties defined up to this point, in document order.
         * @return The expansion; an unset property expands to nothing, which is what MSBuild does
         *         and what makes `'$(Configuration)' == ''` the default-value guard it is.
         */
        [[nodiscard]] std::string Expand(const std::string& text,
                                         const std::vector<std::pair<std::string, std::string>>& properties)
        {
            std::string out;
            for (std::size_t i = 0; i < text.size();)
            {
                if (text[i] == '$' && i + 1u < text.size() && text[i + 1u] == '(')
                {
                    const std::size_t close = text.find(')', i + 2u);
                    if (close == std::string::npos)
                    {
                        out += text[i++];
                        continue;
                    }
                    const std::string name = text.substr(i + 2u, close - i - 2u);
                    for (auto it = properties.rbegin(); it != properties.rend(); ++it)
                    {
                        if (Fold(it->first) == Fold(name))
                        {
                            out += it->second;
                            break;
                        }
                    }
                    i = close + 1u;
                    continue;
                }
                out += text[i++];
            }
            return out;
        }

        /**
         * @brief Decides a `Condition`, or says it cannot.
         *
         * MSBuild conditions are a small expression language and guessing at one would silently
         * add or drop assets. Every condition the 170 public sample projects on this machine use
         * is one comparison of two quoted, property-expanded strings -- `'$(Configuration)' == ''`
         * and `'$(Configuration)|$(Platform)' == 'Debug|x86'` are the whole vocabulary -- so that
         * is what is decided, in document order so a default-value guard means what it says.
         * Anything else is refused by name.
         *
         * @param condition The condition text; empty is true.
         * @param properties The properties defined so far.
         * @param take Set to whether the guarded element applies.
         * @return true when the condition could be decided at all.
         */
        [[nodiscard]] bool ConditionIsDecidable(
            const std::string& condition,
            const std::vector<std::pair<std::string, std::string>>& properties, bool& take)
        {
            const std::string trimmed = Trim(condition);
            if (trimmed.empty())
            {
                take = true;
                return true;
            }
            const std::size_t equals = trimmed.find("==");
            const std::size_t notEquals = trimmed.find("!=");
            const bool negated = notEquals != std::string::npos &&
                                 (equals == std::string::npos || notEquals < equals);
            const std::size_t at = negated ? notEquals : equals;
            if (at == std::string::npos)
            {
                return false;
            }
            const std::string left = Trim(trimmed.substr(0, at));
            const std::string right = Trim(trimmed.substr(at + 2u));
            // Both sides must be quoted: an unquoted operand is a function call, a numeric
            // comparison or a boolean, none of which this reader decides.
            const auto unquote = [](const std::string& text, std::string& out)
            {
                if (text.size() < 2u || text.front() != '\'' || text.back() != '\'')
                {
                    return false;
                }
                out = text.substr(1u, text.size() - 2u);
                return out.find('\'') == std::string::npos;
            };
            std::string leftText;
            std::string rightText;
            if (!unquote(left, leftText) || !unquote(right, rightText))
            {
                return false;
            }
            const std::string a = Expand(leftText, properties);
            const std::string b = Expand(rightText, properties);
            // MSBuild compares strings case-insensitively.
            take = negated ? Fold(a) != Fold(b) : Fold(a) == Fold(b);
            return true;
        }
    }

    std::string ContentProject::Item::Get(const std::string& name) const
    {
        for (const auto& [key, value] : metadata)
        {
            if (Fold(key) == Fold(name))
            {
                return value;
            }
        }
        return {};
    }

    ContentProject ContentProject::Load(const std::string& filename)
    {
        std::error_code error;
        if (!std::filesystem::exists(filename, error) || error)
        {
            throw InvalidContentException("The content project \"" + filename + "\" does not exist.");
        }
        std::ifstream file(filename, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        ContentProject project = Parse(text, filename);
        project.directory_ = std::filesystem::path(filename).parent_path().string();
        return project;
    }

    ContentProject ContentProject::Parse(const std::string& text, const std::string& origin)
    {
        CNA::Internal::XmlElement root;
        try
        {
            root = CNA::Internal::ParseXml(text, origin);
        }
        catch (const std::exception& failure)
        {
            throw InvalidContentException("The content project \"" + origin +
                                          "\" is not readable XML: " + failure.what());
        }
        if (Fold(Local(root.name)) != "project")
        {
            throw InvalidContentException("The content project \"" + origin +
                                          "\" does not have an MSBuild <Project> root; its root is <" +
                                          root.name + ">.");
        }
        ContentProject project;
        for (const CNA::Internal::XmlElement& group : root.children)
        {
            const std::string groupName = Fold(Local(group.name));
            if (groupName != "propertygroup" && groupName != "itemgroup")
            {
                // Import, Target, Choose and the rest are MSBuild's own machinery; a content
                // project's assets never live in them, and reading them would mean hosting
                // MSBuild.
                continue;
            }
            bool take = true;
            if (!ConditionIsDecidable(Attribute(group, "Condition"), project.properties_, take))
            {
                throw InvalidContentException(
                    "The content project \"" + origin + "\" guards a <" + Local(group.name) +
                    "> with the condition \"" + Trim(Attribute(group, "Condition")) +
                    "\", which this reader will not guess at. Remove the condition, or build the "
                    "project through MSBuild.");
            }
            if (!take)
            {
                continue;
            }
            for (const CNA::Internal::XmlElement& child : group.children)
            {
                bool takeChild = true;
                if (!ConditionIsDecidable(Attribute(child, "Condition"), project.properties_, takeChild))
                {
                    throw InvalidContentException(
                        "The content project \"" + origin + "\" guards <" + Local(child.name) +
                        "> with the condition \"" + Trim(Attribute(child, "Condition")) +
                        "\", which this reader will not guess at.");
                }
                if (!takeChild)
                {
                    continue;
                }
                if (groupName == "propertygroup")
                {
                    project.properties_.emplace_back(Local(child.name), Trim(child.text));
                    continue;
                }
                Item item;
                item.kind = Local(child.name);
                item.include = Trim(Attribute(child, "Include"));
                for (const CNA::Internal::XmlElement& metadata : child.children)
                {
                    item.metadata.emplace_back(Local(metadata.name), Trim(metadata.text));
                }
                project.items_.push_back(std::move(item));
            }
        }
        return project;
    }

    const std::string& ContentProject::DirectoryEXT() const noexcept { return directory_; }

    std::string ContentProject::Property(const std::string& name) const
    {
        // The last one wins, which is MSBuild's own rule for a property set more than once.
        std::string found;
        for (const auto& [key, value] : properties_)
        {
            if (Fold(key) == Fold(name) && !value.empty())
            {
                found = value;
            }
        }
        return found;
    }

    const std::vector<ContentProject::Item>& ContentProject::Items() const noexcept { return items_; }

    std::vector<ContentProject::Item> ContentProject::SourceAssets() const
    {
        std::vector<Item> assets;
        for (const Item& item : items_)
        {
            if (Fold(item.kind) == "compile")
            {
                assets.push_back(item);
            }
        }
        return assets;
    }

    std::vector<ContentProject::Item> ContentProject::CopiedFiles() const
    {
        std::vector<Item> copied;
        for (const Item& item : items_)
        {
            const std::string kind = Fold(item.kind);
            if (kind == "content" || kind == "none")
            {
                copied.push_back(item);
            }
        }
        return copied;
    }

    std::vector<std::string> ContentProject::UnroutableEXT() const
    {
        std::vector<std::string> reasons;
        for (const Item& asset : SourceAssets())
        {
            const std::string importer = asset.Get("Importer");
            if (!importer.empty() && MapXnaImporterName(importer).canonicalName.empty() &&
                Fold(importer) != "xmlimporter")
            {
                reasons.push_back(asset.include + ": " + MapXnaImporterName(importer).reason);
            }
            const std::string processor = asset.Get("Processor");
            if (!processor.empty() && MapXnaProcessorName(processor).canonicalName.empty() &&
                Fold(processor) != "passthroughprocessor")
            {
                reasons.push_back(asset.include + ": " + MapXnaProcessorName(processor).reason);
            }
        }
        return reasons;
    }

    BuildContent ContentProject::ToBuildContentEXT(const std::string& outputDirectory,
                                                   const std::string& intermediateDirectory) const
    {
        BuildContent task;
        task.setRootDirectoryProperty(directory_.empty() ? std::string(".") : directory_);
        task.setOutputDirectoryProperty(outputDirectory);
        task.setIntermediateDirectoryProperty(intermediateDirectory);
        task.setContentProjectGUIDProperty(Property("ProjectGuid"));
        const std::string configuration = Property("Configuration");
        task.setBuildConfigurationProperty(configuration.empty() ? "Release" : configuration);
        // `Xbox 360` and `Windows Phone` are how a project spells them and `Xbox360` and
        // `WindowsPhone` are how the pipeline does; the space is the whole difference.
        std::string platform = Property("XnaPlatform");
        platform.erase(std::remove(platform.begin(), platform.end(), ' '), platform.end());
        task.setTargetPlatformProperty(platform.empty() ? "Windows" : platform);
        const std::string profile = Property("XnaProfile");
        task.setTargetProfileProperty(profile.empty() ? "HiDef" : profile);
        task.setCompressContentProperty(Fold(Property("XnaCompressContent")) == "true");

        std::vector<TaskItem> assets;
        for (const Item& asset : SourceAssets())
        {
            TaskItem item(asset.include);
            for (const auto& [key, value] : asset.metadata)
            {
                item.SetMetadata(key, value);
            }
            assets.push_back(std::move(item));
        }
        task.setSourceAssetsProperty(std::move(assets));

        // A project that references a pipeline assembly is refused by the task rather than built
        // without the importers it expects; the reference is passed on so the refusal names it.
        std::vector<TaskItem> pipelineAssemblies;
        for (const Item& item : items_)
        {
            const std::string kind = Fold(item.kind);
            if (kind != "reference" && kind != "projectreference")
            {
                continue;
            }
            // Microsoft's own pipeline assemblies are the framework, not a custom component; a
            // project naming those is an ordinary project.
            if (item.include.rfind("Microsoft.Xna.Framework", 0) == 0)
            {
                continue;
            }
            pipelineAssemblies.emplace_back(item.include);
        }
        task.setPipelineAssembliesProperty(std::move(pipelineAssemblies));
        return task;
    }
}
