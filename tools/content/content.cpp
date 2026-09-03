// SPDX-License-Identifier: MS-PL

#include <algorithm>
#include <charconv>
#include <cmath>
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
#include "CNA/Content/Pipeline/EffectContentPipeline.hpp"
#include "CNA/Content/Pipeline/ModelContentPipeline.hpp"
#include "CNA/Content/Pipeline/SongContentPipeline.hpp"
#include "CNA/Content/Pipeline/SoundEffectContentPipeline.hpp"
#include "CNA/Content/Pipeline/SpriteFontContentPipeline.hpp"
#include "CNA/Content/Pipeline/TextureCompressionPipeline.hpp"
#include "CNA/Content/Pipeline/Texture2DContentPipeline.hpp"
#include "CNA/Content/Pipeline/VideoContentPipeline.hpp"
#include "CNA/Content/Pipeline/XnbContentPipeline.hpp"
#include "CNA/Content/Pipeline/XnbOutputContentPipeline.hpp"
#include "CNA/Internal/Xnb/XnbFileOptions.hpp"
#include "CNA/Internal/ContentPath.hpp"
#include "CNA/Internal/Json.hpp"
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
        bool explain = false;

        /** @brief Default compiled container for every asset this invocation builds. */
        Pipeline::ContentOutputFormat format = Pipeline::ContentOutputFormat::Cnb;

        /** @brief Container options applied to every `.xnb` this invocation writes. */
        CNA::Internal::Xnb::XnbFileOptions xnbOptions;
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
        Pipeline::ContentOutputFormat format = Pipeline::ContentOutputFormat::Cnb;
        Pipeline::ContentProcessorParameters parameters;
    };

    enum class ContentBuildReasonCode
    {
        ManifestUnavailable,
        ManifestIncompatible,
        ManifestCorrupt,
        NewAsset,
        LogicalIdentityChanged,
        PrimarySourceBytesChanged,
        ImporterIdentityChanged,
        ProcessorIdentityChanged,
        WriterIdentityChanged,
        WriterSchemaIdentityChanged,
        WriterCodecIdentityChanged,
        ProcessorParametersChanged,
        SourceDependencySetChanged,
        SourceDependencyBytesChanged,
        ContentDependencySetChanged,
        ContentDependencyFingerprintChanged,
        OutputDefinitionsChanged,
        DeploymentDefinitionsChanged,
        CompiledOutputMissing,
        CompiledOutputDigestMismatch,
        DeploymentOutputMissing,
        DeploymentOutputDigestMismatch,
        PublishedOutputUnsafe,
        DirectFingerprintChanged,
        EffectiveFingerprintChanged,
        FingerprintUnchanged,
    };

    struct ContentBuildReason
    {
        ContentBuildReasonCode code = ContentBuildReasonCode::NewAsset;
        std::string detail;

        bool operator==(const ContentBuildReason&) const = default;
    };

    struct ContentBuildDecision
    {
        std::vector<ContentBuildReason> reasons;
    };

    void NormalizeReasons(ContentBuildDecision& decision)
    {
        std::sort(decision.reasons.begin(), decision.reasons.end(),
                  [](const ContentBuildReason& left, const ContentBuildReason& right)
        {
            if (left.code != right.code) { return left.code < right.code; }
            return left.detail < right.detail;
        });
        decision.reasons.erase(
            std::unique(decision.reasons.begin(), decision.reasons.end()),
            decision.reasons.end());
    }

    const char* ContentBuildReasonText(ContentBuildReasonCode code)
    {
        switch (code)
        {
        case ContentBuildReasonCode::ManifestUnavailable:
            return "manifest unavailable";
        case ContentBuildReasonCode::ManifestIncompatible:
            return "manifest format changed or is incompatible";
        case ContentBuildReasonCode::ManifestCorrupt:
            return "manifest corrupt";
        case ContentBuildReasonCode::NewAsset:
            return "new asset";
        case ContentBuildReasonCode::LogicalIdentityChanged:
            return "logical asset/output identity changed";
        case ContentBuildReasonCode::PrimarySourceBytesChanged:
            return "primary source bytes changed";
        case ContentBuildReasonCode::ImporterIdentityChanged:
            return "importer identity/version changed";
        case ContentBuildReasonCode::ProcessorIdentityChanged:
            return "processor identity/version changed";
        case ContentBuildReasonCode::WriterIdentityChanged:
            return "writer identity/version changed";
        case ContentBuildReasonCode::WriterSchemaIdentityChanged:
            return "writer asset schema identity changed";
        case ContentBuildReasonCode::WriterCodecIdentityChanged:
            return "writer codec identity/version changed";
        case ContentBuildReasonCode::ProcessorParametersChanged:
            return "processor parameters changed";
        case ContentBuildReasonCode::SourceDependencySetChanged:
            return "source dependency set changed";
        case ContentBuildReasonCode::SourceDependencyBytesChanged:
            return "source dependency bytes changed";
        case ContentBuildReasonCode::ContentDependencySetChanged:
            return "content-build dependency set changed";
        case ContentBuildReasonCode::ContentDependencyFingerprintChanged:
            return "content-build dependency effective fingerprint changed";
        case ContentBuildReasonCode::OutputDefinitionsChanged:
            return "compiled output definition set changed";
        case ContentBuildReasonCode::DeploymentDefinitionsChanged:
            return "deployment definition set changed";
        case ContentBuildReasonCode::CompiledOutputMissing:
            return "compiled output missing";
        case ContentBuildReasonCode::CompiledOutputDigestMismatch:
            return "compiled output digest mismatch";
        case ContentBuildReasonCode::DeploymentOutputMissing:
            return "deployment output missing";
        case ContentBuildReasonCode::DeploymentOutputDigestMismatch:
            return "deployment output digest mismatch";
        case ContentBuildReasonCode::PublishedOutputUnsafe:
            return "published output path is unsafe or not a regular file";
        case ContentBuildReasonCode::DirectFingerprintChanged:
            return "direct fingerprint changed outside a persisted reason domain";
        case ContentBuildReasonCode::EffectiveFingerprintChanged:
            return "effective fingerprint changed outside a persisted reason domain";
        case ContentBuildReasonCode::FingerprintUnchanged:
            return "fingerprint and published output digests unchanged";
        }
        return "unknown build reason";
    }

    std::string FormatExplainedStatus(const std::string& status,
                                      ContentBuildDecision decision)
    {
        NormalizeReasons(decision);
        std::ostringstream output;
        output << status;
        for (const ContentBuildReason& reason : decision.reasons)
        {
            output << "\n  reason: " << ContentBuildReasonText(reason.code);
            if (!reason.detail.empty()) { output << ": " << reason.detail; }
        }
        return output.str();
    }

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
            << "Usage: cna-content build <source-file-or-directory> -o <output>\n"
               "         [--format cnb|xnb] [--config <file>] [--workers <1..64>]\n"
               "         [--xnb-platform <name>] [--xnb-version 4|5]\n"
               "         [--xnb-profile reach|hidef] [--xnb-compress none|lzx|lz4]\n"
               "         [--xnb-reader-names xna40|portable]\n"
               "         [--xnb-allow-unverified-xbox] [--explain] [--quiet]\n"
            << "       cna-content clean <output-directory> [--quiet]\n\n"
            << "Builds source content through Importer -> Processor -> Content Type Writer.\n"
            << "--format selects the compiled container: CNA's native .cnb (the default) or the\n"
            << "XNA-compatible .xnb. Importers and processors are the same either way; only the\n"
            << "writer differs, so every source route reaches both containers.\n\n"
            << "A single source file requires an output path whose extension matches --format.\n"
            << "A source directory requires an output directory; relative paths and logical\n"
            << "content names are preserved. Clean removes only unchanged files proven to be\n"
            << "pipeline-owned by a valid output manifest.\n\n"
            << "XNA 4.0 target platforms: windows, windowsphone, xbox360. Every other\n"
            << "--xnb-platform value (desktopgl, linux, ios, android, windowsgl) is an extended\n"
            << "XNB ecosystem identifier that Microsoft XNA 4.0 never produced or consumed.\n"
            << "--xnb-version 5 is the XNA 4.0-era container; 4 is earlier, legacy XNB.\n\n"
            << "--xnb-platform xbox360 is refused by default. The Xbox 360 is big-endian and\n"
               "this build has one piece of Xbox byte-order handling (the SoundEffect\n"
               "WAVEFORMATEX fields), so writing the 'x' header byte over the other payloads\n"
               "would claim a compatibility it cannot deliver. --xnb-allow-unverified-xbox\n"
               "produces one anyway, for testing on real hardware.\n";
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
        bool formatSpecified = false;
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
            else if (IsOption(argument, "--explain"))
            {
                command.explain = true;
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
            else if (IsOption(argument, "--format"))
            {
                if (++index >= arguments.size())
                {
                    throw std::invalid_argument("--format requires 'cnb' or 'xnb'.");
                }
                if (formatSpecified)
                {
                    throw std::invalid_argument("--format was specified more than once.");
                }
                const std::string name = CNA::Internal::ContentPathToUtf8(arguments[index]);
                if (!Pipeline::TryParseContentOutputFormat(name, command.format))
                {
                    throw std::invalid_argument("--format must be 'cnb' or 'xnb', not '" + name +
                                                "'.");
                }
                formatSpecified = true;
            }
            else if (IsOption(argument, "--xnb-allow-unverified-xbox"))
            {
                // plans/plan_xnapipeline.md XNAP-82: the escape hatch has to be reachable from
                // the tool, or the only way to produce a candidate Xbox file for somebody with
                // real hardware to test is to write a custom compiler.
                command.xnbOptions.allowUnverifiedXboxPayloads = true;
            }
            else if (IsOption(argument, "--xnb-platform"))
            {
                if (++index >= arguments.size())
                {
                    throw std::invalid_argument("--xnb-platform requires a platform name.");
                }
                const std::string name = CNA::Internal::ContentPathToUtf8(arguments[index]);
                if (!CNA::Internal::Xnb::TryParseXnbTargetPlatform(
                        name, command.xnbOptions.platform))
                {
                    std::string known;
                    for (const std::string& candidate :
                         CNA::Internal::Xnb::XnbTargetPlatformNames())
                    {
                        if (!known.empty()) { known += ", "; }
                        known += candidate;
                    }
                    throw std::invalid_argument("--xnb-platform '" + name +
                                                "' is not a known target; expected one of: " +
                                                known + ".");
                }
            }
            else if (IsOption(argument, "--xnb-version"))
            {
                if (++index >= arguments.size())
                {
                    throw std::invalid_argument("--xnb-version requires 4 or 5.");
                }
                const std::string name = CNA::Internal::ContentPathToUtf8(arguments[index]);
                if (name == "5")
                {
                    command.xnbOptions.version = CNA::Internal::Xnb::XnbContainerVersion::Xna40;
                }
                else if (name == "4")
                {
                    command.xnbOptions.version = CNA::Internal::Xnb::XnbContainerVersion::Legacy4;
                }
                else
                {
                    throw std::invalid_argument(
                        "--xnb-version must be 5 (the XNA 4.0-era container) or 4 (legacy), not '" +
                        name + "'.");
                }
            }
            else if (IsOption(argument, "--xnb-profile"))
            {
                if (++index >= arguments.size())
                {
                    throw std::invalid_argument("--xnb-profile requires 'reach' or 'hidef'.");
                }
                const std::string name = CNA::Internal::ContentPathToUtf8(arguments[index]);
                if (name == "reach")
                {
                    command.xnbOptions.graphicsProfile =
                        CNA::Internal::Xnb::XnbGraphicsProfile::Reach;
                }
                else if (name == "hidef")
                {
                    command.xnbOptions.graphicsProfile =
                        CNA::Internal::Xnb::XnbGraphicsProfile::HiDef;
                }
                else
                {
                    throw std::invalid_argument("--xnb-profile must be 'reach' or 'hidef', not '" +
                                                name + "'.");
                }
            }
            else if (IsOption(argument, "--xnb-compress"))
            {
                if (++index >= arguments.size())
                {
                    throw std::invalid_argument(
                        "--xnb-compress requires 'none', 'lzx' or 'lz4'.");
                }
                const std::string name = CNA::Internal::ContentPathToUtf8(arguments[index]);
                if (name == "none")
                {
                    command.xnbOptions.compression =
                        CNA::Internal::Xnb::XnbOutputCompression::None;
                }
                else if (name == "lzx")
                {
                    command.xnbOptions.compression = CNA::Internal::Xnb::XnbOutputCompression::Lzx;
                }
                else if (name == "lz4")
                {
                    command.xnbOptions.compression = CNA::Internal::Xnb::XnbOutputCompression::Lz4;
                }
                else
                {
                    throw std::invalid_argument(
                        "--xnb-compress must be 'none', 'lzx' or 'lz4', not '" + name + "'.");
                }
            }
            else if (IsOption(argument, "--xnb-reader-names"))
            {
                if (++index >= arguments.size())
                {
                    throw std::invalid_argument(
                        "--xnb-reader-names requires 'xna40' or 'portable'.");
                }
                const std::string name = CNA::Internal::ContentPathToUtf8(arguments[index]);
                if (name == "xna40")
                {
                    command.xnbOptions.readerNameStyle =
                        CNA::Internal::Xnb::XnbReaderNameStyle::Xna40;
                }
                else if (name == "portable")
                {
                    command.xnbOptions.readerNameStyle =
                        CNA::Internal::Xnb::XnbReaderNameStyle::Portable;
                }
                else
                {
                    throw std::invalid_argument(
                        "--xnb-reader-names must be 'xna40' or 'portable', not '" + name + "'.");
                }
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
        CNA::Internal::Xnb::ValidateXnbFileOptions(command.xnbOptions);
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
            const std::string extension =
                Pipeline::ContentOutputFormatExtension(command.format);
            if (command.output.extension() != std::filesystem::path(extension))
            {
                throw std::invalid_argument(
                    "a single source file built as " +
                    std::string(Pipeline::ContentOutputFormatName(command.format)) +
                    " requires an output path ending in '" + extension + "'.");
            }
            const std::filesystem::path output = WeaklyCanonical(command.output);
            if (output == source)
            {
                throw std::invalid_argument("the output path must not replace the source file.");
            }
            sourceRoot = source.parent_path();
            outputRoot = output.parent_path();
            BuildItem item{source, output,
                           CNA::Internal::ContentPathToUtf8(source.filename()),
                           LogicalName(source.filename())};
            item.format = command.format;
            return {std::move(item)};
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
            output.replace_extension(Pipeline::ContentOutputFormatExtension(command.format));
            BuildItem item{entry.path(), std::move(output),
                           CNA::Internal::ContentPathToUtf8(relative), LogicalName(relative)};
            item.format = command.format;
            builds.push_back(std::move(item));
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
            // Container selection narrows from the command line, to the project-wide default,
            // to the one asset: each level only overrides the one above it when it says so.
            if (configuration.OutputFormat().has_value())
            {
                item.format = *configuration.OutputFormat();
            }
            const Pipeline::ContentAssetBuildConfiguration* entry =
                configuration.Find(item.relativeSource);
            if (entry != nullptr)
            {
                if (!entry->logicalName.empty()) { item.logicalName = entry->logicalName; }
                item.importer = entry->importer;
                item.processor = entry->processor;
                item.writer = entry->writer;
                item.parameters = entry->parameters;
                if (entry->outputFormat.has_value()) { item.format = *entry->outputFormat; }
            }
            if (directoryBuild)
            {
                if (entry != nullptr && !entry->logicalName.empty())
                {
                    item.output = outputRoot /
                                  CNA::Internal::ContentPathFromUtf8(item.logicalName);
                    item.output += Pipeline::ContentOutputFormatExtension(item.format);
                }
                else
                {
                    item.output.replace_extension(
                        Pipeline::ContentOutputFormatExtension(item.format));
                }
            }
            else if (item.output.extension() !=
                     std::filesystem::path(Pipeline::ContentOutputFormatExtension(item.format)))
            {
                throw std::runtime_error(
                    "content configuration asset '" + item.relativeSource + "' selects " +
                    Pipeline::ContentOutputFormatName(item.format) +
                    " output, but the requested output path ends in '" +
                    CNA::Internal::ContentPathToUtf8(item.output.extension()) + "'.");
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

    void RequireExternalRootsSeparateFromOutput(
        const std::filesystem::path& outputRoot,
        const Pipeline::ContentSourceRootCapabilities& externalSourceRoots)
    {
        const std::filesystem::path canonicalOutputRoot = WeaklyCanonical(outputRoot);
        for (const auto& [alias, externalRoot] : externalSourceRoots.Entries())
        {
            if (IsWithin(canonicalOutputRoot, externalRoot) ||
                IsWithin(externalRoot, canonicalOutputRoot))
            {
                throw std::runtime_error(
                    "content output root must not equal, contain, or be contained by external "
                    "source root '" + alias + "'.");
            }
        }
    }

    enum class ManifestLoadState
    {
        Missing,
        Current,
        Incompatible,
        Corrupt,
    };

    struct LoadedManifest
    {
        Pipeline::ContentBuildManifest manifest;
        std::string original;
        ManifestLoadState state = ManifestLoadState::Missing;
    };

    bool HasIncompatibleManifestVersion(const std::string& json)
    {
        try
        {
            const CNA::Internal::JsonValue root = CNA::Internal::ParseJson(json);
            if (root.type != CNA::Internal::JsonType::Object) { return false; }
            const CNA::Internal::JsonValue* version = root.FindMember("version");
            if (version == nullptr || version->type != CNA::Internal::JsonType::Number ||
                !std::isfinite(version->numberValue) ||
                std::floor(version->numberValue) != version->numberValue ||
                version->numberValue < 0.0)
            {
                return false;
            }
            return version->numberValue !=
                   static_cast<double>(Pipeline::ContentBuildManifestVersion);
        }
        catch (...)
        {
            return false;
        }
    }

    LoadedManifest LoadManifest(const std::filesystem::path& path, bool quiet)
    {
        LoadedManifest result;
        try
        {
            if (!std::filesystem::exists(path)) { return result; }
            result.original = ReadText(path);
            result.manifest = Pipeline::ContentBuildManifest::Parse(result.original);
            result.state = ManifestLoadState::Current;
            return result;
        }
        catch (const std::exception& error)
        {
            result.state = HasIncompatibleManifestVersion(result.original)
                               ? ManifestLoadState::Incompatible
                               : ManifestLoadState::Corrupt;
            result.original.clear();
            if (!quiet)
            {
                std::cout << "[WARN] Ignoring incompatible or corrupt manifest '"
                          << CNA::Internal::ContentPathToUtf8(path) << "': " << error.what()
                          << "\n";
            }
            return result;
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
                            item.writer.empty() ? entry.writer.name : item.writer,
                            item.format);
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
                                const std::filesystem::path& outputRoot,
                                const Pipeline::ContentSourceRootCapabilities&
                                    externalSourceRoots)
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
            return Pipeline::ComputeContentBuildDirectFingerprint(
                       entry, sourceRoot, externalSourceRoots) ==
                   entry.directFingerprint;
        }
        catch (...)
        {
            return false;
        }
    }

    std::string ComponentIdentityText(const Pipeline::ContentComponentIdentity& identity)
    {
        return identity.name + "/" + identity.version;
    }

    ContentBuildDecision CompareDirectBuildState(
        const Pipeline::ContentBuildManifestEntry& previous,
        const Pipeline::ContentBuildManifestEntry& current)
    {
        ContentBuildDecision decision;
        const auto add = [&](ContentBuildReasonCode code, std::string detail = {})
        {
            decision.reasons.push_back({code, std::move(detail)});
        };
        if (previous.nodeId != current.nodeId || previous.source != current.source)
        {
            add(ContentBuildReasonCode::LogicalIdentityChanged,
                previous.nodeId + " (" + previous.source + ") -> " + current.nodeId + " (" +
                    current.source + ")");
        }
        if (previous.importer != current.importer)
        {
            add(ContentBuildReasonCode::ImporterIdentityChanged,
                ComponentIdentityText(previous.importer) + " -> " +
                    ComponentIdentityText(current.importer));
        }
        if (previous.processor != current.processor)
        {
            add(ContentBuildReasonCode::ProcessorIdentityChanged,
                ComponentIdentityText(previous.processor) + " -> " +
                    ComponentIdentityText(current.processor));
        }
        if (previous.writer != current.writer)
        {
            add(ContentBuildReasonCode::WriterIdentityChanged,
                ComponentIdentityText(previous.writer) + " -> " +
                    ComponentIdentityText(current.writer));
        }

        std::vector<Pipeline::ContentWriterSchemaIdentity> previousSchemas =
            previous.writerSchemas;
        std::vector<Pipeline::ContentWriterSchemaIdentity> currentSchemas = current.writerSchemas;
        const auto schemaOrder = [](const Pipeline::ContentWriterSchemaIdentity& left,
                                    const Pipeline::ContentWriterSchemaIdentity& right)
        {
            if (left.assetTypeId != right.assetTypeId)
            {
                return left.assetTypeId < right.assetTypeId;
            }
            return left.assetTypeName < right.assetTypeName;
        };
        std::sort(previousSchemas.begin(), previousSchemas.end(), schemaOrder);
        std::sort(currentSchemas.begin(), currentSchemas.end(), schemaOrder);
        bool schemaIdentityChanged = previousSchemas.size() != currentSchemas.size();
        bool codecIdentityChanged = false;
        for (std::size_t index = 0u;
             index < std::min(previousSchemas.size(), currentSchemas.size()); ++index)
        {
            const auto& oldSchema = previousSchemas[index];
            const auto& newSchema = currentSchemas[index];
            schemaIdentityChanged =
                schemaIdentityChanged || oldSchema.assetTypeId != newSchema.assetTypeId ||
                oldSchema.assetSchemaVersion != newSchema.assetSchemaVersion ||
                oldSchema.assetTypeName != newSchema.assetTypeName;
            codecIdentityChanged = codecIdentityChanged || oldSchema.codec != newSchema.codec;
        }
        if (schemaIdentityChanged)
        {
            add(ContentBuildReasonCode::WriterSchemaIdentityChanged);
        }
        if (codecIdentityChanged)
        {
            add(ContentBuildReasonCode::WriterCodecIdentityChanged);
        }

        const Pipeline::ContentBuildFingerprintState& oldState = previous.fingerprintState;
        const Pipeline::ContentBuildFingerprintState& newState = current.fingerprintState;
        if (oldState.primarySourceBytes != newState.primarySourceBytes)
        {
            add(ContentBuildReasonCode::PrimarySourceBytesChanged, current.source);
        }
        if (oldState.processorParameters != newState.processorParameters)
        {
            add(ContentBuildReasonCode::ProcessorParametersChanged);
        }
        if (oldState.writerSchemas != newState.writerSchemas &&
            !schemaIdentityChanged && !codecIdentityChanged)
        {
            add(ContentBuildReasonCode::WriterSchemaIdentityChanged);
        }
        if (oldState.sourceDependencySet != newState.sourceDependencySet)
        {
            add(ContentBuildReasonCode::SourceDependencySetChanged);
        }
        else if (oldState.sourceDependencyBytes != newState.sourceDependencyBytes)
        {
            add(ContentBuildReasonCode::SourceDependencyBytesChanged);
        }
        if (oldState.contentDependencySet != newState.contentDependencySet)
        {
            add(ContentBuildReasonCode::ContentDependencySetChanged);
        }
        if (oldState.outputDefinitions != newState.outputDefinitions)
        {
            add(ContentBuildReasonCode::OutputDefinitionsChanged);
        }
        if (oldState.deploymentDefinitions != newState.deploymentDefinitions)
        {
            add(ContentBuildReasonCode::DeploymentDefinitionsChanged);
        }
        if (previous.directFingerprint != current.directFingerprint &&
            decision.reasons.empty())
        {
            add(ContentBuildReasonCode::DirectFingerprintChanged);
        }
        NormalizeReasons(decision);
        return decision;
    }

    const Pipeline::ContentBuildManifestEntry* FindPreviousEntryForReason(
        const Pipeline::ContentBuildManifest& manifest, const BuildItem& item)
    {
        if (const Pipeline::ContentBuildManifestEntry* exact =
                manifest.Find(item.logicalName))
        {
            return exact;
        }
        const Pipeline::ContentBuildManifestEntry* match = nullptr;
        for (const auto& [nodeId, entry] : manifest.Entries())
        {
            static_cast<void>(nodeId);
            if (entry.source != item.relativeSource) { continue; }
            if (match != nullptr) { return nullptr; }
            match = &entry;
        }
        return match;
    }

    ContentBuildDecision InitialBuildDecision(
        ManifestLoadState manifestState,
        const Pipeline::ContentBuildManifestEntry* previousForReason,
        const Pipeline::ContentBuildManifestEntry& current)
    {
        if (previousForReason != nullptr)
        {
            return CompareDirectBuildState(*previousForReason, current);
        }
        ContentBuildDecision decision;
        switch (manifestState)
        {
        case ManifestLoadState::Missing:
            decision.reasons.push_back(
                {ContentBuildReasonCode::ManifestUnavailable, {}});
            decision.reasons.push_back({ContentBuildReasonCode::NewAsset, current.source});
            break;
        case ManifestLoadState::Incompatible:
            decision.reasons.push_back(
                {ContentBuildReasonCode::ManifestIncompatible, {}});
            break;
        case ManifestLoadState::Corrupt:
            decision.reasons.push_back({ContentBuildReasonCode::ManifestCorrupt, {}});
            break;
        case ManifestLoadState::Current:
            decision.reasons.push_back({ContentBuildReasonCode::NewAsset, current.source});
            break;
        }
        NormalizeReasons(decision);
        return decision;
    }

    ContentBuildReason AssessPublishedOutput(
        const std::filesystem::path& outputRoot, const std::string& relativePath,
        const std::string& expectedDigest, ContentBuildReasonCode missing,
        ContentBuildReasonCode mismatch)
    {
        try
        {
            const std::filesystem::path path =
                outputRoot / CNA::Internal::ContentPathFromUtf8(relativePath);
            std::error_code error;
            const std::filesystem::file_status status =
                std::filesystem::symlink_status(path, error);
            if (error == std::errc::no_such_file_or_directory ||
                status.type() == std::filesystem::file_type::not_found)
            {
                return {missing, relativePath};
            }
            if (error || std::filesystem::is_symlink(status) ||
                !std::filesystem::is_regular_file(status))
            {
                return {ContentBuildReasonCode::PublishedOutputUnsafe, relativePath};
            }
            const std::filesystem::path canonicalPath = WeaklyCanonical(path);
            if (!IsWithin(WeaklyCanonical(outputRoot), canonicalPath))
            {
                return {ContentBuildReasonCode::PublishedOutputUnsafe, relativePath};
            }
            if (Pipeline::ContentFileSha256(canonicalPath) != expectedDigest)
            {
                return {mismatch, relativePath};
            }
            return {};
        }
        catch (...)
        {
            return {ContentBuildReasonCode::PublishedOutputUnsafe, relativePath};
        }
    }

    ContentBuildDecision AssessEffectiveBuildState(
        const Pipeline::ContentBuildManifestEntry& entry,
        const std::filesystem::path& outputRoot,
        const std::map<std::string, std::string>& contentBuildFingerprints)
    {
        ContentBuildDecision decision;
        try
        {
            Pipeline::ContentBuildManifestEntry current = entry;
            Pipeline::RefreshContentBuildEffectiveFingerprint(
                current, contentBuildFingerprints);
            if (current.fingerprintState.contentDependencyFingerprints !=
                entry.fingerprintState.contentDependencyFingerprints)
            {
                decision.reasons.push_back(
                    {ContentBuildReasonCode::ContentDependencyFingerprintChanged, {}});
            }
            if (current.fingerprint != entry.fingerprint && decision.reasons.empty())
            {
                decision.reasons.push_back(
                    {ContentBuildReasonCode::EffectiveFingerprintChanged, {}});
            }
            for (const Pipeline::ContentBuildManifestOutput& output : entry.outputs)
            {
                ContentBuildReason reason = AssessPublishedOutput(
                    outputRoot, output.path, output.sha256,
                    ContentBuildReasonCode::CompiledOutputMissing,
                    ContentBuildReasonCode::CompiledOutputDigestMismatch);
                if (!reason.detail.empty())
                {
                    decision.reasons.push_back(std::move(reason));
                }
            }
            for (const Pipeline::ContentBuildManifestDeploymentFile& deployment :
                 entry.deploymentFiles)
            {
                ContentBuildReason reason = AssessPublishedOutput(
                    outputRoot, deployment.path, deployment.sha256,
                    ContentBuildReasonCode::DeploymentOutputMissing,
                    ContentBuildReasonCode::DeploymentOutputDigestMismatch);
                if (!reason.detail.empty())
                {
                    decision.reasons.push_back(std::move(reason));
                }
            }
        }
        catch (...)
        {
            decision.reasons.push_back(
                {ContentBuildReasonCode::EffectiveFingerprintChanged, {}});
        }
        NormalizeReasons(decision);
        return decision;
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
        ContentBuildDecision decision;
        bool prepared = false;
        bool hasManifest = false;
        std::string failure;

        /** @brief Diagnostics the pipeline emitted while this node was built. */
        std::vector<Pipeline::ContentLogMessage> messages;
    };

    struct BuildNodeOutcome
    {
        Pipeline::ContentBuildManifestEntry manifest;
        bool success = false;
        bool skipped = false;
        ContentBuildDecision decision;
        std::string failure;
        std::string statusLine;

        /** @brief Diagnostics the pipeline emitted while this node was built. */
        std::vector<Pipeline::ContentLogMessage> messages;
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
        ManifestLoadState manifestState,
        const std::filesystem::path& sourceRoot, const std::filesystem::path& outputRoot,
        const Pipeline::ContentSourceRootCapabilities& externalSourceRoots,
        const std::filesystem::path& stagingRoot)
    {
        BuildNodePlan plan;
        plan.item = &item;
        try
        {
            const Pipeline::ContentBuildManifestEntry* previous =
                previousManifest.Find(item.logicalName);
            const Pipeline::ContentBuildManifestEntry* previousForReason =
                FindPreviousEntryForReason(previousManifest, item);
            if (previous != nullptr &&
                IsPreviousGraphCurrent(*previous, registry, item, sourceRoot, outputRoot,
                                       externalSourceRoots))
            {
                plan.manifest = *previous;
                plan.hasManifest = true;
                return plan;
            }

            Pipeline::ContentBuildRequest request;
            request.sourceRoot = sourceRoot;
            request.source = item.source;
            request.externalSourceRoots = externalSourceRoots;
            request.logicalName = item.logicalName;
            request.importer = item.importer;
            request.processor = item.processor;
            request.writer = item.writer;
            request.outputFormat = item.format;
            request.parameters = item.parameters;
            Pipeline::ContentBuildResult result = pipeline.Build(request);
            plan.messages = result.messages;

            plan.manifest = Pipeline::MakeContentBuildManifestEntry(
                result, sourceRoot, outputRoot, item.output, externalSourceRoots);
            Pipeline::RefreshContentBuildDirectFingerprint(
                plan.manifest, sourceRoot, externalSourceRoots);
            plan.decision =
                InitialBuildDecision(manifestState, previousForReason, plan.manifest);
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
                        nodeStage / (std::to_string(outputIndex) + ".stage");
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
        const Pipeline::ContentSourceRootCapabilities& externalSourceRoots,
        const std::map<std::string, std::string>& effectiveFingerprints)
    {
        BuildNodeOutcome outcome;
        const BuildItem& item = *plan.item;
        outcome.decision = plan.decision;
        try
        {
            if (plan.prepared)
            {
                outcome.manifest = plan.manifest;
                Pipeline::RefreshContentBuildEffectiveFingerprint(
                    outcome.manifest, effectiveFingerprints);
                if (outcome.decision.reasons.empty())
                {
                    outcome.decision.reasons.push_back(
                        {ContentBuildReasonCode::DirectFingerprintChanged, {}});
                }
                try
                {
                    const std::uintmax_t outputBytes = PublishStagedResult(plan, outputRoot);
                    outcome.statusLine = BuildStatusLine(item, outcome.manifest, outputBytes);
                    outcome.messages = plan.messages;
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

            outcome.decision = AssessEffectiveBuildState(
                plan.manifest, outputRoot, effectiveFingerprints);
            if (outcome.decision.reasons.empty())
            {
                outcome.manifest = plan.manifest;
                outcome.success = true;
                outcome.skipped = true;
                outcome.decision.reasons.push_back(
                    {ContentBuildReasonCode::FingerprintUnchanged, {}});
                outcome.statusLine = "[SKIP] " + item.logicalName + " -> " +
                                     CNA::Internal::ContentPathToUtf8(item.output);
                return outcome;
            }

            Pipeline::ContentBuildRequest request;
            request.sourceRoot = sourceRoot;
            request.source = item.source;
            request.externalSourceRoots = externalSourceRoots;
            request.logicalName = item.logicalName;
            request.importer = item.importer;
            request.processor = item.processor;
            request.writer = item.writer;
            request.outputFormat = item.format;
            request.parameters = item.parameters;
            Pipeline::ContentBuildResult result = pipeline.Build(request);
            outcome.manifest = Pipeline::MakeContentBuildManifestEntry(
                result, sourceRoot, outputRoot, item.output, externalSourceRoots);
            Pipeline::RefreshContentBuildDirectFingerprint(
                outcome.manifest, sourceRoot, externalSourceRoots);
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
            Pipeline::RefreshContentBuildEffectiveFingerprint(
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
            outcome.messages = result.messages;
            outcome.success = true;
        }
        catch (const std::exception& error)
        {
            outcome.failure = error.what();
        }
        return outcome;
    }

    int Run(const std::vector<std::filesystem::path>& arguments,
            std::shared_ptr<Pipeline::ContentPipelineRegistry> mutableRegistry)
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

        // The XNB writers are bound to the container options this invocation selected, which is
        // what puts those options into each writer's own build version and therefore into the
        // incremental manifest: rebuilding for another platform or container version invalidates
        // the previous artifacts instead of silently reusing them. They are registered whatever
        // --format says, because a project file may still select xnb for individual assets, and
        // writer resolution is keyed by (format, processed type) so they never shadow the CNB
        // writers. Importers and processors are format-neutral and the caller already registered
        // them.
        try
        {
            Pipeline::RegisterXnbOutputContentPipeline(*mutableRegistry, command.xnbOptions);
        }
        catch (const std::exception& error)
        {
            std::cerr << "error: " << error.what() << "\n";
            return 2;
        }
        mutableRegistry->Freeze();
        std::shared_ptr<const Pipeline::ContentPipelineRegistry> registry =
            std::move(mutableRegistry);

        std::filesystem::path sourceRoot;
        std::filesystem::path outputRoot;
        Pipeline::ContentSourceRootCapabilities externalSourceRoots;
        bool directoryBuild = false;
        std::vector<BuildItem> builds;
        try
        {
            builds = DiscoverBuilds(command, *registry, sourceRoot, outputRoot, directoryBuild);
            const Pipeline::ContentBuildConfiguration configuration =
                LoadConfiguration(command, sourceRoot);
            externalSourceRoots = Pipeline::ResolveContentSourceRootCapabilities(
                sourceRoot, configuration.SourceRoots());
            RequireExternalRootsSeparateFromOutput(outputRoot, externalSourceRoots);
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
        const LoadedManifest loadedManifest = LoadManifest(manifestPath, command.quiet);
        const Pipeline::ContentBuildManifest& previousManifest = loadedManifest.manifest;
        const std::string& originalManifest = loadedManifest.original;
        const bool previousOwnershipTrusted =
            loadedManifest.state == ManifestLoadState::Current;
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
                        builds[offset], offset, pipeline, *registry, previousManifest,
                        loadedManifest.state, sourceRoot, outputRoot, externalSourceRoots,
                        staging->Path());
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
                                loadedManifest.state, sourceRoot, outputRoot,
                                externalSourceRoots, staging->Path());
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
                    externalSourceRoots, effectiveFingerprints));
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
                                    externalSourceRoots, effectiveFingerprints);
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
                    if (command.explain)
                    {
                        outcome.statusLine = FormatExplainedStatus(
                            outcome.statusLine, std::move(outcome.decision));
                    }
                    events.push_back({false, std::move(outcome.statusLine)});
                    // A pipeline warning names something the author lost -- a material downgraded
                    // for a container that cannot hold it, an animation an output format has no
                    // place for. Collecting it in the build result and never showing it would
                    // leave that discovery to run time.
                    for (const Pipeline::ContentLogMessage& message : outcome.messages)
                    {
                        if (message.level == Pipeline::ContentLogLevel::Info) { continue; }
                        events.push_back(
                            {false,
                             std::string("  ") +
                                 (message.level == Pipeline::ContentLogLevel::Error ? "error"
                                                                                    : "warning") +
                                 " (" + message.component + "): " + message.text});
                    }
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
        RegisterTexture2DContentPipeline(registry, MakeBlockCompressionTextureEncoder());
        RegisterSoundEffectContentPipeline(registry);
        RegisterSongContentPipeline(registry);
        RegisterVideoContentPipeline(registry);
        RegisterModelContentPipeline(registry);
        RegisterCnjContentPipeline(registry);
        RegisterXnbContentPipeline(registry);
        RegisterSpriteFontSourceContentPipeline(registry);
        RegisterCompiledEffectContentPipeline(registry);
    }

    int RunContentCompiler(const std::vector<std::filesystem::path>& arguments,
                           std::shared_ptr<ContentPipelineRegistry> registry)
    {
        if (registry == nullptr)
        {
            throw std::invalid_argument("RunContentCompiler(): registry must not be null.");
        }
        return Run(arguments, std::move(registry));
    }
} // namespace CNA::Content::Pipeline
