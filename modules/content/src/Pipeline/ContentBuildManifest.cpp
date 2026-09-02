// SPDX-License-Identifier: MS-PL

#include "CNA/Content/Pipeline/ContentBuildManifest.hpp"

#include "CNA/Content/Pipeline/XnbOutput.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include <stdexcept>
#include <string_view>
#include <tuple>
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

        JsonValue FingerprintStateValue(const ContentBuildFingerprintState& state)
        {
            JsonValue value = JsonValue::MakeObject();
            value.Set("primarySourceBytes", StringValue(state.primarySourceBytes));
            value.Set("sourceDependencySet", StringValue(state.sourceDependencySet));
            value.Set("sourceDependencyBytes", StringValue(state.sourceDependencyBytes));
            value.Set("contentDependencySet", StringValue(state.contentDependencySet));
            value.Set("contentDependencyFingerprints",
                      StringValue(state.contentDependencyFingerprints));
            value.Set("processorParameters", StringValue(state.processorParameters));
            value.Set("writerSchemas", StringValue(state.writerSchemas));
            value.Set("outputDefinitions", StringValue(state.outputDefinitions));
            value.Set("deploymentDefinitions", StringValue(state.deploymentDefinitions));
            return value;
        }

        ContentBuildFingerprintState ParseFingerprintState(const JsonValue& object)
        {
            const JsonValue& value = RequireMember(object, "fingerprintState", JsonType::Object);
            return {
                RequireString(value, "primarySourceBytes"),
                RequireString(value, "sourceDependencySet"),
                RequireString(value, "sourceDependencyBytes"),
                RequireString(value, "contentDependencySet"),
                RequireString(value, "contentDependencyFingerprints"),
                RequireString(value, "processorParameters"),
                RequireString(value, "writerSchemas"),
                RequireString(value, "outputDefinitions"),
                RequireString(value, "deploymentDefinitions"),
            };
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

        const std::filesystem::path& SelectSourceRoot(
            const std::filesystem::path& sourceRoot,
            const ContentSourceRootCapabilities& externalSourceRoots,
            const std::string& alias, const char* description)
        {
            if (alias.empty()) { return sourceRoot; }
            const std::string problem = ContentSourceRootAliasProblem(alias);
            if (!problem.empty())
            {
                throw std::runtime_error(std::string(description) + " uses invalid source-root "
                                         "alias '" + alias + "': " + problem + ".");
            }
            const std::filesystem::path* root = externalSourceRoots.Find(alias);
            if (root == nullptr)
            {
                throw std::runtime_error(std::string(description) +
                                         " uses unavailable source-root alias '" + alias + "'.");
            }
            return *root;
        }

        std::filesystem::path ResolveSourceIdentity(
            const std::filesystem::path& sourceRoot,
            const ContentSourceRootCapabilities& externalSourceRoots,
            const std::string& alias, const std::string& identity, const char* description)
        {
            return ResolveContained(
                SelectSourceRoot(sourceRoot, externalSourceRoots, alias, description),
                identity, description);
        }

        // SharpRuntime deliberately omits ComputeHash(Stream), but its protected hash core is
        // incremental. This adapter keeps one SHA-256 implementation while feeding bounded chunks.
        class StreamingSha256 final : public System::Security::Cryptography::SHA256
        {
        public:
            void Append(const std::vector<std::uint8_t>& bytes, std::size_t count)
            {
                if (count > bytes.size() ||
                    count > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
                {
                    throw std::invalid_argument(
                        "StreamingSha256::Append(): count is outside the input buffer.");
                }
                HashCore(bytes, 0, static_cast<SharpRuntime::intcs>(count));
            }

            [[nodiscard]] std::vector<std::uint8_t> Finish()
            {
                std::vector<std::uint8_t> digest = HashFinal();
                Initialize();
                return digest;
            }
        };

        std::string LowerHex(const std::vector<std::uint8_t>& bytes)
        {
            constexpr char digits[] = "0123456789abcdef";
            std::string result;
            result.reserve(bytes.size() * 2u);
            for (std::uint8_t byte : bytes)
            {
                result.push_back(digits[byte >> 4u]);
                result.push_back(digits[byte & 0x0Fu]);
            }
            return result;
        }

        std::string HashFileStreaming(const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream)
            {
                throw std::runtime_error("cannot open '" +
                                         CNA::Internal::ContentPathToUtf8(path) +
                                         "' for hashing.");
            }

            constexpr std::size_t kBufferSize = 1024u * 1024u;
            std::vector<std::uint8_t> buffer(kBufferSize);
            StreamingSha256 sha256;
            for (;;)
            {
                stream.read(reinterpret_cast<char*>(buffer.data()),
                            static_cast<std::streamsize>(buffer.size()));
                const std::streamsize count = stream.gcount();
                if (count > 0)
                {
                    sha256.Append(buffer, static_cast<std::size_t>(count));
                }
                if (stream.eof()) { break; }
                if (!stream)
                {
                    throw std::runtime_error("cannot read '" +
                                             CNA::Internal::ContentPathToUtf8(path) +
                                             "' completely for hashing.");
                }
            }
            return LowerHex(sha256.Finish());
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

        ContentBuildFingerprintState ComputeDirectFingerprintState(
            const ContentBuildManifestEntry& entry, const std::filesystem::path& sourceRoot,
            const ContentSourceRootCapabilities& externalSourceRoots)
        {
            ContentBuildFingerprintState state;
            const std::string primarySourceDigest =
                ContentFileSha256(ResolveContained(sourceRoot, entry.source, "primary source"));
            state.primarySourceBytes = primarySourceDigest;

            std::vector<ContentDependency> sourceDependencies;
            std::vector<ContentDependency> contentDependencies;
            for (const ContentDependency& dependency : entry.dependencies)
            {
                if (dependency.kind == ContentDependencyKind::ContentBuild)
                {
                    contentDependencies.push_back(dependency);
                }
                else if (dependency.kind != ContentDependencyKind::PrimarySource)
                {
                    sourceDependencies.push_back(dependency);
                }
            }
            std::sort(sourceDependencies.begin(), sourceDependencies.end());
            std::sort(contentDependencies.begin(), contentDependencies.end());

            CanonicalFingerprint sourceSet;
            sourceSet.AddString("CNA.ContentPipeline.SourceDependencySet");
            sourceSet.AddU64(ContentBuildManifestVersion);
            sourceSet.AddU64(sourceDependencies.size());
            CanonicalFingerprint sourceBytes;
            sourceBytes.AddString("CNA.ContentPipeline.SourceDependencyBytes");
            sourceBytes.AddU64(ContentBuildManifestVersion);
            sourceBytes.AddU64(sourceDependencies.size());
            for (const ContentDependency& dependency : sourceDependencies)
            {
                sourceSet.AddU64(static_cast<std::uint64_t>(dependency.kind));
                sourceSet.AddString(dependency.sourceRoot);
                sourceSet.AddString(dependency.identity);
                sourceBytes.AddU64(static_cast<std::uint64_t>(dependency.kind));
                sourceBytes.AddString(dependency.sourceRoot);
                sourceBytes.AddString(dependency.identity);
                sourceBytes.AddString(ContentFileSha256(
                    ResolveSourceIdentity(sourceRoot, externalSourceRoots,
                                          dependency.sourceRoot, dependency.identity,
                                          "dependency")));
            }
            state.sourceDependencySet = sourceSet.Finish();
            state.sourceDependencyBytes = sourceBytes.Finish();

            CanonicalFingerprint contentSet;
            contentSet.AddString("CNA.ContentPipeline.ContentDependencySet");
            contentSet.AddU64(ContentBuildManifestVersion);
            contentSet.AddU64(contentDependencies.size());
            for (const ContentDependency& dependency : contentDependencies)
            {
                contentSet.AddString(dependency.identity);
            }
            state.contentDependencySet = contentSet.Finish();

            CanonicalFingerprint parameters;
            parameters.AddString("CNA.ContentPipeline.ProcessorParameters");
            parameters.AddU64(ContentBuildManifestVersion);
            parameters.AddU64(entry.parameters.Values().size());
            for (const auto& [name, value] : entry.parameters.Values())
            {
                parameters.AddString(name);
                parameters.AddString(ParameterTypeName(value));
                parameters.AddString(ParameterText(value));
            }
            state.processorParameters = parameters.Finish();

            std::vector<ContentWriterSchemaIdentity> writerSchemas = entry.writerSchemas;
            std::sort(writerSchemas.begin(), writerSchemas.end(),
                      [](const ContentWriterSchemaIdentity& left,
                         const ContentWriterSchemaIdentity& right)
            {
                if (left.assetTypeId != right.assetTypeId)
                {
                    return left.assetTypeId < right.assetTypeId;
                }
                if (left.assetTypeName != right.assetTypeName)
                {
                    return left.assetTypeName < right.assetTypeName;
                }
                return left.assetSchemaVersion < right.assetSchemaVersion;
            });
            CanonicalFingerprint schemas;
            schemas.AddString("CNA.ContentPipeline.WriterSchemas");
            schemas.AddU64(ContentBuildManifestVersion);
            schemas.AddString(entry.outputFormat);
            schemas.AddU64(writerSchemas.size());
            for (const ContentWriterSchemaIdentity& schema : writerSchemas)
            {
                schemas.AddU64(schema.assetTypeId);
                schemas.AddU64(schema.assetSchemaVersion);
                schemas.AddString(schema.assetTypeName);
                AddIdentity(schemas, schema.codec);
            }
            state.writerSchemas = schemas.Finish();

            std::vector<ContentBuildManifestOutput> outputs = entry.outputs;
            std::sort(outputs.begin(), outputs.end(),
                      [](const ContentBuildManifestOutput& left,
                         const ContentBuildManifestOutput& right)
            {
                return left.logicalName < right.logicalName;
            });
            std::vector<RuntimeContentReference> runtimeReferences = entry.runtimeReferences;
            std::sort(runtimeReferences.begin(), runtimeReferences.end());
            CanonicalFingerprint outputDefinitions;
            outputDefinitions.AddString("CNA.ContentPipeline.OutputDefinitions");
            outputDefinitions.AddU64(ContentBuildManifestVersion);
            outputDefinitions.AddU64(outputs.size());
            for (const ContentBuildManifestOutput& output : outputs)
            {
                outputDefinitions.AddString(output.logicalName);
                outputDefinitions.AddString(output.path);
                outputDefinitions.AddU64(output.assetTypeId);
                outputDefinitions.AddU64(output.assetSchemaVersion);
                outputDefinitions.AddString(output.assetTypeName);
                outputDefinitions.AddString(output.rootReaderName);
            }
            outputDefinitions.AddU64(runtimeReferences.size());
            for (const RuntimeContentReference& reference : runtimeReferences)
            {
                outputDefinitions.AddString(reference.logicalName);
                outputDefinitions.AddU64(reference.expectedAssetTypeId);
            }
            state.outputDefinitions = outputDefinitions.Finish();

            std::vector<ContentBuildManifestDeploymentFile> deploymentFiles =
                entry.deploymentFiles;
            std::sort(deploymentFiles.begin(), deploymentFiles.end(),
                      [](const ContentBuildManifestDeploymentFile& left,
                         const ContentBuildManifestDeploymentFile& right)
            {
                return left.path < right.path;
            });
            CanonicalFingerprint deploymentDefinitions;
            deploymentDefinitions.AddString("CNA.ContentPipeline.DeploymentDefinitions");
            deploymentDefinitions.AddU64(ContentBuildManifestVersion);
            deploymentDefinitions.AddU64(deploymentFiles.size());
            for (const ContentBuildManifestDeploymentFile& deployment : deploymentFiles)
            {
                deploymentDefinitions.AddString(deployment.sourceRoot);
                deploymentDefinitions.AddString(deployment.source);
                deploymentDefinitions.AddString(deployment.path);
            }
            state.deploymentDefinitions = deploymentDefinitions.Finish();
            return state;
        }

        std::string ComputeDirectFingerprintFromState(
            const ContentBuildManifestEntry& entry,
            const ContentBuildFingerprintState& state)
        {
            CanonicalFingerprint fingerprint;
            fingerprint.AddString("CNA.ContentPipeline.DirectFingerprint");
            fingerprint.AddU64(ContentBuildManifestVersion);
            fingerprint.AddU64(Cnb::Format::ContainerMajor);
            fingerprint.AddU64(Cnb::Format::ContainerMinor);
            fingerprint.AddString(entry.nodeId);
            fingerprint.AddString(entry.source);
            AddIdentity(fingerprint, entry.importer);
            AddIdentity(fingerprint, entry.processor);
            AddIdentity(fingerprint, entry.writer);
            fingerprint.AddString(state.primarySourceBytes);
            fingerprint.AddString(state.sourceDependencySet);
            fingerprint.AddString(state.sourceDependencyBytes);
            fingerprint.AddString(state.contentDependencySet);
            fingerprint.AddString(state.processorParameters);
            fingerprint.AddString(state.writerSchemas);
            fingerprint.AddString(state.outputDefinitions);
            fingerprint.AddString(state.deploymentDefinitions);
            return fingerprint.Finish();
        }

        std::string ComputeContentDependencyFingerprint(
            const ContentBuildManifestEntry& entry,
            const std::map<std::string, std::string>& contentBuildFingerprints)
        {
            std::vector<ContentDependency> dependencies;
            std::copy_if(entry.dependencies.begin(), entry.dependencies.end(),
                         std::back_inserter(dependencies),
                         [](const ContentDependency& dependency)
            {
                return dependency.kind == ContentDependencyKind::ContentBuild;
            });
            std::sort(dependencies.begin(), dependencies.end());

            CanonicalFingerprint fingerprint;
            fingerprint.AddString("CNA.ContentPipeline.ContentDependencyFingerprints");
            fingerprint.AddU64(ContentBuildManifestVersion);
            fingerprint.AddU64(dependencies.size());
            for (const ContentDependency& dependency : dependencies)
            {
                const auto found = contentBuildFingerprints.find(dependency.identity);
                if (found == contentBuildFingerprints.end() || !IsLowerHexDigest(found->second))
                {
                    throw std::runtime_error(
                        "content-build dependency '" + dependency.identity +
                        "' has no current effective fingerprint.");
                }
                fingerprint.AddString(dependency.identity);
                fingerprint.AddString(found->second);
            }
            return fingerprint.Finish();
        }

        std::string ComputeEffectiveFingerprintFromState(
            const ContentBuildManifestEntry& entry,
            const std::string& contentDependencyFingerprint)
        {
            if (!IsLowerHexDigest(entry.directFingerprint))
            {
                throw std::runtime_error("content build node '" + entry.nodeId +
                                         "' has no current direct fingerprint.");
            }
            if (!IsLowerHexDigest(contentDependencyFingerprint))
            {
                throw std::runtime_error("content build node '" + entry.nodeId +
                                         "' has no current content-dependency fingerprint.");
            }
            CanonicalFingerprint fingerprint;
            fingerprint.AddString("CNA.ContentPipeline.EffectiveFingerprint");
            fingerprint.AddU64(ContentBuildManifestVersion);
            fingerprint.AddString(entry.directFingerprint);
            fingerprint.AddString(contentDependencyFingerprint);
            return fingerprint.Finish();
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
            entry.nodeId = RequireString(value, "nodeId");
            entry.outputFormat = RequireString(value, "outputFormat");
            entry.source = RequireString(value, "source");
            entry.importer = ParseComponent(value, "importer");
            entry.processor = ParseComponent(value, "processor");
            entry.writer = ParseComponent(value, "writer");
            for (const JsonValue& schema :
                 RequireMember(value, "writerSchemas", JsonType::Array).arrayValue)
            {
                if (schema.type != JsonType::Object)
                {
                    throw std::runtime_error(
                        "content manifest writer schema identity must be an object.");
                }
                entry.writerSchemas.push_back(
                    {RequireUInt32(schema, "assetTypeId"),
                     RequireUInt32(schema, "assetSchemaVersion"),
                     RequireString(schema, "assetTypeName"),
                     ParseComponent(schema, "codec")});
            }
            entry.directFingerprint = RequireString(value, "directFingerprint");
            entry.fingerprint = RequireString(value, "fingerprint");
            entry.fingerprintState = ParseFingerprintState(value);

            for (const JsonValue& output :
                 RequireMember(value, "outputs", JsonType::Array).arrayValue)
            {
                if (output.type != JsonType::Object)
                {
                    throw std::runtime_error("content manifest output must be an object.");
                }
                entry.outputs.push_back(ContentBuildManifestOutput{
                    .logicalName = RequireString(output, "logicalName"),
                    .path = RequireString(output, "path"),
                    .assetTypeId = RequireUInt32(output, "assetTypeId"),
                    .assetSchemaVersion = RequireUInt32(output, "assetSchemaVersion"),
                    .assetTypeName = RequireString(output, "assetTypeName"),
                    .rootReaderName = RequireString(output, "rootReaderName"),
                    .sha256 = RequireString(output, "sha256")});
            }

            for (const JsonValue& deployment :
                 RequireMember(value, "deploymentFiles", JsonType::Array).arrayValue)
            {
                if (deployment.type != JsonType::Object)
                {
                    throw std::runtime_error(
                        "content manifest deployment file must be an object.");
                }
                entry.deploymentFiles.push_back(
                    {RequireString(deployment, "sourceRoot"),
                     RequireString(deployment, "source"),
                     RequireString(deployment, "path"),
                     RequireString(deployment, "sha256")});
            }

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
                     RequireString(dependency, "identity"),
                     RequireString(dependency, "sourceRoot")});
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
            if (manifest.Find(entry.nodeId) != nullptr)
            {
                throw std::runtime_error("content manifest repeats build node '" + entry.nodeId +
                                         "'.");
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
            value.Set("nodeId", StringValue(entry.nodeId));
            value.Set("outputFormat", StringValue(entry.outputFormat));
            value.Set("source", StringValue(entry.source));
            value.Set("importer", ComponentValue(entry.importer));
            value.Set("processor", ComponentValue(entry.processor));
            value.Set("writer", ComponentValue(entry.writer));

            JsonValue writerSchemas = JsonValue::MakeArray();
            for (const ContentWriterSchemaIdentity& schema : entry.writerSchemas)
            {
                JsonValue item = JsonValue::MakeObject();
                item.Set("assetTypeId", JsonValue::MakeNumber(schema.assetTypeId));
                item.Set("assetSchemaVersion",
                         JsonValue::MakeNumber(schema.assetSchemaVersion));
                item.Set("assetTypeName", StringValue(schema.assetTypeName));
                item.Set("codec", ComponentValue(schema.codec));
                writerSchemas.arrayValue.push_back(std::move(item));
            }
            value.Set("writerSchemas", std::move(writerSchemas));

            JsonValue outputs = JsonValue::MakeArray();
            for (const ContentBuildManifestOutput& output : entry.outputs)
            {
                JsonValue item = JsonValue::MakeObject();
                item.Set("logicalName", StringValue(output.logicalName));
                item.Set("path", StringValue(output.path));
                item.Set("assetTypeId", JsonValue::MakeNumber(output.assetTypeId));
                item.Set("assetSchemaVersion",
                         JsonValue::MakeNumber(output.assetSchemaVersion));
                item.Set("assetTypeName", StringValue(output.assetTypeName));
                item.Set("rootReaderName", StringValue(output.rootReaderName));
                item.Set("sha256", StringValue(output.sha256));
                outputs.arrayValue.push_back(std::move(item));
            }
            value.Set("outputs", std::move(outputs));

            JsonValue deploymentFiles = JsonValue::MakeArray();
            for (const ContentBuildManifestDeploymentFile& deployment :
                 entry.deploymentFiles)
            {
                JsonValue item = JsonValue::MakeObject();
                item.Set("sourceRoot", StringValue(deployment.sourceRoot));
                item.Set("source", StringValue(deployment.source));
                item.Set("path", StringValue(deployment.path));
                item.Set("sha256", StringValue(deployment.sha256));
                deploymentFiles.arrayValue.push_back(std::move(item));
            }
            value.Set("deploymentFiles", std::move(deploymentFiles));

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
                item.Set("sourceRoot", StringValue(dependency.sourceRoot));
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
            value.Set("fingerprintState", FingerprintStateValue(entry.fingerprintState));
            value.Set("directFingerprint", StringValue(entry.directFingerprint));
            value.Set("fingerprint", StringValue(entry.fingerprint));
            assets.arrayValue.push_back(std::move(value));
        }
        root.Set("assets", std::move(assets));
        return CNA::Internal::WriteJson(root) + "\n";
    }

    const ContentBuildManifestEntry*
    ContentBuildManifest::Find(const std::string& nodeId) const
    {
        const auto found = entries_.find(nodeId);
        return found == entries_.end() ? nullptr : &found->second;
    }

    void ContentBuildManifest::Set(ContentBuildManifestEntry entry)
    {
        const std::string logicalProblem = Cnb::CnbLogicalNameProblem(entry.nodeId);
        if (!logicalProblem.empty())
        {
            throw std::invalid_argument("content manifest node id '" + entry.nodeId +
                                        "' is invalid: " + logicalProblem + ".");
        }
        RequireSafeRelativePath(entry.source, "source");
        if (entry.importer.name.empty() || entry.importer.version.empty() ||
            entry.processor.name.empty() || entry.processor.version.empty() ||
            entry.writer.name.empty() || entry.writer.version.empty())
        {
            throw std::invalid_argument("content manifest component identities must not be empty.");
        }
        if (entry.outputFormat != "cnb" && entry.outputFormat != "xnb")
        {
            throw std::invalid_argument("content manifest output format '" + entry.outputFormat +
                                        "' is not 'cnb' or 'xnb'.");
        }
        const bool xnbEntry = entry.outputFormat == "xnb";
        if (xnbEntry)
        {
            // An .xnb file has no asset type id, schema version or codec identity; its
            // compatibility identity is the root reader each output declares, checked below.
            if (!entry.writerSchemas.empty())
            {
                throw std::invalid_argument(
                    "content manifest xnb node must not declare CNB writer schema identities.");
            }
        }
        else if (entry.writerSchemas.empty() ||
                 entry.writerSchemas.size() > MaxContentBuildOutputs)
        {
            throw std::invalid_argument(
                "content manifest writer must declare between one and " +
                std::to_string(MaxContentBuildOutputs) + " asset/schema/codec identities.");
        }
        std::sort(entry.writerSchemas.begin(), entry.writerSchemas.end(),
                  [](const ContentWriterSchemaIdentity& left,
                     const ContentWriterSchemaIdentity& right)
        {
            if (left.assetTypeId != right.assetTypeId)
            {
                return left.assetTypeId < right.assetTypeId;
            }
            if (left.assetTypeName != right.assetTypeName)
            {
                return left.assetTypeName < right.assetTypeName;
            }
            return left.assetSchemaVersion < right.assetSchemaVersion;
        });
        std::set<std::tuple<std::uint32_t, std::string, std::uint32_t>> writerSchemaKeys;
        for (const ContentWriterSchemaIdentity& schema : entry.writerSchemas)
        {
            if (schema.assetTypeId == 0u || schema.assetSchemaVersion == 0u ||
                schema.assetTypeName.empty() || schema.codec.name.empty() ||
                schema.codec.version.empty())
            {
                throw std::invalid_argument(
                    "content manifest writer schema identities must be complete and nonzero.");
            }
            if (!writerSchemaKeys.emplace(
                    schema.assetTypeId, schema.assetTypeName,
                    schema.assetSchemaVersion).second)
            {
                throw std::invalid_argument(
                    "content manifest repeats writer schema asset/schema identity " +
                    std::to_string(schema.assetTypeId) + " ('" + schema.assetTypeName +
                    "') schema " + std::to_string(schema.assetSchemaVersion) + ".");
            }
        }
        if (entry.outputs.empty() || entry.outputs.size() > MaxContentBuildOutputs)
        {
            throw std::invalid_argument(
                "content manifest node must contain between one and " +
                std::to_string(MaxContentBuildOutputs) + " outputs.");
        }
        if (entry.deploymentFiles.size() > MaxContentDeploymentFiles)
        {
            throw std::invalid_argument(
                "content manifest node exceeds the maximum of " +
                std::to_string(MaxContentDeploymentFiles) + " deployment files.");
        }
        if (!IsLowerHexDigest(entry.directFingerprint) || !IsLowerHexDigest(entry.fingerprint))
        {
            throw std::invalid_argument("content manifest direct and effective fingerprints must "
                                        "be lowercase SHA-256 values.");
        }
        const std::array<const std::string*, 9u> fingerprintDomains = {
            &entry.fingerprintState.primarySourceBytes,
            &entry.fingerprintState.sourceDependencySet,
            &entry.fingerprintState.sourceDependencyBytes,
            &entry.fingerprintState.contentDependencySet,
            &entry.fingerprintState.contentDependencyFingerprints,
            &entry.fingerprintState.processorParameters,
            &entry.fingerprintState.writerSchemas,
            &entry.fingerprintState.outputDefinitions,
            &entry.fingerprintState.deploymentDefinitions,
        };
        if (std::any_of(fingerprintDomains.begin(), fingerprintDomains.end(),
                        [](const std::string* digest)
                        {
                            return !IsLowerHexDigest(*digest);
                        }))
        {
            throw std::invalid_argument(
                "content manifest fingerprint-state domains must be lowercase SHA-256 values.");
        }
        std::set<std::string> outputNames;
        std::set<std::string> outputPaths;
        for (const ContentBuildManifestOutput& output : entry.outputs)
        {
            const std::string problem = Cnb::CnbLogicalNameProblem(output.logicalName);
            if (!problem.empty())
            {
                throw std::invalid_argument("content manifest output logical name '" +
                                            output.logicalName + "' is invalid: " + problem +
                                            ".");
            }
            RequireSafeRelativePath(output.path, "output path");
            if (!outputNames.insert(output.logicalName).second)
            {
                throw std::invalid_argument("content manifest repeats output logical name '" +
                                            output.logicalName + "'.");
            }
            if (!outputPaths.insert(output.path).second)
            {
                throw std::invalid_argument("content manifest repeats output path '" +
                                            output.path + "'.");
            }
            if (xnbEntry)
            {
                if (output.assetTypeId != 0u || output.assetSchemaVersion != 0u ||
                    !output.assetTypeName.empty())
                {
                    throw std::invalid_argument(
                        "content manifest xnb output must not carry CNB asset identities.");
                }
                if (output.rootReaderName.empty())
                {
                    throw std::invalid_argument(
                        "content manifest xnb output '" + output.logicalName +
                        "' must declare the root ContentTypeReader it dispatches to.");
                }
                if (!IsLowerHexDigest(output.sha256))
                {
                    throw std::invalid_argument(
                        "content manifest output digest must be a lowercase SHA-256 value.");
                }
                continue;
            }
            if (!output.rootReaderName.empty())
            {
                throw std::invalid_argument(
                    "content manifest cnb output must not declare a root ContentTypeReader.");
            }
            if (output.assetTypeId == 0u)
            {
                throw std::invalid_argument("content manifest output asset type id must not be "
                                            "zero.");
            }
            if (output.assetSchemaVersion == 0u || output.assetTypeName.empty())
            {
                throw std::invalid_argument(
                    "content manifest output schema version and type name must not be empty.");
            }
            const auto schema = std::find_if(
                entry.writerSchemas.begin(), entry.writerSchemas.end(),
                [&](const ContentWriterSchemaIdentity& candidate)
            {
                return candidate.assetTypeId == output.assetTypeId &&
                       candidate.assetSchemaVersion == output.assetSchemaVersion &&
                       candidate.assetTypeName == output.assetTypeName;
            });
            if (schema == entry.writerSchemas.end())
            {
                throw std::invalid_argument(
                    "content manifest output '" + output.logicalName +
                    "' does not match a declared writer asset/schema identity.");
            }
            if (!IsLowerHexDigest(output.sha256))
            {
                throw std::invalid_argument("content manifest output digest must be a lowercase "
                                            "SHA-256 value.");
            }
        }
        if (!outputNames.contains(entry.nodeId))
        {
            throw std::invalid_argument("content manifest node must own one primary output named '" +
                                        entry.nodeId + "'.");
        }
        for (const ContentBuildManifestDeploymentFile& deployment : entry.deploymentFiles)
        {
            if (!deployment.sourceRoot.empty())
            {
                const std::string problem =
                    ContentSourceRootAliasProblem(deployment.sourceRoot);
                if (!problem.empty())
                {
                    throw std::invalid_argument(
                        "content manifest deployment source-root alias '" +
                        deployment.sourceRoot + "' is invalid: " + problem + ".");
                }
            }
            RequireSafeRelativePath(deployment.source, "deployment source");
            RequireSafeRelativePath(deployment.path, "deployment output path");
            if (!outputPaths.insert(deployment.path).second)
            {
                throw std::invalid_argument(
                    "content manifest repeats compiled/deployment output path '" +
                    deployment.path + "'.");
            }
            if (!IsLowerHexDigest(deployment.sha256))
            {
                throw std::invalid_argument(
                    "content manifest deployment-file digest must be a lowercase SHA-256 value.");
            }
        }
        for (const ContentDependency& dependency : entry.dependencies)
        {
            if (!dependency.sourceRoot.empty())
            {
                const std::string problem =
                    ContentSourceRootAliasProblem(dependency.sourceRoot);
                if (dependency.kind != ContentDependencyKind::SourceFile || !problem.empty())
                {
                    throw std::invalid_argument(
                        "content manifest only permits a valid external source-root alias on a "
                        "source-file dependency.");
                }
            }
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
        for (const ContentBuildManifestDeploymentFile& deployment : entry.deploymentFiles)
        {
            const bool fingerprinted = std::any_of(
                entry.dependencies.begin(), entry.dependencies.end(),
                [&](const ContentDependency& dependency)
                {
                    return dependency.kind != ContentDependencyKind::ContentBuild &&
                           dependency.sourceRoot == deployment.sourceRoot &&
                           dependency.identity == deployment.source;
                });
            if (!fingerprinted)
            {
                throw std::invalid_argument("content manifest deployment source '" +
                                            deployment.source +
                                            "' is not a byte-hashed build dependency.");
            }
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
        std::sort(entry.outputs.begin(), entry.outputs.end(),
                  [](const ContentBuildManifestOutput& left,
                     const ContentBuildManifestOutput& right)
        {
            return left.logicalName < right.logicalName;
        });
        std::sort(entry.deploymentFiles.begin(), entry.deploymentFiles.end(),
                  [](const ContentBuildManifestDeploymentFile& left,
                     const ContentBuildManifestDeploymentFile& right)
        {
            return left.path < right.path;
        });
        entries_.insert_or_assign(entry.nodeId, std::move(entry));
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
        return LowerHex(sha256.ComputeHash(bytes));
    }

    std::string ContentFileSha256(const std::filesystem::path& path)
    {
        return HashFileStreaming(path);
    }

    std::string ComputeContentBuildDirectFingerprint(
        const ContentBuildManifestEntry& entry, const std::filesystem::path& sourceRoot,
        const ContentSourceRootCapabilities& externalSourceRoots)
    {
        const ContentBuildFingerprintState state =
            ComputeDirectFingerprintState(entry, sourceRoot, externalSourceRoots);
        return ComputeDirectFingerprintFromState(entry, state);
    }

    void RefreshContentBuildDirectFingerprint(
        ContentBuildManifestEntry& entry, const std::filesystem::path& sourceRoot,
        const ContentSourceRootCapabilities& externalSourceRoots)
    {
        entry.fingerprintState =
            ComputeDirectFingerprintState(entry, sourceRoot, externalSourceRoots);
        entry.directFingerprint =
            ComputeDirectFingerprintFromState(entry, entry.fingerprintState);
    }

    std::string ComputeContentBuildEffectiveFingerprint(
        const ContentBuildManifestEntry& entry,
        const std::map<std::string, std::string>& contentBuildFingerprints)
    {
        return ComputeEffectiveFingerprintFromState(
            entry, ComputeContentDependencyFingerprint(entry, contentBuildFingerprints));
    }

    void RefreshContentBuildEffectiveFingerprint(
        ContentBuildManifestEntry& entry,
        const std::map<std::string, std::string>& contentBuildFingerprints)
    {
        entry.fingerprintState.contentDependencyFingerprints =
            ComputeContentDependencyFingerprint(entry, contentBuildFingerprints);
        entry.fingerprint = ComputeEffectiveFingerprintFromState(
            entry, entry.fingerprintState.contentDependencyFingerprints);
    }

    std::string ComputeContentBuildFingerprint(
        const ContentBuildManifestEntry& entry, const std::filesystem::path& sourceRoot,
        const std::map<std::string, std::string>& contentBuildFingerprints,
        const ContentSourceRootCapabilities& externalSourceRoots)
    {
        ContentBuildManifestEntry current = entry;
        RefreshContentBuildDirectFingerprint(current, sourceRoot, externalSourceRoots);
        RefreshContentBuildEffectiveFingerprint(current, contentBuildFingerprints);
        return current.fingerprint;
    }

    namespace
    {
        /**
         * @brief Records a build result's deployment files and dependencies on a manifest entry.
         *
         * Shared by both output formats, because neither is a property of the compiled container:
         * a streaming `.ogg` beside a `Song` and the source files a build read are the same
         * whichever serializer produced the asset.
         */
        void AppendDeploymentFiles(ContentBuildManifestEntry& entry,
                                   const ContentBuildResult& result,
                                   const std::filesystem::path& sourceRoot,
                                   const std::filesystem::path& outputRoot,
                                   const ContentSourceRootCapabilities& externalSourceRoots)
        {
        entry.deploymentFiles.reserve(result.deploymentFiles.size());
        for (const ContentDeploymentFile& deployment : result.deploymentFiles)
        {
            const std::filesystem::path& deploymentRoot = SelectSourceRoot(
                sourceRoot, externalSourceRoots, deployment.sourceRoot, "deployment source");
            const std::string source = RelativeContained(
                deploymentRoot, deployment.source, "deployment source");
            const std::filesystem::path destination =
                outputRoot / CNA::Internal::ContentPathFromUtf8(deployment.outputPath);
            const std::string path =
                RelativeContained(outputRoot, destination, "deployment output");
            const std::filesystem::path canonicalSource = ResolveContained(
                deploymentRoot, source, "deployment source");
            const std::filesystem::path canonicalDestination =
                ResolveContained(outputRoot, path, "deployment output");
            bool destinationInsideAuthoredRoot =
                IsWithin(WeaklyCanonical(sourceRoot), canonicalDestination);
            for (const auto& [alias, externalRoot] : externalSourceRoots.Entries())
            {
                static_cast<void>(alias);
                destinationInsideAuthoredRoot =
                    destinationInsideAuthoredRoot ||
                    IsWithin(WeaklyCanonical(externalRoot), canonicalDestination);
            }
            if (destinationInsideAuthoredRoot)
            {
                if (deployment.sourceRoot.empty() &&
                    canonicalSource == canonicalDestination)
                {
                    continue;
                }
                throw std::invalid_argument(
                    "deployment output '" + path +
                    "' is inside the source root and could overwrite authored content; choose a "
                    "separate output root.");
            }
            entry.deploymentFiles.push_back(
                {deployment.sourceRoot, source, path,
                 ContentFileSha256(deployment.source)});
        }
        entry.dependencies.reserve(result.dependencies.size());
        for (const ContentDependency& dependency : result.dependencies)
        {
            ContentDependency normalized = dependency;
            if (dependency.kind != ContentDependencyKind::ContentBuild)
            {
                const std::filesystem::path& dependencyRoot = SelectSourceRoot(
                    sourceRoot, externalSourceRoots, dependency.sourceRoot, "dependency");
                normalized.identity = RelativeContained(
                    dependencyRoot,
                    CNA::Internal::ContentPathFromUtf8(dependency.identity),
                    "dependency");
            }
            entry.dependencies.push_back(std::move(normalized));
        }
        std::sort(entry.dependencies.begin(), entry.dependencies.end());
        }
    }

    ContentBuildManifestEntry MakeContentBuildManifestEntry(const ContentBuildResult& result,
                                                            const std::filesystem::path& sourceRoot,
                                                            const std::filesystem::path& outputRoot,
                                                            const std::filesystem::path& outputPath,
                                                            const ContentSourceRootCapabilities&
                                                                externalSourceRoots)
    {
        ContentBuildManifestEntry entry;
        entry.nodeId = result.logicalName;
        entry.outputFormat = result.outputFormat == ContentOutputFormat::Xnb ? "xnb" : "cnb";
        entry.source = RelativeContained(sourceRoot, result.source, "primary source");
        entry.importer = result.importer;
        entry.processor = result.processor;
        entry.writer = result.writer;
        entry.writerSchemas = result.writerSchemas;
        entry.parameters = result.parameters;
        entry.runtimeReferences = result.runtimeReferences;
        if (result.outputFormat == ContentOutputFormat::Xnb)
        {
            if (result.xnbOutput == nullptr)
            {
                throw std::invalid_argument("xnb build result carries no .xnb output.");
            }
            entry.outputs.reserve(1u + result.xnbOutput->additionalOutputs.size());
            entry.outputs.push_back(ContentBuildManifestOutput{
                .logicalName = result.logicalName,
                .path = RelativeContained(outputRoot, outputPath, "primary output"),
                .rootReaderName = result.xnbOutput->rootReaderName,
                .sha256 = ContentSha256(result.xnbOutput->bytes)});
            for (const XnbAdditionalWriteOutput& output : result.xnbOutput->additionalOutputs)
            {
                std::filesystem::path path =
                    outputRoot / CNA::Internal::ContentPathFromUtf8(output.logicalName);
                path += ".xnb";
                entry.outputs.push_back(ContentBuildManifestOutput{
                    .logicalName = output.logicalName,
                    .path = RelativeContained(outputRoot, path, "additional output"),
                    .rootReaderName = output.rootReaderName,
                    .sha256 = ContentSha256(output.bytes)});
            }
            AppendDeploymentFiles(entry, result, sourceRoot, outputRoot, externalSourceRoots);
            return entry;
        }
        const auto schemaFor = [&](std::uint32_t assetTypeId,
                                   const std::string& assetTypeName,
                                   const std::uint32_t assetSchemaVersion)
            -> const ContentWriterSchemaIdentity&
        {
            const auto found = std::find_if(
                result.writerSchemas.begin(), result.writerSchemas.end(),
                [&](const ContentWriterSchemaIdentity& schema)
            {
                return schema.assetTypeId == assetTypeId &&
                       schema.assetTypeName == assetTypeName &&
                       schema.assetSchemaVersion == assetSchemaVersion;
            });
            if (found == result.writerSchemas.end())
            {
                throw std::invalid_argument(
                    "build result output has no matching writer schema identity.");
            }
            return *found;
        };
        entry.outputs.reserve(1u + result.output.additionalOutputs.size());
        const ContentWriterSchemaIdentity& primarySchema =
            schemaFor(result.output.assetTypeId, result.output.assetTypeName,
                      result.output.assetSchemaVersion);
        entry.outputs.push_back(ContentBuildManifestOutput{
            .logicalName = result.logicalName,
            .path = RelativeContained(outputRoot, outputPath, "primary output"),
            .assetTypeId = result.output.assetTypeId,
            .assetSchemaVersion = primarySchema.assetSchemaVersion,
            .assetTypeName = result.output.assetTypeName,
            .sha256 = ContentSha256(result.output.bytes)});
        for (const ContentAdditionalWriteOutput& output : result.output.additionalOutputs)
        {
            std::filesystem::path path =
                outputRoot / CNA::Internal::ContentPathFromUtf8(output.logicalName);
            path += ".cnb";
            const ContentWriterSchemaIdentity& schema =
                schemaFor(output.assetTypeId, output.assetTypeName,
                          output.assetSchemaVersion);
            entry.outputs.push_back(ContentBuildManifestOutput{
                .logicalName = output.logicalName,
                .path = RelativeContained(outputRoot, path, "additional output"),
                .assetTypeId = output.assetTypeId,
                .assetSchemaVersion = schema.assetSchemaVersion,
                .assetTypeName = output.assetTypeName,
                .sha256 = ContentSha256(output.bytes)});
        }
        AppendDeploymentFiles(entry, result, sourceRoot, outputRoot, externalSourceRoots);
        return entry;
    }
} // namespace CNA::Content::Pipeline
