// SPDX-License-Identifier: MS-PL

#include "CNA/Content/Pipeline/ContentBuildConfiguration.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <initializer_list>
#include <set>
#include <stdexcept>
#include <string_view>

#include "CNA/Content/Cnb/CnbFormat.hpp"
#include "CNA/Internal/ContentPath.hpp"
#include "CNA/Internal/Json.hpp"

namespace CNA::Content::Pipeline
{
    namespace
    {
        using CNA::Internal::JsonType;
        using CNA::Internal::JsonValue;

        constexpr const char* kConfigurationKind = "CNA.ContentPipeline.Config";

        std::string DisplayPath(const std::filesystem::path& path)
        {
            return path.empty() ? std::string("<memory>")
                                : CNA::Internal::ContentPathToUtf8(path);
        }

        [[noreturn]] void Fail(const std::filesystem::path& path, std::string_view context,
                               const std::string& reason)
        {
            throw std::runtime_error("content configuration '" + DisplayPath(path) + "' " +
                                     std::string(context) + ": " + reason);
        }

        void RequireKnownUniqueMembers(const JsonValue& object,
                                       std::initializer_list<std::string_view> allowed,
                                       const std::filesystem::path& path,
                                       std::string_view context)
        {
            if (object.type != JsonType::Object)
            {
                Fail(path, context, "must be a JSON object.");
            }
            std::set<std::string> seen;
            for (const auto& [name, value] : object.objectValue)
            {
                static_cast<void>(value);
                if (!seen.insert(name).second)
                {
                    Fail(path, context, "repeats field '" + name + "'.");
                }
                if (std::find(allowed.begin(), allowed.end(), name) == allowed.end())
                {
                    Fail(path, context, "contains unknown field '" + name + "'.");
                }
            }
        }

        const JsonValue& RequireMember(const JsonValue& object, const char* name, JsonType type,
                                       const std::filesystem::path& path,
                                       std::string_view context)
        {
            const JsonValue* value = object.FindMember(name);
            if (value == nullptr)
            {
                Fail(path, context, "is missing field '" + std::string(name) + "'.");
            }
            if (value->type != type)
            {
                Fail(path, context, "field '" + std::string(name) + "' has the wrong JSON type.");
            }
            return *value;
        }

        std::string OptionalNonEmptyString(const JsonValue& object, const char* name,
                                           const std::filesystem::path& path,
                                           std::string_view context)
        {
            const JsonValue* value = object.FindMember(name);
            if (value == nullptr) { return {}; }
            if (value->type != JsonType::String || value->stringValue.empty())
            {
                Fail(path, context, "field '" + std::string(name) +
                                        "' must be a non-empty string when present.");
            }
            return value->stringValue;
        }

        void RequireSafeSource(const std::string& source, const std::filesystem::path& path)
        {
            const std::filesystem::path native = CNA::Internal::ContentPathFromUtf8(source);
            if (source.empty() || source.find('\\') != std::string::npos ||
                native.is_absolute() || native.has_root_name() ||
                native.has_root_directory())
            {
                Fail(path, "asset entry '" + source + "'",
                     "source key must be a non-empty relative path using '/'.");
            }
            for (const std::filesystem::path& part : native)
            {
                if (part == "..")
                {
                    Fail(path, "asset entry '" + source + "'",
                         "source key must not contain '..'.");
                }
            }
            if (CNA::Internal::ContentPathToUtf8(native.lexically_normal()) != source)
            {
                Fail(path, "asset entry '" + source + "'",
                     "source key must use normalized generic UTF-8 path spelling.");
            }
        }

        template<typename T>
        T ParseInteger(const std::string& text, const std::filesystem::path& path,
                       std::string_view context, const std::string& name)
        {
            T result{};
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
            if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
            {
                Fail(path, context, "parameter '" + name + "' has an invalid integer value.");
            }
            return result;
        }

        ContentProcessorParameterValue ParseParameter(const std::string& name,
                                                       const JsonValue& parameter,
                                                       const std::filesystem::path& path,
                                                       std::string_view assetContext)
        {
            const std::string context = std::string(assetContext) + " parameter '" + name + "'";
            RequireKnownUniqueMembers(parameter, {"type", "value"}, path, context);
            const std::string type =
                RequireMember(parameter, "type", JsonType::String, path, context).stringValue;
            const JsonValue* value = parameter.FindMember("value");
            if (value == nullptr) { Fail(path, context, "is missing field 'value'."); }
            if (type == "bool")
            {
                if (value->type != JsonType::Boolean)
                {
                    Fail(path, context, "boolean value must be true or false.");
                }
                return value->boolValue;
            }
            if (type == "string")
            {
                if (value->type != JsonType::String)
                {
                    Fail(path, context, "string value must be a JSON string.");
                }
                return value->stringValue;
            }
            if (value->type != JsonType::String)
            {
                Fail(path, context,
                     "numeric values must be strings so their exact persisted value is stable.");
            }
            if (type == "i64")
            {
                return ParseInteger<std::int64_t>(value->stringValue, path, context, name);
            }
            if (type == "u64")
            {
                return ParseInteger<std::uint64_t>(value->stringValue, path, context, name);
            }
            if (type == "f64")
            {
                double result = 0.0;
                const auto parsed = std::from_chars(value->stringValue.data(),
                                                    value->stringValue.data() +
                                                        value->stringValue.size(),
                                                    result, std::chars_format::general);
                if (parsed.ec != std::errc{} ||
                    parsed.ptr != value->stringValue.data() + value->stringValue.size() ||
                    !std::isfinite(result))
                {
                    Fail(path, context, "parameter '" + name +
                                            "' has an invalid finite floating-point value.");
                }
                return result;
            }
            Fail(path, context, "unknown parameter type '" + type +
                                    "'; expected bool, i64, u64, f64, or string.");
        }
    } // namespace

    ContentBuildConfiguration ContentBuildConfiguration::Parse(
        const std::string& json, const std::filesystem::path& sourceName)
    {
        JsonValue root;
        try
        {
            root = CNA::Internal::ParseJson(json);
        }
        catch (const CNA::Internal::JsonParseException& error)
        {
            Fail(sourceName, "JSON", error.what());
        }

        RequireKnownUniqueMembers(root, {"format", "version", "assets"}, sourceName, "root");
        if (RequireMember(root, "format", JsonType::String, sourceName, "root").stringValue !=
            kConfigurationKind)
        {
            Fail(sourceName, "root field 'format'", "has an unknown format identity.");
        }
        const double version =
            RequireMember(root, "version", JsonType::Number, sourceName, "root").numberValue;
        if (version != static_cast<double>(ContentBuildConfigurationVersion))
        {
            Fail(sourceName, "root field 'version'", "is not supported.");
        }

        const JsonValue& assets =
            RequireMember(root, "assets", JsonType::Object, sourceName, "root");
        ContentBuildConfiguration configuration;
        for (const auto& [source, value] : assets.objectValue)
        {
            RequireSafeSource(source, sourceName);
            const std::string context = "asset entry '" + source + "'";
            RequireKnownUniqueMembers(
                value, {"logicalName", "importer", "processor", "writer", "parameters"},
                sourceName, context);

            ContentAssetBuildConfiguration entry;
            entry.source = source;
            entry.logicalName = OptionalNonEmptyString(value, "logicalName", sourceName, context);
            entry.importer = OptionalNonEmptyString(value, "importer", sourceName, context);
            entry.processor = OptionalNonEmptyString(value, "processor", sourceName, context);
            entry.writer = OptionalNonEmptyString(value, "writer", sourceName, context);
            if (!entry.logicalName.empty())
            {
                const std::string problem = Cnb::CnbLogicalNameProblem(entry.logicalName);
                if (!problem.empty())
                {
                    Fail(sourceName, context + " field 'logicalName'", problem + ".");
                }
            }

            if (const JsonValue* parameters = value.FindMember("parameters"))
            {
                if (parameters->type != JsonType::Object)
                {
                    Fail(sourceName, context + " field 'parameters'", "must be a JSON object.");
                }
                std::set<std::string> names;
                for (const auto& [name, parameter] : parameters->objectValue)
                {
                    if (name.empty())
                    {
                        Fail(sourceName, context + " field 'parameters'",
                             "parameter names must not be empty.");
                    }
                    if (!names.insert(name).second)
                    {
                        Fail(sourceName, context + " field 'parameters'",
                             "repeats parameter '" + name + "'.");
                    }
                    entry.parameters.Set(
                        name, ParseParameter(name, parameter, sourceName, context));
                }
            }
            if (!configuration.entries_.emplace(source, std::move(entry)).second)
            {
                Fail(sourceName, context, "is repeated.");
            }
        }
        return configuration;
    }

    const ContentAssetBuildConfiguration* ContentBuildConfiguration::Find(
        const std::string& source) const
    {
        const auto found = entries_.find(source);
        return found == entries_.end() ? nullptr : &found->second;
    }

    const std::map<std::string, ContentAssetBuildConfiguration>&
    ContentBuildConfiguration::Entries() const noexcept
    {
        return entries_;
    }
} // namespace CNA::Content::Pipeline
