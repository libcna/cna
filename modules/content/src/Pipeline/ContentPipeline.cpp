// SPDX-License-Identifier: MS-PL

#include "CNA/Content/Pipeline/ContentPipeline.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <exception>
#include <mutex>
#include <sstream>
#include <type_traits>

#include "CNA/Content/Cnb/CnbFormat.hpp"
#include "CNA/Internal/ContentPath.hpp"

namespace CNA::Content::Pipeline
{
    namespace
    {
        class NullContentBuildLogger final : public ContentBuildLogger
        {
        public:
            void Log(const ContentLogMessage&) override {}
        };

        class RecordingContentBuildLogger final : public ContentBuildLogger
        {
        public:
            explicit RecordingContentBuildLogger(ContentBuildLogger& downstream)
                : downstream_(&downstream)
            {
            }

            void Log(const ContentLogMessage& message) override
            {
                messages_.push_back(message);
                downstream_->Log(message);
            }

            [[nodiscard]] std::vector<ContentLogMessage> TakeMessages()
            {
                return std::move(messages_);
            }

        private:
            ContentBuildLogger* downstream_;
            std::vector<ContentLogMessage> messages_;
        };

        ContentBuildLogger& NullLogger()
        {
            static NullContentBuildLogger logger;
            return logger;
        }

        std::filesystem::path WeaklyCanonicalOrAbsolute(const std::filesystem::path& path)
        {
            std::error_code ec;
            std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
            if (!ec) { return canonical; }
            canonical = std::filesystem::absolute(path, ec);
            if (!ec) { return canonical.lexically_normal(); }
            throw std::invalid_argument("cannot resolve path '" +
                                        CNA::Internal::ContentPathToUtf8(path) + "': " +
                                        ec.message() + ".");
        }

        bool PathIsWithin(const std::filesystem::path& root, const std::filesystem::path& path)
        {
            auto rootIt = root.begin();
            auto pathIt = path.begin();
            while (rootIt != root.end() && pathIt != path.end())
            {
                if (*rootIt != *pathIt) { return false; }
                ++rootIt;
                ++pathIt;
            }
            return rootIt == root.end();
        }

        std::filesystem::path RequireContained(const std::filesystem::path& root,
                                               const std::filesystem::path& path,
                                               const char* what)
        {
            const std::filesystem::path canonicalRoot = WeaklyCanonicalOrAbsolute(root);
            const std::filesystem::path canonicalPath = WeaklyCanonicalOrAbsolute(path);
            if (!PathIsWithin(canonicalRoot, canonicalPath))
            {
                throw std::invalid_argument(std::string(what) + " '" +
                                            CNA::Internal::ContentPathToUtf8(path) +
                                            "' resolves outside source root '" +
                                            CNA::Internal::ContentPathToUtf8(canonicalRoot) + "'.");
            }
            return canonicalPath;
        }

        struct ResolvedSourceDependency
        {
            std::filesystem::path path;
            std::string sourceRoot;
        };

        ResolvedSourceDependency ResolveDependency(
            const std::filesystem::path& root,
            const ContentSourceRootCapabilities& externalSourceRoots,
            const std::filesystem::path& source,
            const std::filesystem::path& authored)
        {
            if (authored.empty())
            {
                throw std::invalid_argument("source dependency path must not be empty.");
            }
            if (authored.is_absolute() || authored.has_root_name() || authored.has_root_directory())
            {
                throw std::invalid_argument("source dependency '" +
                                            CNA::Internal::ContentPathToUtf8(authored) +
                                            "' must be relative to its source asset.");
            }
            const auto& nativeAuthored = authored.native();
            const auto isSeparator = [](const auto character)
            {
                using Character = std::decay_t<decltype(character)>;
                return character == static_cast<Character>('/') ||
                       character == static_cast<Character>('\\');
            };
            if (std::adjacent_find(nativeAuthored.begin(), nativeAuthored.end(),
                                   [&](const auto left, const auto right)
                { return isSeparator(left) && isSeparator(right); }) != nativeAuthored.end())
            {
                throw std::invalid_argument(
                    "source dependency paths must use one normalized separator between "
                    "components.");
            }
            const std::string authoredText = CNA::Internal::ContentPathToUtf8(authored);
            if (!authoredText.empty() && authoredText.front() == '@')
            {
                const std::size_t slash = authoredText.find('/');
                if (slash == std::string::npos || slash <= 1u || slash + 1u >= authoredText.size())
                {
                    throw std::invalid_argument(
                        "external source dependency '" + authoredText +
                        "' must use '@alias/root-relative-path'.");
                }
                const std::string alias = authoredText.substr(1u, slash - 1u);
                const std::string problem = ContentSourceRootAliasProblem(alias);
                if (!problem.empty())
                {
                    throw std::invalid_argument("external source dependency alias '" + alias +
                                                "' is invalid: " + problem + ".");
                }
                const std::filesystem::path* externalRoot = externalSourceRoots.Find(alias);
                if (externalRoot == nullptr)
                {
                    throw std::invalid_argument("external source dependency uses unknown root "
                                                "alias '" + alias + "'.");
                }
                const std::string relativeText = authoredText.substr(slash + 1u);
                const std::filesystem::path relative =
                    CNA::Internal::ContentPathFromUtf8(relativeText);
                if (relativeText.find('\\') != std::string::npos || relative.empty() ||
                    relative.is_absolute() || relative.has_root_name() ||
                    relative.has_root_directory() ||
                    CNA::Internal::ContentPathToUtf8(relative.lexically_normal()) != relativeText)
                {
                    throw std::invalid_argument(
                        "external source dependency '" + authoredText +
                        "' must use a normalized root-relative path with '/'.");
                }
                for (const std::filesystem::path& part : relative)
                {
                    if (part == "." || part == "..")
                    {
                        throw std::invalid_argument(
                            "external source dependency '" + authoredText +
                            "' must not contain '.' or '..'.");
                    }
                }
                return {RequireContained(*externalRoot, *externalRoot / relative,
                                         "external source dependency"),
                        alias};
            }
            return {RequireContained(root, source.parent_path() / authored,
                                     "source dependency"),
                    {}};
        }

        std::string LowerExtension(const std::filesystem::path& source)
        {
            std::string extension = source.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return extension;
        }

        void ValidateIdentity(const ContentComponentIdentity& identity, const char* kind)
        {
            if (identity.name.empty())
            {
                throw std::invalid_argument(std::string(kind) +
                                            " component name must not be empty.");
            }
            if (identity.version.empty())
            {
                throw std::invalid_argument(std::string(kind) + " '" + identity.name +
                                            "' version must not be empty.");
            }
        }

        bool WriterSchemaLess(const ContentWriterSchemaIdentity& left,
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
        }

        void ValidateWriterSchemas(const std::vector<ContentWriterSchemaIdentity>& schemas)
        {
            if (schemas.empty())
            {
                throw std::logic_error(
                    "writer returned no asset/schema/codec identity declarations.");
            }
            if (schemas.size() > MaxContentBuildOutputs)
            {
                throw std::logic_error(
                    "writer returned more than the maximum of " +
                    std::to_string(MaxContentBuildOutputs) +
                    " asset/schema/codec identity declarations.");
            }
            for (std::size_t index = 0u; index < schemas.size(); ++index)
            {
                const ContentWriterSchemaIdentity& schema = schemas[index];
                if (schema.assetTypeId == 0u || schema.assetSchemaVersion == 0u ||
                    schema.assetTypeName.empty())
                {
                    throw std::logic_error(
                        "writer returned an incomplete asset/schema identity declaration.");
                }
                ValidateIdentity(schema.codec, "writer codec");
                if (index != 0u && !WriterSchemaLess(schemas[index - 1u], schema))
                {
                    throw std::logic_error(
                        "writer asset/schema identities must be strictly ordered by asset type "
                        "id, canonical type name, and schema version without duplicates.");
                }
            }
        }

        void RequireDeclaredWriterOutput(
            const std::vector<ContentWriterSchemaIdentity>& schemas,
            std::uint32_t assetTypeId, const std::string& assetTypeName,
            const std::uint32_t assetSchemaVersion,
            const std::string& outputName)
        {
            const auto found = std::find_if(
                schemas.begin(), schemas.end(), [&](const ContentWriterSchemaIdentity& schema)
            {
                return schema.assetTypeId == assetTypeId &&
                       schema.assetTypeName == assetTypeName &&
                       schema.assetSchemaVersion == assetSchemaVersion;
            });
            if (found == schemas.end())
            {
                throw std::logic_error(
                    "writer output '" + outputName + "' returned undeclared asset identity " +
                    std::to_string(assetTypeId) + " ('" + assetTypeName + "') schema " +
                    std::to_string(assetSchemaVersion) + ".");
            }
        }

        void ValidateStableType(const std::string& type, const std::string& component,
                                const char* role)
        {
            if (type.empty())
            {
                throw std::invalid_argument("component '" + component + "' " + role +
                                            " type must not be empty.");
            }
        }

        std::string JoinCandidates(const std::set<std::string>& candidates)
        {
            std::string result;
            for (const std::string& candidate : candidates)
            {
                if (!result.empty()) { result += ", "; }
                result += candidate;
            }
            return result;
        }

        template<typename Component, typename RouteKey>
        std::shared_ptr<const Component> ResolveByRoute(
            const std::map<std::string, std::shared_ptr<const Component>>& components,
            const std::map<RouteKey, std::set<std::string>>& routes, const RouteKey& route,
            const std::string& routeText,
            const std::string& explicitName, const char* kind, const char* routeLabel)
        {
            if (!explicitName.empty())
            {
                const auto component = components.find(explicitName);
                if (component == components.end())
                {
                    throw std::logic_error("unknown " + std::string(kind) + " '" + explicitName +
                                           "'.");
                }
                const auto routeIt = routes.find(route);
                if (routeIt == routes.end() || !routeIt->second.contains(explicitName))
                {
                    throw std::logic_error(std::string(kind) + " '" + explicitName +
                                           "' does not accept " + routeLabel + " '" + routeText +
                                           "'.");
                }
                return component->second;
            }

            const auto routeIt = routes.find(route);
            if (routeIt == routes.end() || routeIt->second.empty())
            {
                throw std::logic_error("no " + std::string(kind) + " is registered for " +
                                       routeLabel + " '" + routeText + "'.");
            }
            if (routeIt->second.size() != 1u)
            {
                throw std::logic_error("ambiguous " + std::string(kind) + " for " + routeLabel +
                                       " '" + routeText + "': " + JoinCandidates(routeIt->second) +
                                       ". Select one explicitly.");
            }
            return components.at(*routeIt->second.begin());
        }

        void EmitLog(ContentBuildLogger& logger, ContentLogLevel level,
                     const std::filesystem::path& source, const std::string& logicalName,
                     ContentPipelineStage stage, const std::string& component, std::string text)
        {
            logger.Log(ContentLogMessage{level, source, logicalName, stage, component,
                                         std::move(text)});
        }

        [[noreturn]] void RethrowWithContext(const std::filesystem::path& source,
                                             const std::string& logicalName,
                                             ContentPipelineStage stage,
                                             const std::string& component)
        {
            try
            {
                throw;
            }
            catch (const ContentPipelineError&)
            {
                throw;
            }
            catch (const std::exception& error)
            {
                std::throw_with_nested(
                    ContentPipelineError(source, logicalName, stage, component, error.what()));
            }
            catch (...)
            {
                std::throw_with_nested(ContentPipelineError(
                    source, logicalName, stage, component, "non-standard exception"));
            }
        }
    }

    const char* ContentOutputFormatName(const ContentOutputFormat format) noexcept
    {
        return format == ContentOutputFormat::Xnb ? "xnb" : "cnb";
    }

    const char* ContentOutputFormatExtension(const ContentOutputFormat format) noexcept
    {
        return format == ContentOutputFormat::Xnb ? ".xnb" : ".cnb";
    }

    bool TryParseContentOutputFormat(const std::string& name, ContentOutputFormat& format)
    {
        if (name == "cnb") { format = ContentOutputFormat::Cnb; return true; }
        if (name == "xnb") { format = ContentOutputFormat::Xnb; return true; }
        return false;
    }

    const char* ContentPipelineStageName(ContentPipelineStage stage) noexcept
    {
        switch (stage)
        {
            case ContentPipelineStage::Selection: return "Selection";
            case ContentPipelineStage::Import: return "Import";
            case ContentPipelineStage::Process: return "Process";
            case ContentPipelineStage::Write: return "Write";
            case ContentPipelineStage::Graph: return "Graph";
            case ContentPipelineStage::Publish: return "Publish";
        }
        return "Unknown";
    }

    std::string ContentSourceRootAliasProblem(const std::string& alias)
    {
        if (alias.empty()) { return "must not be empty"; }
        if (alias.size() > 64u) { return "must contain at most 64 characters"; }
        if (alias.front() < 'a' || alias.front() > 'z')
        {
            return "must begin with a lowercase ASCII letter";
        }
        if (std::any_of(alias.begin() + 1, alias.end(), [](const char character)
            {
                return !((character >= 'a' && character <= 'z') ||
                         (character >= '0' && character <= '9') || character == '-');
            }))
        {
            return "must contain only lowercase ASCII letters, digits, or '-'";
        }
        return {};
    }

    void ContentSourceRootCapabilities::Add(std::string alias,
                                            std::filesystem::path root)
    {
        const std::string problem = ContentSourceRootAliasProblem(alias);
        if (!problem.empty())
        {
            throw std::invalid_argument("source-root alias '" + alias + "' " + problem + ".");
        }
        if (root.empty())
        {
            throw std::invalid_argument("source-root alias '" + alias +
                                        "' must name a non-empty physical path.");
        }
        if (entries_.contains(alias))
        {
            throw std::invalid_argument("source-root alias '" + alias + "' is repeated.");
        }
        if (entries_.size() >= MaxContentSourceRoots)
        {
            throw std::invalid_argument(
                "content build exceeds the maximum external source-root count of " +
                std::to_string(MaxContentSourceRoots) + ".");
        }
        entries_.emplace(std::move(alias), std::move(root));
    }

    const std::filesystem::path* ContentSourceRootCapabilities::Find(
        const std::string& alias) const
    {
        const auto found = entries_.find(alias);
        return found == entries_.end() ? nullptr : &found->second;
    }

    const std::map<std::string, std::filesystem::path>&
    ContentSourceRootCapabilities::Entries() const noexcept
    {
        return entries_;
    }

    bool ContentSourceRootCapabilities::Empty() const noexcept
    {
        return entries_.empty();
    }

    ContentSourceRootCapabilities ResolveContentSourceRootCapabilities(
        const std::filesystem::path& sourceRoot,
        const ContentSourceRootCapabilities& configured)
    {
        const std::filesystem::path canonicalSourceRoot =
            WeaklyCanonicalOrAbsolute(sourceRoot);
        if (!std::filesystem::is_directory(canonicalSourceRoot))
        {
            throw std::invalid_argument("primary source root '" +
                                        CNA::Internal::ContentPathToUtf8(sourceRoot) +
                                        "' is not a directory.");
        }
        ContentSourceRootCapabilities resolved;
        for (const auto& [alias, authoredRoot] : configured.Entries())
        {
            const std::filesystem::path candidate = authoredRoot.is_relative()
                                                        ? canonicalSourceRoot / authoredRoot
                                                        : authoredRoot;
            const std::filesystem::path canonicalRoot = WeaklyCanonicalOrAbsolute(candidate);
            if (!std::filesystem::is_directory(canonicalRoot))
            {
                throw std::invalid_argument("external source root '" + alias + "' at '" +
                                            CNA::Internal::ContentPathToUtf8(candidate) +
                                            "' is not a directory.");
            }
            if (PathIsWithin(canonicalSourceRoot, canonicalRoot) ||
                PathIsWithin(canonicalRoot, canonicalSourceRoot))
            {
                throw std::invalid_argument("external source root '" + alias +
                                            "' must not equal, contain, or be contained by the "
                                            "primary source root.");
            }
            for (const auto& [otherAlias, otherRoot] : resolved.Entries())
            {
                if (PathIsWithin(otherRoot, canonicalRoot) ||
                    PathIsWithin(canonicalRoot, otherRoot))
                {
                    throw std::invalid_argument("external source roots '" + otherAlias +
                                                "' and '" + alias +
                                                "' must not equal or contain one another.");
                }
            }
            resolved.Add(alias, canonicalRoot);
        }
        return resolved;
    }

    bool ContentDependency::operator<(const ContentDependency& other) const noexcept
    {
        if (kind != other.kind) { return kind < other.kind; }
        if (sourceRoot != other.sourceRoot) { return sourceRoot < other.sourceRoot; }
        return identity < other.identity;
    }

    bool RuntimeContentReference::operator<(const RuntimeContentReference& other) const noexcept
    {
        if (logicalName != other.logicalName) { return logicalName < other.logicalName; }
        return expectedAssetTypeId < other.expectedAssetTypeId;
    }

    void ContentDependencyCollector::Add(ContentDependency dependency)
    {
        if (dependency.identity.empty())
        {
            throw std::invalid_argument("content dependency identity must not be empty.");
        }
        if (!dependency.sourceRoot.empty())
        {
            const std::string problem = ContentSourceRootAliasProblem(dependency.sourceRoot);
            if (dependency.kind != ContentDependencyKind::SourceFile || !problem.empty())
            {
                throw std::invalid_argument(
                    "only a source-file dependency may carry a valid external source-root "
                    "alias.");
            }
        }
        dependencies_.insert(std::move(dependency));
    }

    void ContentDependencyCollector::AddRuntimeReference(RuntimeContentReference reference)
    {
        const std::string problem = Cnb::CnbLogicalNameProblem(reference.logicalName);
        if (!problem.empty())
        {
            throw std::invalid_argument("runtime content reference '" + reference.logicalName +
                                        "' is invalid: " + problem + ".");
        }
        runtimeReferences_.insert(std::move(reference));
    }

    void ContentDependencyCollector::AddDeploymentFile(ContentDeploymentFile file)
    {
        const auto found = deploymentFiles_.find(file.outputPath);
        if (found != deploymentFiles_.end() && found->second.source != file.source)
        {
            throw std::invalid_argument("deployment output path '" + file.outputPath +
                                        "' is already mapped to another source file.");
        }
        if (found == deploymentFiles_.end() &&
            deploymentFiles_.size() >= MaxContentDeploymentFiles)
        {
            throw std::invalid_argument(
                "content build exceeds the maximum deployment-file count of " +
                std::to_string(MaxContentDeploymentFiles) + ".");
        }
        deploymentFiles_.insert_or_assign(file.outputPath, std::move(file));
    }

    std::vector<ContentDependency> ContentDependencyCollector::Dependencies() const
    {
        return {dependencies_.begin(), dependencies_.end()};
    }

    std::vector<RuntimeContentReference> ContentDependencyCollector::RuntimeReferences() const
    {
        return {runtimeReferences_.begin(), runtimeReferences_.end()};
    }

    std::vector<ContentDeploymentFile> ContentDependencyCollector::DeploymentFiles() const
    {
        std::vector<ContentDeploymentFile> result;
        result.reserve(deploymentFiles_.size());
        for (const auto& [outputPath, file] : deploymentFiles_)
        {
            static_cast<void>(outputPath);
            result.push_back(file);
        }
        return result;
    }

    void ContentProcessorParameters::Set(std::string name,
                                         ContentProcessorParameterValue value)
    {
        if (name.empty())
        {
            throw std::invalid_argument("processor parameter name must not be empty.");
        }
        if (const double* floating = std::get_if<double>(&value);
            floating != nullptr && !std::isfinite(*floating))
        {
            throw std::invalid_argument("processor parameter '" + name +
                                        "' must be finite.");
        }
        values_.insert_or_assign(std::move(name), std::move(value));
    }

    const ContentProcessorParameterValue* ContentProcessorParameters::Find(
        const std::string& name) const
    {
        const auto found = values_.find(name);
        return found == values_.end() ? nullptr : &found->second;
    }

    const std::map<std::string, ContentProcessorParameterValue>&
    ContentProcessorParameters::Values() const noexcept
    {
        return values_;
    }

    bool ContentProcessorParameters::Empty() const noexcept
    {
        return values_.empty();
    }

    const std::string& ContentValue::StableType() const noexcept
    {
        return stableType_;
    }

    bool ContentValue::Empty() const noexcept
    {
        return value_ == nullptr;
    }

    ContentImporterContext::ContentImporterContext(
        std::filesystem::path sourceRoot, std::filesystem::path source, std::string logicalName,
        std::string component, const ContentSourceRootCapabilities& externalSourceRoots,
        ContentDependencyCollector& dependencies, ContentBuildLogger& logger)
        : sourceRoot_(std::move(sourceRoot)), source_(std::move(source)),
          logicalName_(std::move(logicalName)), component_(std::move(component)),
          dependencies_(&dependencies), logger_(&logger),
          externalSourceRoots_(&externalSourceRoots)
    {
    }

    const std::filesystem::path& ContentImporterContext::SourceRoot() const noexcept
    {
        return sourceRoot_;
    }

    const std::filesystem::path& ContentImporterContext::SourcePath() const noexcept
    {
        return source_;
    }

    const std::string& ContentImporterContext::LogicalName() const noexcept
    {
        return logicalName_;
    }

    std::filesystem::path ContentImporterContext::ResolveSourceDependency(
        const std::filesystem::path& authoredPath)
    {
        const ResolvedSourceDependency resolved =
            ResolveDependency(sourceRoot_, *externalSourceRoots_, source_, authoredPath);
        dependencies_->Add(
            ContentDependency{ContentDependencyKind::SourceFile,
                              CNA::Internal::ContentPathToUtf8(resolved.path),
                              resolved.sourceRoot});
        return resolved.path;
    }

    void ContentImporterContext::LogInfo(std::string text) const
    {
        EmitLog(*logger_, ContentLogLevel::Info, source_, logicalName_,
                ContentPipelineStage::Import, component_, std::move(text));
    }

    void ContentImporterContext::LogWarning(std::string text) const
    {
        EmitLog(*logger_, ContentLogLevel::Warning, source_, logicalName_,
                ContentPipelineStage::Import, component_, std::move(text));
    }

    ContentProcessorContext::ContentProcessorContext(
        std::filesystem::path sourceRoot, std::filesystem::path source, std::string logicalName,
        std::string component, const ContentProcessorParameters& parameters,
        const ContentSourceRootCapabilities& externalSourceRoots,
        ContentDependencyCollector& dependencies, ContentBuildLogger& logger)
        : sourceRoot_(std::move(sourceRoot)), source_(std::move(source)),
          logicalName_(std::move(logicalName)), component_(std::move(component)),
          parameters_(&parameters), dependencies_(&dependencies), logger_(&logger),
          externalSourceRoots_(&externalSourceRoots)
    {
    }

    const std::string& ContentProcessorContext::LogicalName() const noexcept
    {
        return logicalName_;
    }

    const ContentProcessorParameters& ContentProcessorContext::Parameters() const noexcept
    {
        return *parameters_;
    }

    std::filesystem::path ContentProcessorContext::ResolveSourceDependency(
        const std::filesystem::path& authoredPath)
    {
        const ResolvedSourceDependency resolved =
            ResolveDependency(sourceRoot_, *externalSourceRoots_, source_, authoredPath);
        dependencies_->Add(
            ContentDependency{ContentDependencyKind::SourceFile,
                              CNA::Internal::ContentPathToUtf8(resolved.path),
                              resolved.sourceRoot});
        return resolved.path;
    }

    void ContentProcessorContext::AddContentBuildDependency(std::string logicalName)
    {
        const std::string problem = Cnb::CnbLogicalNameProblem(logicalName);
        if (!problem.empty())
        {
            throw std::invalid_argument("content build dependency '" + logicalName +
                                        "' is invalid: " + problem + ".");
        }
        dependencies_->Add(
            ContentDependency{ContentDependencyKind::ContentBuild, std::move(logicalName)});
    }

    void ContentProcessorContext::AddGeneratedDependency(
        const std::filesystem::path& generatedPath)
    {
        const std::filesystem::path candidate =
            generatedPath.is_relative() ? sourceRoot_ / generatedPath : generatedPath;
        const std::filesystem::path resolved =
            RequireContained(sourceRoot_, candidate, "generated dependency");
        dependencies_->Add(
            ContentDependency{ContentDependencyKind::Generated,
                              CNA::Internal::ContentPathToUtf8(resolved)});
    }

    void ContentProcessorContext::AddRuntimeReference(std::string logicalName,
                                                       std::uint32_t expectedAssetTypeId)
    {
        dependencies_->AddRuntimeReference(
            RuntimeContentReference{std::move(logicalName), expectedAssetTypeId});
    }

    void ContentProcessorContext::AddDeploymentFile(
        const std::filesystem::path& sourcePath, std::string outputPath)
    {
        const std::string problem = Cnb::CnbLogicalNameProblem(outputPath);
        if (!problem.empty())
        {
            throw std::invalid_argument("deployment output path '" + outputPath +
                                        "' is invalid: " + problem + ".");
        }
        const std::filesystem::path candidate =
            sourcePath.is_relative() ? sourceRoot_ / sourcePath : sourcePath;
        const std::filesystem::path resolved = WeaklyCanonicalOrAbsolute(candidate);
        std::string resolvedSourceRoot;
        if (!PathIsWithin(WeaklyCanonicalOrAbsolute(sourceRoot_), resolved))
        {
            const std::string identity = CNA::Internal::ContentPathToUtf8(resolved);
            const std::vector<ContentDependency> dependencies = dependencies_->Dependencies();
            const auto explicitDependency = std::find_if(
                dependencies.begin(), dependencies.end(), [&](const ContentDependency& dependency)
            {
                return dependency.kind == ContentDependencyKind::SourceFile &&
                       !dependency.sourceRoot.empty() && dependency.identity == identity;
            });
            if (explicitDependency == dependencies.end())
            {
                throw std::invalid_argument(
                    "deployment source '" + CNA::Internal::ContentPathToUtf8(sourcePath) +
                    "' is outside the primary source root and was not explicitly resolved "
                    "through a named external source capability.");
            }
            resolvedSourceRoot = explicitDependency->sourceRoot;
        }
        if (!std::filesystem::is_regular_file(resolved))
        {
            throw std::invalid_argument("deployment source '" +
                                        CNA::Internal::ContentPathToUtf8(sourcePath) +
                                        "' is not a regular file.");
        }
        if (resolved != source_)
        {
            dependencies_->Add(
                ContentDependency{ContentDependencyKind::SourceFile,
                                  CNA::Internal::ContentPathToUtf8(resolved),
                                  resolvedSourceRoot});
        }
        dependencies_->AddDeploymentFile(
            ContentDeploymentFile{resolved, std::move(outputPath),
                                  std::move(resolvedSourceRoot)});
    }

    void ContentProcessorContext::LogInfo(std::string text) const
    {
        EmitLog(*logger_, ContentLogLevel::Info, source_, logicalName_,
                ContentPipelineStage::Process, component_, std::move(text));
    }

    void ContentProcessorContext::LogWarning(std::string text) const
    {
        EmitLog(*logger_, ContentLogLevel::Warning, source_, logicalName_,
                ContentPipelineStage::Process, component_, std::move(text));
    }

    void ContentPipelineRegistry::Freeze() const
    {
        const std::unique_lock lock(configurationMutex_);
        frozen_.store(true, std::memory_order_release);
    }

    bool ContentPipelineRegistry::IsFrozen() const noexcept
    {
        return frozen_.load(std::memory_order_acquire);
    }

    void ContentPipelineRegistry::RequireMutable() const
    {
        if (frozen_.load(std::memory_order_relaxed))
        {
            throw std::logic_error(
                "content pipeline registry is frozen; configure every component before build "
                "execution begins.");
        }
    }

    void ContentPipelineRegistry::RegisterImporter(std::shared_ptr<const ContentImporter> importer)
    {
        const std::unique_lock lock(configurationMutex_);
        RequireMutable();
        if (importer == nullptr)
        {
            throw std::invalid_argument("RegisterImporter(): importer must not be null.");
        }
        const ContentComponentIdentity identity = importer->Identity();
        ValidateIdentity(identity, "importer");
        const std::vector<std::string> outputTypes = importer->OutputTypes();
        if (outputTypes.empty())
        {
            throw std::invalid_argument("importer '" + identity.name +
                                        "' must declare at least one output type.");
        }
        std::set<std::string> uniqueOutputTypes;
        for (const std::string& outputType : outputTypes)
        {
            ValidateStableType(outputType, identity.name, "output");
            if (!uniqueOutputTypes.insert(outputType).second)
            {
                throw std::invalid_argument("importer '" + identity.name +
                                            "' declares duplicate output type '" + outputType +
                                            "'.");
            }
        }
        const std::vector<std::string> extensions = importer->SourceExtensions();
        if (extensions.empty())
        {
            throw std::invalid_argument("importer '" + identity.name +
                                        "' must declare at least one source extension.");
        }
        if (importers_.contains(identity.name))
        {
            throw std::logic_error("importer '" + identity.name + "' is already registered.");
        }
        for (const std::string& extension : extensions)
        {
            if (extension.size() < 2u || extension.front() != '.' ||
                LowerExtension(std::filesystem::path("x" + extension)) != extension)
            {
                throw std::invalid_argument("importer '" + identity.name + "' extension '" +
                                            extension +
                                            "' must be lowercase and include the leading dot.");
            }
        }
        importers_.emplace(identity.name, std::move(importer));
        for (const std::string& extension : extensions)
        {
            importersByExtension_[extension].insert(identity.name);
        }
    }

    void ContentPipelineRegistry::RegisterProcessor(
        std::shared_ptr<const ContentProcessor> processor)
    {
        const std::unique_lock lock(configurationMutex_);
        RequireMutable();
        if (processor == nullptr)
        {
            throw std::invalid_argument("RegisterProcessor(): processor must not be null.");
        }
        const ContentComponentIdentity identity = processor->Identity();
        ValidateIdentity(identity, "processor");
        ValidateStableType(processor->InputType(), identity.name, "input");
        ValidateStableType(processor->OutputType(), identity.name, "output");
        if (processors_.contains(identity.name))
        {
            throw std::logic_error("processor '" + identity.name + "' is already registered.");
        }
        processorsByInputType_[processor->InputType()].insert(identity.name);
        processors_.emplace(identity.name, std::move(processor));
    }

    void ContentPipelineRegistry::RegisterWriter(std::shared_ptr<const ContentTypeWriter> writer)
    {
        const std::unique_lock lock(configurationMutex_);
        RequireMutable();
        if (writer == nullptr)
        {
            throw std::invalid_argument("RegisterWriter(): writer must not be null.");
        }
        const ContentComponentIdentity identity = writer->Identity();
        ValidateIdentity(identity, "writer");
        ValidateStableType(writer->InputType(), identity.name, "input");
        if (writers_.contains(identity.name))
        {
            throw std::logic_error("writer '" + identity.name + "' is already registered.");
        }
        writersByInputType_[{writer->OutputFormat(), writer->InputType()}].insert(identity.name);
        writers_.emplace(identity.name, std::move(writer));
    }

    std::shared_ptr<const ContentImporter> ContentPipelineRegistry::ResolveImporter(
        const std::filesystem::path& source, const std::string& explicitName) const
    {
        const std::shared_lock lock(configurationMutex_);
        const std::string extension = LowerExtension(source);
        return ResolveByRoute(importers_, importersByExtension_, extension, extension,
                              explicitName, "importer", "source extension");
    }

    bool ContentPipelineRegistry::HasImporterForSource(
        const std::filesystem::path& source) const
    {
        const std::shared_lock lock(configurationMutex_);
        const auto route = importersByExtension_.find(LowerExtension(source));
        return route != importersByExtension_.end() && !route->second.empty();
    }

    std::shared_ptr<const ContentProcessor> ContentPipelineRegistry::ResolveProcessor(
        const std::string& inputType, const std::string& explicitName) const
    {
        const std::shared_lock lock(configurationMutex_);
        return ResolveByRoute(processors_, processorsByInputType_, inputType, inputType,
                              explicitName, "processor", "imported type");
    }

    std::shared_ptr<const ContentTypeWriter> ContentPipelineRegistry::ResolveWriter(
        const std::string& inputType, const std::string& explicitName,
        const ContentOutputFormat format) const
    {
        const std::shared_lock lock(configurationMutex_);
        const std::pair<ContentOutputFormat, std::string> route{format, inputType};
        const std::string routeText =
            std::string(ContentOutputFormatName(format)) + " output of processed type '" +
            inputType + "'";
        return ResolveByRoute(writers_, writersByInputType_, route, routeText, explicitName,
                              "writer", "the");
    }

    namespace
    {
        std::string BuildErrorMessage(const std::filesystem::path& source,
                                      const std::string& logicalName,
                                      ContentPipelineStage stage,
                                      const std::string& component,
                                      const std::string& reason)
        {
            std::ostringstream message;
            message << CNA::Internal::ContentPathToUtf8(source);
            if (!logicalName.empty()) { message << " [" << logicalName << ']'; }
            message << "\n  " << ContentPipelineStageName(stage);
            if (!component.empty()) { message << " (" << component << ')'; }
            message << ": " << reason;
            return message.str();
        }
    }

    ContentPipelineError::ContentPipelineError(std::filesystem::path source,
                                               std::string logicalName,
                                               ContentPipelineStage stage,
                                               std::string component, std::string reason)
        : std::runtime_error(BuildErrorMessage(source, logicalName, stage, component, reason)),
          source_(std::move(source)), logicalName_(std::move(logicalName)), stage_(stage),
          component_(std::move(component))
    {
    }

    const std::filesystem::path& ContentPipelineError::Source() const noexcept
    {
        return source_;
    }

    const std::string& ContentPipelineError::LogicalName() const noexcept
    {
        return logicalName_;
    }

    ContentPipelineStage ContentPipelineError::Stage() const noexcept
    {
        return stage_;
    }

    const std::string& ContentPipelineError::Component() const noexcept
    {
        return component_;
    }

    ContentPipeline::ContentPipeline(std::shared_ptr<const ContentPipelineRegistry> registry)
        : registry_(std::move(registry))
    {
        if (registry_ == nullptr)
        {
            throw std::invalid_argument("ContentPipeline(): registry must not be null.");
        }
        registry_->Freeze();
    }

    ContentBuildResult ContentPipeline::Build(const ContentBuildRequest& request) const
    {
        std::filesystem::path source = request.source;
        std::filesystem::path root = request.sourceRoot;
        const std::string logicalName = request.logicalName;
        ContentSourceRootCapabilities externalSourceRoots;
        ContentBuildLogger& downstreamLogger =
            request.logger == nullptr ? NullLogger() : *request.logger;
        RecordingContentBuildLogger logger(downstreamLogger);

        try
        {
            if (root.empty()) { throw std::invalid_argument("sourceRoot must not be empty."); }
            root = WeaklyCanonicalOrAbsolute(root);
            externalSourceRoots =
                ResolveContentSourceRootCapabilities(root, request.externalSourceRoots);
            if (source.empty()) { throw std::invalid_argument("source must not be empty."); }
            if (source.is_relative()) { source = root / source; }
            source = RequireContained(root, source, "primary source");
            if (!std::filesystem::is_regular_file(source))
            {
                throw std::invalid_argument("primary source '" +
                                            CNA::Internal::ContentPathToUtf8(source) +
                                            "' is not a regular file.");
            }
            const std::string logicalProblem = Cnb::CnbLogicalNameProblem(logicalName);
            if (!logicalProblem.empty())
            {
                throw std::invalid_argument("logical content name '" + logicalName +
                                            "' is invalid: " + logicalProblem + ".");
            }
        }
        catch (...)
        {
            RethrowWithContext(source, logicalName, ContentPipelineStage::Selection, {});
        }

        std::shared_ptr<const ContentImporter> importer;
        try
        {
            importer = registry_->ResolveImporter(source, request.importer);
        }
        catch (...)
        {
            RethrowWithContext(source, logicalName, ContentPipelineStage::Selection, {});
        }

        ContentDependencyCollector dependencies;
        dependencies.Add(ContentDependency{ContentDependencyKind::PrimarySource,
                                           CNA::Internal::ContentPathToUtf8(source)});

        ContentValue imported;
        const ContentComponentIdentity importerIdentity = importer->Identity();
        try
        {
            ContentImporterContext context(root, source, logicalName, importerIdentity.name,
                                           externalSourceRoots, dependencies, logger);
            imported = importer->Import(context);
            if (imported.Empty())
            {
                throw std::logic_error("importer returned an empty value.");
            }
            const std::vector<std::string> outputTypes = importer->OutputTypes();
            if (std::find(outputTypes.begin(), outputTypes.end(), imported.StableType()) ==
                outputTypes.end())
            {
                throw std::logic_error("importer returned undeclared output type '" +
                                       imported.StableType() + "'.");
            }
        }
        catch (...)
        {
            RethrowWithContext(source, logicalName, ContentPipelineStage::Import,
                               importerIdentity.name);
        }

        std::shared_ptr<const ContentProcessor> processor;
        try
        {
            processor = registry_->ResolveProcessor(imported.StableType(), request.processor);
        }
        catch (...)
        {
            RethrowWithContext(source, logicalName, ContentPipelineStage::Selection, {});
        }

        ContentValue processed;
        const ContentComponentIdentity processorIdentity = processor->Identity();
        try
        {
            processor->ValidateParameters(request.parameters);
            ContentProcessorContext context(root, source, logicalName, processorIdentity.name,
                                            request.parameters, externalSourceRoots,
                                            dependencies, logger);
            processed = processor->Process(imported, context);
            if (processed.Empty())
            {
                throw std::logic_error("processor returned an empty value.");
            }
            if (processed.StableType() != processor->OutputType())
            {
                throw std::logic_error("processor declared output type '" +
                                       processor->OutputType() + "' but returned '" +
                                       processed.StableType() + "'.");
            }
        }
        catch (...)
        {
            RethrowWithContext(source, logicalName, ContentPipelineStage::Process,
                               processorIdentity.name);
        }

        std::shared_ptr<const ContentTypeWriter> writer;
        try
        {
            writer = registry_->ResolveWriter(processed.StableType(), request.writer,
                                              request.outputFormat);
        }
        catch (...)
        {
            RethrowWithContext(source, logicalName, ContentPipelineStage::Selection, {});
        }

        ContentWriteResult output;
        std::vector<ContentWriterSchemaIdentity> writerSchemas;
        const ContentComponentIdentity writerIdentity = writer->Identity();
        try
        {
            writerSchemas = writer->OutputSchemaIdentities();
            ValidateWriterSchemas(writerSchemas);
            output = writer->Write(processed, logicalName);
            if (output.bytes.empty())
            {
                throw std::logic_error("writer returned an empty file image.");
            }
            if (output.assetTypeId == 0u)
            {
                throw std::logic_error("writer returned invalid asset type id 0.");
            }
            if (output.assetSchemaVersion == 0u)
            {
                throw std::logic_error("writer returned invalid asset schema version 0.");
            }
            RequireDeclaredWriterOutput(writerSchemas, output.assetTypeId,
                                        output.assetTypeName, output.assetSchemaVersion,
                                        logicalName);
            for (const std::string& warning : output.warnings)
            {
                EmitLog(logger, ContentLogLevel::Warning, source, logicalName,
                        ContentPipelineStage::Write, writerIdentity.name, warning);
            }
            if (output.additionalOutputs.size() >= MaxContentBuildOutputs)
            {
                throw std::logic_error(
                    "writer returned more than the maximum of " +
                    std::to_string(MaxContentBuildOutputs) + " outputs.");
            }
            std::set<std::string> outputNames{logicalName};
            for (const ContentAdditionalWriteOutput& additional : output.additionalOutputs)
            {
                const std::string problem = Cnb::CnbLogicalNameProblem(additional.logicalName);
                if (!problem.empty())
                {
                    throw std::logic_error("writer returned invalid additional logical name '" +
                                           additional.logicalName + "': " + problem + ".");
                }
                if (!outputNames.insert(additional.logicalName).second)
                {
                    throw std::logic_error("writer returned duplicate output logical name '" +
                                           additional.logicalName + "'.");
                }
                if (additional.bytes.empty())
                {
                    throw std::logic_error("writer returned an empty file image for additional "
                                           "output '" +
                                           additional.logicalName + "'.");
                }
                if (additional.assetTypeId == 0u)
                {
                    throw std::logic_error("writer returned invalid asset type id 0 for "
                                           "additional output '" +
                                           additional.logicalName + "'.");
                }
                if (additional.assetSchemaVersion == 0u)
                {
                    throw std::logic_error(
                        "writer returned invalid asset schema version 0 for additional output '" +
                        additional.logicalName + "'.");
                }
                RequireDeclaredWriterOutput(writerSchemas, additional.assetTypeId,
                                            additional.assetTypeName,
                                            additional.assetSchemaVersion,
                                            additional.logicalName);
            }
        }
        catch (...)
        {
            RethrowWithContext(source, logicalName, ContentPipelineStage::Write,
                               writerIdentity.name);
        }

        ContentBuildResult result;
        result.source = std::move(source);
        result.logicalName = logicalName;
        result.importer = importerIdentity;
        result.processor = processorIdentity;
        result.writer = writerIdentity;
        result.outputFormat = writer->OutputFormat();
        result.writerSchemas = std::move(writerSchemas);
        result.parameters = request.parameters;
        result.dependencies = dependencies.Dependencies();
        result.runtimeReferences = dependencies.RuntimeReferences();
        result.deploymentFiles = dependencies.DeploymentFiles();
        result.messages = logger.TakeMessages();
        result.output = std::move(output);
        return result;
    }
}
