// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /**
     * @brief One 2D texture-coordinate transform: scale, then rotate, then translate.
     *
     * @note CNAEXT — not part of the XNA 4.0 API. The shape follows
     * `KHR_texture_transform` directly so imported values do not lose their authored form before
     * reaching a PBR effect. Rotation is counter-clockwise in radians. The identity defaults make
     * an unconfigured effect behave exactly as it did before this property existed.
     */
    CNAEXT struct TextureTransformEXT
    {
        /** @brief Translation applied after scale and rotation. */
        Vector2 Offset{0.0f, 0.0f};
        /** @brief Per-axis scale applied first. */
        Vector2 Scale{1.0f, 1.0f};
        /** @brief Counter-clockwise rotation in radians, applied after scale. */
        float Rotation = 0.0f;

        /** @brief Value equality over all authored components. */
        [[nodiscard]] bool operator==(const TextureTransformEXT&) const = default;
    };
}
