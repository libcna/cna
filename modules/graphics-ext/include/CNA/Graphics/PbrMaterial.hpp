// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaModeEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureTransformEXT.hpp"

#include <array>
#include <cstddef>
#include <string>

namespace Microsoft::Xna::Framework::Graphics { class Texture2D; }

namespace CNA::Graphics {

    /**
     * @brief Which of a PBR material's texture slots a per-slot setting applies to.
     *
     * The order is the one `PbrEffect` uses for its own packed UV selectors and texture
     * transforms, so a slot index never has to be translated between the two.
     */
    enum class PbrTextureSlot
    {
        /** @brief Base colour (albedo). */
        BaseColor = 0,
        /** @brief Tangent-space normal map. */
        Normal = 1,
        /** @brief Metallic-roughness map (glTF packing: G = roughness, B = metallic). */
        MetallicRoughness = 2,
        /** @brief Emissive map. */
        Emissive = 3,
        /** @brief Ambient-occlusion map (R channel). */
        Occlusion = 4,
        /** @brief `KHR_materials_specular` strength map (A channel). */
        Specular = 5,
        /** @brief `KHR_materials_specular` colour map (RGB). */
        SpecularColor = 6,
    };

    /** @brief How many texture slots a `PbrMaterial` describes. */
    inline constexpr int kPbrTextureSlotCount = 7;

    /**
     * @brief A complete, storable description of what `PbrEffect` can render.
     *
     * The glTF 2.0 metallic-roughness model, plus the extensions CNA's `PbrEffect` implements
     * (`KHR_texture_transform`, `KHR_materials_ior`, `KHR_materials_specular`,
     * `KHR_materials_emissive_strength`) and the three alpha-coverage modes. Textures are
     * non-owning pointers; see the ownership note below.
     *
     * **The point of this type is that it is lossless** (plan_modern.md `MOD-1300`–`MOD-1305`).
     * Every field here corresponds to exactly one piece of `PbrEffect` state, so
     * `applyMaterial(material, effect)` followed by `extractMaterial(effect)` returns an equal
     * material. Before Phase 13 it described a subset and quietly dropped the rest, which made it
     * unusable as the serialization form it was meant to be.
     *
     * The field-for-field mapping onto `PbrEffect` lives in `docs/cnaext-engine-layer.md`
     * ("Materials"), in one place rather than two that would drift apart.
     *
     * **What it deliberately does not carry**: matrices, lights, fog, shadows and image-based
     * lighting. Those describe the *scene* a material is drawn in, not the material, and putting
     * them here would make two draws of the same material in different places two materials.
     *
     * **Ownership** (`MOD-1314`): a material never owns its textures. It is a description, and a
     * description that owned GPU resources could not be copied freely or held by value in a
     * container of materials, which is exactly how it is meant to be used. Where an effect needs
     * to keep a texture alive, `PbrEffect::SetOwned*` is the mechanism, and it stays on the effect.
     */
    class PbrMaterial
    {
    public:
        /** @brief Constructs the glTF default material: opaque, white, fully rough, dielectric. */
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

        /** @brief Returns the `KHR_materials_specular` strength map (A channel), or nullptr. */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::Texture2D* getSpecularTexture() const;
        /** @brief Sets the specular-strength map. */
        void setSpecularTexture(Microsoft::Xna::Framework::Graphics::Texture2D* texture);

        /** @brief Returns the `KHR_materials_specular` colour map (RGB), or nullptr. */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::Texture2D* getSpecularColorTexture() const;
        /** @brief Sets the specular-colour map. */
        void setSpecularColorTexture(Microsoft::Xna::Framework::Graphics::Texture2D* texture);

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

        /**
         * @brief Returns the linear emissive factor.
         *
         * A `Vector3`, not a `Color` (plan_modern.md `MOD-1301`): `KHR_materials_emissive_strength`
         * multiplies the authored factor by an unbounded scale, so an 8-bit colour cannot hold
         * what an HDR pipeline is meant to receive. The base colour factor stays a `Color` because
         * glTF bounds it to [0,1], where 8 bits per channel is the authored precision.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 getEmissiveFactor() const;
        /** @brief Sets the linear emissive factor; values above 1 are meaningful. */
        void setEmissiveFactor(const Microsoft::Xna::Framework::Vector3& value);

        /** @brief Returns the normal map intensity scale (1.0 = full strength). */
        [[nodiscard]] float getNormalScale() const;
        /** @brief Sets the normal map intensity scale. */
        void setNormalScale(float value);

        /** @brief Returns the ambient occlusion strength in [0,1]. */
        [[nodiscard]] float getOcclusionStrength() const;
        /** @brief Sets the ambient occlusion strength. */
        void setOcclusionStrength(float value);

        /** @brief Returns the `KHR_materials_ior` index of refraction (1.5 by default). */
        [[nodiscard]] float getIor() const;
        /** @brief Sets the dielectric index of refraction. */
        void setIor(float value);

        /** @brief Returns the `KHR_materials_specular` strength factor (1 by default). */
        [[nodiscard]] float getSpecularFactor() const;
        /** @brief Sets the dielectric specular strength. */
        void setSpecularFactor(float value);

        /** @brief Returns the linear specular colour factor (white by default). */
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 getSpecularColorFactor() const;
        /** @brief Sets the linear specular colour factor. */
        void setSpecularColorFactor(const Microsoft::Xna::Framework::Vector3& value);

        // ── Coverage and sidedness ───────────────────────────────────────────

        /**
         * @brief Returns how this material's alpha is interpreted.
         *
         * `Microsoft::Xna::Framework::Graphics::AlphaModeEXT`, not an enumeration of this layer's
         * own (plan_modern.md `MOD-1302`): `PbrEffect` already carries that type, and two
         * independently declared alpha-mode enums would only ever meet in a conversion function
         * nobody could delete.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::AlphaModeEXT getAlphaMode() const;
        /** @brief Sets how this material's alpha is interpreted. */
        void setAlphaMode(Microsoft::Xna::Framework::Graphics::AlphaModeEXT value);

        /** @brief Returns the alpha cutoff, meaningful only in `AlphaModeEXT::Mask`. */
        [[nodiscard]] float getAlphaCutoff() const;
        /** @brief Sets the alpha cutoff used by `AlphaModeEXT::Mask`. */
        void setAlphaCutoff(float value);

        /**
         * @brief Returns whether both faces of the surface are drawn (glTF `doubleSided`).
         *
         * A property of the material rather than of the draw, which is why it lives here even
         * though it is applied to `RasterizerState` rather than to an effect: a leaf card is
         * double-sided wherever it appears, and a game should not have to remember that per draw.
         */
        [[nodiscard]] bool isDoubleSided() const;
        /** @brief Sets whether both faces of the surface are drawn. */
        void setDoubleSided(bool value);

        // ── Per-slot texture settings ────────────────────────────────────────

        /**
         * @brief Returns the packed vertex UV channel one texture slot samples (0 or 1).
         *
         * @param slot The slot to query.
         * @return The UV channel index; 0 by default.
         */
        [[nodiscard]] int getTextureCoordinateSet(PbrTextureSlot slot) const;

        /**
         * @brief Selects the packed vertex UV channel one texture slot samples.
         *
         * @param slot  The slot to configure.
         * @param value 0 or 1; other values are stored as given, exactly as `PbrEffect` does.
         */
        void setTextureCoordinateSet(PbrTextureSlot slot, int value);

        /**
         * @brief Returns one slot's `KHR_texture_transform`.
         *
         * @param slot The slot to query.
         * @return The transform; the identity by default.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::TextureTransformEXT
        getTextureTransform(PbrTextureSlot slot) const;

        /**
         * @brief Sets one slot's `KHR_texture_transform`.
         *
         * @param slot  The slot to configure.
         * @param value The transform to store.
         */
        void setTextureTransform(PbrTextureSlot slot,
                                 const Microsoft::Xna::Framework::Graphics::TextureTransformEXT& value);

        // ── Colour management ────────────────────────────────────────────────

        /** @brief Whether the albedo texture's samples are sRGB-encoded (true by default). */
        [[nodiscard]] bool isBaseColorTextureSrgb() const;
        /** @brief Sets whether the albedo texture's samples must be decoded from sRGB. */
        void setBaseColorTextureSrgb(bool value);

        /** @brief Whether the emissive texture's samples are sRGB-encoded (true by default). */
        [[nodiscard]] bool isEmissiveTextureSrgb() const;
        /** @brief Sets whether the emissive texture's samples must be decoded from sRGB. */
        void setEmissiveTextureSrgb(bool value);

        /** @brief Whether the specular-colour texture is sRGB-encoded (true by default). */
        [[nodiscard]] bool isSpecularColorTextureSrgb() const;
        /** @brief Sets whether the specular-colour texture must be decoded from sRGB. */
        void setSpecularColorTextureSrgb(bool value);

        /** @brief Whether the lit result is encoded back to sRGB (true by default). */
        [[nodiscard]] bool isOutputEncodedToSrgb() const;
        /** @brief Sets whether the lit result is encoded back to sRGB before the framebuffer. */
        void setOutputEncodedToSrgb(bool value);

        // ── Value semantics ──────────────────────────────────────────────────

        /**
         * @brief Compares every field, textures included (by pointer identity).
         *
         * @param other The material to compare with.
         * @return True when the two would produce the same draw.
         */
        [[nodiscard]] bool operator==(const PbrMaterial& other) const;

        /**
         * @brief The negation of @ref operator==.
         *
         * @param other The material to compare with.
         * @return True when the two differ in any field.
         */
        [[nodiscard]] bool operator!=(const PbrMaterial& other) const;

        /**
         * @brief A hash consistent with @ref operator==.
         *
         * @return A hash of every compared field; equal materials hash equally.
         */
        [[nodiscard]] std::size_t GetHashCode() const;

        /**
         * @brief A one-line summary of the material's distinguishing values.
         *
         * The format is
         * `{Albedo:{R:255 G:255 B:255 A:255} Metallic:0 Roughness:1 Emissive:{X:0 Y:0 Z:0} AlphaMode:Opaque DoubleSided:False Textures:0}`,
         * where `Textures` counts the bound slots. Deliberately not every field: a material has
         * more than thirty, and a line nobody can read is not a diagnostic.
         *
         * @return The summary.
         */
        [[nodiscard]] std::string ToString() const;

    private:
        using Tex2D = Microsoft::Xna::Framework::Graphics::Texture2D;
        using Color = Microsoft::Xna::Framework::Color;
        using Vector3 = Microsoft::Xna::Framework::Vector3;
        using AlphaModeEXT = Microsoft::Xna::Framework::Graphics::AlphaModeEXT;
        using TextureTransformEXT = Microsoft::Xna::Framework::Graphics::TextureTransformEXT;

        Tex2D* albedoTexture_            = nullptr;
        Tex2D* normalTexture_            = nullptr;
        Tex2D* metallicRoughnessTexture_ = nullptr;
        Tex2D* ambientOcclusionTexture_  = nullptr;
        Tex2D* emissiveTexture_          = nullptr;
        Tex2D* specularTexture_          = nullptr;
        Tex2D* specularColorTexture_     = nullptr;

        Color   albedoColor_       {255, 255, 255, 255};
        float   metallicFactor_     = 1.0f;
        float   roughnessFactor_    = 1.0f;
        Vector3 emissiveFactor_    {0.0f, 0.0f, 0.0f};
        float   normalScale_        = 1.0f;
        float   occlusionStrength_  = 1.0f;
        float   ior_                = 1.5f;
        float   specularFactor_     = 1.0f;
        Vector3 specularColorFactor_{1.0f, 1.0f, 1.0f};

        AlphaModeEXT alphaMode_ = AlphaModeEXT::Opaque;
        float        alphaCutoff_ = 0.5f;
        bool         doubleSided_ = false;

        std::array<int, kPbrTextureSlotCount> textureCoordinateSets_{};
        std::array<TextureTransformEXT, kPbrTextureSlotCount> textureTransforms_{};

        bool baseColorTextureSrgb_     = true;
        bool emissiveTextureSrgb_      = true;
        bool specularColorTextureSrgb_ = true;
        bool outputEncodedToSrgb_      = true;
    };

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
