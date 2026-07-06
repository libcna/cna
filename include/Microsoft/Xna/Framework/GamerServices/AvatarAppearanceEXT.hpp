// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    /**
     * @brief CNA-original avatar body customization (skin tone, hair, and per-garment-slot
     * clothing tint).
     *
     * @note NOXNA — not part of the XNA 4.0 API. CNA extension used only by
     * AvatarRenderer::EnableRealRenderingEXT's real-rendering path. This is not a
     * reproduction of Microsoft's proprietary, undocumented 1021-byte AvatarDescription
     * format (which was never public and cannot be reverse-engineered from the reference
     * assembly alone) — it is a wholly new, CNA-invented data model. Tint only, no texture:
     * the content pipeline does not yet export per-part textures (Task 11.19).
     */
    NOXNA struct AvatarAppearanceEXT
    {
        /**
         * @brief Gets the skin tint color applied to the body part.
         * @return The current skin color.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Color getSkinColorProperty() const { return skinColor_; }

        /**
         * @brief Sets the skin tint color applied to the body part.
         * @param value The new skin color.
         */
        void setSkinColorProperty(Microsoft::Xna::Framework::Color value) { skinColor_ = value; }

        /**
         * @brief Gets the hair tint color applied to the hair part.
         * @return The current hair color.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Color getHairColorProperty() const { return hairColor_; }

        /**
         * @brief Sets the hair tint color applied to the hair part.
         * @param value The new hair color.
         */
        void setHairColorProperty(Microsoft::Xna::Framework::Color value) { hairColor_ = value; }

        /**
         * @brief Gets the tint color applied to the Shirt garment slot part.
         * @return The current shirt color.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Color getShirtColorProperty() const { return shirtColor_; }

        /**
         * @brief Sets the tint color applied to the Shirt garment slot part.
         * @param value The new shirt color.
         */
        void setShirtColorProperty(Microsoft::Xna::Framework::Color value) { shirtColor_ = value; }

        /**
         * @brief Gets the tint color applied to the Pants garment slot part.
         * @return The current pants color.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Color getPantsColorProperty() const { return pantsColor_; }

        /**
         * @brief Sets the tint color applied to the Pants garment slot part.
         * @param value The new pants color.
         */
        void setPantsColorProperty(Microsoft::Xna::Framework::Color value) { pantsColor_ = value; }

        /**
         * @brief Gets the tint color applied to the Shoes garment slot part.
         * @return The current shoes color.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Color getShoesColorProperty() const { return shoesColor_; }

        /**
         * @brief Sets the tint color applied to the Shoes garment slot part.
         * @param value The new shoes color.
         */
        void setShoesColorProperty(Microsoft::Xna::Framework::Color value) { shoesColor_ = value; }

    private:
        Microsoft::Xna::Framework::Color skinColor_ = Microsoft::Xna::Framework::Color::NavajoWhite;
        Microsoft::Xna::Framework::Color hairColor_ = Microsoft::Xna::Framework::Color::SaddleBrown;
        // Defaults mirror tools/avatar_builder/generate_materials.py's placeholder
        // MATERIAL_COLORS so an untouched AvatarAppearanceEXT looks the same as the
        // Blender-side preview, not an arbitrary unrelated palette.
        Microsoft::Xna::Framework::Color shirtColor_ = Microsoft::Xna::Framework::Color(0.20f, 0.35f, 0.60f);
        Microsoft::Xna::Framework::Color pantsColor_ = Microsoft::Xna::Framework::Color(0.15f, 0.15f, 0.20f);
        Microsoft::Xna::Framework::Color shoesColor_ = Microsoft::Xna::Framework::Color(0.05f, 0.05f, 0.05f);
    };
}
