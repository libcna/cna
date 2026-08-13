// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /**
     * @brief How a material's alpha channel is interpreted — glTF 2.0 §3.9.4 `alpha-coverage`.
     *
     * @note CNAEXT — not part of the XNA 4.0 API. XNA has no material-level alpha-coverage concept
     * at all: transparency there is a `BlendState` the application sets per draw, and cutout
     * rendering is `AlphaTestEffect`'s own `ReferenceAlpha`. glTF makes it a property of the
     * *material*, so it needs somewhere to live that travels with the material rather than with the
     * device.
     *
     * Declared here, in the graphics module, rather than on any one effect: `CNAEXT.md` §5.5's
     * engine-layer `CNA::Graphics::PbrMaterial` needs the same three values, and two independently
     * declared alpha-mode enums would only ever converge into a conversion function nobody could
     * delete (`docs/gltf-api-change-review.md` §1.3).
     */
    enum class AlphaModeEXT
    {
        /**
         * @brief The alpha value is ignored and the surface is rendered fully opaque.
         *
         * glTF's default when a material declares no `alphaMode`.
         */
        Opaque = 0,
        /**
         * @brief The surface is rendered fully opaque or not at all, cut at `AlphaCutoff`.
         *
         * A fragment whose alpha is **below** the cutoff is discarded; one at or above it is drawn
         * fully opaque. The comparison is against the cutoff, so a mask material's own alpha never
         * reaches the framebuffer as partial coverage.
         */
        Mask = 1,
        /**
         * @brief The alpha value composites the source and destination, per the usual over operator.
         *
         * PBR effects emit straight RGB, so the application selects
         * `BlendState::NonPremultiplied` and draws BLEND parts back-to-front. CNA preserves the
         * selected state and source part order; it does not sort by default.
         */
        Blend = 2,
    };
}
