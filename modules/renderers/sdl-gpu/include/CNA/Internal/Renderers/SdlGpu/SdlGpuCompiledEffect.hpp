// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file SdlGpuCompiledEffect.hpp
 * @brief plan_fx.md FX-061: compiled XNA Effect Framework bytecode on the SDL_GPU renderer.
 *
 * MojoShader's own SDL_GPU adapter owns translation, shader creation, program linking and
 * uniform-buffer mapping; the renderer-neutral reflection and state translation live in
 * CNA::Internal::Renderers::MojoShaderEffect, shared with the FNA3D backend. What is left here is
 * this renderer's own share: creating and disposing the native effect against its `SDL_GPUDevice`,
 * selecting a technique, applying a pass, and resolving a texture parameter to an SDL_GPU texture.
 *
 * The capability this backs (`GraphicsCapability::CompiledEffects`) stays **false** until draws
 * execute the effect's own shaders. The runtime below is complete and observable through the XNA
 * API, but this renderer's draw path selects one of eight built-in shader variants by vertex
 * stride, and until a compiled pass can take over that selection, advertising support would mean
 * silently drawing with a stock shader -- which plan_fx.md forbids explicitly.
 */

#if defined(CNA_SDL_GPU_COMPILED_EFFECTS)

#include "CNA/CNAHelper.hpp"
#include "CNA/Internal/Renderers/Common/ICompiledEffectRuntime.hpp"

#include "mojoshader.h"

#include <SDL3/SDL.h>

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
         * plan_fx.md FX-071: this renderer defers a draw's actual GPU submission to `Present()`, by
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
    };
}

#endif  // CNA_SDL_GPU_COMPILED_EFFECTS
