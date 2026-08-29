// SPDX-License-Identifier: MS-PL

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <numeric>
#include <sstream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "CNA/Content/Pipeline/ContentBuildManifest.hpp"
#include "CNA/Content/Pipeline/ContentBuildConfiguration.hpp"
#include "CNA/Content/Pipeline/CnjContentPipeline.hpp"
#include "CNA/Content/Pipeline/ContentCompiler.hpp"
#include "CNA/Content/Pipeline/ContentPipeline.hpp"
#include "CNA/Content/Pipeline/ModelContentPipeline.hpp"
#include "CNA/Content/Pipeline/SongContentPipeline.hpp"
#include "CNA/Content/Pipeline/SoundEffectContentPipeline.hpp"
#include "CNA/Content/Pipeline/Texture2DContentPipeline.hpp"
#include "CNA/Content/Pipeline/VideoContentPipeline.hpp"
#include "CNA/Content/Pipeline/XnbContentPipeline.hpp"
#include "CNA/Internal/ContentPath.hpp"
#include "CnaContentStaging.hpp"
#include "CnaToolAtomicWrite.hpp"

namespace Pipeline = CNA::Content::Pipeline;

namespace
{
    enum class ContentCommand
    {
        Build,
        Clean,
    };

    struct CommandLine
    {
        ContentCommand operation = ContentCommand::Build;
        std::filesystem::path source;
        std::filesystem::path output;
        std::filesystem::path configuration;
        std::size_t workers = 1u;
        bool quiet = false;
    };

    constexpr std::size_t MaxContentCompilerWorkers = 64u;

    struct BuildItem
    {
        std::filesystem::path source;
        std::filesystem::path output;
        std::string relativeSource;
        std::string logicalName;
        std::string importer;
        std::string processor;
        std::string writer;
        Pipeline::ContentProcessorParameters parameters;
    };

    class ContentBuildCycleError final : public Pipeline::ContentPipelineError
    {
    public:
        ContentBuildCycleError(const BuildItem& item, const std::string& reason)
            : ContentPipelineError(item.source, item.logicalName,
                                   Pipeline::ContentPipelineStage::Graph,
                                   "CNA.ContentBuildGraph", reason)
        {
        }
    };

    void PrintUsage()
    {
        std::cerr
            << "Usage: cna-content build <source-file-or-directory> -o <output> "
               "[--config <file>] [--workers <1..64>] [--quiet]\n"
            << "       cna-content clean <output-directory> [--quiet]\n\n"
            << "Builds source content through Importer -> Processor -> Content Type Writer -> "
               "CNB.\n"
            << "A source file requires an output .cnb path. A source directory requires an "
               "output\n"
            << "directory; relative paths and logical content names are preserved. Clean removes "
               "only\n"
            << "unchanged files proven to be pipeline-owned by a valid output manifest.\n";
    }

    std::size_t ParseWorkerCount(const std::filesystem::path& argument)
    {
        const std::string text = CNA::Internal::ContentPathToUtf8(argument);
        std::uint64_t value = 0u;
        const auto [end, error] =
            std::from_chars(text.data(), text.data() + text.size(), value, 10);
        if (text.empty() || error != std::errc{} || end != text.data() + text.size() ||
            value == 0u || value > MaxContentCompilerWorkers)
        {
            throw std::invalid_argument("--workers must be an integer between 1 and " +
                                        std::to_string(MaxContentCompilerWorkers) + ".");
        }
        return static_cast<std::size_t>(value);
    }

    bool IsOption(const std::filesystem::path& argument, const char* spelling)
    {
        return argument == std::filesystem::path(spelling);
    }

    CommandLine ParseCommandLine(const std::vector<std::filesystem::path>& arguments)
    {
        if (arguments.empty() ||
            (!IsOption(arguments[0], "build") && !IsOption(arguments[0], "clean")))
        {
            throw std::invalid_argument("the first argument must be 'build' or 'clean'.");
        }

        CommandLine command;
        if (IsOption(arguments[0], "clean"))
        {
            command.operation = ContentCommand::Clean;
            for (std::size_t index = 1u; index < arguments.size(); ++index)
            {
                const std::filesystem::path& argument = arguments[index];
                if (IsOption(argument, "--quiet"))
                {
                    command.quiet = true;
                }
                else if (!argument.empty() && argument.native().front() ==
                                                  std::filesystem::path("-").native().front())
                {
                    throw std::invalid_argument("unknown clean option '" +
                                                CNA::Internal::ContentPathToUtf8(argument) +
                                                "'.");
                }
                else if (command.output.empty())
                {
                    command.output = argument;
                }
                else
                {
                    throw std::invalid_argument(
                        "more than one clean output directory was provided.");
                }
            }
            if (command.output.empty())
            {
                throw std::invalid_argument("clean requires an output directory.");
            }
            return command;
        }

        bool workersSpecified = false;
        for (std::size_t index = 1u; index < arguments.size(); ++index)
        {
            const std::filesystem::path& argument = arguments[index];
            if (IsOption(argument, "-o") || IsOption(argument, "--output"))
            {
                if (++index >= arguments.size())
                {
                    throw std::invalid_argument("-o/--output requires a path.");
                }
                if (!command.output.empty())
                {
                    throw std::invalid_argument("the output path was specified more than once.");
                }
                command.output = arguments[index];
            }
            else if (IsOption(argument, "--quiet"))
            {
                command.quiet = true;
            }
            else if (IsOption(argument, "--config"))
            {
                if (++index >= arguments.size())
                {
                    throw std::invalid_argument("--config requires a path.");
                }
                if (!command.configuration.empty())
                {
                    throw std::invalid_argument("--config was specified more than once.");
                }
                command.configuration = arguments[index];
            }
            else if (IsOption(argument, "--workers"))
            {
                if (++index >= arguments.size())
                {
                    throw std::invalid_argument("--workers requires a count.");
                }
                if (workersSpecified)
                {
                    throw std::invalid_argument("--workers was specified more than once.");
                }
                command.workers = ParseWorkerCount(arguments[index]);
                workersSpecified = true;
            }
            else if (!argument.empty() && argument.native().front() ==
                                              std::filesystem::path("-").native().front())
            {
                throw std::invalid_argument("unknown option '" +
                                            CNA::Internal::ContentPathToUtf8(argument) + "'.");
            }
            else if (command.source.empty())
            {
                command.source = argument;
            }
            else
            {
                throw std::invalid_argument("more than one source path was provided.");
            }
        }
        if (command.source.empty()) { throw std::invalid_argument("a source path is required."); }
        if (command.output.empty()) { throw std::invalid_argument("-o/--output is required."); }
        return command;
    }

    std::filesystem::path WeaklyCanonical(const std::filesystem::path& path)
    {
        std::error_code error;
        std::filesystem::path result = std::filesystem::weakly_canonical(path, error);
        if (error)
        {
            throw std::runtime_error("cannot resolve path '" +
                                     CNA::Internal::ContentPathToUtf8(path) + "': " +
                                     error.message() + ".");
        }
        return result;
    }

    bool IsWithin(const std::filesystem::path& root, const std::filesystem::path& path)
    {
        auto rootPart = root.begin();
        auto pathPart = path.begin();
        while (rootPart != root.end() && pathPart != path.end())
        {
            if (*rootPart != *pathPart) { return false; }
            ++rootPart;
            ++pathPart;
        }
        return rootPart == root.end();
    }

    void AcquireOutputLease(
        const std::filesystem::path& outputRoot,
        CNA::Tools::ContentStagingDetail::LeaseHandle& lease)
    {
        std::error_code error;
        std::filesystem::create_directories(outputRoot, error);
        if (error)
        {
            throw std::runtime_error("cannot create the content output root: " +
                                     error.message() + ".");
        }

        const std::filesystem::path path =
            outputRoot / CNA::Tools::ContentOutputLeaseFile;
        const std::filesystem::file_status status =
            std::filesystem::symlink_status(path, error);
        if (error && error != std::errc::no_such_file_or_directory)
        {
            throw std::runtime_error("cannot inspect the content output lease: " +
                                     error.message() + ".");
        }

        std::string reason;
        if (!error && status.type() != std::filesystem::file_type::not_found)
        {
            const auto result = lease.ClaimExisting(path, reason);
            if (result ==
                CNA::Tools::ContentStagingDetail::LeaseHandle::ClaimResult::Claimed)
            {
                return;
            }
            if (result ==
                CNA::Tools::ContentStagingDetail::LeaseHandle::ClaimResult::Active)
            {
                throw std::runtime_error(
                    "another content build or clean operation is active for output root '" +
                    CNA::Internal::ContentPathToUtf8(outputRoot) + "'.");
            }
            throw std::runtime_error("the content output lease is unsafe: " + reason + ".");
        }

        error.clear();
        if (lease.CreateAndHold(path, reason)) { return; }

        const auto raced = lease.ClaimExisting(path, reason);
        if (raced == CNA::Tools::ContentStagingDetail::LeaseHandle::ClaimResult::Active)
        {
            throw std::runtime_error(
                "another content build or clean operation is active for output root '" +
                CNA::Internal::ContentPathToUtf8(outputRoot) + "'.");
        }
        if (raced == CNA::Tools::ContentStagingDetail::LeaseHandle::ClaimResult::Claimed)
        {
            return;
        }
        throw std::runtime_error("cannot establish the content output lease: " + reason + ".");
    }

    std::string LogicalName(const std::filesystem::path& relativeSource)
    {
        std::filesystem::path withoutExtension = relativeSource;
        withoutExtension.replace_extension();
        return CNA::Internal::ContentPathToUtf8(withoutExtension);
    }

    std::vector<BuildItem> DiscoverBuilds(const CommandLine& command,
                                          const Pipeline::ContentPipelineRegistry& registry,
                                          std::filesystem::path& sourceRoot,
                                          std::filesystem::path& outputRoot,
                                          bool& directoryBuild)
    {
        const std::filesystem::path source = WeaklyCanonical(command.source);
        if (std::filesystem::is_regular_file(source))
        {
            directoryBuild = false;
            if (command.output.extension() != ".cnb")
            {
                throw std::invalid_argument(
                    "a single source file requires an output path ending in '.cnb'.");
            }
            const std::filesystem::path output = WeaklyCanonical(command.output);
            if (output == source)
            {
                throw std::invalid_argument("the output path must not replace the source file.");
            }
            sourceRoot = source.parent_path();
            outputRoot = output.parent_path();
            return {{source, output, CNA::Internal::ContentPathToUtf8(source.filename()),
                     LogicalName(source.filename())}};
        }
        if (!std::filesystem::is_directory(source))
        {
            throw std::invalid_argument("source '" +
                                        CNA::Internal::ContentPathToUtf8(source) +
                                        "' is neither a regular file nor a directory.");
        }

        directoryBuild = true;
        sourceRoot = source;
        outputRoot = WeaklyCanonical(command.output);
        if (IsWithin(sourceRoot, outputRoot))
        {
            throw std::invalid_argument(
                "a directory build's output must not be inside its source root.");
        }

        std::vector<BuildItem> builds;
        for (const std::filesystem::directory_entry& entry :
             std::filesystem::recursive_directory_iterator(sourceRoot))
        {
            if (!entry.is_regular_file()) { continue; }
            if (!registry.HasImporterForSource(entry.path())) { continue; }
            const std::filesystem::path relative =
                std::filesystem::relative(entry.path(), sourceRoot);
            std::filesystem::path output = outputRoot / relative;
            output.replace_extension(".cnb");
            builds.push_back({entry.path(), std::move(output),
                              CNA::Internal::ContentPathToUtf8(relative), LogicalName(relative)});
        }
        std::sort(builds.begin(), builds.end(), [](const BuildItem& left, const BuildItem& right)
        {
            return left.logicalName < right.logicalName;
        });
        return builds;
    }

    std::string ReadText(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            throw std::runtime_error("cannot open '" +
                                     CNA::Internal::ContentPathToUtf8(path) + "'.");
        }
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }

    Pipeline::ContentBuildConfiguration LoadConfiguration(
        const CommandLine& command, const std::filesystem::path& sourceRoot)
    {
        const bool explicitPath = !command.configuration.empty();
        const std::filesystem::path authored =
            explicitPath ? command.configuration
                         : sourceRoot / Pipeline::ContentBuildConfigurationFileName;
        std::error_code existsError;
        if (!explicitPath && !std::filesystem::exists(authored, existsError))
        {
            if (existsError)
            {
                throw std::runtime_error(
                    "cannot inspect default content configuration '" +
                    CNA::Internal::ContentPathToUtf8(authored) + "': " +
                    existsError.message() + ".");
            }
            return {};
        }

        const std::filesystem::path path = WeaklyCanonical(authored);
        if (!IsWithin(WeaklyCanonical(sourceRoot), path))
        {
            throw std::runtime_error("content configuration '" +
                                     CNA::Internal::ContentPathToUtf8(path) +
                                     "' must remain inside source root '" +
                                     CNA::Internal::ContentPathToUtf8(sourceRoot) + "'.");
        }
        if (!std::filesystem::is_regular_file(path))
        {
            throw std::runtime_error("content configuration '" +
                                     CNA::Internal::ContentPathToUtf8(path) +
                                     "' is not a regular file.");
        }
        return Pipeline::ContentBuildConfiguration::Parse(ReadText(path), path);
    }

    void ApplyConfiguration(std::vector<BuildItem>& builds,
                            const Pipeline::ContentBuildConfiguration& configuration,
                            const Pipeline::ContentPipelineRegistry& registry,
                            const std::filesystem::path& sourceRoot,
                            const std::filesystem::path& outputRoot, bool directoryBuild)
    {
        for (const auto& [source, entry] : configuration.Entries())
        {
            const std::filesystem::path path = WeaklyCanonical(
                sourceRoot / CNA::Internal::ContentPathFromUtf8(source));
            if (!IsWithin(WeaklyCanonical(sourceRoot), path) ||
                !std::filesystem::is_regular_file(path))
            {
                throw std::runtime_error("content configuration asset '" + source +
                                         "' does not name a contained regular source file.");
            }
            if (!registry.HasImporterForSource(path))
            {
                throw std::runtime_error("content configuration asset '" + source +
                                         "' has no registered importer for its source extension.");
            }
            static_cast<void>(entry);
        }

        std::map<std::string, std::string> logicalOwners;
        std::map<std::string, std::string> outputOwners;
        for (BuildItem& item : builds)
        {
            if (const Pipeline::ContentAssetBuildConfiguration* entry =
                    configuration.Find(item.relativeSource))
            {
                if (!entry->logicalName.empty()) { item.logicalName = entry->logicalName; }
                item.importer = entry->importer;
                item.processor = entry->processor;
                item.writer = entry->writer;
                item.parameters = entry->parameters;
                if (directoryBuild && !entry->logicalName.empty())
                {
                    item.output = outputRoot /
                                  CNA::Internal::ContentPathFromUtf8(item.logicalName);
                    item.output += ".cnb";
                }
            }

            if (!logicalOwners.emplace(item.logicalName, item.relativeSource).second)
            {
                throw std::runtime_error("content assets '" +
                                         logicalOwners.at(item.logicalName) + "' and '" +
                                         item.relativeSource + "' both resolve to logical name '" +
                                         item.logicalName + "'.");
            }
            const std::filesystem::path canonicalOutput = WeaklyCanonical(item.output);
            if (!IsWithin(WeaklyCanonical(outputRoot), canonicalOutput))
            {
                throw std::runtime_error("configured output for asset '" + item.relativeSource +
                                         "' escapes the output root.");
            }
            const std::string outputIdentity =
                CNA::Internal::ContentPathToUtf8(canonicalOutput);
            if (!outputOwners.emplace(outputIdentity, item.relativeSource).second)
            {
                throw std::runtime_error("content assets '" + outputOwners.at(outputIdentity) +
                                         "' and '" + item.relativeSource +
                                         "' resolve to the same output path.");
            }
        }
    }

    Pipeline::ContentBuildManifest LoadManifest(const std::filesystem::path& path,
                                                std::string& original,
                                                bool quiet,
                                                bool& ownershipTrusted)
    {
        ownershipTrusted = false;
        try
        {
            if (!std::filesystem::exists(path)) { return {}; }
            original = ReadText(path);
            Pipeline::ContentBuildManifest manifest =
                Pipeline::ContentBuildManifest::Parse(original);
            ownershipTrusted = true;
            return manifest;
        }
        catch (const std::exception& error)
        {
            original.clear();
            if (!quiet)
            {
                std::cout << "[WARN] Ignoring incompatible or corrupt manifest '"
                          << CNA::Internal::ContentPathToUtf8(path) << "': " << error.what()
                          << "\n";
            }
            return {};
        }
    }

    std::map<std::string, std::string> OwnedOutputDigests(
        const Pipeline::ContentBuildManifest& manifest)
    {
        std::map<std::string, std::string> result;
        for (const auto& [nodeId, entry] : manifest.Entries())
        {
            for (const Pipeline::ContentBuildManifestOutput& output : entry.outputs)
            {
                const auto [found, inserted] = result.emplace(output.path, output.sha256);
                if (!inserted && found->second != output.sha256)
                {
                    throw std::runtime_error(
                        "content manifest has conflicting ownership records for output '" +
                        output.path + "'.");
                }
            }
            for (const Pipeline::ContentBuildManifestDeploymentFile& deployment :
                 entry.deploymentFiles)
            {
                const auto [found, inserted] =
                    result.emplace(deployment.path, deployment.sha256);
                if (!inserted && found->second != deployment.sha256)
                {
                    throw std::runtime_error(
                        "content manifest has conflicting ownership records for output '" +
                        deployment.path + "'.");
                }
            }
            static_cast<void>(nodeId);
        }
        return result;
    }

    std::filesystem::file_status SymlinkStatus(const std::filesystem::path& path)
    {
        std::error_code error;
        const std::filesystem::file_status status =
            std::filesystem::symlink_status(path, error);
        if (error && error != std::errc::no_such_file_or_directory)
        {
            throw std::runtime_error("cannot inspect manifest-owned output '" +
                                     CNA::Internal::ContentPathToUtf8(path) + "': " +
                                     error.message() + ".");
        }
        return status;
    }

    void RequireUnlinkedOutputParents(const std::filesystem::path& outputRoot,
                                      const std::filesystem::path& relative)
    {
        std::filesystem::path parent = outputRoot;
        for (const std::filesystem::path& component : relative.parent_path())
        {
            parent /= component;
            const std::filesystem::file_status status = SymlinkStatus(parent);
            if (status.type() == std::filesystem::file_type::not_found) { return; }
            if (std::filesystem::is_symlink(status) || !std::filesystem::is_directory(status))
            {
                throw std::runtime_error(
                    "refusing to collect manifest-owned output '" +
                    CNA::Internal::ContentPathToUtf8(relative) +
                    "' because an output-path parent is not a real directory.");
            }
        }
    }

    std::size_t CollectObsoleteOwnedOutputs(
        const Pipeline::ContentBuildManifest& previousManifest,
        const Pipeline::ContentBuildManifest& nextManifest,
        const std::filesystem::path& outputRoot, bool quiet,
        const char* deletionReason)
    {
        const std::map<std::string, std::string> previous =
            OwnedOutputDigests(previousManifest);
        const std::map<std::string, std::string> current =
            OwnedOutputDigests(nextManifest);
        const std::filesystem::path canonicalRoot = WeaklyCanonical(outputRoot);
        std::vector<std::pair<std::filesystem::path, std::string>> obsolete;
        for (const auto& [relativeText, sha256] : previous)
        {
            if (current.contains(relativeText)) { continue; }

            const std::filesystem::path relative =
                CNA::Internal::ContentPathFromUtf8(relativeText);
            RequireUnlinkedOutputParents(canonicalRoot, relative);
            const std::filesystem::path candidate = canonicalRoot / relative;
            const std::filesystem::file_status status = SymlinkStatus(candidate);
            if (status.type() == std::filesystem::file_type::not_found) { continue; }
            if (std::filesystem::is_symlink(status) ||
                !std::filesystem::is_regular_file(status))
            {
                throw std::runtime_error(
                    "refusing to collect manifest-owned output '" + relativeText +
                    "' because it is not a real regular file.");
            }
            const std::filesystem::path canonicalCandidate = WeaklyCanonical(candidate);
            if (!IsWithin(canonicalRoot, canonicalCandidate))
            {
                throw std::runtime_error("refusing to collect manifest-owned output '" +
                                         relativeText + "' because it escapes the output root.");
            }
            if (Pipeline::ContentFileSha256(candidate) != sha256)
            {
                throw std::runtime_error(
                    "refusing to collect manifest-owned output '" + relativeText +
                    "' because its bytes no longer match the previous manifest.");
            }
            obsolete.emplace_back(candidate, relativeText);
        }

        std::size_t removed = 0u;
        for (const auto& [path, relative] : obsolete)
        {
            std::error_code error;
            const bool deleted = std::filesystem::remove(path, error);
            if (error)
            {
                throw std::runtime_error("cannot remove manifest-owned output '" + relative +
                                         "': " + error.message() + ".");
            }
            if (deleted)
            {
                ++removed;
                if (!quiet)
                {
                    std::cout << "[CLEAN] " << relative << " (" << deletionReason << ")\n";
                }
            }
        }
        return removed;
    }

    int RunClean(const CommandLine& command)
    {
        std::size_t removed = 0u;
        std::size_t failed = 0u;
        try
        {
            std::error_code error;
            const std::filesystem::file_status requestedStatus =
                std::filesystem::symlink_status(command.output, error);
            if (error == std::errc::no_such_file_or_directory ||
                requestedStatus.type() == std::filesystem::file_type::not_found)
            {
                if (!command.quiet)
                {
                    std::cout << "Cleaned: 0  Failed: 0\n";
                }
                return 0;
            }
            if (error)
            {
                throw std::runtime_error("cannot inspect the clean output directory: " +
                                         error.message() + ".");
            }
            if (std::filesystem::is_symlink(requestedStatus) ||
                !std::filesystem::is_directory(requestedStatus))
            {
                throw std::runtime_error(
                    "the clean output path must be a real directory, not a symlink or file.");
            }

            const std::filesystem::path outputRoot = WeaklyCanonical(command.output);
            CNA::Tools::ContentStagingDetail::LeaseHandle outputLease;
            AcquireOutputLease(outputRoot, outputLease);

            const std::filesystem::path manifestPath =
                outputRoot / Pipeline::ContentBuildManifestFileName;
            error.clear();
            const std::filesystem::file_status manifestStatus =
                std::filesystem::symlink_status(manifestPath, error);
            if (error == std::errc::no_such_file_or_directory ||
                manifestStatus.type() == std::filesystem::file_type::not_found)
            {
                if (!command.quiet)
                {
                    std::cout << "Cleaned: 0  Failed: 0\n";
                }
                return 0;
            }
            if (error || std::filesystem::is_symlink(manifestStatus) ||
                !std::filesystem::is_regular_file(manifestStatus))
            {
                throw std::runtime_error(
                    "the ownership manifest is unreadable, symlinked, or not a regular file.");
            }

            const std::string originalManifest = ReadText(manifestPath);
            const Pipeline::ContentBuildManifest previousManifest =
                Pipeline::ContentBuildManifest::Parse(originalManifest);
            removed = CollectObsoleteOwnedOutputs(
                previousManifest, Pipeline::ContentBuildManifest{}, outputRoot, command.quiet,
                "manifest-owned output");

            if (ReadText(manifestPath) != originalManifest)
            {
                throw std::runtime_error(
                    "the ownership manifest changed while clean was running; it was retained.");
            }
            error.clear();
            const bool manifestRemoved = std::filesystem::remove(manifestPath, error);
            if (error || !manifestRemoved)
            {
                throw std::runtime_error(
                    "cannot remove the ownership manifest" +
                    std::string(error ? ": " + error.message() : "") + ".");
            }
            if (!command.quiet)
            {
                std::cout << "[CLEAN] " << Pipeline::ContentBuildManifestFileName
                          << " (ownership manifest)\n";
            }
        }
        catch (const std::exception& error)
        {
            ++failed;
            std::cerr << "content clean failed: " << error.what() << "\n";
        }

        if (!command.quiet || failed != 0u)
        {
            std::cout << "Cleaned: " << removed << "  Failed: " << failed << "\n";
        }
        return failed == 0u ? 0 : 1;
    }

    std::vector<std::string> ContentBuildDependencies(
        const Pipeline::ContentBuildManifestEntry& entry)
    {
        std::vector<std::string> result;
        for (const Pipeline::ContentDependency& dependency : entry.dependencies)
        {
            if (dependency.kind == Pipeline::ContentDependencyKind::ContentBuild)
            {
                result.push_back(dependency.identity);
            }
        }
        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());
        return result;
    }

    bool IsCurrentRoute(const Pipeline::ContentBuildManifestEntry& entry,
                        const Pipeline::ContentPipelineRegistry& registry,
                        const BuildItem& item)
    {
        try
        {
            const std::shared_ptr<const Pipeline::ContentImporter> importer =
                registry.ResolveImporter(item.source, item.importer);
            if (entry.importer != importer->Identity() || entry.parameters != item.parameters)
            {
                return false;
            }
            for (const std::string& outputType : importer->OutputTypes())
            {
                try
                {
                    const std::shared_ptr<const Pipeline::ContentProcessor> processor =
                        registry.ResolveProcessor(
                            outputType,
                            item.processor.empty() ? entry.processor.name : item.processor);
                    processor->ValidateParameters(item.parameters);
                    const std::shared_ptr<const Pipeline::ContentTypeWriter> writer =
                        registry.ResolveWriter(
                            processor->OutputType(),
                            item.writer.empty() ? entry.writer.name : item.writer);
                    if (entry.processor == processor->Identity() &&
                        entry.writer == writer->Identity() &&
                        entry.writerSchemas == writer->OutputSchemaIdentities())
                    {
                        return true;
                    }
                }
                catch (...)
                {
                }
            }
            return false;
        }
        catch (...)
        {
            return false;
        }
    }

    bool IsPreviousGraphCurrent(const Pipeline::ContentBuildManifestEntry& entry,
                                const Pipeline::ContentPipelineRegistry& registry,
                                const BuildItem& item,
                                const std::filesystem::path& sourceRoot,
                                const std::filesystem::path& outputRoot)
    {
        try
        {
            const auto primaryOutput = std::find_if(
                entry.outputs.begin(), entry.outputs.end(), [&](const auto& output)
            {
                return output.logicalName == item.logicalName;
            });
            if (entry.nodeId != item.logicalName || primaryOutput == entry.outputs.end())
            {
                return false;
            }
            if (WeaklyCanonical(sourceRoot /
                                CNA::Internal::ContentPathFromUtf8(entry.source)) !=
                    WeaklyCanonical(item.source) ||
                WeaklyCanonical(outputRoot /
                                CNA::Internal::ContentPathFromUtf8(primaryOutput->path)) !=
                    WeaklyCanonical(item.output) ||
                !IsCurrentRoute(entry, registry, item))
            {
                return false;
            }
            return Pipeline::ComputeContentBuildDirectFingerprint(entry, sourceRoot) ==
                   entry.directFingerprint;
        }
        catch (...)
        {
            return false;
        }
    }

    bool CanSkipEffective(
        const Pipeline::ContentBuildManifestEntry& entry,
        const std::filesystem::path& outputRoot,
        const std::map<std::string, std::string>& contentBuildFingerprints)
    {
        try
        {
            if (Pipeline::ComputeContentBuildEffectiveFingerprint(
                    entry, contentBuildFingerprints) != entry.fingerprint)
            {
                return false;
            }
            for (const Pipeline::ContentBuildManifestOutput& output : entry.outputs)
            {
                const std::filesystem::path path = WeaklyCanonical(
                    outputRoot / CNA::Internal::ContentPathFromUtf8(output.path));
                if (!IsWithin(WeaklyCanonical(outputRoot), path) ||
                    !std::filesystem::is_regular_file(path) ||
                    Pipeline::ContentFileSha256(path) != output.sha256)
                {
                    return false;
                }
            }
            for (const Pipeline::ContentBuildManifestDeploymentFile& deployment :
                 entry.deploymentFiles)
            {
                const std::filesystem::path path = WeaklyCanonical(
                    outputRoot / CNA::Internal::ContentPathFromUtf8(deployment.path));
                if (!IsWithin(WeaklyCanonical(outputRoot), path) ||
                    !std::filesystem::is_regular_file(path) ||
                    Pipeline::ContentFileSha256(path) != deployment.sha256)
                {
                    return false;
                }
            }
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    class OutputReservationConflict final : public std::runtime_error
    {
    public:
        OutputReservationConflict(std::string message, std::string existingOwner)
            : std::runtime_error(std::move(message)),
              existingOwner_(std::move(existingOwner))
        {
        }

        [[nodiscard]] const std::string& ExistingOwner() const noexcept
        {
            return existingOwner_;
        }

    private:
        std::string existingOwner_;
    };

    void ReserveOutputs(const Pipeline::ContentBuildManifestEntry& entry,
                        const std::string& owner, const std::filesystem::path& outputRoot,
                        std::map<std::string, std::string>& logicalOwners,
                        std::map<std::string, std::string>& pathOwners)
    {
        std::set<std::string> entryLogicalNames;
        std::set<std::string> entryPaths;
        const std::filesystem::path canonicalRoot = WeaklyCanonical(outputRoot);
        for (const Pipeline::ContentBuildManifestOutput& output : entry.outputs)
        {
            if (!entryLogicalNames.insert(output.logicalName).second)
            {
                throw std::runtime_error("content build node '" + owner +
                                         "' repeats output logical name '" +
                                         output.logicalName + "'.");
            }
            const auto logical = logicalOwners.find(output.logicalName);
            if (logical != logicalOwners.end() && logical->second != owner)
            {
                throw OutputReservationConflict(
                    "content build nodes '" + logical->second + "' and '" + owner +
                        "' both own output logical name '" + output.logicalName + "'.",
                    logical->second);
            }

            const std::filesystem::path path = WeaklyCanonical(
                outputRoot / CNA::Internal::ContentPathFromUtf8(output.path));
            if (!IsWithin(canonicalRoot, path))
            {
                throw std::runtime_error("output '" + output.logicalName +
                                         "' escapes the output root.");
            }
            const std::string identity = CNA::Internal::ContentPathToUtf8(path);
            if (!entryPaths.insert(identity).second)
            {
                throw std::runtime_error("content build node '" + owner +
                                         "' resolves multiple outputs to path '" + identity +
                                         "'.");
            }
            const auto physical = pathOwners.find(identity);
            if (physical != pathOwners.end() && physical->second != owner)
            {
                throw OutputReservationConflict(
                    "content build nodes '" + physical->second + "' and '" + owner +
                        "' resolve outputs to the same path '" + identity + "'.",
                    physical->second);
            }
        }
        for (const Pipeline::ContentBuildManifestDeploymentFile& deployment :
             entry.deploymentFiles)
        {
            const std::filesystem::path path = WeaklyCanonical(
                outputRoot / CNA::Internal::ContentPathFromUtf8(deployment.path));
            if (!IsWithin(canonicalRoot, path))
            {
                throw std::runtime_error("deployment output '" + deployment.path +
                                         "' escapes the output root.");
            }
            const std::string identity = CNA::Internal::ContentPathToUtf8(path);
            if (!entryPaths.insert(identity).second)
            {
                throw std::runtime_error("content build node '" + owner +
                                         "' resolves multiple outputs to path '" + identity +
                                         "'.");
            }
            const auto physical = pathOwners.find(identity);
            if (physical != pathOwners.end() && physical->second != owner)
            {
                throw OutputReservationConflict(
                    "content build nodes '" + physical->second + "' and '" + owner +
                        "' resolve outputs to the same path '" + identity + "'.",
                    physical->second);
            }
        }
        for (const std::string& logicalName : entryLogicalNames)
        {
            logicalOwners.emplace(logicalName, owner);
        }
        for (const std::string& path : entryPaths) { pathOwners.emplace(path, owner); }
    }

    const std::vector<std::uint8_t>& OutputBytes(
        const Pipeline::ContentBuildResult& result,
        const Pipeline::ContentBuildManifestOutput& output)
    {
        if (output.logicalName == result.logicalName) { return result.output.bytes; }
        const auto found = std::find_if(
            result.output.additionalOutputs.begin(), result.output.additionalOutputs.end(),
            [&](const Pipeline::ContentAdditionalWriteOutput& candidate)
        {
            return candidate.logicalName == output.logicalName;
        });
        if (found == result.output.additionalOutputs.end())
        {
            throw std::logic_error("manifest output '" + output.logicalName +
                                   "' has no matching writer result.");
        }
        return found->bytes;
    }

    const Pipeline::ContentBuildManifestOutput& ManifestOutput(
        const Pipeline::ContentBuildManifestEntry& entry, const std::string& logicalName)
    {
        const auto found = std::find_if(
            entry.outputs.begin(), entry.outputs.end(), [&](const auto& output)
        {
            return output.logicalName == logicalName;
        });
        if (found == entry.outputs.end())
        {
            throw std::logic_error("writer output '" + logicalName +
                                   "' has no matching manifest record.");
        }
        return *found;
    }

    const Pipeline::ContentDeploymentFile& DeploymentFile(
        const Pipeline::ContentBuildResult& result,
        const Pipeline::ContentBuildManifestDeploymentFile& manifest)
    {
        const auto found = std::find_if(
            result.deploymentFiles.begin(), result.deploymentFiles.end(),
            [&](const Pipeline::ContentDeploymentFile& deployment)
        {
            return deployment.outputPath == manifest.path;
        });
        if (found == result.deploymentFiles.end())
        {
            throw std::logic_error("manifest deployment output '" + manifest.path +
                                   "' has no matching build result.");
        }
        return *found;
    }

    struct StagedOutput
    {
        std::filesystem::path path;
        std::uintmax_t byteCount = 0u;
    };

    struct BuildNodePlan
    {
        const BuildItem* item = nullptr;
        Pipeline::ContentBuildManifestEntry manifest;
        std::map<std::string, StagedOutput> stagedOutputs;
        std::map<std::string, StagedOutput> stagedDeploymentFiles;
        bool prepared = false;
        bool hasManifest = false;
        std::string failure;
    };

    struct BuildNodeOutcome
    {
        Pipeline::ContentBuildManifestEntry manifest;
        bool success = false;
        bool skipped = false;
        std::string failure;
        std::string statusLine;
    };

    std::vector<std::uint8_t> ReadBinaryFile(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            throw std::runtime_error("cannot open staged content '" +
                                     CNA::Internal::ContentPathToUtf8(path) + "'.");
        }
        std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(stream),
                                        std::istreambuf_iterator<char>()};
        if (stream.bad())
        {
            throw std::runtime_error("cannot completely read staged content '" +
                                     CNA::Internal::ContentPathToUtf8(path) + "'.");
        }
        return bytes;
    }

    void PublishBytes(const Pipeline::ContentBuildManifestEntry& manifest,
                      const std::string& logicalName,
                      const std::vector<std::uint8_t>& bytes,
                      const std::filesystem::path& outputRoot)
    {
        const Pipeline::ContentBuildManifestOutput& output =
            ManifestOutput(manifest, logicalName);
        const std::filesystem::path path =
            outputRoot / CNA::Internal::ContentPathFromUtf8(output.path);
        if (path.has_parent_path()) { std::filesystem::create_directories(path.parent_path()); }
        CNA::Tools::WriteFileAtomically(path, bytes);
    }

    void PublishResult(const Pipeline::ContentBuildResult& result,
                       const Pipeline::ContentBuildManifestEntry& manifest,
                       const std::filesystem::path& outputRoot)
    {
        PublishBytes(manifest, result.logicalName, result.output.bytes, outputRoot);
        for (const Pipeline::ContentAdditionalWriteOutput& output :
             result.output.additionalOutputs)
        {
            PublishBytes(manifest, output.logicalName, output.bytes, outputRoot);
        }
        for (const Pipeline::ContentBuildManifestDeploymentFile& deployment :
             manifest.deploymentFiles)
        {
            const Pipeline::ContentDeploymentFile& source =
                DeploymentFile(result, deployment);
            if (Pipeline::ContentFileSha256(source.source) != deployment.sha256)
            {
                throw std::runtime_error("deployment source for '" + deployment.path +
                                         "' changed while the node was building.");
            }
            const std::filesystem::path destination =
                outputRoot / CNA::Internal::ContentPathFromUtf8(deployment.path);
            if (destination.has_parent_path())
            {
                std::filesystem::create_directories(destination.parent_path());
            }
            CNA::Tools::CopyFileAtomically(source.source, destination);
            if (Pipeline::ContentFileSha256(destination) != deployment.sha256)
            {
                throw std::runtime_error("deployed support file '" + deployment.path +
                                         "' failed its content digest check.");
            }
        }
    }

    std::uintmax_t PublishStagedResult(const BuildNodePlan& plan,
                                       const std::filesystem::path& outputRoot)
    {
        std::uintmax_t byteCount = 0u;
        for (const Pipeline::ContentBuildManifestOutput& output : plan.manifest.outputs)
        {
            const auto staged = plan.stagedOutputs.find(output.logicalName);
            if (staged == plan.stagedOutputs.end())
            {
                throw std::logic_error("prepared output '" + output.logicalName +
                                       "' has no staged file.");
            }
            std::vector<std::uint8_t> bytes = ReadBinaryFile(staged->second.path);
            if (bytes.size() != staged->second.byteCount ||
                Pipeline::ContentSha256(bytes) != output.sha256)
            {
                throw std::runtime_error("staged output '" + output.logicalName +
                                         "' failed its content digest check.");
            }
            PublishBytes(plan.manifest, output.logicalName, bytes, outputRoot);
            byteCount += bytes.size();
        }
        for (const Pipeline::ContentBuildManifestDeploymentFile& deployment :
             plan.manifest.deploymentFiles)
        {
            const auto staged = plan.stagedDeploymentFiles.find(deployment.path);
            if (staged == plan.stagedDeploymentFiles.end())
            {
                throw std::logic_error("prepared deployment output '" + deployment.path +
                                       "' has no staged file.");
            }
            if (std::filesystem::file_size(staged->second.path) !=
                    staged->second.byteCount ||
                Pipeline::ContentFileSha256(staged->second.path) != deployment.sha256)
            {
                throw std::runtime_error("staged deployment output '" + deployment.path +
                                         "' failed its content digest check.");
            }
            const std::filesystem::path destination =
                outputRoot / CNA::Internal::ContentPathFromUtf8(deployment.path);
            if (destination.has_parent_path())
            {
                std::filesystem::create_directories(destination.parent_path());
            }
            CNA::Tools::CopyFileAtomically(staged->second.path, destination);
            byteCount += staged->second.byteCount;
        }
        return byteCount;
    }

    std::string BuildStatusLine(const BuildItem& item,
                                const Pipeline::ContentBuildManifestEntry& manifest,
                                std::uintmax_t outputBytes)
    {
        std::ostringstream status;
        status << "[BUILD] " << item.logicalName << " -> "
               << CNA::Internal::ContentPathToUtf8(item.output) << " ("
               << manifest.outputs.size() << " output(s)";
        if (!manifest.deploymentFiles.empty())
        {
            status << ", " << manifest.deploymentFiles.size() << " deployment file(s)";
        }
        status << ", " << outputBytes << " bytes; "
               << manifest.importer.name << " -> " << manifest.processor.name << " -> "
               << manifest.writer.name << ")";
        return status.str();
    }

    BuildNodePlan PrepareBuildNode(
        const BuildItem& item, std::size_t index, const Pipeline::ContentPipeline& pipeline,
        const Pipeline::ContentPipelineRegistry& registry,
        const Pipeline::ContentBuildManifest& previousManifest,
        const std::filesystem::path& sourceRoot, const std::filesystem::path& outputRoot,
        const std::filesystem::path& stagingRoot)
    {
        BuildNodePlan plan;
        plan.item = &item;
        try
        {
            const Pipeline::ContentBuildManifestEntry* previous =
                previousManifest.Find(item.logicalName);
            if (previous != nullptr &&
                IsPreviousGraphCurrent(*previous, registry, item, sourceRoot, outputRoot))
            {
                plan.manifest = *previous;
                plan.hasManifest = true;
                return plan;
            }

            Pipeline::ContentBuildRequest request;
            request.sourceRoot = sourceRoot;
            request.source = item.source;
            request.logicalName = item.logicalName;
            request.importer = item.importer;
            request.processor = item.processor;
            request.writer = item.writer;
            request.parameters = item.parameters;
            Pipeline::ContentBuildResult result = pipeline.Build(request);

            plan.manifest = Pipeline::MakeContentBuildManifestEntry(
                result, sourceRoot, outputRoot, item.output);
            plan.manifest.directFingerprint =
                Pipeline::ComputeContentBuildDirectFingerprint(plan.manifest, sourceRoot);
            const std::filesystem::path nodeStage = stagingRoot / std::to_string(index);
            try
            {
                std::filesystem::create_directories(nodeStage);
                for (std::size_t outputIndex = 0u;
                     outputIndex < plan.manifest.outputs.size(); ++outputIndex)
                {
                    const Pipeline::ContentBuildManifestOutput& output =
                        plan.manifest.outputs[outputIndex];
                    const std::vector<std::uint8_t>& bytes = OutputBytes(result, output);
                    const std::filesystem::path staged =
                        nodeStage / (std::to_string(outputIndex) + ".cnb");
                    CNA::Tools::WriteFileAtomically(staged, bytes);
                    plan.stagedOutputs.emplace(output.logicalName,
                                               StagedOutput{staged, bytes.size()});
                }
                for (std::size_t deploymentIndex = 0u;
                     deploymentIndex < plan.manifest.deploymentFiles.size(); ++deploymentIndex)
                {
                    const Pipeline::ContentBuildManifestDeploymentFile& deployment =
                        plan.manifest.deploymentFiles[deploymentIndex];
                    const Pipeline::ContentDeploymentFile& source =
                        DeploymentFile(result, deployment);
                    const std::filesystem::path staged =
                        nodeStage / (std::to_string(deploymentIndex) + ".support");
                    CNA::Tools::CopyFileAtomically(source.source, staged);
                    if (Pipeline::ContentFileSha256(staged) != deployment.sha256)
                    {
                        throw std::runtime_error("deployment source for '" + deployment.path +
                                                 "' changed while it was staged.");
                    }
                    plan.stagedDeploymentFiles.emplace(
                        deployment.path,
                        StagedOutput{staged, std::filesystem::file_size(staged)});
                }
            }
            catch (const std::exception& error)
            {
                throw Pipeline::ContentPipelineError(
                    item.source, item.logicalName, Pipeline::ContentPipelineStage::Write,
                    "CNA.ContentBuildStaging", error.what());
            }
            plan.prepared = true;
            plan.hasManifest = true;
        }
        catch (const std::exception& error)
        {
            plan.failure = error.what();
        }
        return plan;
    }

    BuildNodeOutcome ExecuteBuildNode(
        const BuildNodePlan& plan, const Pipeline::ContentPipeline& pipeline,
        const std::filesystem::path& sourceRoot, const std::filesystem::path& outputRoot,
        const std::map<std::string, std::string>& effectiveFingerprints)
    {
        BuildNodeOutcome outcome;
        const BuildItem& item = *plan.item;
        try
        {
            if (plan.prepared)
            {
                outcome.manifest = plan.manifest;
                outcome.manifest.fingerprint =
                    Pipeline::ComputeContentBuildEffectiveFingerprint(
                        outcome.manifest, effectiveFingerprints);
                try
                {
                    const std::uintmax_t outputBytes = PublishStagedResult(plan, outputRoot);
                    outcome.statusLine = BuildStatusLine(item, outcome.manifest, outputBytes);
                }
                catch (const std::exception& error)
                {
                    throw Pipeline::ContentPipelineError(
                        item.source, item.logicalName, Pipeline::ContentPipelineStage::Publish,
                        "CNA.AtomicPublisher", error.what());
                }
                outcome.success = true;
                return outcome;
            }

            if (CanSkipEffective(plan.manifest, outputRoot, effectiveFingerprints))
            {
                outcome.manifest = plan.manifest;
                outcome.success = true;
                outcome.skipped = true;
                outcome.statusLine = "[SKIP] " + item.logicalName + " -> " +
                                     CNA::Internal::ContentPathToUtf8(item.output);
                return outcome;
            }

            Pipeline::ContentBuildRequest request;
            request.sourceRoot = sourceRoot;
            request.source = item.source;
            request.logicalName = item.logicalName;
            request.importer = item.importer;
            request.processor = item.processor;
            request.writer = item.writer;
            request.parameters = item.parameters;
            Pipeline::ContentBuildResult result = pipeline.Build(request);
            outcome.manifest = Pipeline::MakeContentBuildManifestEntry(
                result, sourceRoot, outputRoot, item.output);
            outcome.manifest.directFingerprint =
                Pipeline::ComputeContentBuildDirectFingerprint(outcome.manifest, sourceRoot);
            if (outcome.manifest.directFingerprint != plan.manifest.directFingerprint ||
                ContentBuildDependencies(outcome.manifest) !=
                    ContentBuildDependencies(plan.manifest))
            {
                throw Pipeline::ContentPipelineError(
                    item.source, item.logicalName, Pipeline::ContentPipelineStage::Graph,
                    "CNA.ContentBuildGraph",
                    "a component changed the frozen build topology without a changed direct "
                    "fingerprint.");
            }
            outcome.manifest.fingerprint =
                Pipeline::ComputeContentBuildEffectiveFingerprint(
                    outcome.manifest, effectiveFingerprints);
            try
            {
                PublishResult(result, outcome.manifest, outputRoot);
            }
            catch (const std::exception& error)
            {
                throw Pipeline::ContentPipelineError(
                    item.source, item.logicalName, Pipeline::ContentPipelineStage::Publish,
                    "CNA.AtomicPublisher", error.what());
            }
            std::uintmax_t outputBytes = std::accumulate(
                outcome.manifest.outputs.begin(), outcome.manifest.outputs.end(),
                std::uintmax_t{0u}, [&](std::uintmax_t total, const auto& output)
                { return total + OutputBytes(result, output).size(); });
            for (const Pipeline::ContentDeploymentFile& deployment : result.deploymentFiles)
            {
                outputBytes += std::filesystem::file_size(deployment.source);
            }
            outcome.statusLine = BuildStatusLine(item, outcome.manifest, outputBytes);
            outcome.success = true;
        }
        catch (const std::exception& error)
        {
            outcome.failure = error.what();
        }
        return outcome;
    }

    int Run(const std::vector<std::filesystem::path>& arguments,
            std::shared_ptr<const Pipeline::ContentPipelineRegistry> registry)
    {
        CommandLine command;
        try
        {
            command = ParseCommandLine(arguments);
        }
        catch (const std::exception& error)
        {
            std::cerr << "error: " << error.what() << "\n";
            PrintUsage();
            return 2;
        }
        if (command.operation == ContentCommand::Clean) { return RunClean(command); }

        std::filesystem::path sourceRoot;
        std::filesystem::path outputRoot;
        bool directoryBuild = false;
        std::vector<BuildItem> builds;
        try
        {
            builds = DiscoverBuilds(command, *registry, sourceRoot, outputRoot, directoryBuild);
            const Pipeline::ContentBuildConfiguration configuration =
                LoadConfiguration(command, sourceRoot);
            ApplyConfiguration(builds, configuration, *registry, sourceRoot, outputRoot,
                               directoryBuild);
            std::sort(builds.begin(), builds.end(), [](const BuildItem& left,
                                                       const BuildItem& right)
            {
                return left.logicalName < right.logicalName;
            });
        }
        catch (const std::exception& error)
        {
            std::cerr << "error: " << error.what() << "\n";
            return 1;
        }

        const Pipeline::ContentPipeline pipeline(registry);
        const std::filesystem::path manifestPath =
            outputRoot / Pipeline::ContentBuildManifestFileName;
        CNA::Tools::ContentStagingDetail::LeaseHandle outputLease;
        try
        {
            AcquireOutputLease(outputRoot, outputLease);
        }
        catch (const std::exception& error)
        {
            std::cerr << "content output lease failed: " << error.what() << "\n";
            return 1;
        }
        std::string originalManifest;
        bool previousOwnershipTrusted = false;
        const Pipeline::ContentBuildManifest previousManifest =
            LoadManifest(manifestPath, originalManifest, command.quiet,
                         previousOwnershipTrusted);
        Pipeline::ContentBuildManifest nextManifest = previousManifest;
        if (directoryBuild) { nextManifest.Clear(); }

        std::size_t built = 0u;
        std::size_t skipped = 0u;
        std::size_t failed = 0u;
        std::map<std::string, std::string> logicalOwners;
        std::map<std::string, std::string> pathOwners;
        std::map<std::string, std::size_t> plansByNode;
        for (const BuildItem& item : builds)
        {
            logicalOwners.emplace(item.logicalName, item.logicalName);
            pathOwners.emplace(CNA::Internal::ContentPathToUtf8(WeaklyCanonical(item.output)),
                               item.logicalName);
        }

        std::unique_ptr<CNA::Tools::ContentBuildStagingDirectory> staging;
        try
        {
            staging = std::make_unique<CNA::Tools::ContentBuildStagingDirectory>();
            if (!command.quiet)
            {
                const CNA::Tools::ContentStagingScavengeResult& scavenged =
                    staging->ScavengeResult();
                if (scavenged.removedDirectories > 0u)
                {
                    std::cerr << "[CLEAN] removed " << scavenged.removedDirectories
                              << " abandoned content staging director"
                              << (scavenged.removedDirectories == 1u ? "y" : "ies") << ".\n";
                }
                for (const std::string& diagnostic : scavenged.diagnostics)
                {
                    std::cerr << "warning: content staging scavenger: " << diagnostic << "\n";
                }
            }
        }
        catch (const std::exception& error)
        {
            std::cerr << "content staging failed: " << error.what() << "\n";
            return 1;
        }

        std::vector<BuildNodePlan> plans(builds.size());
        try
        {
            for (std::size_t offset = 0u; offset < builds.size(); offset += command.workers)
            {
                const std::size_t end = std::min(builds.size(), offset + command.workers);
                if (command.workers == 1u)
                {
                    plans[offset] = PrepareBuildNode(
                        builds[offset], offset, pipeline, *registry, previousManifest, sourceRoot,
                        outputRoot, staging->Path());
                    continue;
                }

                std::vector<std::future<BuildNodePlan>> futures;
                futures.reserve(end - offset);
                for (std::size_t index = offset; index < end; ++index)
                {
                    futures.push_back(std::async(
                        std::launch::async,
                        [&, index]
                        {
                            return PrepareBuildNode(
                                builds[index], index, pipeline, *registry, previousManifest,
                                sourceRoot, outputRoot, staging->Path());
                        }));
                }
                for (std::size_t index = offset; index < end; ++index)
                {
                    plans[index] = futures[index - offset].get();
                }
            }
        }
        catch (const std::exception& error)
        {
            std::cerr << "content worker preparation failed: " << error.what() << "\n";
            return 1;
        }

        for (std::size_t index = 0u; index < plans.size(); ++index)
        {
            plansByNode.emplace(plans[index].item->logicalName, index);
        }
        for (std::size_t index = 0u; index < plans.size(); ++index)
        {
            if (!plans[index].failure.empty() || !plans[index].hasManifest) { continue; }
            try
            {
                ReserveOutputs(plans[index].manifest, plans[index].item->logicalName,
                               outputRoot, logicalOwners, pathOwners);
            }
            catch (const OutputReservationConflict& error)
            {
                plans[index].failure = Pipeline::ContentPipelineError(
                    plans[index].item->source, plans[index].item->logicalName,
                    Pipeline::ContentPipelineStage::Graph, "CNA.ContentBuildGraph",
                    error.what()).what();
                const auto existing = plansByNode.find(error.ExistingOwner());
                if (existing != plansByNode.end() &&
                    plans[existing->second].failure.empty())
                {
                    BuildNodePlan& conflicting = plans[existing->second];
                    conflicting.failure = Pipeline::ContentPipelineError(
                        conflicting.item->source, conflicting.item->logicalName,
                        Pipeline::ContentPipelineStage::Graph, "CNA.ContentBuildGraph",
                        error.what()).what();
                }
            }
            catch (const std::exception& error)
            {
                plans[index].failure = Pipeline::ContentPipelineError(
                    plans[index].item->source, plans[index].item->logicalName,
                    Pipeline::ContentPipelineStage::Graph, "CNA.ContentBuildGraph",
                    error.what()).what();
            }
        }

        for (BuildNodePlan& plan : plans)
        {
            if (!plan.failure.empty() || !plan.hasManifest) { continue; }
            for (const std::string& dependency : ContentBuildDependencies(plan.manifest))
            {
                if (!plansByNode.contains(dependency))
                {
                    plan.failure = Pipeline::ContentPipelineError(
                        plan.item->source, plan.item->logicalName,
                        Pipeline::ContentPipelineStage::Graph, "CNA.ContentBuildGraph",
                        "content-build dependency '" + dependency +
                            "' does not name a discovered primary build node.").what();
                    break;
                }
            }
        }

        enum class VisitState
        {
            Unvisited,
            Visiting,
            Done,
        };
        std::map<std::string, VisitState> visitStates;
        std::vector<std::string> activeNodes;
        std::map<std::string, std::size_t> activePositions;
        std::map<std::string, std::string> cycleFailures;
        struct VisitFrame
        {
            std::string nodeId;
            std::vector<std::string> dependencies;
            std::size_t nextDependency = 0u;
        };
        std::vector<VisitFrame> visitStack;
        const auto pushVisit = [&](const std::string& nodeId)
        {
            visitStates[nodeId] = VisitState::Visiting;
            activePositions.emplace(nodeId, activeNodes.size());
            activeNodes.push_back(nodeId);
            visitStack.push_back(
                {nodeId, ContentBuildDependencies(plans[plansByNode.at(nodeId)].manifest)});
        };
        for (const BuildNodePlan& rootPlan : plans)
        {
            const std::string& rootId = rootPlan.item->logicalName;
            if (visitStates[rootId] != VisitState::Unvisited || !rootPlan.failure.empty() ||
                !rootPlan.hasManifest)
            {
                continue;
            }
            pushVisit(rootId);
            while (!visitStack.empty())
            {
                VisitFrame& frame = visitStack.back();
                if (frame.nextDependency == frame.dependencies.size())
                {
                    activePositions.erase(frame.nodeId);
                    activeNodes.pop_back();
                    visitStates[frame.nodeId] = VisitState::Done;
                    visitStack.pop_back();
                    continue;
                }

                const std::string dependency =
                    frame.dependencies[frame.nextDependency++];
                BuildNodePlan& dependencyPlan = plans[plansByNode.at(dependency)];
                if (!dependencyPlan.failure.empty() || !dependencyPlan.hasManifest) { continue; }
                if (visitStates[dependency] == VisitState::Visiting)
                {
                    const std::size_t cycleStart = activePositions.at(dependency);
                    std::ostringstream reason;
                    reason << "content-build dependency cycle:";
                    for (std::size_t node = cycleStart; node < activeNodes.size(); ++node)
                    {
                        reason << "\n  " << (node == cycleStart ? "" : "-> ")
                               << activeNodes[node];
                    }
                    reason << "\n  -> " << dependency;
                    const std::string message =
                        ContentBuildCycleError(*dependencyPlan.item, reason.str()).what();
                    for (std::size_t node = cycleStart; node < activeNodes.size(); ++node)
                    {
                        cycleFailures.try_emplace(activeNodes[node], message);
                    }
                    continue;
                }
                if (visitStates[dependency] == VisitState::Unvisited) { pushVisit(dependency); }
            }
        }
        for (const auto& [nodeId, message] : cycleFailures)
        {
            if (plans[plansByNode.at(nodeId)].failure.empty())
            {
                plans[plansByNode.at(nodeId)].failure = message;
            }
        }

        enum class Resolution
        {
            Pending,
            Succeeded,
            Failed,
        };
        struct TerminalEvent
        {
            bool failure = false;
            std::string text;
        };
        std::map<std::string, Resolution> resolutions;
        std::map<std::string, std::string> effectiveFingerprints;
        std::map<std::string, std::string> failureMessages;
        std::vector<TerminalEvent> events;
        std::size_t resolved = 0u;
        for (const BuildNodePlan& plan : plans)
        {
            if (!plan.failure.empty())
            {
                resolutions[plan.item->logicalName] = Resolution::Failed;
                failureMessages.emplace(plan.item->logicalName, plan.failure);
                events.push_back({true, plan.failure});
                ++failed;
                ++resolved;
            }
        }

        while (resolved < plans.size())
        {
            std::vector<std::size_t> ready;
            bool propagatedFailure = false;
            for (std::size_t index = 0u; index < plans.size(); ++index)
            {
                BuildNodePlan& plan = plans[index];
                const std::string& nodeId = plan.item->logicalName;
                if (resolutions[nodeId] != Resolution::Pending) { continue; }
                bool dependenciesResolved = true;
                std::string failedDependency;
                for (const std::string& dependency :
                     ContentBuildDependencies(plan.manifest))
                {
                    if (resolutions[dependency] == Resolution::Pending)
                    {
                        dependenciesResolved = false;
                        break;
                    }
                    if (failedDependency.empty() &&
                        resolutions[dependency] == Resolution::Failed)
                    {
                        failedDependency = dependency;
                    }
                }
                if (!dependenciesResolved) { continue; }
                if (!failedDependency.empty())
                {
                    const std::string message = Pipeline::ContentPipelineError(
                        plan.item->source, nodeId, Pipeline::ContentPipelineStage::Graph,
                        "CNA.ContentBuildGraph",
                        "dependency '" + failedDependency + "' failed: " +
                            failureMessages.at(failedDependency)).what();
                    resolutions[nodeId] = Resolution::Failed;
                    failureMessages.emplace(nodeId, message);
                    events.push_back({true, message});
                    ++failed;
                    ++resolved;
                    propagatedFailure = true;
                    continue;
                }
                ready.push_back(index);
            }

            if (propagatedFailure) { continue; }
            if (ready.empty())
            {
                for (BuildNodePlan& plan : plans)
                {
                    const std::string& nodeId = plan.item->logicalName;
                    if (resolutions[nodeId] != Resolution::Pending) { continue; }
                    const std::string message = Pipeline::ContentPipelineError(
                        plan.item->source, nodeId, Pipeline::ContentPipelineStage::Graph,
                        "CNA.ContentBuildGraph",
                        "internal scheduler error: unresolved dependency graph remained after "
                        "cycle detection.").what();
                    resolutions[nodeId] = Resolution::Failed;
                    failureMessages.emplace(nodeId, message);
                    events.push_back({true, message});
                    ++failed;
                    ++resolved;
                }
                break;
            }

            if (ready.size() > command.workers) { ready.resize(command.workers); }
            std::vector<BuildNodeOutcome> outcomes;
            outcomes.reserve(ready.size());
            if (command.workers == 1u)
            {
                outcomes.push_back(ExecuteBuildNode(
                    plans[ready.front()], pipeline, sourceRoot, outputRoot,
                    effectiveFingerprints));
            }
            else
            {
                try
                {
                    std::vector<std::future<BuildNodeOutcome>> futures;
                    futures.reserve(ready.size());
                    for (const std::size_t index : ready)
                    {
                        futures.push_back(std::async(
                            std::launch::async,
                            [&, index]
                            {
                                return ExecuteBuildNode(
                                    plans[index], pipeline, sourceRoot, outputRoot,
                                    effectiveFingerprints);
                            }));
                    }
                    for (std::future<BuildNodeOutcome>& future : futures)
                    {
                        outcomes.push_back(future.get());
                    }
                }
                catch (const std::exception& error)
                {
                    outcomes.clear();
                    for (const std::size_t index : ready)
                    {
                        BuildNodeOutcome outcome;
                        outcome.failure = Pipeline::ContentPipelineError(
                            plans[index].item->source, plans[index].item->logicalName,
                            Pipeline::ContentPipelineStage::Graph, "CNA.ContentBuildScheduler",
                            "worker dispatch failed: " + std::string(error.what())).what();
                        outcomes.push_back(std::move(outcome));
                    }
                }
            }

            for (std::size_t outcomeIndex = 0u; outcomeIndex < outcomes.size(); ++outcomeIndex)
            {
                const std::string& nodeId = plans[ready[outcomeIndex]].item->logicalName;
                BuildNodeOutcome& outcome = outcomes[outcomeIndex];
                if (!outcome.success)
                {
                    resolutions[nodeId] = Resolution::Failed;
                    failureMessages.emplace(nodeId, outcome.failure);
                    events.push_back({true, outcome.failure});
                    ++failed;
                }
                else
                {
                    resolutions[nodeId] = Resolution::Succeeded;
                    effectiveFingerprints.insert_or_assign(nodeId,
                                                           outcome.manifest.fingerprint);
                    nextManifest.Set(std::move(outcome.manifest));
                    events.push_back({false, std::move(outcome.statusLine)});
                    if (outcome.skipped) { ++skipped; }
                    else { ++built; }
                }
                ++resolved;
            }
        }

        std::set<std::string> reportedFailures;
        for (const TerminalEvent& event : events)
        {
            if (event.failure)
            {
                if (reportedFailures.insert(event.text).second)
                {
                    std::cerr << event.text << "\n";
                }
            }
            else if (!command.quiet)
            {
                std::cout << event.text << "\n";
            }
        }

        if (failed == 0u)
        {
            if (previousOwnershipTrusted)
            {
                try
                {
                    static_cast<void>(CollectObsoleteOwnedOutputs(
                        previousManifest, nextManifest, outputRoot, command.quiet,
                        "obsolete manifest-owned output"));
                }
                catch (const std::exception& error)
                {
                    ++failed;
                    std::cerr << "obsolete owned-output collection failed: " << error.what()
                              << "\n";
                }
            }
        }

        if (failed == 0u)
        {
            try
            {
                const std::string serialized = nextManifest.Serialize();
                if (serialized != originalManifest)
                {
                    std::filesystem::create_directories(outputRoot);
                    CNA::Tools::WriteFileAtomically(
                        manifestPath,
                        std::vector<std::uint8_t>(serialized.begin(), serialized.end()));
                }
            }
            catch (const std::exception& error)
            {
                ++failed;
                std::cerr << "content manifest publication failed: " << error.what() << "\n";
            }
        }

        if (!command.quiet || failed != 0u)
        {
            std::cout << "Built: " << built << "  Skipped: " << skipped
                      << "  Failed: " << failed << "\n";
        }
        return failed == 0u ? 0 : 1;
    }
}

namespace CNA::Content::Pipeline
{
    void RegisterBuiltInContentPipeline(ContentPipelineRegistry& registry)
    {
        RegisterTexture2DContentPipeline(registry);
        RegisterSoundEffectContentPipeline(registry);
        RegisterSongContentPipeline(registry);
        RegisterVideoContentPipeline(registry);
        RegisterModelContentPipeline(registry);
        RegisterCnjContentPipeline(registry);
        RegisterXnbContentPipeline(registry);
    }

    int RunContentCompiler(const std::vector<std::filesystem::path>& arguments,
                           std::shared_ptr<const ContentPipelineRegistry> registry)
    {
        if (registry == nullptr)
        {
            throw std::invalid_argument("RunContentCompiler(): registry must not be null.");
        }
        registry->Freeze();
        return Run(arguments, std::move(registry));
    }
} // namespace CNA::Content::Pipeline
