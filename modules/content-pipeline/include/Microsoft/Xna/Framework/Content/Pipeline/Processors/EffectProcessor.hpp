// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "CNA/CNAHelper.hpp"
#include "CNA/Content/Pipeline/EffectCompilerService.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentProcessor.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/EffectContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/CompiledEffectContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/ProcessorEnums.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Processors
{
    /**
     * @brief Compiles effect source into the byte code an `Effect` loads.
     *
     * XNA compiles in-process with D3DX; CNA drives the same compiler through the canonical
     * `CNA::Content::Pipeline::EffectCompilerService`, which is the one effect compiler this
     * repository has. The observable contract is measured
     * (`tests/reference/xna40/graphics/graphics-content-oracle.json`, cases `effectprocessor/*`):
     * a successful compile answers the byte code, a failed one raises an
     * `InvalidContentException` beginning `Errors compiling <file>:`, and a null input is refused.
     */
    class EffectProcessor : public ContentProcessor<Graphics::EffectContent, CompiledEffectContent>
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Processors.EffectProcessor";

        /** @brief Initializes a processor that uses the external effect compiler. */
        EffectProcessor();

        /**
         * @brief Initializes a processor that uses the given compiler.
         *
         * @param compiler The compiler service; never null.
         */
        CNAEXT explicit EffectProcessor(std::shared_ptr<const CNA::Content::Pipeline::EffectCompilerService> compiler);

        /** @brief Destroys the processor. */
        ~EffectProcessor() override = default;

        /**
         * @brief Gets how the effect is compiled: for debugging, for speed, or as the build says.
         *
         * @return `Auto` by default.
         */
        [[nodiscard]] EffectProcessorDebugMode getDebugModeProperty() const noexcept;

        /**
         * @brief Sets how the effect is compiled.
         *
         * @param value The wanted mode.
         */
        void setDebugModeProperty(EffectProcessorDebugMode value) noexcept;

        /**
         * @brief Gets the preprocessor definitions applied to the source.
         *
         * @return The definitions, `NAME=value` separated by semicolons; empty by default, which
         *         is the null XNA starts with.
         */
        [[nodiscard]] const std::string& getDefinesProperty() const noexcept;

        /**
         * @brief Sets the preprocessor definitions applied to the source.
         *
         * @param value The definitions, `NAME=value` separated by semicolons.
         */
        void setDefinesProperty(std::string value);

        /**
         * @brief Compiles the effect.
         *
         * @param input The effect source to compile.
         * @param context The processor context, whose target profile and build configuration the
         *        compile follows.
         * @return The compiled effect.
         * @throws System::ArgumentNullException when the input is null.
         * @throws InvalidContentException when the compiler refuses the source, or when no effect
         *         compiler is available.
         */
        [[nodiscard]] std::shared_ptr<CompiledEffectContent> Process(
            const std::shared_ptr<Graphics::EffectContent>& input, ContentProcessorContext& context) override;

        /**
         * @brief Returns the type's stable name.
         *
         * @return The .NET full name of this processor.
         */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const;

    private:
        std::shared_ptr<const CNA::Content::Pipeline::EffectCompilerService> compiler_;
        EffectProcessorDebugMode debugMode_ = EffectProcessorDebugMode::Auto;
        std::string defines_;
    };
}
