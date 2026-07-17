// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <vector>

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
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
     * @brief PbrEffect's GPU-skinned sibling: the same real glTF metallic-roughness BRDF, applied
     * to a mesh with up to 72 bone transforms (SkinnedEffect's own MaxBones).
     *
     * @note NOXNA — not part of the XNA 4.0 API, same as PbrEffect itself (real XNA predates both
     * PBR and this combination entirely). A separate class rather than a boolean "skinned" flag
     * on PbrEffect, mirroring real XNA's own BasicEffect/SkinnedEffect precedent (distinct classes
     * per major shader variant, not one class with a mode flag). Requires the stride-68
     * VertexPositionNormalTangentTextureSkinned vertex layout. Bone transforms are not set by the
     * content loader -- exactly like SkinnedEffect, game code feeds AnimationPlayer::
     * GetSkinTransforms() into SetBoneTransforms() itself, every frame.
     */
    class SkinnedPbrEffect : public Effect, public IEffectMatrices, public IEffectFog, public IEffectLights
    {
    public:
        /** @brief Maximum number of bone transform matrices supported. */
        static const int MaxBones = 72;

        /**
         * @brief Constructs a SkinnedPbrEffect for the given graphics device.
         *
         * @param device The graphics device that will own this effect.
         */
        explicit SkinnedPbrEffect(GraphicsDevice& device);

        /**
         * @brief Creates a clone of this effect.
         *
         * @return Pointer to the cloned Effect.
         */
        [[nodiscard]] Effect* Clone() override;

        /** @brief Returns the fully qualified .NET type name. */
        NOXNA [[nodiscard]] const std::string& GetTypeName() const override;

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

        /** @brief Gets the base color (albedo) factor. @return The base color factor. */
        [[nodiscard]] Vector3 getDiffuseColorProperty() const;
        /** @brief Sets the base color (albedo) factor. @param value The new base color factor. */
        void setDiffuseColorProperty(const Vector3& value);

        /** @brief Gets the material alpha (opacity), in the range [0, 1]. @return The alpha value. */
        [[nodiscard]] float getAlphaProperty() const;
        /** @brief Sets the material alpha (opacity). @param value The new alpha value. */
        void setAlphaProperty(float value);

        // IEffectLights

        /** @brief Gets the ambient light color. @return The ambient light color. */
        [[nodiscard]] Vector3 getAmbientLightColorProperty() const override;
        /** @brief Sets the ambient light color. @param value The new ambient light color. */
        void setAmbientLightColorProperty(const Vector3& value) override;
        /** @brief Gets whether lighting is enabled (always true). @return Always true. */
        [[nodiscard]] bool getLightingEnabledProperty() const override;
        /**
         * @brief Sets whether lighting is enabled. Must be true (mirrors SkinnedEffect's own
         * identical constraint) -- throws if set to false.
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

        /** @brief Gets the base color (albedo) map. @return Pointer to the Texture2D, or nullptr. */
        [[nodiscard]] Texture2D* getTextureProperty() const;
        /** @brief Sets the base color (albedo) map. @param value Pointer to the Texture2D to use. */
        void setTextureProperty(Texture2D* value);
        /** @brief Gives this effect shared ownership of its base color texture. @param texture The texture to take shared ownership of. */
        NOXNA void SetOwnedTexture(std::shared_ptr<Texture2D> texture);

        /** @brief Gets the tangent-space normal map. @return Pointer to the Texture2D, or nullptr. */
        NOXNA [[nodiscard]] Texture2D* getNormalMapProperty() const;
        /** @brief Sets the tangent-space normal map. @param value Pointer to the Texture2D to use. */
        NOXNA void setNormalMapProperty(Texture2D* value);
        /** @brief Gives this effect shared ownership of its normal map. @param texture The texture to take shared ownership of. */
        NOXNA void SetOwnedNormalMap(std::shared_ptr<Texture2D> texture);

        /** @brief Gets the metallic-roughness map (glTF's own packing: G=roughness, B=metallic). @return Pointer to the Texture2D, or nullptr. */
        NOXNA [[nodiscard]] Texture2D* getMetallicRoughnessMapProperty() const;
        /** @brief Sets the metallic-roughness map. @param value Pointer to the Texture2D to use. */
        NOXNA void setMetallicRoughnessMapProperty(Texture2D* value);
        /** @brief Gives this effect shared ownership of its metallic-roughness map. @param texture The texture to take shared ownership of. */
        NOXNA void SetOwnedMetallicRoughnessMap(std::shared_ptr<Texture2D> texture);

        /** @brief Gets the emissive map. @return Pointer to the Texture2D, or nullptr. */
        NOXNA [[nodiscard]] Texture2D* getEmissiveMapProperty() const;
        /** @brief Sets the emissive map. @param value Pointer to the Texture2D to use. */
        NOXNA void setEmissiveMapProperty(Texture2D* value);
        /** @brief Gives this effect shared ownership of its emissive map. @param texture The texture to take shared ownership of. */
        NOXNA void SetOwnedEmissiveMap(std::shared_ptr<Texture2D> texture);

        /** @brief Gets the occlusion map (R channel: 1=fully lit .. 0=fully occluded). @return Pointer to the Texture2D, or nullptr. */
        NOXNA [[nodiscard]] Texture2D* getOcclusionMapProperty() const;
        /** @brief Sets the occlusion map. @param value Pointer to the Texture2D to use. */
        NOXNA void setOcclusionMapProperty(Texture2D* value);
        /** @brief Gives this effect shared ownership of its occlusion map. @param texture The texture to take shared ownership of. */
        NOXNA void SetOwnedOcclusionMap(std::shared_ptr<Texture2D> texture);

        /** @brief Gets the metallic factor [0,1]. @return The metallic factor. */
        NOXNA [[nodiscard]] float getMetallicFactorProperty() const;
        /** @brief Sets the metallic factor [0,1]. @param value The new metallic factor. */
        NOXNA void setMetallicFactorProperty(float value);

        /** @brief Gets the roughness factor [0,1]. @return The roughness factor. */
        NOXNA [[nodiscard]] float getRoughnessFactorProperty() const;
        /** @brief Sets the roughness factor [0,1]. @param value The new roughness factor. */
        NOXNA void setRoughnessFactorProperty(float value);

        /** @brief Gets the emissive factor, multiplied with the emissive map's RGB. @return The emissive factor. */
        NOXNA [[nodiscard]] Vector3 getEmissiveFactorProperty() const;
        /** @brief Sets the emissive factor. @param value The new emissive factor. */
        NOXNA void setEmissiveFactorProperty(const Vector3& value);

        /**
         * @brief Gets the number of bone weights used per vertex (1, 2, or 4).
         * @return The weights-per-vertex count.
         */
        [[nodiscard]] int getWeightsPerVertexProperty() const;
        /**
         * @brief Sets the number of bone weights used per vertex (1, 2, or 4).
         * @param value The new weights-per-vertex count.
         */
        void setWeightsPerVertexProperty(int value);

        /**
         * @brief Sets the array of bone transform matrices.
         * @param boneTransforms Vector of bone-space-to-world-space matrices; size must not exceed MaxBones.
         */
        void SetBoneTransforms(const std::vector<Matrix>& boneTransforms);

        /**
         * @brief Gets a subset of the current bone transform matrices.
         * @param count Number of bone matrices to retrieve.
         * @return Vector of the first @p count bone matrices.
         */
        [[nodiscard]] std::vector<Matrix> GetBoneTransforms(int count) const;

        /**
         * @brief Fills a GpuDrawParams struct with this effect's current render parameters.
         *
         * Populates all 5 texture slots, base color/metallic/roughness/emissive factors,
         * lighting, bone palette, and the `pbr`+`skinned` flags so the backend selects the
         * skinned metallic-roughness BRDF shader variant.
         *
         * @param params Output struct to populate.
         */
        NOXNA void FillGpuDrawParams(CNA::Internal::Backends::GpuDrawParams& params) const override;

    protected:
        /** @brief Applies shader parameters to the graphics device before drawing. */
        void OnApply() override;

    private:
        explicit SkinnedPbrEffect(const SkinnedPbrEffect& cloneSource);

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
        EffectParameter* bonesParam_         = nullptr;

        bool fogEnabled_ = false;
        int  weightsPerVertex_ = 4;

        Matrix world_      = Matrix::getIdentityProperty();
        Matrix view_       = Matrix::getIdentityProperty();
        Matrix projection_ = Matrix::getIdentityProperty();
        Matrix worldView_;

        Vector3 diffuseColor_      = Vector3{1.0f, 1.0f, 1.0f};
        float   alpha_             = 1.0f;
        Vector3 ambientLightColor_ = Vector3::Zero;
        Vector3 emissiveFactor_    = Vector3::Zero;
        float   metallicFactor_    = 1.0f;
        float   roughnessFactor_   = 1.0f;

        float fogStart_ = 0.0f;
        float fogEnd_   = 1.0f;

        int dirtyFlags_;
    };
}
