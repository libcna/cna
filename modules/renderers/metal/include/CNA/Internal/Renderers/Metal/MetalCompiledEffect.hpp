// SPDX-License-Identifier: MS-PL
#pragma once

// plans/plan_apple_m4.md AM4-144: compiled XNA Effect Framework bytecode on Metal.
//
// MojoShader has no Metal adapter CNA could use: its own metal profile hard-fails several shader
// model 1-3 constructs real XNA content contains (centroid, relative input addressing, predicated
// destinations). So this backend takes the route Vulkan and WebGPU already take -- the
// nine-function effect context written directly against MOJOSHADER_parse with the portable SPIR-V
// profile -- and adds one translation: SPIRV-Cross turns each linked stage into MSL, in process,
// which the renderer compiles with MTLDevice newLibraryWithSource. Everything in this header is
// plain C++; the Metal objects belong to MetalRenderer.

#if defined(CNA_METAL_COMPILED_EFFECTS)

#include "CNA/CNAHelper.hpp"
#include "CNA/Internal/Renderers/Common/ICompiledEffectRuntime.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"

#include "mojoshader.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace CNA::Internal::Renderers::Metal
{
    class MetalRenderer;

    /** @brief One shader object MojoShader compiled out of an effect, reference counted by it. */
    struct MetalCompiledShaderEXT
    {
        /** @brief MojoShader's SPIR-V parse result, owned by this object. */
        const MOJOSHADER_parseData* parseData = nullptr;
        /** @brief MojoShader's reference count for the object. */
        int refcount = 1;
    };

    /**
     * @brief The renderer-wide MojoShader effect context: bound shaders and the constant registers.
     *
     * One per renderer, shared by every compiled effect, because XNA's pass binding is device
     * state: a SpriteBatch pass that sets only a pixel shader keeps the vertex shader SpriteEffect
     * bound before it.
     */
    struct MetalMojoShaderContextEXT
    {
        /** @brief Float4 constant registers per stage (vs_3_0 / ps_3_0 maximum). */
        static constexpr int kMaxFloat4Registers = 256;
        /** @brief Int4 constant registers per stage. */
        static constexpr int kMaxInt4Registers = 16;
        /** @brief Bool constant registers per stage. */
        static constexpr int kMaxBoolRegisters = 16;

        /** @brief Vertex shader the current pass bound. */
        MetalCompiledShaderEXT* boundVertex = nullptr;
        /** @brief Pixel shader the current pass bound. */
        MetalCompiledShaderEXT* boundPixel = nullptr;
        /** @brief Vertex float registers. */
        std::array<float, kMaxFloat4Registers * 4> vsRegF{};
        /** @brief Vertex int registers. */
        std::array<int, kMaxInt4Registers * 4> vsRegI{};
        /** @brief Vertex bool registers. */
        std::array<unsigned char, kMaxBoolRegisters> vsRegB{};
        /** @brief Pixel float registers. */
        std::array<float, kMaxFloat4Registers * 4> psRegF{};
        /** @brief Pixel int registers. */
        std::array<int, kMaxInt4Registers * 4> psRegI{};
        /** @brief Pixel bool registers. */
        std::array<unsigned char, kMaxBoolRegisters> psRegB{};
        /** @brief Last error MojoShader asked the context for. */
        std::string lastError;
        /**
         * @brief Each texture stage's BUMPENVMAT/BUMPENVLSCALE/BUMPENVLOFFSET, device state as in
         *        Direct3D 9, merged from every pass that assigns them (AM4-144).
         */
        std::array<CompiledEffectLegacyBumpMapEnvState, 16> bumpEnvironment{};
    };

    /** @brief Vertex-stage buffer argument index of the translated vertex shader's uniform block. */
    inline constexpr std::uint32_t kMetalCompiledVertexUniformBuffer = 1u;
    /** @brief Fragment-stage buffer argument index of the translated pixel shader's uniform block. */
    inline constexpr std::uint32_t kMetalCompiledPixelUniformBuffer = 0u;

    /** @brief The texture type a translated pixel sampler declares. */
    enum class MetalCompiledTextureKind : std::uint8_t
    {
        /** @brief texture2d. */
        Texture2D,
        /** @brief texturecube. */
        TextureCube,
        /** @brief texture3d. */
        Texture3D,
    };

    /** @brief One pixel sampler of a translated pass: XNA sampler register == texture == sampler index. */
    struct MetalCompiledSamplerBindingEXT
    {
        /** @brief The ps_3_0 sampler register, and the MSL texture and sampler index. */
        std::uint32_t slot = 0;
        /** @brief Declared texture type. */
        MetalCompiledTextureKind kind = MetalCompiledTextureKind::Texture2D;
    };

    /** @brief One vertex attribute a translated vertex shader reads, resolved to a bound stream. */
    struct MetalCompiledAttributeEXT
    {
        /** @brief `[[attribute(location)]]`, MojoShader's input order. */
        std::uint32_t location = 0;
        /** @brief Index into the streams handed to LinkAndGetShadersEXT. */
        std::uint32_t streamIndex = 0;
        /** @brief Byte offset inside that stream's record. */
        std::uint32_t offset = 0;
        /** @brief The declared element format (the linked shader input matches it). */
        Microsoft::Xna::Framework::Graphics::VertexElementFormat format =
            Microsoft::Xna::Framework::Graphics::VertexElementFormat::Vector4;
    };

    /** @brief One translated stage: MSL source, its entry point and a content hash. */
    struct MetalCompiledStageEXT
    {
        /** @brief Complete MSL translation unit. */
        std::string msl;
        /** @brief Entry point name inside it. */
        std::string entryPoint;
        /** @brief FNV-1a of the linked SPIR-V, the renderer's library cache key. */
        std::uint64_t hash = 0;
    };

    /** @brief Pipeline stage a SPIR-V module is translated for. */
    enum class MetalCompiledStageKind : std::uint8_t
    {
        /** @brief A vertex shader. */
        Vertex,
        /** @brief A pixel (fragment) shader. */
        Pixel,
    };

    /** @brief Result of translating one SPIR-V stage to MSL. */
    struct MetalSpirvToMslResultEXT
    {
        /** @brief The translation; empty on failure. */
        MetalCompiledStageEXT stage;
        /** @brief Pixel samplers the stage declares (vertex samplers are refused, see below). */
        std::vector<MetalCompiledSamplerBindingEXT> samplers;
        /** @brief True when the stage declares a uniform block. */
        bool hasUniforms = false;
        /** @brief True when a vertex stage samples a texture. */
        bool samplesInVertexStage = false;
        /** @brief Bit n set when a pixel stage writes COLORn (`[[color(n)]]`). */
        std::uint32_t colorOutputMask = 0;
        /** @brief Why translation failed; empty on success. */
        std::string error;
    };

    /**
     * @brief Translates one linked MojoShader SPIR-V stage to MSL.
     *
     * MojoShader's SPIR-V profile puts vertex samplers in descriptor set 0, the vertex uniform block
     * in set 1, pixel samplers in set 2 (binding = sampler register) and the pixel uniform block in
     * set 3. They become the vertex buffer kMetalCompiledVertexUniformBuffer, the fragment buffer
     * kMetalCompiledPixelUniformBuffer, and texture/sampler index = sampler register; vertex inputs
     * keep their SPIR-V locations as `[[attribute(n)]]`.
     *
     * @param words SPIR-V words (patch table already excluded).
     * @param wordCount Number of words.
     * @param stageKind Stage the module is.
     * @param iosPlatform Translate for iOS rather than macOS.
     * @return The MSL and the stage's resources, or the error.
     */
    CNAEXT [[nodiscard]] MetalSpirvToMslResultEXT TranslateMetalCompiledStageEXT(
        const std::uint32_t* words, std::size_t wordCount, MetalCompiledStageKind stageKind,
        bool iosPlatform);

    /**
     * @brief Device-bound runtime of one compiled XNA effect on Metal.
     */
    class MetalCompiledEffect final : public ICompiledEffectRuntime
    {
    public:
        /**
         * @brief Parses an XNA Effect Framework binary against the renderer's MojoShader context.
         *
         * @param renderer Owning Metal renderer.
         * @param effectCode Effect bytes.
         * @param effectCodeLength Number of bytes.
         */
        MetalCompiledEffect(MetalRenderer& renderer,
                            const std::uint8_t* effectCode,
                            std::size_t effectCodeLength);

        /** @brief Ends an open pass and frees MojoShader's effect. */
        ~MetalCompiledEffect() override;

        MetalCompiledEffect(const MetalCompiledEffect&) = delete;
        MetalCompiledEffect& operator=(const MetalCompiledEffect&) = delete;

        /**
         * @brief Creates an independent copy with its own parameter storage.
         *
         * @return The clone.
         */
        [[nodiscard]] std::unique_ptr<ICompiledEffectRuntime> Clone() const override;

        /**
         * @brief Returns the reflected techniques and parameters.
         *
         * @return The immutable description.
         */
        [[nodiscard]] const CompiledEffectDescription& GetDescription() const override;

        /**
         * @brief Selects a technique.
         *
         * @param techniqueIndex Zero-based technique index.
         */
        void SetTechnique(std::uint32_t techniqueIndex) override;

        /**
         * @brief Replaces one parameter's raw value.
         *
         * @param runtimeIndex MojoShader parameter index.
         * @param data Value bytes.
         * @param dataBytes Number of bytes.
         */
        void SetParameterValue(std::uint32_t runtimeIndex,
                               const void* data,
                               std::size_t dataBytes) override;

        /**
         * @brief Assigns a texture parameter.
         *
         * @param runtimeIndex MojoShader parameter index.
         * @param texture Texture created by this renderer, or null.
         */
        void SetParameterTexture(std::uint32_t runtimeIndex, Texture* texture) override;

        /**
         * @brief Applies a pass of the selected technique and reports its state assignments.
         *
         * @param passIndex Zero-based pass index.
         * @param deviceState The device's current state groups.
         * @param changes Receives the assigned state groups.
         */
        void ApplyPass(std::uint32_t passIndex,
                       const CompiledEffectDeviceState& deviceState,
                       CompiledEffectPassStateChanges& changes) override;

        /**
         * @brief Returns the shader objects the renderer-wide context has bound.
         *
         * @param vertex Receives the bound vertex shader, or null.
         * @param pixel Receives the bound pixel shader, or null.
         */
        CNAEXT void GetBoundShadersEXT(MetalCompiledShaderEXT*& vertex,
                                       MetalCompiledShaderEXT*& pixel) const;

        /**
         * @brief Packs the bound shaders' constant registers into their uniform-block layout.
         *
         * @param vertexBytes Receives the vertex uniform block.
         * @param pixelBytes Receives the pixel uniform block.
         */
        CNAEXT void CaptureUniformSnapshotEXT(std::vector<std::uint8_t>& vertexBytes,
                                              std::vector<std::uint8_t>& pixelBytes) const;

        /**
         * @brief Returns the texture and sampler state a pass assigned to a sampler register.
         *
         * @param slot Sampler register.
         * @param vertexStage True for a vertex sampler.
         * @param texture Receives the texture, or null.
         * @param sampler Receives the sampler state.
         * @param samplerAssigned Receives whether a pass assigned the slot's sampler state.
         */
        CNAEXT void GetBoundSamplerEXT(
            std::uint32_t slot, bool vertexStage, Texture*& texture,
            Microsoft::Xna::Framework::Graphics::SamplerState& sampler,
            bool* samplerAssigned = nullptr) const;

        /** @brief One vertex stream a draw binds, described by its declaration. */
        struct CompiledVertexStreamEXT
        {
            /** @brief The stream's declaration elements. */
            const std::vector<Microsoft::Xna::Framework::Graphics::VertexElement>* elements =
                nullptr;
            /** @brief The stream's record stride. */
            std::uint32_t stride = 0;
            /** @brief True when the stream advances per instance. */
            bool perInstance = false;
        };

        /** @brief The bound pass, linked against a draw's vertex layout and translated to MSL. */
        struct LinkedPassEXT
        {
            /** @brief Vertex stage, owned by the process-wide translation cache (never erased). */
            const MetalCompiledStageEXT* vertex = nullptr;
            /** @brief Pixel stage, owned by the same cache. */
            const MetalCompiledStageEXT* pixel = nullptr;
            /** @brief The vertex inputs, each resolved to a stream. */
            std::vector<MetalCompiledAttributeEXT> attributes;
            /** @brief The pixel samplers. */
            std::vector<MetalCompiledSamplerBindingEXT> pixelSamplers;
            /** @brief The vertex stage has a uniform block. */
            bool vertexHasUniforms = false;
            /** @brief The pixel stage has a uniform block. */
            bool pixelHasUniforms = false;
            /** @brief Bit n set when the pixel stage writes COLORn; MRT attachments it does not write keep their contents. */
            std::uint32_t pixelColorOutputs = 1;
            /** @brief Identity of the stage pair and the vertex layout, for the pipeline cache. */
            std::uint64_t pipelineKey = 0;
        };

        /**
         * @brief Links the bound shader pair against a draw's streams and translates both stages.
         *
         * A shader input no stream's declaration supplies, and a vertex shader that samples a
         * texture (plans/plan_fx.md FX-109), refuse by name with System::NotSupportedException.
         *
         * @param streams Every bound stream, in the order the draw binds them.
         * @return The linked, translated pass.
         */
        CNAEXT [[nodiscard]] LinkedPassEXT LinkAndGetShadersEXT(
            const std::vector<CompiledVertexStreamEXT>& streams) const;

    private:
        MetalCompiledEffect(MetalRenderer& renderer, const MetalCompiledEffect& cloneSource);
        void BuildDescriptionAndBackend(const std::uint8_t* effectCode,
                                        std::size_t effectCodeLength);

        MetalRenderer& renderer_;
        MetalMojoShaderContextEXT* context_ = nullptr;
        MOJOSHADER_effect* effectData_ = nullptr;
        MOJOSHADER_effectStateChanges stateChanges_{};
        CompiledEffectDescription description_;
        std::unordered_map<std::string, std::uint32_t> samplerTextureParameters_;
        std::vector<Texture*> textures_;
        std::uint32_t techniqueIndex_ = 0;
        bool passActive_ = false;
        std::array<Microsoft::Xna::Framework::Graphics::SamplerState, 16> boundSamplers_{};
        std::array<Microsoft::Xna::Framework::Graphics::SamplerState, 16> boundVertexSamplers_{};
        std::array<Texture*, 16> boundSamplerTextures_{};
        std::array<Texture*, 16> boundVertexSamplerTextures_{};
        std::array<bool, 16> samplerAssigned_{};
        std::array<bool, 16> vertexSamplerAssigned_{};
    };
}

#endif  // CNA_METAL_COMPILED_EFFECTS
