// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/IEffectFog.hpp"
#include "Microsoft/Xna/Framework/Graphics/IEffectMatrices.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class EffectParameter;

    /**
     * @brief Built-in effect that supports two-layer multitexturing with optional fog and vertex color.
     */
    class DualTextureEffect : public Effect, public IEffectMatrices, public IEffectFog
    {
    public:
        /**
         * @brief Constructs a DualTextureEffect for the given graphics device.
         *
         * @param device The graphics device that will own this effect.
         */
        explicit DualTextureEffect(GraphicsDevice& device);

        /**
         * @brief Creates a clone of this effect.
         *
         * @return Pointer to the cloned Effect.
         */
        [[nodiscard]] Effect* Clone() override;

        /** @brief Returns the fully qualified .NET type name. */
        NOXNA [[nodiscard]] const std::string& GetTypeName() const override;

        /**
         * @brief Gets the world matrix.
         *
         * @return The current world matrix.
         */
        [[nodiscard]] Matrix getWorldProperty() const override;

        /**
         * @brief Sets the world matrix.
         *
         * @param value The new world matrix.
         */
        void setWorldProperty(const Matrix& value) override;

        /**
         * @brief Gets the view matrix.
         *
         * @return The current view matrix.
         */
        [[nodiscard]] Matrix getViewProperty() const override;

        /**
         * @brief Sets the view matrix.
         *
         * @param value The new view matrix.
         */
        void setViewProperty(const Matrix& value) override;

        /**
         * @brief Gets the projection matrix.
         *
         * @return The current projection matrix.
         */
        [[nodiscard]] Matrix getProjectionProperty() const override;

        /**
         * @brief Sets the projection matrix.
         *
         * @param value The new projection matrix.
         */
        void setProjectionProperty(const Matrix& value) override;

        /**
         * @brief Gets the material diffuse color (range 0 to 1).
         *
         * @return The diffuse color as a Vector3.
         */
        [[nodiscard]] Vector3 getDiffuseColorProperty() const;

        /**
         * @brief Sets the material diffuse color (range 0 to 1).
         *
         * @param value The new diffuse color.
         */
        void setDiffuseColorProperty(const Vector3& value);

        /**
         * @brief Gets the material alpha (opacity), in the range [0, 1].
         *
         * @return The alpha value.
         */
        [[nodiscard]] float getAlphaProperty() const;

        /**
         * @brief Sets the material alpha (opacity), in the range [0, 1].
         *
         * @param value The new alpha value.
         */
        void setAlphaProperty(float value);

        // IEffectFog

        /**
         * @brief Gets the fog color.
         *
         * @return The fog color as a Vector3.
         */
        [[nodiscard]] Vector3 getFogColorProperty() const override;

        /**
         * @brief Sets the fog color.
         *
         * @param value The new fog color.
         */
        void setFogColorProperty(const Vector3& value) override;

        /**
         * @brief Gets whether distance fog is enabled.
         *
         * @return True if fog is enabled.
         */
        [[nodiscard]] bool getFogEnabledProperty() const override;

        /**
         * @brief Sets whether distance fog is enabled.
         *
         * @param value True to enable fog.
         */
        void setFogEnabledProperty(bool value) override;

        /**
         * @brief Gets the camera-space distance at which fog begins.
         *
         * @return The fog start distance.
         */
        [[nodiscard]] float getFogStartProperty() const override;

        /**
         * @brief Sets the camera-space distance at which fog begins.
         *
         * @param value The new fog start distance.
         */
        void setFogStartProperty(float value) override;

        /**
         * @brief Gets the camera-space distance at which fog reaches full density.
         *
         * @return The fog end distance.
         */
        [[nodiscard]] float getFogEndProperty() const override;

        /**
         * @brief Sets the camera-space distance at which fog reaches full density.
         *
         * @param value The new fog end distance.
         */
        void setFogEndProperty(float value) override;

        /**
         * @brief Gets the first texture applied to geometry rendered with this effect.
         *
         * @return Pointer to the first Texture2D, or nullptr if none.
         */
        [[nodiscard]] Texture2D* getTextureProperty() const;

        /**
         * @brief Sets the first texture applied to geometry rendered with this effect.
         *
         * @param value Pointer to the Texture2D to use as texture layer 0.
         */
        void setTextureProperty(Texture2D* value);

        /**
         * @brief Gets the second texture applied to geometry rendered with this effect.
         *
         * @return Pointer to the second Texture2D, or nullptr if none.
         */
        [[nodiscard]] Texture2D* getTexture2Property() const;

        /**
         * @brief Sets the second texture applied to geometry rendered with this effect.
         *
         * @param value Pointer to the Texture2D to use as texture layer 1.
         */
        void setTexture2Property(Texture2D* value);

        /**
         * @brief Gets whether per-vertex color is used for rendering.
         *
         * @return True if vertex color is enabled.
         */
        [[nodiscard]] bool getVertexColorEnabledProperty() const;

        /**
         * @brief Sets whether per-vertex color is used for rendering.
         *
         * @param value True to enable vertex color.
         */
        void setVertexColorEnabledProperty(bool value);

        /**
         * @brief Fills a GpuDrawParams struct with this effect's current render parameters.
         *
         * Populates both texture slots, diffuse color, vertex-color flag, world matrix,
         * and the dualTexture flag so the backend selects a two-sampler shader variant.
         *
         * @param params Output struct to populate.
         */
        NOXNA void FillGpuDrawParams(CNA::Internal::Backends::GpuDrawParams& params) const override;

    protected:
        /**
         * @brief Applies shader parameters to the graphics device before drawing.
         */
        void OnApply() override;

    private:
        explicit DualTextureEffect(const DualTextureEffect& cloneSource);

        void CacheEffectParameters();

        // Textures stored directly (Texture2D does not inherit from Texture in CNA)
        Texture2D* texture_  = nullptr;
        Texture2D* texture2_ = nullptr;

        EffectParameter* diffuseColorParam_  = nullptr;
        EffectParameter* fogColorParam_      = nullptr;
        EffectParameter* fogVectorParam_     = nullptr;
        EffectParameter* worldViewProjParam_ = nullptr;
        EffectParameter* shaderIndexParam_   = nullptr;

        bool    fogEnabled_         = false;
        bool    vertexColorEnabled_ = false;

        Matrix  world_      = Matrix::getIdentityProperty();
        Matrix  view_       = Matrix::getIdentityProperty();
        Matrix  projection_ = Matrix::getIdentityProperty();
        Matrix  worldView_;

        Vector3 diffuseColor_ = Vector3{1.0f, 1.0f, 1.0f};
        float   alpha_        = 1.0f;

        float   fogStart_ = 0.0f;
        float   fogEnd_   = 1.0f;

        int dirtyFlags_;
    };
}
