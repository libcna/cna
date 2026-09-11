// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file SoftwareCompiledEffect.hpp
 * @brief Headless Effect Framework parser and D3D9 shader-token IR for the
 * Software renderer.
 */

#if defined(CNA_SOFTWARE_COMPILED_EFFECTS)

#include "CNA/CNAHelper.hpp"
#include "CNA/Internal/Renderers/Common/ICompiledEffectRuntime.hpp"

#include "mojoshader.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace CNA::Internal::Renderers::Software
{
    /** @brief Identifies the stage encoded by one Direct3D 9 shader token stream.
     */
    enum class SoftwareShaderStageEXT
    {
        /** @brief A Direct3D vertex shader program. */
        Vertex,
        /** @brief A Direct3D pixel shader program. */
        Pixel
    };

    /** @brief One bounded instruction slice retained from original Direct3D 9
     * shader bytecode. */
    struct SoftwareShaderInstructionEXT
    {
        /** @brief Direct3D shader opcode from the low 16 bits of the instruction
         * token. */
        std::uint16_t opcode = 0;
        /** @brief Opcode-specific instruction control bits. */
        std::uint8_t controls = 0;
        /** @brief Whether this Shader Model 1 pixel instruction is co-issued. */
        bool coissue = false;
        /** @brief Whether Shader Model 3 predicates this instruction. */
        bool predicated = false;
        /** @brief Offset of the instruction token in the original DWORD stream. */
        std::size_t tokenOffset = 0;
        /** @brief Exact instruction token followed by its encoded operand/comment
         * tokens. */
        std::vector<std::uint32_t> tokens;
    };

    /** @brief A validated Direct3D 9 shader program prepared for the later CPU
     * execution phases. */
    struct SoftwareShaderProgramEXT
    {
        /** @brief Shader stage encoded by the version token. */
        SoftwareShaderStageEXT stage = SoftwareShaderStageEXT::Vertex;
        /** @brief Direct3D Shader Model major version. */
        std::uint8_t majorVersion = 0;
        /** @brief Direct3D Shader Model minor version. */
        std::uint8_t minorVersion = 0;
        /** @brief Exact original little-endian DWORD token stream. */
        std::vector<std::uint32_t> tokens;
        /** @brief Validated instruction slices in stream order, excluding the final
         * END token. */
        std::vector<SoftwareShaderInstructionEXT> instructions;
    };

    /**
     * @brief Renderer-local parsed representation of one classic XNA Effect
     * Framework binary.
     *
     * SOFTWARE-162 intentionally stops before shader execution. It nevertheless
     * owns the real MojoShader Effect graph, public reflection/value storage,
     * technique/pass state machine, preshader evaluation and the original validated
     * D3D9 programs selected by each pass. The vertex and pixel interpreters added
     * by SOFTWARE-163/164 consume the exposed program IR.
     */
    class SoftwareCompiledEffect final : public ICompiledEffectRuntime
    {
    public:
        /**
         * @brief Parses an XNA Direct3D 9 Effect Framework binary without a graphics
         * context.
         * @param effectCode Compiled effect bytes.
         * @param effectCodeLength Number of bytes at @p effectCode.
         */
        SoftwareCompiledEffect(const std::uint8_t* effectCode, std::size_t effectCodeLength);

        /** @brief Releases the parsed Effect graph and its retained shader programs.
         */
        ~SoftwareCompiledEffect() override;

        SoftwareCompiledEffect(const SoftwareCompiledEffect&) = delete;
        SoftwareCompiledEffect& operator=(const SoftwareCompiledEffect&) = delete;

        /**
         * @brief Creates an independent Effect instance with the same current values
         * and technique.
         * @return A separately mutable Software compiled-effect runtime.
         */
        [[nodiscard]] std::unique_ptr<ICompiledEffectRuntime> Clone() const override;

        /**
         * @brief Returns the immutable public reflection built from the Effect
         * Framework graph.
         * @return Reflected parameters, techniques, passes and annotations.
         */
        [[nodiscard]] const CompiledEffectDescription& GetDescription() const override;

        /**
         * @brief Selects the active technique.
         * @param techniqueIndex Zero-based index in the reflected technique list.
         */
        void SetTechnique(std::uint32_t techniqueIndex) override;

        /**
         * @brief Updates one top-level parameter's padded Effect storage.
         * @param runtimeIndex Reflected parameter index.
         * @param data Source bytes.
         * @param dataBytes Number of source bytes.
         */
        void SetParameterValue(std::uint32_t runtimeIndex, const void* data,
                               std::size_t dataBytes) override;

        /**
         * @brief Associates a public texture with one reflected texture parameter.
         * @param runtimeIndex Reflected parameter index.
         * @param texture Texture value, or null.
         */
        void SetParameterTexture(std::uint32_t runtimeIndex, Texture* texture) override;

        /**
         * @brief Applies one pass, evaluates preshaders and reports its XNA state
         * assignments.
         * @param passIndex Zero-based pass index in the selected technique.
         * @param deviceState Current GraphicsDevice state used as the assignment
         * base.
         * @param changes Receives the state groups changed by the pass.
         */
        void ApplyPass(std::uint32_t passIndex, const CompiledEffectDeviceState& deviceState,
                       CompiledEffectPassStateChanges& changes) override;

        /**
         * @brief Returns the vertex program selected by the most recently applied
         * pass.
         * @return The retained program, or null when the pass selected no vertex
         * shader.
         */
        CNAEXT [[nodiscard]] const SoftwareShaderProgramEXT* GetVertexProgramEXT() const noexcept;

        /**
         * @brief Returns the pixel program selected by the most recently applied
         * pass.
         * @return The retained program, or null when the pass selected no pixel
         * shader.
         */
        CNAEXT [[nodiscard]] const SoftwareShaderProgramEXT* GetPixelProgramEXT() const noexcept;

        /**
         * @brief Returns the float4 constant-register file populated for a shader
         * stage.
         * @param stage Shader stage whose registers are requested.
         * @return Flat four-float cells indexed as Direct3D constant register `cN` at
         * `4 * N`.
         */
        CNAEXT [[nodiscard]] std::span<const float>
        GetFloatRegistersEXT(SoftwareShaderStageEXT stage) const noexcept;

        /**
         * @brief Returns the int4 constant-register file populated for a shader
         * stage.
         * @param stage Shader stage whose registers are requested.
         * @return Flat four-integer cells indexed as Direct3D constant register `iN`
         * at `4 * N`.
         */
        CNAEXT [[nodiscard]] std::span<const int>
        GetIntegerRegistersEXT(SoftwareShaderStageEXT stage) const noexcept;

        /**
         * @brief Returns the Boolean constant-register file populated for a shader
         * stage.
         * @param stage Shader stage whose registers are requested.
         * @return Boolean cells indexed as Direct3D constant register `bN`.
         */
        CNAEXT [[nodiscard]] std::span<const unsigned char>
        GetBooleanRegistersEXT(SoftwareShaderStageEXT stage) const noexcept;

    private:
        struct Shader;
        struct ParserContext;

        SoftwareCompiledEffect(const SoftwareCompiledEffect& cloneSource, int);
        void CreateEffect();
        void CloseActivePass() noexcept;

        std::unique_ptr<ParserContext> parserContext_;
        MOJOSHADER_effect* effectData_ = nullptr;
        std::shared_ptr<const std::vector<std::uint8_t>> effectCode_;
        std::vector<std::vector<std::uint8_t>> parameterValues_;
        std::vector<Texture*> textures_;
        std::unordered_map<std::string, std::uint32_t> samplerTextureParameters_;
        CompiledEffectDescription description_;
        MOJOSHADER_effectStateChanges stateChanges_{};
        std::uint32_t techniqueIndex_ = 0;
        bool passActive_ = false;
    };
} // namespace CNA::Internal::Renderers::Software

#endif // CNA_SOFTWARE_COMPILED_EFFECTS
