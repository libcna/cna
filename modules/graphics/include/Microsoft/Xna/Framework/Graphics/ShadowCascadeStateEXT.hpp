// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"

namespace Microsoft::Xna::Framework::Graphics {

    /**
     * @brief Everything a lit effect needs in order to sample a cascaded shadow map.
     *
     * CNAEXT: not part of the XNA 4.0 API.
     *
     * A struct rather than a dozen setters on `IShadowReceiverEXT`, for a reason that is about
     * correctness and not only about typing: these values are only meaningful together. A matrix
     * from this frame paired with a split distance from the last one produces a shadow that is
     * subtly in the wrong cascade, which looks like a resolution artefact rather than like the
     * torn update it is. Passing them as one value makes that pairing impossible.
     *
     * Always compiled, like `IShadowReceiverEXT` itself, so an effect's public surface does not
     * change with a build flag. The engine layer's `CNA::Graphics::CascadedShadowMap` fills it.
     */
    CNAEXT struct ShadowCascadeStateEXT
    {
        /** @brief Largest number of cascades a receiver shader carries. */
        static constexpr int kMaxCascades = 4;

        /**
         * @brief How many cascades are in use; 0 means "not cascaded", and every field below is
         *        then ignored, which is what leaves an ordinary single-map draw untouched.
         */
        int Count = 0;

        /**
         * @brief World space to shadow-atlas space, per cascade, with the cascade's own slice of
         *        the atlas already applied.
         */
        Matrix WorldToAtlas[kMaxCascades]{};

        /** @brief View-space depth at which each cascade stops being used, ascending. */
        float SplitDistance[kMaxCascades]{};

        /**
         * @brief The camera's view matrix, from which the receiver derives a fragment's view depth.
         *
         * Kept here rather than taken from the effect's own `View`, because a shadow-receiving
         * draw may legitimately be rendered from a different camera than the one the cascades were
         * fitted to -- a reflection, say -- and the cascade selection must follow the fitting.
         */
        Matrix CameraView{};

        /**
         * @brief Width of the cross-fade between neighbouring cascades, in view-depth units.
         *
         * 0 switches hard at the split, which draws a visible line across the ground wherever the
         * two cascades disagree about an edge.
         */
        float BlendBand = 0.0f;

        /** @brief Tints each cascade a distinct colour. A debugging aid; off by default. */
        bool DebugTint = false;
    };

} // namespace Microsoft::Xna::Framework::Graphics
