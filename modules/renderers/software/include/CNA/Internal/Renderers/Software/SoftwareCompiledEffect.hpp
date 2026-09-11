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

    /** @brief One Direct3D declaration semantic bound to a shader register. */
    struct SoftwareShaderSemanticEXT
    {
        /** @brief Direct3D/MojoShader semantic kind. */
        MOJOSHADER_usage usage = MOJOSHADER_USAGE_UNKNOWN;
        /** @brief Semantic usage index. */
        std::uint8_t usageIndex = 0;
        /** @brief Direct3D input or output register number. */
        std::uint16_t registerNumber = 0;
        /** @brief Encoded Direct3D register family after type-bit normalization. */
        std::uint8_t registerType = 1;
    };

    /** @brief One semantic float4 value entering or leaving the CPU shader. */
    struct SoftwareShaderSemanticValueEXT
    {
        /** @brief Direct3D/MojoShader semantic kind. */
        MOJOSHADER_usage usage = MOJOSHADER_USAGE_UNKNOWN;
        /** @brief Semantic usage index. */
        std::uint8_t usageIndex = 0;
        /** @brief Four-component value, with absent declaration components expanded by vertex fetch. */
        std::array<float, 4> value{0.0f, 0.0f, 0.0f, 1.0f};
    };

    /** @brief Rasterizer-provided Direct3D 9 pixel-shader values that are not
     * interpolated vertex semantics. */
    struct SoftwarePixelShaderBuiltinsEXT
    {
        /** @brief Integer-centred, top-down VPOS value for the current fragment. */
        std::array<float, 4> position{0.0f, 0.0f, 0.0f, 1.0f};
        /** @brief VFACE sign: positive for clockwise/front-facing triangles. */
        float face = 1.0f;
    };

    /** @brief Result of executing one compiled Direct3D vertex shader invocation. */
    struct SoftwareVertexShaderResultEXT
    {
        /** @brief Homogeneous clip-space POSITION0 output. */
        std::array<float, 4> position{0.0f, 0.0f, 0.0f, 1.0f};
        /** @brief Non-position declared outputs for clipping and perspective interpolation. */
        std::vector<SoftwareShaderSemanticValueEXT> varyings;
        /** @brief Point-size output, or one when the shader did not write it. */
        float pointSize = 1.0f;
    };

    /** @brief Result of executing one compiled Direct3D pixel shader invocation. */
    struct SoftwarePixelShaderResultEXT
    {
        /** @brief Shader-domain COLOR0 through COLOR3 values. */
        std::array<std::array<float, 4>, 4> colors{};
        /** @brief Bit N is set when COLOR N was written by the program. */
        std::uint8_t colorWriteMask = 0;
        /** @brief True when TEXKILL/discard rejected the fragment. */
        bool discarded = false;
        /** @brief Shader-written depth when @ref depthWritten is true. */
        float depth = 0.0f;
        /** @brief Whether the program wrote the depth output register. */
        bool depthWritten = false;
    };

    /** @brief Direct3D sampler dimensionality declared by a pixel program. */
    enum class SoftwareShaderSamplerTypeEXT : std::uint8_t
    {
        /** @brief No declaration was available, as in Shader Model 1. */
        Unknown = 0,
        /** @brief A normalized two-dimensional texture sampler. */
        Texture2D = 2,
        /** @brief A direction-addressed cube-map sampler. */
        Cube = 3,
        /** @brief A normalized three-dimensional volume sampler. */
        Volume = 4
    };

    /** @brief LOD source selected by a Direct3D texture instruction. */
    enum class SoftwareTextureLodModeEXT : std::uint8_t
    {
        /** @brief Use the rasterizer's implicit coordinate derivatives. */
        Implicit,
        /** @brief Use the explicit scalar level in @ref SoftwarePixelSampleRequestEXT::lod. */
        Explicit,
        /** @brief Use the explicit x/y coordinate gradients in the sample request. */
        Gradients
    };

    /** @brief One texture lookup requested by the CPU pixel machine. */
    struct SoftwarePixelSampleRequestEXT
    {
        /** @brief Pixel sampler register number. */
        std::uint8_t samplerRegister = 0;
        /** @brief Texture-coordinate register used by the instruction. */
        std::uint8_t coordinateRegister = 0;
        /** @brief Source-register components that become the sampled u/v/w coordinates. */
        std::array<std::uint8_t, 3> coordinateComponents{0u, 1u, 2u};
        /** @brief Legacy matrix-row texture registers, or `-1` when no matrix transform applies. */
        std::array<std::int8_t, 2> legacyMatrix2RowRegisters{-1, -1};
        /** @brief Declared sampler dimensionality. */
        SoftwareShaderSamplerTypeEXT samplerType = SoftwareShaderSamplerTypeEXT::Unknown;
        /** @brief Texture coordinates after source swizzle/modification. */
        std::array<float, 4> coordinate{};
        /** @brief LOD mode encoded by TEX/TEXLDD/TEXLDL. */
        SoftwareTextureLodModeEXT lodMode = SoftwareTextureLodModeEXT::Implicit;
        /** @brief Shader-provided LOD or additional implicit LOD bias. */
        float lod = 0.0f;
        /** @brief Explicit horizontal coordinate gradient for TEXLDD. */
        std::array<float, 4> gradientX{};
        /** @brief Explicit vertical coordinate gradient for TEXLDD. */
        std::array<float, 4> gradientY{};
    };

    /** @brief Renderer-side texture provider used by the CPU pixel machine. */
    class ISoftwarePixelSamplerEXT
    {
    public:
        /** @brief Destroys the sampler provider. */
        virtual ~ISoftwarePixelSamplerEXT() = default;

        /**
         * @brief Resolves one shader texture lookup.
         * @param request Register, dimensionality, coordinates and LOD information.
         * @return Four shader-visible sampled components.
         */
        CNAEXT [[nodiscard]] virtual std::array<float, 4> SampleEXT(
            const SoftwarePixelSampleRequestEXT& request) const = 0;
    };

    /** @brief One sampler declaration retained from a Direct3D pixel program. */
    struct SoftwareShaderSamplerEXT
    {
        /** @brief Direct3D sampler register number. */
        std::uint8_t registerNumber = 0;
        /** @brief Dimensionality encoded by the sampler DCL token. */
        SoftwareShaderSamplerTypeEXT type = SoftwareShaderSamplerTypeEXT::Unknown;
    };

    struct SoftwareShaderProgramEXT;

    /**
     * @brief Executes one validated Direct3D vertex program without renderer state.
     * @param program Vertex program token IR.
     * @param floatRegisters Direct3D float4 constant register storage.
     * @param integerRegisters Direct3D int4 constant register storage.
     * @param booleanRegisters Direct3D Boolean constant register storage.
     * @param inputs Declaration-semantic values for one vertex.
     * @return Homogeneous position and interpolator outputs.
     */
    CNAEXT [[nodiscard]] SoftwareVertexShaderResultEXT ExecuteSoftwareVertexShaderEXT(
        const SoftwareShaderProgramEXT& program, std::span<const float> floatRegisters,
        std::span<const int> integerRegisters,
        std::span<const unsigned char> booleanRegisters,
        std::span<const SoftwareShaderSemanticValueEXT> inputs);

    /**
     * @brief Executes one validated Direct3D pixel program without texture sampling.
     * @param program Pixel program token IR.
     * @param floatRegisters Direct3D float4 constant register storage.
     * @param integerRegisters Direct3D int4 constant register storage.
     * @param booleanRegisters Direct3D Boolean constant register storage.
     * @param inputs Perspective-correct declaration-semantic values for one fragment.
     * @param sampler Renderer-side texture provider, or null for texture-free programs.
     * @param builtins Rasterizer-provided VPOS/VFACE values, or null for defaults.
     * @return Colour/depth outputs and discard state.
     * @throws std::runtime_error if a texture instruction has no sampler provider.
     */
    CNAEXT [[nodiscard]] SoftwarePixelShaderResultEXT ExecuteSoftwarePixelShaderEXT(
        const SoftwareShaderProgramEXT& program, std::span<const float> floatRegisters,
        std::span<const int> integerRegisters,
        std::span<const unsigned char> booleanRegisters,
        std::span<const SoftwareShaderSemanticValueEXT> inputs,
        const ISoftwarePixelSamplerEXT* sampler = nullptr,
        const SoftwarePixelShaderBuiltinsEXT* builtins = nullptr);

    /**
     * @brief Executes four lock-step Direct3D pixel invocations in one 2x2 quad.
     * @param program Pixel program token IR.
     * @param floatRegisters Direct3D float4 constant register storage.
     * @param integerRegisters Direct3D int4 constant register storage.
     * @param booleanRegisters Direct3D Boolean constant register storage.
     * @param inputs Perspective-correct semantic inputs in top-left, top-right,
     * bottom-left, bottom-right order.
     * @param sampler Renderer-side texture provider, or null for texture-free programs.
     * @param builtins Rasterizer-provided VPOS/VFACE values in lane order, or null for defaults.
     * @return Colour/depth outputs and discard state for the four lanes in input order.
     */
    CNAEXT [[nodiscard]] std::array<SoftwarePixelShaderResultEXT, 4>
    ExecuteSoftwarePixelShaderQuadEXT(
        const SoftwareShaderProgramEXT& program, std::span<const float> floatRegisters,
        std::span<const int> integerRegisters,
        std::span<const unsigned char> booleanRegisters,
        const std::array<std::span<const SoftwareShaderSemanticValueEXT>, 4>& inputs,
        const ISoftwarePixelSamplerEXT* sampler = nullptr,
        const std::array<SoftwarePixelShaderBuiltinsEXT, 4>* builtins = nullptr);

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
        /** @brief Declared vertex inputs, mapped to their Direct3D input registers. */
        std::vector<SoftwareShaderSemanticEXT> inputSemantics;
        /** @brief Declared Shader Model 3 outputs, mapped to output registers. */
        std::vector<SoftwareShaderSemanticEXT> outputSemantics;
        /** @brief Declared pixel sampler registers and their texture dimensions. */
        std::vector<SoftwareShaderSamplerEXT> samplers;
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

        /**
         * @brief Executes the selected classic Direct3D vertex program on one semantic input set.
         * @param inputs Declaration-semantic values for one vertex.
         * @return Homogeneous position and declared interpolator outputs.
         */
        CNAEXT [[nodiscard]] SoftwareVertexShaderResultEXT ExecuteVertexEXT(
            std::span<const SoftwareShaderSemanticValueEXT> inputs) const;

        /**
         * @brief Executes the selected classic Direct3D pixel program for one fragment.
         * @param inputs Perspective-correct semantic values from the vertex stage.
         * @param sampler Renderer-side texture provider, or null for texture-free programs.
         * @param builtins Rasterizer-provided VPOS/VFACE values, or null for defaults.
         * @return Colour/depth outputs and discard state.
         */
        CNAEXT [[nodiscard]] SoftwarePixelShaderResultEXT ExecutePixelEXT(
            std::span<const SoftwareShaderSemanticValueEXT> inputs,
            const ISoftwarePixelSamplerEXT* sampler = nullptr,
            const SoftwarePixelShaderBuiltinsEXT* builtins = nullptr) const;

        /**
         * @brief Executes the selected pixel program on one lock-step 2x2 fragment quad.
         * @param inputs Perspective-correct semantic inputs in top-left, top-right,
         * bottom-left, bottom-right order.
         * @param sampler Renderer-side texture provider, or null for texture-free programs.
         * @param builtins Rasterizer-provided VPOS/VFACE values in lane order, or null for defaults.
         * @return Colour/depth outputs and discard state for all four lanes.
         */
        CNAEXT [[nodiscard]] std::array<SoftwarePixelShaderResultEXT, 4> ExecutePixelQuadEXT(
            const std::array<std::span<const SoftwareShaderSemanticValueEXT>, 4>& inputs,
            const ISoftwarePixelSamplerEXT* sampler = nullptr,
            const std::array<SoftwarePixelShaderBuiltinsEXT, 4>* builtins = nullptr) const;

        /**
         * @brief Returns how many successful vertex invocations this runtime executed.
         * @return Monotonic per-runtime invocation count used by renderer conformance probes.
         */
        CNAEXT [[nodiscard]] std::size_t GetVertexExecutionCountEXT() const noexcept;

        /**
         * @brief Returns the most recent successful vertex result.
         * @return Last homogeneous position and varying set.
         */
        CNAEXT [[nodiscard]] const SoftwareVertexShaderResultEXT&
        GetLastVertexResultEXT() const noexcept;

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
        mutable std::size_t vertexExecutionCount_ = 0;
        mutable SoftwareVertexShaderResultEXT lastVertexResult_{};
    };
} // namespace CNA::Internal::Renderers::Software

#endif // CNA_SOFTWARE_COMPILED_EFFECTS
