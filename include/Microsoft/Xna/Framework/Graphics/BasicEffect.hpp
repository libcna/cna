#pragma once

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/DirectionalLight.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;

    /**
     * @brief Minimal subset of XNA's `BasicEffect` for colored vertices.
     *
     * Acts as a small bag of `World`/`View`/`Projection` matrices plus a
     * `VertexColorEnabled` flag. The CNA 3D pipeline reads these values
     * directly when a draw is issued and forwards them to the backend's
     * built-in colored shader.
     *
     * Inherits from `Effect`, so the standard XNA flow is also supported:
     * @code
     *   foreach (var pass in effect.CurrentTechnique.Passes) {
     *       pass.Apply();
     *       // ...
     *   }
     * @endcode
     *
     * @note Status: PARTIAL. Lighting, fog, textures and full multi-pass
     *       technique infrastructure are intentionally not provided. The
     *       single default technique exposes one pass which simply
     *       re-binds this effect on the device.
     */
    class BasicEffect : public Effect
    {
    public:
        explicit BasicEffect(GraphicsDevice& device);

        Matrix World      = Matrix::getIdentityProperty();
        Matrix View       = Matrix::getIdentityProperty();
        Matrix Projection = Matrix::getIdentityProperty();

        bool VertexColorEnabled = true;

        /// Per-vertex diffuse color multiplier.
        [[nodiscard]] Vector3 getDiffuseColorProperty() const;
        void setDiffuseColorProperty(const Vector3& value);

        /// Emissive color contribution.
        [[nodiscard]] Vector3 getEmissiveColorProperty() const;
        void setEmissiveColorProperty(const Vector3& value);

        /// Specular highlight color.
        [[nodiscard]] Vector3 getSpecularColorProperty() const;
        void setSpecularColorProperty(const Vector3& value);

        /// Sharpness of specular highlights.
        [[nodiscard]] float getSpecularPowerProperty() const;
        void setSpecularPowerProperty(float value);

        /// Ambient light color applied to all geometry.
        [[nodiscard]] Vector3 getAmbientLightColorProperty() const;
        void setAmbientLightColorProperty(const Vector3& value);

        /// Overall opacity multiplier.
        [[nodiscard]] float getAlphaProperty() const;
        void setAlphaProperty(float value);

        /// Enables or disables the per-pixel lighting model.
        [[nodiscard]] bool getLightingEnabledProperty() const;
        void setLightingEnabledProperty(bool value);

        /// Prefers per-pixel lighting over per-vertex when true.
        [[nodiscard]] bool getPreferPerPixelLightingProperty() const;
        void setPreferPerPixelLightingProperty(bool value);

        /// Enables or disables texture mapping.
        [[nodiscard]] bool getTextureEnabledProperty() const;
        void setTextureEnabledProperty(bool value);

        /// The texture applied to geometry.
        [[nodiscard]] Texture2D* getTextureProperty() const;
        void setTextureProperty(Texture2D* value);

        /// Enables or disables distance fog.
        [[nodiscard]] bool getFogEnabledProperty() const;
        void setFogEnabledProperty(bool value);

        /// Fog color.
        [[nodiscard]] Vector3 getFogColorProperty() const;
        void setFogColorProperty(const Vector3& value);

        /// Distance at which fogging begins.
        [[nodiscard]] float getFogStartProperty() const;
        void setFogStartProperty(float value);

        /// Distance at which geometry is fully obscured by fog.
        [[nodiscard]] float getFogEndProperty() const;
        void setFogEndProperty(float value);

        /// The first directional light source.
        DirectionalLight DirectionalLight0;

        /// The second directional light source.
        DirectionalLight DirectionalLight1;

        /// The third directional light source.
        DirectionalLight DirectionalLight2;

        /// Configures a white light from above and enables lighting.
        void EnableDefaultLighting();

    protected:
        void OnApply() override;

    private:
        Vector3 diffuseColor_      = Vector3{1.0f, 1.0f, 1.0f};
        Vector3 emissiveColor_     = Vector3::Zero;
        Vector3 specularColor_     = Vector3{1.0f, 1.0f, 1.0f};
        float   specularPower_     = 16.0f;
        Vector3 ambientLightColor_ = Vector3::Zero;
        float   alpha_             = 1.0f;
        bool    lightingEnabled_       = false;
        bool    preferPerPixelLighting_ = false;
        bool    textureEnabled_        = false;
        Texture2D* texture_            = nullptr;
        bool    fogEnabled_            = false;
        Vector3 fogColor_              = Vector3::Zero;
        float   fogStart_              = 0.0f;
        float   fogEnd_                = 1.0f;
    };
}
