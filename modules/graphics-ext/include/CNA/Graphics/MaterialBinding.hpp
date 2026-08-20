// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/PbrMaterial.hpp"

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
    class PbrEffect;
    class SkinnedPbrEffect;
}

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief Writes every field of a material onto a `PbrEffect`.
     *
     * plans/plan_modern.md `MOD-1303`. Free functions rather than methods on either type, for the
     * layering reason that runs through this whole module: `PbrMaterial` is engine-layer and
     * compiled out by default, `PbrEffect` is XNA-layer and always compiled, and neither may
     * depend on the other. A function that knows both is the only shape that leaves both alone.
     *
     * Matrices, lights, fog, shadows and image-based lighting are untouched -- a material
     * describes a surface, not the scene it stands in, so applying one to an effect already
     * configured for a frame does not disturb that configuration.
     *
     * @param material The material to apply.
     * @param effect   The effect to write to.
     */
    void applyMaterial(const PbrMaterial& material,
                       Microsoft::Xna::Framework::Graphics::PbrEffect& effect);

    /**
     * @brief Writes every field of a material onto a `SkinnedPbrEffect`.
     *
     * plans/plan_modern.md `MOD-1304`. The same coverage as the rigid overload; the skinning state
     * (bone palette, weights per vertex) is scene data and is left alone.
     *
     * @param material The material to apply.
     * @param effect   The effect to write to.
     */
    void applyMaterial(const PbrMaterial& material,
                       Microsoft::Xna::Framework::Graphics::SkinnedPbrEffect& effect);

    /**
     * @brief Reads a material back out of a `PbrEffect`.
     *
     * plans/plan_modern.md `MOD-1305`. `extractMaterial(effect)` after `applyMaterial(material, effect)`
     * returns a material equal to the original -- that round trip is what makes this type usable
     * as a serialization form, and it is asserted field by field in the tests.
     *
     * @param effect The effect to read.
     * @return The material it is currently configured with.
     */
    [[nodiscard]] PbrMaterial
    extractMaterial(const Microsoft::Xna::Framework::Graphics::PbrEffect& effect);

    /**
     * @brief Reads a material back out of a `SkinnedPbrEffect`.
     *
     * @param effect The effect to read.
     * @return The material it is currently configured with.
     */
    [[nodiscard]] PbrMaterial
    extractMaterial(const Microsoft::Xna::Framework::Graphics::SkinnedPbrEffect& effect);

    /**
     * @brief Applies the device state a material implies: blending and face culling.
     *
     * plans/plan_modern.md `MOD-1306`/`MOD-1307`. Separate from @ref applyMaterial, and deliberately
     * something the application calls rather than something a draw does behind its back:
     * `PbrEffect::getDoubleSidedEXTProperty` is documented as carried state precisely because a
     * `Model::Draw` that mutated `BlendState` as a side effect would surprise every XNA caller.
     * This function is the convenience for an application that has decided it wants that; it is
     * not a policy the engine applies on its own.
     *
     * The mapping:
     * - `Opaque` and `Mask` set `BlendState::Opaque`. `Mask` needs no blending: the cutout is a
     *   discard in the shader, driven by the effect's own alpha mode and cutoff.
     * - `Blend` sets `BlendState::NonPremultiplied`, because PBR effects emit straight (not
     *   premultiplied) RGB. Draw order still belongs to the application: CNA does not sort.
     * - `doubleSided` selects `RasterizerState::CullNone`, otherwise
     *   `RasterizerState::CullCounterClockwise` -- XNA's own default, and glTF's.
     *
     * @param material The material whose coverage and sidedness to apply.
     * @param device   The device to set state on.
     */
    void applyMaterialState(const PbrMaterial& material,
                            Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
