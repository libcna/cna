// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/IEffectFog.hpp"
#include "Microsoft/Xna/Framework/Graphics/IEffectMatrices.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class EffectParameter;

    /**
     * @brief Built-in effect that supports alpha testing against a reference value.
     */
    class AlphaTestEffect : public Effect, public IEffectMatrices, public IEffectFog
    {
    public:
        /**
         * @brief Constructs an AlphaTestEffect for the given graphics device.
         *
         * @param device The graphics device that will own this effect.
         */
        explicit AlphaTestEffect(GraphicsDevice& device);

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
         * @brief Gets the texture applied to geometry rendered with this effect.
         *
         * @return Pointer to the current Texture2D, or nullptr if none.
         */
        [[nodiscard]] Texture2D* getTextureProperty() const;

        /**
         * @brief Sets the texture applied to geometry rendered with this effect.
         *
         * @param value Pointer to the Texture2D to use.
         */
        void setTextureProperty(Texture2D* value);

        /**
         * @brief Gives this effect shared ownership of a texture, keeping it alive for as long as
         *        the effect exists -- matching real XNA's GC-tracked `Effect.Texture` reference.
         *        See `BasicEffect::SetOwnedTexture()`'s own docs for why this exists alongside
         *        the non-owning `setTextureProperty(Texture2D*)`.
         *
         * @param texture The texture to take shared ownership of; also becomes the effect's
         *                current texture (as if passed to `setTextureProperty()`).
         */
        NOXNA void SetOwnedTexture(std::shared_ptr<Texture2D> texture);

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
         * @brief Gets the comparison function used for alpha testing.
         *
         * @return The current CompareFunction.
         */
        [[nodiscard]] CompareFunction getAlphaFunctionProperty() const;

        /**
         * @brief Sets the comparison function used for alpha testing.
         *
         * @param value The new CompareFunction.
         */
        void setAlphaFunctionProperty(CompareFunction value);

        /**
         * @brief Gets the reference alpha value used for alpha testing, in the range [0, 255].
         *
         * @return The reference alpha value.
         */
        [[nodiscard]] int getReferenceAlphaProperty() const;

        /**
         * @brief Sets the reference alpha value used for alpha testing, in the range [0, 255].
         *
         * @param value The new reference alpha value.
         */
        void setReferenceAlphaProperty(int value);

        /**
         * @brief Fills a GpuDrawParams struct with this effect's current render parameters.
         *
         * Populates texture, diffuse color, alpha, vertex-color flag, world matrix,
         * and the alphaTest vec4 (reference value, tolerance, pass/fail weights) so
         * the renderer fragment shader can perform per-pixel alpha testing via discard.
         *
         * @param params Output struct to populate.
         */
        NOXNA void FillGpuDrawParams(CNA::Internal::Renderers::GpuDrawParams& params) const override;

    protected:
        /**
         * @brief Applies shader parameters to the graphics device before drawing.
         */
        void OnApply() override;

    private:
        explicit AlphaTestEffect(const AlphaTestEffect& cloneSource);

        void CacheEffectParameters();

        Texture2D*       texture_            = nullptr;
        std::shared_ptr<Texture2D> ownedTexture_; // see SetOwnedTexture()
        EffectParameter* diffuseColorParam_ = nullptr;
        EffectParameter* alphaTestParam_    = nullptr;
        EffectParameter* fogColorParam_     = nullptr;
        EffectParameter* fogVectorParam_    = nullptr;
        EffectParameter* worldViewProjParam_ = nullptr;
        EffectParameter* shaderIndexParam_  = nullptr;

        bool    fogEnabled_          = false;
        bool    vertexColorEnabled_  = false;

        Matrix  world_      = Matrix::getIdentityProperty();
        Matrix  view_       = Matrix::getIdentityProperty();
        Matrix  projection_ = Matrix::getIdentityProperty();
        Matrix  worldView_;

        Vector3 diffuseColor_ = Vector3{1.0f, 1.0f, 1.0f};
        float   alpha_        = 1.0f;

        float   fogStart_ = 0.0f;
        float   fogEnd_   = 1.0f;

        CompareFunction alphaFunction_  = CompareFunction::Greater;
        int             referenceAlpha_ = 0;
        bool            isEqNe_         = false;

        int dirtyFlags_;
    };
}
