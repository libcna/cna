// SPDX-License-Identifier: MS-PL

#include "CNA/Content/Pipeline/ContentBuildManifest.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <type_traits>

#include "CNA/Content/Cnb/CnbFormat.hpp"
#include "CNA/Internal/ContentPath.hpp"
#include "CNA/Internal/Json.hpp"
#include "System/Security/Cryptography/SHA256.hpp"

namespace CNA::Content::Pipeline
{
    namespace
    {
        using CNA::Internal::JsonType;
        using CNA::Internal::JsonValue;

        constexpr const char* kManifestKind = "CNA.ContentPipeline.Manifest";

        const JsonValue& RequireMember(const JsonValue& object, const std::string& name,
                                       JsonType type)
        {
            const JsonValue* value = object.FindMember(name);
            if (value == nullptr)
            {
                throw std::runtime_error("content manifest is missing '" + name + "'.");
            }
            if (value->type != type)
            {
                throw std::runtime_error("content manifest member '" + name +
                                         "' has the wrong JSON type.");
            }
            return *value;
        }

        std::string RequireString(const JsonValue& object, const std::string& name)
        {
            return RequireMember(object, name, JsonType::String).stringValue;
        }

        std::uint32_t RequireUInt32(const JsonValue& object, const std::string& name)
        {
            const double value = RequireMember(object, name, JsonType::Number).numberValue;
            if (!std::isfinite(value) || value < 0.0 ||
                value > static_cast<double>(std::numeric_limits<std::uint32_t>::max()) ||
                std::floor(value) != value)
            {
                throw std::runtime_error("content manifest member '" + name + "' is not a u32.");
            }
            return static_cast<std::uint32_t>(value);
        }

        JsonValue StringValue(std::string value)
        {
            return JsonValue::MakeString(std::move(value));
        }

        JsonValue ComponentValue(const ContentComponentIdentity& identity)
        {
            JsonValue value = JsonValue::MakeObject();
            value.Set("name", StringValue(identity.name));
            value.Set("version", StringValue(identity.version));
            return value;
        }

        ContentComponentIdentity ParseComponent(const JsonValue& object, const std::string& name)
        {
            const JsonValue& value = RequireMember(object, name, JsonType::Object);
            return {RequireString(value, "name"), RequireString(value, "version")};
        }

        const char* DependencyKindName(ContentDependencyKind kind)
        {
            switch (kind)
            {
            case ContentDependencyKind::PrimarySource:
                return "primary-source";
            case ContentDependencyKind::SourceFile:
                return "source-file";
            case ContentDependencyKind::ContentBuild:
                return "content-build";
            case ContentDependencyKind::Generated:
                return "generated";
            }
            return "unknown";
        }

        ContentDependencyKind ParseDependencyKind(const std::string& name)
        {
            if (name == "primary-source")
            {
                return ContentDependencyKind::PrimarySource;
            }
            if (name == "source-file")
            {
                return ContentDependencyKind::SourceFile;
            }
            if (name == "content-build")
            {
                return ContentDependencyKind::ContentBuild;
            }
            if (name == "generated")
            {
                return ContentDependencyKind::Generated;
            }
            throw std::runtime_error("content manifest contains unknown dependency kind '" + name +
                                     "'.");
        }

        std::string ParameterTypeName(const ContentProcessorParameterValue& value)
        {
            if (std::holds_alternative<bool>(value))
            {
                return "bool";
            }
            if (std::holds_alternative<std::int64_t>(value))
            {
                return "i64";
            }
            if (std::holds_alternative<std::uint64_t>(value))
            {
                return "u64";
            }
            if (std::holds_alternative<double>(value))
            {
                return "f64";
            }
            return "string";
        }

        std::string ParameterText(const ContentProcessorParameterValue& value)
        {
            return std::visit(
                [](const auto& item) -> std::string
                {
                    using T = std::decay_t<decltype(item)>;
                    if constexpr (std::is_same_v<T, bool>)
                    {
                        return item ? "true" : "false";
                    }
                    else if constexpr (std::is_same_v<T, std::string>)
                    {
                        return item;
                    }
                    else
                    {
                        char buffer[128]{};
                        const auto result = [&]
                        {
                            if constexpr (std::is_same_v<T, double>)
                            {
                                return std::to_chars(buffer, buffer + sizeof(buffer), item,
                                                     std::chars_format::general,
                                                     std::numeric_limits<double>::max_digits10);
                            }
                            else
                            {
                                return std::to_chars(buffer, buffer + sizeof(buffer), item);
                            }
                        }();
                        if (result.ec != std::errc{})
                        {
                            throw std::runtime_error("cannot serialize a processor parameter.");
                        }
                        return {buffer, result.ptr};
                    }
                },
                value);
        }

        template <typename T>
        T ParseIntegerParameter(const std::string& name, const std::string& text)
        {
            T value{};
            const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
            if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
            {
                throw std::runtime_error("content manifest parameter '" + name +
                                         "' has an invalid integer value.");
            }
            return value;
        }

        ContentProcessorParameterValue ParseParameterValue(const std::string& name,
                                                           const std::string& type,
                                                           const std::string& text)
        {
            if (type == "bool")
            {
                if (text == "true")
                {
                    return true;
                }
                if (text == "false")
                {
                    return false;
                }
                throw std::runtime_error("content manifest parameter '" + name +
                                         "' has an invalid boolean value.");
            }
            if (type == "i64")
            {
                return ParseIntegerParameter<std::int64_t>(name, text);
            }
            if (type == "u64")
            {
                return ParseIntegerParameter<std::uint64_t>(name, text);
            }
            if (type == "f64")
            {
                double value = 0.0;
                const auto result = std::from_chars(text.data(), text.data() + text.size(), value,
                                                    std::chars_format::general);
                if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
                    !std::isfinite(value))
                {
                    throw std::runtime_error("content manifest parameter '" + name +
                                             "' has an invalid floating-point value.");
                }
                return value;
            }
            if (type == "string")
            {
                return text;
            }
            throw std::runtime_error("content manifest parameter '" + name +
                                     "' has unknown type '" + type + "'.");
        }

        bool IsLowerHexDigest(const std::string& value)
        {
            return value.size() == 64u &&
                   std::all_of(value.begin(), value.end(),
                               [](char character)
                               {
                                   return (character >= '0' && character <= '9') ||
                                          (character >= 'a' && character <= 'f');
                               });
        }

        void RequireSafeRelativePath(const std::string& value, const char* field)
        {
            const std::filesystem::path path = CNA::Internal::ContentPathFromUtf8(value);
            if (value.empty() || path.is_absolute())
            {
                throw std::invalid_argument(std::string("content manifest ") + field +
                                            " must be a non-empty relative path.");
            }
            for (const std::filesystem::path& part : path)
            {
                if (part == "..")
                {
                    throw std::invalid_argument(std::string("content manifest ") + field +
                                                " must not contain '..'.");
                }
            }
        }

        std::filesystem::path WeaklyCanonical(const std::filesystem::path& path)
        {
            std::error_code error;
            std::filesystem::path result = std::filesystem::weakly_canonical(path, error);
            if (error)
            {
                throw std::runtime_error("cannot resolve '" +
                                         CNA::Internal::ContentPathToUtf8(path) +
                                         "': " + error.message() + ".");
            }
            return result;
        }

        bool IsWithin(const std::filesystem::path& root, const std::filesystem::path& path)
        {
            auto rootPart = root.begin();
            auto pathPart = path.begin();
            while (rootPart != root.end() && pathPart != path.end())
            {
                if (*rootPart != *pathPart)
                {
                    return false;
                }
                ++rootPart;
                ++pathPart;
            }
            return rootPart == root.end();
        }

        std::string RelativeContained(const std::filesystem::path& root,
                                      const std::filesystem::path& path, const char* description)
        {
            const std::filesystem::path canonicalRoot = WeaklyCanonical(root);
            const std::filesystem::path canonicalPath = WeaklyCanonical(path);
            if (!IsWithin(canonicalRoot, canonicalPath))
            {
                throw std::runtime_error(
                    std::string(description) + " '" +
                    CNA::Internal::ContentPathToUtf8(path) + "' escapes root '" +
                    CNA::Internal::ContentPathToUtf8(root) + "'.");
            }
            return CNA::Internal::ContentPathToUtf8(
                std::filesystem::relative(canonicalPath, canonicalRoot));
        }

        std::filesystem::path ResolveContained(const std::filesystem::path& root,
                                               const std::string& relative, const char* description)
        {
            RequireSafeRelativePath(relative, description);
            const std::filesystem::path canonicalRoot = WeaklyCanonical(root);
            const std::filesystem::path resolved = WeaklyCanonical(
                canonicalRoot / CNA::Internal::ContentPathFromUtf8(relative));
            if (!IsWithin(canonicalRoot, resolved))
            {
                throw std::runtime_error(std::string(description) + " '" + relative +
                                         "' escapes the source root.");
            }
            return resolved;
        }

        std::vector<std::uint8_t> ReadAllBytes(const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::binary | std::ios::ate);
            if (!stream)
            {
                throw std::runtime_error("cannot open '" +
                                         CNA::Internal::ContentPathToUtf8(path) +
                                         "' for hashing.");
            }
            const std::streamoff size = stream.tellg();
            if (size < 0 ||
                static_cast<std::uint64_t>(size) >
                    static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()))
            {
                throw std::runtime_error("file '" + CNA::Internal::ContentPathToUtf8(path) +
                                         "' is too large for the current SHA-256 API.");
            }
            stream.seekg(0, std::ios::beg);
            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
            if (!bytes.empty())
            {
                stream.read(reinterpret_cast<char*>(bytes.data()), size);
                if (!stream)
                {
                    throw std::runtime_error("cannot read '" +
                                             CNA::Internal::ContentPathToUtf8(path) +
                                             "' completely for hashing.");
                }
            }
            return bytes;
        }

        class CanonicalFingerprint
        {
        public:
            void AddString(std::string_view value)
            {
                AddU64(static_cast<std::uint64_t>(value.size()));
                bytes_.insert(bytes_.end(), value.begin(), value.end());
            }

            void AddU64(std::uint64_t value)
            {
                for (unsigned shift = 0u; shift < 64u; shift += 8u)
                {
                    bytes_.push_back(static_cast<std::uint8_t>(value >> shift));
                }
            }

            [[nodiscard]] std::string Finish() const
            {
                return ContentSha256(bytes_);
            }

        private:
            std::vector<std::uint8_t> bytes_;
        };

        void AddIdentity(CanonicalFingerprint& fingerprint,
                         const ContentComponentIdentity& identity)
        {
            fingerprint.AddString(identity.name);
            fingerprint.AddString(identity.version);
        }
    } // namespace

    ContentBuildManifest ContentBuildManifest::Parse(const std::string& json)
    {
        const JsonValue root = CNA::Internal::ParseJson(json);
        if (root.type != JsonType::Object)
        {
            throw std::runtime_error("content manifest root must be a JSON object.");
        }
        if (RequireString(root, "format") != kManifestKind)
        {
            throw std::runtime_error("content manifest has an unknown format identity.");
        }
        if (RequireUInt32(root, "version") != ContentBuildManifestVersion)
        {
            throw std::runtime_error("content manifest version is not supported.");
        }

        ContentBuildManifest manifest;
        const JsonValue& assets = RequireMember(root, "assets", JsonType::Array);
        for (const JsonValue& value : assets.arrayValue)
        {
            if (value.type != JsonType::Object)
            {
                throw std::runtime_error("content manifest asset entry must be an object.");
            }
            ContentBuildManifestEntry entry;
            entry.logicalName = RequireString(value, "logicalName");
            entry.source = RequireString(value, "source");
            entry.output = RequireString(value, "output");
            entry.importer = ParseComponent(value, "importer");
            entry.processor = ParseComponent(value, "processor");
            entry.writer = ParseComponent(value, "writer");
            entry.assetTypeId = RequireUInt32(value, "assetTypeId");
            entry.fingerprint = RequireString(value, "fingerprint");
            entry.outputSha256 = RequireString(value, "outputSha256");

            for (const JsonValue& parameter :
                 RequireMember(value, "parameters", JsonType::Array).arrayValue)
            {
                if (parameter.type != JsonType::Object)
                {
                    throw std::runtime_error("content manifest parameter must be an object.");
                }
                const std::string name = RequireString(parameter, "name");
                if (entry.parameters.Find(name) != nullptr)
                {
                    throw std::runtime_error("content manifest repeats parameter '" + name + "'.");
                }
                entry.parameters.Set(name,
                                     ParseParameterValue(name, RequireString(parameter, "type"),
                                                         RequireString(parameter, "value")));
            }

            for (const JsonValue& dependency :
                 RequireMember(value, "dependencies", JsonType::Array).arrayValue)
            {
                if (dependency.type != JsonType::Object)
                {
                    throw std::runtime_error("content manifest dependency must be an object.");
                }
                entry.dependencies.push_back(
                    {ParseDependencyKind(RequireString(dependency, "kind")),
                     RequireString(dependency, "identity")});
            }
            std::sort(entry.dependencies.begin(), entry.dependencies.end());

            for (const JsonValue& reference :
                 RequireMember(value, "runtimeReferences", JsonType::Array).arrayValue)
            {
                if (reference.type != JsonType::Object)
                {
                    throw std::runtime_error(
                        "content manifest runtime reference must be an object.");
                }
                entry.runtimeReferences.push_back(
                    {RequireString(reference, "logicalName"),
                     RequireUInt32(reference, "expectedAssetTypeId")});
            }
            std::sort(entry.runtimeReferences.begin(), entry.runtimeReferences.end());
            if (manifest.Find(entry.logicalName) != nullptr)
            {
                throw std::runtime_error("content manifest repeats logical asset '" +
                                         entry.logicalName + "'.");
            }
            manifest.Set(std::move(entry));
        }
        return manifest;
    }

    std::string ContentBuildManifest::Serialize() const
    {
        JsonValue root = JsonValue::MakeObject();
        root.Set("format", StringValue(kManifestKind));
        root.Set("version", JsonValue::MakeNumber(ContentBuildManifestVersion));
        JsonValue assets = JsonValue::MakeArray();
        for (const auto& [logicalName, entry] : entries_)
        {
            static_cast<void>(logicalName);
            JsonValue value = JsonValue::MakeObject();
            value.Set("logicalName", StringValue(entry.logicalName));
            value.Set("source", StringValue(entry.source));
            value.Set("output", StringValue(entry.output));
            value.Set("importer", ComponentValue(entry.importer));
            value.Set("processor", ComponentValue(entry.processor));
            value.Set("writer", ComponentValue(entry.writer));

            JsonValue parameters = JsonValue::MakeArray();
            for (const auto& [name, parameterValue] : entry.parameters.Values())
            {
                JsonValue parameter = JsonValue::MakeObject();
                parameter.Set("name", StringValue(name));
                parameter.Set("type", StringValue(ParameterTypeName(parameterValue)));
                parameter.Set("value", StringValue(ParameterText(parameterValue)));
                parameters.arrayValue.push_back(std::move(parameter));
            }
            value.Set("parameters", std::move(parameters));

            JsonValue dependencies = JsonValue::MakeArray();
            for (const ContentDependency& dependency : entry.dependencies)
            {
                JsonValue item = JsonValue::MakeObject();
                item.Set("kind", StringValue(DependencyKindName(dependency.kind)));
                item.Set("identity", StringValue(dependency.identity));
                dependencies.arrayValue.push_back(std::move(item));
            }
            value.Set("dependencies", std::move(dependencies));

            JsonValue references = JsonValue::MakeArray();
            for (const RuntimeContentReference& reference : entry.runtimeReferences)
            {
                JsonValue item = JsonValue::MakeObject();
                item.Set("logicalName", StringValue(reference.logicalName));
                item.Set("expectedAssetTypeId",
                         JsonValue::MakeNumber(reference.expectedAssetTypeId));
                references.arrayValue.push_back(std::move(item));
            }
            value.Set("runtimeReferences", std::move(references));
            value.Set("assetTypeId", JsonValue::MakeNumber(entry.assetTypeId));
            value.Set("fingerprint", StringValue(entry.fingerprint));
            value.Set("outputSha256", StringValue(entry.outputSha256));
            assets.arrayValue.push_back(std::move(value));
        }
        root.Set("assets", std::move(assets));
        return CNA::Internal::WriteJson(root) + "\n";
    }

    const ContentBuildManifestEntry*
    ContentBuildManifest::Find(const std::string& logicalName) const
    {
        const auto found = entries_.find(logicalName);
        return found == entries_.end() ? nullptr : &found->second;
    }

    void ContentBuildManifest::Set(ContentBuildManifestEntry entry)
    {
        const std::string logicalProblem = Cnb::CnbLogicalNameProblem(entry.logicalName);
        if (!logicalProblem.empty())
        {
            throw std::invalid_argument("content manifest logical name '" + entry.logicalName +
                                        "' is invalid: " + logicalProblem + ".");
        }
        RequireSafeRelativePath(entry.source, "source");
        RequireSafeRelativePath(entry.output, "output");
        if (entry.importer.name.empty() || entry.importer.version.empty() ||
            entry.processor.name.empty() || entry.processor.version.empty() ||
            entry.writer.name.empty() || entry.writer.version.empty())
        {
            throw std::invalid_argument("content manifest component identities must not be empty.");
        }
        if (entry.assetTypeId == 0u)
        {
            throw std::invalid_argument("content manifest asset type id must not be zero.");
        }
        if (!IsLowerHexDigest(entry.fingerprint) || !IsLowerHexDigest(entry.outputSha256))
        {
            throw std::invalid_argument(
                "content manifest fingerprints must be lowercase SHA-256 values.");
        }
        for (const ContentDependency& dependency : entry.dependencies)
        {
            if (dependency.kind == ContentDependencyKind::ContentBuild)
            {
                const std::string problem = Cnb::CnbLogicalNameProblem(dependency.identity);
                if (!problem.empty())
                {
                    throw std::invalid_argument("content-build dependency '" + dependency.identity +
                                                "' is invalid: " + problem + ".");
                }
            }
            else
            {
                RequireSafeRelativePath(dependency.identity, "dependency identity");
            }
        }
        const std::size_t primarySources = static_cast<std::size_t>(
            std::count(entry.dependencies.begin(), entry.dependencies.end(),
                       ContentDependency{ContentDependencyKind::PrimarySource, entry.source}));
        if (primarySources != 1u)
        {
            throw std::invalid_argument("content manifest must contain exactly one "
                                        "primary-source dependency matching "
                                        "its source field.");
        }
        for (const RuntimeContentReference& reference : entry.runtimeReferences)
        {
            const std::string problem = Cnb::CnbLogicalNameProblem(reference.logicalName);
            if (!problem.empty())
            {
                throw std::invalid_argument("runtime reference '" + reference.logicalName +
                                            "' is invalid: " + problem + ".");
            }
        }
        std::sort(entry.dependencies.begin(), entry.dependencies.end());
        entry.dependencies.erase(std::unique(entry.dependencies.begin(), entry.dependencies.end()),
                                 entry.dependencies.end());
        std::sort(entry.runtimeReferences.begin(), entry.runtimeReferences.end());
        entry.runtimeReferences.erase(
            std::unique(entry.runtimeReferences.begin(), entry.runtimeReferences.end()),
            entry.runtimeReferences.end());
        entries_.insert_or_assign(entry.logicalName, std::move(entry));
    }

    void ContentBuildManifest::Clear() noexcept
    {
        entries_.clear();
    }

    const std::map<std::string, ContentBuildManifestEntry>&
    ContentBuildManifest::Entries() const noexcept
    {
        return entries_;
    }

    std::string ContentSha256(const std::vector<std::uint8_t>& bytes)
    {
        if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
        {
            throw std::runtime_error("byte sequence is too large for the current SHA-256 API.");
        }
        System::Security::Cryptography::SHA256 sha256;
        const std::vector<std::uint8_t> digest = sha256.ComputeHash(bytes);
        constexpr char digits[] = "0123456789abcdef";
        std::string result;
        result.reserve(digest.size() * 2u);
        for (std::uint8_t byte : digest)
        {
            result.push_back(digits[byte >> 4u]);
            result.push_back(digits[byte & 0x0Fu]);
        }
        return result;
    }

    std::string ContentFileSha256(const std::filesystem::path& path)
    {
        return ContentSha256(ReadAllBytes(path));
    }

    std::string ComputeContentBuildFingerprint(
        const ContentBuildManifestEntry& entry, const std::filesystem::path& sourceRoot,
        const std::map<std::string, std::string>& contentBuildFingerprints)
    {
        CanonicalFingerprint fingerprint;
        fingerprint.AddString("CNA.ContentPipeline.Fingerprint");
        fingerprint.AddU64(ContentBuildManifestVersion);
        fingerprint.AddU64(Cnb::Format::ContainerMajor);
        fingerprint.AddU64(Cnb::Format::ContainerMinor);
        fingerprint.AddString(entry.logicalName);
        fingerprint.AddString(entry.source);
        AddIdentity(fingerprint, entry.importer);
        AddIdentity(fingerprint, entry.processor);
        AddIdentity(fingerprint, entry.writer);
        fingerprint.AddU64(entry.assetTypeId);
        fingerprint.AddString(
            ContentFileSha256(ResolveContained(sourceRoot, entry.source, "primary source")));

        fingerprint.AddU64(entry.parameters.Values().size());
        for (const auto& [name, value] : entry.parameters.Values())
        {
            fingerprint.AddString(name);
            fingerprint.AddString(ParameterTypeName(value));
            fingerprint.AddString(ParameterText(value));
        }

        std::vector<ContentDependency> dependencies = entry.dependencies;
        std::sort(dependencies.begin(), dependencies.end());
        fingerprint.AddU64(dependencies.size());
        for (const ContentDependency& dependency : dependencies)
        {
            fingerprint.AddU64(static_cast<std::uint64_t>(dependency.kind));
            fingerprint.AddString(dependency.identity);
            if (dependency.kind == ContentDependencyKind::ContentBuild)
            {
                const auto found = contentBuildFingerprints.find(dependency.identity);
                if (found == contentBuildFingerprints.end() || !IsLowerHexDigest(found->second))
                {
                    throw std::runtime_error("content-build dependency '" + dependency.identity +
                                             "' has no current effective fingerprint.");
                }
                fingerprint.AddString(found->second);
            }
            else
            {
                const std::filesystem::path path =
                    ResolveContained(sourceRoot, dependency.identity, "dependency");
                fingerprint.AddString(ContentFileSha256(path));
            }
        }
        return fingerprint.Finish();
    }

    ContentBuildManifestEntry MakeContentBuildManifestEntry(const ContentBuildResult& result,
                                                            const std::filesystem::path& sourceRoot,
                                                            const std::filesystem::path& outputRoot,
                                                            const std::filesystem::path& outputPath)
    {
        ContentBuildManifestEntry entry;
        entry.logicalName = result.logicalName;
        entry.source = RelativeContained(sourceRoot, result.source, "primary source");
        entry.output = RelativeContained(outputRoot, outputPath, "output");
        entry.importer = result.importer;
        entry.processor = result.processor;
        entry.writer = result.writer;
        entry.parameters = result.parameters;
        entry.runtimeReferences = result.runtimeReferences;
        entry.assetTypeId = result.output.assetTypeId;
        entry.dependencies.reserve(result.dependencies.size());
        for (const ContentDependency& dependency : result.dependencies)
        {
            ContentDependency normalized = dependency;
            if (dependency.kind != ContentDependencyKind::ContentBuild)
            {
                normalized.identity = RelativeContained(
                    sourceRoot, CNA::Internal::ContentPathFromUtf8(dependency.identity),
                    "dependency");
            }
            entry.dependencies.push_back(std::move(normalized));
        }
        std::sort(entry.dependencies.begin(), entry.dependencies.end());
        return entry;
    }
} // namespace CNA::Content::Pipeline
