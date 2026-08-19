// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/FullscreenPass.hpp"
#include "CNA/Graphics/PostProcessPass.hpp"
#include "CNA/Graphics/RenderTargetPool.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <memory>
#include <string>

namespace Microsoft::Xna::Framework::Graphics {
    class ShaderEffect;
    class Texture2D;
}

namespace CNA::Graphics {

    class ShadowMap;

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief Fog that a light can be blocked from reaching, so it holds the shape of a shadow.
     *
     * `HeightFogPass` answers "how much air is between here and there". This answers the harder
     * question: **how much of that air is lit**. Where the sun reaches the medium it glows; where a
     * wall shadows it, it stays dark -- which is what makes a beam through a window read as a beam
     * rather than as a general haze.
     *
     * **How it stores the volume, and why that is not a `Texture3D`.** The scattering is computed
     * into a *slice atlas*: a 2D render target holding the volume's depth slices side by side, the
     * same layout `ColorGradePass` reads a 3D lookup table from. CNA has a `Texture3D` a shader can
     * sample but no render target that writes into one, and filling a volume with compute needs
     * image stores that GL ES refuses for CNA's textures (`plan_modern.md` `MOD-1514`). An atlas
     * needs neither: one fullscreen draw fills every slice, because each atlas pixel knows which
     * froxel it is and can march to it on its own.
     *
     * The cost is the resolution: the atlas is `sliceCount` slices of `resolution` by `resolution`,
     * so 32 slices at 128 is a 4096 by 128 target. That is deliberate -- volumetric fog is low
     * frequency, and a screen-resolution volume would buy nothing an interpolation cannot.
     */
    class VolumetricFogPass final : public PostProcessPass
    {
    public:
        /**
         * @brief Creates the pass and compiles its shaders.
         *
         * @param device The device to render with.
         */
        explicit VolumetricFogPass(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);

        /** @brief Destroys the pass, its shaders and its atlas. */
        ~VolumetricFogPass() override;

        /** @brief Adds lit fog to @ref PostProcessContext::source. @param context The images, camera and size. */
        void apply(const PostProcessContext& context) override;
        /** @brief Returns `"VolumetricFog"`. */
        [[nodiscard]] const std::string& getName() const override;
        /** @brief Returns whether this renderer can run the pass. @param device The device queried. @return True when its shaders compiled and it can allocate the atlas. */
        [[nodiscard]] bool isSupported(
            Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) const override;

        /**
         * @brief Sets the light the fog is lit by, and the shadow it is blocked by.
         *
         * Without a shadow map the medium is lit everywhere the light points, which is a haze
         * rather than a beam -- so this is the one input that makes the pass worth its cost.
         *
         * @param shadowMap      The map to sample, or null for unshadowed fog.
         * @param lightDirection The direction the light travels, in world space.
         * @param lightColor     The light's colour and strength.
         */
        void setLight(ShadowMap* shadowMap,
                      const Microsoft::Xna::Framework::Vector3& lightDirection,
                      const Microsoft::Xna::Framework::Vector3& lightColor);

        /** @brief Returns the medium's density; zero disables the pass. */
        [[nodiscard]] float getDensity() const;
        /** @brief Sets the medium's density. @param value The density; negatives are ignored. */
        void setDensity(float value);

        /**
         * @brief Returns how forward-biased the scattering is, from -1 to 1.
         */
        [[nodiscard]] float getAnisotropy() const;
        /**
         * @brief Sets how forward-biased the scattering is.
         *
         * 0 scatters evenly in every direction. Positive values scatter forward, which is why fog
         * glows far more brightly when you look towards the sun than away from it -- the one cue
         * that separates real airlight from a grey wash.
         *
         * @param value The Henyey-Greenstein parameter, clamped to [-0.95, 0.95]; the ends are
         *              excluded because the phase function divides by zero at exactly +/-1.
         */
        void setAnisotropy(float value);

        /** @brief Returns how far into the scene the volume reaches, in world units. */
        [[nodiscard]] float getRange() const;
        /** @brief Sets how far into the scene the volume reaches. @param value The distance; values at or below zero are ignored. */
        void setRange(float value);

        /** @brief The number of depth slices the atlas holds. */
        static constexpr int kSliceCount = 32;
        /** @brief The width and height of one slice. */
        static constexpr int kSliceResolution = 96;

    private:
        std::unique_ptr<FullscreenPass> fullscreen_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> buildEffect_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> resolveEffect_;
        RenderTargetPool pool_;

        ShadowMap* shadowMap_ = nullptr;
        Microsoft::Xna::Framework::Vector3 lightDirection_{0.0f, -1.0f, 0.0f};
        Microsoft::Xna::Framework::Vector3 lightColor_{1.0f, 0.97f, 0.9f};
        float density_    = 0.0f;
        float anisotropy_ = 0.6f;
        float range_      = 60.0f;
    };

/** @} */

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
