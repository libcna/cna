// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file EasyGLCompiledEffect.hpp
 * @brief plan_fx.md FX-062: compiled XNA Effect Framework bytecode on the EasyGL renderer.
 *
 * MojoShader's own OpenGL adapter (`mojoshader_opengl.c`) owns translation, shader compilation,
 * program linking and uniform pushing; the renderer-neutral reflection and state translation live
 * in `CNA::Internal::Renderers::MojoShaderEffect`, shared with the FNA3D and SDL_GPU backends. What
 * is left here is this renderer's own share: creating and disposing the native effect against this
 * renderer's one `MOJOSHADER_glContext`, selecting a technique, and applying a pass.
 *
 * Unlike the SDL_GPU adapter, this one needs no separate explicit link step and no uniform
 * snapshot captured ahead of a deferred draw: `MOJOSHADER_glBindShaders` (driven internally by
 * `ApplyPass()`'s `bindShaders` backend callback) links AND binds (`glUseProgram`) the program in
 * one step, and EasyGL draws immediately rather than deferring GPU submission to `Present()` the
 * way SDL_GPU does -- so a compiled-effect draw route can read the live shared register files
 * directly at `MOJOSHADER_glProgramReady()` time, in the same synchronous call chain as `ApplyPass`.
 * That draw route lives in `EasyGLRenderer.cpp`'s `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx`
 * (plan_fx.md FX-062), calling the global `MOJOSHADER_gl*` functions directly rather than through
 * a per-effect accessor the way `SdlGpuCompiledEffect::LinkAndGetShadersEXT` does -- MojoShader's
 * OpenGL adapter keeps its "currently bound program" as context-level state, not an object this
 * class would need to hand back.
 *
 * `GraphicsCapability::CompiledEffects` is **true** for this renderer: the FX-060 shared
 * conformance suite passes here in full -- including its draw matrix, multi-stream, instancing,
 * SpriteBatch and orientation sections -- alongside this renderer's own golden-pixel test.
 */

#if defined(CNA_EASYGL_COMPILED_EFFECTS)

#include "CNA/CNAHelper.hpp"
#include "CNA/Internal/Renderers/Common/ICompiledEffectRuntime.hpp"

#include "mojoshader.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace CNA::Internal::Renderers::EasyGL
{
    class EasyGLRenderer;

    /**
     * @brief One compiled XNA effect owned by the EasyGL renderer.
     */
    class EasyGLCompiledEffect final : public ICompiledEffectRuntime
    {
    public:
        /**
         * @brief Parses and compiles an Effect Framework binary for this renderer's GL context.
         *
         * @param renderer Owning renderer; supplies the MojoShader context.
         * @param effectCode Compiled effect bytes.
         * @param effectCodeLength Number of bytes in @p effectCode.
         * @throws std::invalid_argument if the buffer is empty or implausibly large.
         * @throws std::runtime_error if the parser rejects the content.
         */
        EasyGLCompiledEffect(EasyGLRenderer& renderer,
                             const std::uint8_t* effectCode,
                             std::size_t effectCodeLength);

        /** @brief Releases the native effect. */
        ~EasyGLCompiledEffect() override;

        EasyGLCompiledEffect(const EasyGLCompiledEffect&) = delete;
        EasyGLCompiledEffect& operator=(const EasyGLCompiledEffect&) = delete;

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
         * @brief CNAEXT. Returns the texture and sampler state currently bound to one sampler slot.
         *
         * plan_fx.md FX-062: mirrors `SdlGpuCompiledEffect::GetBoundSamplerEXT` for the same reason
         * -- a compiled effect's texture/sampler bindings are arbitrary and do not route through
         * `GraphicsDevice`'s fixed texture0/texture1/envMap slots, and `ApplyPass()` keeps this
         * state persistent across passes/techniques, matching real XNA behavior: a pass that
         * reassigns nothing leaves an earlier pass's binding standing.
         *
         * @param slot Sampler register index.
         * @param vertexStage True for a vertex-stage sampler, false for a pixel-stage one.
         * @param texture Receives the bound texture, or null if the slot was never assigned.
         * @param sampler Receives the bound sampler state; only meaningful when @p samplerAssigned
         *        comes back true.
         * @param samplerAssigned Receives whether any applied pass has assigned sampler state to
         *        this slot at all. plan_fx.md FX-083: the draw route pushes an assigned state to
         *        the GPU, and must leave the slot exactly as the game selected it otherwise --
         *        writing a default-constructed SamplerState there would silently override, for
         *        example, the filter SpriteBatch.Begin just installed.
         */
        CNAEXT void GetBoundSamplerEXT(std::uint32_t slot, bool vertexStage,
                                       Texture*& texture,
                                       Microsoft::Xna::Framework::Graphics::SamplerState& sampler,
                                       bool& samplerAssigned) const;

    private:
        EasyGLCompiledEffect(EasyGLRenderer& renderer, const EasyGLCompiledEffect& cloneSource);

        EasyGLRenderer& renderer_;
        MOJOSHADER_glContext* context_ = nullptr;
        MOJOSHADER_effect* effectData_ = nullptr;
        MOJOSHADER_effectStateChanges stateChanges_{};
        CompiledEffectDescription description_;
        std::unordered_map<std::string, std::uint32_t> samplerTextureParameters_;
        std::vector<Texture*> textures_;
        std::uint32_t techniqueIndex_ = 0;
        bool passActive_ = false;
        // FX-062: persistent per-slot sampler/texture state, updated from every ApplyPass()'s own
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
        // FX-074: which of those slots an applied pass has actually assigned. A default-constructed
        // SamplerState is a legitimate value, so "assigned" cannot be inferred from the value.
        std::array<bool, Microsoft::Xna::Framework::Graphics::SamplerStateCollection::MaxSamplers>
            samplerAssigned_{};
        std::array<bool, Microsoft::Xna::Framework::Graphics::SamplerStateCollection::MaxSamplers>
            vertexSamplerAssigned_{};
    };
}

#endif  // CNA_EASYGL_COMPILED_EFFECTS
