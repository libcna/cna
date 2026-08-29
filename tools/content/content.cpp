// SPDX-License-Identifier: MS-PL

#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <numeric>
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
#include "CNA/Internal/ContentPath.hpp"
#include "CnaToolAtomicWrite.hpp"

namespace Pipeline = CNA::Content::Pipeline;

namespace
{
    struct CommandLine
    {
        std::filesystem::path source;
        std::filesystem::path output;
        std::filesystem::path configuration;
        bool quiet = false;
    };

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

    void PrintUsage()
    {
        std::cerr
            << "Usage: cna-content build <source-file-or-directory> -o <output> "
               "[--config <file>] [--quiet]\n\n"
            << "Builds source content through Importer -> Processor -> Content Type Writer -> "
               "CNB.\n"
            << "A source file requires an output .cnb path. A source directory requires an "
               "output\n"
            << "directory; relative paths and logical content names are preserved.\n";
    }

    bool IsOption(const std::filesystem::path& argument, const char* spelling)
    {
        return argument == std::filesystem::path(spelling);
    }

    CommandLine ParseCommandLine(const std::vector<std::filesystem::path>& arguments)
    {
        if (arguments.empty() || !IsOption(arguments[0], "build"))
        {
            throw std::invalid_argument("the first argument must be 'build'.");
        }

        CommandLine command;
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
                                                bool quiet)
    {
        try
        {
            if (!std::filesystem::exists(path)) { return {}; }
            original = ReadText(path);
            return Pipeline::ContentBuildManifest::Parse(original);
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
                        entry.writer == writer->Identity())
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
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

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
                throw std::runtime_error("content build nodes '" + logical->second +
                                         "' and '" + owner + "' both own output logical name '" +
                                         output.logicalName + "'.");
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
                throw std::runtime_error("content build nodes '" + physical->second +
                                         "' and '" + owner +
                                         "' resolve outputs to the same path '" + identity +
                                         "'.");
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
        }
        catch (const std::exception& error)
        {
            std::cerr << "error: " << error.what() << "\n";
            return 1;
        }

        const Pipeline::ContentPipeline pipeline(registry);
        const std::filesystem::path manifestPath =
            outputRoot / Pipeline::ContentBuildManifestFileName;
        std::string originalManifest;
        const Pipeline::ContentBuildManifest previousManifest =
            LoadManifest(manifestPath, originalManifest, command.quiet);
        Pipeline::ContentBuildManifest nextManifest = previousManifest;
        if (directoryBuild) { nextManifest.Clear(); }

        std::size_t built = 0u;
        std::size_t skipped = 0u;
        std::size_t failed = 0u;
        std::map<std::string, std::string> logicalOwners;
        std::map<std::string, std::string> pathOwners;
        std::map<std::string, const BuildItem*> itemsByNode;
        for (const BuildItem& item : builds)
        {
            logicalOwners.emplace(item.logicalName, item.relativeSource);
            pathOwners.emplace(CNA::Internal::ContentPathToUtf8(WeaklyCanonical(item.output)),
                               item.relativeSource);
            itemsByNode.emplace(item.logicalName, &item);
        }

        enum class BuildState
        {
            Unvisited,
            Visiting,
            Done,
            Failed,
        };
        std::map<std::string, BuildState> states;
        std::map<std::string, std::string> effectiveFingerprints;
        std::map<std::string, std::string> failureMessages;
        constexpr const char* graphComponent = "CNA.ContentBuildGraph";

        std::function<void(const std::string&)> buildNode;
        buildNode = [&](const std::string& nodeId)
        {
            const auto itemFound = itemsByNode.find(nodeId);
            if (itemFound == itemsByNode.end())
            {
                throw std::runtime_error("content-build dependency '" + nodeId +
                                         "' does not name a discovered primary build node.");
            }
            const BuildItem& item = *itemFound->second;
            BuildState& state = states[nodeId];
            if (state == BuildState::Done) { return; }
            if (state == BuildState::Failed)
            {
                throw std::runtime_error(failureMessages.at(nodeId));
            }
            if (state == BuildState::Visiting)
            {
                throw Pipeline::ContentPipelineError(
                    item.source, item.logicalName, Pipeline::ContentPipelineStage::Graph,
                    graphComponent,
                    "content-build dependency cycle detected at node '" + nodeId + "'.");
            }
            state = BuildState::Visiting;

            try
            {
                const Pipeline::ContentBuildManifestEntry* previous =
                    previousManifest.Find(item.logicalName);
                const bool previousGraphCurrent =
                    previous != nullptr && IsPreviousGraphCurrent(
                                               *previous, *registry, item, sourceRoot, outputRoot);
                if (previousGraphCurrent)
                {
                    for (const std::string& dependency : ContentBuildDependencies(*previous))
                    {
                        try
                        {
                            buildNode(dependency);
                        }
                        catch (const std::exception& error)
                        {
                            throw Pipeline::ContentPipelineError(
                                item.source, item.logicalName,
                                Pipeline::ContentPipelineStage::Graph, graphComponent,
                                "dependency '" + dependency + "' failed: " + error.what());
                        }
                    }
                    if (CanSkipEffective(*previous, outputRoot, effectiveFingerprints))
                    {
                        ReserveOutputs(*previous, item.relativeSource, outputRoot, logicalOwners,
                                       pathOwners);
                        nextManifest.Set(*previous);
                        effectiveFingerprints.insert_or_assign(item.logicalName,
                                                              previous->fingerprint);
                        state = BuildState::Done;
                        ++skipped;
                        if (!command.quiet)
                        {
                            std::cout << "[SKIP] " << item.logicalName << " -> "
                                      << CNA::Internal::ContentPathToUtf8(item.output) << "\n";
                        }
                        return;
                    }
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

                Pipeline::ContentBuildManifestEntry manifestEntry =
                    Pipeline::MakeContentBuildManifestEntry(
                        result, sourceRoot, outputRoot, item.output);
                for (const std::string& dependency : ContentBuildDependencies(manifestEntry))
                {
                    try
                    {
                        buildNode(dependency);
                    }
                    catch (const std::exception& error)
                    {
                        throw Pipeline::ContentPipelineError(
                            item.source, item.logicalName, Pipeline::ContentPipelineStage::Graph,
                            graphComponent,
                            "dependency '" + dependency + "' failed: " + error.what());
                    }
                }
                manifestEntry.directFingerprint =
                    Pipeline::ComputeContentBuildDirectFingerprint(manifestEntry, sourceRoot);
                manifestEntry.fingerprint = Pipeline::ComputeContentBuildEffectiveFingerprint(
                    manifestEntry, effectiveFingerprints);
                ReserveOutputs(manifestEntry, item.relativeSource, outputRoot, logicalOwners,
                               pathOwners);

                try
                {
                    const auto publish = [&](const std::string& logicalName,
                                             const std::vector<std::uint8_t>& bytes)
                    {
                        const Pipeline::ContentBuildManifestOutput& output =
                            ManifestOutput(manifestEntry, logicalName);
                        const std::filesystem::path path =
                            outputRoot / CNA::Internal::ContentPathFromUtf8(output.path);
                        if (path.has_parent_path())
                        {
                            std::filesystem::create_directories(path.parent_path());
                        }
                        CNA::Tools::WriteFileAtomically(path, bytes);
                    };
                    publish(result.logicalName, result.output.bytes);
                    for (const Pipeline::ContentAdditionalWriteOutput& output :
                         result.output.additionalOutputs)
                    {
                        publish(output.logicalName, output.bytes);
                    }
                }
                catch (const std::exception& error)
                {
                    throw Pipeline::ContentPipelineError(
                        item.source, item.logicalName, Pipeline::ContentPipelineStage::Publish,
                        "CNA.AtomicPublisher", error.what());
                }

                const std::size_t outputCount = manifestEntry.outputs.size();
                const std::size_t outputBytes = std::accumulate(
                    manifestEntry.outputs.begin(), manifestEntry.outputs.end(), std::size_t{0u},
                    [&](std::size_t total, const auto& output)
                    { return total + OutputBytes(result, output).size(); });
                effectiveFingerprints.insert_or_assign(item.logicalName,
                                                       manifestEntry.fingerprint);
                nextManifest.Set(std::move(manifestEntry));

                state = BuildState::Done;
                ++built;
                if (!command.quiet)
                {
                    std::cout << "[BUILD] " << item.logicalName << " -> "
                              << CNA::Internal::ContentPathToUtf8(item.output)
                              << " (" << outputCount << " output(s), " << outputBytes
                              << " bytes; "
                              << result.importer.name << " -> " << result.processor.name << " -> "
                              << result.writer.name << ")\n";
                }
            }
            catch (const std::exception& error)
            {
                state = BuildState::Failed;
                failureMessages.insert_or_assign(nodeId, error.what());
                throw;
            }
        };

        for (const BuildItem& item : builds)
        {
            try
            {
                buildNode(item.logicalName);
            }
            catch (const std::exception& error)
            {
                ++failed;
                std::cerr << error.what() << "\n";
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
    }

    int RunContentCompiler(const std::vector<std::filesystem::path>& arguments,
                           std::shared_ptr<const ContentPipelineRegistry> registry)
    {
        if (registry == nullptr)
        {
            throw std::invalid_argument("RunContentCompiler(): registry must not be null.");
        }
        return Run(arguments, std::move(registry));
    }
} // namespace CNA::Content::Pipeline
