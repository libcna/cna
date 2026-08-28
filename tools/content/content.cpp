// SPDX-License-Identifier: MS-PL

#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "CNA/Content/Pipeline/ContentPipeline.hpp"
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
                                          std::filesystem::path& sourceRoot)
    {
        const std::filesystem::path source = WeaklyCanonical(command.source);
        if (std::filesystem::is_regular_file(source))
        {
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
            return {{source, output, LogicalName(source.filename())}};
        }
        if (!std::filesystem::is_directory(source))
        {
            throw std::invalid_argument("source '" + source.string() +
                                        "' is neither a regular file nor a directory.");
        }

        sourceRoot = source;
        const std::filesystem::path outputRoot = WeaklyCanonical(command.output);
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
        return registry;
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

        std::filesystem::path sourceRoot;
        std::vector<BuildItem> builds;
        try
        {
            builds = DiscoverBuilds(command, sourceRoot);
        }
        catch (const std::exception& error)
        {
            std::cerr << "error: " << error.what() << "\n";
            return 1;
        }

        const Pipeline::ContentPipeline pipeline(CreateRegistry());
        std::size_t built = 0u;
        std::size_t failed = 0u;
        for (const BuildItem& item : builds)
        {
            try
            {
                Pipeline::ContentBuildRequest request;
                request.sourceRoot = sourceRoot;
                request.source = item.source;
                request.logicalName = item.logicalName;
                Pipeline::ContentBuildResult result = pipeline.Build(request);

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

        if (!command.quiet || failed != 0u)
        {
            std::cout << "Built: " << built << "  Failed: " << failed << "\n";
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
