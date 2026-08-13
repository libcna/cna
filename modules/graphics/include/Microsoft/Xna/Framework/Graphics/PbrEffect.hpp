// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaModeEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/DirectionalLight.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/IEffectFog.hpp"
#include "Microsoft/Xna/Framework/Graphics/IEffectLights.hpp"
#include "Microsoft/Xna/Framework/Graphics/IEffectMatrices.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class EffectParameter;

    /**
     * @brief Built-in effect implementing glTF 2.0's metallic-roughness PBR material model:
     * base color, normal, metallic-roughness, emissive, and occlusion maps, lit with the same
     * three-directional-light + ambient convention every other CNA stock effect uses.
     *
     * @note CNAEXT — not part of the XNA 4.0 API. Real XNA predates the PBR content pipeline this
     * describes entirely; there is no FNA/XNA equivalent to mirror. Uses the real glTF 2.0 spec's
     * own reference BRDF (GGX distribution + Smith-Schlick-GGX visibility + Schlick Fresnel, see
     * EasyGLRenderer::EnsurePbrProgram()'s own doc comment), not image-based lighting (a
     * separate, much larger feature). Every renderer except Software/Canvas/Ascii/Headless/
     * SDL_Renderer/FreeDirect has a real shader for this effect (plan_cnj.md CNB-58, CNB-103..109); those
     * remaining renderers accept a bound PbrEffect without erroring but currently render it as an
     * untextured/unlit fallback. WebGPU's shader covers the unskinned case only — see
     * SkinnedPbrEffect's own doc comment.
     */
    class PbrEffect : public Effect, public IEffectMatrices, public IEffectFog, public IEffectLights
    {
    public:
        /**
         * @brief Constructs a PbrEffect for the given graphics device.
         *
         * @param device The graphics device that will own this effect.
         */
        explicit PbrEffect(GraphicsDevice& device);

        /**
         * @brief Creates a clone of this effect.
         *
         * @return Pointer to the cloned Effect.
         */
        [[nodiscard]] Effect* Clone() override;

        /** @brief Returns the fully qualified .NET type name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

        // IEffectMatrices

        /** @brief Gets the world matrix. @return The current world matrix. */
        [[nodiscard]] Matrix getWorldProperty() const override;
        /** @brief Sets the world matrix. @param value The new world matrix. */
        void setWorldProperty(const Matrix& value) override;
        /** @brief Gets the view matrix. @return The current view matrix. */
        [[nodiscard]] Matrix getViewProperty() const override;
        /** @brief Sets the view matrix. @param value The new view matrix. */
        void setViewProperty(const Matrix& value) override;
        /** @brief Gets the projection matrix. @return The current projection matrix. */
        [[nodiscard]] Matrix getProjectionProperty() const override;
        /** @brief Sets the projection matrix. @param value The new projection matrix. */
        void setProjectionProperty(const Matrix& value) override;

        /**
         * @brief Gets the base color (albedo) factor, multiplied with the base color map's RGB.
         * @return The base color factor as a Vector3.
         */
        [[nodiscard]] Vector3 getDiffuseColorProperty() const;
        /**
         * @brief Sets the base color (albedo) factor.
         * @param value The new base color factor.
         */
        void setDiffuseColorProperty(const Vector3& value);

        /**
         * @brief Gets the material alpha (opacity), in the range [0, 1].
         * @return The alpha value.
         */
        [[nodiscard]] float getAlphaProperty() const;
        /**
         * @brief Sets the material alpha (opacity), in the range [0, 1].
         * @param value The new alpha value.
         */
        void setAlphaProperty(float value);

        // IEffectLights

        /** @brief Gets the ambient light color. @return The ambient light color. */
        [[nodiscard]] Vector3 getAmbientLightColorProperty() const override;
        /** @brief Sets the ambient light color. @param value The new ambient light color. */
        void setAmbientLightColorProperty(const Vector3& value) override;
        /** @brief Gets whether lighting is enabled (always true). @return Always true. */
        [[nodiscard]] bool getLightingEnabledProperty() const override;
        /**
         * @brief Sets whether lighting is enabled.
         *
         * CNAEXT divergence: real XNA's SkinnedEffect throws if set to false (lighting is always
         * on); PbrEffect mirrors that same constraint, since a physically-based material with no
         * lighting at all has no well-defined meaning.
         * @param value Must be true.
         */
        void setLightingEnabledProperty(bool value) override;
        /** @brief Gets the first directional light. @return Reference to DirectionalLight0. */
        [[nodiscard]] DirectionalLight& getDirectionalLight0Property() override;
        /** @brief Gets the second directional light. @return Reference to DirectionalLight1. */
        [[nodiscard]] DirectionalLight& getDirectionalLight1Property() override;
        /** @brief Gets the third directional light. @return Reference to DirectionalLight2. */
        [[nodiscard]] DirectionalLight& getDirectionalLight2Property() override;
        /** @brief Configures three-point lighting using standard key, fill, and back light directions. */
        void EnableDefaultLighting() override;

        /** @brief The first directional light source ("key" light after EnableDefaultLighting()). */
        DirectionalLight DirectionalLight0;
        /** @brief The second directional light source ("fill" light after EnableDefaultLighting()). */
        DirectionalLight DirectionalLight1;
        /** @brief The third directional light source ("back" light after EnableDefaultLighting()). */
        DirectionalLight DirectionalLight2;

        // IEffectFog

        /** @brief Gets the fog color. @return The fog color. */
        [[nodiscard]] Vector3 getFogColorProperty() const override;
        /** @brief Sets the fog color. @param value The new fog color. */
        void setFogColorProperty(const Vector3& value) override;
        /** @brief Gets whether distance fog is enabled. @return True if fog is enabled. */
        [[nodiscard]] bool getFogEnabledProperty() const override;
        /** @brief Sets whether distance fog is enabled. @param value True to enable fog. */
        void setFogEnabledProperty(bool value) override;
        /** @brief Gets the camera-space distance at which fog begins. @return The fog start distance. */
        [[nodiscard]] float getFogStartProperty() const override;
        /** @brief Sets the camera-space distance at which fog begins. @param value The new fog start distance. */
        void setFogStartProperty(float value) override;
        /** @brief Gets the camera-space distance at which fog reaches full density. @return The fog end distance. */
        [[nodiscard]] float getFogEndProperty() const override;
        /** @brief Sets the camera-space distance at which fog reaches full density. @param value The new fog end distance. */
        void setFogEndProperty(float value) override;

        /**
         * @brief Gets the base color (albedo) map.
         * @return Pointer to the Texture2D, or nullptr if none.
         */
        [[nodiscard]] Texture2D* getTextureProperty() const;
        /**
         * @brief Sets the base color (albedo) map.
         * @param value Pointer to the Texture2D to use.
         */
        void setTextureProperty(Texture2D* value);
        /**
         * @brief Gives this effect shared ownership of its base color texture. See
         *        `BasicEffect::SetOwnedTexture()`'s own docs for why this exists.
         * @param texture The texture to take shared ownership of.
         */
        CNAEXT void SetOwnedTexture(std::shared_ptr<Texture2D> texture);

        /** @brief Gets the tangent-space normal map. @return Pointer to the Texture2D, or nullptr. */
        CNAEXT [[nodiscard]] Texture2D* getNormalMapProperty() const;
        /** @brief Sets the tangent-space normal map. @param value Pointer to the Texture2D to use. */
        CNAEXT void setNormalMapProperty(Texture2D* value);
        /** @brief Gives this effect shared ownership of its normal map. @param texture The texture to take shared ownership of. */
        CNAEXT void SetOwnedNormalMap(std::shared_ptr<Texture2D> texture);

        /**
         * @brief Gets the metallic-roughness map (glTF's own packing: G=roughness, B=metallic).
         * @return Pointer to the Texture2D, or nullptr.
         */
        CNAEXT [[nodiscard]] Texture2D* getMetallicRoughnessMapProperty() const;
        /** @brief Sets the metallic-roughness map. @param value Pointer to the Texture2D to use. */
        CNAEXT void setMetallicRoughnessMapProperty(Texture2D* value);
        /** @brief Gives this effect shared ownership of its metallic-roughness map. @param texture The texture to take shared ownership of. */
        CNAEXT void SetOwnedMetallicRoughnessMap(std::shared_ptr<Texture2D> texture);

        /** @brief Gets the emissive map. @return Pointer to the Texture2D, or nullptr. */
        CNAEXT [[nodiscard]] Texture2D* getEmissiveMapProperty() const;
        /** @brief Sets the emissive map. @param value Pointer to the Texture2D to use. */
        CNAEXT void setEmissiveMapProperty(Texture2D* value);
        /** @brief Gives this effect shared ownership of its emissive map. @param texture The texture to take shared ownership of. */
        CNAEXT void SetOwnedEmissiveMap(std::shared_ptr<Texture2D> texture);

        /** @brief Gets the occlusion map (R channel: 1=fully lit .. 0=fully occluded). @return Pointer to the Texture2D, or nullptr. */
        CNAEXT [[nodiscard]] Texture2D* getOcclusionMapProperty() const;
        /** @brief Sets the occlusion map. @param value Pointer to the Texture2D to use. */
        CNAEXT void setOcclusionMapProperty(Texture2D* value);
        /** @brief Gives this effect shared ownership of its occlusion map. @param texture The texture to take shared ownership of. */
        CNAEXT void SetOwnedOcclusionMap(std::shared_ptr<Texture2D> texture);

        /** @brief Gets the metallic factor [0,1]. @return The metallic factor. */
        CNAEXT [[nodiscard]] float getMetallicFactorProperty() const;
        /** @brief Sets the metallic factor [0,1]. @param value The new metallic factor. */
        CNAEXT void setMetallicFactorProperty(float value);

        /** @brief Gets the roughness factor [0,1]. @return The roughness factor. */
        CNAEXT [[nodiscard]] float getRoughnessFactorProperty() const;
        /** @brief Sets the roughness factor [0,1]. @param value The new roughness factor. */
        CNAEXT void setRoughnessFactorProperty(float value);

        /** @brief Gets `KHR_materials_ior`'s index of refraction (default 1.5). @return The IOR. */
        CNAEXT [[nodiscard]] float getIorEXTProperty() const;
        /** @brief Sets the dielectric index of refraction. @param value The new IOR. */
        CNAEXT void setIorEXTProperty(float value);
        /** @brief Gets the specular reflection strength (default 1). @return The strength. */
        CNAEXT [[nodiscard]] float getSpecularFactorEXTProperty() const;
        /** @brief Sets the dielectric specular strength. @param value The new strength. */
        CNAEXT void setSpecularFactorEXTProperty(float value);
        /** @brief Gets the linear-RGB F0 colour factor (default white). @return The colour factor. */
        CNAEXT [[nodiscard]] Vector3 getSpecularColorFactorEXTProperty() const;
        /** @brief Sets the dielectric F0 colour factor. @param value The new colour factor. */
        CNAEXT void setSpecularColorFactorEXTProperty(const Vector3& value);

        /** @brief Gets the emissive factor, multiplied with the emissive map's RGB. @return The emissive factor. */
        CNAEXT [[nodiscard]] Vector3 getEmissiveFactorProperty() const;

        /**
         * @brief Whether the bound base-colour texture's samples are sRGB-encoded.
         *
         * @note CNAEXT — not part of the XNA 4.0 API (plan_gltf.md `GLTF-210`). glTF §3.9.2
         * declares `baseColorTexture` sRGB-encoded, so `true` is the default and is what an
         * imported glTF material wants. It is a property rather than a constant because this
         * effect is reachable from content that is not glTF, where a caller may bind a texture it
         * has already linearised.
         *
         * The base-colour **factor** (@ref getDiffuseColorProperty) is linear either way and is
         * never decoded: the two multiply, and decoding both would apply the transfer twice.
         *
         * @return True when the texture is sRGB-encoded and must be decoded before lighting.
         */
        /**
         * @brief How far the bound normal map perturbs the surface (glTF `normalTexture.scale`).
         *
         * @note CNAEXT — not part of the XNA 4.0 API (plan_gltf.md `GLTF-224`). Scales the sampled
         * tangent-space normal's x and y before the tangent basis is applied: 0 flattens the map to
         * the geometric normal, 1 is the map as authored, and values above 1 exaggerate it — glTF
         * puts no upper bound on it, so neither does this.
         *
         * @return The normal scale; 1 by default.
         */
        CNAEXT [[nodiscard]] float getNormalScaleEXTProperty() const;

        /**
         * @brief Sets how far the bound normal map perturbs the surface.
         * @param value The scale; 0 flattens the map, 1 is as authored.
         */
        CNAEXT void setNormalScaleEXTProperty(float value);

        /**
         * @brief How far the bound occlusion map darkens (glTF `occlusionTexture.strength`).
         *
         * @note CNAEXT — not part of the XNA 4.0 API (plan_gltf.md `GLTF-225`). Applied as
         * `1 + strength * (sampled - 1)`, the specification's own formula: at 0 the result is 1 —
         * no occlusion at all, whatever the map holds — and at 1 it is the map unchanged.
         *
         * @return The occlusion strength; 1 by default.
         */
        CNAEXT [[nodiscard]] float getOcclusionStrengthEXTProperty() const;

        /**
         * @brief Sets how far the bound occlusion map darkens.
         * @param value The strength; 0 disables occlusion, 1 applies the map as authored.
         */
        CNAEXT void setOcclusionStrengthEXTProperty(float value);

        /**
         * @brief Whether the bound base-colour texture's samples are sRGB-encoded.
         *
         * @note CNAEXT — not part of the XNA 4.0 API. glTF §3.9.2 assigns each material texture a
         * colour space, and the base-colour map is the one that is sRGB while the base-colour
         * *factor* beside it is linear. The shader decodes only the sample, so this flag has to
         * travel separately from the factor rather than being inferred from either.
         *
         * @return True when the sample must be decoded before lighting.
         */
        CNAEXT [[nodiscard]] bool getBaseColorTextureIsSrgbEXTProperty() const;

        /**
         * @brief Sets whether the bound base-colour texture's samples are sRGB-encoded.
         * @param value True for an sRGB-encoded texture (glTF's own rule), false for a linear one.
         */
        CNAEXT void setBaseColorTextureIsSrgbEXTProperty(bool value);

        /**
         * @brief Whether the bound emissive texture's samples are sRGB-encoded.
         *
         * @note CNAEXT — plan_gltf.md `GLTF-210`, on the same terms as
         * @ref getBaseColorTextureIsSrgbEXTProperty. The emissive **factor** is linear and is not
         * decoded, which matters because `KHR_materials_emissive_strength` can legitimately push
         * it above 1.
         *
         * @return True when the texture is sRGB-encoded and must be decoded before lighting.
         */
        CNAEXT [[nodiscard]] bool getEmissiveTextureIsSrgbEXTProperty() const;

        /**
         * @brief Sets whether the bound emissive texture's samples are sRGB-encoded.
         * @param value True for an sRGB-encoded texture (glTF's own rule), false for a linear one.
         */
        CNAEXT void setEmissiveTextureIsSrgbEXTProperty(bool value);

        /**
         * @brief Whether the lit result is encoded from linear back to sRGB for display.
         *
         * @note CNAEXT — not part of the XNA 4.0 API (plan_gltf.md `GLTF-212`). Unlike the two
         * decode flags, this is a genuine policy choice rather than a fact about a texture: an
         * application drawing into an sRGB render target, or doing its own tone mapping, must turn
         * it off or the transfer is applied twice. It defaults to `true` because the common case
         * is an ordinary UNORM back buffer shown directly.
         *
         * Alpha is never encoded — glTF §3.9.4 makes it coverage, not colour.
         *
         * @return True when the fragment's RGB is encoded to sRGB before it leaves the shader.
         */
        CNAEXT [[nodiscard]] bool getEncodeOutputToSrgbEXTProperty() const;

        /**
         * @brief Sets whether the lit result is encoded from linear back to sRGB for display.
         * @param value False when the render target or a later pass already applies the transfer.
         */
        CNAEXT void setEncodeOutputToSrgbEXTProperty(bool value);
        /** @brief Sets the emissive factor. @param value The new emissive factor. */
        CNAEXT void setEmissiveFactorProperty(const Vector3& value);

        /**
         * @brief Gets how this material's alpha channel is interpreted (glTF §3.9.4).
         *
         * @note CNAEXT — not part of the XNA 4.0 API. Defaults to `AlphaModeEXT::Opaque`, glTF's
         * own default, so an effect nobody configures behaves exactly as it did before this
         * existed.
         *
         * @return The alpha-coverage mode.
         */
        CNAEXT [[nodiscard]] AlphaModeEXT getAlphaModeEXTProperty() const;
        /** @brief Sets how this material's alpha channel is interpreted. @param value The mode. */
        CNAEXT void setAlphaModeEXTProperty(AlphaModeEXT value);

        /**
         * @brief Gets the alpha threshold a `Mask` material is cut at (glTF §3.9.4).
         *
         * @note CNAEXT — not part of the XNA 4.0 API. Defaults to `0.5`, glTF's own default.
         * Meaningful only when @ref getAlphaModeEXTProperty is `AlphaModeEXT::Mask`; it is carried
         * regardless, because a material that switches modes must not lose the threshold it
         * authored.
         *
         * @return The alpha cutoff.
         */
        CNAEXT [[nodiscard]] float getAlphaCutoffEXTProperty() const;
        /** @brief Sets the alpha threshold a `Mask` material is cut at. @param value The cutoff. */
        CNAEXT void setAlphaCutoffEXTProperty(float value);

        /**
         * @brief Gets whether this material's back faces are drawn (glTF §3.9.5).
         *
         * @note CNAEXT — not part of the XNA 4.0 API. Defaults to `false`, matching both glTF's
         * own default and XNA's `CullCounterClockwise`.
         *
         * **This is carried state, not applied state.** Culling is a `RasterizerState` the
         * application sets per draw, and having `Model::Draw` mutate device state as a side effect
         * would surprise every XNA caller (`docs/gltf-api-change-review.md` §1.4). An application
         * that wants glTF's sidedness honoured reads this and sets `RasterizerState::CullNone`
         * itself. This remains application-owned, like `GLTF-230`'s verified blend-state and draw-
         * ordering boundary.
         *
         * @return True when the material asks for both faces to be drawn.
         */
        CNAEXT [[nodiscard]] bool getDoubleSidedEXTProperty() const;
        /** @brief Sets whether this material's back faces are drawn. @param value True for both. */
        CNAEXT void setDoubleSidedEXTProperty(bool value);


        /**
         * @brief Fills a GpuDrawParams struct with this effect's current render parameters.
         *
         * Populates all 5 texture slots, base color/metallic/roughness/emissive factors,
         * lighting, world matrix, and the `pbr` flag so the renderer selects the
         * metallic-roughness BRDF shader variant.
         *
         * @param params Output struct to populate.
         */
        CNAEXT void FillGpuDrawParams(CNA::Internal::Renderers::GpuDrawParams& params) const override;

    protected:
        /** @brief Applies shader parameters to the graphics device before drawing. */
        void OnApply() override;

    private:
        explicit PbrEffect(const PbrEffect& cloneSource);

        void CacheEffectParameters();

        Texture2D* texture_                = nullptr;
        Texture2D* normalMap_              = nullptr;
        Texture2D* metallicRoughnessMap_   = nullptr;
        Texture2D* emissiveMap_            = nullptr;
        Texture2D* occlusionMap_           = nullptr;
        std::shared_ptr<Texture2D> ownedTexture_;
        std::shared_ptr<Texture2D> ownedNormalMap_;
        std::shared_ptr<Texture2D> ownedMetallicRoughnessMap_;
        std::shared_ptr<Texture2D> ownedEmissiveMap_;
        std::shared_ptr<Texture2D> ownedOcclusionMap_;

        EffectParameter* diffuseColorParam_  = nullptr;
        EffectParameter* fogColorParam_      = nullptr;
        EffectParameter* fogVectorParam_     = nullptr;
        EffectParameter* worldViewProjParam_ = nullptr;

        bool fogEnabled_ = false;

        float normalScale_       = 1.0f;
        float occlusionStrength_ = 1.0f;
        bool baseColorTextureIsSrgb_ = true;
        bool emissiveTextureIsSrgb_  = true;
        bool encodeOutputToSrgb_     = true;
        Matrix world_      = Matrix::getIdentityProperty();
        Matrix view_       = Matrix::getIdentityProperty();
        Matrix projection_ = Matrix::getIdentityProperty();
        Matrix worldView_;

        Vector3 diffuseColor_   = Vector3{1.0f, 1.0f, 1.0f};
        float   alpha_          = 1.0f;
        Vector3 ambientLightColor_ = Vector3::Zero;
        Vector3 emissiveFactor_    = Vector3::Zero;
        float   metallicFactor_    = 1.0f;
        float   roughnessFactor_   = 1.0f;
        float   iorEXT_                 = 1.5f;
        float   specularFactorEXT_      = 1.0f;
        Vector3 specularColorFactorEXT_ = Vector3{1.0f, 1.0f, 1.0f};

        float fogStart_ = 0.0f;
        float fogEnd_   = 1.0f;


        // plan_gltf.md GLTF-228/GLTF-229/GLTF-231: glTF's material-level alpha and sidedness state.
        AlphaModeEXT alphaMode_ = AlphaModeEXT::Opaque;
        float        alphaCutoff_ = 0.5f;
        bool         doubleSided_ = false;

        int dirtyFlags_;
    };
}
