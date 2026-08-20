// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/FullscreenPass.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <memory>
#include <string>

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
    class ShaderEffect;
    class Texture2D;
}

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief A sky computed from the sun's position rather than sampled from a cube map.
     *
     * `Skybox` draws whatever image it was given. This draws what the air would look like: the blue
     * of a clear day is Rayleigh scattering, which sends short wavelengths sideways far more than
     * long ones, and the white glare around the sun is Mie scattering off larger particles, which
     * barely cares about wavelength at all. Moving the sun down turns the sky orange because the
     * light has further to travel and the blue has been scattered out of it before it arrives.
     *
     * That is what a parameterised sky buys over an image: **a time of day is a number, not an
     * asset.** What it costs is control -- a cube map can hold a photograph or a painting, and this
     * can only hold a sky.
     *
     * It is an alternative to `Skybox` rather than a replacement: the cube path is untouched, and a
     * game that wants an artist's sky keeps using it.
     */
    class AtmosphericSky
    {
    public:
        /**
         * @brief Creates the sky and compiles its shader.
         *
         * @param device The device to render with.
         */
        explicit AtmosphericSky(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);

        /** @brief Destroys the sky and its shader. */
        ~AtmosphericSky();

        AtmosphericSky(const AtmosphericSky&)            = delete;
        AtmosphericSky& operator=(const AtmosphericSky&) = delete;

        /** @brief Returns whether this renderer can draw the sky. */
        [[nodiscard]] bool isSupported() const;

        /**
         * @brief Draws the sky across the currently bound target.
         *
         * Call before the scene's opaque geometry, exactly as with `Skybox`.
         *
         * @param view       The camera's view matrix; its translation is ignored.
         * @param projection The camera's projection matrix.
         * @param width      Target width in pixels.
         * @param height     Target height in pixels.
         * @throws std::invalid_argument If either dimension is not positive.
         */
        void draw(const Microsoft::Xna::Framework::Matrix& view,
                  const Microsoft::Xna::Framework::Matrix& projection,
                  int width, int height);

        /** @brief Returns the direction the sunlight travels. */
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 getSunDirection() const;
        /**
         * @brief Sets the direction the sunlight travels.
         *
         * Straight down is midday; near horizontal is sunrise or sunset, and the sky reddens on its
         * own because the light path through the air has lengthened.
         *
         * @param value The direction; it is normalised, and a zero vector is ignored.
         */
        void setSunDirection(const Microsoft::Xna::Framework::Vector3& value);

        /** @brief Returns how hazy the air is. */
        [[nodiscard]] float getTurbidity() const;
        /**
         * @brief Sets how hazy the air is.
         *
         * 1 is the clearest air the model admits; larger values add the Mie scattering that turns
         * the sky white near the horizon and puts a glare around the sun.
         *
         * @param value The turbidity, clamped to [1, 10].
         */
        void setTurbidity(float value);

        /** @brief Returns the overall brightness multiplier. */
        [[nodiscard]] float getIntensity() const;
        /** @brief Sets the overall brightness multiplier. @param value The multiplier; negatives are ignored. */
        void setIntensity(float value);

        /**
         * @brief The scattering model as GLSL, for a pass that needs the same air on geometry.
         *
         * plan_modern.md `MOD-2141`. Emitted rather than duplicated, for the reason `MOD-2035`
         * charged this layer for: two copies of one model agree until somebody edits one of them,
         * and the symptom is a frame that looks slightly wrong with nothing to point at.
         *
         * The fragment declares `cnaSkyRadiance(vec3, vec3, float)`,
         * `cnaScatteringAlongPath(vec3, vec3, float, float)` — the same integral with the view path
         * supplied instead of assumed — `cnaAtmosphereTransmittance(float, float)`,
         * `cnaAerialAirMass(vec3, float, float)` and `cnaAerialPerspective(vec3, vec3, vec3, float,
         * float)`. The sky is the integral over the whole atmosphere; aerial perspective is the
         * same integral over however far the geometry is.
         *
         * @return A GLSL fragment declaring the model's functions.
         */
        [[nodiscard]] static std::string getModelGlsl();

        /**
         * @brief The sky's radiance along one direction, computed on the CPU.
         *
         * The same model the shader runs, exposed so it can be checked against the physics rather
         * than against a screenshot -- and so a game can ask what colour the sky is without
         * rendering one, which is what an ambient term or a fog colour wants to know.
         *
         * @param viewDirection Where to look, in world space; it is normalised.
         * @param sunDirection  The direction the sunlight travels; it is normalised.
         * @param turbidity     How hazy the air is.
         * @return The radiance, unbounded above.
         */
        [[nodiscard]] static Microsoft::Xna::Framework::Vector3 radiance(
            const Microsoft::Xna::Framework::Vector3& viewDirection,
            const Microsoft::Xna::Framework::Vector3& sunDirection, float turbidity);

    private:
        std::unique_ptr<FullscreenPass> fullscreen_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> effect_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> white_;

        Microsoft::Xna::Framework::Vector3 sunDirection_{0.0f, -1.0f, 0.0f};
        float turbidity_ = 2.5f;
        float intensity_ = 1.0f;
        bool  supported_ = false;
    };

/** @} */

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
