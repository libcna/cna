// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Tasks/BuildContent.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <sstream>

#include "CNA/Content/Pipeline/ContentBuildConfiguration.hpp"
#include "CNA/Content/Pipeline/ContentBuildManifest.hpp"
#include "CNA/Content/Pipeline/ContentCompiler.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Tasks/XnaComponentNames.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Tasks
{
    namespace Canon = CNA::Content::Pipeline;

    // XNA's own value, verbatim: the template a host formats with the content project's GUID.
    const std::string BuildContent::CancelEventNameFormat =
        "Local\\Microsoft.Xna.GameStudio.ContentPipeline.CancelBuildEvent+{0}";

    namespace TaskDetail
    {
        /** @brief Splits `a=b;c=d`, which is how a `.contentproj` writes processor parameters. */
        [[nodiscard]] std::vector<std::pair<std::string, std::string>> SplitParameters(
            const std::string& text)
        {
            std::vector<std::pair<std::string, std::string>> parameters;
            std::istringstream stream(text);
            std::string entry;
            while (std::getline(stream, entry, ';'))
            {
                const std::size_t equals = entry.find('=');
                if (equals == std::string::npos || equals == 0u)
                {
                    continue;
                }
                parameters.emplace_back(entry.substr(0, equals), entry.substr(equals + 1u));
            }
            return parameters;
        }

        /** @brief A path as the configuration file spells it: root-relative, forward slashes. */
        [[nodiscard]] std::string RootRelative(const std::filesystem::path& root,
                                               const std::filesystem::path& path)
        {
            std::error_code error;
            std::filesystem::path relative = std::filesystem::relative(path, root, error);
            if (error || relative.empty() || *relative.begin() == "..")
            {
                relative = path.filename();
            }
            std::string text = relative.generic_string();
            return text;
        }

        /**
         * @brief The configuration type for a value a project wrote as plain text.
         *
         * `ProcessorParameters` metadata is untyped, and the canonical configuration is typed, so
         * the text decides: `true`/`false` is a boolean, digits are an integer, digits with a
         * point are a double, and anything else -- a colour key, an enum name -- is a string. A
         * value that is typed wrongly is refused by the processor that reads it, naming the
         * parameter, which is a better failure than a silent coercion.
         */
        [[nodiscard]] std::string ParameterType(const std::string& value)
        {
            std::string lowered(value);
            std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                           [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lowered == "true" || lowered == "false")
            {
                return "bool";
            }
            if (value.empty())
            {
                return "string";
            }
            bool digits = false;
            bool point = false;
            for (std::size_t i = 0; i < value.size(); ++i)
            {
                const char c = value[i];
                if (std::isdigit(static_cast<unsigned char>(c)) != 0)
                {
                    digits = true;
                }
                else if (c == '.' && !point)
                {
                    point = true;
                }
                else if (!((c == '-' || c == '+') && i == 0))
                {
                    return "string";
                }
            }
            if (!digits)
            {
                return "string";
            }
            return point ? "f64" : "i64";
        }

        /** @brief A GUID reduced to what a file name may hold. */
        [[nodiscard]] std::string Sanitize(const std::string& text)
        {
            std::string out;
            for (const char c : text)
            {
                if (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_')
                {
                    out += c;
                }
            }
            return out.empty() ? std::string("project") : out;
        }

        /** @brief JSON string escaping, for the configuration this task writes. */
        [[nodiscard]] std::string Escape(const std::string& text)
        {
            std::string out;
            for (const char c : text)
            {
                if (c == '"' || c == '\\')
                {
                    out += '\\';
                    out += c;
                }
                else if (static_cast<unsigned char>(c) < 0x20)
                {
                    static const char* const digits = "0123456789ABCDEF";
                    out += "\\u00";
                    out += digits[(static_cast<unsigned char>(c) >> 4) & 0xF];
                    out += digits[static_cast<unsigned char>(c) & 0xF];
                }
                else
                {
                    out += c;
                }
            }
            return out;
        }
    }

    const std::string& BuildContent::getBuildConfigurationProperty() const noexcept
    {
        return buildConfiguration_;
    }

    void BuildContent::setBuildConfigurationProperty(std::string value)
    {
        buildConfiguration_ = std::move(value);
    }

    bool BuildContent::getCompressContentProperty() const noexcept { return compressContent_; }

    void BuildContent::setCompressContentProperty(const bool value) noexcept { compressContent_ = value; }

    const std::string& BuildContent::getContentProjectGUIDProperty() const noexcept
    {
        return contentProjectGuid_;
    }

    void BuildContent::setContentProjectGUIDProperty(std::string value)
    {
        contentProjectGuid_ = std::move(value);
    }

    const std::string& BuildContent::getIntermediateDirectoryProperty() const noexcept
    {
        return intermediateDirectory_;
    }

    void BuildContent::setIntermediateDirectoryProperty(std::string value)
    {
        intermediateDirectory_ = std::move(value);
    }

    const std::vector<TaskItem>& BuildContent::getIntermediateFilesProperty() const noexcept
    {
        return intermediateFiles_;
    }

    const std::string& BuildContent::getLoggerRootDirectoryProperty() const noexcept
    {
        return loggerRootDirectory_;
    }

    void BuildContent::setLoggerRootDirectoryProperty(std::string value)
    {
        loggerRootDirectory_ = std::move(value);
    }

    const std::vector<TaskItem>& BuildContent::getOutputContentFilesProperty() const noexcept
    {
        return outputContentFiles_;
    }

    const std::string& BuildContent::getOutputDirectoryProperty() const noexcept
    {
        return outputDirectory_;
    }

    void BuildContent::setOutputDirectoryProperty(std::string value)
    {
        outputDirectory_ = std::move(value);
    }

    const std::vector<TaskItem>& BuildContent::getPipelineAssembliesProperty() const noexcept
    {
        return pipelineAssemblies_;
    }

    void BuildContent::setPipelineAssembliesProperty(std::vector<TaskItem> value)
    {
        pipelineAssemblies_ = std::move(value);
    }

    const std::vector<TaskItem>& BuildContent::getPipelineAssemblyDependenciesProperty() const noexcept
    {
        return pipelineAssemblyDependencies_;
    }

    void BuildContent::setPipelineAssemblyDependenciesProperty(std::vector<TaskItem> value)
    {
        pipelineAssemblyDependencies_ = std::move(value);
    }

    bool BuildContent::getRebuildAllProperty() const noexcept { return rebuildAll_; }

    void BuildContent::setRebuildAllProperty(const bool value) noexcept { rebuildAll_ = value; }

    const std::vector<TaskItem>& BuildContent::getRebuiltContentFilesProperty() const noexcept
    {
        return rebuiltContentFiles_;
    }

    const std::string& BuildContent::getRootDirectoryProperty() const noexcept { return rootDirectory_; }

    void BuildContent::setRootDirectoryProperty(std::string value) { rootDirectory_ = std::move(value); }

    const std::vector<TaskItem>& BuildContent::getSourceAssetsProperty() const noexcept
    {
        return sourceAssets_;
    }

    void BuildContent::setSourceAssetsProperty(std::vector<TaskItem> value)
    {
        sourceAssets_ = std::move(value);
    }

    const std::string& BuildContent::getTargetPlatformProperty() const noexcept { return targetPlatform_; }

    void BuildContent::setTargetPlatformProperty(std::string value)
    {
        targetPlatform_ = std::move(value);
    }

    const std::string& BuildContent::getTargetProfileProperty() const noexcept { return targetProfile_; }

    void BuildContent::setTargetProfileProperty(std::string value) { targetProfile_ = std::move(value); }

    bool BuildContent::Execute()
    {
        intermediateFiles_.clear();
        outputContentFiles_.clear();
        rebuiltContentFiles_.clear();

        if (rootDirectory_.empty() || outputDirectory_.empty())
        {
            LogError("BuildContent needs both RootDirectory and OutputDirectory before it can run.");
            return false;
        }
        if (!pipelineAssemblies_.empty())
        {
            // Refusing beats ignoring. A project naming its own pipeline assembly expects its own
            // importers to run, and a C++ build has no assembly to load; accepting the list
            // silently would let the project believe they did.
            LogError("BuildContent cannot load pipeline assemblies: C++ has no assembly loading, so "
                     "a custom importer, processor or writer is registered in code with "
                     "RegisterXnaImporter/RegisterXnaProcessor before the build runs, and the "
                     "PipelineAssemblies item has no counterpart. " +
                     std::to_string(pipelineAssemblies_.size()) + " were named.");
            return false;
        }
        if (sourceAssets_.empty())
        {
            // Not a failure: a content project with nothing in it builds nothing, and MSBuild's
            // own task answers true for an empty item list. Checked *after* the assembly refusal,
            // because a project that names its own pipeline assembly is wrong whether or not it
            // also lists assets.
            LogMessage("BuildContent: no source assets, so nothing was built.");
            return true;
        }

        const std::filesystem::path root(rootDirectory_);
        const std::filesystem::path output(outputDirectory_);
        const std::filesystem::path intermediate(
            intermediateDirectory_.empty() ? (output / "obj") : std::filesystem::path(intermediateDirectory_));

        std::error_code error;
        std::filesystem::create_directories(intermediate, error);
        if (error)
        {
            LogError("BuildContent could not create the intermediate directory \"" +
                     intermediate.string() + "\": " + error.message() + ".");
            return false;
        }

        // The source assets and their metadata become the one thing the canonical coordinator
        // already understands: a build configuration naming, per source, the importer, the
        // processor, the logical name and the processor parameters. This is the whole of the
        // translation; the build itself is the coordinator's, not this task's.
        std::ostringstream configuration;
        configuration << "{\n \"format\": \"CNA.ContentPipeline.Config\",\n \"version\": "
                      << Canon::ContentBuildConfigurationVersion << ",\n \"assets\": {\n";
        bool firstAsset = true;
        for (const TaskItem& asset : sourceAssets_)
        {
            const std::filesystem::path spec(asset.getItemSpecProperty());
            const std::filesystem::path absolute = spec.is_absolute() ? spec : (root / spec);
            if (!std::filesystem::exists(absolute, error) || error)
            {
                LogError("BuildContent: the source asset \"" + asset.getItemSpecProperty() +
                         "\" does not exist.");
                return false;
            }
            // Link is what a project uses when the file lives outside the project directory; it
            // names where the asset belongs in the content tree.
            const std::string link = asset.GetMetadata("Link");
            const std::string key =
                TaskDetail::RootRelative(root, link.empty() ? absolute : (root / link));
            if (!firstAsset)
            {
                configuration << ",\n";
            }
            firstAsset = false;
            configuration << "  \"" << TaskDetail::Escape(key) << "\": {";
            bool firstField = true;
            const auto field = [&configuration, &firstField](const std::string& name,
                                                             const std::string& value)
            {
                if (value.empty())
                {
                    return;
                }
                if (!firstField)
                {
                    configuration << ", ";
                }
                firstField = false;
                configuration << "\"" << name << "\": \"" << TaskDetail::Escape(value) << "\"";
            };
            field("logicalName", asset.GetMetadata("Name"));
            // A project names Microsoft's components; the canonical engine has its own. The
            // translation lives in one place so this task and the .contentproj reader cannot
            // disagree about what a name means.
            std::vector<std::pair<std::string, std::string>> parameters;
            const std::string importerName = asset.GetMetadata("Importer");
            if (!importerName.empty())
            {
                const XnaComponentMapping mapping = MapXnaImporterName(importerName);
                if (mapping.canonicalName.empty())
                {
                    // Not a hard failure for the two that mean "let the graph choose": naming
                    // XmlImporter or nothing reaches the same route.
                    LogMessage("BuildContent: importer \"" + importerName + "\" for \"" + key +
                               "\" is not named to the canonical build graph -- " + mapping.reason);
                }
                else
                {
                    field("importer", mapping.canonicalName);
                }
            }
            const std::string processorName = asset.GetMetadata("Processor");
            if (!processorName.empty())
            {
                const XnaComponentMapping mapping = MapXnaProcessorName(processorName);
                if (mapping.canonicalName.empty())
                {
                    LogMessage("BuildContent: processor \"" + processorName + "\" for \"" + key +
                               "\" is not named to the canonical build graph -- " + mapping.reason);
                }
                else
                {
                    field("processor", mapping.canonicalName);
                    // The defaults that make the canonical processor behave as the named XNA one;
                    // the project's own ProcessorParameters are appended after and win.
                    parameters = mapping.defaults;
                }
            }
            // Both spellings a content project uses: one packed metadata value, and one metadata
            // entry per parameter, which is what XNA's own project system writes.
            for (const auto& one : TaskDetail::SplitParameters(asset.GetMetadata("ProcessorParameters")))
            {
                parameters.push_back(one);
            }
            for (const std::string& name : asset.MetadataNames())
            {
                static const std::string prefix = "processorparameters_";
                if (name.rfind(prefix, 0) == 0 && name.size() > prefix.size())
                {
                    parameters.emplace_back(name.substr(prefix.size()), asset.GetMetadata(name));
                }
            }
            if (!parameters.empty())
            {
                // The reader refuses a repeated parameter name, and the project's own value must
                // win over the mapping's default, so the last one written is kept.
                std::map<std::string, std::string> effective;
                for (const auto& [name, value] : parameters)
                {
                    effective[name] = value;
                }
                if (!firstField)
                {
                    configuration << ", ";
                }
                firstField = false;
                configuration << "\"parameters\": {";
                bool firstParameter = true;
                for (const auto& [name, value] : effective)
                {
                    if (!firstParameter)
                    {
                        configuration << ", ";
                    }
                    firstParameter = false;
                    const std::string type = TaskDetail::ParameterType(value);
                    configuration << "\"" << TaskDetail::Escape(name) << "\": {\"type\": \"" << type
                                  << "\", \"value\": ";
                    if (type == "bool")
                    {
                        std::string lowered(value);
                        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                                       [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
                        configuration << lowered;
                    }
                    else
                    {
                        // Every other type persists as a string, so its exact written value is
                        // stable across hosts.
                        configuration << "\"" << TaskDetail::Escape(value) << "\"";
                    }
                    configuration << "}";
                }
                configuration << "}";
            }
            configuration << "}";
        }
        configuration << "\n }\n}\n";

        // The coordinator requires a configuration to live inside the source root it is reading,
        // which is the same containment rule every source read obeys. So the file is written
        // there under a name no project would choose and removed again whichever way this
        // returns; a copy is kept under the intermediate directory, because that is where a
        // project looks to see what its build was actually told.
        const std::filesystem::path configurationFile =
            root / (".cna-buildcontent-" +
                    (contentProjectGuid_.empty() ? std::string("project") : TaskDetail::Sanitize(contentProjectGuid_)) +
                    ".json");
        const std::filesystem::path configurationCopy = intermediate / "cna-buildcontent.json";
        struct Remover
        {
            std::filesystem::path path;
            ~Remover()
            {
                std::error_code ignored;
                std::filesystem::remove(path, ignored);
            }
        } remover{configurationFile};
        for (const std::filesystem::path& target : {configurationFile, configurationCopy})
        {
            std::ofstream file(target, std::ios::binary | std::ios::trunc);
            if (!file)
            {
                LogError("BuildContent could not write its build configuration to \"" +
                         target.string() + "\".");
                return false;
            }
            file << configuration.str();
        }
        intermediateFiles_.emplace_back(configurationCopy.string());

        if (rebuildAll_)
        {
            // The coordinator's incremental state is the output manifest; removing it is what
            // "rebuild everything" means without a second mechanism for the same thing.
            std::filesystem::remove(output / Canon::ContentBuildManifestFileName, error);
        }
        const std::string beforeManifest = [&output]
        {
            std::ifstream file(output / Canon::ContentBuildManifestFileName, std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
        }();

        std::vector<std::filesystem::path> arguments{
            "build", root, "-o", output, "--format", "xnb", "--config", configurationFile, "--quiet"};
        if (!targetPlatform_.empty())
        {
            std::string platform(targetPlatform_);
            std::transform(platform.begin(), platform.end(), platform.begin(),
                           [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            arguments.emplace_back("--xnb-platform");
            arguments.emplace_back(platform);
            if (platform == "xbox360")
            {
                // The coordinator refuses an Xbox target by default and says why; a project that
                // asked for one meant it, and the task is the place that asked.
                arguments.emplace_back("--xnb-allow-unverified-xbox");
            }
        }
        if (!targetProfile_.empty())
        {
            std::string profile(targetProfile_);
            std::transform(profile.begin(), profile.end(), profile.begin(),
                           [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            arguments.emplace_back("--xnb-profile");
            arguments.emplace_back(profile);
        }
        if (compressContent_)
        {
            // LZX is the compression XNA 4.0 itself produced and the only compressed form its
            // runtime loads.
            arguments.emplace_back("--xnb-compress");
            arguments.emplace_back("lzx");
        }

        int status = 1;
        try
        {
            status = Canon::RunContentCompiler(
                arguments, [](const Canon::ContentCompilerOptions& options)
                {
                    auto registry = std::make_shared<Canon::ContentPipelineRegistry>();
                    Canon::RegisterBuiltInContentPipeline(*registry, options);
                    return registry;
                });
        }
        catch (const std::exception& failure)
        {
            LogError(std::string("BuildContent failed: ") + failure.what());
            return false;
        }
        if (status != 0)
        {
            LogError("BuildContent failed: the content build reported status " +
                     std::to_string(status) + ".");
            return false;
        }

        // What was built, and which of it was new, both come from the manifest the coordinator
        // wrote -- the same record a later GetLastOutputs reads.
        std::string afterManifest;
        {
            std::ifstream file(output / Canon::ContentBuildManifestFileName, std::ios::binary);
            afterManifest.assign((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());
        }
        if (afterManifest.empty())
        {
            LogError("BuildContent: the build left no manifest, so what it produced cannot be "
                     "reported.");
            return false;
        }
        intermediateFiles_.emplace_back((output / Canon::ContentBuildManifestFileName).string());

        Canon::ContentBuildManifest manifest;
        Canon::ContentBuildManifest previous;
        try
        {
            manifest = Canon::ContentBuildManifest::Parse(afterManifest);
            if (!beforeManifest.empty())
            {
                previous = Canon::ContentBuildManifest::Parse(beforeManifest);
            }
        }
        catch (const std::exception& failure)
        {
            LogError(std::string("BuildContent could not read the build manifest: ") + failure.what());
            return false;
        }
        for (const auto& [nodeId, entry] : manifest.Entries())
        {
            const Canon::ContentBuildManifestEntry* was = previous.Find(nodeId);
            const bool rebuilt = was == nullptr || was->fingerprint != entry.fingerprint;
            for (const Canon::ContentBuildManifestOutput& produced : entry.outputs)
            {
                TaskItem item((output / produced.path).string());
                item.SetMetadata("Name", nodeId);
                item.SetMetadata("SourceAsset", entry.source);
                outputContentFiles_.push_back(item);
                if (rebuilt)
                {
                    rebuiltContentFiles_.push_back(item);
                }
            }
            for (const Canon::ContentBuildManifestDeploymentFile& deployed : entry.deploymentFiles)
            {
                TaskItem item((output / deployed.path).string());
                item.SetMetadata("Name", nodeId);
                item.SetMetadata("SourceAsset", entry.source);
                outputContentFiles_.push_back(item);
                if (rebuilt)
                {
                    rebuiltContentFiles_.push_back(item);
                }
            }
        }
        LogMessage("BuildContent: " + std::to_string(outputContentFiles_.size()) + " output file(s), " +
                   std::to_string(rebuiltContentFiles_.size()) + " rebuilt.");
        return true;
    }

    const std::string& BuildContent::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }
}
