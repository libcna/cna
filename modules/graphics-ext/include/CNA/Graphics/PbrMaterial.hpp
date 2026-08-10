// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_NOXNA

#include "Microsoft/Xna/Framework/Color.hpp"

namespace Microsoft::Xna::Framework::Graphics { class Texture2D; }

namespace CNA::Graphics {
    /**
     * @brief PBR material definition for the NOXNA extended render layer.
     *
     * Stores texture slots and scalar parameters following the glTF 2.0
     * metallic-roughness PBR material model.  Textures are non-owning
     * pointers — the caller is responsible for lifetime management.
     *
     * This class is a settings bag only.  Actual PBR rendering requires a
     * matching `PbrEffect` (Task N52) that reads these values and applies
     * them through a PBR GLSL/SPIR-V shader.
     */
    class PbrMaterial
    {
    public:
        /** @brief Constructs a default PBR material (dielectric, mid-grey, no textures). */
        PbrMaterial();

        // ── Texture slots ────────────────────────────────────────────────────

        /** @brief Returns the albedo (base colour) texture, or nullptr. */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::Texture2D* getAlbedoTexture() const;
        /** @brief Sets the albedo (base colour) texture. Passing nullptr clears the slot. */
        void setAlbedoTexture(Microsoft::Xna::Framework::Graphics::Texture2D* texture);

        /** @brief Returns the normal map texture (tangent-space), or nullptr. */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::Texture2D* getNormalTexture() const;
        /** @brief Sets the normal map texture. */
        void setNormalTexture(Microsoft::Xna::Framework::Graphics::Texture2D* texture);

        /**
         * @brief Returns the metallic-roughness texture, or nullptr.
         *
         * Channel convention (glTF 2.0): B = metallic, G = roughness.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::Texture2D* getMetallicRoughnessTexture() const;
        /** @brief Sets the metallic-roughness texture. */
        void setMetallicRoughnessTexture(Microsoft::Xna::Framework::Graphics::Texture2D* texture);

        /** @brief Returns the ambient occlusion texture, or nullptr. */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::Texture2D* getAmbientOcclusionTexture() const;
        /** @brief Sets the ambient occlusion texture (R channel). */
        void setAmbientOcclusionTexture(Microsoft::Xna::Framework::Graphics::Texture2D* texture);

        /** @brief Returns the emissive texture, or nullptr. */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::Texture2D* getEmissiveTexture() const;
        /** @brief Sets the emissive texture. */
        void setEmissiveTexture(Microsoft::Xna::Framework::Graphics::Texture2D* texture);

        // ── Scalar / colour factors ──────────────────────────────────────────

        /** @brief Returns the albedo colour factor (multiplied with the albedo texture). */
        [[nodiscard]] Microsoft::Xna::Framework::Color getAlbedoColor() const;
        /** @brief Sets the albedo colour factor. */
        void setAlbedoColor(Microsoft::Xna::Framework::Color color);

        /** @brief Returns the metallic factor in [0,1]. */
        [[nodiscard]] float getMetallicFactor() const;
        /** @brief Sets the metallic factor in [0,1]. */
        void setMetallicFactor(float value);

        /** @brief Returns the roughness factor in [0,1]. */
        [[nodiscard]] float getRoughnessFactor() const;
        /** @brief Sets the roughness factor in [0,1]. */
        void setRoughnessFactor(float value);

        /** @brief Returns the emissive colour factor. */
        [[nodiscard]] Microsoft::Xna::Framework::Color getEmissiveColor() const;
        /** @brief Sets the emissive colour factor. */
        void setEmissiveColor(Microsoft::Xna::Framework::Color color);

        /** @brief Returns the normal map intensity scale (1.0 = full strength). */
        [[nodiscard]] float getNormalScale() const;
        /** @brief Sets the normal map intensity scale. */
        void setNormalScale(float value);

        /** @brief Returns the ambient occlusion strength in [0,1]. */
        [[nodiscard]] float getOcclusionStrength() const;
        /** @brief Sets the ambient occlusion strength. */
        void setOcclusionStrength(float value);

        /** @brief Returns true if this material should be rendered with alpha blending. */
        [[nodiscard]] bool isAlphaBlendEnabled() const;
        /** @brief Enables or disables alpha blending for this material. */
        void setAlphaBlendEnabled(bool value);

        /** @brief Returns the alpha cutoff threshold for AlphaTest mode. */
        [[nodiscard]] float getAlphaCutoff() const;
        /** @brief Sets the alpha cutoff threshold. */
        void setAlphaCutoff(float value);

    private:
        using Tex2D = Microsoft::Xna::Framework::Graphics::Texture2D;
        using Color = Microsoft::Xna::Framework::Color;

        Tex2D* albedoTexture_            = nullptr;
        Tex2D* normalTexture_            = nullptr;
        Tex2D* metallicRoughnessTexture_ = nullptr;
        Tex2D* ambientOcclusionTexture_  = nullptr;
        Tex2D* emissiveTexture_          = nullptr;

        Color  albedoColor_    { 255, 255, 255, 255 };
        float  metallicFactor_  = 0.0f;
        float  roughnessFactor_ = 0.5f;
        Color  emissiveColor_  { 0, 0, 0, 255 };
        float  normalScale_     = 1.0f;
        float  occlusionStrength_ = 1.0f;
        bool   alphaBlend_      = false;
        float  alphaCutoff_     = 0.5f;
    };

} // namespace CNA::Graphics

#endif // CNA_NOXNA
