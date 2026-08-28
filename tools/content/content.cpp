// SPDX-License-Identifier: MS-PL

#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "CNA/Content/Pipeline/ContentBuildManifest.hpp"
#include "CNA/Content/Pipeline/ContentPipeline.hpp"
#include "CNA/Content/Pipeline/ModelContentPipeline.hpp"
#include "CNA/Content/Pipeline/SoundEffectContentPipeline.hpp"
#include "CNA/Content/Pipeline/Texture2DContentPipeline.hpp"
#include "CnaToolAtomicWrite.hpp"

namespace Pipeline = CNA::Content::Pipeline;

namespace
{
    struct CommandLine
    {
        std::filesystem::path source;
        std::filesystem::path output;
        bool quiet = false;
    };

    struct BuildItem
    {
        std::filesystem::path source;
        std::filesystem::path output;
        std::string logicalName;
    };

    void PrintUsage()
    {
        std::cerr
            << "Usage: cna-content build <source-file-or-directory> -o <output> [--quiet]\n\n"
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
            else if (!argument.empty() && argument.native().front() ==
                                              std::filesystem::path("-").native().front())
            {
                throw std::invalid_argument("unknown option '" + argument.string() + "'.");
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
            throw std::runtime_error("cannot resolve path '" + path.string() + "': " +
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
        const std::u8string utf8 = withoutExtension.generic_u8string();
        return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
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
            return {{source, output, LogicalName(source.filename())}};
        }
        if (!std::filesystem::is_directory(source))
        {
            throw std::invalid_argument("source '" + source.string() +
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
            builds.push_back({entry.path(), std::move(output), LogicalName(relative)});
        }
        std::sort(builds.begin(), builds.end(), [](const BuildItem& left, const BuildItem& right)
        {
            return left.logicalName < right.logicalName;
        });
        return builds;
    }

    std::shared_ptr<const Pipeline::ContentPipelineRegistry> CreateRegistry()
    {
        auto registry = std::make_shared<Pipeline::ContentPipelineRegistry>();
        Pipeline::RegisterTexture2DContentPipeline(*registry);
        Pipeline::RegisterSoundEffectContentPipeline(*registry);
        Pipeline::RegisterModelContentPipeline(*registry);
        return registry;
    }

    std::string ReadText(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            throw std::runtime_error("cannot open '" + path.string() + "'.");
        }
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
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
                          << path.string() << "': " << error.what() << "\n";
            }
            return {};
        }
    }

    bool HasContentBuildDependency(const Pipeline::ContentBuildManifestEntry& entry)
    {
        return std::any_of(entry.dependencies.begin(), entry.dependencies.end(),
                           [](const Pipeline::ContentDependency& dependency)
        {
            return dependency.kind == Pipeline::ContentDependencyKind::ContentBuild;
        });
    }

    bool IsCurrentRoute(const Pipeline::ContentBuildManifestEntry& entry,
                        const Pipeline::ContentPipelineRegistry& registry,
                        const BuildItem& item,
                        const Pipeline::ContentProcessorParameters& parameters)
    {
        try
        {
            const std::shared_ptr<const Pipeline::ContentImporter> importer =
                registry.ResolveImporter(item.source);
            const std::shared_ptr<const Pipeline::ContentProcessor> processor =
                registry.ResolveProcessor(importer->OutputType());
            processor->ValidateParameters(parameters);
            const std::shared_ptr<const Pipeline::ContentTypeWriter> writer =
                registry.ResolveWriter(processor->OutputType());
            return entry.importer == importer->Identity() &&
                   entry.processor == processor->Identity() &&
                   entry.writer == writer->Identity() && entry.parameters == parameters;
        }
        catch (...)
        {
            return false;
        }
    }

    bool CanSkip(const Pipeline::ContentBuildManifestEntry& entry,
                 const Pipeline::ContentPipelineRegistry& registry,
                 const BuildItem& item, const std::filesystem::path& sourceRoot,
                 const std::filesystem::path& outputRoot,
                 const Pipeline::ContentProcessorParameters& parameters)
    {
        try
        {
            if (HasContentBuildDependency(entry)) { return false; }
            if (WeaklyCanonical(sourceRoot / entry.source) != WeaklyCanonical(item.source) ||
                WeaklyCanonical(outputRoot / entry.output) != WeaklyCanonical(item.output) ||
                !IsCurrentRoute(entry, registry, item, parameters))
            {
                return false;
            }
            if (Pipeline::ComputeContentBuildFingerprint(entry, sourceRoot) != entry.fingerprint ||
                !std::filesystem::is_regular_file(item.output) ||
                Pipeline::ContentFileSha256(item.output) != entry.outputSha256)
            {
                return false;
            }
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    int Run(const std::vector<std::filesystem::path>& arguments)
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

        const std::shared_ptr<const Pipeline::ContentPipelineRegistry> registry = CreateRegistry();
        std::filesystem::path sourceRoot;
        std::filesystem::path outputRoot;
        bool directoryBuild = false;
        std::vector<BuildItem> builds;
        try
        {
            builds = DiscoverBuilds(command, *registry, sourceRoot, outputRoot, directoryBuild);
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
        for (const BuildItem& item : builds)
        {
            try
            {
                const Pipeline::ContentBuildManifestEntry* previous =
                    previousManifest.Find(item.logicalName);
                const Pipeline::ContentProcessorParameters parameters;
                if (previous != nullptr &&
                    CanSkip(*previous, *registry, item, sourceRoot, outputRoot, parameters))
                {
                    nextManifest.Set(*previous);
                    ++skipped;
                    if (!command.quiet)
                    {
                        std::cout << "[SKIP] " << item.logicalName << " -> "
                                  << item.output.string() << "\n";
                    }
                    continue;
                }

                Pipeline::ContentBuildRequest request;
                request.sourceRoot = sourceRoot;
                request.source = item.source;
                request.logicalName = item.logicalName;
                request.parameters = parameters;
                Pipeline::ContentBuildResult result = pipeline.Build(request);

                Pipeline::ContentBuildManifestEntry manifestEntry =
                    Pipeline::MakeContentBuildManifestEntry(
                        result, sourceRoot, outputRoot, item.output);
                if (HasContentBuildDependency(manifestEntry))
                {
                    throw std::runtime_error(
                        "content-build dependencies require graph fingerprint scheduling, which "
                        "is not enabled in this serial CLI yet.");
                }
                manifestEntry.fingerprint =
                    Pipeline::ComputeContentBuildFingerprint(manifestEntry, sourceRoot);
                manifestEntry.outputSha256 = Pipeline::ContentSha256(result.output.bytes);

                try
                {
                    if (item.output.has_parent_path())
                    {
                        std::filesystem::create_directories(item.output.parent_path());
                    }
                    CNA::Tools::WriteFileAtomically(item.output, result.output.bytes);
                }
                catch (const std::exception& error)
                {
                    throw Pipeline::ContentPipelineError(
                        item.source, item.logicalName, Pipeline::ContentPipelineStage::Publish,
                        "CNA.AtomicPublisher", error.what());
                }

                nextManifest.Set(std::move(manifestEntry));

                ++built;
                if (!command.quiet)
                {
                    std::cout << "[BUILD] " << item.logicalName << " -> " << item.output.string()
                              << " (" << result.output.bytes.size() << " bytes; "
                              << result.importer.name << " -> " << result.processor.name << " -> "
                              << result.writer.name << ")\n";
                }
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

#if defined(_WIN32)
int wmain(int argc, wchar_t** argv)
{
    std::vector<std::filesystem::path> arguments;
    arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0u);
    for (int index = 1; index < argc; ++index) { arguments.emplace_back(argv[index]); }
    return Run(arguments);
}
#else
int main(int argc, char** argv)
{
    std::vector<std::filesystem::path> arguments;
    arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0u);
    for (int index = 1; index < argc; ++index) { arguments.emplace_back(argv[index]); }
    return Run(arguments);
}
#endif
