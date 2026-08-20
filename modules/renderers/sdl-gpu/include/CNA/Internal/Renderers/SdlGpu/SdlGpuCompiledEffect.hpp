// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file SdlGpuCompiledEffect.hpp
 * @brief plans/plan_fx.md FX-061: compiled XNA Effect Framework bytecode on the SDL_GPU renderer.
 *
 * MojoShader's own SDL_GPU adapter owns translation, shader creation, program linking and
 * uniform-buffer mapping; the renderer-neutral reflection and state translation live in
 * CNA::Internal::Renderers::MojoShaderEffect, shared with the FNA3D backend. What is left here is
 * this renderer's own share: creating and disposing the native effect against its `SDL_GPUDevice`,
 * selecting a technique, applying a pass, and resolving a texture parameter to an SDL_GPU texture.
 *
 * FX-071 gives both ordinary 3D draws (`SdlGpuRenderer::QueueCompiledEffectDraw`/
 * `GetOrCreatePipelineCompiledEffect`/`IssueCompiledEffectDraw`) and SpriteBatch
 * (`QueueSprite`'s compiled-effect branch, against SpriteVertex's own fixed layout) a real draw
 * route, sharing one implementation (`SdlGpuRenderer::BuildCompiledEffectBindingEXT`/
 * `BindCompiledEffectForDrawEXT`) built from `LinkAndGetShadersEXT`, `CaptureUniformSnapshotEXT`
 * and `GetBoundSamplerEXT` below -- what a draw route captures at queue time, since this renderer
 * defers actual GPU submission to `Present()`.
 *
 * The capability this backs (`GraphicsCapability::CompiledEffects`) reports **true**. This comment
 * used to say it stayed false pending the FX-060 shared conformance suite and a golden-pixel test;
 * both have run since FX-071, and the capability flipped with them. `SdlGpuCompiledEffectTests.cpp`
 * runs the shared suite and every drawing section of it.
 *
 * What is still refused, explicitly and by name rather than drawn with a stock shader (plans/plan_fx.md
 * section 10.5 classifies each):
 *
 * - a compiled effect's vertex shader sampling a texture -- renderer-wide, since no CNA renderer
 *   implements vertex-stage sampling through the public surface at all (FX-109);
 * - a 3D or cube texture bound to a compiled sampler -- compiled-Effect-specific, since this
 *   renderer samples both in its ordinary draw families (FX-110);
 * - more than one vertex stream -- renderer-wide, and `GraphicsDevice` refuses it before this
 *   layer is reached because `MultiStreamVertexInput` is false here.
 *
 * A `RenderTarget2D` IS accepted as a compiled sampler's source since FX-099; this renderer's
 * targets store rows the same way up as an uploaded texture, so nothing has to be corrected.
 */

#if defined(CNA_SDL_GPU_COMPILED_EFFECTS)

#include "CNA/CNAHelper.hpp"
#include "CNA/Internal/Renderers/Common/ICompiledEffectRuntime.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"

#include "mojoshader.h"

#include <SDL3/SDL.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace CNA::Internal::Renderers::SdlGpu
{
    class SdlGpuRenderer;

    /**
     * @brief One compiled XNA effect owned by the SDL_GPU renderer.
     */
    class SdlGpuCompiledEffect final : public ICompiledEffectRuntime
    {
    public:
        /**
         * @brief Parses and compiles an Effect Framework binary for this renderer's device.
         *
         * @param renderer Owning renderer; supplies the MojoShader context and the device.
         * @param effectCode Compiled effect bytes.
         * @param effectCodeLength Number of bytes in @p effectCode.
         * @throws std::invalid_argument if the buffer is empty or implausibly large.
         * @throws std::runtime_error if the parser rejects the content.
         */
        SdlGpuCompiledEffect(SdlGpuRenderer& renderer,
                             const std::uint8_t* effectCode,
                             std::size_t effectCodeLength);

        /** @brief Releases the native effect. */
        ~SdlGpuCompiledEffect() override;

        SdlGpuCompiledEffect(const SdlGpuCompiledEffect&) = delete;
        SdlGpuCompiledEffect& operator=(const SdlGpuCompiledEffect&) = delete;

        /** @brief Creates an independent copy carrying the same current values. */
        [[nodiscard]] std::unique_ptr<ICompiledEffectRuntime> Clone() const override;

        /** @brief Returns the reflection the public Effect object graph is built from. */
        [[nodiscard]] const CompiledEffectDescription& GetDescription() const override;

        /**
         * @brief Selects a technique by its stable zero-based index.
         * @param techniqueIndex Index into the reflected technique list.
         * @throws std::out_of_range if the index is not a technique of this effect.
         */
        void SetTechnique(std::uint32_t techniqueIndex) override;

        /**
         * @brief Replaces one top-level parameter's padded raw value storage.
         * @param runtimeIndex Reflected parameter index.
         * @param data Padded value bytes.
         * @param dataBytes Number of bytes at @p data.
         * @throws std::out_of_range if the parameter does not exist.
         * @throws std::invalid_argument if the value does not fit the parameter's storage.
         */
        void SetParameterValue(std::uint32_t runtimeIndex,
                               const void* data,
                               std::size_t dataBytes) override;

        /**
         * @brief Associates a texture parameter with a texture this renderer owns.
         * @param runtimeIndex Reflected parameter index.
         * @param texture Texture to bind, or null to clear.
         * @throws std::out_of_range if the parameter does not exist.
         * @throws std::invalid_argument if the parameter is not a texture, or the texture was not
         *         created by this renderer.
         */
        void SetParameterTexture(std::uint32_t runtimeIndex, Texture* texture) override;

        /**
         * @brief Applies one pass of the selected technique and reports the state it assigned.
         * @param passIndex Zero-based pass index inside the selected technique.
         * @param deviceState State groups currently selected on the owning GraphicsDevice.
         * @param changes Receives every state group the pass assigned.
         * @throws std::out_of_range if the pass does not exist.
         * @throws std::runtime_error if the native apply reports implausible state changes.
         */
        void ApplyPass(std::uint32_t passIndex,
                       const CompiledEffectDeviceState& deviceState,
                       CompiledEffectPassStateChanges& changes) override;

        /**
         * @brief CNAEXT. Returns the shader pair the last applied pass bound, or nulls.
         *
         * This is what a compiled-effect draw route will need from here. It is exposed now so the
         * pair is observable and testable before that route exists.
         * @param vertex Receives the vertex shader data, or null.
         * @param pixel Receives the pixel shader data, or null.
         */
        CNAEXT void GetBoundShadersEXT(MOJOSHADER_sdlShaderData*& vertex,
                                       MOJOSHADER_sdlShaderData*& pixel) const;

        /**
         * @brief CNAEXT. Packs the currently applied pass's constant register values into the exact
         * byte layout its vertex and pixel shaders' uniform buffers expect.
         *
         * plans/plan_fx.md FX-071: this renderer defers a draw's actual GPU submission to `Present()`, by
         * when a later `ApplyPass()` on this same effect (or a different one sharing this renderer's
         * one MojoShader context) may have overwritten the native constant register files. Capturing
         * the packed bytes immediately after `ApplyPass()` -- while the register files still hold
         * exactly the values that call wrote -- is what makes a deferred draw command self-contained.
         * The packing itself mirrors MojoShader's own `mojoshader_sdlgpu.c` `update_uniform_buffer`
         * (float/int registers copied 16 bytes per element; a bool register's value occupies only the
         * low 4 bytes of its 16-byte slot, matching that function's own layout) using only the public
         * `MOJOSHADER_sdlMapUniformBufferMemory` accessor, since the register files themselves are not
         * exposed by any other public API.
         *
         * @param vertexBytes Receives the packed vertex-shader uniform buffer bytes; empty if the
         *        applied vertex shader declares no uniform buffer.
         * @param pixelBytes Receives the packed pixel-shader uniform buffer bytes; empty if the
         *        applied pixel shader declares no uniform buffer.
         */
        CNAEXT void CaptureUniformSnapshotEXT(std::vector<std::uint8_t>& vertexBytes,
                                              std::vector<std::uint8_t>& pixelBytes) const;

        /**
         * @brief CNAEXT. Returns the texture and sampler state currently bound to one sampler slot.
         *
         * plans/plan_fx.md FX-071: this renderer does not route a compiled effect's texture/sampler
         * bindings through `GraphicsDevice`'s public collections the way stock effects' fixed
         * texture0/texture1/envMap slots do (a compiled effect's bindings are arbitrary, and its
         * texture may be 2D, 3D or a cube, which `GpuDrawParams` has no single field type for).
         * `ApplyPass()` keeps this state persistent across passes/techniques, matching real XNA
         * behavior: a pass that reassigns nothing leaves an earlier pass's binding standing.
         *
         * @param slot Sampler register index.
         * @param vertexStage True for a vertex-stage sampler, false for a pixel-stage one.
         * @param texture Receives the bound texture, or null if the slot was never assigned.
         * @param sampler Receives the bound sampler state; only meaningful when @p texture is
         *        non-null.
         */
        CNAEXT void GetBoundSamplerEXT(
            std::uint32_t slot, bool vertexStage, Texture*& texture,
            Microsoft::Xna::Framework::Graphics::SamplerState& sampler,
            bool* samplerAssigned = nullptr) const;

        /**
         * @brief CNAEXT. Links the currently applied pass's shader pair and returns the native
         * shader modules and vertex attribute set a draw's pipeline is built from.
         *
         * `MOJOSHADER_sdlBindShaders` -- the effect-framework backend callback `ApplyPass()` drives
         * -- only records which shader *data* (reflection) is bound; it does not link a program or
         * produce `SDL_GPUShader` handles (`GetBoundShadersEXT` above returns exactly that shader
         * data, not shader modules). This performs the separate, explicit linking step
         * `MOJOSHADER_sdlLinkProgram` documents, using @p declaredElements to patch the linked
         * SPIR-V vertex shader's non-float input types to match the actual vertex buffer format.
         * MojoShader caches linked programs by shader identity and vertex attribute list for this
         * context's whole lifetime, so the returned handles stay valid after a later pass links
         * something else -- safe to store for a deferred draw.
         *
         * @param declaredElements The caller's `VertexDeclaration` elements for this draw.
         * @param vertexShader Receives the linked vertex shader module.
         * @param pixelShader Receives the linked pixel shader module.
         * @return The pipeline's vertex attribute set, one entry per shader input.
         * @throws std::runtime_error if the applied pass bound no shader pair, or linking fails.
         * @throws System::NotSupportedException if @p declaredElements does not supply an input the
         *         vertex shader consumes.
         */
        CNAEXT [[nodiscard]] std::vector<SDL_GPUVertexAttribute> LinkAndGetShadersEXT(
            const std::vector<Microsoft::Xna::Framework::Graphics::VertexElement>& declaredElements,
            SDL_GPUShader*& vertexShader, SDL_GPUShader*& pixelShader) const;

    private:
        SdlGpuCompiledEffect(SdlGpuRenderer& renderer, const SdlGpuCompiledEffect& cloneSource);

        SdlGpuRenderer& renderer_;
        MOJOSHADER_sdlContext* context_ = nullptr;
        MOJOSHADER_effect* effectData_ = nullptr;
        MOJOSHADER_effectStateChanges stateChanges_{};
        CompiledEffectDescription description_;
        std::unordered_map<std::string, std::uint32_t> samplerTextureParameters_;
        std::vector<Texture*> textures_;
        std::uint32_t techniqueIndex_ = 0;
        bool passActive_ = false;
        // FX-071: persistent per-slot sampler/texture state, updated from every ApplyPass()'s own
        // CompiledEffectSamplerChange list -- see GetBoundSamplerEXT's own doc comment for why this
        // exists instead of reading GraphicsDevice.
        std::array<Texture*, Microsoft::Xna::Framework::Graphics::SamplerStateCollection::MaxSamplers>
            boundTextures_{};
        std::array<Texture*, Microsoft::Xna::Framework::Graphics::SamplerStateCollection::MaxSamplers>
            boundVertexTextures_{};
        std::array<Microsoft::Xna::Framework::Graphics::SamplerState,
                   Microsoft::Xna::Framework::Graphics::SamplerStateCollection::MaxSamplers>
            boundSamplers_{};
        std::array<Microsoft::Xna::Framework::Graphics::SamplerState,
                   Microsoft::Xna::Framework::Graphics::SamplerStateCollection::MaxSamplers>
            boundVertexSamplers_{};
        // plans/plan_fx.md FX-083: which of those slots an applied pass has actually assigned. A
        // default-constructed SamplerState is a legitimate value, so "assigned" cannot be inferred
        // from the value -- and an unassigned slot must keep whatever the game selected on the
        // device instead of being overwritten with defaults.
        std::array<bool, Microsoft::Xna::Framework::Graphics::SamplerStateCollection::MaxSamplers>
            samplerAssigned_{};
        std::array<bool, Microsoft::Xna::Framework::Graphics::SamplerStateCollection::MaxSamplers>
            vertexSamplerAssigned_{};
    };
}

#endif  // CNA_SDL_GPU_COMPILED_EFFECTS
