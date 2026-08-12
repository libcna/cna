// SPDX-License-Identifier: MS-PL
#pragma once

#include <array>

#include "Microsoft/Xna/Framework/Graphics/AlphaModeEXT.hpp"

/**
 * @file
 * @brief glTF alpha coverage expressed in the alpha-test vector every renderer already reads
 *        (plan_gltf.md `GLTF-372`).
 *
 * glTF §3.9.4 gives a material three alpha-coverage modes; only `MASK` changes what a fragment
 * program does, and what it asks for — *discard below a cutoff* — is exactly what
 * `GpuDrawParams::alphaTest` already expresses for `AlphaTestEffect`. The shaders evaluate
 *
 * ```
 * at = (y > 0) ? (|a - x| < y ? z : w) : ((a < x) ? z : w);   if (at < 0) discard;
 * ```
 *
 * so a `MASK` material is `{cutoff, 0, -1, +1}`: the equality band is unused, alpha below the
 * cutoff selects the negative weight and is discarded, and alpha at or above it is kept. `OPAQUE`
 * and `BLEND` never discard, which is the `{0, 0, 1, 1}` default `GpuDrawParams` already carries.
 *
 * The mapping lives here rather than in either PBR effect because both need it and a second
 * hand-written copy of a four-element sign convention is a defect waiting for a fixture: getting
 * `z`/`w` the wrong way round inverts the cutout, and every renderer would inherit it.
 */

namespace CNA::Internal::Graphics
{
    /**
     * @brief The `GpuDrawParams::alphaTest` vector a glTF alpha-coverage mode requires.
     *
     * @param mode The material's alpha-coverage mode (glTF §3.9.4).
     * @param cutoff The material's `alphaCutoff`; meaningful only for
     *               `AlphaModeEXT::Mask`, and used verbatim rather than quantised — glTF states it
     *               as a float in `[0,1]`, unlike XNA's byte `ReferenceAlpha`, so the half-step
     *               tolerance `AlphaTestEffect` needs has nothing to correct for here.
     * @return `{x, y, z, w}` in the shader's own convention: reference, equality tolerance, then
     *         the pass and fail weights whose sign decides the discard.
     */
    [[nodiscard]] inline std::array<float, 4> AlphaTestVectorForAlphaModeEXT(
        Microsoft::Xna::Framework::Graphics::AlphaModeEXT mode, float cutoff) noexcept
    {
        using Microsoft::Xna::Framework::Graphics::AlphaModeEXT;
        switch (mode)
        {
            case AlphaModeEXT::Mask:
                return {cutoff, 0.0f, -1.0f, 1.0f};
            case AlphaModeEXT::Opaque:
            case AlphaModeEXT::Blend:
                break;
        }
        // Never discard. BLEND's compositing and OPAQUE's "alpha is ignored" are both blend state,
        // which the application owns per draw -- see docs/gltf-api-change-review.md §1.3.
        return {0.0f, 0.0f, 1.0f, 1.0f};
    }
}
