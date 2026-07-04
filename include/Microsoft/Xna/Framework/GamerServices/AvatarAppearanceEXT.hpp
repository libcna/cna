// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    /**
     * @brief CNA-original avatar body customization (skin tone, hair color).
     *
     * @note NOXNA — not part of the XNA 4.0 API. CNA extension used only by
     * AvatarRenderer::EnableRealRenderingEXT's real-rendering path. This is not a
     * reproduction of Microsoft's proprietary, undocumented 1021-byte AvatarDescription
     * format (which was never public and cannot be reverse-engineered from the reference
     * assembly alone) — it is a wholly new, CNA-invented data model. No clothing
     * customization is provided; only skin and hair tint.
     */
    NOXNA struct AvatarAppearanceEXT
    {
        /**
         * @brief Gets the skin tint color applied to the "body" part.
         * @return The current skin color.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Color getSkinColorProperty() const { return skinColor_; }

        /**
         * @brief Sets the skin tint color applied to the "body" part.
         * @param value The new skin color.
         */
        void setSkinColorProperty(Microsoft::Xna::Framework::Color value) { skinColor_ = value; }

        /**
         * @brief Gets the hair tint color applied to the "hair" part.
         * @return The current hair color.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Color getHairColorProperty() const { return hairColor_; }

        /**
         * @brief Sets the hair tint color applied to the "hair" part.
         * @param value The new hair color.
         */
        void setHairColorProperty(Microsoft::Xna::Framework::Color value) { hairColor_ = value; }

    private:
        Microsoft::Xna::Framework::Color skinColor_ = Microsoft::Xna::Framework::Color::NavajoWhite;
        Microsoft::Xna::Framework::Color hairColor_ = Microsoft::Xna::Framework::Color::SaddleBrown;
    };
}
