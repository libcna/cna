// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include <memory>
#include <vector>

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
    class Texture2D;
}

namespace CNA::Graphics {

    class ComputeShader;
    class RenderPipelineSettings;
    template<typename T> class StorageBufferT;

    /**
     * @brief Measures a frame's average luminance on the GPU and turns it into an exposure.
     *
     * plan_modern.md `MOD-1552`, revisiting `MOD-308`. Auto-exposure was out of scope for the
     * tonemapping phase for one concrete reason -- it needs a reduction over the whole frame, which
     * is a compute problem rather than a tonemapping one. Phase 15 gave CNA compute, so this is
     * that reduction and the small amount of control theory around it.
     *
     * The reduction samples a fixed grid over the scene, reduces each work group's samples in
     * shared memory, and returns one partial per group; the last, tiny sum happens on the CPU
     * because reading 64 floats costs less than a second dispatch to add them.
     *
     * **Log-average**, not a plain mean: a few very bright pixels would otherwise drag the whole
     * frame dark, which is exactly the artefact auto-exposure is blamed for.
     *
     * **Adaptation is deliberate and asymmetric.** An eye adjusts to darkness far more slowly than
     * to light, and a camera that snapped instantly to each frame's luminance would strobe on every
     * muzzle flash. @ref update moves the exposure toward its target exponentially, with separate
     * speeds for brightening and darkening.
     */
    class AutoExposureEXT
    {
    public:
        /**
         * @brief Creates the reducer and compiles its compute program.
         *
         * @param device The device to run on.
         * @throws System::NotSupportedException If the renderer has no compute support.
         * @throws std::runtime_error If the reduction shader does not compile.
         */
        explicit AutoExposureEXT(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);

        /** @brief Destroys the reducer and its GPU resources. */
        ~AutoExposureEXT();

        AutoExposureEXT(const AutoExposureEXT&)            = delete;
        AutoExposureEXT& operator=(const AutoExposureEXT&) = delete;

        /**
         * @brief Measures the log-average luminance of one texture.
         *
         * @param scene The frame to measure.
         * @return The log-average luminance, in the texture's own units.
         */
        [[nodiscard]] float measureAverageLuminance(
            Microsoft::Xna::Framework::Graphics::Texture2D& scene);

        /**
         * @brief Measures the frame and moves the exposure toward what it asks for.
         *
         * @param scene        The frame to measure.
         * @param deltaSeconds Time since the last call; zero or less snaps straight to the target.
         * @return The exposure to render the next frame with.
         */
        float update(Microsoft::Xna::Framework::Graphics::Texture2D& scene, float deltaSeconds);

        /**
         * @brief Writes the current exposure into a settings bag.
         *
         * The whole integration with the pipeline: `TonemapPass` already reads
         * `RenderPipelineSettings::getExposure()` every frame, so nothing else has to change.
         *
         * @param settings The settings to write to.
         */
        void applyTo(RenderPipelineSettings& settings) const;

        /** @brief Returns the exposure as it stands. */
        [[nodiscard]] float getExposure() const;

        /**
         * @brief Sets the exposure directly, skipping adaptation.
         *
         * @param value The exposure; must be positive.
         * @throws std::invalid_argument If @p value is not positive.
         */
        void setExposure(float value);

        /**
         * @brief Sets the middle-grey the exposure aims to put the frame's log-average at.
         *
         * @param value Photography's 0.18 by default; must be positive.
         * @throws std::invalid_argument If @p value is not positive.
         */
        void setKeyValue(float value);

        /** @brief Returns the target middle-grey. */
        [[nodiscard]] float getKeyValue() const;

        /**
         * @brief Sets how fast the exposure adapts, in units of e-foldings per second.
         *
         * The speeds are named for the **scene**, not for the exposure: a brighter scene needs a
         * lower exposure, so `brighteningPerSecond` is the speed at which the exposure comes
         * *down*. That is the eye's fast direction.
         *
         * @param brighteningPerSecond Speed when the scene has become brighter; must be positive.
         * @param darkeningPerSecond   Speed when it has become darker; must be positive.
         * @throws std::invalid_argument If either is not positive.
         */
        void setAdaptationSpeeds(float brighteningPerSecond, float darkeningPerSecond);

        /** @brief Returns the brightening adaptation speed. */
        [[nodiscard]] float getBrighteningSpeed() const;

        /** @brief Returns the darkening adaptation speed. */
        [[nodiscard]] float getDarkeningSpeed() const;

        /**
         * @brief Clamps the exposure the adaptation may reach.
         *
         * @param minimum Lowest allowed exposure; must be positive.
         * @param maximum Highest allowed exposure; must be at least @p minimum.
         * @throws std::invalid_argument If the range is not valid.
         */
        void setExposureRange(float minimum, float maximum);

    private:
        std::unique_ptr<ComputeShader> reducer_;
        std::unique_ptr<StorageBufferT<float>> partials_;
        float exposure_ = 1.0f;
        float keyValue_ = 0.18f;
        float brighteningSpeed_ = 3.0f;
        float darkeningSpeed_ = 1.0f;
        float minimumExposure_ = 0.01f;
        float maximumExposure_ = 64.0f;
    };

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
