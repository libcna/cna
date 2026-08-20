// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file EffectTranslation.hpp
 * @brief Renderer-neutral translation between a parsed MojoShader effect and CNA's own types.
 *
 * plans/plan_fx.md FX-061/FX-062/FX-063/FX-065: every backend that executes compiled XNA effects reads
 * the same parsed `MOJOSHADER_effect` and has to turn it into the same CNA reflection, render
 * states and sampler states. Only the native calls differ -- creating the effect, selecting a
 * technique, applying a pass, and binding a texture and sampler to a slot.
 *
 * This is that shared half, extracted from the FNA3D renderer once a second backend needed it. It
 * depends on MojoShader and on CNA's graphics types, and on no graphics API at all.
 */

#include "CNA/Internal/Renderers/Common/ICompiledEffectRuntime.hpp"

#include "mojoshader.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace CNA::Internal::Renderers::MojoShaderEffect
{
    using Microsoft::Xna::Framework::Graphics::Texture;

    /**
     * @brief Rejects a parsed effect CNA cannot safely reflect.
     *
     * Checks the parser's own error list and every count the reflection walk will trust, so a
     * malformed binary fails with a specific message before anything reads its tables.
     *
     * @param effectData Parsed effect, or null.
     * @param operation Operation name used in the thrown message.
     * @throws std::runtime_error if the effect is missing, carries parser errors, or declares
     *         counts outside CNA's limits.
     */
    void ValidateNativeEffect(const MOJOSHADER_effect* effectData, const char* operation);

    /**
     * @brief Answers whether MOJOSHADER_deleteEffect may be called on a parse result.
     *
     * Several parse failures are reported as static sentinel objects rather than allocations --
     * out of memory, no backend, unexpected end of file, not an effect. The pinned
     * MOJOSHADER_deleteEffect guards against only one of them, and walking any of the others as
     * if it owned heap storage crashes. A normally allocated parse tree always carries a resolved
     * free callback, which is what distinguishes it.
     *
     * @param effectData Parse result, or null.
     * @return True if the parse result owns storage that must be released.
     */
    [[nodiscard]] bool CanSafelyDeleteNativeEffect(const MOJOSHADER_effect* effectData);

    /**
     * @brief Builds the renderer-neutral reflection the public Effect object graph is made from.
     *
     * @param effectData Parsed effect, already validated.
     * @return Parameters, techniques and passes with their names, types, dimensions and
     *         annotations.
     * @throws std::runtime_error if the reflection exceeds CNA's item, depth or storage limits.
     */
    [[nodiscard]] CompiledEffectDescription BuildDescription(const MOJOSHADER_effect* effectData);

    /**
     * @brief Maps each sampler's name to the runtime index of the texture parameter it samples.
     *
     * A pass assigns a texture by naming a parameter, so this is what lets a sampler register
     * resolve to the texture the game set on that parameter.
     *
     * @param effectData Parsed effect, already validated.
     * @return Sampler name to texture parameter runtime index.
     */
    [[nodiscard]] std::unordered_map<std::string, std::uint32_t> BuildSamplerTextureParameterMap(
        const MOJOSHADER_effect* effectData);

    /**
     * @brief Translates one pass's Direct3D 9 render-state assignments into CNA state objects.
     *
     * A pass assigns individual legacy render states, so each group is rebuilt from the state
     * currently selected on the device and published only if the pass touched that group.
     *
     * @param stateChanges Render-state changes MojoShader reported for the applied pass.
     * @param deviceState State groups currently selected on the owning GraphicsDevice.
     * @param changes Receives every group the pass assigned.
     * @throws std::runtime_error on a render state CNA does not support.
     */
    void TranslateRenderStates(const MOJOSHADER_effectStateChanges& stateChanges,
                               const CompiledEffectDeviceState& deviceState,
                               CompiledEffectPassStateChanges& changes);

    /**
     * @brief Translates one shader stage's sampler-state registers into CNA sampler changes.
     *
     * Appends to @p changes rather than binding anything: a backend walks the appended entries and
     * performs its own native binding, which is the only part of this that differs between them.
     *
     * @param changeList Sampler registers MojoShader reported for the applied pass.
     * @param count Number of registers in @p changeList.
     * @param vertexStage Whether these registers belong to the vertex stage.
     * @param maxSlots Number of sampler slots the calling renderer has for that stage.
     * @param samplerTextureParameters Map from BuildSamplerTextureParameterMap().
     * @param textures Current texture per parameter runtime index.
     * @param deviceState State groups currently selected on the owning GraphicsDevice.
     * @param changes Receives one entry per slot the pass changed.
     * @throws std::runtime_error on an out-of-range register, a missing value, or an unsupported
     *         sampler state.
     */
    void TranslateSamplers(const MOJOSHADER_samplerStateRegister* changeList,
                           std::uint32_t count, bool vertexStage, std::size_t maxSlots,
                           const std::unordered_map<std::string, std::uint32_t>&
                               samplerTextureParameters,
                           const std::vector<Texture*>& textures,
                           const CompiledEffectDeviceState& deviceState,
                           CompiledEffectPassStateChanges& changes);
}
