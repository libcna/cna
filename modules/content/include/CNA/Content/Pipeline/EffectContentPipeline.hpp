// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "CNA/Content/Pipeline/ContentPipeline.hpp"

namespace CNA::Content::Pipeline
{
    /** @brief Stable in-memory type identity for an imported compiled effect binary. */
    inline constexpr const char* ImportedCompiledEffectType =
        "CNA.Content.Pipeline.ImportedCompiledEffect";

    /** @brief Stable in-memory type identity for a processed compiled effect. */
    inline constexpr const char* ProcessedCompiledEffectType =
        "CNA.Content.Pipeline.CompiledEffect";

    /**
     * @brief An already-compiled Effect Framework binary, as read from a `.fxb` file.
     *
     * The pipeline serializes bytecode it is given. It does not compile HLSL: see
     * `plans/plan_fx.md`, which records the standing decision that CNA will not embed an HLSL
     * source compiler, and `docs/xnb-interoperability.md` for what that means for interop.
     */
    struct ImportedCompiledEffect
    {
        /** @brief The complete compiled effect binary, byte for byte as authored. */
        std::vector<std::uint8_t> bytecode;

        /** @brief Compares the complete payload. */
        bool operator==(const ImportedCompiledEffect& other) const = default;
    };

    /**
     * @brief Whether a byte sequence begins with an Effect Framework 9.1 signature.
     *
     * Accepts both the bare `0xFEFF0901` token and the XNA 4.0 wrapper (`0xBCF00BCF` followed by
     * the offset of the wrapped token), which is the form XNA's own Content Pipeline emits.
     *
     * @param bytes Candidate file contents.
     * @return True when the signature is present and self-consistent.
     */
    [[nodiscard]] bool IsCompiledEffectBinary(const std::vector<std::uint8_t>& bytes);

    /** @brief Reads a compiled `.fxb` effect binary without compiling anything. */
    class CompiledEffectImporter final : public ContentImporter
    {
    public:
        /** @brief Returns the stable built-in importer identity. */
        [[nodiscard]] ContentComponentIdentity Identity() const override;

        /** @brief Returns the `.fxb` source route. */
        [[nodiscard]] std::vector<std::string> SourceExtensions() const override;

        /** @brief Returns ImportedCompiledEffectType. */
        [[nodiscard]] std::vector<std::string> OutputTypes() const override;

        /**
         * @brief Reads the file and validates its Effect Framework signature.
         *
         * @param context Call-scoped importer context.
         * @return An ImportedCompiledEffect holding the exact source bytes.
         */
        [[nodiscard]] ContentValue Import(ContentImporterContext& context) const override;
    };

    /** @brief Validates a compiled effect and passes its bytecode through unchanged. */
    class CompiledEffectProcessor final : public ContentProcessor
    {
    public:
        /** @brief Returns the stable built-in processor identity. */
        [[nodiscard]] ContentComponentIdentity Identity() const override;

        /** @brief Returns ImportedCompiledEffectType. */
        [[nodiscard]] std::string InputType() const override;

        /** @brief Returns ProcessedCompiledEffectType. */
        [[nodiscard]] std::string OutputType() const override;

        /**
         * @brief Rejects every parameter: there is no processing policy to configure.
         *
         * @param parameters Parameters to validate.
         */
        void ValidateParameters(const ContentProcessorParameters& parameters) const override;

        /**
         * @brief Returns the bytecode unchanged, after checking it is non-empty.
         *
         * @param input ImportedCompiledEffect value.
         * @param context Call-scoped processor context.
         * @return The same bytecode boxed as ProcessedCompiledEffectType.
         */
        [[nodiscard]] ContentValue Process(const ContentValue& input,
                                           ContentProcessorContext& context) const override;
    };

    /**
     * @brief Registers the compiled-effect source route.
     *
     * Only the XNB writer is registered for it, and deliberately so: the CNB container has no
     * `Effect` schema (`plans/plan_cnb.md` reserves the identifier and explains why -- CNA has
     * many renderers, and a `.cnb` carrying one API's shader bytecode would be useless on the
     * others). A `.cnb` build of a `.fxb` therefore fails to resolve a writer rather than
     * producing a file no renderer could use.
     *
     * @param registry Explicit registry to configure before builds begin.
     */
    void RegisterCompiledEffectContentPipeline(ContentPipelineRegistry& registry);
}
