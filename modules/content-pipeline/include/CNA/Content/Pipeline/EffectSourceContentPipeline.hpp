// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "CNA/Content/Pipeline/ContentPipeline.hpp"
#include "CNA/Content/Pipeline/EffectCompilerService.hpp"

namespace CNA::Content::Pipeline
{
    /** @brief Stable in-memory type identity for imported `.fx` effect source. */
    inline constexpr const char* ImportedEffectSourceType =
        "CNA.Content.Pipeline.ImportedEffectSource";

    /** @brief `EffectSourceProcessor` parameter (`reach`/`hidef`) selecting the graphics profile. */
    inline constexpr const char* EffectProfileParameter = "profile";

    /**
     * @brief `EffectSourceProcessor` parameter carrying preprocessor definitions.
     *
     * A single string of `NAME=VALUE` pairs separated by `;`, with a bare `NAME` meaning an empty
     * value. Applied in sorted key order so the compiler command -- and therefore the output -- is
     * deterministic regardless of how the parameter was written.
     */
    inline constexpr const char* EffectDefinesParameter = "defines";

    /** @brief `EffectSourceProcessor` parameter (`true`/`false`) embedding shader debug information. */
    inline constexpr const char* EffectDebugParameter = "debug";

    /** @brief One `#include` the importer resolved, and where it came from. */
    struct EffectSourceInclude
    {
        /** @brief The path as written in the source, verbatim. */
        std::string authored;

        /** @brief The resolved, contained native path. */
        std::filesystem::path resolved;

        /** @brief File the directive appeared in. */
        std::filesystem::path from;

        /** @brief One-based line of the directive. */
        int line = 0;

        /** @brief Compares every field. */
        bool operator==(const EffectSourceInclude& other) const = default;
    };

    /**
     * @brief `.fx` source, with its include tree resolved and recorded.
     *
     * The text is carried so that nothing downstream re-reads the file and can disagree with what
     * the dependency scan saw.
     */
    struct ImportedEffectSource
    {
        /** @brief Absolute path of the root `.fx`. */
        std::filesystem::path source;

        /** @brief The root file's text exactly as authored. */
        std::string text;

        /** @brief Every `#include` reached transitively, in first-seen order. */
        std::vector<EffectSourceInclude> includes;

        /** @brief Compares every field. */
        bool operator==(const ImportedEffectSource& other) const = default;
    };

    /**
     * @brief Scans one effect source for `#include` directives.
     *
     * A deliberately small, documented subset of the preprocessor: line comments, block comments
     * and string literals are skipped so a commented-out or quoted directive is not mistaken for
     * a real one, and `#include "file"` and `#include <file>` are both recognized. Conditional
     * compilation is **not** evaluated -- an include inside a false `#if` is still recorded as a
     * dependency, which is the safe direction: a rebuild too many is a wasted second, a rebuild
     * too few is a stale artifact.
     *
     * @param text The source text.
     * @return The authored include paths with their one-based line numbers, in order.
     */
    [[nodiscard]] std::vector<std::pair<std::string, int>> ScanEffectSourceIncludes(
        const std::string& text);

    /**
     * @brief Reads a `.fx` file and resolves its whole include tree as build dependencies.
     *
     * Every included file is resolved through `ContentImporterContext::ResolveSourceDependency`,
     * which both records it for the incremental build and refuses a path that escapes the source
     * root -- so an `#include "../../../etc/passwd"` fails rather than reading it.
     */
    class EffectSourceImporter final : public ContentImporter
    {
    public:
        /** @brief Returns the stable built-in importer identity. */
        [[nodiscard]] ContentComponentIdentity Identity() const override;

        /** @brief Returns the `.fx` source route. */
        [[nodiscard]] std::vector<std::string> SourceExtensions() const override;

        /** @brief Returns ImportedEffectSourceType. */
        [[nodiscard]] std::vector<std::string> OutputTypes() const override;

        /**
         * @brief Reads the root file and every file it includes, transitively.
         *
         * @param context Call-scoped importer context.
         * @return An ImportedEffectSource with the resolved include tree.
         */
        [[nodiscard]] ContentValue Import(ContentImporterContext& context) const override;
    };

    /**
     * @brief Compiles imported `.fx` source into the same canonical compiled-effect value a
     *        `.fxb` produces.
     *
     * The processor is container-neutral: it produces compiled effect bytes and knows nothing
     * about `.xnb` or `.cnb`. It also *validates* what the compiler gave back -- an output that is
     * not an Effect Framework 9.1 binary is refused here rather than written into a file that
     * claims to be an XNA Effect.
     *
     * The compiler's identity is folded into this processor's component version, which the build
     * manifest already fingerprints, so changing compiler or compiler version rebuilds instead of
     * reusing an artifact the new compiler would not have produced.
     */
    class EffectSourceProcessor final : public ContentProcessor
    {
    public:
        /**
         * @brief Creates a processor bound to one compiler backend.
         *
         * @param compiler The compiler to use; must not be null.
         */
        explicit EffectSourceProcessor(std::shared_ptr<const EffectCompilerService> compiler);

        /** @brief Returns the identity, with the compiler's identity folded into the version. */
        [[nodiscard]] ContentComponentIdentity Identity() const override;

        /** @brief Returns ImportedEffectSourceType. */
        [[nodiscard]] std::string InputType() const override;

        /** @brief Returns ProcessedCompiledEffectType. */
        [[nodiscard]] std::string OutputType() const override;

        /**
         * @brief Validates the profile, defines and debug parameters.
         *
         * @param parameters Parameters to validate.
         */
        void ValidateParameters(const ContentProcessorParameters& parameters) const override;

        /**
         * @brief Compiles the source and returns the compiled effect bytes.
         *
         * @param input ImportedEffectSource value.
         * @param context Call-scoped processor context.
         * @return The compiled bytecode boxed as ProcessedCompiledEffectType.
         */
        [[nodiscard]] ContentValue Process(const ContentValue& input,
                                           ContentProcessorContext& context) const override;

    private:
        std::shared_ptr<const EffectCompilerService> compiler_;
    };

    /**
     * @brief Registers the `.fx` source route.
     *
     * Only the XNB writer consumes the result, and deliberately: the CNB container reserves an
     * `Effect` identifier and has no schema for it, because a `.cnb` carrying one API's shader
     * bytecode would be useless on CNA's other renderers.
     *
     * The route is registered even when no compiler is available, so that a build tree containing
     * a `.fx` reports *why* it cannot be compiled -- with the discovery order and the exact
     * reproduction commands -- rather than reporting that nothing imports the extension.
     *
     * @param registry Explicit registry to configure before builds begin.
     * @param compiler Compiler backend; when null, the external backend is discovered.
     */
    void RegisterEffectSourceContentPipeline(
        ContentPipelineRegistry& registry,
        std::shared_ptr<const EffectCompilerService> compiler = nullptr);
}
