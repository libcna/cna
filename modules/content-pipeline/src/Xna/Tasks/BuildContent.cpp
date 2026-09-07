// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Tasks/BuildContent.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <iostream>
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
        /** @brief MSBuild compares a metadata name without regard to case; so does this. */
        [[nodiscard]] bool EqualsIgnoringCase(const std::string& left, const std::string& right)
        {
            if (left.size() != right.size()) { return false; }
            for (std::size_t at = 0; at < left.size(); ++at)
            {
                if (std::tolower(static_cast<unsigned char>(left[at])) !=
                    std::tolower(static_cast<unsigned char>(right[at])))
                {
                    return false;
                }
            }
            return true;
        }

        /**
         * @brief One MSBuild item spec as a path this host can open.
         *
         * An `Include` in a `.contentproj` is a Windows relative path -- `Textures\\hero.png` --
         * because MSBuild's separator is the backslash and every project a game actually has was
         * written on Windows. `std::filesystem` on a POSIX host reads that as one file name
         * containing a backslash and reports that the asset does not exist, which is an accurate
         * statement about a path nobody meant. XNA resolves it as a separator, so this does too;
         * the diagnostics keep the project's own spelling.
         *
         * @param spec The item spec or `Link` metadata as the project spelled it.
         * @return The same path with MSBuild's separator replaced by the portable one.
         */
        [[nodiscard]] std::string HostPath(std::string spec)
        {
            std::replace(spec.begin(), spec.end(), '\\', '/');
            return spec;
        }

        /**
         * @brief Redirects the two standard streams into a buffer for as long as it lives.
         *
         * MSBuild's own `BuildContent` reports what the build said through the engine, and a
         * project reads those lines to find out which asset failed and why. CNA's task drives the
         * canonical coordinator, whose diagnostics are its streams: a failure on `cerr`, everything
         * else on `cout`. Without this the task could only report that the build "returned status
         * 1", which names neither the asset nor the reason and is not something a developer can act
         * on (plans/plan_xnapipeline_parity.md XNAPP-267).
         *
         * The redirection is process-wide for the length of one `Execute()`, which is what it has
         * to be: the coordinator writes to the same two objects everything else does.
         */
        class CapturedOutput
        {
        public:
            /** @brief Starts capturing, remembering where the streams pointed. */
            CapturedOutput()
                : previousOut_(std::cout.rdbuf(out_.rdbuf())),
                  previousError_(std::cerr.rdbuf(error_.rdbuf()))
            {
            }

            /** @brief Puts both streams back. */
            ~CapturedOutput()
            {
                std::cout.rdbuf(previousOut_);
                std::cerr.rdbuf(previousError_);
            }

            CapturedOutput(const CapturedOutput&) = delete;
            CapturedOutput& operator=(const CapturedOutput&) = delete;

            /** @brief What the coordinator reported as a failure. */
            [[nodiscard]] std::vector<std::string> Failures() const { return Lines(error_.str()); }

            /** @brief Everything else the coordinator said. */
            [[nodiscard]] std::vector<std::string> Progress() const { return Lines(out_.str()); }

        private:
            /** @brief Splits captured text into lines, dropping blanks and trailing space. */
            [[nodiscard]] static std::vector<std::string> Lines(const std::string& text)
            {
                std::vector<std::string> lines;
                std::istringstream stream(text);
                std::string line;
                while (std::getline(stream, line))
                {
                    while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                    {
                        line.pop_back();
                    }
                    if (!line.empty()) { lines.push_back(line); }
                }
                return lines;
            }

            std::ostringstream out_;
            std::ostringstream error_;
            std::streambuf* previousOut_;
            std::streambuf* previousError_;
        };

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
         * @brief The content name one project item is built under.
         *
         * An item's `Name` metadata is MSBuild's `%(Filename)`, which the XNA project system writes
         * into every item: 8790 of the 8795 `Compile` items in the sample corpus have a `Name` that
         * is exactly the source file's stem. It is not the asset's content name, and reading it as
         * one flattens every asset in a subdirectory into the output root -- and refuses outright a
         * project with two same-named files in two folders, which the Platformer sample has five
         * times over.
         *
         * XNA's own build answers what the rule is. `Sprites\\MonsterA\\Idle.png` with
         * `Name` `Idle` was built to `Content/Sprites/MonsterA/Idle.xnb`, so the directory is kept;
         * `cat_depth.jpg` with `Name` `cat_normalmap` was built to `Content/cat_normalmap.xnb`, so
         * the name is honoured; and `Textures\\Backgrounds\\honeycombRush_instructions.png` with
         * `Name` `instructions` was built to `Content/Textures/Backgrounds/instructions.xnb`, which
         * is both at once. The content name is therefore the item's relative **directory** plus its
         * `Name`, and `Link` -- which moves the item within the content tree -- supplies that
         * directory when it is present.
         *
         * @param key The item's root-relative path, `Link` already applied.
         * @param name The item's `Name` metadata, empty if the project wrote none.
         * @return The content name to build the asset under.
         */
        [[nodiscard]] std::string ContentName(const std::string& key, const std::string& name)
        {
            const std::filesystem::path path(key);
            const std::string stem = name.empty() ? path.stem().string() : name;
            const std::filesystem::path directory = path.parent_path();
            return directory.empty() ? stem : (directory / stem).generic_string();
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
        // Refusing beats ignoring. A project naming its own pipeline assembly expects its own
        // importers to run, and a C++ build has no assembly to load; accepting the list silently
        // would let the project believe they did. The task therefore fails -- but it fails the way
        // XNA's own does, after building what it can, rather than instead of building anything: an
        // assembly's components are named per asset by that asset's own metadata, so every asset
        // whose importer and processor are built-in is one this build produces correctly. Stopping
        // here cost the whole project for one game-defined processor, and in the public sample
        // corpus that is 107 of 328 content projects producing nothing
        // (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-103`).
        const bool assembliesRefused = !pipelineAssemblies_.empty();
        if (assembliesRefused)
        {
            LogError("BuildContent cannot load pipeline assemblies: C++ has no assembly loading, so "
                     "a custom importer, processor or writer is registered in code with "
                     "RegisterXnaImporter/RegisterXnaProcessor before the build runs, and the "
                     "PipelineAssemblies item has no counterpart. " +
                     std::to_string(pipelineAssemblies_.size()) +
                     " were named; every asset whose components are built-in is still built.");
        }
        if (sourceAssets_.empty())
        {
            // Not a failure: a content project with nothing in it builds nothing, and MSBuild's
            // own task answers true for an empty item list. Checked *after* the assembly refusal,
            // because a project that names its own pipeline assembly is wrong whether or not it
            // also lists assets.
            LogMessage("BuildContent: no source assets, so nothing was built.");
            return !assembliesRefused;
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
        // One asset's entry is composed on its own before it joins the document, so an asset this
        // build has no component for can be reported and left out instead of ending the build. A
        // project *is* its item list and XNA builds every item it can, failing the ones it cannot
        // (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-103`).
        bool refusedAnAsset = false;
        for (const TaskItem& asset : sourceAssets_)
        {
            std::ostringstream assetJson;
            const std::filesystem::path spec(TaskDetail::HostPath(asset.getItemSpecProperty()));
            const std::filesystem::path absolute = spec.is_absolute() ? spec : (root / spec);
            if (!std::filesystem::exists(absolute, error) || error)
            {
                LogError("BuildContent: the source asset \"" + asset.getItemSpecProperty() +
                         "\" does not exist.");
                refusedAnAsset = true;
                continue;
            }
            // Link is what a project uses when the file lives outside the project directory; it
            // names where the asset belongs in the content tree.
            const std::string link = TaskDetail::HostPath(asset.GetMetadata("Link"));
            const std::string key =
                TaskDetail::RootRelative(root, link.empty() ? absolute : (root / link));
            assetJson << "  \"" << TaskDetail::Escape(key) << "\": {";
            bool firstField = true;
            const auto field = [&assetJson, &firstField](const std::string& name,
                                                             const std::string& value)
            {
                if (value.empty())
                {
                    return;
                }
                if (!firstField)
                {
                    assetJson << ", ";
                }
                firstField = false;
                assetJson << "\"" << name << "\": \"" << TaskDetail::Escape(value) << "\"";
            };
            field("logicalName", TaskDetail::ContentName(key, asset.GetMetadata("Name")));
            // A project names Microsoft's components; the canonical engine has its own. The
            // translation lives in one place so this task and the .contentproj reader cannot
            // disagree about what a name means.
            std::vector<std::pair<std::string, std::string>> parameters;
            std::vector<std::pair<std::string, std::string>> parameterNames;
            const std::string importerName = asset.GetMetadata("Importer");
            if (!importerName.empty())
            {
                // With the processor beside it: an audio importer's canonical counterpart depends
                // on which of CNA's two imported audio types the item is asking for (XNAPP-332).
                const XnaComponentMapping mapping =
                    MapXnaImporterName(importerName, asset.GetMetadata("Processor"));
                if (!mapping.known)
                {
                    // XNA's own words, because the situation is XNA's: a project naming an importer
                    // no assembly defines does not build there either. Building the source through
                    // whatever route its extension suggests would hide that until the project
                    // reached a real toolchain (plans/plan_xnapipeline_parity.md XNAPP-267).
                    LogError("Cannot find importer \"" + importerName + "\". " + mapping.reason);
                    refusedAnAsset = true;
                    continue;
                }
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
                if (!mapping.known)
                {
                    LogError("Cannot find content processor \"" + processorName + "\". " +
                             mapping.reason);
                    refusedAnAsset = true;
                    continue;
                }
                if (!mapping.obsoleteMessage.empty())
                {
                    // XNA's own sentence, in XNA's own place: a project naming an obsolete
                    // processor is told so and builds anyway.
                    LogWarning(mapping.obsoleteMessage);
                }
                if (mapping.canonicalName.empty())
                {
                    LogMessage("BuildContent: processor \"" + processorName + "\" for \"" + key +
                               "\" is not named to the canonical build graph -- " + mapping.reason);
                }
                else if (!mapping.chooseByImportedType)
                {
                    field("processor", mapping.canonicalName);
                }
                if (!mapping.canonicalName.empty())
                {
                    // The defaults that make the canonical processor behave as the named XNA one;
                    // the project's own ProcessorParameters are appended after and win. Written
                    // whether or not the processor itself is named, because a mapping that lets
                    // the imported type choose still carries the policy the XNA name stands for.
                    parameters = mapping.defaults;
                    parameterNames = mapping.parameterNames;
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
                // The name is compared without regard to case, as MSBuild compares metadata, but
                // what follows the prefix is taken exactly as the project wrote it: it becomes a
                // processor parameter, and a processor's own property has a case.
                static const std::string prefix = "ProcessorParameters_";
                if (name.size() > prefix.size() &&
                    TaskDetail::EqualsIgnoringCase(name.substr(0, prefix.size()), prefix))
                {
                    parameters.emplace_back(name.substr(prefix.size()), asset.GetMetadata(name));
                }
            }
            if (!parameters.empty())
            {
                // The reader refuses a repeated parameter name, and the project's own value must
                // win over the mapping's default, so the last value written is kept. The *name*
                // kept is the first one written, which is the mapping's: a canonical processor
                // standing in for an XNA one spells its parameters its own way (`textureFormat`
                // where XNA writes `TextureFormat`), and a project that names the XNA spelling is
                // setting that same parameter rather than an unknown second one. Matched without
                // regard to case, as MSBuild matches metadata (XNAPP-265).
                std::vector<std::pair<std::string, std::string>> effectiveOrder;
                for (const auto& [name, value] : parameters)
                {
                    const auto found = std::find_if(
                        effectiveOrder.begin(), effectiveOrder.end(),
                        [&name](const std::pair<std::string, std::string>& entry)
                        { return TaskDetail::EqualsIgnoringCase(entry.first, name); });
                    if (found == effectiveOrder.end()) { effectiveOrder.emplace_back(name, value); }
                    else { found->second = value; }
                }
                // XNA's own spelling for a parameter the canonical processor names differently
                // becomes the canonical one. Done after the merge so a project's value is the one
                // that survives, and before the write so the processor sees a name it knows: an
                // unrecognised parameter is only a warning, which means a `PremultiplyAlpha` left
                // untranslated would be dropped and silently replaced by the default.
                for (const auto& [xnaName, canonicalName] : parameterNames)
                {
                    const auto found = std::find_if(
                        effectiveOrder.begin(), effectiveOrder.end(),
                        [&xnaName](const std::pair<std::string, std::string>& entry)
                        { return TaskDetail::EqualsIgnoringCase(entry.first, xnaName); });
                    if (found == effectiveOrder.end()) { continue; }
                    const std::string value = found->second;
                    effectiveOrder.erase(found);
                    const auto existing = std::find_if(
                        effectiveOrder.begin(), effectiveOrder.end(),
                        [&canonicalName](const std::pair<std::string, std::string>& entry)
                        { return TaskDetail::EqualsIgnoringCase(entry.first, canonicalName); });
                    if (existing == effectiveOrder.end())
                    {
                        effectiveOrder.emplace_back(canonicalName, value);
                    }
                    else
                    {
                        existing->second = value;
                    }
                }
                // `ColorKeyEnabled` is the half of XNA's colour-key pair the canonical processor
                // does not have: there, a colour key is on exactly when one is named. So the
                // project's boolean decides whether the colour survives at all, and the name
                // itself never reaches a processor (plans/plan_xnapipeline_parity.md XNAPP-251).
                const auto colorKeyEnabled = std::find_if(
                    effectiveOrder.begin(), effectiveOrder.end(),
                    [](const std::pair<std::string, std::string>& entry)
                    { return TaskDetail::EqualsIgnoringCase(entry.first, "ColorKeyEnabled"); });
                if (colorKeyEnabled != effectiveOrder.end())
                {
                    const bool keyed = TaskDetail::EqualsIgnoringCase(colorKeyEnabled->second,
                                                                      "true");
                    effectiveOrder.erase(colorKeyEnabled);
                    if (!keyed)
                    {
                        std::erase_if(effectiveOrder,
                                      [](const std::pair<std::string, std::string>& entry)
                                      {
                                          return TaskDetail::EqualsIgnoringCase(entry.first,
                                                                                "colorKey");
                                      });
                    }
                }
                std::map<std::string, std::string> effective;
                for (const auto& [name, value] : effectiveOrder) { effective[name] = value; }
                if (!firstField)
                {
                    assetJson << ", ";
                }
                firstField = false;
                assetJson << "\"parameters\": {";
                bool firstParameter = true;
                for (const auto& [name, value] : effective)
                {
                    if (!firstParameter)
                    {
                        assetJson << ", ";
                    }
                    firstParameter = false;
                    const std::string type = TaskDetail::ParameterType(value);
                    assetJson << "\"" << TaskDetail::Escape(name) << "\": {\"type\": \"" << type
                                  << "\", \"value\": ";
                    if (type == "bool")
                    {
                        std::string lowered(value);
                        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                                       [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
                        assetJson << lowered;
                    }
                    else
                    {
                        // Every other type persists as a string, so its exact written value is
                        // stable across hosts.
                        assetJson << "\"" << TaskDetail::Escape(value) << "\"";
                    }
                    assetJson << "}";
                }
                assetJson << "}";
            }
            assetJson << "}";
            if (!firstAsset) { configuration << ",\n"; }
            firstAsset = false;
            configuration << assetJson.str();
        }
        configuration << "\n }\n}\n";

        // The configuration goes to the intermediate directory, which is the task's own, and the
        // coordinator is *told* about it rather than discovering it. It used to be written into
        // the source root and deleted again, because the coordinator required a configuration to
        // live inside the root it was reading -- and that made a read-only source tree
        // unbuildable and changed the modification time of every project directory a build
        // touched. Vendored content, a checkout mounted read-only and somebody else's sample
        // corpus are all ordinary things to build (plans/plan_xna_sample_xnb_sweep.md
        // `XNASWEEP-110`).
        const std::filesystem::path configurationFile = intermediate / "cna-buildcontent.json";
        {
            std::ofstream file(configurationFile, std::ios::binary | std::ios::trunc);
            if (!file)
            {
                LogError("BuildContent could not write its build configuration to \"" +
                         configurationFile.string() + "\".");
                return false;
            }
            file << configuration.str();
        }
        intermediateFiles_.emplace_back(configurationFile.string());

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

        // No `--quiet`: that flag suppresses everything that is not a failure, and a build's
        // warnings are exactly what a project needs -- XNA warns rather than fails for an unknown
        // processor parameter, a value it cannot convert and a character region the font cannot
        // cover. Nothing reaches a console regardless, because the streams are captured below.
        // `--xna-compatible` is XNA's own strictness, measured: a `.contentproj` naming a processor
        // parameter the processor has not got, writing a value it cannot convert, or asking for
        // characters the font has no glyphs for, builds there with a warning. The canonical tool
        // refuses all three, which is right for it and wrong for a façade standing in for XNA
        // (plans/plan_xnapipeline_parity.md XNAPP-267).
        // `--only-configured-assets`: the configuration this task just wrote names exactly the
        // project's `Compile` items, and a project *is* its item list -- a source file sitting in
        // the same folder that the project does not name is not part of it. Without this the build
        // discovers the whole directory, which produces assets XNA never built and, where two
        // unlisted neighbours share a name, refuses a project XNA builds: the Platformer sample's
        // `Sounds/ExitReached.wav` beside the `Sounds/ExitReached.wma` the project actually lists
        // (plans/plan_xnapipeline_parity.md XNAPP-330).
        std::vector<std::filesystem::path> arguments{
            "build",    root,              "-o",  output, "--format", "xnb",
            "--config", configurationFile, "--xna-compatible", "--only-configured-assets"};
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
        if (!buildConfiguration_.empty())
        {
            // `EffectProcessor.DebugMode` defaults to `Auto`, which follows the build
            // configuration; a content project's own default is `Debug`. Without this the
            // coordinator saw `Release` for every project and compiled every effect optimized
            // (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-108`).
            arguments.emplace_back("--build-configuration");
            arguments.emplace_back(buildConfiguration_);
        }
        if (compressContent_)
        {
            // LZX is the compression XNA 4.0 itself produced and the only compressed form its
            // runtime loads.
            arguments.emplace_back("--xnb-compress");
            arguments.emplace_back("lzx");
        }

        int status = 1;
        std::vector<std::string> failures;
        std::vector<std::string> progress;
        std::string threw;
        {
            TaskDetail::CapturedOutput captured;
            try
            {
                status = Canon::RunContentCompiler(
                    arguments,
                    registryFactory_ ? registryFactory_
                                     : Canon::ContentPipelineRegistryFactory(
                                           [](const Canon::ContentCompilerOptions& options)
                                           {
                                               auto registry =
                                                   std::make_shared<Canon::ContentPipelineRegistry>();
                                               Canon::RegisterBuiltInContentPipeline(*registry, options);
                                               return registry;
                                           }));
            }
            catch (const std::exception& failure)
            {
                threw = failure.what();
            }
            failures = captured.Failures();
            progress = captured.Progress();
        }
        // Reported after the streams are back, keeping the coordinator's own three-way split
        // rather than guessing at one from the words: a failure is what it wrote to the error
        // stream, a warning is what it labelled one, and everything else is progress. Which line
        // is which is what a project reads the list for.
        for (const std::string& line : failures) { LogError(line); }
        for (const std::string& line : progress)
        {
            const std::size_t at = line.find_first_not_of(' ');
            if (at != std::string::npos && line.compare(at, 8, "warning ") == 0)
            {
                LogWarning(line.substr(at));
            }
            else
            {
                LogMessage(line);
            }
        }
        if (!threw.empty())
        {
            LogError(std::string("BuildContent failed: ") + threw);
            return false;
        }
        // A non-zero status means at least one asset failed, not that none was built: the
        // coordinator builds every asset it can and reports the ones it could not. The failure is
        // remembered and answered at the end, so `OutputContentFiles` still names what a partly
        // failed build produced -- which is what XNA's own task leaves behind
        // (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-103`).
        bool failed = assembliesRefused || refusedAnAsset;
        if (status != 0)
        {
            LogError("BuildContent failed: the content build reported status " +
                     std::to_string(status) + ".");
            failed = true;
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
        return !failed;
    }

    void BuildContent::setRegistryFactoryEXT(Canon::ContentPipelineRegistryFactory value)
    {
        registryFactory_ = std::move(value);
    }

    const std::string& BuildContent::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }
}
