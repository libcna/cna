// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline.md XNAP-A3: `.fx` source -> Effect XNB.
//
// The route is deliberately the ordinary one, with the compiler behind an interface:
//
//   .fx  ->  EffectSourceImporter   (include tree resolved as build dependencies)
//        ->  EffectSourceProcessor  (profile/defines/debug policy, calls the compiler service)
//        ->  EffectCompilerService  (external process; see EffectCompilerService.cpp)
//        ->  ProcessedCompiledEffect -- the *same* canonical value a .fxb produces
//        ->  the existing XNB Effect writer
//
// Two things this file does not do, on purpose. It does not teach the XNB writer anything about
// HLSL: the writer receives compiled bytes and cannot tell where they came from. And it never
// embeds source text as bytecode -- the compiler's output is checked for the Effect Framework 9.1
// signature before it is accepted, so a compiler that succeeded and wrote something else is a
// build failure rather than an `.xnb` that claims to be an XNA Effect and is not.

#include "CNA/Content/Pipeline/EffectSourceContentPipeline.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

#include "CNA/Content/Pipeline/EffectContentPipeline.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

namespace CNA::Content::Pipeline
{
    namespace
    {
        using Microsoft::Xna::Framework::Content::ContentLoadException;

        constexpr const char* kImporterName = "CNA.EffectSourceImporter";
        constexpr const char* kProcessorName = "CNA.EffectSourceProcessor";

        /** @brief Ceiling on the include tree, so a cycle or a fan-out cannot run away. */
        constexpr std::size_t kMaxIncludeFiles = 256u;

        /** @brief Ceiling on one effect source file, so a hostile tree cannot exhaust memory. */
        constexpr std::size_t kMaxSourceBytes = 8u * 1024u * 1024u;

        [[nodiscard]] std::string ReadTextFile(const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream)
            {
                throw ContentLoadException("cannot open effect source '" + path.string() + "'.");
            }
            // Braces, not parentheses: the parenthesized form declares a function.
            std::string text{std::istreambuf_iterator<char>(stream),
                             std::istreambuf_iterator<char>()};
            if (text.size() > kMaxSourceBytes)
            {
                throw ContentLoadException(
                    "effect source '" + path.string() + "' is " + std::to_string(text.size()) +
                    " bytes, above the " + std::to_string(kMaxSourceBytes) + "-byte ceiling.");
            }
            return text;
        }

        [[nodiscard]] std::string Trim(const std::string& text)
        {
            const auto begin = text.find_first_not_of(" \t\r\n");
            if (begin == std::string::npos) { return {}; }
            const auto end = text.find_last_not_of(" \t\r\n");
            return text.substr(begin, end - begin + 1u);
        }

        [[nodiscard]] std::string Lowercase(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(),
                           [](const unsigned char value)
                           { return static_cast<char>(std::tolower(value)); });
            return text;
        }

        /** @brief Reads the `profile` parameter, defaulting to Reach. */
        [[nodiscard]] EffectSourceProfile ReadProfile(
            const ContentProcessorParameters& parameters)
        {
            const ContentProcessorParameterValue* value =
                parameters.Find(EffectProfileParameter);
            if (value == nullptr) { return EffectSourceProfile::Reach; }
            const std::string* text = std::get_if<std::string>(value);
            EffectSourceProfile profile = EffectSourceProfile::Reach;
            if (text == nullptr || !TryParseEffectSourceProfile(Lowercase(*text), profile))
            {
                throw std::invalid_argument(
                    std::string("EffectSourceProcessor parameter '") + EffectProfileParameter +
                    "' must be 'reach' or 'hidef'.");
            }
            return profile;
        }

        /** @brief Reads the `debug` parameter, defaulting to false. */
        [[nodiscard]] bool ReadDebug(const ContentProcessorParameters& parameters)
        {
            const ContentProcessorParameterValue* value = parameters.Find(EffectDebugParameter);
            if (value == nullptr) { return false; }
            if (const bool* boolean = std::get_if<bool>(value); boolean != nullptr)
            {
                return *boolean;
            }
            if (const std::string* text = std::get_if<std::string>(value); text != nullptr)
            {
                const std::string lowered = Lowercase(*text);
                if (lowered == "true") { return true; }
                if (lowered == "false") { return false; }
            }
            throw std::invalid_argument(
                std::string("EffectSourceProcessor parameter '") + EffectDebugParameter +
                "' must be true or false.");
        }

        /** @brief Reads and validates the `defines` parameter into a deterministic ordered map. */
        [[nodiscard]] std::map<std::string, std::string> ReadDefines(
            const ContentProcessorParameters& parameters)
        {
            std::map<std::string, std::string> defines;
            const ContentProcessorParameterValue* value =
                parameters.Find(EffectDefinesParameter);
            if (value == nullptr) { return defines; }
            const std::string* text = std::get_if<std::string>(value);
            if (text == nullptr)
            {
                throw std::invalid_argument(
                    std::string("EffectSourceProcessor parameter '") + EffectDefinesParameter +
                    "' must be a string of NAME=VALUE pairs separated by ';'.");
            }

            std::istringstream stream(*text);
            std::string entry;
            while (std::getline(stream, entry, ';'))
            {
                const std::string trimmed = Trim(entry);
                if (trimmed.empty()) { continue; }
                const std::size_t equals = trimmed.find('=');
                const std::string name = Trim(trimmed.substr(0u, equals));
                const std::string definition =
                    equals == std::string::npos ? std::string{}
                                                : Trim(trimmed.substr(equals + 1u));
                if (name.empty() ||
                    !std::all_of(name.begin(), name.end(),
                                 [](const unsigned char value)
                                 { return std::isalnum(value) != 0 || value == '_'; }))
                {
                    throw std::invalid_argument(
                        std::string("EffectSourceProcessor parameter '") +
                        EffectDefinesParameter + "' names '" + name +
                        "', which is not a valid preprocessor identifier.");
                }
                if (definition.find_first_of("\"'\r\n") != std::string::npos)
                {
                    throw std::invalid_argument(
                        std::string("EffectSourceProcessor parameter '") +
                        EffectDefinesParameter + "' gives '" + name +
                        "' a value containing a quote or newline, which cannot be passed to a "
                        "compiler unambiguously.");
                }
                // A repeated name is a mistake worth naming: silently keeping one of the two
                // makes the build depend on parameter order, which the ordered map then hides.
                if (!defines.emplace(name, definition).second)
                {
                    throw std::invalid_argument(
                        std::string("EffectSourceProcessor parameter '") +
                        EffectDefinesParameter + "' defines '" + name + "' twice.");
                }
            }
            return defines;
        }
    }

    std::vector<std::pair<std::string, int>> ScanEffectSourceIncludes(const std::string& text)
    {
        std::vector<std::pair<std::string, int>> includes;
        int line = 1;
        std::size_t index = 0u;
        bool atLineStart = true;

        const auto skipSpaces = [&text](std::size_t& cursor)
        {
            while (cursor < text.size() && (text[cursor] == ' ' || text[cursor] == '\t'))
            {
                ++cursor;
            }
        };

        while (index < text.size())
        {
            const char current = text[index];
            if (current == '\n')
            {
                ++line;
                ++index;
                atLineStart = true;
                continue;
            }
            if (current == '/' && index + 1u < text.size() && text[index + 1u] == '/')
            {
                while (index < text.size() && text[index] != '\n') { ++index; }
                continue;
            }
            if (current == '/' && index + 1u < text.size() && text[index + 1u] == '*')
            {
                index += 2u;
                while (index + 1u < text.size() &&
                       !(text[index] == '*' && text[index + 1u] == '/'))
                {
                    if (text[index] == '\n') { ++line; }
                    ++index;
                }
                index = std::min(text.size(), index + 2u);
                atLineStart = false;
                continue;
            }
            if (current == '"' && !atLineStart)
            {
                // A string literal in ordinary code. Skipped so a quoted path in, say, a comment
                // banner or a semantic string cannot be read as a directive.
                ++index;
                while (index < text.size() && text[index] != '"')
                {
                    if (text[index] == '\\' && index + 1u < text.size()) { ++index; }
                    if (text[index] == '\n') { ++line; }
                    ++index;
                }
                index = std::min(text.size(), index + 1u);
                continue;
            }
            if (atLineStart && current == '#')
            {
                std::size_t cursor = index + 1u;
                skipSpaces(cursor);
                if (text.compare(cursor, 7u, "include") == 0)
                {
                    cursor += 7u;
                    skipSpaces(cursor);
                    if (cursor < text.size() && (text[cursor] == '"' || text[cursor] == '<'))
                    {
                        const char closing = text[cursor] == '"' ? '"' : '>';
                        const std::size_t start = cursor + 1u;
                        std::size_t end = start;
                        while (end < text.size() && text[end] != closing && text[end] != '\n')
                        {
                            ++end;
                        }
                        if (end < text.size() && text[end] == closing && end > start)
                        {
                            includes.emplace_back(text.substr(start, end - start), line);
                        }
                    }
                }
                // Skip the rest of the directive line either way.
                while (index < text.size() && text[index] != '\n') { ++index; }
                continue;
            }
            if (current != ' ' && current != '\t' && current != '\r') { atLineStart = false; }
            ++index;
        }
        return includes;
    }

    ContentComponentIdentity EffectSourceImporter::Identity() const
    {
        return {kImporterName, "1"};
    }

    std::vector<std::string> EffectSourceImporter::SourceExtensions() const
    {
        return {".fx"};
    }

    std::vector<std::string> EffectSourceImporter::OutputTypes() const
    {
        return {ImportedEffectSourceType};
    }

    ContentValue EffectSourceImporter::Import(ContentImporterContext& context) const
    {
        ImportedEffectSource imported;
        imported.source = context.SourcePath();
        imported.text = ReadTextFile(context.SourcePath());

        // Breadth-first over the include tree, keyed by resolved path so a diamond include is
        // recorded once and a cycle terminates.
        struct Pending
        {
            std::filesystem::path file;
            std::string text;
            std::filesystem::path directory;
        };
        std::vector<Pending> queue{
            {context.SourcePath(), imported.text, context.SourcePath().parent_path()}};
        std::set<std::filesystem::path> seen{context.SourcePath()};

        for (std::size_t head = 0u; head < queue.size(); ++head)
        {
            const Pending current = queue[head];
            for (const auto& [authored, line] : ScanEffectSourceIncludes(current.text))
            {
                // Resolved relative to the *including* file, which is what every C-style
                // preprocessor does and what an author expects of a nested `.fxh`.
                std::filesystem::path relative =
                    std::filesystem::relative(current.directory, context.SourcePath().parent_path())
                        .lexically_normal();
                std::filesystem::path candidate =
                    relative.empty() || relative == "." ? std::filesystem::path(authored)
                                                        : relative / authored;
                std::filesystem::path resolved;
                try
                {
                    // ResolveSourceDependency both records the dependency and refuses a path that
                    // escapes the source root, which is what stops `#include "../../secrets"`.
                    resolved = context.ResolveSourceDependency(candidate.lexically_normal());
                }
                catch (const std::exception& error)
                {
                    throw ContentLoadException(
                        current.file.filename().string() + "(" + std::to_string(line) +
                        "): cannot include '" + authored + "': " + error.what());
                }
                if (!std::filesystem::exists(resolved))
                {
                    throw ContentLoadException(
                        current.file.filename().string() + "(" + std::to_string(line) +
                        "): included file '" + authored + "' does not exist (looked for '" +
                        resolved.string() + "').");
                }

                EffectSourceInclude record;
                record.authored = authored;
                record.resolved = resolved;
                record.from = current.file;
                record.line = line;
                imported.includes.push_back(record);

                if (seen.insert(resolved).second)
                {
                    if (seen.size() > kMaxIncludeFiles)
                    {
                        throw ContentLoadException(
                            "effect source '" + context.SourcePath().filename().string() +
                            "' includes more than " + std::to_string(kMaxIncludeFiles) +
                            " files.");
                    }
                    queue.push_back({resolved, ReadTextFile(resolved), resolved.parent_path()});
                }
            }
        }

        context.LogInfo("read " + std::to_string(imported.text.size()) +
                        " bytes of effect source with " +
                        std::to_string(imported.includes.size()) + " include(s).");
        return ContentValue::Create(ImportedEffectSourceType, std::move(imported));
    }

    EffectSourceProcessor::EffectSourceProcessor(
        std::shared_ptr<const EffectCompilerService> compiler)
        : compiler_(std::move(compiler))
    {
        if (compiler_ == nullptr)
        {
            throw std::invalid_argument("EffectSourceProcessor(): compiler must not be null.");
        }
    }

    ContentComponentIdentity EffectSourceProcessor::Identity() const
    {
        // The compiler's identity is part of the processor's version, because the manifest
        // fingerprints the processor identity and the same source legitimately compiles to
        // different bytes under a different compiler. Changing compilers therefore rebuilds.
        return {kProcessorName, "1+" + compiler_->Identity().ToString()};
    }

    std::string EffectSourceProcessor::InputType() const { return ImportedEffectSourceType; }

    std::string EffectSourceProcessor::OutputType() const { return ProcessedCompiledEffectType; }

    void EffectSourceProcessor::ValidateParameters(
        const ContentProcessorParameters& parameters) const
    {
        for (const auto& [name, value] : parameters.Values())
        {
            static_cast<void>(value);
            if (name != EffectProfileParameter && name != EffectDefinesParameter &&
                name != EffectDebugParameter)
            {
                throw std::invalid_argument(
                    "EffectSourceProcessor does not recognize parameter '" + name + "'.");
            }
        }
        static_cast<void>(ReadProfile(parameters));
        static_cast<void>(ReadDefines(parameters));
        static_cast<void>(ReadDebug(parameters));
    }

    ContentValue EffectSourceProcessor::Process(const ContentValue& input,
                                                ContentProcessorContext& context) const
    {
        const ImportedEffectSource& source = input.Get<ImportedEffectSource>();
        const ContentProcessorParameters& parameters = context.Parameters();

        if (!compiler_->Available())
        {
            throw ContentLoadException("'" + source.source.filename().string() + "': " +
                                       compiler_->UnavailableReason());
        }

        EffectCompileRequest request;
        request.source = source.source;
        request.profile = ReadProfile(parameters);
        request.defines = ReadDefines(parameters);
        request.debugInformation = ReadDebug(parameters);
        // The source's own directory, so `#include "Common.fxh"` beside the effect works without
        // configuration. Nothing else is added: an include the importer did not resolve is an
        // include the incremental build does not know about, and the two must agree.
        request.includeDirectories.push_back(source.source.parent_path());

        const EffectCompileResult result = compiler_->Compile(request);
        for (const EffectCompilerDiagnostic& diagnostic : result.diagnostics)
        {
            if (diagnostic.isError) { continue; }
            context.LogWarning(diagnostic.ToString());
        }
        if (!result.succeeded)
        {
            std::ostringstream message;
            message << "'" << source.source.filename().string()
                    << "': the effect compiler ("
                    << compiler_->Identity().ToString() << ") rejected it";
            if (result.diagnostics.empty())
            {
                message << " without saying why.";
            }
            else
            {
                message << ":";
                for (const EffectCompilerDiagnostic& diagnostic : result.diagnostics)
                {
                    if (!diagnostic.isError) { continue; }
                    message << "\n  " << diagnostic.ToString();
                }
            }
            throw ContentLoadException(message.str());
        }

        ImportedCompiledEffect compiled;
        compiled.bytecode = result.bytecode;
        if (!IsCompiledEffectBinary(compiled.bytecode))
        {
            // The one refusal that matters most. A compiler that succeeds and hands back
            // something other than an Effect Framework 9.1 binary -- a bare shader blob, a
            // different effect container, or the source text itself -- must not reach the writer:
            // the result would be an `.xnb` that claims to be an XNA Effect and is not one.
            throw ContentLoadException(
                "'" + source.source.filename().string() + "': the effect compiler (" +
                compiler_->Identity().ToString() + ") produced " +
                std::to_string(compiled.bytecode.size()) +
                " bytes that do not begin with an Effect Framework 9.1 signature, so they are "
                "not an XNA-compatible compiled effect. An XNA 4.0 runtime's Effect constructor "
                "requires that container; the compiler must be asked for the fx_2_0 profile.");
        }

        context.LogInfo("compiled " + std::to_string(compiled.bytecode.size()) +
                        " bytes of effect bytecode with " + compiler_->Identity().ToString() +
                        " at profile " + EffectSourceProfileName(request.profile) + ".");
        return ContentValue::Create(ProcessedCompiledEffectType, std::move(compiled));
    }

    void RegisterEffectSourceContentPipeline(
        ContentPipelineRegistry& registry, std::shared_ptr<const EffectCompilerService> compiler)
    {
        if (compiler == nullptr) { compiler = MakeExternalEffectCompiler(); }
        registry.RegisterImporter(std::make_shared<EffectSourceImporter>());
        registry.RegisterProcessor(std::make_shared<EffectSourceProcessor>(std::move(compiler)));
        // The same processed type the `.fxb` route produces, so the same documented absence --
        // registered here too because either route may be the only one a registry has.
        if (registry.AbsentWriterReason(ContentOutputFormat::Cnb,
                                        ProcessedCompiledEffectType).empty())
        {
            registry.DocumentAbsentWriter(ContentOutputFormat::Cnb, ProcessedCompiledEffectType,
                                          CompiledEffectHasNoCnbSchemaReason());
        }
    }
}
